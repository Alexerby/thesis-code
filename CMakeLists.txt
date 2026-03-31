cmake_minimum_required(VERSION 3.24)
project(thesis)

# Global settings
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# External Dependencies
include(FetchContent)
FetchContent_Declare(
    databento
    GIT_REPOSITORY https://github.com/databento/databento-cpp
    GIT_TAG v0.51.0
)
FetchContent_MakeAvailable(databento)

# Define the single main executable
# Include main.cpp and all supporting implementation files
add_executable(thesis
    src/main.cpp
    src/book.cpp
    src/market.cpp
    src/visualizer.cpp
    src/order_tracker.cpp
)

# Include paths
target_include_directories(thesis PRIVATE include)

# Linking
target_link_libraries(thesis 
    PRIVATE 
    databento::databento 
    date::date
)
