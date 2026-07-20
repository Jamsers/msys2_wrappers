#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* copy of append_quoted_arg from shell_wrapper.c */
static wchar_t *append_quoted_arg(wchar_t *pos, const wchar_t *arg)
{
    int backslash = 0;
    BOOL needQuote = wcspbrk(arg, L" \t\"") != NULL || *arg == L'\0';

    if (needQuote)
        *pos++ = L'"';

    for (const wchar_t *p = arg; *p; p++)
    {
        if (*p == L'\\')
        {
            backslash++;
        }
        else if (*p == L'"')
        {
            for (int i = 0; i < backslash * 2; i++)
                *pos++ = L'\\';
            *pos++ = L'\\';
            *pos++ = L'"';
            backslash = 0;
        }
        else
        {
            for (int i = 0; i < backslash; i++)
                *pos++ = L'\\';
            *pos++ = *p;
            backslash = 0;
        }
    }

    if (needQuote)
    {
        for (int i = 0; i < backslash * 2; i++)
            *pos++ = L'\\';
        *pos++ = L'"';
    }
    else
    {
        for (int i = 0; i < backslash; i++)
            *pos++ = L'\\';
    }

    return pos;
}

/* Test: quote arg, then parse it back with CommandLineToArgvW, expect original */
static void test_roundtrip(const wchar_t *arg, const wchar_t *label)
{
    wchar_t cmdline[4096];
    int argc;
    LPWSTR *argv;
    int match;

    /* Build: "dummy.exe" <quoted-arg> */
    wchar_t *pos = cmdline;
    pos = append_quoted_arg(pos, L"dummy.exe");
    *pos++ = L' ';
    pos = append_quoted_arg(pos, arg);
    *pos = L'\0';

    argv = CommandLineToArgvW(cmdline, &argc);
    if (!argv)
    {
        wprintf(L"FAIL [%s]: CommandLineToArgvW returned NULL\n", label);
        tests_failed++;
        tests_run++;
        return;
    }

    if (argc != 2)
    {
        wprintf(L"FAIL [%s]: expected 2 args, got %d\n", label, argc);
        tests_failed++;
        tests_run++;
        LocalFree(argv);
        return;
    }

    match = (wcscmp(argv[1], arg) == 0);
    if (match)
    {
        tests_passed++;
    }
    else
    {
        wprintf(L"FAIL [%s]:\n  expected: \"%s\"\n  got:      \"%s\"\n",
                label, arg, argv[1]);
        tests_failed++;
    }

    tests_run++;
    LocalFree(argv);
}

int main()
{
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

    wprintf(L"\n%d passed, %d failed, %d total\n",
            tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
