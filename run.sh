#!/usr/bin/bash

set -e

# Configuration: Default to 'main' if no target is provided
TARGET=${1:-main}

# Shift the arguments so that $1 becomes the first argument meant for the executable
if [ $# -gt 0 ]; then
    shift
fi

mkdir -p build

# Configure project
cmake -S . -B build

# Build the specific target
echo "--- Building: $TARGET ---"
cmake --build build -j $(nproc || sysctl -n hw.ncpu) --target "$TARGET"

# Maintain clangd support
[ ! -L compile_commands.json ] && ln -s build/compile_commands.json

# Execute the target with any remaining arguments
echo "--- Executing: $TARGET ---"
"./build/$TARGET" "$@"
