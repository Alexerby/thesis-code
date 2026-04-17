#!/bin/bash
set -e

COVERAGE=0
OPEN_REPORT=0

usage() {
  echo "Usage: $0 [--coverage [--open]]"
  echo ""
  echo "  (no flags)   Build and run unit tests normally"
  echo "  --coverage   Build with gcov instrumentation, run tests, generate lcov HTML report"
  echo "  --open       Open the HTML report in the browser after generating it (implies --coverage)"
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --coverage) COVERAGE=1; shift ;;
    --open)     COVERAGE=1; OPEN_REPORT=1; shift ;;
    -h|--help)  usage ;;
    *) echo "Unknown option: $1"; usage ;;
  esac
done

# --- Build ---
if [[ $COVERAGE -eq 1 ]]; then
  echo "Building with coverage instrumentation..."
  ./build.sh -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
else
  ./build.sh
fi

# --- Run tests ---
echo ""
echo "Running unit tests..."
./build/unit_tests

if [[ $COVERAGE -eq 0 ]]; then
  exit 0
fi

# --- Coverage report ---
REPORT_DIR="coverage_report"
LCOV_INFO="coverage.info"

echo ""
echo "Generating coverage report..."

# Capture coverage data
lcov --capture \
     --directory build \
     --output-file "$LCOV_INFO" \
     --quiet

# Strip external headers (system, Eigen, Catch2, matplot, etc.)
lcov --remove "$LCOV_INFO" \
     '/usr/*' \
     '*/build/_deps/*' \
     '*/tests/*' \
     --output-file "$LCOV_INFO" \
     --quiet

# Generate HTML
genhtml "$LCOV_INFO" \
        --output-directory "$REPORT_DIR" \
        --title "thesis unit-test coverage" \
        --quiet

echo "Coverage report written to $REPORT_DIR/index.html"

if [[ $OPEN_REPORT -eq 1 ]]; then
  if command -v xdg-open &>/dev/null; then
    xdg-open "$REPORT_DIR/index.html"
  elif command -v open &>/dev/null; then
    open "$REPORT_DIR/index.html"
  else
    echo "Cannot auto-open browser; open $REPORT_DIR/index.html manually."
  fi
fi
