#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include "quoting.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void record_pass(void)
{
    tests_passed++;
    tests_run++;
}

static void record_fail(const wchar_t *label, const wchar_t *detail,
                        const wchar_t *expected, const wchar_t *got)
{
    wprintf(L"FAIL [%s]: %s\n  expected: \"%s\"\n  got:      \"%s\"\n",
            label, detail, expected, got);
    tests_failed++;
    tests_run++;
}

/* Test: quote arg with the shared header, parse back with
 * CommandLineToArgvW, expect the original. */
static void test_roundtrip(const wchar_t *arg, const wchar_t *label)
{
    wchar_t cmdline[32768];
    wchar_t *pos;
    wchar_t *end = cmdline + _countof(cmdline);
    int argc;
    LPWSTR *argv;
    int match;

    /* Build: "dummy.exe" <quoted-arg> */
    pos = append_quoted_arg_checked(cmdline, end, L"dummy.exe");
    if (pos && pos < end)
        *pos++ = L' ';
    else
        pos = NULL;
    if (pos)
        pos = append_quoted_arg_checked(pos, end, arg);
    if (!pos || pos >= end)
    {
        record_fail(label, L"quoting overflowed test buffer", arg, L"(overflow)");
        return;
    }
    *pos = L'\0';

    argv = CommandLineToArgvW(cmdline, &argc);
    if (!argv)
    {
        record_fail(label, L"CommandLineToArgvW returned NULL", arg, L"(null)");
        return;
    }

    if (argc != 2)
    {
        wchar_t got[64];
        _snwprintf_s(got, _countof(got), _TRUNCATE, L"%d args", argc);
        record_fail(label, L"argc mismatch", L"2 args", got);
        LocalFree(argv);
        return;
    }

    match = (wcscmp(argv[1], arg) == 0);
    if (match)
        record_pass();
    else
        record_fail(label, L"roundtrip mismatch", arg, argv[1]);

    LocalFree(argv);
}

/* Test: every arg must come out wrapped in quotes (protects against
 * Cygwin's parser mangling unquoted *?[]' newline ~). */
static void test_always_quotes(const wchar_t *arg, const wchar_t *label)
{
    wchar_t buf[32768];
    wchar_t *end;
    wchar_t *pos = append_quoted_arg_checked(buf, buf + _countof(buf), arg);
    if (!pos)
    {
        record_fail(label, L"quoting overflowed test buffer", arg, L"(overflow)");
        return;
    }
    end = pos;
    if (end - buf >= 2 && buf[0] == L'"' && end[-1] == L'"')
        record_pass();
    else
    {
        *pos = L'\0';
        record_fail(label, L"not wrapped in quotes", arg, buf);
    }
}

/* Test: appending past the end must return NULL, not overflow. */
static void test_checked_overflow(void)
{
    wchar_t buf[8];
    wchar_t *r = append_quoted_arg_checked(buf, buf + _countof(buf),
                                           L"this arg is far too long");
    if (!r)
        record_pass();
    else
        record_fail(L"checked append overflow", L"expected NULL", L"(null)", L"(wrote)");
}

/* Test: quoted_arg_len must match the actual quoted length. */
static void test_len_matches(const wchar_t *arg, const wchar_t *label)
{
    wchar_t buf[32768];
    wchar_t *pos = append_quoted_arg_checked(buf, buf + _countof(buf), arg);
    size_t predicted;
    if (!pos)
    {
        record_fail(label, L"quoting overflowed test buffer", arg, L"(overflow)");
        return;
    }
    predicted = quoted_arg_len(arg);
    if (predicted == (size_t)(pos - buf))
        record_pass();
    else
    {
        wchar_t exp[64];
        wchar_t got[64];
        _snwprintf_s(exp, _countof(exp), _TRUNCATE, L"len %Iu", predicted);
        _snwprintf_s(got, _countof(got), _TRUNCATE, L"len %Iu", (size_t)(pos - buf));
        record_fail(label, L"length mismatch", exp, got);
    }
}

/* Test: skip_argv0 must return the argument tail for raw command lines. */
static void test_skip_argv0(const wchar_t *cmdline, const wchar_t *expected,
                            const wchar_t *label)
{
    const wchar_t *tail = skip_argv0(cmdline, L"ucrt64_shell_wrapper.exe",
                                     L"ucrt64_shell_wrapper");
    if (wcscmp(tail, expected) == 0)
        record_pass();
    else
        record_fail(label, L"argv0 skip mismatch", expected, tail);
}

/* Test: a long arg must roundtrip (guards the fixed-buffer era). */
static void test_long_arg(void)
{
    static wchar_t arg[20000];
    for (size_t i = 0; i + 1 < _countof(arg); i++)
        arg[i] = (wchar_t)(L'a' + (i % 26));
    arg[_countof(arg) - 1] = L'\0';
    test_roundtrip(arg, L"20000-char arg");
}

int main()
{
    size_t i;
    static const wchar_t *const cygwin_args[] = {
        L"*.txt", L"a?b", L"[ab]", L"it's", L"~",
        L"a\tb", L"a\nb", L"plain", L"--login", L"-c",
    };

    test_roundtrip(L"hello",                          L"plain word");
    test_roundtrip(L"hello world",                    L"with space");
    test_roundtrip(L"  leading spaces",               L"leading spaces");
    test_roundtrip(L"trailing spaces  ",              L"trailing spaces");
    test_roundtrip(L"",                               L"empty string");
    test_roundtrip(L"a\"b",                          L"single quote inside");
    test_roundtrip(L"\"hello\"",                     L"wrapped in quotes");
    test_roundtrip(L"a\"b\"c\"d",                    L"multiple quotes");
    test_roundtrip(L"a\\b",                          L"backslash mid-word");
    test_roundtrip(L"trail\\",                       L"trailing backslash");
    test_roundtrip(L"trail\\\\",                     L"two trailing backslashes");
    test_roundtrip(L"a\\\"b",                        L"backslash before quote");
    test_roundtrip(L"a\\\\\"b",                      L"two backslashes before quote");
    test_roundtrip(L"a\\\\\\\"b",                    L"three backslashes before quote");
    test_roundtrip(L"\\\"",                          L"just backslash-quote");
    test_roundtrip(L"\\\\\\\"",                      L"three backslashes then quote");
    test_roundtrip(L"has \"quotes\" and\\ backslashes",
                                                     L"mixed quotes and backslashes");
    test_roundtrip(L"\" leading quote",              L"quote at start");
    test_roundtrip(L"trailing quote \"",             L"quote at end");
    test_roundtrip(L"\\ \\\" \\\\\\\"",
                                                     L"spaced backslash-quote combos");
    test_roundtrip(L"C:\\msys64\\usr\\bin\\bash.exe",
                                                     L"typical exe path (no spaces)");
    test_roundtrip(L"C:\\Program Files\\Git\\bin\\bash.exe",
                                                     L"path with spaces");
    /* New: Cygwin-sensitive chars, tab/newline, Unicode, quote-only, long */
    test_roundtrip(L"*.txt",                         L"glob star");
    test_roundtrip(L"a?b",                           L"glob question mark");
    test_roundtrip(L"[ab]",                          L"glob brackets");
    test_roundtrip(L"it's",                          L"single quote");
    test_roundtrip(L"~",                             L"tilde");
    test_roundtrip(L"a\tb",                         L"tab inside");
    test_roundtrip(L"a\nb",                         L"newline inside");
    test_roundtrip(L"héllo-日本語",                    L"unicode");
    test_roundtrip(L"\"",                            L"lone double quote");
    test_roundtrip(L"\\\\server\\share",             L"UNC path");
    test_long_arg();

    for (i = 0; i < _countof(cygwin_args); i++)
        test_always_quotes(cygwin_args[i], L"always-quote");

    test_checked_overflow();

    test_len_matches(L"hello", L"len plain");
    test_len_matches(L"", L"len empty");
    test_len_matches(L"a\"b", L"len quote");
    test_len_matches(L"trail\\\\\\", L"len trailing backslashes");

    test_skip_argv0(L"ucrt64_shell_wrapper.exe -c 'echo hi'",
                    L"-c 'echo hi'", L"bare argv0");
    test_skip_argv0(L"\"C:\\My Dir\\ucrt64_shell_wrapper.exe\" -c 'echo hi'",
                    L"-c 'echo hi'", L"quoted argv0 with spaces");
    test_skip_argv0(L"C:\\My Dir\\ucrt64_shell_wrapper.exe -c 'echo hi'",
                    L"-c 'echo hi'", L"unquoted argv0 with spaces");
    test_skip_argv0(L"C:\\msys64\\ucrt64_shell_wrapper -c 'echo hi'",
                    L"-c 'echo hi'", L"argv0 without extension");
    test_skip_argv0(L"  ucrt64_shell_wrapper.exe   -c x  ",
                    L"-c x  ", L"leading/multiple spaces");
    test_skip_argv0(L"ucrt64_shell_wrapper.exe",
                    L"", L"no args");
    test_skip_argv0(L"\"ucrt64_shell_wrapper.exe",
                    L"", L"unterminated quote");
    test_skip_argv0(L"C:\\Tools\\OTHER.exe -c hi",
                    L"-c hi", L"unknown argv0 fallback");

    wprintf(L"\n%d passed, %d failed, %d total\n",
            tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
