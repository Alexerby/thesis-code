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

# Testing Framework
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)

# Eigen (Linear Algebra)
FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
)

FetchContent_MakeAvailable(databento glfw imgui catch2 eigen)

find_package(OpenGL REQUIRED)

# Shared source files (excluding mains)
set(COMMON_SOURCES
    src/data/book.cpp
    src/data/market.cpp
    src/app/gui_application.cpp
    src/app/dashboard.cpp
    src/app/replay_controller.cpp
    src/features/order_tracker.cpp
    src/model/gmm.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Define the single main executable
add_executable(thesis
    src/app/main.cpp
    ${COMMON_SOURCES}
)

# Define the test executable
add_executable(unit_tests
    tests/test_order_tracker.cpp
    ${COMMON_SOURCES}
)

# Include paths
target_include_directories(thesis PRIVATE 
    src
    .
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_include_directories(unit_tests PRIVATE 
    src
    .
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# Link targets
target_link_libraries(thesis
    PRIVATE
    databento::databento
    date::date
    glfw
    OpenGL::GL
    Eigen3::Eigen
)
target_link_libraries(unit_tests
    PRIVATE
    databento::databento
    date::date
    glfw
    OpenGL::GL
    Catch2::Catch2WithMain
    Eigen3::Eigen
)

# Distribution
install(TARGETS thesis COMPONENT runtime DESTINATION .)
install(DIRECTORY scripts/ COMPONENT runtime DESTINATION scripts)
