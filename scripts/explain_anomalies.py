"""
Explain Anomaly Drivers using SHAP (SHapley Additive exPlanations).

Single-window mode: produces a SHAP summary plot for one time window.
Multi-window mode:  compares mean |SHAP| across several windows side-by-side.

Usage (single):
    python scripts/explain_anomalies.py \
      --model output/models/MULN_20221025_T4.joblib \
      --data output/features/MULN_20221025_SCORES_T4.csv \
      --time "14:28" \
      --output output/plots/MULN_20221025_explanation_1428.png

Usage (compare):
    python scripts/explain_anomalies.py \
      --model output/models/MULN_20221025_T4.joblib \
      --data output/features/MULN_20221025_SCORES_T4.csv \
      --time "14:28" "12:28" "12:34" "12:50" \
      --labels "TP 14:28" "FP? 12:28" "FP? 12:34" "FP? 12:50" \
      --output output/plots/MULN_20221025_explanation_compare.png
"""

import argparse
import joblib
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import shap
import sys
from datetime import datetime

from sklearn.ensemble import IsolationForest
from sklearn.preprocessing import StandardScaler


class KPIFEnsemble:
    """k-Partitioned Isolation Forest Ensemble."""

    def __init__(self, features, k=3, contamination=0.01, random_state=42):
        self.features = features
        self.k = min(k, len(features))
        self.contamination = contamination
        self.random_state = random_state
        self.models = []
        self.feature_subsets = []

    def _get_feature_subsets(self, features):
        import random
        random.seed(self.random_state)
        shuffled = list(features)
        random.shuffle(shuffled)
        subsets = np.array_split(shuffled, self.k)
        return [list(s) for s in subsets if len(s) > 0]

    def fit(self, X):
        self.feature_subsets = self._get_feature_subsets(self.features)
        self.models = []
        for i, subset in enumerate(self.feature_subsets):
            model = IsolationForest(
                contamination=self.contamination,
                random_state=self.random_state + i,
                n_jobs=-1,
            )
            model.fit(X[subset])
            self.models.append(model)

    def decision_function(self, X):
        scores = []
        for i, subset in enumerate(self.feature_subsets):
            scores.append(self.models[i].decision_function(X[subset]))
        return np.mean(scores, axis=0)


def parse_args():
    p = argparse.ArgumentParser(description="Explain Anomaly Drivers")
    p.add_argument("--model", required=True, help="Path to saved .joblib model")
    p.add_argument("--data", required=True, help="Path to scored CSV features")
    p.add_argument("--time", required=True, nargs="+", help="One or more HH:MM windows (ET)")
    p.add_argument("--labels", nargs="+", help="Labels for each window (default: the time strings)")
    p.add_argument("--output", default="output/plots/explanation.png", help="Path to save the plot")
    return p.parse_args()


def get_window(df, time_str):
    target = datetime.strptime(time_str, "%H:%M").time()
    mask = df['dt_et'].dt.time.apply(lambda t: t.replace(second=0, microsecond=0)) == target
    return df[mask]


def make_predictor(ensemble, features):
    def model_predict(X):
        scores = []
        for i, subset in enumerate(ensemble.feature_subsets):
            idx = [features.index(f) for f in subset]
            scores.append(ensemble.models[i].decision_function(X[:, idx]))
        return -np.mean(scores, axis=0)
    return model_predict


def compute_shap(model_predict, background_data, explain_data):
    explainer = shap.KernelExplainer(model_predict, background_data)
    return explainer.shap_values(explain_data)


def plot_single(shap_values, explain_df, time_str, output_path):
    plt.figure(figsize=(10, 6))
    shap.summary_plot(shap_values, explain_df, show=False)
    plt.title(
        f"Feature Importance for Anomalies at {time_str} ET\n"
        "(Positive = Pushes toward Anomaly)",
        fontsize=12,
    )
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    print(f"Saved: {output_path}")


def plot_comparison(shap_per_window, feature_names, labels, output_path):
    """Heatmap: features as rows (sorted by TP), windows as columns."""
    mean_abs = np.array([np.abs(sv).mean(axis=0) for sv in shap_per_window])  # (n_windows, n_features)

    # Sort features by TP window (first column) descending
    order = np.argsort(mean_abs[0])[::-1]
    mean_abs = mean_abs[:, order]                          # (n_windows, n_features)
    sorted_features = [feature_names[i] for i in order]

    # Normalise each feature to [0,1] across windows so colour reflects relative dominance
    row_max = mean_abs.max(axis=0, keepdims=True)
    row_max[row_max == 0] = 1
    norm = mean_abs / row_max                              # (n_windows, n_features)

    fig, ax = plt.subplots(figsize=(max(7, len(sorted_features) * 0.9), max(3, len(labels) * 0.9 + 1.5)))

    im = ax.imshow(norm, aspect="auto", cmap="YlOrRd", vmin=0, vmax=1)

    # Annotate cells with raw mean |SHAP|
    for r in range(len(labels)):
        for c in range(len(sorted_features)):
            ax.text(c, r, f"{mean_abs[r, c]:.3f}", ha="center", va="center",
                    fontsize=8, color="black" if norm[r, c] < 0.7 else "white")

    ax.set_xticks(range(len(sorted_features)))
    ax.set_xticklabels(sorted_features, rotation=35, ha="right", fontsize=10)
    ax.set_yticks(range(len(labels)))
    ax.set_yticklabels(labels, fontsize=10)

    # Highlight TP row
    ax.add_patch(plt.Rectangle((-0.5, -0.5), len(sorted_features), 1,
                                fill=False, edgecolor="#d62728", linewidth=2.5))

    cbar = fig.colorbar(im, ax=ax, fraction=0.03, pad=0.02)
    cbar.set_label("Relative importance\n(1 = max across windows)", fontsize=8)

    ax.set_title("SHAP Feature Importance — per-window comparison\n"
                 "(features sorted by TP; values are mean |SHAP|)", fontsize=11)
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    print(f"Saved: {output_path}")


def main():
    args = parse_args()
    labels = args.labels if args.labels else args.time
    if len(labels) != len(args.time):
        sys.exit("Error: --labels must have the same number of entries as --time.")

    print(f"Loading model: {args.model}")
    checkpoint = joblib.load(args.model)
    ensemble = checkpoint['ensemble']
    features = ensemble.features
    model_predict = make_predictor(ensemble, features)

    df = pd.read_csv(args.data)
    df['dt_et'] = pd.to_datetime(df['ts_recv'], unit='ns', utc=True).dt.tz_convert('US/Eastern')

    background_data = (
        df[df['anomaly'] == 0]
        .sample(min(100, (df['anomaly'] == 0).sum()), random_state=42)[features]
        .values
    )

    shap_per_window = []
    explain_dfs = []

    for time_str in args.time:
        window = get_window(df, time_str)
        if len(window) == 0:
            sys.exit(f"Error: No data found at {time_str} ET.")
        print(f"{time_str}: {len(window)} orders total", end="")

        flagged = window[window['anomaly'] == 1]
        if len(flagged) == 0:
            print(f"  (no flagged anomalies — using all {len(window)} orders)")
            flagged = window
        else:
            print(f", {len(flagged)} anomalies")

        explain_dfs.append(flagged)

    print("Calculating SHAP values (this may take a while)...")
    for flagged in explain_dfs:
        sv = compute_shap(model_predict, background_data, flagged[features].values)
        shap_per_window.append(sv)

    if len(args.time) == 1:
        plot_single(shap_per_window[0], explain_dfs[0][features], args.time[0], args.output)
    else:
        plot_comparison(shap_per_window, features, labels, args.output)


if __name__ == "__main__":
    main()
