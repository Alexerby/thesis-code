#!/usr/bin/bash

set -e

TARGET="thesis"
BUILD_DIR="build"
CORES=4

mkdir -p "$BUILD_DIR"

cmake -S . -B "$BUILD_DIR"

echo "--- Building: $TARGET ---"

cmake --build "$BUILD_DIR" -j "$CORES" --target "$TARGET"

if [ ! -L compile_commands.json ]; then
    ln -s "$BUILD_DIR/compile_commands.json" .
fi

echo "--- Executing: $TARGET ---"
"./$BUILD_DIR/$TARGET" "$@"
