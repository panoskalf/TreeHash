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