#!/usr/bin/env bash
set -euo pipefail

BINARY="./build/thesis"
OUT_DIR="data/MANIPULATION_WINDOWS"
mkdir -p "$OUT_DIR"

DATES=(
  2022-10-12
  2022-10-20
  2022-11-03
  2022-11-07
  2022-11-21
  2023-02-09
  2023-02-14
  2023-02-15
  2023-02-23
  2023-02-27
  2023-02-28
  2023-03-01
  2023-03-13
  2023-03-20
  2023-03-24
  2023-06-15
  2023-06-16
  2023-06-29
  2023-06-30
  2023-09-15
)

for DATE in "${DATES[@]}"; do
  COMPACT="${DATE//-/}"
  OUT="$OUT_DIR/MULN_${COMPACT}.dbn.zst"

  if [[ -f "$OUT" ]]; then
    echo "  skip  $DATE — already exists"
    continue
  fi

  NEXT=$(date -d "$DATE + 1 day" +%Y-%m-%d)
  echo "  fetch $DATE → $OUT"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start "${DATE}T00:00:00Z" \
    --end   "${NEXT}T00:00:00Z" \
    --output "$OUT" \
    --yes
done

echo "Done."
