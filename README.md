# On Detection of Spoofing in High Frequency Trading
### Unsupervised Classification of Strategic Order Placement

*Second thesis (15 ECTS) submitted to the Department of Economics, Lund University, in candidacy for the degree of Master of Science in Economics.*

C++ implementation of an unsupervised spoofing detection framework for limit order book markets. The approach reconstructs full order lifecycles from raw L3/MBO data and applies a two-component Gaussian Mixture Model (GMM) to separate strategically-cancelled orders from genuine liquidity withdrawals — without requiring labelled training data.

---

## Implementation Progress

### Infrastructure

| | Component |
|---|---|
| ✅ | L3/MBO order book reconstruction |
| ✅ | Order lifecycle tracking |
| ✅ | Best bid/offer & imbalance calculation |
| ✅ | Market visualizer (GUI) |
| ✅ | Data streaming (DBN format) |

### Features

| | Feature |
|---|---|
| ☑️ | Order Age (Δt) |
| ❌ | Imbalance Change (ΔI) |
| ❌ | Order-Size Ratio |
| ❌ | Queue Position |
| ❌ | Distance from Touch |
| ❌ | Local Cancel Rate |

### Model

| | Component |
|---|---|
| ❌ | Gaussian Mixture Model (EM algorithm) |

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
./build/thesis <command> <data_path> --symbol <id>
```

If an invalid symbol ID is provided, the CLI lists all available IDs found in the data file.

### Commands

| Command | Description |
|---|---|
| `viz` | Real-time order book visualizer |
| `order_analyser` | Extract and analyse order lifecycles |

### Examples

```bash
# Launch the market visualizer
./build/thesis viz data/multi_instrument.dbn.zst --symbol 38

# Run order lifecycle analysis
./build/thesis order_analyser data/multi_instrument.dbn.zst --symbol 38
```

## Data

Market-By-Order (L3) data is sourced from [Databento](https://databento.com) in DBN format. The expected schema is `XNAS.ITCH` (NASDAQ TotalView). Data files are *not* included in this repository.
