# Market Data Analysis Framework

A high-performance C++ framework for analyzing and visualizing limit order book dynamics using the Databento Market-By-Order (MBO) dataset.

![Market Visualization](assets/readme_animation.gif)

## Overview

This repository provides tools for real-time replay, analysis, and visualization of high-frequency market data. The framework is designed to process granular MBO messages, maintaining a local state of multiple publisher-specific order books to derive global market signals.

## Requirements

### System Dependencies
*   **CMake** (v3.24 or higher)
*   **C++ Compiler** (C++17 or higher, e.g., GCC 9+, Clang 10+)
*   **libzstd-dev**: For Zstandard decompression.
    ```bash
    sudo apt install libzstd-dev
    ```

### Third-party Libraries
The following dependencies are integrated via CMake `FetchContent`:
*   **databento-cpp**: Official client library for Databento market data.
*   **date**: Howard Hinnant's date/time library for ISO-8601 formatting.

## Building and Usage

### Quick Start
To configure, build, and run one of the framework components:

```bash
chmod +x run.sh
./run.sh
```

If no target is specified, the script will prompt you to select from the available components (e.g., `visualizer`).

### Available Targets
You can use `run.sh` to run different components of the framework by providing the target name as an argument:

```bash
./run.sh visualizer  # Start the TUI market visualizer
./run.sh downloader <API_KEY>  # Fetch new data from Databento
./run.sh feature_exporter  # Export engineered features to CSV (WIP)
```

Alternatively, run without arguments to select interactively. Use `-h` or `--help` for more information.


Alternatively, you can set the `DATABENTO_API_KEY` environment variable:

```bash
export DATABENTO_API_KEY="your_key_here"
./run.sh downloader
```

The downloader will perform a cost analysis and ask for confirmation before incurring any charges.
