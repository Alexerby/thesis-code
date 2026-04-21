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

    # 3. Plotting (Single Plot)
    plt.figure(figsize=(12, 5))
    plt.plot(density.index, density['anomalies'], color='firebrick', lw=1.5, label='Anomaly Count')
    plt.fill_between(density.index, density['anomalies'], color='firebrick', alpha=0.15)
    
    # Highlight the SEC Window: 14:26:10 - 14:28:10 ET
    if args.target_date:
        target_dt = pd.to_datetime(args.target_date).date()
        win_start = pd.Timestamp.combine(target_dt, datetime.strptime("14:26:10", "%H:%M:%S").time())
        win_end = pd.Timestamp.combine(target_dt, datetime.strptime("14:28:10", "%H:%M:%S").time())
        
        plt.axvspan(win_start, win_end, color='yellow', alpha=0.4, label='SEC Spoofing Window')
    
    plt.title(f"Anomaly Density (Ticker: MULN, Date: {args.target_date})", fontsize=14)
    plt.ylabel("Anomalies per Bin")
    plt.xlabel("Time (Eastern Time)")
    
    # Standardize X-Axis: HH:MM with 45 deg tilt
    plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
    plt.xticks(rotation=45)
    
    plt.legend(loc='upper left')
    plt.grid(alpha=0.3, linestyle='--')
    plt.tight_layout()
    
    plt.savefig(args.output, dpi=150)
    print(f"Clean density plot saved to {args.output}")

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
