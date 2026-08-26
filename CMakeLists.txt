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

# CLI tests: a plain bash suite (tests/cases/*.sh, sourcing tests/common.sh)
# driving the compiled binary. Each case script is registered as its own
# CTest test (not one aggregate test) so `ctest -R <case-name>` and CTest's
# own pass/fail summary give per-case granularity.
enable_testing()
file(GLOB CLI_TEST_CASES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/cases/*.sh)
foreach(case_script ${CLI_TEST_CASES})
    get_filename_component(case_name ${case_script} NAME_WE)
    add_test(NAME ${case_name} COMMAND bash ${case_script})
    set_tests_properties(${case_name} PROPERTIES
        ENVIRONMENT "TREEHASH_BIN=$<TARGET_FILE:TreeHash>;TESTS_DIR=${CMAKE_SOURCE_DIR}/tests")
endforeach()