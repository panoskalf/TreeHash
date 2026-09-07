# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once the first release is tagged.

## [Unreleased]

### Added
- Recursive directory walk with multi-threaded, size-based load-balanced SHA-256 hashing.
- `sha256sum`-compatible output format, grouped by directory with a summary count.
- CLI parsing with configurable directory excludes (`--exclude`); `.git` and `build` excluded by default.
- CLI test suite (`tests/`) driving the compiled binary against fixture directories and golden manifests, wired into `ctest`.
- `.clang-format` style definition.
- Compiler warnings (`-Wall -Wextra -Wpedantic`) enabled on the `TreeHash` target.
- `-c, --check` flag to verify a directory tree against a previously generated manifest, `sha256sum -c`-compatible: prints `OK`/`FAILED`/`MISSING` per file (plus a `NOT IN MANIFEST` status of its own for files on disk absent from the manifest).
- Exits non-zero if any file fails to hash or check, in both plain and `--check` modes.
- Inaccessible files/directories (permission denied, deleted mid-scan) are skipped and reported to stderr instead of crashing the process; I/O error messages include the OS-level reason when the platform makes one available.
- Concurrent stderr output (verbose per-thread progress, per-file errors) is serialized so it can't interleave into garbled lines.
- Release builds are optimized (`-O3`) by default.
- CI: build + `ctest` matrix across Ubuntu/macOS/Windows, with `FetchContent` caching.
- CI: blocking `clang-tidy` job (`lint.yml`).

### Known limitations
- The OS-level reason in an I/O error message relies on `errno`, which the C++ standard doesn't guarantee an iostream failure sets on every platform — reliable on Linux (glibc), unverified on macOS/Windows. Bounded to fail safe rather than misleadingly: worst case is no reason given, never a wrong one.
- A permission-denied (or otherwise inaccessible) directory entry stops the entire remaining walk, not just the affected branch — any file or directory not yet visited at that point, anywhere in the tree, is skipped (traversal order isn't guaranteed, so which files make it in before the error occurs is unpredictable). A limitation of `recursive_directory_iterator`'s error model, not fixable without hand-rolling the recursion.
