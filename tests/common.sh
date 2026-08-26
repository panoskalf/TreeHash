#!/usr/bin/env bash
# Sourced by every script in tests/cases/. Provides fixture creation and
# assertion helpers so individual test cases stay short and declarative.
#
# Expects TREEHASH_BIN (path to the binary under test) and TESTS_DIR
# (this directory) to already be set - CTest supplies both per-test via
# the ENVIRONMENT property set in CMakeLists.txt.

set -uo pipefail

# Every path passed to register_cleanup is queued here and removed when
# this process exits - on success, on failure, or on Ctrl-C, since EXIT
# fires in all of those cases. Same idea as an RAII destructor.
_cleanup_dirs=()
_cleanup() {
    local dir
    for dir in "${_cleanup_dirs[@]:-}"; do
        [[ -n "$dir" ]] && rm -rf "$dir"
    done
}
trap _cleanup EXIT

register_cleanup() {
    _cleanup_dirs+=("$1")
}

# make_fixture: create a fresh, empty temp directory and print its path.
# mktemp guarantees a unique name each call, so tests never collide even
# if several were run concurrently.
#
# NOTE: this only creates the directory - it does NOT register it for
# cleanup. Callers must do `fixture=$(make_fixture); register_cleanup
# "$fixture"` themselves. That split looks redundant but it's load-bearing:
# `fixture=$(make_fixture)` runs make_fixture in a SUBSHELL (that's what
# $(...) does), so if make_fixture appended to _cleanup_dirs itself, that
# append would happen in the subshell's copy of the array and vanish the
# instant the subshell exits - the parent shell's array would never see
# it. register_cleanup has to be called directly, unsubstituted, in the
# same shell that owns _cleanup_dirs.
make_fixture() {
    mktemp -d
}

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_eq() {
    local expected="$1" actual="$2" msg="${3:-values differ}"
    [[ "$expected" == "$actual" ]] || fail "$msg
  expected: $expected
  actual:   $actual"
}

assert_exit_code() {
    local expected="$1" actual="$2" msg="${3:-unexpected exit code}"
    [[ "$expected" -eq "$actual" ]] || fail "$msg (expected exit $expected, got $actual)"
}

assert_contains() {
    local haystack="$1" needle="$2" msg="${3:-expected substring not found}"
    [[ "$haystack" == *"$needle"* ]] || fail "$msg
  needle:   $needle
  haystack: $haystack"
}
