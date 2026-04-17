# Testing

Tests are written with [Catch2 v3](https://github.com/catchorg/Catch2) and live in `tests/`.

## Running tests

Use `run_tests.sh` from the repo root.

```bash
# Build and run tests (no coverage)
./run_tests.sh

# Build with gcov instrumentation, run tests, and generate an HTML coverage report
./run_tests.sh --coverage

# Same as above, and open the report in the default browser when done
./run_tests.sh --coverage --open
```

The coverage report is written to `coverage_report/index.html`.

## Running a subset of tests

The compiled test binary is `./build/unit_tests`.
Pass a Catch2 tag filter to run only matching tests:

```bash
# All tests tagged [order_tracker]
./build/unit_tests "[order_tracker]"

# Exclude slow tests
./build/unit_tests "~[slow]"

# List all available test cases
./build/unit_tests --list-tests
```

## Test files

| File | What it covers |
|------|----------------|
| `tests/test_order_tracker.cpp` | `OrderTracker`: add, zombie pruning, CSV dump, `CancelType` classification |

## Coverage

Coverage requires `lcov` and `genhtml` to be installed.

```bash
# Debian / Ubuntu
sudo apt install lcov
```

The `--coverage` flag in `run_tests.sh` passes `-DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug`
to CMake, which adds `--coverage` to both the compiler and linker flags for the
`unit_tests` target (see `CMakeLists.txt`).

After `genhtml` finishes, open `coverage_report/index.html` to browse line-level
coverage for every source file under `src/`.
