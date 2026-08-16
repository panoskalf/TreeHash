#!/usr/bin/env bash
# Discovers and runs every tests/cases/*.sh script against the TreeHash
# binary, then reports a pass/fail summary. This script's own exit code is
# 0 only if every case passed - that single number is the whole contract
# CTest (and you, running it by hand) checks.
set -uo pipefail

# Resolve paths relative to this script's own location, not the caller's
# working directory - CTest invokes tests from the build directory, so a
# path like "tests/run_tests.sh" would break if we assumed anything about $PWD.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# The binary under test is passed as $1 by CTest (see CMakeLists.txt), but
# falls back to the conventional build/ location so this also runs by hand:
#   ./tests/run_tests.sh
treehash_bin="${1:-$SCRIPT_DIR/../build/TreeHash}"

if [[ ! -x "$treehash_bin" ]]; then
    echo "TreeHash binary not found or not executable: $treehash_bin" >&2
    exit 1
fi

# Resolve to an absolute path: every test case cd's into a fixture
# directory before invoking $TREEHASH_BIN, which would silently break a
# relative path like the "build/TreeHash" default above (the cd changes
# what it's relative TO). `cd ... && pwd` is the portable way to get an
# absolute directory - no dependency on GNU-only tools like `realpath`.
export TREEHASH_BIN="$(cd "$(dirname "$treehash_bin")" && pwd)/$(basename "$treehash_bin")"
export TESTS_DIR="$SCRIPT_DIR"

pass=0
fail=0

for case_script in "$SCRIPT_DIR"/cases/*.sh; do
    name=$(basename "$case_script")
    # each case runs as its own bash process, so one case's `set -e` or a
    # stray exit can't take down the whole suite
    if output=$(bash "$case_script" 2>&1); then
        echo "PASS  $name"
        pass=$((pass + 1))
    else
        echo "FAIL  $name"
        echo "$output" | sed 's/^/      /'
        fail=$((fail + 1))
    fi
done

echo
echo "$pass passed, $fail failed"

# the last statement's exit status becomes this script's exit status - the
# one thing CTest actually looks at to decide PASS/FAIL for "cli-tests"
[[ "$fail" -eq 0 ]]
