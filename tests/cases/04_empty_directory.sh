#!/usr/bin/env bash
# Zero files is an edge case worth locking down: no threads to spawn, no
# manifest lines to print, but it must still exit 0 and stay quiet on stdout.
#
# No `set -e` here: we deliberately want to capture a possibly-non-zero
# exit code into a variable and assert on it, rather than let the script
# die the instant the command under test returns non-zero.
set -uo pipefail
source "$TESTS_DIR/common.sh"

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/empty_subdir"

stdout=$(cd "$fixture" && "$TREEHASH_BIN" . 2>/dev/null)
status=$?

assert_exit_code 0 "$status" "empty directory tree should exit 0"
assert_eq "" "$stdout" "empty directory tree should produce no manifest lines"
