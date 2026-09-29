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

/* Decode the next double-quoted token of an emitted command line the way
 * the MSYS runtime's command-line tokenizer does -- bash's argv source
 * when the parent is a native process. Rules inside quotes, verified
 * against real C:\msys64\usr\bin\bash.exe:
 *   \\ -> \    (backslash pairs are HALVED -- the CTA divergence)
 *   \" -> "
 *   \X -> \X   (any other backslash sequence is literal)
 * A closing double quote ends the token. (The authoritative check of the
 * real tokenizer is test_wrapper.sh against real bash; this model exists
 * so unit tests catch dialect regressions.) Advances *pp past the token.
 * Returns 0 on success, -1 on malformed input or overflow. */
static int msys_decode_next(const wchar_t **pp, wchar_t *out, size_t outCap)
{
    const wchar_t *p = *pp;
    size_t n = 0;

    if (*p != L'"')
        return -1;
    p++;
    while (*p && *p != L'"')
    {
        if (*p == L'\\' && (p[1] == L'\\' || p[1] == L'"'))
        {
            if (n + 1 >= outCap)
                return -1;
            out[n++] = p[1];        /* \\ -> \,  \" -> " */
            p += 2;
        }
        else if (*p == L'\\' && p[1] != L'\0')
        {
            if (n + 2 >= outCap)
                return -1;
            out[n++] = *p++;        /* \X -> \X literal */
            out[n++] = *p++;
        }
        else
        {
            if (n + 1 >= outCap)
                return -1;
            out[n++] = *p++;        /* ordinary char (or lone trailing \) */
        }
    }
    if (*p != L'"')
        return -1;                  /* unterminated quote */
    out[n] = L'\0';
    *pp = p + 1;
    return 0;
}

/* True when arg contains a backslash run followed by an ordinary (non-
 * quote) character -- the one shape where CommandLineToArgvW and the MSYS
 * tokenizer disagree (runs before a quote and runs at end-of-string decode
 * identically under both). */
static int has_midword_bs_run(const wchar_t *arg)
{
    size_t run = 0;
    for (const wchar_t *p = arg; *p; p++)
    {
        if (*p == L'\\')
            run++;
        else
        {
            if (run > 0 && *p != L'"')
                return 1;
            run = 0;
        }
    }
    return 0;
}

/* Test: quote arg with the shared header, then decode it two ways:
 * PRIMARY: the MSYS tokenizer model (bash is the real consumer; the
 * emission targets its dialect).
 * SECONDARY: CommandLineToArgvW. Both dialects must agree everywhere
 * except mid-word backslash runs, where the output is deliberately
 * non-CTA (see quoting.h); a CTA mismatch on such an arg is expected. */
static void test_roundtrip(const wchar_t *arg, const wchar_t *label)
{
    wchar_t cmdline[32768];
    wchar_t decoded[32768];
    wchar_t *pos;
    wchar_t *end = cmdline + _countof(cmdline);
    int argc;
    LPWSTR *argv;
    const wchar_t *p = cmdline;

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

    /* PRIMARY: MSYS tokenizer model. */
    if (msys_decode_next(&p, decoded, _countof(decoded)) != 0 ||
        wcscmp(decoded, L"dummy.exe") != 0)
    {
        record_fail(label, L"MSYS model: dummy token mangled",
                    L"dummy.exe", decoded);
        return;
    }
    while (*p == L' ')
        p++;
    if (msys_decode_next(&p, decoded, _countof(decoded)) != 0)
    {
        record_fail(label, L"MSYS model: arg token malformed", arg, L"(parse error)");
        return;
    }
    if (wcscmp(decoded, arg) != 0)
    {
        record_fail(label, L"MSYS roundtrip mismatch", arg, decoded);
        return;
    }
    while (*p == L' ')
        p++;
    if (*p != L'\0')
    {
        record_fail(label, L"MSYS model: trailing garbage", L"(end of token)", p);
        return;
    }

    /* SECONDARY: CommandLineToArgvW agreement outside the divergent shape. */
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
    if (wcscmp(argv[1], arg) == 0 || has_midword_bs_run(arg))
        record_pass();
    else
        record_fail(label, L"CTA roundtrip mismatch", arg, argv[1]);
    LocalFree(argv);
}

/* Locks in the dialect split the quoting targets: for a mid-word backslash
 * pair, the emitted form must decode to the original under the MSYS model
 * and must NOT under CommandLineToArgvW (its output is deliberately
 * non-CTA there). If this ever fails, either the tokenizer rules moved or
 * the emission became CTA-compatible again -- re-evaluate the doubling. */
static void test_dialect_divergence(void)
{
    static const wchar_t *const arg = L"a\\\\b"; /* a, \, \, b */
    wchar_t cmdline[32768];
    wchar_t decoded[32768];
    wchar_t *pos;
    wchar_t *end = cmdline + _countof(cmdline);
    const wchar_t *p = cmdline;
    int argc = 0;
    LPWSTR *argv;

    pos = append_quoted_arg_checked(cmdline, end, L"dummy.exe");
    if (pos && pos < end)
        *pos++ = L' ';
    else
        pos = NULL;
    if (pos)
        pos = append_quoted_arg_checked(pos, end, arg);
    if (!pos || pos >= end)
    {
        record_fail(L"dialect divergence", L"quoting overflowed test buffer",
                    arg, L"(overflow)");
        return;
    }
    *pos = L'\0';

    if (msys_decode_next(&p, decoded, _countof(decoded)) != 0 ||
        wcscmp(decoded, L"dummy.exe") != 0)
    {
        record_fail(L"dialect divergence", L"MSYS model: dummy token mangled",
                    L"dummy.exe", decoded);
        return;
    }
    p++; /* the separating space */
    if (msys_decode_next(&p, decoded, _countof(decoded)) != 0 ||
        wcscmp(decoded, arg) != 0)
    {
        record_fail(L"dialect divergence", L"MSYS model must decode doubled run",
                    arg, decoded);
        return;
    }
    argv = CommandLineToArgvW(cmdline, &argc);
    if (!argv)
    {
        record_fail(L"dialect divergence", L"CommandLineToArgvW returned NULL",
                    L"(null)", L"(null)");
        return;
    }
    if (argc == 2 && wcscmp(argv[1], arg) != 0)
        record_pass();  /* divergence intact */
    else
        record_fail(L"dialect divergence", L"CTA unexpectedly agrees with MSYS",
                    L"CTA differs", L"CTA agrees");
    LocalFree(argv);
}

/* Test: parse_arg_tail must decode a raw tail exactly like
 * CommandLineToArgvW decodes the same args in argument position of a full
 * command line. Tails here are written as parents actually build them
 * (e.g. msys bash emits "a\"b" for first arg a"b) -- this is the wrapper's
 * real input shape, and the case the bare-tail parse corrupted. */
static void test_parse_tail(const wchar_t *tail, int expectedUserArgc,
                            const wchar_t *const *expectedArgs,
                            const wchar_t *label)
{
    LPWSTR *argv = NULL;
    int argc = 0;
    int i;

    if (parse_arg_tail(tail, &argv, &argc) != 0)
    {
        record_fail(label, L"parse_arg_tail failed", tail, L"(error)");
        return;
    }
    if (argv == NULL || argc < 1)
    {
        if (expectedUserArgc == 0 && argv == NULL && argc == 0)
        {
            record_pass();
            return;
        }
        record_fail(label, L"argc mismatch (NULL/empty block)",
                    L"non-empty", L"(null)");
        if (argv)
            LocalFree(argv);
        return;
    }
    /* argv[0] is the dummy; user args are argv[1..argc-1]. */
    if (argc - 1 != expectedUserArgc)
    {
        wchar_t exp[64];
        wchar_t got[64];
        _snwprintf_s(exp, _countof(exp), _TRUNCATE, L"%d user args", expectedUserArgc);
        _snwprintf_s(got, _countof(got), _TRUNCATE, L"%d user args", argc - 1);
        record_fail(label, L"user argc mismatch", exp, got);
        LocalFree(argv);
        return;
    }
    for (i = 0; i < expectedUserArgc; i++)
    {
        if (wcscmp(argv[i + 1], expectedArgs[i]) != 0)
        {
            wchar_t detail[64];
            _snwprintf_s(detail, _countof(detail), _TRUNCATE,
                         L"user arg %d mismatch", i);
            record_fail(label, detail, expectedArgs[i], argv[i + 1]);
            LocalFree(argv);
            return;
        }
    }
    /* The dummy must be exactly "x" (contract, not cosmetic: a quoted or
     * spaced dummy would shift parsing). */
    if (wcscmp(argv[0], L"x") != 0)
        record_fail(label, L"dummy argv0 mangled", L"x", argv[0]);
    else
        record_pass();
    LocalFree(argv);
}

/* Locks in the platform trap parse_arg_tail exists to avoid: the SAME
 * quoted text decodes DIFFERENTLY at position 0 (argv0 grammar) vs
 * argument position. If this ever passes, the dummy prefix is redundant;
 * while it fails-as-expected, removing the prefix corrupts quoted first
 * args containing backslash-quote sequences. */
static void test_first_position_trap(void)
{
    int argc0 = 0;
    int argcN = 0;
    LPWSTR *argv0 = CommandLineToArgvW(L"\"a\\\"b\"", &argc0);
    LPWSTR *argvN = CommandLineToArgvW(L"x \"a\\\"b\"", &argcN);
    int differs;

    if (!argv0 || !argvN)
    {
        record_fail(L"first-position trap", L"CommandLineToArgvW returned NULL",
                    L"both parse", L"(null)");
        if (argv0)
            LocalFree(argv0);
        if (argvN)
            LocalFree(argvN);
        return;
    }
    /* Position 0 must NOT decode to the clean single arg (that IS the
     * trap -- today it yields argc=2, 'a\' + 'b second'), while argument
     * position must decode cleanly. Either side changing means the
     * platform behavior moved and the dummy prefix needs re-evaluation. */
    differs = !(argc0 == 1 && wcscmp(argv0[0], L"a\"b") == 0);
    if (differs && argcN == 2 && wcscmp(argvN[1], L"a\"b") == 0)
        record_pass();
    else
    {
        wchar_t got[128];
        _snwprintf_s(got, _countof(got), _TRUNCATE,
                     L"pos0 argc=%d, argpos argc=%d", argc0, argcN);
        record_fail(L"first-position trap", L"platform behavior changed", L"trap intact", got);
    }
    LocalFree(argv0);
    LocalFree(argvN);
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

/* Test: skip_argv0 must return the argument tail for raw command lines.
 * basename/stem are passed in (not hardcoded) so renamed-exe cases where
 * argv0 does NOT contain the wrapper name can be tested. */
static void test_skip_argv0(const wchar_t *cmdline, const wchar_t *expected,
                            const wchar_t *basename, const wchar_t *stem,
                            const wchar_t *label)
{
    const wchar_t *tail = skip_argv0(cmdline, basename, stem);
    if (wcscmp(tail, expected) == 0)
        record_pass();
    else
        record_fail(label, L"argv0 skip mismatch", expected, tail);
}

/* Locks in the platform behavior the wrapper's empty-tail fix relies on:
 * CommandLineToArgvW("") returns argc=1 (the exe path), NOT argc=0 --
 * so shell_wrapper.c must special-case an empty tail to zero args. */
static void test_empty_tail_argc1(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(L"", &argc);
    if (!argv)
    {
        record_fail(L"empty tail argc", L"CommandLineToArgvW returned NULL",
                    L"argc=1", L"(null)");
        return;
    }
    if (argc == 1)
        record_pass();
    else
    {
        wchar_t got[64];
        _snwprintf_s(got, _countof(got), _TRUNCATE, L"argc=%d", argc);
        record_fail(L"empty tail argc", L"argc mismatch", L"argc=1", got);
    }
    LocalFree(argv);
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
    test_roundtrip(L"a\\\\b",                        L"double backslash mid-word");
    test_roundtrip(L"a\\\\\\b",                      L"triple backslash mid-word");
    test_roundtrip(L"a\\\\\\\\b",                    L"quadruple backslash mid-word");
    test_roundtrip(L"a\\\\b c",                      L"double backslash with space");
    test_roundtrip(L"a\\$b",                         L"backslash before dollar");
    test_roundtrip(L"a\\`b",                         L"backslash before backtick");
    test_roundtrip(L"a\\\\$b",                       L"double backslash before dollar");
    test_roundtrip(L"a\\\\b\"c\\\\d",                L"mixed runs and quote");
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
    test_len_matches(L"a\\\\b", L"len mid-word backslash run");
    test_len_matches(L"a\\\\\\b\"c", L"len mixed backslash runs");
    test_len_matches(L"trail\\\\\\", L"len trailing backslashes");

    test_skip_argv0(L"ucrt64_shell_wrapper.exe -c 'echo hi'",
                    L"-c 'echo hi'", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"bare argv0");
    test_skip_argv0(L"\"C:\\My Dir\\ucrt64_shell_wrapper.exe\" -c 'echo hi'",
                    L"-c 'echo hi'", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"quoted argv0 with spaces");
    test_skip_argv0(L"C:\\My Dir\\ucrt64_shell_wrapper.exe -c 'echo hi'",
                    L"-c 'echo hi'", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"unquoted argv0 with spaces");
    test_skip_argv0(L"C:\\msys64\\ucrt64_shell_wrapper -c 'echo hi'",
                    L"-c 'echo hi'", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"argv0 without extension");
    test_skip_argv0(L"  ucrt64_shell_wrapper.exe   -c x  ",
                    L"-c x  ", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"leading/multiple spaces");
    test_skip_argv0(L"ucrt64_shell_wrapper.exe",
                    L"", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"no args");
    test_skip_argv0(L"\"ucrt64_shell_wrapper.exe",
                    L"", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"unterminated quote");
    test_skip_argv0(L"C:\\Tools\\OTHER.exe -c hi",
                    L"-c hi", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"unknown argv0 fallback");
    /* Regression: renamed exe + user arg equal to wrapper name must NOT be
     * swallowed as argv0 (before-check: match only at start or after a
     * path separator). */
    test_skip_argv0(L"C:\\Tools\\OTHER.exe ucrt64_shell_wrapper.exe -c hi",
                    L"ucrt64_shell_wrapper.exe -c hi", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"basename as user arg not swallowed");
    test_skip_argv0(L"C:\\Tools\\OTHER.exe ucrt64_shell_wrapper -c hi",
                    L"ucrt64_shell_wrapper -c hi", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"stem as user arg not swallowed");
    /* Quoted twins: a quote before the match means a QUOTED USER ARG
     * (quoted argv0 never reaches the name search), so it must not match
     * either. Regression: the before-check used to allow '"'. */
    test_skip_argv0(L"C:\\Tools\\OTHER.exe \"ucrt64_shell_wrapper.exe\" -c hi",
                    L"\"ucrt64_shell_wrapper.exe\" -c hi", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"quoted basename as user arg not swallowed");
    test_skip_argv0(L"C:\\Tools\\OTHER.exe \"ucrt64_shell_wrapper\" -c hi",
                    L"\"ucrt64_shell_wrapper\" -c hi", L"ucrt64_shell_wrapper.exe",
                    L"ucrt64_shell_wrapper", L"quoted stem as user arg not swallowed");
    /* Baseline checks the tail-parsing fix depends on. */
    test_empty_tail_argc1();
    test_first_position_trap();
    test_dialect_divergence();

    /* parse_arg_tail: raw tails in the exact shapes parents build.
     * (Verified wire forms: msys bash emits "a\"b" for first arg a"b,
     * "a\\\"b" for a\"b, "x y\\" for [x y\], bare trail\ for trail\,
     * and "" for empty. Native list2cmdline emits bare a\"b.) */
    {
        static const wchar_t *const one_ab[] = { L"a\"b" };
        static const wchar_t *const one_absqb[] = { L"a\\\"b" };
        static const wchar_t *const one_trail[] = { L"trail\\" };
        static const wchar_t *const one_spaced_trail[] = { L"x y\\" };
        static const wchar_t *const one_empty[] = { L"" };
        static const wchar_t *const one_space[] = { L"with space" };
        static const wchar_t *const one_c[] = { L"-c" };
        static const wchar_t *const one_glob[] = { L"*.txt" };
        static const wchar_t *const two_ab_second[] = { L"a\"b", L"second" };
        static const wchar_t *const two_empty_second[] = { L"", L"second" };
        static const wchar_t *const two_c_cmd[] = { L"-c", L"echo hi" };
        test_parse_tail(L"\"a\\\"b\"", 1, one_ab, L"tail quoted quote-first");
        test_parse_tail(L"\"a\\\\\\\"b\"", 1, one_absqb, L"tail quoted backslash-quote-first");
        test_parse_tail(L"\"trail\\\\\"", 1, one_trail, L"tail quoted trailing-backslash-first");
        test_parse_tail(L"trail\\", 1, one_trail, L"tail bare trailing backslash");
        test_parse_tail(L"\"x y\\\\\"", 1, one_spaced_trail, L"tail quoted spaced trailing backslash");
        test_parse_tail(L"a\\\"b", 1, one_ab, L"tail native-unquoted quote-first");
        test_parse_tail(L"\"\"", 1, one_empty, L"tail empty-string-first");
        test_parse_tail(L"\"with space\"", 1, one_space, L"tail quoted spaced-first");
        test_parse_tail(L"-c", 1, one_c, L"tail bare flag");
        test_parse_tail(L"*.txt", 1, one_glob, L"tail bare glob");
        test_parse_tail(L"\"a\\\"b\" second", 2, two_ab_second, L"tail tricky-first plus second");
        test_parse_tail(L"\"\" second", 2, two_empty_second, L"tail empty-first plus second");
        test_parse_tail(L"-c \"echo hi\"", 2, two_c_cmd, L"tail flag plus quoted command");
        test_parse_tail(L"", 0, NULL, L"tail empty is zero args");
        test_parse_tail(L"   ", 0, NULL, L"tail whitespace-only is zero args");
    }

    wprintf(L"\n%d passed, %d failed, %d total\n",
            tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
