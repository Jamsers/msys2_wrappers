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

# Temp dirs created by tests; cleaned up on exit (success or abort).
GLOB_DIR=""; SPACED_TMP=""; CWD_DIR=""; MISSING_DIR=""; DIRBAIT=""
cleanup() {
    [ -n "$GLOB_DIR" ] && rm -rf "$GLOB_DIR"
    [ -n "$SPACED_TMP" ] && rm -rf "$SPACED_TMP"
    [ -n "$CWD_DIR" ] && rmdir "$CWD_DIR" 2>/dev/null
    [ -n "$MISSING_DIR" ] && rm -rf "$MISSING_DIR"
    [ -n "$DIRBAIT" ] && rm -rf "$DIRBAIT"
}
trap cleanup EXIT

deploy_failed() {
    echo "ERROR: cannot copy $1 to $MSYS2_ROOT/" >&2
    echo "HINT: if the error is 'Device or resource busy', a running process is" >&2
    echo "holding the installed wrapper (e.g. this very shell was launched via it)." >&2
    echo "Close shells/IDEs using the wrappers and retry, or test against a copy" >&2
    echo "of the MSYS2 tree:  MSYS2_ROOT=/path/to/copy bash test_wrapper.sh" >&2
    exit 1
}

# Deploy wrappers to where bash lives (they find bash relative to themselves).
# Checked: never silently test a stale installed copy.
cp "$WRAPPER_DIR/ucrt64_shell_wrapper.exe" "$MSYS2_ROOT/" \
    || deploy_failed ucrt64_shell_wrapper.exe
cp "$WRAPPER_DIR/clang64_shell_wrapper.exe" "$MSYS2_ROOT/" \
    || deploy_failed clang64_shell_wrapper.exe
cp "$WRAPPER_DIR/msys_shell_wrapper.exe" "$MSYS2_ROOT/" \
    || deploy_failed msys_shell_wrapper.exe

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
# The `_` is the $0 placeholder: with `bash -c CMD`, the first arg after
# the command string becomes $0 (NOT $@), so omitting it makes both sides
# print nothing and the comparison vacuous. The guard then asserts the
# direct run actually delivered the arg before trusting the comparison.
check_passthrough() {
    local desc="$1"; shift
    local wrapped direct expected
    expected=$(printf '[%s]' "$@")
    direct=$("$BASH" --login -c 'printf "[%s]" "$@"' _ "$@" 2>&1)
    wrapped=$("$UCRT64" -c 'printf "[%s]" "$@"' _ "$@" 2>&1)
    if [ "$direct" != "$expected" ]; then
        fail "$desc: TEST BUG -- direct bash delivered '$direct', expected '$expected'"
        return
    fi
    if [ "$wrapped" = "$direct" ]; then
        pass "$desc"
    else
        fail "$desc: wrapper='$wrapped' direct='$direct'"
    fi
}

# ---------------------------------------------------------------------------
bold "=== Bare invocation (no args) ==="

# Test: wrapper with zero args must launch an interactive-style login shell,
# NOT forward a bogus script path (regression: CommandLineToArgvW("") yields
# argc=1 with the exe path, which bash tried to execute -> exit 126).
result=$(printf 'echo BARE_OK\nexit 0\n' | "$UCRT64" 2>/dev/null)
if [ "$result" = "BARE_OK" ]; then
    pass "bare invocation runs login shell from stdin"
else
    fail "bare invocation: got '$result', expected 'BARE_OK'"
fi

result=$("$UCRT64" -l -c 'echo "$MSYSTEM"' </dev/null 2>/dev/null)
if [ "$result" = "UCRT64" ]; then
    pass "-l flag style invocation"
else
    fail "-l flag style invocation: got '$result'"
fi

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
GLOB_DIR=""

# Test 15: wrapper path with spaces (copy, so GetModuleFileNameW sees the
# spaced path; MSYS ln -s on .exe files copies anyway).
SPACED_TMP=$(mktemp -d)
SPACED_BASE="$SPACED_TMP/dir with spaces"
mkdir -p "$SPACED_BASE/usr/bin"
cp "$BASH" "$SPACED_BASE/usr/bin/bash.exe" || {
    echo "ERROR: cannot copy bash to spaced test dir" >&2
    exit 1
}
cp "$UCRT64" "$SPACED_BASE/ucrt64_shell_wrapper.exe" || {
    echo "ERROR: cannot copy wrapper to spaced test dir" >&2
    exit 1
}
result=$("$SPACED_BASE/ucrt64_shell_wrapper.exe" -c 'echo "$MSYSTEM"')
if [ "$result" = "UCRT64" ]; then
    pass "wrapper path with spaces"
else
    fail "wrapper path with spaces: got '$result'"
fi
rm -rf "$SPACED_TMP"
SPACED_TMP=""

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
CWD_DIR=""

# ---------------------------------------------------------------------------
bold "=== Error messages ==="

# Test 17: missing bash.exe
MISSING_DIR=$(mktemp -d)
cp "$UCRT64" "$MISSING_DIR/" || {
    echo "ERROR: cannot stage wrapper for missing-bash test" >&2
    exit 1
}
stderr=$("$MISSING_DIR/ucrt64_shell_wrapper.exe" -c 'echo' 2>&1 1>/dev/null)
exitcode=$?
rm -rf "$MISSING_DIR"
MISSING_DIR=""
if [ $exitcode -ne 0 ] && echo "$stderr" | grep -q "not found"; then
    pass "error message on missing bash"
else
    fail "error message on missing bash: exit=$exitcode stderr='$stderr'"
fi

# Test 18: directory at bash path is also rejected
DIRBAIT=$(mktemp -d)
mkdir -p "$DIRBAIT/usr/bin/bash.exe"
cp "$UCRT64" "$DIRBAIT/" || {
    echo "ERROR: cannot stage wrapper for dir-at-bash-path test" >&2
    exit 1
}
stderr=$("$DIRBAIT/ucrt64_shell_wrapper.exe" -c 'echo' 2>&1 1>/dev/null)
exitcode=$?
rm -rf "$DIRBAIT"
DIRBAIT=""
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
