"""
Baseline regression: Spread_t = alpha + beta * pi_hat_t

Reads features/posteriors.csv (produced by ./thesis gmm ...), aggregates
observations into non-overlapping 5-minute windows, then estimates the
baseline OLS regression with Newey-West standard errors.

Usage:
    python scripts/regression.py [--posteriors PATH] [--window-min N]
"""

import argparse
import sys

import numpy as np
import pandas as pd
import statsmodels.api as sm


def load_posteriors(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    required = {"ts_recv_ns", "r_i", "spread_bps"}
    missing = required - set(df.columns)
    if missing:
        sys.exit(f"posteriors.csv is missing columns: {missing}")
    return df


def aggregate_windows(df: pd.DataFrame, window_min: int) -> pd.DataFrame:
    window_ns = window_min * 60 * 1_000_000_000
    df = df.copy()
    df["window"] = df["ts_recv_ns"] // window_ns

    windows = (
        df.groupby("window")
        .agg(
            pi_hat=("r_i", "mean"),
            spread=("spread_bps", "mean"),
            n=("r_i", "count"),
        )
        .reset_index()
    )
    # Drop windows with very few events — likely partial windows at day boundaries
    windows = windows[windows["n"] >= 10].copy()
    return windows


def run_baseline(windows: pd.DataFrame) -> None:
    y = windows["spread"]
    X = sm.add_constant(windows["pi_hat"])

    # Newey-West HAC standard errors — lag order follows the common rule ceil(T^0.25)
    n_lags = max(1, int(np.ceil(len(windows) ** 0.25)))
    result = sm.OLS(y, X).fit(cov_type="HAC", cov_kwds={"maxlags": n_lags})

    print(result.summary(
        title=f"Baseline OLS  (N={len(windows)} windows, Newey-West lags={n_lags})",
        yname="Spread_t (bps)",
        xname=["alpha", "beta (pi_hat_t)"],
    ))

    beta = result.params["pi_hat"]
    se   = result.bse["pi_hat"]
    pval = result.pvalues["pi_hat"]
    print(
        f"\nKey result:\n"
        f"  beta  = {beta:+.4f} bps per unit pi_hat\n"
        f"  SE    = {se:.4f}   (Newey-West)\n"
        f"  p     = {pval:.4f}\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--posteriors",
        default="features/posteriors.csv",
        help="Path to posteriors.csv (default: features/posteriors.csv)",
    )
    parser.add_argument(
        "--window-min",
        type=int,
        default=5,
        help="Window length in minutes (default: 5)",
    )
    args = parser.parse_args()

    print(f"Loading {args.posteriors} ...")
    df = load_posteriors(args.posteriors)
    print(f"  {len(df):,} order observations loaded")

    windows = aggregate_windows(df, args.window_min)
    print(
        f"  {len(windows)} {args.window_min}-minute windows "
        f"(pi_hat range: [{windows['pi_hat'].min():.3f}, {windows['pi_hat'].max():.3f}])\n"
    )

    run_baseline(windows)


if __name__ == "__main__":
    main()
