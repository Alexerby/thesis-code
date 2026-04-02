#!/usr/bin/bash
set -e

BUILD_DIR="build"
DIST_DIR="dist"
# Calculate 80% of available cores, or default to 2 for safety
TOTAL_CORES=$(nproc 2>/dev/null || echo 2)
SAFE_CORES=$(( TOTAL_CORES > 1 ? TOTAL_CORES * 80 / 100 : 1 ))

mkdir -p "$BUILD_DIR"
mkdir -p "$DIST_DIR"

cmake -S . -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "--- Building (Using $SAFE_CORES cores) ---"
cmake --build "$BUILD_DIR" -j "$SAFE_CORES" -- -l "$SAFE_CORES"

echo "--- Installing to $DIST_DIR ---"
rm -rf "$DIST_DIR"/*
cmake --install "$BUILD_DIR" --component runtime --prefix "$DIST_DIR"

if [ ! -L compile_commands.json ]; then
    ln -s "$BUILD_DIR/compile_commands.json" .
fi

echo "--- Done. Binary available in $DIST_DIR/thesis ---"
