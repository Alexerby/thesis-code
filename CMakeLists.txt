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

# Graphics & UI Dependencies
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.9
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.89.9
)

FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v0.16
)

# Testing Framework
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(databento glfw imgui implot catch2)

find_package(OpenGL REQUIRED)

# Shared source files (excluding mains)
set(COMMON_SOURCES
    src/data/book.cpp
    src/data/market.cpp
    src/app/gui_application.cpp
    src/app/dashboard.cpp
    src/app/replay_controller.cpp
    src/features/order_tracker.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

# Define the single main executable
add_executable(thesis
    src/app/main.cpp
    ${COMMON_SOURCES}
)

# Define the test executable
add_executable(unit_tests
    tests/test_order_tracker.cpp
    tests/test_book.cpp
    ${COMMON_SOURCES}
)

# Include paths
target_include_directories(thesis PRIVATE
    src
    .
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)
target_include_directories(unit_tests PRIVATE
    src
    .
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)

# Link targets
target_link_libraries(thesis
    PRIVATE
    databento::databento
    date::date
    glfw
    OpenGL::GL
)
target_link_libraries(unit_tests
    PRIVATE
    databento::databento
    date::date
    glfw
    OpenGL::GL
    Catch2::Catch2WithMain
)

# Distribution
install(TARGETS thesis COMPONENT runtime DESTINATION .)
install(DIRECTORY scripts/ COMPONENT runtime DESTINATION scripts)

# Coverage (Enable with -DENABLE_COVERAGE=ON)
option(ENABLE_COVERAGE "Build with gcov coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
  target_compile_options(unit_tests PRIVATE --coverage)
  target_link_options(unit_tests PRIVATE --coverage)
  target_compile_options(thesis PRIVATE --coverage)
  target_link_options(thesis PRIVATE --coverage)
endif()
