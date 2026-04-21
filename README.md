# On Detection of Spoofing in High Frequency Trading
### Unsupervised Classification of Strategic Order Placement

[![CI](https://github.com/Alexerby/thesis-code/actions/workflows/ci.yml/badge.svg)](https://github.com/Alexerby/thesis-code/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/Alexerby/thesis-code/branch/main/graph/badge.svg)](https://codecov.io/gh/Alexerby/thesis-code)
[![Documentation](https://img.shields.io/badge/docs-online-blue.svg)](https://alexerby.github.io/thesis-code/)

*Second thesis (15 ECTS) submitted to the Department of Economics, Lund University, in candidacy for the degree of Master of Science in Economics.*

A C++ implementation of a spoofing detection framework for limit order book (LOB) markets. This project reconstructs full order lifecycles from raw L3/MBO data and extracts per-order features to separate strategic cancellations from genuine liquidity withdrawals.

![Market Visualizer](assets/REAMDE_SCREENSHOT_VISUALIZER.png)

[**Read the Documentation &raquo;**](https://alexerby.github.io/thesis-code/)

---

## Key Features

- **Order Life-Cycle Reconstruction**: Efficiently tracks individual orders from `Add` to `Fill` or `Cancel` using raw MBO (Market-By-Order) data.
- **Real-time Visualization**: OpenGL-based market visualizer for inspecting order book dynamics and liquidity clusters.
- **L3/MBO Support**: Native support for Databento's DBN format and XNAS.ITCH schema.
- **Performance-Oriented**: Core logic implemented in C++20.

## Requirements

| Dependency | Version |
|---|---|
| CMake | 3.24+ |
| C++ compiler | C++20 (GCC 13+) |
| zstd | system package |
| OpenGL | system package |

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake git gcc-13 g++-13 \
    libzstd-dev libgl1-mesa-dev mesa-common-dev \
    libx11-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libxkbcommon-dev
```

All other dependencies (Databento SDK, ImGui, GLFW, Catch2) are fetched automatically via CMake's `FetchContent`.

## Build & Install

First-time setup — configure CMake with GCC 13:

```bash
CXX=g++-13 CC=gcc-13 cmake -B build -DCMAKE_BUILD_TYPE=Release
```

Then use `make` for all subsequent build and install operations:

| Command | Description |
|---|---|
| `make` | Rebuild the binary |
| `make install` | Rebuild and install to `/opt/market-visualizer/`, add to PATH, create desktop entry |
| `make uninstall` | Remove the installed binary and desktop entry |

`make install` requires `sudo` once to write to `/opt/` and `/usr/local/bin/`. After installation, the visualizer is launchable from the app menu as **Market Visualizer** or from any terminal as `market-visualizer gui`.

The install prefix can be overridden:

```bash
make install PREFIX=/usr/local
```

## Detection Pipeline

The Python pipeline runs the full spoofing detection across all four court-confirmed MULN events (Case 1:23-cv-07613):

```bash
python scripts/pipeline.py            # use cached feature CSVs
python scripts/pipeline.py --reextract  # re-run C++ feature extraction
```

This produces per-event density plots, SHAP explanations, grid comparisons, and a formatted Excel workbook at `output/comparison.xlsx`.

## CLI Usage

The binary also supports direct CLI invocation:

```bash
market-visualizer <command> [args] [options]
```

| Command | Description |
|---|---|
| `gui` | Real-time order book visualizer — launches file picker if no path given |
| `describe` | Print file metadata and instrument ID → ticker map |
| `extract-features` | Run order tracking and write feature CSV |
| `databento-fetch` | Download historical MBO data from Databento |

### Examples

```bash
# Launch the visualizer with a file picker
market-visualizer gui

# Launch directly with a specific file
market-visualizer gui data/MANIPULATION_WINDOWS/MULN_20221025.dbn.zst

# Inspect file metadata
market-visualizer describe data/MANIPULATION_WINDOWS/MULN_20221025.dbn.zst

# Extract features for a ticker
market-visualizer extract-features data/MANIPULATION_WINDOWS/MULN_20221025.dbn.zst --ticker MULN --output output/MULN_20221025/FEATURES.csv
```

## Data

Market-By-Order (L3) data is sourced from [Databento](https://databento.com) in DBN format. The system is optimized for the `XNAS.ITCH` (NASDAQ TotalView-ITCH) schema.

### Downloading Data

1. Set your API key: `export DATABENTO_API_KEY=db-your-key-here`
2. Fetch data:

```bash
market-visualizer databento-fetch \
    --symbols AAPL,MSFT \
    --start 2026-03-18T00:00:00Z \
    --end   2026-03-19T00:00:00Z \
    --output ./data/sample.dbn.zst
```

## Testing

The project uses [Catch2](https://github.com/catchorg/Catch2) for unit testing.

```bash
cmake --build build --target unit_tests && ./build/unit_tests
```
