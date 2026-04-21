#!/bin/bash
set -e

TIER=2
mkdir -p output/plots
MODEL="output/models/MULN_BASELINE_T${TIER}.joblib"
SCORES="output/features/MULN_OCT25_SCORES_T${TIER}.csv"
PLOT="output/plots/MULN_OCT25_DENSITY_T${TIER}.png"

echo "=== Running Pipeline for Tier ${TIER} (Safety) ==="

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

# 3. Quantify
python3 scripts/quantify_anomalies.py \
  --input ${SCORES} \
  --target-date 2022-10-25 \
  --output ${PLOT}

echo "Tier ${TIER} complete. Results in ${PLOT}"
