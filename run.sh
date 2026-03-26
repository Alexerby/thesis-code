#!/usr/bin/bash

set -e

mkdir -p build

# Configure
cmake -S . -B build

# Build
cmake --build build -j $(nproc || sysctl -n hw.ncpu)

# Create symlink for clangd
[ ! -L compile_commands.json ]&& ln -s build/compile_commands.json

# Run executable
./build/main

