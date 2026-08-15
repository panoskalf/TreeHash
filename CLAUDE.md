# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

TreeHash is a single-binary C++17 CLI tool that recursively walks a directory tree and computes the SHA-256 hash of every regular file, using multi-threaded, size-based load balancing across files. The entire implementation lives in `main.cpp`.

## Build

```bash
cmake -S . -B build
cmake --build build
```

The binary is produced at `build/TreeHash`. `CMAKE_EXPORT_COMPILE_COMMANDS` is enabled, so `build/compile_commands.json` is available for IntelliSense/clangd after configuring.

There is no separate lint or test suite/command in this repo — verify changes by building and running the binary against a sample directory (see Usage below).

## Dependencies

The only external dependency is [amosnier/sha-2](https://github.com/amosnier/sha-2) (a C SHA-256 implementation), fetched automatically at configure time via CMake `FetchContent` in `Dependencies.cmake`. That upstream repo has no `CMakeLists.txt` of its own, so `Dependencies.cmake` manually builds it into a static library and exposes it as the `sha2::sha2` target (C99 standard), which `TreeHash` links against.

## Usage

```bash
./build/TreeHash /path/to/directory
```

Output is grouped by directory, each file listed with its SHA-256 hex digest, followed by a summary count of files processed/hashed/failed.

## Architecture (`main.cpp`)

The program runs in four sequential stages:

1. **Discovery**: `std::filesystem::recursive_directory_iterator` walks the target path. `.git` and `build` directories are skipped via `disable_recursion_pending()`. Every regular file becomes a `FileInfo{path, size, hash=nullopt}` in a flat `std::vector<FileInfo>`.
2. **Parallel hashing** (`hashFilesParallel`): files are sorted largest-first, then greedily assigned to whichever worker thread currently has the least accumulated byte workload (min-workload bin packing, not a simple round-robin split) — this is the load-balancing strategy referenced in the README. Thread count is `min(hardware_concurrency(), file_count)`. Each thread hashes its assigned files in place, writing back into the shared `FileInfo` vector via pointers (safe because each file is only ever touched by one thread — no shared mutable state, no locking needed).
3. **Hashing a single file** (`calculateFileHash`): streams the file in `CHUNK_SIZE` (64 KiB) chunks through the `sha_256_*` C API from the sha2 library, so memory use stays flat regardless of file size. Returns `std::nullopt` on open/read failure rather than throwing.
4. **Reporting** (`printByDir`): buckets the flat file vector by parent directory into an `unordered_map` for display, then prints per-directory listings followed by summary totals.

`SHA256Hash` is a `std::array<uint8_t, 32>` with a custom `operator<<` overload that hex-encodes it for printing (careful to save/restore stream formatting flags/fill so it doesn't leak state into subsequent output).

Threads currently print progress (`Starting thread N`, `Thread N calculating hash for ...`) directly to `std::cout` from within each worker; this works today but is not synchronized with a mutex — keep that in mind if adding more concurrent output.
