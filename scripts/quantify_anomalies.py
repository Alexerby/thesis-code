"""
Quantify Anomaly Density and Visualize against Ground Truth.

Usage:
    python scripts/quantify_anomalies.py --input output/features/MULN_20221025_SCORES_T4.csv --target-date 2022-10-25
"""

import argparse
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from datetime import datetime
from scipy import stats

def quantify(input_path, target_date, window_start, window_end,
             bin_size="2min", output=None, fp_threshold_mult=1.1):
    """Compute anomaly density stats and save a density plot.

    Returns a dict with keys: total_window, avg_in, avg_out, snr, t_stat, p_val.
    Any value that cannot be computed is returned as None.
    """
    bin_freq = bin_size.replace('T', 'min')

    df = pd.read_csv(input_path)
    if 'ts_recv' not in df.columns or 'anomaly' not in df.columns:
        raise ValueError("CSV must contain 'ts_recv' and 'anomaly' columns.")

    df['dt_utc'] = pd.to_datetime(df['ts_recv'], unit='ns', utc=True)
    df['dt_et'] = df['dt_utc'].dt.tz_convert('US/Eastern')
    df.set_index('dt_et', inplace=True)

    density = df['anomaly'].resample(bin_freq).sum().to_frame(name='anomalies')
    density['total_orders'] = df['anomaly'].resample(bin_freq).count()
    density = density.between_time('09:00', '16:30')
    density.index = density.index.tz_localize(None)

    avg_anomalies = density['anomalies'].mean()
    fp_threshold = avg_anomalies * fp_threshold_mult
    sig_threshold_val = density['anomalies'].median() + 3 * density['anomalies'].std()

    target_dt = pd.to_datetime(target_date).date()
    ws, we = window_start, window_end
    win_start_ts = pd.Timestamp.combine(target_dt, datetime.strptime(ws, "%H:%M:%S").time())
    win_end_ts   = pd.Timestamp.combine(target_dt, datetime.strptime(we, "%H:%M:%S").time())

    in_window_mask = (density.index >= (win_start_ts - pd.Timedelta(bin_freq))) & (density.index <= win_end_ts)
    is_fp_spike = (~in_window_mask) & (density['anomalies'] >= fp_threshold)
    is_tp_spike = in_window_mask & (density['anomalies'] > 0)

    # --- Plot ---
    plt.figure(figsize=(12, 6))
    plt.scatter(density.index[is_tp_spike], density['anomalies'][is_tp_spike],
                color='forestgreen', s=20, zorder=6, label='True Positive Alert (TP)')
    for idx, row in density[is_tp_spike].iterrows():
        plt.annotate(f"TP: {idx.strftime('%H:%M')}", (idx, row['anomalies']),
                     textcoords="offset points", xytext=(0, 10), ha='center',
                     fontsize=8, color='forestgreen', weight='bold')
    plt.scatter(density.index[is_fp_spike], density['anomalies'][is_fp_spike],
                color='darkorange', s=10, zorder=5, label=f'False Positive Alert (>{fp_threshold_mult}x Avg)')
    top_fps = density[is_fp_spike].sort_values('anomalies', ascending=False).head(3)
    for idx, row in top_fps.iterrows():
        plt.annotate(f"FP: {idx.strftime('%H:%M')}", (idx, row['anomalies']),
                     textcoords="offset points", xytext=(0, 10), ha='center', fontsize=8, color='darkorange')

    plt.fill_between(density.index, density['anomalies'], color='gray', alpha=0.1)
    plt.axhline(fp_threshold, color='darkorange', linestyle='--', alpha=0.4,
                label=f'Decision Threshold ({fp_threshold_mult}x Avg)')
    plt.axhline(sig_threshold_val, color='red', linestyle='--', alpha=0.3,
                label='Stat. Significance (Median+3σ)')
    plt.axvspan(win_start_ts, win_end_ts, color='yellow', alpha=0.3, label='SEC Spoofing Window')

    plt.title(f"Anomaly Density & Surveillance Alerts (Ticker: MULN, Date: {target_date})", fontsize=14)
    plt.ylabel("Anomalies per Bin")
    plt.xlabel("Time (Eastern Time)")
    plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
    plt.gca().xaxis.set_minor_locator(mdates.MinuteLocator(byminute=[0, 10, 20, 30, 40, 50]))
    plt.xticks(rotation=45)
    plt.legend(loc='upper left', fontsize=9)
    plt.grid(alpha=0.2, which='major', linestyle='-')
    plt.grid(alpha=0.1, which='minor', linestyle='--')
    plt.tight_layout()

    if output:
        plt.savefig(output, dpi=150)
        print(f"Enhanced surveillance plot saved to {output}")
    plt.close()

    # --- Stats ---
    target_dt_obj = pd.to_datetime(target_date).date()
    day_data = density[density.index.date == target_dt_obj]
    window_bins  = day_data[(day_data.index >= (win_start_ts - pd.Timedelta(bin_freq))) &
                             (day_data.index <= win_end_ts)]
    outside_bins = day_data[~day_data.index.isin(window_bins.index)]

    total_window = int(window_bins['anomalies'].sum())
    avg_in  = window_bins['anomalies'].mean()
    avg_out = outside_bins['anomalies'].mean() if len(outside_bins) > 0 else None

    snr = None
    if avg_out is not None and avg_out > 0:
        snr = avg_in / avg_out

    t_stat, p_val = None, None
    if len(window_bins) >= 2 and len(outside_bins) >= 2:
        t_stat, p_val = stats.ttest_ind(
            window_bins['anomalies'].values,
            outside_bins['anomalies'].values,
            equal_var=False,
            alternative='greater',
        )

    return {
        "total_window": total_window,
        "avg_in":   round(avg_in,   2) if avg_in  is not None else None,
        "avg_out":  round(avg_out,  2) if avg_out is not None else None,
        "snr":      round(snr,      3) if snr     is not None else None,
        "t_stat":   round(t_stat,   3) if t_stat  is not None else None,
        "p_val":    round(p_val,    4) if p_val   is not None else None,
    }


def parse_args():
    p = argparse.ArgumentParser(description="Quantify Anomaly Density")
    p.add_argument("--input", required=True, help="Path to scored CSV")
    p.add_argument("--output", default="output/plots/MULN_20221025_DENSITY.png", help="Path to save the plot")
    p.add_argument("--bin-size", default="1min", help="Pandas frequency string (e.g. '1min', '5min')")
    p.add_argument("--target-date", help="The date of the manipulation (YYYY-MM-DD)")
    p.add_argument("--window-start", help="Start of spoofing window (HH:MM:SS), e.g. 14:26:10")
    p.add_argument("--window-end",   help="End   of spoofing window (HH:MM:SS), e.g. 14:28:10")
    p.add_argument("--fp-threshold-mult", type=float, default=1.1,
                   help="Only mark FP dots if they are X times higher than intraday average (default 1.1)")
    return p.parse_args()

def main():
    args = parse_args()

    result = quantify(
        input_path=args.input,
        target_date=args.target_date,
        window_start=args.window_start or "14:26:10",
        window_end=args.window_end or "14:28:10",
        bin_size=args.bin_size,
        output=args.output,
        fp_threshold_mult=args.fp_threshold_mult,
    )

    ws = args.window_start or "14:26:10"
    we = args.window_end   or "14:28:10"
    print(f"\n--- Statistics for {args.target_date} ---")
    print(f"Total Anomalies in Window (Bins Overlapping {ws}-{we}): {result['total_window']}")
    print(f"Avg Anomalies (In Window Bins):  {result['avg_in']}")
    print(f"Avg Anomalies (Outside Bins):    {result['avg_out']}")
    if result['snr'] is not None:
        print(f"Signal-to-Noise Ratio:     {result['snr']}x higher in window")
    if result['t_stat'] is not None:
        print(f"T-statistic:               {result['t_stat']}")
        print(f"P-value (one-sided):       {result['p_val']}")

if __name__ == "__main__":
    main()
