# Thesis Research Snapshot: MULN Spoofing Detection
Generated: tis 21 apr 2026 13:25:39 CEST

## Summary of Results
- **Winning Model:** Tier 4 (Aggressive Baiting)
- **Key Features:** delta_t, relative_size, induced_imbalance (MBO)
- **Primary Metric:** Signal-to-Noise Ratio (SNR) of 1.79x
- **Discovery:** Detected unprosecuted manipulation event at 12:34 PM ET (confirmed via visualizer).

## Exact Hyperparameters
- **Anomaly Budget:** 1.0% (--threshold-pct 1.0)
- **Trading Window:** 09:45 - 15:55 ET (Continuous Trading Session only)
- **Bin Resolution:** 2-Minute Intervals (matching the 120s SEC window)
- **Surveillance Threshold:** 1.1x intraday average for False Positive marking.
- **Significance Threshold:** Median + 3 Standard Deviations.

## File Map
- `output/plots/MULN_OCT25_DENSITY_T4.png`: Primary visual evidence.
- `output/comparison.csv`: Statistical breakdown of all feature tiers.
- `code/`: Snapshot of the Python logic and pipeline scripts.
