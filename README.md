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

To configure, build, and run the default analysis:

```bash
chmod +x run.sh
./run.sh
```

The system will automatically process the data located in the `data/` directory and initialize the visual interface.
