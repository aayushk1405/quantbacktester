cmake_minimum_required(VERSION 3.18)
project(quant_engine VERSION 1.0.0 LANGUAGES CXX)

# ─── Compiler settings ───────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release builds: full optimisation (-O3) + link-time optimisation.
# Use -DCMAKE_BUILD_TYPE=Debug for debug symbols.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -fsanitize=address,undefined")

# ─── Sources ─────────────────────────────────────────────────
set(ENGINE_SOURCES
    src/DataHandler.cpp
    src/Strategy.cpp
    src/Portfolio.cpp
    src/Backtest.cpp
)

# ─── Static library (used by tests and the CLI binary) ───────
add_library(quant_core STATIC ${ENGINE_SOURCES})
target_include_directories(quant_core PUBLIC include)

# ─── CLI binary ──────────────────────────────────────────────
#  Minimal harness: run a single backtest from the command line.
add_executable(quant_cli src/main_cli.cpp)
target_link_libraries(quant_cli PRIVATE quant_core)

# ─── pybind11 Python extension ───────────────────────────────
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# pybind11 is fetched via FetchContent so you don't need a
# system install.  Pin to a specific tag for reproducibility.
include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.11.1
)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(quant_engine src/bindings.cpp)
target_link_libraries(quant_engine PRIVATE quant_core)

# Install the .so to CMAKE_INSTALL_PREFIX (set via -DCMAKE_INSTALL_PREFIX=...).
#   • Local dev (scripts/build.sh):  prefix = ../api      → installs straight into api/
#   • Docker build (Dockerfile.api): prefix = /install    → copied out in stage 2
install(TARGETS quant_engine
        LIBRARY DESTINATION .
        RUNTIME DESTINATION .)

# ─── Tests ───────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
