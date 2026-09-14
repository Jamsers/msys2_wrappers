#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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

/* Max command line for CreateProcessW, in chars excluding NUL. */
#define MAX_CMDLINE 32767
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

static void fail(const wchar_t *fmt, ...)
{
    wchar_t buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    err_write(buf);
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
     * The argv0 shift for spaced paths is handled by skipping the raw
     * argv0 tail and re-parsing only the tail: argv[0] of THAT parse is the
     * real first user arg. */
    argTail = skip_argv0(GetCommandLineW(), WRAPPER_BASENAME, WRAPPER_STEM);
    argv = CommandLineToArgvW(argTail, &argc);
    if (!argv)
    {
        fail(L"%s: failed to parse command line (error %lu)\n",
             PROGRAM_NAME, GetLastError());
        return 1;
    }

    bashQ = quoted_arg_len(bashPath);
    loginQ = quoted_arg_len(L"--login");
    if (bashQ == (size_t)-1 || loginQ == (size_t)-1)
    {
        fail(L"%s: path too long to quote\n", PROGRAM_NAME);
        LocalFree(argv);
        return 1;
    }
    need = bashQ + 1 + loginQ;
    for (i = 0; i < (size_t)argc; i++)
    {
        size_t q = quoted_arg_len(argv[i]);
        if (q == (size_t)-1 || need > MAX_CMDLINE - (1 + q))
        {
            fail(L"%s: command line too long (max %d chars)\n",
                 PROGRAM_NAME, MAX_CMDLINE);
            LocalFree(argv);
            return 1;
        }
        need += 1 + q;
    }

    cmdline = (wchar_t *)malloc((need + 1) * sizeof(wchar_t));
    if (!cmdline)
    {
        fail(L"%s: out of memory\n", PROGRAM_NAME);
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
    for (i = 0; pos && i < (size_t)argc; i++)
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
        LocalFree(argv);
        return 1;
    }
    *pos = L'\0';
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
        ResumeThread(pi.hThread);
    }

    /* Ignore Ctrl-C/Ctrl-Break in the wrapper while waiting; bash shares
     * our console and handles them itself. */
    SetConsoleCtrlHandler(NULL, TRUE);
    WaitForSingleObject(pi.hProcess, INFINITE);
    SetConsoleCtrlHandler(NULL, FALSE);

    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
        exitCode = 1;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hJob)
        CloseHandle(hJob);

    return (int)exitCode;
}
