# msys2_wrappers

Tiny `.exe` wrappers that launch MSYS2 `bash --login` with the correct `MSYSTEM` env var — no mintty, no new window, no `.bat`/`.cmd` script.

## How it works

- `ucrt64_shell_wrapper.c` and `msys_shell_wrapper.c` are 3-line stubs that `#include "shell_wrapper.c"` after `#define`-ing `PROGRAM_NAME` and `MSYSTEM_VALUE`.
- `shell_wrapper.c` (shared) finds `<exe_dir>\usr\bin\bash.exe`, sets `MSYSTEM` and `CHERE_INVOKING`, and launches `bash --login` forwarding all args via `CreateProcessW`. Propagates exit code.

## Adding a new environment

Create a 3-line `.c` file:
```c
#define PROGRAM_NAME L"mingw64_shell_wrapper"
#define MSYSTEM_VALUE L"MINGW64"
#include "shell_wrapper.c"
```
Then add a `cl` line to `build.cmd`.

## Build

- Requires MSVC. Run `build.cmd` from a **VS Developer Command Prompt**, or from any shell after sourcing `vcvars64.bat`.
- The VS path in `build.cmd` (`VS\18`) is version-specific — adjust if needed.
- Flags: `cl /nologo /O1 /MD /GL /W4 /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib`
- Outputs are gitignored (`*.exe`) — you always need to build locally.

## Notable

- No test suite — verify by running the produced `.exe` from a terminal.
- No CI.
