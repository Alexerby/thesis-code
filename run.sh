#!/usr/bin/bash

set -e

# Available targets in CMakeLists.txt
AVAILABLE_TARGETS=("visualizer" "downloader" "feature_exporter")

usage() {
    echo "Usage: $0 [TARGET] [ARGS...]"
    echo ""
    echo "Available targets:"
    for target in "${AVAILABLE_TARGETS[@]}"; do
        echo "  - $target"
    done
    echo ""
    echo "Options:"
    echo "  -h, --help    Show this help message and exit"
    echo ""
    echo "Example:"
    echo "  $0 visualizer"
    echo "  $0 downloader <API_KEY>"
    exit 0
}

# Handle help flag
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    usage
fi

# Target selection logic
if [ -z "$1" ]; then
    echo "No target specified. Please select from the available targets:"
    PS3="Select a target (1-${#AVAILABLE_TARGETS[@]}): "
    select t in "${AVAILABLE_TARGETS[@]}"; do
        if [ -n "$t" ]; then
            TARGET=$t
            break
        else
            echo "Invalid selection. Please try again."
        fi
    done
    echo "Selected target: $TARGET"
else
    TARGET=$1
    # Validate provided target
    VALID=false
    for t in "${AVAILABLE_TARGETS[@]}"; do
        if [[ "$t" == "$TARGET" ]]; then
            VALID=true
            break
        fi
    done

    if [[ "$VALID" == false ]]; then
        echo "Error: Unknown target '$TARGET'"
        usage
    fi
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
