#!/usr/bin/env bash
# Confirms directories named via --exclude (and the built-in .git/build
# defaults) never appear in the output, while everything else still does.
set -euo pipefail
source "$TESTS_DIR/common.sh"

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/.git" "$fixture/build" "$fixture/vendor" "$fixture/keep"
printf 'a\n' > "$fixture/.git/config"
printf 'b\n' > "$fixture/build/output.o"
printf 'c\n' > "$fixture/vendor/lib.c"
printf 'd\n' > "$fixture/keep/file.txt"

# default excludes: .git and build should be skipped with no flags at all
default_out=$(cd "$fixture" && "$TREEHASH_BIN" . 2>/dev/null)
assert_contains "$default_out" "./keep/file.txt" "keep/ should always be hashed"
[[ "$default_out" != *".git"* ]] || fail ".git contents leaked into output by default"
[[ "$default_out" != *"/build/"* ]] || fail "build/ contents leaked into output by default"
assert_contains "$default_out" "./vendor/lib.c" "vendor/ should be hashed when not excluded"

# custom exclude list: vendor should now also disappear, on top of the defaults
custom_out=$(cd "$fixture" && "$TREEHASH_BIN" . --exclude .git,build,vendor 2>/dev/null)
[[ "$custom_out" != *"vendor"* ]] || fail "vendor/ should be excluded via --exclude"
assert_contains "$custom_out" "./keep/file.txt" "keep/ should survive a custom --exclude list"
