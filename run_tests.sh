#!/usr/bin/bash
set -e

THESIS_BIN="./dist/thesis"

if [ -f "$THESIS_BIN" ]; then
    echo "--- Running Integrated Tests ---"
    "$THESIS_BIN" test
else
    echo "Error: Thesis binary not found in dist/. Run ./build.sh first."
    exit 1
fi
