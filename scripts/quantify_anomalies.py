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
    bin_freq = args.bin_size.replace('T', 'min')
    
    # 1. Load data
    df = pd.read_csv(args.input)
    if 'ts_recv' not in df.columns or 'anomaly' not in df.columns:
        print("Error: CSV must contain 'ts_recv' and 'anomaly' columns.")
        return

    # 2. Convert to US/Eastern time
    df['dt_utc'] = pd.to_datetime(df['ts_recv'], unit='ns', utc=True)
    df['dt_et'] = df['dt_utc'].dt.tz_convert('US/Eastern')
    
    # Set index and aggregate
    df.set_index('dt_et', inplace=True)
    density = df['anomaly'].resample(bin_freq).sum().to_frame(name='anomalies')
    density['total_orders'] = df['anomaly'].resample(bin_freq).count()
    
    # Filter for core hours + padding (09:00 - 16:30) to remove overnight clutter
    density = density.between_time('09:00', '16:30')
    
    # CRITICAL: Convert index to naive local time for plotting to fix alignment
    density.index = density.index.tz_localize(None)

    # 3. Plotting (Enhanced for Surveillance Decision Logic)
    plt.figure(figsize=(12, 6))
    
    # Calculate intraday average for decision logic
    avg_anomalies = density['anomalies'].mean()
    fp_threshold = avg_anomalies * args.fp_threshold_mult
    sig_threshold_val = density['anomalies'].median() + 3 * density['anomalies'].std()
    
    if args.target_date:
        target_dt = pd.to_datetime(args.target_date).date()
        ws = args.window_start or "14:26:10"
        we = args.window_end   or "14:28:10"
        win_start = pd.Timestamp.combine(target_dt, datetime.strptime(ws, "%H:%M:%S").time())
        win_end   = pd.Timestamp.combine(target_dt, datetime.strptime(we, "%H:%M:%S").time())
        
        in_window_mask = (density.index >= (win_start - pd.Timedelta(bin_freq))) & (density.index <= win_end)
        
        # RULE: Only plot False Positives if they exceed the Decision Threshold
        is_fp_spike = (~in_window_mask) & (density['anomalies'] >= fp_threshold)
        is_tp_spike = in_window_mask & (density['anomalies'] > 0)
        
        # Plot TPs (Green)
        plt.scatter(density.index[is_tp_spike], density['anomalies'][is_tp_spike], 
                    color='forestgreen', s=20, zorder=6, label='True Positive Alert (TP)')
        
        # Label each TP bin
        for idx, row in density[is_tp_spike].iterrows():
             plt.annotate(f"TP: {idx.strftime('%H:%M')}", (idx, row['anomalies']), 
                         textcoords="offset points", xytext=(0,10), ha='center', fontsize=8, color='forestgreen', weight='bold')

        # Plot FPs (Orange) - ONLY THE SIGNIFICANT ONES
        plt.scatter(density.index[is_fp_spike], density['anomalies'][is_fp_spike], 
                    color='darkorange', s=10, zorder=5, label=f'False Positive Alert (>{args.fp_threshold_mult}x Avg)')
    else:
        # Simple plot where anomalies > 0
        pos_anomalies = density['anomalies'] > 0
        plt.scatter(density.index[pos_anomalies], density['anomalies'][pos_anomalies], 
                    color='firebrick', s=8)

    plt.fill_between(density.index, density['anomalies'], color='gray', alpha=0.1)
    plt.axhline(fp_threshold, color='darkorange', linestyle='--', alpha=0.4, label=f'Decision Threshold ({args.fp_threshold_mult}x Avg)')
    plt.axhline(sig_threshold_val, color='red', linestyle='--', alpha=0.3, label='Stat. Significance (Median+3σ)')

    # Highlight the SEC Window
    if args.target_date:
        plt.axvspan(win_start, win_end, color='yellow', alpha=0.3, label='SEC Spoofing Window')
    
    # Label top 3 significant FPs
    if args.target_date:
        top_fps = density[is_fp_spike].sort_values('anomalies', ascending=False).head(3)
        for idx, row in top_fps.iterrows():
            plt.annotate(f"FP: {idx.strftime('%H:%M')}", (idx, row['anomalies']), 
                         textcoords="offset points", xytext=(0,10), ha='center', fontsize=8, color='darkorange')

    plt.title(f"Anomaly Density & Surveillance Alerts (Ticker: MULN, Date: {args.target_date})", fontsize=14)
    plt.ylabel("Anomalies per Bin")
    plt.xlabel("Time (Eastern Time)")
    
    plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
    plt.gca().xaxis.set_minor_locator(mdates.MinuteLocator(byminute=[0, 10, 20, 30, 40, 50]))
    plt.xticks(rotation=45)
    plt.legend(loc='upper left', fontsize=9)
    plt.grid(alpha=0.2, which='major', linestyle='-')
    plt.grid(alpha=0.1, which='minor', linestyle='--')
    plt.tight_layout()
    
    plt.savefig(args.output, dpi=150)
    print(f"Enhanced surveillance plot saved to {args.output}")

    # 4. Statistical Summary (Local Date only)
    if args.target_date:
        target_dt_obj = pd.to_datetime(args.target_date).date()
        day_data = density[density.index.date == target_dt_obj]
        
        # Define the exact start/end of the spoofing window
        w_start_dt = win_start
        w_end_dt   = win_end
        
        # Find all bins that OVERLAP with the spoofing window
        window_bins = day_data[(day_data.index >= (w_start_dt - pd.Timedelta(bin_freq))) & 
                               (day_data.index <= w_end_dt)]
        
        outside_bins = day_data[~day_data.index.isin(window_bins.index)]
        
        print(f"\n--- Statistics for {args.target_date} ---")
        print(f"Total Anomalies in Window (Bins Overlapping {ws}-{we}): {int(window_bins['anomalies'].sum())}")
        print(f"Avg Anomalies (In Window Bins):  {window_bins['anomalies'].mean():.2f}")
        print(f"Avg Anomalies (Outside Bins):    {outside_bins['anomalies'].mean():.2f}")
        
        if len(outside_bins) > 0 and outside_bins['anomalies'].mean() > 0:
            ratio = window_bins['anomalies'].mean() / outside_bins['anomalies'].mean()
            print(f"Signal-to-Noise Ratio:     {ratio:.2f}x higher in window")

        # Welch's t-test: are window bins significantly elevated vs outside bins?
        if len(window_bins) >= 2 and len(outside_bins) >= 2:
            t_stat, p_val = stats.ttest_ind(
                window_bins['anomalies'].values,
                outside_bins['anomalies'].values,
                equal_var=False,  # Welch's — does not assume equal variance
                alternative='greater',
            )
            print(f"T-statistic:               {t_stat:.3f}")
            print(f"P-value (one-sided):       {p_val:.4f}")

if __name__ == "__main__":
    main()
