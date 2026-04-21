#!/bin/bash
set -e

# Setup Output
mkdir -p output/features output/models output/plots
COMPARISON_CSV="output/comparison.csv"

# Initialize Comparison CSV
echo "Tier,Features,Total_Window_Anomalies,Avg_In_Window,Avg_Outside_Window,SNR" > ${COMPARISON_CSV}

# Define Tiers and Descriptions
declare -A TIER_FEATURES
TIER_FEATURES[1]="Patience (delta_t, rel_size)"
TIER_FEATURES[2]="Safety (delta_t, rel_size, dist, vol_ahead)"
TIER_FEATURES[3]="Full Model (All Features)"
TIER_FEATURES[4]="Aggressive Baiting (delta_t, rel_size, imbalance)"

echo "=== Thesis Pipeline: Starting Comparison of All Tiers ==="

for TIER in 1 2 3 4; do
    echo "--- Processing Tier ${TIER} (${TIER_FEATURES[$TIER]}) ---"
    
    MODEL="output/models/MULN_BASELINE_T${TIER}.joblib"
    SCORES="output/features/MULN_OCT25_SCORES_T${TIER}.csv"
    PLOT="output/plots/MULN_OCT25_DENSITY_T${TIER}.png"

    # 1. Train
    python3 scripts/isolation_forest.py \
      --train output/features/MULN_BASELINE.csv \
      --tier ${TIER} \
      --save-model ${MODEL}

    # 2. Score
    python3 scripts/isolation_forest.py \
      --test output/features/MULN_MANIPULATED_OCT25.csv \
      --load-model ${MODEL} \
      --output ${SCORES}

    # 3. Quantify & Append to Comparison
    # We use 2-minute bins (2min) to match the SEC window length
    STATS=$(python3 scripts/quantify_anomalies.py \
      --input ${SCORES} \
      --target-date 2022-10-25 \
      --bin-size 2min \
      --output ${PLOT})

    # Parsing the output from quantify_anomalies.py
    # We use grep and awk to find the exact value in the output
    TOTAL_WIN=$(echo "$STATS" | grep "Total Anomalies in Window" | awk -F': ' '{print $NF}')
    AVG_IN=$(echo "$STATS" | grep "Avg Anomalies (In Window)" | awk -F': ' '{print $NF}' | awk '{print $1}')
    AVG_OUT=$(echo "$STATS" | grep "Avg Anomalies (Outside)" | awk -F': ' '{print $NF}' | awk '{print $1}')
    SNR=$(echo "$STATS" | grep "Signal-to-Noise Ratio" | awk -F': ' '{print $NF}' | sed 's/x//' | awk '{print $1}')

    # If SNR is empty or not a number, set to N/A
    if [[ -z "$SNR" ]]; then
      SNR="N/A"
    fi

    # Append to Comparison CSV
    echo "${TIER},\"${TIER_FEATURES[$TIER]}\",${TOTAL_WIN},${AVG_IN},${AVG_OUT},${SNR}" >> ${COMPARISON_CSV}
done

echo "=== All Tiers Complete ==="
echo "Comparison Summary saved to: ${COMPARISON_CSV}"
cat ${COMPARISON_CSV} | column -t -s,
