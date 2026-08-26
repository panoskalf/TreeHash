#!/usr/bin/env bash
# Confirms .git is excluded by default, while other directories still appear.
set -euo pipefail
source "$TESTS_DIR/common.sh"

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/.git" "$fixture/keep"
printf 'a\n' > "$fixture/.git/config"
printf 'd\n' > "$fixture/keep/file.txt"

default_out=$(cd "$fixture" && "$TREEHASH_BIN" . 2>/dev/null)
assert_contains "$default_out" "./keep/file.txt" "keep/ should always be hashed"
[[ "$default_out" != *".git"* ]] || fail ".git contents leaked into output by default"
