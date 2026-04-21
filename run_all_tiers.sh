#!/bin/bash
# Full pipeline for a single MULN event date: extract → train → score → quantify.
#
# Usage:
#   ./run_all_tiers.sh <YYYYMMDD> [--reextract]
#
# Arguments:
#   YYYYMMDD      Compact date, e.g. 20221025
#
# Flags:
#   --reextract   Re-run C++ feature extraction even if CSVs already exist.
#                 Omit to skip extraction and go straight to train/score/quantify.
#
# Examples:
#   ./run_all_tiers.sh 20221025
#   ./run_all_tiers.sh 20221215 --reextract
set -e

DATE_COMPACT="${1}"
REEXTRACT=false

for arg in "$@"; do
  [[ "$arg" == "--reextract" ]] && REEXTRACT=true
done

if [[ -z "$DATE_COMPACT" ]]; then
  echo "Usage: $0 <YYYYMMDD> [--reextract]"
  echo "  e.g: $0 20221025"
  exit 1
fi

# Derive ISO date (YYYY-MM-DD) from compact form
DATE_ISO="${DATE_COMPACT:0:4}-${DATE_COMPACT:4:2}-${DATE_COMPACT:6:2}"

BINARY="./build/thesis"
if [[ ! -x "$BINARY" ]]; then
  echo "Error: binary not found at $BINARY — run cmake --build build first."
  exit 1
fi

EVENT_DIR="output/MULN_${DATE_COMPACT}"
mkdir -p "$EVENT_DIR"

BASELINE_DBN="data/BASELINE/MULN_${DATE_COMPACT}_BASELINE.dbn.zst"
EVENT_DBN="data/MANIPULATION_WINDOWS/MULN_${DATE_COMPACT}.dbn.zst"
BASELINE_FEATURES="${EVENT_DIR}/BASELINE_FEATURES.csv"
EVENT_FEATURES="${EVENT_DIR}/FEATURES.csv"
COMPARISON_CSV="${EVENT_DIR}/comparison.csv"

# ---------------------------------------------------------------------------
# 0. Feature extraction (skipped unless --reextract or CSVs are missing)
# ---------------------------------------------------------------------------
extract_if_needed() {
  local dbn="$1" csv="$2" label="$3"
  if [[ "$REEXTRACT" == true || ! -f "$csv" ]]; then
    if [[ ! -f "$dbn" ]]; then
      echo "Error: $label DBN not found: $dbn"
      exit 1
    fi
    echo "  Extracting features: $dbn → $csv"
    "$BINARY" extract-features "$dbn" --ticker MULN --output "$csv"
  else
    echo "  Skipping extraction (CSV exists): $csv"
  fi
}

echo "=== Thesis Pipeline: MULN ${DATE_ISO} ==="
echo ""
echo "--- Step 0: Feature Extraction ---"
extract_if_needed "$BASELINE_DBN" "$BASELINE_FEATURES" "baseline"
extract_if_needed "$EVENT_DBN"    "$EVENT_FEATURES"    "event"

# ---------------------------------------------------------------------------
# 1-3. Train → Score → Quantify for each tier
# ---------------------------------------------------------------------------
echo "Tier,Features,Total_Window_Anomalies,Avg_In_Window,Avg_Outside_Window,SNR" > "${COMPARISON_CSV}"

# Spoofing windows from court documents (Case 1:23-cv-07613)
declare -A WIN_START WIN_END
WIN_START[20221025]="14:26:10"; WIN_END[20221025]="14:28:10"  # Para 112
WIN_START[20221215]="13:25:30"; WIN_END[20221215]="13:27:30"  # Para 124
WIN_START[20230606]="15:50:59"; WIN_END[20230606]="15:52:59"  # Para 118
WIN_START[20230817]="15:53:27"; WIN_END[20230817]="15:55:27"  # Para 130

WINDOW_START="${WIN_START[$DATE_COMPACT]}"
WINDOW_END="${WIN_END[$DATE_COMPACT]}"
if [[ -z "$WINDOW_START" ]]; then
  echo "Error: no spoofing window defined for $DATE_COMPACT"
  exit 1
fi

declare -A TIER_FEATURES
TIER_FEATURES[1]="Patience (delta_t, rel_size)"
TIER_FEATURES[2]="Safety (delta_t, rel_size, dist, vol_ahead)"
TIER_FEATURES[3]="Full Model (All Features)"
TIER_FEATURES[4]="Aggressive Baiting (delta_t, rel_size, imbalance)"

for TIER in 1 2 3 4; do
    echo ""
    echo "--- Tier ${TIER}: ${TIER_FEATURES[$TIER]} ---"

    MODEL="${EVENT_DIR}/T${TIER}.joblib"
    SCORES="${EVENT_DIR}/SCORES_T${TIER}.csv"
    PLOT="${EVENT_DIR}/DENSITY_T${TIER}.png"

    # 1. Train on the baseline week
    python3 scripts/isolation_forest.py \
      --train "${BASELINE_FEATURES}" \
      --tier ${TIER} \
      --save-model "${MODEL}"

    # 2. Score the event day (1% threshold for selectivity)
    python3 scripts/isolation_forest.py \
      --test "${EVENT_FEATURES}" \
      --load-model "${MODEL}" \
      --threshold-pct 1.0 \
      --output "${SCORES}"

    # 3. Quantify — 2-minute bins to match the SEC window length
    STATS=$(python3 scripts/quantify_anomalies.py \
      --input "${SCORES}" \
      --target-date "${DATE_ISO}" \
      --window-start "${WINDOW_START}" \
      --window-end   "${WINDOW_END}" \
      --bin-size 2min \
      --output "${PLOT}")

    TOTAL_WIN=$(echo "$STATS" | grep "Total Anomalies in Window"      | awk -F': ' '{print $NF}')
    AVG_IN=$(echo    "$STATS" | grep "Avg Anomalies (In Window Bins)" | awk -F': ' '{print $NF}' | awk '{print $1}')
    AVG_OUT=$(echo   "$STATS" | grep "Avg Anomalies (Outside Bins)"   | awk -F': ' '{print $NF}' | awk '{print $1}')
    SNR=$(echo       "$STATS" | grep "Signal-to-Noise Ratio"          | awk -F': ' '{print $NF}' | sed 's/x//' | awk '{print $1}')

    [[ -z "$SNR" ]] && SNR="N/A"

    echo "${TIER},\"${TIER_FEATURES[$TIER]}\",${TOTAL_WIN},${AVG_IN},${AVG_OUT},${SNR}" >> "${COMPARISON_CSV}"
done

echo ""
echo "=== All Tiers Complete ==="
echo "Comparison summary: ${COMPARISON_CSV}"
column -t -s, "${COMPARISON_CSV}"
