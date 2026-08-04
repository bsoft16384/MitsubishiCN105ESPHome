## CMakeLists.txt — CN105 unit tests (standalone, no ESPHome dependency)
## Role: Build system for the regression tests covering the pure CN105 protocol functions.
## Deps: CMake >= 3.14 (GoogleTest is fetched automatically via FetchContent)

cmake_minimum_required(VERSION 3.14)
project(cn105_unit_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── GoogleTest via FetchContent (portable: works in CI without apt install) ──
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Component source (relative path to components/cn105)
set(CN105_SRC_DIR "${CMAKE_SOURCE_DIR}/../../components/cn105")

# ESPHome stubs
include_directories(${CMAKE_SOURCE_DIR}/mocks)
# Component headers
include_directories(${CN105_SRC_DIR})

# --- Test executable ---
add_executable(cn105_tests
    test_checksum.cpp
    test_lookup_tables.cpp
    test_temperature.cpp
    test_types.cpp
    test_real_frames.cpp
    test_frame_parser.cpp
    test_protocol.cpp
    test_scheduling_policy.cpp
    test_decoder.cpp
    test_reconciliation.cpp
)

target_link_libraries(cn105_tests
    GTest::gtest
    GTest::gtest_main
)

# Test discovery for ctest
enable_testing()
include(GoogleTest)
gtest_discover_tests(cn105_tests)
