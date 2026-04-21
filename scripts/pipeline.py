"""
Full spoofing detection pipeline for MULN (Case 1:23-cv-07613).

Replaces run_all_tiers.sh. Covers all four court-confirmed events:
  extract → train → score → quantify → SHAP → grid plots → Excel workbook

Usage:
    python scripts/pipeline.py [--reextract]

Flags:
    --reextract   Re-run C++ feature extraction even if CSVs already exist.
"""

import argparse
import csv
import os
import subprocess
import sys

import matplotlib.pyplot as plt
import matplotlib.image as mpimg

sys.path.insert(0, os.path.dirname(__file__))
from isolation_forest import train, score
from quantify_anomalies import quantify
from explain_anomalies import explain_single
from build_comparison_sheet import build_sheet

# ---------------------------------------------------------------------------
# Event registry — spoofing windows from court documents (Case 1:23-cv-07613)
# ---------------------------------------------------------------------------
EVENTS = [
    {
        "compact": "20221025",
        "iso": "2022-10-25",
        "win_start": "14:26:10",
        "win_end": "14:28:10",
    },  # Para 112
    {
        "compact": "20221215",
        "iso": "2022-12-15",
        "win_start": "13:25:30",
        "win_end": "13:27:30",
    },  # Para 124
    {
        "compact": "20230606",
        "iso": "2023-06-06",
        "win_start": "15:50:59",
        "win_end": "15:52:59",
    },  # Para 118
    {
        "compact": "20230817",
        "iso": "2023-08-17",
        "win_start": "15:53:27",
        "win_end": "15:55:27",
    },  # Para 130
]

TIER_FEATURES = {
    1: "Baiting signature (rel_size, imbalance)",
    2: "+ Cancellation timing (+ delta_t)",
    3: "+ Order book context (+ dist, vol_ahead)",
    4: "Full model (+ spread_bps)",
    5: "SHAP-informed (rel_size, imbalance, dist)",
}

BINARY = "./build/thesis"
COMPARISON_CSV = "output/comparison.csv"
COMPARISON_XLSX = "output/comparison.xlsx"


def extract(binary, dbn_path, csv_path, label, reextract):
    """If user requests so using --reextract flag."""
    if not reextract and os.path.exists(csv_path):
        print(f"  Skipping extraction (CSV exists): {csv_path}")
        return
    if not os.path.exists(dbn_path):
        sys.exit(f"  Error: {label} DBN not found: {dbn_path}")
    print(f"  Extracting: {dbn_path} → {csv_path}")
    subprocess.run(
        [
            binary,
            "extract-features",
            dbn_path,
            "--ticker",
            "MULN",
            "--output",
            csv_path,
        ],
        check=True,
    )


def build_density_grids():
    events = [e["compact"] for e in EVENTS]
    labels = ["Oct 25 2022", "Dec 15 2022", "Jun 06 2023", "Aug 17 2023"]

    for tier in range(1, 5):
        fig, axes = plt.subplots(2, 2, figsize=(22, 10))
        fig.suptitle(
            f"Anomaly Density — Tier {tier} (all confirmed events)",
            fontsize=15,
            fontweight="bold",
        )
        for ax, date, label in zip(axes.flat, events, labels):
            path = f"output/MULN_{date}/DENSITY_T{tier}.png"
            try:
                ax.imshow(mpimg.imread(path))
            except FileNotFoundError:
                ax.text(
                    0.5,
                    0.5,
                    f"Missing:\n{path}",
                    ha="center",
                    va="center",
                    transform=ax.transAxes,
                    color="red",
                )
            ax.set_title(label, fontsize=11)
            ax.axis("off")
        plt.tight_layout()
        out = f"output/GRID_DENSITY_T{tier}.png"
        plt.savefig(out, dpi=150)
        plt.close()
        print(f"  Saved {out}")


def build_shap_grid():
    events = [e["compact"] for e in EVENTS]
    labels = ["Oct 25 2022", "Dec 15 2022", "Jun 06 2023", "Aug 17 2023"]

    fig, axes = plt.subplots(2, 2, figsize=(24, 12))
    fig.suptitle(
        "SHAP Feature Importance — T4 (full model) across all confirmed spoofing events",
        fontsize=14,
        fontweight="bold",
    )
    for ax, date, label in zip(axes.flat, events, labels):
        path = f"output/MULN_{date}/SHAP_T4.png"
        try:
            ax.imshow(mpimg.imread(path))
        except FileNotFoundError:
            ax.text(
                0.5,
                0.5,
                f"Missing:\n{path}",
                ha="center",
                va="center",
                transform=ax.transAxes,
                color="red",
            )
        ax.set_title(label, fontsize=11)
        ax.axis("off")
    plt.tight_layout()
    plt.savefig("output/GRID_SHAP_T4.png", dpi=150)
    plt.close()
    print("  Saved output/GRID_SHAP_T4.png")


def main():
    p = argparse.ArgumentParser(description="MULN spoofing detection pipeline")
    p.add_argument(
        "--reextract",
        action="store_true",
        help="Re-run C++ feature extraction even if CSVs already exist",
    )
    args = p.parse_args()

    if not os.path.isfile(BINARY) or not os.access(BINARY, os.X_OK):
        sys.exit(
            f"Error: binary not found at {BINARY} — run cmake --build build first."
        )

    os.makedirs("output", exist_ok=True)

    with open(COMPARISON_CSV, "w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(
            [
                "Date",
                "Tier",
                "Features",
                "Total_Window_Anomalies",
                "Avg_In_Window",
                "Avg_Outside_Window",
                "SNR",
                "T_Stat",
                "P_Value",
            ]
        )

        for event in EVENTS:
            compact = event["compact"]
            iso = event["iso"]
            win_start = event["win_start"]
            win_end = event["win_end"]
            event_dir = f"output/MULN_{compact}"
            os.makedirs(event_dir, exist_ok=True)

            baseline_dbn = f"data/BASELINE/MULN_{compact}_BASELINE.dbn.zst"
            event_dbn = f"data/MANIPULATION_WINDOWS/MULN_{compact}.dbn.zst"
            baseline_features = f"{event_dir}/BASELINE_FEATURES.csv"
            event_features = f"{event_dir}/FEATURES.csv"

            print()
            print("=" * 70)
            print(f"=== MULN {iso}  (window {win_start}–{win_end}) ===")
            print("=" * 70)

            print("\n--- Step 0: Feature Extraction ---")
            extract(BINARY, baseline_dbn, baseline_features, "baseline", args.reextract)
            extract(BINARY, event_dbn, event_features, "event", args.reextract)

            for tier in range(1, 6):
                print(f"\n--- Tier {tier}: {TIER_FEATURES[tier]} ---")
                model_path = f"{event_dir}/T{tier}.joblib"
                scores_path = f"{event_dir}/SCORES_T{tier}.csv"
                plot_path = f"{event_dir}/DENSITY_T{tier}.png"

                train([baseline_features], tier=tier, save_model=model_path)
                score(event_features, model_path, threshold_pct=1.0, output=scores_path)

                stats = quantify(
                    input_path=scores_path,
                    target_date=iso,
                    window_start=win_start,
                    window_end=win_end,
                    bin_size="2min",
                    output=plot_path,
                )

                writer.writerow(
                    [
                        iso,
                        tier,
                        TIER_FEATURES[tier],
                        stats["total_window"],
                        stats["avg_in"] if stats["avg_in"] is not None else "N/A",
                        stats["avg_out"] if stats["avg_out"] is not None else "N/A",
                        stats["snr"] if stats["snr"] is not None else "N/A",
                        stats["t_stat"] if stats["t_stat"] is not None else "N/A",
                        stats["p_val"] if stats["p_val"] is not None else "N/A",
                    ]
                )

            print(f"\n--- Step 4: SHAP (T4 full model, {win_start[:5]}) ---")
            explain_single(
                model_path=f"{event_dir}/T4.joblib",
                data_path=f"{event_dir}/SCORES_T4.csv",
                time_str=win_start[:5],
                output_path=f"{event_dir}/SHAP_T4.png",
            )

    print("\n--- Step 5: Density Grid Plots (one per tier) ---")
    build_density_grids()
    build_shap_grid()

    print("\n--- Step 6: Comparison Spreadsheet ---")
    build_sheet(COMPARISON_CSV, COMPARISON_XLSX)

    print()
    print("=" * 70)
    print("=== Pipeline complete ===")
    print(f"Comparison table: {COMPARISON_XLSX}")
    print("=" * 70)


if __name__ == "__main__":
    main()
