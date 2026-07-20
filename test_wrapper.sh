#!/usr/bin/env bash
# Integration tests for msys2_wrappers.
# Assumes ucrt64_shell_wrapper.exe and msys_shell_wrapper.exe are in the
# same directory as this script and C:\msys64\usr\bin\bash.exe exists.

set -u

WRAPPER_DIR="$(cd "$(dirname "$0")" && pwd)"
MSYS2_ROOT="/c/msys64"
BASH="$MSYS2_ROOT/usr/bin/bash.exe"
UCRT64="$MSYS2_ROOT/ucrt64_shell_wrapper.exe"
CLANG64="$MSYS2_ROOT/clang64_shell_wrapper.exe"
MSYS="$MSYS2_ROOT/msys_shell_wrapper.exe"

# Deploy wrappers to where bash lives (they find bash relative to themselves)
cp "$WRAPPER_DIR/ucrt64_shell_wrapper.exe" "$MSYS2_ROOT/" 2>/dev/null
cp "$WRAPPER_DIR/clang64_shell_wrapper.exe" "$MSYS2_ROOT/" 2>/dev/null
cp "$WRAPPER_DIR/msys_shell_wrapper.exe" "$MSYS2_ROOT/" 2>/dev/null

PASS=0
FAIL=0

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

pass() { green "  PASS: $*"; ((PASS++)); }
fail() { red   "  FAIL: $*"; ((FAIL++)); }

check_exe() {
    [ -x "$1" ] || {
        red "ERROR: $1 not found or not executable"
        exit 1
    }
}

check_exe "$UCRT64"
check_exe "$CLANG64"
check_exe "$MSYS"

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

# Test 4: exit 0
"$UCRT64" -c 'exit 0' 2>/dev/null
[ $? -eq 0 ] && pass "exit 0" || fail "exit 0"

# Test 5: exit 42
"$UCRT64" -c 'exit 42' 2>/dev/null
[ $? -eq 42 ] && pass "exit 42" || fail "exit 42"

# Test 6: exit 255
"$UCRT64" -c 'exit 255' 2>/dev/null
[ $? -eq 255 ] && pass "exit 255" || fail "exit 255"

# ---------------------------------------------------------------------------
bold "=== Arguments passthrough ==="

# Test 7: -c with simple command
result=$("$UCRT64" -c 'echo hello from msys2')
[ "$result" = "hello from msys2" ] \
    && pass "-c simple echo" \
    || fail "-c simple echo: got '$result'"

# Test 8: arguments after -c (-- passthrough)
result=$("$UCRT64" -c 'for i in "$@"; do printf "[%s]\n" "$i"; done' \
    -- 'arg one' 'arg two' 2>/dev/null)
expected=$'[arg one]\n[arg two]'
[ "$result" = "$expected" ] \
    && pass "args after --" \
    || fail "args after --: got '$result'"

# Test 9: tricky quoting in arguments
result=$("$UCRT64" -c 'printf "[%s]\n" "$1"' -- 'a"b' 2>/dev/null)
expected=$'[a"b]'
[ "$result" = "$expected" ] \
    && pass "arg with double quote" \
    || fail "arg with double quote: got '$result'"

# Test 10: trailing backslash in argument
result=$("$UCRT64" -c 'printf "[%s]\n" "$1"' -- 'trail\' 2>/dev/null)
expected=$'[trail\\]'
[ "$result" = "$expected" ] \
    && pass "arg with trailing backslash" \
    || fail "arg with trailing backslash: got '$result'"

# Test 11: backslash-before-quote in argument
result=$("$UCRT64" -c 'printf "[%s]\n" "$1"' -- 'a\"b' 2>/dev/null)
expected=$'[a\\"b]'
[ "$result" = "$expected" ] \
    && pass "arg with backslash-before-quote" \
    || fail "arg with backslash-before-quote: got '$result'"

# Test 12: multiple tricky args
result=$("$UCRT64" -c 'for i in "$@"; do printf "[%s]\n" "$i"; done' -- \
    'simple' 'has space' 'a"b' 'trail\' 2>/dev/null)
expected=$'[simple]\n[has space]\n[a"b]\n[trail\\]'
[ "$result" = "$expected" ] \
    && pass "multiple tricky args" \
    || fail "multiple tricky args: got '$result'"

# ---------------------------------------------------------------------------
bold "=== Working directory ==="

# Test 13: CHERE_INVOKING - stays in current directory
result=$("$UCRT64" -c 'pwd' 2>/dev/null)
cyg_cwd=$(pwd)
[ "$result" = "$cyg_cwd" ] \
    && pass "stays in current directory" \
    || fail "stays in current directory: got '$result', expected '$cyg_cwd'"

# ---------------------------------------------------------------------------
bold "=== Error messages ==="

# Test 14: missing bash.exe
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

# ---------------------------------------------------------------------------
bold "=== Summary ==="
printf "\n%d passed, %d failed\n" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] && green "All tests passed!" || red "Some tests failed."
exit $FAIL
