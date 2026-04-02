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

FetchContent_MakeAvailable(databento glfw imgui)

find_package(OpenGL REQUIRED)

# Define the single main executable
add_executable(thesis
    src/app/main.cpp
    src/core/book.cpp
    src/core/market.cpp
    src/app/gui_application.cpp
    src/app/market_dashboard.cpp
    src/app/replay_controller.cpp
    src/feature_engineering/order_tracker.cpp
    src/feature_engineering/feature_engine.cpp
    tests/test_order_tracker.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Include paths
target_include_directories(thesis PRIVATE 
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
)

# Distribution
install(TARGETS thesis COMPONENT runtime DESTINATION .)
install(DIRECTORY scripts/ COMPONENT runtime DESTINATION scripts)
