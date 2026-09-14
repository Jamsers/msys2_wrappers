# msys2_wrappers

Tiny `.exe` wrappers that launch MSYS2 `bash --login` with the correct `MSYSTEM` env var — no mintty, no new window, no `.bat`/`.cmd` script.

## Architecture

Each wrapper is a 3-line `.c` stub (`#define PROGRAM_NAME` + `#define MSYSTEM_VALUE` + `#include "shell_wrapper.c"`). The shared `shell_wrapper.c` finds `<exe_dir>\usr\bin\bash.exe`, sets `MSYSTEM` and `CHERE_INVOKING`, and launches `bash --login` forwarding the raw argument tail verbatim via `CreateProcessW` (skips argv0 with `skip_argv0` from `quoting.h`, no re-parsing). Exit code is propagated; a Job Object with kill-on-close ensures bash dies with the wrapper.

Wrappers exist for: `UCRT64`, `CLANG64`, `MSYS`.

## Adding a new environment

Create a 3-line `.c` file following the existing pattern (e.g. `clang64_shell_wrapper.c`), then add a `cl` line to `build.cmd`.

## Build

- **MSVC required**. Run `build.cmd` from a VS Developer Command Prompt (or plain prompt — it locates `vcvars64.bat` itself, `VCVARS64` env var overrides).
- Flags: `cl /nologo /utf-8 /O1 /MT /GL /W4 /WX /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib` (static CRT, no VC redist needed)
- `test_quoting.exe` is built without `/O1 /MT /GL /LTCG`.
- `build.cmd` stops on first failed compile (`exit /b 1`).
- Outputs are gitignored (`*.exe`, `*.obj`, `*.ilk`, `*.pdb`, `*.ipdb`, `*.iobj`).

## Tests

```shell
test_quoting.exe           # unit tests for quoting.h (roundtrip via CommandLineToArgvW, argv0 skipping)
bash test_wrapper.sh       # integration tests: env, exit codes, arg passthrough
```

- Integration tests require wrappers deployed to `C:\msys64\` (copied automatically by the script; override root with `MSYS2_ROOT=/path`). Copy failures abort, never silently test stale exes.
- Quoting logic lives in `quoting.h`, shared by `shell_wrapper.c` and `test_quoting.c` — no manual sync.

## Gotchas

- `quoting.h` implements Win32 `CommandLineToArgvW` escaping rules (backslash-quote combinations) and always quotes every arg — MSYS/Cygwin's parser mangles unquoted `*?[]'`, newlines, and `~`, so never emit bare args. If you change it, roundtrip-test with `test_quoting.exe`.
- No CI. Always build and run tests locally to verify.
