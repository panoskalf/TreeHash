#!/usr/bin/env bash
# TreeHash must fail loudly (non-zero exit, message on stderr) rather than
# silently "succeeding" when pointed at a path that doesn't exist - that's
# what makes it safe for a script to call and then check $?.
set -uo pipefail
source "$TESTS_DIR/common.sh"

# 2>&1 duplicates fd2 onto fd1's CURRENT target (the pipe $() is capturing)
# before 1>/dev/null retargets fd1 - so only stderr ends up in $stderr.
# Reverse the order and you'd capture nothing, since fd2 would still point
# at the original stdout instead of the pipe.
stderr=$("$TREEHASH_BIN" /this/path/does/not/exist/i/promise 2>&1 1>/dev/null)
status=$?

assert_exit_code 1 "$status" "hashing a nonexistent path should fail"
assert_contains "$stderr" "Error" "stderr should explain what went wrong"
