"""
Ensemble Approach Using k-Partitioned Isolation Forests (k-PIF)
for Stock Market Manipulation Detection.

This script implements the methodology proposed by Núñez Delafuente et al. (2024),
adapted for Market-By-Order (MBO) features.

Usage:
    # 1. Train and save a "Tier 1" model
    python scripts/isolation_forest.py --train features/muln_baseline.csv --tier 1 --save-model models/muln_t1.joblib

    # 2. "Try it" on your manipulated day
    python scripts/isolation_forest.py --test features/muln_oct25.csv --load-model models/muln_t1.joblib
"""

import argparse
import sys
import os
import random
import joblib

import numpy as np
import pandas as pd
from sklearn.ensemble import IsolationForest
from sklearn.preprocessing import StandardScaler

# --- Feature Tiers ---
# Additive progression T1→T4, then T5 as SHAP-informed optimal subset.
TIERS = {
    1: ["relative_size", "induced_imbalance"],  # Baiting signature only
    2: ["relative_size", "induced_imbalance", "delta_t"],  # + Cancellation timing
    3: [
        "relative_size",
        "induced_imbalance",
        "delta_t",
        "price_distance_ticks",
        "volume_ahead",
    ],  # + Order book context
    4: [
        "relative_size",
        "induced_imbalance",
        "delta_t",
        "price_distance_ticks",
        "volume_ahead",
        "spread_bps",
    ],  # Full model
    5: [
        "relative_size",
        "induced_imbalance",
        "price_distance_ticks",
    ],  # SHAP-informed: cross-event consistent, no timing/spread confounds
}


def load_and_preprocess(
    path: str, features: list, scaler=None
) -> tuple[pd.DataFrame, StandardScaler]:
    """Loads CSV and applies log-transform + scaling."""
    if not os.path.exists(path):
        sys.exit(f"Error: File not found: {path}")

    df = pd.read_csv(path)

    # 1. Convert timestamps to Eastern Time for filtering
    df["dt_utc"] = pd.to_datetime(df["ts_recv"], unit="ns", utc=True)
    df["dt_et"] = df["dt_utc"].dt.tz_convert("US/Eastern")

    # 2. Filter for Continuous Trading (09:45 - 15:55 ET)
    # This excludes the volatile open/close spikes that mask spoofing
    df = df.set_index("dt_et").between_time("09:45", "15:55").reset_index()

    # 3. Filter for pure cancellations (candidate set for spoofing)
    if "cancel_type" in df.columns:
        df = df[df["cancel_type"] == 0].copy()

    missing = set(features) - set(df.columns)
    if missing:
        sys.exit(f"CSV missing columns: {missing}")

    # Log transform highly skewed features
    for col in ("delta_t", "volume_ahead", "relative_size"):
        if col in features:
            df[col] = np.log1p(df[col])

    if scaler is None:
        scaler = StandardScaler()
        df[features] = scaler.fit_transform(df[features])
    else:
        df[features] = scaler.transform(df[features])

    return df, scaler


class KPIFEnsemble:
    """k-Partitioned Isolation Forest Ensemble."""

    def __init__(self, features, k=3, contamination=0.01, random_state=42):
        self.features = features
        self.k = min(k, len(features))  # Cannot have more partitions than features
        self.contamination = contamination
        self.random_state = random_state
        self.models = []
        self.feature_subsets = []

    def _get_feature_subsets(self, features):
        """Randomly partitions features into k subsets."""
        random.seed(self.random_state)
        shuffled = list(features)
        random.shuffle(shuffled)

        subsets = np.array_split(shuffled, self.k)
        return [list(s) for s in subsets if len(s) > 0]

    def fit(self, X):
        self.feature_subsets = self._get_feature_subsets(self.features)
        self.models = []

        print(f"Training k-PIF ensemble with k={len(self.feature_subsets)} ...")
        for i, subset in enumerate(self.feature_subsets):
            print(f"  Model {i + 1}: Features {subset}")
            model = IsolationForest(
                contamination=self.contamination,
                random_state=self.random_state + i,
                n_jobs=-1,
            )
            model.fit(X[subset])
            self.models.append(model)

    def decision_function(self, X):
        """Aggregate anomaly scores (lower means more anomalous)."""
        scores = []
        for i, subset in enumerate(self.feature_subsets):
            scores.append(self.models[i].decision_function(X[subset]))
        return np.mean(scores, axis=0)


def parse_args():
    p = argparse.ArgumentParser(description="k-PIF Spoofing Detector")
    # Action arguments
    p.add_argument("--train", nargs="+", help="Path to baseline CSV(s) for training")
    p.add_argument("--test", help="Path to target CSV for inference")

    # Model persistence
    p.add_argument(
        "--save-model", help="Path to save the trained ensemble and scaler (.joblib)"
    )
    p.add_argument(
        "--load-model", help="Path to load a saved ensemble and scaler (.joblib)"
    )

    # Configuration
    p.add_argument(
        "--tier",
        type=int,
        default=1,
        choices=[1, 2, 3, 4, 5],
        help="Feature tier (default 1)",
    )
    p.add_argument("--k", type=int, default=3, help="Number of partitions (default 3)")
    p.add_argument(
        "--contamination",
        type=float,
        default=0.01,
        help="Expected proportion of anomalies in training set",
    )
    p.add_argument(
        "--threshold-pct",
        type=float,
        default=5.0,
        help="Percentile of lowest scores to flag as anomalous",
    )
    p.add_argument("--output", default="features/anomaly_scores_kpif.csv")
    p.add_argument(
        "--info",
        action="store_true",
        help="Display info about the loaded model and exit",
    )
    return p.parse_args()


def main():
    args = parse_args()
    features = TIERS[args.tier]

    ensemble = None
    scaler = None

    # --- Case 1: Load Existing Model ---
    if args.load_model:
        checkpoint = joblib.load(args.load_model)
        ensemble = checkpoint["ensemble"]
        scaler = checkpoint["scaler"]
        features = ensemble.features  # Override tier with saved features

        if args.info:
            print(f"\n--- Model Info: {args.load_model} ---")
            print(f"Features:   {features}")
            print(f"Partitions: k={ensemble.k}")
            print(f"Subsets:    {ensemble.feature_subsets}")
            return  # Exit early after printing info

        print(f"Loaded model using features: {features}")

    # --- Case 2: Train New Model ---
    elif args.train:
        print(f"Loading training baseline(s): {args.train} (Tier {args.tier})")
        train_dfs = []
        for path in args.train:
            df, s = load_and_preprocess(path, features, scaler=None)
            if scaler is None:
                scaler = s
            train_dfs.append(df)

        train_df = pd.concat(train_dfs, ignore_index=True)
        print(
            f"  {len(train_df):,} records used for training (after filtering for pure cancels)"
        )

        ensemble = KPIFEnsemble(features, k=args.k, contamination=args.contamination)
        ensemble.fit(train_df)

        if args.save_model:
            os.makedirs(os.path.dirname(args.save_model), exist_ok=True)
            joblib.dump({"ensemble": ensemble, "scaler": scaler}, args.save_model)
            print(f"Model and scaler saved to {args.save_model}")

    else:
        sys.exit("Error: Must provide either --train or --load-model")

    # --- Inference ---
    if args.test:
        print(f"\nPerforming inference on: {args.test}")
        test_df, _ = load_and_preprocess(args.test, features, scaler=scaler)

        print("Computing ensemble anomaly scores...")
        scores = ensemble.decision_function(test_df)

        threshold = np.percentile(scores, args.threshold_pct)
        anomalies = (scores <= threshold).astype(int)

        print(f"\nResults:")
        print(f"  Threshold ({args.threshold_pct}th percentile): {threshold:.6f}")
        print(
            f"  Flagged: {anomalies.sum():,} / {len(anomalies):,} ({100 * anomalies.sum() / len(anomalies):.2f}%)"
        )

        output_df = test_df.copy()
        output_df["anomaly_score"] = scores
        output_df["anomaly"] = anomalies

        output_df.to_csv(args.output, index=False)
        print(f"\nScores written to {args.output}")


if __name__ == "__main__":
    main()
