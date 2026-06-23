cmake_minimum_required(VERSION 3.18)

# ── Fetch GoogleTest ──────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# ── Test executable ───────────────────────────────────────────
add_executable(quant_tests
    test_indicators.cpp
    test_portfolio.cpp
    test_datahandler.cpp
    test_backtest_integration.cpp
)

target_link_libraries(quant_tests
    PRIVATE
    quant_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(quant_tests)
