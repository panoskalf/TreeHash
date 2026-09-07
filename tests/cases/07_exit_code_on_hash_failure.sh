#!/usr/bin/env bash
# A file that exists but can't be read must still fail the run: plain
# (non --check) mode must exit non-zero if any file failed to hash, not
# just print "Failed: 1" to stderr and exit 0 anyway.
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
printf 'ok\n' > "$fixture/a.txt"
printf 'unreadable\n' > "$fixture/b.txt"
chmod 000 "$fixture/b.txt"

"$TREEHASH_BIN" "$fixture" >/dev/null 2>/dev/null
status=$?

chmod 600 "$fixture/b.txt"

assert_exit_code 1 "$status" "a file that fails to hash should fail the whole run"
