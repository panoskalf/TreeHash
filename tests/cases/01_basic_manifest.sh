#!/usr/bin/env bash
# Primary correctness net: run TreeHash against a small known tree and diff
# the stdout manifest against a checked-in golden file, byte for byte.
set -euo pipefail
source "$TESTS_DIR/common.sh"

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/sub/deeper"
printf 'hello\n'    > "$fixture/a.txt"
printf 'world\n'    > "$fixture/sub/b.txt"
printf 'treehash\n' > "$fixture/sub/deeper/c.txt"

# cd into the fixture and hash "." rather than the absolute path: TreeHash
# echoes back whatever path string it was given, so this is what keeps the
# output - and therefore the golden file - stable across runs even though
# mktemp gives every run a different, unpredictable absolute directory name.
actual=$(cd "$fixture" && "$TREEHASH_BIN" . 2>/dev/null)
expected=$(cat "$TESTS_DIR/golden/basic_manifest.txt")

diff <(echo "$expected") <(echo "$actual") || fail "manifest does not match golden file"
