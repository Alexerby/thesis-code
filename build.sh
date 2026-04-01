#!/usr/bin/bash
set -e

BUILD_DIR="build"
# Calculate 80% of available cores, or default to 2 for safety
TOTAL_CORES=$(nproc 2>/dev/null || echo 2)
SAFE_CORES=$(( TOTAL_CORES > 1 ? TOTAL_CORES * 80 / 100 : 1 ))

mkdir -p "$BUILD_DIR"

cmake -S . -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "--- Building All Targets (Using $SAFE_CORES cores) ---"

# Use --load-average to stop starting new jobs if the system is struggling
cmake --build "$BUILD_DIR" -j "$SAFE_CORES" -- -l "$SAFE_CORES"

if [ ! -L compile_commands.json ]; then
    ln -s "$BUILD_DIR/compile_commands.json" .
fi
