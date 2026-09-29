#pragma once
/* Shared quoting, argv0-skipping, and tail-parsing logic for shell_wrapper.c
 * and test_quoting.c.
 *
 * Quoting rules: always wrap every arg in double quotes and escape for the
 * MSYS runtime's command-line tokenizer -- the parser bash's argv comes
 * from when its parent is a native process (CreateProcessW). Empirical
 * tokenizer rules inside double quotes (verified against
 * C:\msys64\usr\bin\bash.exe):
 *   \\  -> \     (backslash pairs are HALVED)
 *   \"  -> "
 *   \X  -> \X    (any other backslash sequence is literal)
 * Outside quotes backslashes are literal; MSYS/Cygwin's parser also treats
 * unquoted *?[]' and newline/tilde specially (globbing, quote stripping).
 * Always-double-quoting suppresses that mangling.
 *
 * NOTE the emitted command line is deliberately NOT CommandLineToArgvW
 * for mid-word backslash runs: CTA treats them literally, the MSYS
 * tokenizer halves pairs, so runs before an ordinary character are
 * emitted doubled to survive the halving. Runs before a quote and runs at
 * end-of-string are emitted so BOTH dialects decode them identically.
 * (The INPUT side -- parse_arg_tail -- stays CTA: every known parent,
 * including MSYS's own spawner for native children, quotes CTA-style.)
 */
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
/* Needed by parse_arg_tail below. Both consumers include these first (with
 * WIN32_LEAN_AND_MEAN defined), so this re-inclusion is a no-op. */
#include <windows.h>
#include <shellapi.h>

/* Length of the quoted form of arg, not including NUL. Always quotes.
 * Returns (size_t)-1 on overflow (practically impossible: inputs <= 32767). */
static size_t quoted_arg_len(const wchar_t *arg)
{
    size_t len = 2; /* opening + closing quote */
    size_t backslash = 0;
    for (const wchar_t *p = arg; *p; p++)
    {
        if (*p == L'\\')
        {
            backslash++;
            if (backslash > (size_t)-1 / 4)
                return (size_t)-1;
        }
        else if (*p == L'"')
        {
            size_t add = backslash * 2 + 2;
            if (len > (size_t)-1 - add)
                return (size_t)-1;
            len += add;
            backslash = 0;
        }
        else
        {
            size_t add = backslash * 2 + 1; /* doubled: MSYS halves pairs */
            if (len > (size_t)-1 - add)
                return (size_t)-1;
            len += add;
            backslash = 0;
        }
    }
    {
        size_t add = backslash * 2;
        if (len > (size_t)-1 - add)
            return (size_t)-1;
        len += add;
    }
    return len;
}

/* Append quoted arg at pos. end is one-past-last writable wchar.
 * Returns new pos on success, NULL on overflow. Always quotes. */
static wchar_t *append_quoted_arg_checked(wchar_t *pos, wchar_t *end, const wchar_t *arg)
{
    size_t backslash = 0;

    if (pos >= end)
        return NULL;
    *pos++ = L'"';

    for (const wchar_t *p = arg; *p; p++)
    {
        if (*p == L'\\')
        {
            backslash++;
        }
        else if (*p == L'"')
        {
            for (size_t i = 0; i < backslash * 2; i++)
            {
                if (pos >= end)
                    return NULL;
                *pos++ = L'\\';
            }
            if (end - pos < 2)
                return NULL;
            *pos++ = L'\\';
            *pos++ = L'"';
            backslash = 0;
        }
        else
        {
            /* Doubled so the MSYS tokenizer's pair-halving restores the
             * run verbatim (plain CTA quoting halves it: a\\b -> a\b). */
            for (size_t i = 0; i < backslash * 2; i++)
            {
                if (pos >= end)
                    return NULL;
                *pos++ = L'\\';
            }
            if (pos >= end)
                return NULL;
            *pos++ = *p;
            backslash = 0;
        }
    }

    for (size_t i = 0; i < backslash * 2; i++)
    {
        if (pos >= end)
            return NULL;
        *pos++ = L'\\';
    }
    if (pos >= end)
        return NULL;
    *pos++ = L'"';

    return pos;
}

static const wchar_t *skip_spaces_w(const wchar_t *p)
{
    while (*p == L' ' || *p == L'\t')
        p++;
    return p;
}

/* Find needle in haystack (case-insensitive) where the char after the match
 * is NUL, space, tab, or '"', AND the match starts at the beginning of
 * haystack or right after a path separator or drive colon. The before-check
 * prevents swallowing a USER arg that happens to equal the wrapper name
 * when argv0 itself was renamed (e.g. "other.exe ucrt64_shell_wrapper.exe"
 * must not skip the second token as argv0). NOTE: a quote is deliberately
 * NOT in the before set: quoted argv0 ("C:\dir\wrapper.exe") is handled
 * earlier in skip_argv0 and never reaches this search, so a quote before a
 * match can only mean a QUOTED USER ARG (e.g. other.exe
 * "ucrt64_shell_wrapper.exe" -c hi) -- swallowing it would eat user input.
 * Returns pointer to match start or NULL. */
static const wchar_t *find_name_token(const wchar_t *haystack, const wchar_t *needle)
{
    size_t nlen;
    if (!haystack || !needle || *needle == L'\0')
        return NULL;
    nlen = wcslen(needle);
    for (const wchar_t *p = haystack; *p; p++)
    {
        wchar_t before;
        if (_wcsnicmp(p, needle, nlen) != 0)
            continue;
        before = (p == haystack) ? L'\0' : p[-1];
        if (before != L'\0' && before != L'\\' && before != L'/' &&
            before != L':')
            continue;
        {
            wchar_t after = p[nlen];
            if (after == L'\0' || after == L' ' || after == L'\t' || after == L'"')
                return p;
        }
    }
    return NULL;
}

/* Skip argv[0] in a raw Windows command line and return pointer to args.
 * basename is e.g. L"ucrt64_shell_wrapper.exe", stem is basename without
 * extension (for invocations that omit .exe). Handles:
 *  - quoted argv0:  "C:\My Dir\wrapper.exe" args
 *  - unquoted argv0 with spaces: C:\My Dir\wrapper.exe args
 *  - relative/short names, leading spaces, missing closing quote.
 * Never fails; falls back to skipping to first whitespace. */
static const wchar_t *skip_argv0(const wchar_t *cmdline,
                                 const wchar_t *basename,
                                 const wchar_t *stem)
{
    const wchar_t *p;
    const wchar_t *found;
    if (!cmdline)
        return L"";
    p = skip_spaces_w(cmdline);
    if (*p == L'"')
    {
        p++;
        while (*p && *p != L'"')
            p++;
        if (*p == L'"')
            p++;
        return skip_spaces_w(p);
    }
    if (basename && *basename)
    {
        found = find_name_token(p, basename);
        if (found)
        {
            const wchar_t *after = found + wcslen(basename);
            if (*after == L'"')
                after++;
            return skip_spaces_w(after);
        }
    }
    if (stem && *stem)
    {
        found = find_name_token(p, stem);
        if (found)
        {
            const wchar_t *after = found + wcslen(stem);
            if (*after == L'"')
                after++;
            return skip_spaces_w(after);
        }
    }
    while (*p && *p != L' ' && *p != L'\t')
        p++;
    return skip_spaces_w(p);
}

/* Parse an argument tail (argv0 already skipped) into user args.
 *
 * The tail is parsed with a dummy argv0 prefix ("x <tail>") and argv[0] of
 * THAT parse is discarded. This is load-bearing, not cosmetic:
 * CommandLineToArgvW decodes element 0 with DIFFERENT (argv0) grammar --
 * backslash-quote sequences decode literally there ("a\"b" at position 0
 * yields a\ + b instead of a"b). Parsing the bare tail would therefore
 * corrupt a quoted first user arg containing a quote; the dummy prefix
 * keeps every user arg in argument position. (Empty tail means zero user
 * args: CommandLineToArgvW(L"") does NOT return argc=0 -- it returns the
 * current exe path as a single arg, which would be forwarded to bash as a
 * bogus script path. Whitespace-only tails also yield zero user args:
 * CTA("x   ") returns argc=1, so no tail pre-trimming is needed.)
 *
 * Returns 0 on success, -1 on allocation/parse failure (GetLastError set
 * by CommandLineToArgvW on parse failure). On success *outArgv is NULL
 * and *outArgc 0 when there are no user args; otherwise *outArgv points
 * to a CommandLineToArgvW block whose FIRST element is the dummy and must
 * be skipped: user args are (*outArgv)[1..*outArgc-1]. The caller frees
 * with LocalFree(*outArgv) (safe to call only when non-NULL). On failure
 * *outArgv is NULL and *outArgc 0. */
static int parse_arg_tail(const wchar_t *argTail, LPWSTR **outArgv, int *outArgc)
{
    size_t tailLen;
    wchar_t *prefixed;
    LPWSTR *argv;
    int argc;

    *outArgv = NULL;
    *outArgc = 0;
    if (!argTail || *argTail == L'\0')
        return 0;

    /* "x" + " " + tail + NUL. +3 cannot overflow: tailLen <= 32767. */
    tailLen = wcslen(argTail);
    prefixed = (wchar_t *)malloc((tailLen + 3) * sizeof(wchar_t));
    if (!prefixed)
        return -1;
    prefixed[0] = L'x';
    prefixed[1] = L' ';
    memcpy(prefixed + 2, argTail, (tailLen + 1) * sizeof(wchar_t));

    argv = CommandLineToArgvW(prefixed, &argc);
    free(prefixed);
    if (!argv)
        return -1;
    if (argc < 1)
    {
        /* Unreachable per CTA docs (always >= 1), but never trust it. */
        LocalFree(argv);
        SetLastError(ERROR_INVALID_DATA);
        return -1;
    }
    if (argc == 1)
    {
        /* Whitespace-only tail: dummy alone, zero user args. */
        LocalFree(argv);
        return 0;
    }
    *outArgv = argv;
    *outArgc = argc;
    return 0;
}
