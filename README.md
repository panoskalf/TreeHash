# TreeHash

![TreeHash Logo](assets/logo.jpg)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-blue.svg)](https://cmake.org)
[![Cross Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-brightgreen.svg)](#)

Recursive directory SHA256 calculator with parallel processing. Efficiently computes hashes for all files in a directory tree using multi-threaded, size-based load balancing.

## Features

- **Multi-threaded processing** with intelligent load balancing
- **Cross-platform compatibility** (Windows, Linux, macOS)
- Recursive directory traversal with efficient chunked file reading
- Automatically skips `.git` and `build` directories
- Clean directory-organized output with summary statistics
- Robust error handling with detailed feedback
- Memory efficient - processes large files in 64KB chunks

## Quick Start

**Build:**
```bash
cmake -S . -B build
cmake --build build
```

**Usage:**
```bash
./TreeHash /path/to/directory
```

**Example Output:**
```
Hardware concurrency: 8 threads
Starting thread 0
Starting thread 1
...
Directory: /home/user/project:
  main.cpp a1b2c3d4e5f6789a1b2c3d4e5f6789a1b2c3d4e5f6789a1b2c3d4e5f6789a
  README.md f7e8d9c6b5a4321ff7e8d9c6b5a4321ff7e8d9c6b5a4321ff7e8d9c6b5a4321f
...
=== Summary ===
Total files processed: 12
Successfully hashed: 12
```

## Requirements
C++17 compiler, CMake 3.20+. Dependencies fetched automatically.