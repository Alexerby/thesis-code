#!/usr/bin/env bash
set -euo pipefail
BINARY="./build/thesis"
mkdir -p "data/CONTROL"

if [[ -f "data/CONTROL/MULN_20221010_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-10-10 — already exists"
else
  echo "  fetch 2022-10-10"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-10-10T19:00:00Z \
    --end   2022-10-10T20:00:00Z \
    --output "data/CONTROL/MULN_20221010_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221014_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-10-14 — already exists"
else
  echo "  fetch 2022-10-14"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-10-14T19:00:00Z \
    --end   2022-10-14T20:00:00Z \
    --output "data/CONTROL/MULN_20221014_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221017_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-10-17 — already exists"
else
  echo "  fetch 2022-10-17"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-10-17T19:00:00Z \
    --end   2022-10-17T20:00:00Z \
    --output "data/CONTROL/MULN_20221017_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221018_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-10-18 — already exists"
else
  echo "  fetch 2022-10-18"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-10-18T19:00:00Z \
    --end   2022-10-18T20:00:00Z \
    --output "data/CONTROL/MULN_20221018_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221027_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-10-27 — already exists"
else
  echo "  fetch 2022-10-27"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-10-27T19:00:00Z \
    --end   2022-10-27T20:00:00Z \
    --output "data/CONTROL/MULN_20221027_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221028_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-10-28 — already exists"
else
  echo "  fetch 2022-10-28"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-10-28T19:00:00Z \
    --end   2022-10-28T20:00:00Z \
    --output "data/CONTROL/MULN_20221028_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221031_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-10-31 — already exists"
else
  echo "  fetch 2022-10-31"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-10-31T19:00:00Z \
    --end   2022-10-31T20:00:00Z \
    --output "data/CONTROL/MULN_20221031_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221101_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-11-01 — already exists"
else
  echo "  fetch 2022-11-01"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-11-01T19:00:00Z \
    --end   2022-11-01T20:00:00Z \
    --output "data/CONTROL/MULN_20221101_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221109_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-11-09 — already exists"
else
  echo "  fetch 2022-11-09"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-11-09T20:00:00Z \
    --end   2022-11-09T21:00:00Z \
    --output "data/CONTROL/MULN_20221109_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221110_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-11-10 — already exists"
else
  echo "  fetch 2022-11-10"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-11-10T20:00:00Z \
    --end   2022-11-10T21:00:00Z \
    --output "data/CONTROL/MULN_20221110_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221123_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-11-23 — already exists"
else
  echo "  fetch 2022-11-23"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-11-23T20:00:00Z \
    --end   2022-11-23T21:00:00Z \
    --output "data/CONTROL/MULN_20221123_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221125_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-11-25 — already exists"
else
  echo "  fetch 2022-11-25"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-11-25T20:00:00Z \
    --end   2022-11-25T21:00:00Z \
    --output "data/CONTROL/MULN_20221125_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221212_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-12-12 — already exists"
else
  echo "  fetch 2022-12-12"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-12-12T20:00:00Z \
    --end   2022-12-12T21:00:00Z \
    --output "data/CONTROL/MULN_20221212_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20221213_ctrl.dbn.zst" ]]; then
  echo "  skip  2022-12-13 — already exists"
else
  echo "  fetch 2022-12-13"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2022-12-13T20:00:00Z \
    --end   2022-12-13T21:00:00Z \
    --output "data/CONTROL/MULN_20221213_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230206_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-02-06 — already exists"
else
  echo "  fetch 2023-02-06"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-02-06T20:00:00Z \
    --end   2023-02-06T21:00:00Z \
    --output "data/CONTROL/MULN_20230206_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230207_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-02-07 — already exists"
else
  echo "  fetch 2023-02-07"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-02-07T20:00:00Z \
    --end   2023-02-07T21:00:00Z \
    --output "data/CONTROL/MULN_20230207_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230217_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-02-17 — already exists"
else
  echo "  fetch 2023-02-17"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-02-17T20:00:00Z \
    --end   2023-02-17T21:00:00Z \
    --output "data/CONTROL/MULN_20230217_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230221_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-02-21 — already exists"
else
  echo "  fetch 2023-02-21"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-02-21T20:00:00Z \
    --end   2023-02-21T21:00:00Z \
    --output "data/CONTROL/MULN_20230221_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230303_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-03-03 — already exists"
else
  echo "  fetch 2023-03-03"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-03-03T20:00:00Z \
    --end   2023-03-03T21:00:00Z \
    --output "data/CONTROL/MULN_20230303_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230315_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-03-15 — already exists"
else
  echo "  fetch 2023-03-15"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-03-15T19:00:00Z \
    --end   2023-03-15T20:00:00Z \
    --output "data/CONTROL/MULN_20230315_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230316_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-03-16 — already exists"
else
  echo "  fetch 2023-03-16"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-03-16T19:00:00Z \
    --end   2023-03-16T20:00:00Z \
    --output "data/CONTROL/MULN_20230316_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230322_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-03-22 — already exists"
else
  echo "  fetch 2023-03-22"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-03-22T19:00:00Z \
    --end   2023-03-22T20:00:00Z \
    --output "data/CONTROL/MULN_20230322_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230608_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-06-08 — already exists"
else
  echo "  fetch 2023-06-08"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-06-08T19:00:00Z \
    --end   2023-06-08T20:00:00Z \
    --output "data/CONTROL/MULN_20230608_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230609_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-06-09 — already exists"
else
  echo "  fetch 2023-06-09"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-06-09T19:00:00Z \
    --end   2023-06-09T20:00:00Z \
    --output "data/CONTROL/MULN_20230609_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230612_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-06-12 — already exists"
else
  echo "  fetch 2023-06-12"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-06-12T19:00:00Z \
    --end   2023-06-12T20:00:00Z \
    --output "data/CONTROL/MULN_20230612_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230613_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-06-13 — already exists"
else
  echo "  fetch 2023-06-13"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-06-13T19:00:00Z \
    --end   2023-06-13T20:00:00Z \
    --output "data/CONTROL/MULN_20230613_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230626_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-06-26 — already exists"
else
  echo "  fetch 2023-06-26"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-06-26T19:00:00Z \
    --end   2023-06-26T20:00:00Z \
    --output "data/CONTROL/MULN_20230626_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230627_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-06-27 — already exists"
else
  echo "  fetch 2023-06-27"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-06-27T19:00:00Z \
    --end   2023-06-27T20:00:00Z \
    --output "data/CONTROL/MULN_20230627_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230814_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-08-14 — already exists"
else
  echo "  fetch 2023-08-14"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-08-14T19:00:00Z \
    --end   2023-08-14T20:00:00Z \
    --output "data/CONTROL/MULN_20230814_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230815_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-08-15 — already exists"
else
  echo "  fetch 2023-08-15"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-08-15T19:00:00Z \
    --end   2023-08-15T20:00:00Z \
    --output "data/CONTROL/MULN_20230815_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230911_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-09-11 — already exists"
else
  echo "  fetch 2023-09-11"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-09-11T19:00:00Z \
    --end   2023-09-11T20:00:00Z \
    --output "data/CONTROL/MULN_20230911_ctrl.dbn.zst" \
    --yes
fi

if [[ -f "data/CONTROL/MULN_20230912_ctrl.dbn.zst" ]]; then
  echo "  skip  2023-09-12 — already exists"
else
  echo "  fetch 2023-09-12"
  "$BINARY" databento-fetch \
    --symbols MULN \
    --start 2023-09-12T19:00:00Z \
    --end   2023-09-12T20:00:00Z \
    --output "data/CONTROL/MULN_20230912_ctrl.dbn.zst" \
    --yes
fi

echo "Done. Fetched closing-window data for all control dates."
