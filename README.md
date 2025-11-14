# TreeHash

![TreeHash Logo](assets/logo.jpg)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-blue.svg)](https://cmake.org)
[![Cross Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-brightgreen.svg)](#)

Recursive directory SHA256 calculator. Efficiently computes hashes for all files in a directory tree with organized output.
</div>

## Features

- **Cross-platform compatibility** (Windows, Linux, macOS)
- Recursive directory traversal with chunked processing
- Skips `.git` and `build` directories automatically
- Clean directory-organized output format
- Modern C++17 with robust error handling

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
Directory: /home/user/project:
  main.cpp a1b2c3d4e5f6789a...
  README.md f7e8d9c6b5a4321f...
Total directories: 3, Total files: 12
```

## Requirements
C++17 compiler, CMake 3.20+. Dependencies fetched automatically.