# msys2_wrappers

Tiny `.exe` shell wrappers for MSYS2 environments — drop them in `C:\msys64` and point your IDE or terminal at them.

No mintty, no new window, no `.bat`/`.cmd` scripts that IDEs refuse to accept. Just a proper console executable that launches `bash --login` with the correct `MSYSTEM` environment.

## Why?

MSYS2 ships with `ucrt64.exe`, `clang64.exe`, `mingw64.exe`, etc., but those launch **mintty** (a new terminal window). Tools like VS Code, CLion, and agentic harnesses need a shell that runs in-place in their integrated terminal. They also won't accept a `.cmd` or `.bat` as a shell.

These wrappers are the equivalent of running:

```
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64
```

but as a proper console `.exe`.

## Usage

Drop the `.exe`(s) for the environments you need into `C:\msys64`.

### VS Code

```jsonc
"terminal.integrated.profiles.windows": {
    "UCRT64": {
        "path": "C:\\msys64\\ucrt64_shell_wrapper.exe"
    },
    "CLANG64": {
        "path": "C:\\msys64\\clang64_shell_wrapper.exe"
    },
    "MSYS": {
        "path": "C:\\msys64\\msys_shell_wrapper.exe"
    }
},
"terminal.integrated.defaultProfile.windows": "UCRT64"
```

### CLion / JetBrains IDEs

Settings → Tools → Terminal → Shell path:

```
C:\msys64\ucrt64_shell_wrapper.exe
```

### Windows Terminal

Add a profile:

```json
{
    "name": "UCRT64",
    "commandline": "C:\\msys64\\ucrt64_shell_wrapper.exe"
}
```

## What it does

Each wrapper:

1. Finds `usr\bin\bash.exe` relative to its own location
2. Sets `MSYSTEM` to the appropriate value (`UCRT64`, `CLANG64`, `MSYS`, etc.) and `CHERE_INVOKING=enabled_from_arguments`
3. Launches `bash --login` in the current console, passing through all arguments
4. Propagates the exit code

Failures write a diagnostic to stderr.

## Build

Requires MSVC (Visual Studio). Run from a **Developer Command Prompt for VS 2022+**:

```
build.cmd
```

> **Note:** `build.cmd` has the VS path hardcoded as `C:\Program Files\Microsoft Visual Studio\18\...`. If your VS version or install path differs, adjust the `call` line in it to match your `vcvars64.bat`.

This compiles all wrappers and the test binary.

Or from PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /O1 /MD /GL /W4 ucrt64_shell_wrapper.c /Fe:ucrt64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
cl /nologo /O1 /MD /GL /W4 clang64_shell_wrapper.c /Fe:clang64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
cl /nologo /O1 /MD /GL /W4 msys_shell_wrapper.c /Fe:msys_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
```

Each binary is ~12KB.

## Tests

```bash
# C quoting unit tests (roundtrip through CommandLineToArgvW)
test_quoting.exe

# Integration tests (env, exit codes, arg passthrough)
bash test_wrapper.sh
```

## Adding other environments

Create a 3-line `.c` file:

```c
#define PROGRAM_NAME L"mingw64_shell_wrapper"
#define MSYSTEM_VALUE L"MINGW64"
#include "shell_wrapper.c"
```

Then compile it.

## Project structure

| File | Purpose |
|------|---------|
| `shell_wrapper.c` | Common implementation (140 lines) |
| `ucrt64_shell_wrapper.c` | 3-line wrapper, sets `MSYSTEM=UCRT64` |
| `clang64_shell_wrapper.c` | 3-line wrapper, sets `MSYSTEM=CLANG64` |
| `msys_shell_wrapper.c` | 3-line wrapper, sets `MSYSTEM=MSYS` |
| `test_quoting.c` | C unit tests for quoting logic |
| `test_wrapper.sh` | Integration tests |
| `build.cmd` | One-liner to rebuild all |
