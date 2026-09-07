# CLAUDE.md

## What this is
TreeHash: single-binary C++17 CLI that recursively hashes files under a
directory with SHA-256, multi-threaded. Supports `--check` against a
manifest. Entire implementation is in `main.cpp`.

## Build & test
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

## How to work with me on this
- This is a learning/exploratory project. Explain the reasoning behind 
  non-trivial changes before making them.
- Keep it lean: near-zero-dependency single binary. No new dependencies,
  abstractions, or config without a clear reason.
- Suggest simplifications whenever you spot one, even unprompted.
- Keep code comments short — one line, only when the "why" isn't obvious
  from the code.