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
