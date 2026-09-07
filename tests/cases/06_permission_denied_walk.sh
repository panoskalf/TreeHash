#!/usr/bin/env bash
# A permission-denied subdirectory encountered mid-walk must not crash the
# process (regression test for a std::terminate() abort - see CHANGELOG),
# and must report the run as failed via a non-zero exit code, since the
# tree wasn't fully covered.
#
# Fixture note: recursive_directory_iterator doesn't guarantee traversal
# order, so which sibling gets visited before the error is undefined -
# this only asserts what's actually guaranteed (no crash, correct exit
# code, an explanatory stderr message), not which specific files made it
# into the output.
#
# No `set -e`: capturing a non-zero exit code into a variable is the point.
set -uo pipefail
source "$TESTS_DIR/common.sh"

if [[ "$(id -u)" -eq 0 ]]; then
    echo "SKIP: running as root, chmod 000 doesn't block access" >&2
    exit 0
fi

fixture=$(make_fixture)
register_cleanup "$fixture"
mkdir -p "$fixture/ok" "$fixture/noaccess"
printf 'reachable\n' > "$fixture/ok/a.txt"
printf 'unreachable\n' > "$fixture/noaccess/b.txt"
chmod 000 "$fixture/noaccess"

stderr=$(cd "$fixture" && "$TREEHASH_BIN" . 2>&1 1>/dev/null)
status=$?

# restore permissions so register_cleanup's `rm -rf` can actually delete it -
# GNU rm can't recurse into a mode-000 directory even when it owns it
chmod 700 "$fixture/noaccess"

assert_exit_code 1 "$status" "a permission-denied subdirectory should fail the run, not crash or exit 0"
assert_contains "$stderr" "Permission denied" "stderr should explain what went wrong"
