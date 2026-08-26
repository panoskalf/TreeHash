#!/usr/bin/env bash
# Confirms --exclude adds directories to the default exclusion set, on top
# of (not instead of) the built-in .git/build defaults.
set -euo pipefail
source "$TESTS_DIR/common.sh"

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/.git" "$fixture/build" "$fixture/vendor" "$fixture/keep"
printf 'a\n' > "$fixture/.git/config"
printf 'b\n' > "$fixture/build/output.o"
printf 'c\n' > "$fixture/vendor/lib.c"
printf 'd\n' > "$fixture/keep/file.txt"

custom_out=$(cd "$fixture" && "$TREEHASH_BIN" . --exclude .git,build,vendor 2>/dev/null)
[[ "$custom_out" != *"vendor"* ]] || fail "vendor/ should be excluded via --exclude"
assert_contains "$custom_out" "./keep/file.txt" "keep/ should survive a custom --exclude list"
