#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <string.h>
#include "quoting.h"

#ifndef PROGRAM_NAME
#error "PROGRAM_NAME must be defined by the including stub (e.g. L\"ucrt64_shell_wrapper\")"
#endif

#ifndef MSYSTEM_VALUE
#error "MSYSTEM_VALUE must be defined by the including stub (e.g. L\"UCRT64\")"
#endif

/* Adjacent wide literals concatenate: L"ucrt64_shell_wrapper" L".exe" */
#define WRAPPER_BASENAME PROGRAM_NAME L".exe"
#define WRAPPER_STEM PROGRAM_NAME

/* CreateProcessW accepts at most 32767 chars INCLUDING the terminating NUL,
 * so this is the maximum content length (the last arg check must not allow
 * need == 32767, which fails with ERROR_FILENAME_EXCED_RANGE). */
#define MAX_CMDLINE 32766
/* MSYS2's command-line tokenizer silently truncates a single argument to
 * 8186 characters (observed on 3.5.x; quoting style does not change it).
 * Commands longer than this are staged in a temp file by the -c transport
 * below. Other overlong args are rejected instead of being truncated. */
#define MSYS_ARG_CAP 8186
/* Env var used to pass the staged script path to bash. */
#define MSYS_SCRIPT_ENV L"MSYS2_WRAPPER_SCRIPT"
/* Long-path buffer (chars including NUL). Handles extended paths. */
#define LONG_PATH_CAP 32768

static void err_write(const wchar_t *msg)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    size_t len = wcslen(msg);
    if (h && h != INVALID_HANDLE_VALUE && len > 0 && len <= MAXDWORD)
    {
        DWORD mode;
        DWORD written;
        if (GetConsoleMode(h, &mode))
        {
            WriteConsoleW(h, msg, (DWORD)len, &written, NULL);
            return;
        }
        /* Redirected: emit UTF-8 so non-ASCII paths survive. */
        {
            int n = WideCharToMultiByte(CP_UTF8, 0, msg, -1, NULL, 0, NULL, NULL);
            if (n > 1)
            {
                char *utf8 = (char *)malloc((size_t)n);
                if (utf8)
                {
                    WideCharToMultiByte(CP_UTF8, 0, msg, -1, utf8, n, NULL, NULL);
                    WriteFile(h, utf8, (DWORD)(n - 1), &written, NULL);
                    free(utf8);
                    return;
                }
            }
        }
    }
    /* Last resort (also covers INVALID_HANDLE / malloc failure). */
    fwprintf(stderr, L"%s", msg);
}

/* Error buffer sized for a worst-case message: program name +
 * full 32K path + decoration. Kept static to avoid stack bloat. */
#define FAIL_BUF_CAP (LONG_PATH_CAP + 256)
static wchar_t failBuf[FAIL_BUF_CAP];

static void fail(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(failBuf, _countof(failBuf), _TRUNCATE, fmt, ap);
    va_end(ap);
    err_write(failBuf);
}

/* MSYS2's 8186 limit counts decoded characters, not UTF-16 code units, so
 * count surrogate pairs once (an emoji is one character). */
static size_t arg_char_count(const wchar_t *s)
{
    size_t n = 0;
    for (; *s; s++)
    {
        if (*s >= 0xD800 && *s <= 0xDBFF &&
            s[1] >= 0xDC00 && s[1] <= 0xDFFF)
            s++;
        n++;
    }
    return n;
}

/* Index of the -c command argument in argv, or -1. Handles option clusters
 * (-lc), --rcfile/--init-file FILE, and -o/-O ARG, and stops at -- or the
 * first non-option (script name / stdin). */
static int find_command_arg(int argc, wchar_t **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        const wchar_t *a = argv[i];
        const wchar_t *p;
        if (a[0] != L'-' || a[1] == L'\0')
            return -1;
        if (wcscmp(a, L"--") == 0)
            return -1;
        if (a[1] == L'-')
        {
            if (wcscmp(a, L"--rcfile") == 0 || wcscmp(a, L"--init-file") == 0)
                i++;
            continue;
        }
        for (p = a + 1; *p; p++)
        {
            if (*p == L'c')
                return (i + 1 < argc) ? i + 1 : -1;
            if (*p == L'o' || *p == L'O')
            {
                if (p[1] == L'\0')
                    i++; /* option takes the next argument */
                break;
            }
        }
    }
    return -1;
}

/* Stage a command string in a UTF-8 temp file for bash to eval. This
 * bypasses MSYS2's per-argument truncation. Returns 0 on success, -1 on
 * failure (temp file removed). The path is converted to forward slashes so
 * bash can read it directly. */
static int write_command_file(const wchar_t *cmd, wchar_t *path)
{
    wchar_t tempDir[LONG_PATH_CAP];
    DWORD dirLen;
    int bytes;
    char *utf8;
    HANDLE h;
    DWORD written = 0;
    BOOL ok;

    dirLen = GetTempPathW((DWORD)_countof(tempDir), tempDir);
    if (dirLen == 0 || dirLen >= _countof(tempDir))
        return -1;
    if (GetTempFileNameW(tempDir, L"ms2", 0, path) == 0)
        return -1;

    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                cmd, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
    {
        DeleteFileW(path);
        return -1;
    }
    utf8 = (char *)malloc((size_t)bytes);
    if (!utf8)
    {
        DeleteFileW(path);
        return -1;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            cmd, -1, utf8, bytes, NULL, NULL) <= 0)
    {
        free(utf8);
        DeleteFileW(path);
        return -1;
    }

    h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        free(utf8);
        DeleteFileW(path);
        return -1;
    }
    ok = WriteFile(h, utf8, (DWORD)(bytes - 1), &written, NULL) &&
         written == (DWORD)(bytes - 1);
    CloseHandle(h);
    free(utf8);
    if (!ok)
    {
        DeleteFileW(path);
        return -1;
    }

    for (wchar_t *q = path; *q; q++)
        if (*q == L'\\')
            *q = L'/';
    return 0;
}

int main()
{
    static wchar_t exePath[LONG_PATH_CAP];
    static wchar_t bashPath[LONG_PATH_CAP];
    wchar_t *lastSlash;
    wchar_t *lastFwd;
    wchar_t *cmdline = NULL;
    wchar_t *pos;
    wchar_t *end;
    const wchar_t *argTail;
    size_t bashQ;
    size_t loginQ;
    size_t need;
    size_t i;
    int argc;
    int cmdIndex;
    BOOL scriptCreated = FALSE;
    wchar_t scriptPath[LONG_PATH_CAP];
    wchar_t scriptCmd[160];
    LPWSTR *argv;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE hJob = NULL;
    DWORD exitCode;
    DWORD len;

    len = GetModuleFileNameW(NULL, exePath, LONG_PATH_CAP);
    if (len == 0 || len >= LONG_PATH_CAP)
    {
        fail(L"%s: cannot determine executable path\n", PROGRAM_NAME);
        return 1;
    }

    lastSlash = wcsrchr(exePath, L'\\');
    lastFwd = wcsrchr(exePath, L'/');
    if (lastFwd && (!lastSlash || lastFwd > lastSlash))
        lastSlash = lastFwd;
    if (!lastSlash)
    {
        fail(L"%s: unexpected executable path format\n", PROGRAM_NAME);
        return 1;
    }
    *lastSlash = L'\0';

    if (_snwprintf_s(bashPath, LONG_PATH_CAP, _TRUNCATE,
                     L"%s\\usr\\bin\\bash.exe", exePath) < 0)
    {
        fail(L"%s: executable path too long\n", PROGRAM_NAME);
        return 1;
    }

    /* Fast-path check for a friendly error (a directory is not bash either).
     * The authoritative check is CreateProcessW below; this can race. */
    {
        DWORD attrs = GetFileAttributesW(bashPath);
        if (attrs == INVALID_FILE_ATTRIBUTES ||
            (attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            fail(L"%s: %s not found\n", PROGRAM_NAME, bashPath);
            return 1;
        }
    }

    if (!SetEnvironmentVariableW(L"MSYSTEM", MSYSTEM_VALUE) ||
        !SetEnvironmentVariableW(L"CHERE_INVOKING", L"enabled_from_arguments"))
    {
        fail(L"%s: failed to set environment (error %lu)\n",
             PROGRAM_NAME, GetLastError());
        return 1;
    }

    /* Parse our own command line, then re-quote every arg with
     * append_quoted_arg_checked (which ALWAYS quotes). Re-quoting is
     * mandatory: MSYS/Cygwin's child-side parser globs unquoted *?[],
     * strips unquoted single quotes, and expands a bare ~ -- so a verbatim
     * raw tail would corrupt args the parent sent bare (e.g. '*.txt').
     * Tail parsing goes through parse_arg_tail (dummy-argv0 prefix):
     * CommandLineToArgvW decodes element 0 with different (argv0) grammar,
     * so parsing the bare tail would corrupt a quoted first user arg
     * containing a quote (e.g. '"a\"b"' -> 'a\' + 'b'). The argv0 shift
     * for spaced paths is handled by skipping the raw argv0 with
     * skip_argv0 and parsing only the tail; argv[1..] of THAT parse are
     * the real user args. */
    argTail = skip_argv0(GetCommandLineW(), WRAPPER_BASENAME, WRAPPER_STEM);
    argc = 0;
    argv = NULL;
    if (parse_arg_tail(argTail, &argv, &argc) != 0)
    {
        fail(L"%s: failed to parse command line (error %lu)\n",
             PROGRAM_NAME, GetLastError());
        return 1;
    }
    /* argv[0] is the dummy; user args start at argv[1]. argc >= 2 whenever
     * argv is non-NULL (parse_arg_tail frees the dummy-only block). */

    /* MSYS2's tokenizer silently truncates any single argument beyond ~8K
     * chars. The -c command is the one argument we can route around: stage
     * it in a temp file and eval it, preserving -c semantics ($0/$@/exit,
     * empty BASH_SOURCE, top-level return errors, BASH_EXECUTION_STRING). */
    cmdIndex = find_command_arg(argc, argv);
    if (cmdIndex > 0 && cmdIndex < argc &&
        arg_char_count(argv[cmdIndex]) > (size_t)MSYS_ARG_CAP)
    {
        if (write_command_file(argv[cmdIndex], scriptPath) != 0)
        {
            fail(L"%s: failed to stage long command\n", PROGRAM_NAME);
            LocalFree(argv);
            return 1;
        }
        if (!SetEnvironmentVariableW(MSYS_SCRIPT_ENV, scriptPath))
        {
            DWORD envErr = GetLastError();
            DeleteFileW(scriptPath);
            fail(L"%s: failed to set environment (error %lu)\n",
                 PROGRAM_NAME, envErr);
            LocalFree(argv);
            return 1;
        }
        if (_snwprintf_s(scriptCmd, _countof(scriptCmd), _TRUNCATE,
                         L"BASH_EXECUTION_STRING=\"$(<\"$%s\")\"; "
                         L"builtin eval \"$BASH_EXECUTION_STRING\"",
                         MSYS_SCRIPT_ENV) < 0)
        {
            DeleteFileW(scriptPath);
            fail(L"%s: internal error staging command\n", PROGRAM_NAME);
            LocalFree(argv);
            return 1;
        }
        argv[cmdIndex] = scriptCmd;
        scriptCreated = TRUE;
    }

    /* Any remaining overlong arg cannot be transported without changing
     * its position/env semantics, and MSYS2 would silently truncate it. */
    for (i = 1; i < (size_t)argc; i++)
    {
        if (arg_char_count(argv[i]) > (size_t)MSYS_ARG_CAP)
        {
            fail(L"%s: argument %zu is longer than MSYS2's %d-character "
                 L"command-line limit; split it or pass it in a file\n",
                 PROGRAM_NAME, i, MSYS_ARG_CAP);
            if (scriptCreated)
                DeleteFileW(scriptPath);
            LocalFree(argv);
            return 1;
        }
    }

    bashQ = quoted_arg_len(bashPath);
    loginQ = quoted_arg_len(L"--login");
    if (bashQ == (size_t)-1 || loginQ == (size_t)-1 ||
        loginQ > (size_t)MAX_CMDLINE - 1 ||
        bashQ > (size_t)MAX_CMDLINE - 1 - loginQ)
    {
        fail(L"%s: path too long to quote\n", PROGRAM_NAME);
        if (scriptCreated)
            DeleteFileW(scriptPath);
        if (argv)
            LocalFree(argv);
        return 1;
    }
    need = bashQ + 1 + loginQ;
    for (i = 1; i < (size_t)argc; i++)
    {
        size_t q = quoted_arg_len(argv[i]);
        /* Overflow-safe: reject if 1+q doesn't fit in remaining space.
         * (q is small in practice -- CommandLineToArgvW input is capped
         * at 32767 chars -- but never trust arithmetic that can wrap.
         * The need >= MAX_CMDLINE check must come first so the
         * subtraction below can't underflow.) */
        if (q == (size_t)-1 || need >= (size_t)MAX_CMDLINE ||
            q > (size_t)MAX_CMDLINE - need - 1)
        {
            fail(L"%s: command line too long (max %d chars)\n",
                 PROGRAM_NAME, MAX_CMDLINE);
            if (scriptCreated)
                DeleteFileW(scriptPath);
            if (argv)
                LocalFree(argv);
            return 1;
        }
        need += 1 + q;
    }

    cmdline = (wchar_t *)malloc((need + 1) * sizeof(wchar_t));
    if (!cmdline)
    {
        fail(L"%s: out of memory\n", PROGRAM_NAME);
        if (scriptCreated)
            DeleteFileW(scriptPath);
        if (argv)
            LocalFree(argv);
        return 1;
    }
    pos = cmdline;
    end = cmdline + need + 1; /* one past last writable wchar */
    pos = append_quoted_arg_checked(pos, end, bashPath);
    if (pos && pos < end)
        *pos++ = L' ';
    else
        pos = NULL;
    if (pos)
        pos = append_quoted_arg_checked(pos, end, L"--login");
    for (i = 1; pos && i < (size_t)argc; i++)
    {
        if (pos < end)
            *pos++ = L' ';
        else
            pos = NULL;
        if (pos)
            pos = append_quoted_arg_checked(pos, end, argv[i]);
    }
    if (!pos || pos >= end)
    {
        fail(L"%s: internal error building command line\n", PROGRAM_NAME);
        free(cmdline);
        if (scriptCreated)
            DeleteFileW(scriptPath);
        if (argv)
            LocalFree(argv);
        return 1;
    }
    *pos = L'\0';
    if (argv)
        LocalFree(argv);
    argv = NULL;

    /* Kill bash if the wrapper dies (close/kill of wrapper). Best effort:
     * nested-job environments may refuse assignment; we proceed regardless. */
    hJob = CreateJobObjectW(NULL, NULL);
    if (hJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                     &jeli, sizeof(jeli)))
        {
            CloseHandle(hJob);
            hJob = NULL;
        }
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(bashPath, cmdline, NULL, NULL, TRUE,
                        hJob ? CREATE_SUSPENDED : 0, NULL, NULL, &si, &pi))
    {
        fail(L"%s: failed to launch %s (error %lu)\n",
             PROGRAM_NAME, bashPath, GetLastError());
        free(cmdline);
        if (hJob)
            CloseHandle(hJob);
        if (scriptCreated)
            DeleteFileW(scriptPath);
        return 1;
    }
    free(cmdline);
    cmdline = NULL;

    if (hJob)
    {
        if (!AssignProcessToJobObject(hJob, pi.hProcess))
        {
            /* Already in another job: give up kill-on-close, keep running. */
            CloseHandle(hJob);
            hJob = NULL;
        }
        if (ResumeThread(pi.hThread) == (DWORD)-1)
        {
            /* Should never happen; bash would hang suspended forever.
             * Kill it and report instead of waiting indefinitely. */
            DWORD err = GetLastError();
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (hJob)
                CloseHandle(hJob);
            fail(L"%s: failed to resume %s (error %lu)\n",
                 PROGRAM_NAME, bashPath, err);
            if (scriptCreated)
                DeleteFileW(scriptPath);
            return 1;
        }
    }

    /* Ignore Ctrl-C while waiting; bash shares our console and handles
     * it itself. (Ctrl-Break is separately ignored only if bash opts in;
     * most console children die on Ctrl-Break regardless.) */
    SetConsoleCtrlHandler(NULL, TRUE);
    WaitForSingleObject(pi.hProcess, INFINITE);

    /* Normal completion: bash is gone, so drop kill-on-close before closing
     * the job. That lets intentionally backgrounded children (nohup ... &,
     * dev servers, GUI apps) outlive the shell, matching a normal terminal.
     * If the wrapper itself is killed, this path never runs and the job
     * still tears down bash and its descendants. */
    if (hJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = 0;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                &jeli, sizeof(jeli));
    }
    if (scriptCreated)
        DeleteFileW(scriptPath);
    SetConsoleCtrlHandler(NULL, FALSE);

    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
        exitCode = 1;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hJob)
        CloseHandle(hJob);

    return (int)exitCode;
}
