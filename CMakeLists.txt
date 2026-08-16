cmake_minimum_required(VERSION 3.20)
project(TreeHash)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile commands for IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
include(Dependencies.cmake)

# Create executable
add_executable(TreeHash main.cpp)
target_link_libraries(TreeHash PRIVATE sha2::sha2 cxxopts::cxxopts)
target_compile_options(TreeHash PRIVATE -Wall -Wextra -Wpedantic)

# CLI tests: a plain bash suite (tests/run_tests.sh) driving the compiled
# binary, wired into ctest so `ctest --output-on-failure` is still the one
# command that runs everything, even with no C++ test framework underneath.
enable_testing()
add_test(NAME cli-tests COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_tests.sh $<TARGET_FILE:TreeHash>)