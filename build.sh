#!/usr/bin/bash
set -e

BUILD_DIR="build"
CORES=$(nproc 2>/dev/null || echo 4)

mkdir -p "$BUILD_DIR"

# Generate build files
cmake -S . -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "--- Building All Targets ---"

cmake --build "$BUILD_DIR" -j "$CORES"

if [ ! -L compile_commands.json ]; then
    ln -s "$BUILD_DIR/compile_commands.json" .
fi
