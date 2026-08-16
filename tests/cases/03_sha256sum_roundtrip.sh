#!/usr/bin/env bash
# TreeHash's docs claim its stdout is sha256sum-compatible - this test is
# what actually proves it, by round-tripping a manifest through the real
# `sha256sum -c`, rather than just eyeballing that the format looks similar.
set -euo pipefail
source "$TESTS_DIR/common.sh"

command -v sha256sum >/dev/null || { echo "sha256sum not installed, skipping"; exit 0; }

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/sub"
printf 'one\n' > "$fixture/1.txt"
printf 'two\n' > "$fixture/sub/2.txt"

# The manifest MUST live outside the tree being hashed. `> manifest.sha256`
# is a shell redirection: the shell creates/truncates that file before
# TreeHash even starts running, so if it lived inside the fixture,
# TreeHash's own directory walk would see it mid-write, hash it as an
# empty file, and bake a now-wrong "./manifest.sha256" line into its own
# output - a self-referential false positive, not a real corruption. The
# same trap bites in real usage: `treehash . > manifest.txt` run FROM the
# directory being hashed has this exact problem.
manifest=$(mktemp)
register_cleanup "$manifest"

cd "$fixture"
"$TREEHASH_BIN" . > "$manifest" 2>/dev/null

# used as an `if` condition, so a non-zero exit here does NOT trigger set -e
if ! sha256sum -c "$manifest" --quiet; then
    fail "sha256sum -c should accept an unmodified TreeHash manifest"
fi

# now corrupt a file after the manifest was taken and confirm sha256sum -c
# actually notices - a manifest that always "passes" would be a false negative
printf 'tampered\n' > sub/2.txt

if sha256sum -c "$manifest" --quiet; then
    fail "sha256sum -c should have flagged the tampered file but reported OK"
fi
