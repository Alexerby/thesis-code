# Thesis Research: Market Data Analysis

C++ tools for analyzing and visualizing limit order book dynamics using Databento Market-By-Order (MBO) data.

## Requirements

* **CMake** (v3.24+)
* **C++ Compiler** (C++17+)
* **libzstd-dev**: `sudo apt install libzstd-dev`

## Build

```bash
mkdir build && cd build
cmake ..
make -j4
```

## Usage

Run the `thesis` executable from the `build` directory:

```bash
./thesis [command] [data_path] --symbol [id]
```

### Examples

**Market Visualizer:**
```bash
./build/thesis viz data/multi_instrument.dbn.zst --symbol 38
```

**Order Analysis:**
```bash
./build/thesis order_analyser data/multi_instrument.dbn.zst --symbol 38
```

*Note: If an invalid ID is provided, the CLI will list all available instrument IDs and symbols found in the data file.*
