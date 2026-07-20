# msys2_wrappers

Tiny `.exe` wrappers that launch MSYS2 `bash --login` with the correct `MSYSTEM` env var — no mintty, no new window, no `.bat`/`.cmd` script.

## Architecture

Each wrapper is a 3-line `.c` stub (`#define PROGRAM_NAME` + `#define MSYSTEM_VALUE` + `#include "shell_wrapper.c"`). The shared `shell_wrapper.c` finds `<exe_dir>\usr\bin\bash.exe`, sets `MSYSTEM` and `CHERE_INVOKING`, and launches `bash --login` forwarding all args via `CreateProcessW`. Exit code is propagated.

Wrappers exist for: `UCRT64`, `CLANG64`, `MSYS`.

## Adding a new environment

Create a 3-line `.c` file following the existing pattern (e.g. `clang64_shell_wrapper.c`), then add a `cl` line to `build.cmd`.

## Build

- **MSVC required**. Run `build.cmd` from a VS Developer Command Prompt (or source `vcvars64.bat`).
- VS version path in `build.cmd` (`VS\18`) is version-specific — adjust if needed.
- Flags: `cl /nologo /O1 /MD /GL /W4 /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib`
- `test_quoting.exe` is built without `/O1 /GL /LTCG`.
- Outputs are gitignored (`*.exe`, `*.obj`, `*.ilk`, `*.pdb`).

## Tests

```shell
test_quoting.exe           # unit tests for quoting logic (roundtrip via CommandLineToArgvW)
bash test_wrapper.sh       # integration tests: env, exit codes, arg passthrough
```

- Integration tests require wrappers deployed to `C:\msys64\` (copied automatically by the script).
- `test_quoting.c` duplicates `append_quoted_arg` from `shell_wrapper.c` — keep both in sync manually.

## Gotchas

- The quoting logic in `shell_wrapper.c` implements Win32 `CommandLineToArgvW` escaping rules (backslash-quote combinations). This is the most error-prone part — if you change it, roundtrip-test with `test_quoting.exe`.
- No CI. Always build and run tests locally to verify.
