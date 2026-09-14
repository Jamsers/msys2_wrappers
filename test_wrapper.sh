#!/usr/bin/env bash
# Integration tests for msys2_wrappers.
# Expects the built wrapper exes in the same directory as this script.
# MSYS2_ROOT may be overridden:  MSYS2_ROOT=/d/msys64 bash test_wrapper.sh
#
# NOTE: the wrappers need bash.exe at <wrapper-dir>/usr/bin/bash.exe, so
# tests either run them from the install root (copies there first) or via
# a symlink farm. Copying intentionally overwrites the installed wrappers
# with the just-built ones under test.

set -u

WRAPPER_DIR="$(cd "$(dirname "$0")" && pwd)"
MSYS2_ROOT="${MSYS2_ROOT:-/c/msys64}"
BASH="$MSYS2_ROOT/usr/bin/bash.exe"
UCRT64="$MSYS2_ROOT/ucrt64_shell_wrapper.exe"
CLANG64="$MSYS2_ROOT/clang64_shell_wrapper.exe"
MSYS="$MSYS2_ROOT/msys_shell_wrapper.exe"

# Deploy wrappers to where bash lives (they find bash relative to themselves).
# Checked: never silently test a stale installed copy.
cp "$WRAPPER_DIR/ucrt64_shell_wrapper.exe" "$MSYS2_ROOT/" || {
    echo "ERROR: cannot copy ucrt64_shell_wrapper.exe to $MSYS2_ROOT/" >&2
    exit 1
}
cp "$WRAPPER_DIR/clang64_shell_wrapper.exe" "$MSYS2_ROOT/" || {
    echo "ERROR: cannot copy clang64_shell_wrapper.exe to $MSYS2_ROOT/" >&2
    exit 1
}
cp "$WRAPPER_DIR/msys_shell_wrapper.exe" "$MSYS2_ROOT/" || {
    echo "ERROR: cannot copy msys_shell_wrapper.exe to $MSYS2_ROOT/" >&2
    exit 1
}

PASS=0
FAIL=0

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

pass() { green "  PASS: $*"; ((++PASS)); }
fail() { red   "  FAIL: $*"; ((++FAIL)); }

check_exe() {
    [ -x "$1" ] || {
        red "ERROR: $1 not found or not executable"
        exit 1
    }
}

check_exe "$BASH"
check_exe "$UCRT64"
check_exe "$CLANG64"
check_exe "$MSYS"

# Compare wrapper passthrough against direct bash for the same argv.
# Guard: direct bash MUST deliver argv byte-identical (proving the argv itself
# is valid). Only then is wrapper-vs-direct compared.
check_passthrough() {
    local desc="$1"; shift
    local wrapped direct
    direct=$("$BASH" --login -c 'printf "[%s]" "$@"' "$@" 2>&1)
    wrapped=$("$UCRT64" -c 'printf "[%s]" "$@"' "$@" 2>&1)
    if [ "$wrapped" = "$direct" ]; then
        pass "$desc"
    else
        fail "$desc: wrapper='$wrapped' direct='$direct'"
    fi
}

# ---------------------------------------------------------------------------
bold "=== Environment ==="

# Test 1: MSYSTEM=UCRT64
result=$("$UCRT64" -c 'echo "$MSYSTEM"')
if [ "$result" = "UCRT64" ]; then
    pass "UCRT64: MSYSTEM=UCRT64"
else
    fail "UCRT64: got MSYSTEM='$result', expected 'UCRT64'"
fi

# Test 2: MSYSTEM=CLANG64
result=$("$CLANG64" -c 'echo "$MSYSTEM"')
if [ "$result" = "CLANG64" ]; then
    pass "CLANG64: MSYSTEM=CLANG64"
else
    fail "CLANG64: got MSYSTEM='$result', expected 'CLANG64'"
fi

# Test 3: MSYSTEM=MSYS
result=$("$MSYS" -c 'echo "$MSYSTEM"')
if [ "$result" = "MSYS" ]; then
    pass "MSYS:   MSYSTEM=MSYS"
else
    fail "MSYS:   got MSYSTEM='$result', expected 'MSYS'"
fi

# Test 4: PATH includes each environment's bin directory
result=$("$UCRT64" -c 'echo "$PATH"' | tr ':' '\n' | grep -c '/ucrt64/bin' || true)
if [ "$result" -ge 1 ]; then
    pass "UCRT64: PATH includes /ucrt64/bin"
else
    fail "UCRT64: PATH missing /ucrt64/bin"
fi

result=$("$CLANG64" -c 'echo "$PATH"' | tr ':' '\n' | grep -c '/clang64/bin' || true)
if [ "$result" -ge 1 ]; then
    pass "CLANG64: PATH includes /clang64/bin"
else
    fail "CLANG64: PATH missing /clang64/bin"
fi

# ---------------------------------------------------------------------------
bold "=== Exit codes ==="

# Test 5: exit 0
"$UCRT64" -c 'exit 0' 2>/dev/null
[ $? -eq 0 ] && pass "exit 0" || fail "exit 0"

# Test 6: exit 42
"$UCRT64" -c 'exit 42' 2>/dev/null
[ $? -eq 42 ] && pass "exit 42" || fail "exit 42"

# Test 7: exit 255
"$UCRT64" -c 'exit 255' 2>/dev/null
[ $? -eq 255 ] && pass "exit 255" || fail "exit 255"

# ---------------------------------------------------------------------------
bold "=== Arguments passthrough ==="

# Test 8: -c with simple command
result=$("$UCRT64" -c 'echo hello from msys2')
[ "$result" = "hello from msys2" ] \
    && pass "-c simple echo" \
    || fail "-c simple echo: got '$result'"

# Test 9: arguments after -c (-- passthrough)
result=$("$UCRT64" -c 'for i in "$@"; do printf "[%s]\n" "$i"; done' \
    -- 'arg one' 'arg two' 2>/dev/null)
expected=$'[arg one]\n[arg two]'
[ "$result" = "$expected" ] \
    && pass "args after --" \
    || fail "args after --: got '$result'"

# Test 10: tricky quoting in arguments
result=$("$UCRT64" -c 'printf "[%s]\n" "$1"' -- 'a"b' 2>/dev/null)
expected=$'[a"b]'
[ "$result" = "$expected" ] \
    && pass "arg with double quote" \
    || fail "arg with double quote: got '$result'"

# Test 11: trailing backslash in argument
result=$("$UCRT64" -c 'printf "[%s]\n" "$1"' -- 'trail\' 2>/dev/null)
expected=$'[trail\\]'
[ "$result" = "$expected" ] \
    && pass "arg with trailing backslash" \
    || fail "arg with trailing backslash: got '$result'"

# Test 12: backslash-before-quote in argument
result=$("$UCRT64" -c 'printf "[%s]\n" "$1"' -- 'a\"b' 2>/dev/null)
expected=$'[a\\"b]'
[ "$result" = "$expected" ] \
    && pass "arg with backslash-before-quote" \
    || fail "arg with backslash-before-quote: got '$result'"

# Test 13: multiple tricky args
result=$("$UCRT64" -c 'for i in "$@"; do printf "[%s]\n" "$i"; done' -- \
    'simple' 'has space' 'a"b' 'trail\' 2>/dev/null)
expected=$'[simple]\n[has space]\n[a"b]\n[trail\\]'
[ "$result" = "$expected" ] \
    && pass "multiple tricky args" \
    || fail "multiple tricky args: got '$result'"

# Test 14: Cygwin-sensitive args must pass through unmangled.
# Run in a scratch dir with glob-matching files so a glob-expansion
# regression is caught (unquoted *?[] would expand to file names).
GLOB_DIR=$(mktemp -d)
touch "$GLOB_DIR/a.txt" "$GLOB_DIR/b.txt" "$GLOB_DIR/axb" \
      "$GLOB_DIR/a" "$GLOB_DIR/b"
pushd "$GLOB_DIR" >/dev/null
check_passthrough "glob star passes through" '*.txt'
check_passthrough "glob question mark passes through" 'a?b'
check_passthrough "glob brackets pass through" '[ab]'
check_passthrough "single quote passes through" "it's"
check_passthrough "tilde passes through" '~'
check_passthrough "tab passes through" "$(printf 'a\tb')"
check_passthrough "newline passes through" "$(printf 'a\nb')"
check_passthrough "unicode passes through" 'héllo-日本語'
popd >/dev/null
rm -rf "$GLOB_DIR"

# Test 15: wrapper path with spaces (symlink, avoids copying bash tree)
SPACED_BASE=$(mktemp -d)/"dir with spaces"
mkdir -p "$SPACED_BASE/usr/bin"
ln -s "$BASH" "$SPACED_BASE/usr/bin/bash.exe"
ln -s "$UCRT64" "$SPACED_BASE/ucrt64_shell_wrapper.exe"
result=$("$SPACED_BASE/ucrt64_shell_wrapper.exe" -c 'echo "$MSYSTEM"')
if [ "$result" = "UCRT64" ]; then
    pass "wrapper path with spaces"
else
    fail "wrapper path with spaces: got '$result'"
fi
rm -rf "$(dirname "$SPACED_BASE")"

# ---------------------------------------------------------------------------
bold "=== Working directory ==="

# Test 16: CHERE_INVOKING - stays in current directory.
# Must use a non-$HOME dir: the missing-CHERE fallback cds to $HOME,
# which would make a $HOME-based test vacuous.
CWD_DIR=$(mktemp -d)
pushd "$CWD_DIR" >/dev/null
result=$("$UCRT64" -c 'pwd' 2>/dev/null)
if [ "$result" = "$CWD_DIR" ]; then
    pass "stays in current directory"
else
    fail "stays in current directory: got '$result', expected '$CWD_DIR'"
fi
popd >/dev/null
rmdir "$CWD_DIR"

# ---------------------------------------------------------------------------
bold "=== Error messages ==="

# Test 17: missing bash.exe
MISSING_DIR=$(mktemp -d)
cp "$UCRT64" "$MISSING_DIR/"
stderr=$("$MISSING_DIR/ucrt64_shell_wrapper.exe" -c 'echo' 2>&1 1>/dev/null)
exitcode=$?
rm -rf "$MISSING_DIR"
if [ $exitcode -ne 0 ] && echo "$stderr" | grep -q "not found"; then
    pass "error message on missing bash"
else
    fail "error message on missing bash: exit=$exitcode stderr='$stderr'"
fi

# Test 18: directory at bash path is also rejected
DIRBAIT=$(mktemp -d)
mkdir -p "$DIRBAIT/usr/bin/bash.exe"
cp "$UCRT64" "$DIRBAIT/"
stderr=$("$DIRBAIT/ucrt64_shell_wrapper.exe" -c 'echo' 2>&1 1>/dev/null)
exitcode=$?
rm -rf "$DIRBAIT"
if [ $exitcode -ne 0 ] && echo "$stderr" | grep -q "not found"; then
    pass "directory at bash path rejected"
else
    fail "directory at bash path: exit=$exitcode stderr='$stderr'"
fi

# ---------------------------------------------------------------------------
bold "=== Summary ==="
printf "\n%d passed, %d failed\n" "$PASS" "$FAIL"
if [ "$FAIL" -eq 0 ]; then
    green "All tests passed!"
    exit 0
else
    red "Some tests failed."
    exit 1
fi
