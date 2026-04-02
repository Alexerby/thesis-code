#!/bin/bash
set -e

# Build the project
./build.sh

# Run unit tests
echo "Running Unit Tests..."
./build/unit_tests
