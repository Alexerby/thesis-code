# On Detection of Spoofing in High Frequency Trading
### Unsupervised Classification of Strategic Order Placement

[![CI](https://github.com/Alexerby/thesis-code/actions/workflows/ci.yml/badge.svg)](https://github.com/Alexerby/thesis-code/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/Alexerby/thesis-code/branch/main/graph/badge.svg)](https://codecov.io/gh/Alexerby/thesis-code)

*Second thesis (15 ECTS) submitted to the Department of Economics, Lund University, in candidacy for the degree of Master of Science in Economics.*

C++ implementation of an unsupervised spoofing detection framework for limit order book markets. The approach reconstructs full order lifecycles from raw L3/MBO data and applies a two-component Gaussian Mixture Model (GMM) to separate strategically-cancelled orders from genuine liquidity withdrawals, without requiring labelled training data.

---

## Requirements

| Dependency | Version |
|---|---|
| CMake | 3.24+ |
| C++ compiler | C++17+ |
| zstd | system package |
| OpenGL | system package |
| GnuPlot | system package |

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake git \
    libzstd-dev libgl1-mesa-dev mesa-common-dev \
    libx11-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libxkbcommon-dev
```

All other dependencies (Databento SDK, ImGui, GLFW, Eigen3, Catch2) are fetched automatically by CMake.

## Build

```bash
chmod +x ./build.sh \
./build.sh
```

Debug mode:
```bash
chmod +x ./build.sh \
./build.sh -DCMAKE_BUILD_TYPE=Debug
```
Building in Debug mode will add symbol flags for tools like GDB.



## Usage

```bash
./dist/thesis <command> [args] [options]
```

### Commands

| Command | Description |
|---|---|
| `gui <data_path>` | Real-time order book visualizer (OpenGL) |
| `plot <data_path>` | Plot feature distributions (saves PNGs) |
| `model <data_path>` | Run order tracking + GMM analysis |
| `databento-fetch` | Fetch historical MBO data from Databento |

Pass `--symbol <id>` to `gui`, `plot`, and `model` to focus on a specific instrument ID. If omitted, the first instrument in the file is used.

### Examples

```bash
# Launch the market visualizer
./dist/thesis gui data/multi_instrument.dbn.zst --symbol 38

# Plot feature distributions
./dist/thesis plot data/multi_instrument.dbn.zst --symbol 38

# Run GMM analysis
./dist/thesis model data/multi_instrument.dbn.zst --symbol 38
```

## Data

Market-By-Order (L3) data is sourced from [Databento](https://databento.com) in DBN format. The expected schema is `XNAS.ITCH` (NASDAQ TotalView-ITCH). Data files are *not* included in this repository.

### Downloading data

Set your API key via environment variable (recommended):

```bash
export DATABENTO_API_KEY=db-your-key-here
```

Then run the fetcher. It prints estimated cost and billable size before asking for confirmation, type `N` to do a dry-run cost check without downloading anything.

```bash
./dist/thesis databento-fetch \
    --dataset XNAS.ITCH \
    --symbols AAPL,MSFT,AMZN,NVDA,GOOGL \
    --start 2026-03-18T00:00:00Z \
    --end   2026-03-19T00:00:00Z \
    --output ./data/march18.dbn.zst
```

The API key can also be passed directly with `--key <key>`. All flags except `--key` are required; `--dataset` defaults to `XNAS.ITCH` if omitted.

## Known issues
- Note: Initialisation of EM-algo not complete, [Issue #2](https://github.com/Alexerby/thesis-code/issues/2).
