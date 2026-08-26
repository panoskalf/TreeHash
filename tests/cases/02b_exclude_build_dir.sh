#!/usr/bin/env bash
# Confirms build/ is excluded by default, while other directories still appear.
set -euo pipefail
source "$TESTS_DIR/common.sh"

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/build" "$fixture/vendor" "$fixture/keep"
printf 'b\n' > "$fixture/build/output.o"
printf 'c\n' > "$fixture/vendor/lib.c"
printf 'd\n' > "$fixture/keep/file.txt"

default_out=$(cd "$fixture" && "$TREEHASH_BIN" . 2>/dev/null)
[[ "$default_out" != *"/build/"* ]] || fail "build/ contents leaked into output by default"
assert_contains "$default_out" "./vendor/lib.c" "vendor/ should be hashed when not excluded"
