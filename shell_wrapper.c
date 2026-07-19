#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

#ifndef PROGRAM_NAME
#define PROGRAM_NAME L"msys_shell_wrapper"
#endif

#ifndef MSYSTEM_VALUE
#define MSYSTEM_VALUE L"MSYS"
#endif

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

int main()
{
    wchar_t exePath[MAX_PATH];
    wchar_t bashPath[MAX_PATH];
    wchar_t *lastSlash;
    wchar_t cmdline[32768];
    wchar_t *pos;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exitCode;
    int argc;
    LPWSTR *argv;

    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0)
    {
        fwprintf(stderr, L"%s: cannot determine executable path\n", PROGRAM_NAME);
        return 1;
    }

    lastSlash = wcsrchr(exePath, L'\\');
    if (!lastSlash)
    {
        fwprintf(stderr, L"%s: unexpected executable path format\n", PROGRAM_NAME);
        return 1;
    }
    *lastSlash = L'\0';

    if (_snwprintf_s(bashPath, MAX_PATH, _TRUNCATE, L"%s\\usr\\bin\\bash.exe", exePath) < 0)
    {
        fwprintf(stderr, L"%s: executable path too long\n", PROGRAM_NAME);
        return 1;
    }

    if (GetFileAttributesW(bashPath) == INVALID_FILE_ATTRIBUTES)
    {
        fwprintf(stderr, L"%s: %s not found\n", PROGRAM_NAME, bashPath);
        return 1;
    }

    SetEnvironmentVariableW(L"MSYSTEM", MSYSTEM_VALUE);
    SetEnvironmentVariableW(L"CHERE_INVOKING", L"enabled_from_arguments");

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        fwprintf(stderr, L"%s: failed to parse command line\n", PROGRAM_NAME);
        return 1;
    }

    pos = cmdline;
    pos = append_quoted_arg(pos, bashPath);
    *pos++ = L' ';
    pos = append_quoted_arg(pos, L"--login");
    for (int i = 1; i < argc; i++)
    {
        *pos++ = L' ';
        pos = append_quoted_arg(pos, argv[i]);
    }
    *pos = L'\0';

    LocalFree(argv);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(bashPath, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        fwprintf(stderr, L"%s: failed to launch %s (error %lu)\n",
                 PROGRAM_NAME, bashPath, GetLastError());
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
        exitCode = 1;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)exitCode;
}
