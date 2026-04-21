#!/bin/bash
# Full pipeline across all four confirmed MULN spoofing events.
# For each event: extract → train → score → quantify → SHAP
# Then produces per-tier grid plots combining all four events.
#
# Usage:
#   ./run_all_tiers.sh [--reextract]
#
# Flags:
#   --reextract   Re-run C++ feature extraction even if CSVs already exist.
set -e

REEXTRACT=false
for arg in "$@"; do
  [[ "$arg" == "--reextract" ]] && REEXTRACT=true
done

BINARY="./build/thesis"
if [[ ! -x "$BINARY" ]]; then
  echo "Error: binary not found at $BINARY — run cmake --build build first."
  exit 1
fi

# ---------------------------------------------------------------------------
# Event registry — spoofing windows from court documents (Case 1:23-cv-07613)
# ---------------------------------------------------------------------------
EVENTS=(20221025 20221215 20230606 20230817)

declare -A WIN_START WIN_END
WIN_START[20221025]="14:26:10"; WIN_END[20221025]="14:28:10"  # Para 112
WIN_START[20221215]="13:25:30"; WIN_END[20221215]="13:27:30"  # Para 124
WIN_START[20230606]="15:50:59"; WIN_END[20230606]="15:52:59"  # Para 118
WIN_START[20230817]="15:53:27"; WIN_END[20230817]="15:55:27"  # Para 130

declare -A TIER_FEATURES
TIER_FEATURES[1]="Patience (delta_t, rel_size)"
TIER_FEATURES[2]="Safety (delta_t, rel_size, dist, vol_ahead)"
TIER_FEATURES[3]="Full Model (All Features)"
TIER_FEATURES[4]="Aggressive Baiting (delta_t, rel_size, imbalance)"

# ---------------------------------------------------------------------------
extract_if_needed() {
  local dbn="$1" csv="$2" label="$3"
  if [[ "$REEXTRACT" == true || ! -f "$csv" ]]; then
    if [[ ! -f "$dbn" ]]; then
      echo "  Error: $label DBN not found: $dbn"
      exit 1
    fi
    echo "  Extracting: $dbn → $csv"
    "$BINARY" extract-features "$dbn" --ticker MULN --output "$csv"
  else
    echo "  Skipping extraction (CSV exists): $csv"
  fi
}

# ---------------------------------------------------------------------------
# Steps 0–4 per event
# ---------------------------------------------------------------------------
for DATE_COMPACT in "${EVENTS[@]}"; do
  DATE_ISO="${DATE_COMPACT:0:4}-${DATE_COMPACT:4:2}-${DATE_COMPACT:6:2}"
  EVENT_DIR="output/MULN_${DATE_COMPACT}"
  mkdir -p "$EVENT_DIR"

  BASELINE_DBN="data/BASELINE/MULN_${DATE_COMPACT}_BASELINE.dbn.zst"
  EVENT_DBN="data/MANIPULATION_WINDOWS/MULN_${DATE_COMPACT}.dbn.zst"
  BASELINE_FEATURES="${EVENT_DIR}/BASELINE_FEATURES.csv"
  EVENT_FEATURES="${EVENT_DIR}/FEATURES.csv"
  COMPARISON_CSV="${EVENT_DIR}/comparison.csv"
  WINDOW_START="${WIN_START[$DATE_COMPACT]}"
  WINDOW_END="${WIN_END[$DATE_COMPACT]}"

  echo ""
  echo "======================================================================"
  echo "=== MULN ${DATE_ISO}  (window ${WINDOW_START}–${WINDOW_END}) ==="
  echo "======================================================================"

  echo ""
  echo "--- Step 0: Feature Extraction ---"
  extract_if_needed "$BASELINE_DBN" "$BASELINE_FEATURES" "baseline"
  extract_if_needed "$EVENT_DBN"    "$EVENT_FEATURES"    "event"

  echo "Tier,Features,Total_Window_Anomalies,Avg_In_Window,Avg_Outside_Window,SNR" > "${COMPARISON_CSV}"

  for TIER in 1 2 3 4; do
    echo ""
    echo "--- Tier ${TIER}: ${TIER_FEATURES[$TIER]} ---"

    MODEL="${EVENT_DIR}/T${TIER}.joblib"
    SCORES="${EVENT_DIR}/SCORES_T${TIER}.csv"
    PLOT="${EVENT_DIR}/DENSITY_T${TIER}.png"

    python3 scripts/isolation_forest.py \
      --train "${BASELINE_FEATURES}" \
      --tier ${TIER} \
      --save-model "${MODEL}"

    python3 scripts/isolation_forest.py \
      --test "${EVENT_FEATURES}" \
      --load-model "${MODEL}" \
      --threshold-pct 1.0 \
      --output "${SCORES}"

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
  echo "Comparison summary: ${COMPARISON_CSV}"
  column -t -s, "${COMPARISON_CSV}"

  echo ""
  echo "--- Step 4: SHAP (T4, ${WINDOW_START:0:5}) ---"
  python3 scripts/explain_anomalies.py \
    --model  "${EVENT_DIR}/T4.joblib" \
    --data   "${EVENT_DIR}/SCORES_T4.csv" \
    --time   "${WINDOW_START:0:5}" \
    --output "${EVENT_DIR}/SHAP_T4.png"
done

# ---------------------------------------------------------------------------
# Step 5: Per-tier grid plots across all four events
# ---------------------------------------------------------------------------
echo ""
echo "--- Step 5: Density Grid Plots (one per tier) ---"

python3 - <<'EOF'
import matplotlib.pyplot as plt
import matplotlib.image as mpimg

events = ["20221025", "20221215", "20230606", "20230817"]
labels = ["Oct 25 2022", "Dec 15 2022", "Jun 06 2023", "Aug 17 2023"]

for tier in range(1, 5):
    fig, axes = plt.subplots(2, 2, figsize=(22, 10))
    fig.suptitle(f"Anomaly Density — Tier {tier} (all confirmed events)", fontsize=15, fontweight="bold")
    for ax, date, label in zip(axes.flat, events, labels):
        path = f"output/MULN_{date}/DENSITY_T{tier}.png"
        try:
            img = mpimg.imread(path)
            ax.imshow(img)
        except FileNotFoundError:
            ax.text(0.5, 0.5, f"Missing:\n{path}", ha="center", va="center",
                    transform=ax.transAxes, color="red")
        ax.set_title(label, fontsize=11)
        ax.axis("off")
    plt.tight_layout()
    out = f"output/GRID_DENSITY_T{tier}.png"
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  Saved {out}")
EOF

echo ""
echo "======================================================================"
echo "=== Pipeline complete ==="
echo "======================================================================"
