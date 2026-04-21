"""
Quantify Anomaly Density and Visualize against Ground Truth.

Usage:
    python scripts/quantify_anomalies.py --input output/features/MULN_OCT25_SCORES_T4.csv --target-date 2022-10-25
"""

import argparse
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from datetime import datetime

def parse_args():
    p = argparse.ArgumentParser(description="Quantify Anomaly Density")
    p.add_argument("--input", required=True, help="Path to scored CSV")
    p.add_argument("--output", default="output/plots/MULN_OCT25_DENSITY.png", help="Path to save the plot")
    p.add_argument("--bin-size", default="1min", help="Pandas frequency string (e.g. '1min', '5min')")
    p.add_argument("--target-date", help="The date of the manipulation (YYYY-MM-DD)")
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

    # 3. Plotting (Enhanced for False Positive Analysis)
    plt.figure(figsize=(12, 6))
    
    # Calculate a baseline threshold for "significant" noise
    # (Median + 3*Std of the intraday activity)
    threshold_val = density['anomalies'].median() + 3 * density['anomalies'].std()
    
    # Split data for color coding
    if args.target_date:
        target_dt = pd.to_datetime(args.target_date).date()
        win_start = pd.Timestamp.combine(target_dt, datetime.strptime("14:26:10", "%H:%M:%S").time())
        win_end = pd.Timestamp.combine(target_dt, datetime.strptime("14:28:10", "%H:%M:%S").time())
        
        in_window_mask = (density.index >= (win_start - pd.Timedelta(bin_freq))) & (density.index <= win_end)
        
        # Plot True Positives (Green) and False Positives (Orange)
        plt.scatter(density.index[in_window_mask], density['anomalies'][in_window_mask], 
                    color='forestgreen', s=10, zorder=5, label='True Positive (In Window)')
        plt.scatter(density.index[~in_window_mask], density['anomalies'][~in_window_mask], 
                    color='darkorange', s=5, zorder=4, label='False Positive / Noise')
    else:
        plt.scatter(density.index, density['anomalies'], color='firebrick', s=5)

    plt.fill_between(density.index, density['anomalies'], color='gray', alpha=0.1)
    plt.axhline(threshold_val, color='red', linestyle='--', alpha=0.5, label='Significance Threshold (Median+3σ)')

    # Highlight the SEC Window
    if args.target_date:
        plt.axvspan(win_start, win_end, color='yellow', alpha=0.3, label='SEC Spoofing Window')
    
    # Annotate Top 3 False Positives
    if args.target_date:
        top_fps = density[~in_window_mask].sort_values('anomalies', ascending=False).head(3)
        for idx, row in top_fps.iterrows():
            plt.annotate(f"FP: {idx.strftime('%H:%M')}", (idx, row['anomalies']), 
                         textcoords="offset points", xytext=(0,10), ha='center', fontsize=8, color='darkorange')

    plt.title(f"Anomaly Density & False Positive Analysis (Ticker: MULN, Date: {args.target_date})", fontsize=14)
    plt.ylabel("Anomalies per Bin")
    plt.xlabel("Time (Eastern Time)")
    
    plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
    plt.xticks(rotation=45)
    plt.legend(loc='upper left', fontsize=9)
    plt.grid(alpha=0.2, linestyle='--')
    plt.tight_layout()
    
    plt.savefig(args.output, dpi=150)
    print(f"Enhanced density plot saved to {args.output}")

    # 4. Statistical Summary (Local Date only)
    if args.target_date:
        target_dt_obj = pd.to_datetime(args.target_date).date()
        day_data = density[density.index.date == target_dt_obj]
        
        # Define the exact start/end of the spoofing window
        w_start_dt = pd.Timestamp.combine(target_dt_obj, datetime.strptime("14:26:10", "%H:%M:%S").time())
        w_end_dt = pd.Timestamp.combine(target_dt_obj, datetime.strptime("14:28:10", "%H:%M:%S").time())
        
        # Find all bins that OVERLAP with the spoofing window
        # This works for any bin size (1min, 5min, etc.)
        window_bins = day_data[(day_data.index >= (w_start_dt - pd.Timedelta(bin_freq))) & 
                               (day_data.index <= w_end_dt)]
        
        outside_bins = day_data[~day_data.index.isin(window_bins.index)]
        
        print(f"\n--- Statistics for {args.target_date} ---")
        print(f"Total Anomalies in Window (Bins Overlapping 14:26-14:28): {int(window_bins['anomalies'].sum())}")
        print(f"Avg Anomalies (In Window Bins):  {window_bins['anomalies'].mean():.2f}")
        print(f"Avg Anomalies (Outside Bins):    {outside_bins['anomalies'].mean():.2f}")
        
        if len(outside_bins) > 0 and outside_bins['anomalies'].mean() > 0:
            ratio = window_bins['anomalies'].mean() / outside_bins['anomalies'].mean()
            print(f"Signal-to-Noise Ratio:     {ratio:.2f}x higher in window")

if __name__ == "__main__":
    main()
