"""
Plot feature distributions split by anomaly flag.

Usage:
    python scripts/plot_anomalies.py [--input features/anomaly_scores.csv] [--out-dir features/plots]
"""

import argparse
import os

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

FEATURES = [
    "delta_t",
    "volume_ahead",
    "induced_imbalance",
    "relative_size",
    "price_distance_ticks",
]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--input", default="features/anomaly_scores.csv")
    p.add_argument("--out-dir", default="features/plots")
    p.add_argument("--bins", type=int, default=80)
    p.add_argument("--clip-pct", type=float, default=99.0,
                   help="Clip x-axis at this percentile to suppress extreme outliers in plots")
    return p.parse_args()


def main() -> None:
    args = parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    df = pd.read_csv(args.input)
    normal = df[df["anomaly"] == 0]
    flagged = df[df["anomaly"] == 1]

    print(f"Normal:  {len(normal):,}  |  Flagged: {len(flagged):,}")

    for feat in FEATURES:
        if feat not in df.columns:
            print(f"  skipping {feat} (not in CSV)")
            continue

        clip = np.percentile(df[feat].dropna(), args.clip_pct)

        fig, axes = plt.subplots(1, 2, figsize=(10, 4), sharey=False)
        fig.suptitle(feat, fontsize=13)

        for ax, subset, label, color in [
            (axes[0], normal,  "Normal (anomaly=0)",  "steelblue"),
            (axes[1], flagged, "Flagged (anomaly=1)", "firebrick"),
        ]:
            vals = subset[feat].dropna()
            vals = vals[vals <= clip]
            ax.hist(vals, bins=args.bins, color=color, alpha=0.8, edgecolor="none")
            ax.set_title(f"{label}\n(n={len(subset):,})", fontsize=10)
            ax.set_xlabel(feat)
            ax.set_ylabel("count")
            ax.axvline(vals.median(), color="black", linewidth=1, linestyle="--",
                       label=f"median={vals.median():.3f}")
            ax.legend(fontsize=8)

        plt.tight_layout()
        out = os.path.join(args.out_dir, f"{feat}.png")
        plt.savefig(out, dpi=130)
        plt.close()
        print(f"  saved {out}")

    print("\nDone.")


if __name__ == "__main__":
    main()
