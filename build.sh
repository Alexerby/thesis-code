#!/usr/bin/bash
set -e

BUILD_DIR="build"
DIST_DIR="dist"
# Cap jobs by both CPU count and available memory.
# Debug builds can use ~1.5 GB/job
TOTAL_CORES=$(nproc 2>/dev/null || echo 2)
CPU_JOBS=$(( TOTAL_CORES > 1 ? TOTAL_CORES * 80 / 100 : 1 ))
AVAIL_MEM_MB=$(free -m 2>/dev/null | awk '/^Mem:/{print $7}' || echo 3000)
MEM_JOBS=$(( AVAIL_MEM_MB / 1500 ))
MEM_JOBS=$(( MEM_JOBS < 1 ? 1 : MEM_JOBS ))
SAFE_CORES=$(( CPU_JOBS < MEM_JOBS ? CPU_JOBS : MEM_JOBS ))

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
