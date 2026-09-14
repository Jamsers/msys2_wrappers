#pragma once
/* Shared quoting + argv0-skipping logic for shell_wrapper.c and test_quoting.c.
 *
 * Quoting rules: always wrap every arg in double quotes and implement
 * CommandLineToArgvW escaping (backslash-quote combinations). Always-quote
 * is required because MSYS/Cygwin's parser also treats unquoted *?[]' and
 * newline/tilde specially (globbing, quote stripping). Double-quoting
 * suppresses that mangling while remaining safe for CommandLineToArgvW.
 */
#include <stddef.h>
#include <wchar.h>
#include <string.h>

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
            size_t add = backslash + 1;
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
    int backslash = 0;

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
            for (int i = 0; i < backslash * 2; i++)
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
            for (int i = 0; i < backslash; i++)
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

    for (int i = 0; i < backslash * 2; i++)
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

/* Append raw string at pos (no quoting). end is one-past-last writable wchar.
 * Returns new pos on success, NULL on overflow.
 * NOTE: only safe for tails already quoted by a Cygwin/MSYS parent. Unused
 * by shell_wrapper.c (which re-quotes argv); kept for tests/future use. */
static wchar_t *append_raw_checked(wchar_t *pos, wchar_t *end, const wchar_t *s)
{
    while (*s)
    {
        if (pos >= end)
            return NULL;
        *pos++ = *s++;
    }
    return pos;
}

static const wchar_t *skip_spaces_w(const wchar_t *p)
{
    while (*p == L' ' || *p == L'\t')
        p++;
    return p;
}

/* Find needle in haystack (case-insensitive) where the char after the match
 * is NUL, space, tab, or '"'. Returns pointer to match start or NULL. */
static const wchar_t *find_name_token(const wchar_t *haystack, const wchar_t *needle)
{
    size_t nlen;
    if (!haystack || !needle || *needle == L'\0')
        return NULL;
    nlen = wcslen(needle);
    for (const wchar_t *p = haystack; *p; p++)
    {
        if (_wcsnicmp(p, needle, nlen) == 0)
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
