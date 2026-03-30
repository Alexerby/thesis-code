cmake_minimum_required(VERSION 3.24)
project(thesis)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DCMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    databento
    GIT_REPOSITORY https://github.com/databento/databento-cpp
    GIT_TAG v0.51.0
)


FetchContent_MakeAvailable(databento)

# This projects executables
add_executable(
    visualizer 
    src/visualizer_main.cpp
    src/metadata.cpp
    src/book.cpp
    src/market.cpp
    src/visualizer.cpp
)

add_executable(
    downloader
    src/downloader.cpp
)

add_executable(
    feature_exporter
    src/metadata.cpp
    src/book.cpp
    src/market.cpp
)

# Add include/ directory to the include path
target_include_directories(visualizer PRIVATE include)
target_include_directories(downloader PRIVATE include)
target_include_directories(feature_exporter PRIVATE include)

# Linking the library
target_link_libraries(visualizer PRIVATE databento::databento date::date)
target_link_libraries(downloader PRIVATE databento::databento)
target_link_libraries(feature_exporter PRIVATE databento::databento date::date)

# Set C++ standard to 17 or higher
target_compile_features(visualizer PRIVATE cxx_std_17)
target_compile_features(downloader PRIVATE cxx_std_17)
target_compile_features(feature_exporter PRIVATE cxx_std_17)


