# Thesis II 

This project processes market data using the Databento C++ client library.

## Requirements

### System Dependencies
- **CMake** (v3.24 or higher)
- **C++ Compiler** (supporting C++17 or higher, e.g., GCC 9+, Clang 10+)
- **libzstd-dev**: Required for Zstandard decompression used by Databento.
  ```bash
  sudo apt install libzstd-dev
  ```

### C++ Libraries
The following libraries are automatically managed via CMake's `FetchContent`:
- **databento-cpp** (v0.51.0): Market data client library.
- **date** (v3.0.4): Howard Hinnant's date/time library.

## Building the Project

Configure and Build
```bash
./run.sh
```
