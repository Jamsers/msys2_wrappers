# msys2_wrappers

Small C wrappers that launch MSYS2 bash from a convenient `.exe`.

## Architecture

- `ucrt64_shell_wrapper.c` and `msys_shell_wrapper.c` are thin stubs that `#include "shell_wrapper.c"` after `#define`-ing `PROGRAM_NAME` and `MSYSTEM_VALUE` — they are **not** independent compilation units.
- `shell_wrapper.c` finds `<exe_dir>\usr\bin\bash.exe`, sets `MSYSTEM` and `CHERE_INVOKING` env vars, then launches `bash --login` forwarding all arguments.
- The only difference between the two wrappers is the `MSYSTEM` value (`UCRT64` vs `MSYS`).

## Build

- Requires MSVC (VS 2019, v18). Run `build.cmd` from a Developer Command Prompt, or from any shell after calling `vcvars64.bat`.
- Produces `ucrt64_shell_wrapper.exe` and `msys_shell_wrapper.exe`.
- Flags: `/O1 /MD /GL /W4 /link /LTCG`.

## Notable

- No README, no tests, no CI, no `.gitignore`.
