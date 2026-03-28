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

# This projects executable
add_executable(
    main 
    src/main.cpp
    src/metadata.cpp
    src/book.cpp
    src/market.cpp
    src/visualizer.cpp
)

add_executable(
    downloader
    src/downloader.cpp
)

# Add include/ directory to the include path
target_include_directories(main PRIVATE include)
target_include_directories(downloader PRIVATE include)

# Linking the library
target_link_libraries(main PRIVATE databento::databento date::date)
target_link_libraries(downloader PRIVATE databento::databento)

# Set C++ standard to 17 or higher
target_compile_features(main PRIVATE cxx_std_17)
target_compile_features(downloader PRIVATE cxx_std_17)


