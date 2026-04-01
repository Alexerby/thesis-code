#!/usr/bin/bash
set -e

BUILD_DIR="build"
TEST_TARGET="order_tracker_tests"

if [ -f "$BUILD_DIR/$TEST_TARGET" ]; then
    echo "--- Running Tests: $TEST_TARGET ---"
    "./$BUILD_DIR/$TEST_TARGET"
else
    echo "Error: Test binary not found. Run ./build.sh first."
    exit 1
fi
