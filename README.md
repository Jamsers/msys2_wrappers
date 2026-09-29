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

1. Finds `usr\bin\bash.exe` relative to its own location (long paths supported)
2. Sets `MSYSTEM` to the appropriate value (`UCRT64`, `CLANG64`, `MSYS`, etc.) and `CHERE_INVOKING=enabled_from_arguments`
3. Launches `bash --login` in the current console, re-parsing and re-quoting every argument (always double-quoted, so MSYS/Cygwin's parser can't glob `*?[]` or strip `'`/`~`; mid-word backslash runs are emitted doubled because that parser halves `\\` pairs inside double quotes)
4. Propagates the exit code; kills `bash` if the wrapper itself is killed
5. Refuses command lines over 32767 chars with a clear error instead of overflowing

Failures write a diagnostic to stderr.

## Build

Requires MSVC (Visual Studio 2022 or later). Run from a **Developer Command Prompt**:

```
build.cmd
```

`build.cmd` also works from a plain prompt: it uses `cl` if already on `PATH`, else tries the standard VS install locations (`VS\18` and `VS\2022` Community/Professional/Enterprise, `VS\2022` BuildTools). Override with the `VCVARS64` env var pointing at your `vcvars64.bat` (a bad path fails loudly). It stops on the first failed compile (`exit /b 1`).

This compiles all wrappers and the test binary.

Or compile manually from a Developer Command Prompt:

```
cl /nologo /utf-8 /O1 /MT /GL /W4 /WX ucrt64_shell_wrapper.c /Fe:ucrt64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
cl /nologo /utf-8 /O1 /MT /GL /W4 /WX clang64_shell_wrapper.c /Fe:clang64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
cl /nologo /utf-8 /O1 /MT /GL /W4 /WX msys_shell_wrapper.c /Fe:msys_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
```

(The static `/MT` runtime makes each wrapper a drop-in single file with no VC redist dependency.)

Each binary is ~150KB.

## Tests

```bash
# C quoting unit tests (MSYS-tokenizer roundtrip + CommandLineToArgvW
# agreement, argv0 skipping)
test_quoting.exe

# Integration tests (env, exit codes, arg passthrough, error paths)
# Uses /c/msys64 by default; override with MSYS2_ROOT=/path/to/msys64
bash test_wrapper.sh
```

## Adding other environments

Create a 3-line `.c` file:

```c
#define PROGRAM_NAME L"mingw64_shell_wrapper"
#define MSYSTEM_VALUE L"MINGW64"
#include "shell_wrapper.c"
```

Then add a `cl` line for it in `build.cmd` and compile.

## Project structure

| File | Purpose |
|------|---------|
| `shell_wrapper.c` | Common implementation (re-quote args, spawn bash) |
| `quoting.h` | Shared quoting + argv0-skipping logic |
| `ucrt64_shell_wrapper.c` | 3-line wrapper, sets `MSYSTEM=UCRT64` |
| `clang64_shell_wrapper.c` | 3-line wrapper, sets `MSYSTEM=CLANG64` |
| `msys_shell_wrapper.c` | 3-line wrapper, sets `MSYSTEM=MSYS` |
| `test_quoting.c` | C unit tests for quoting logic |
| `test_wrapper.sh` | Integration tests |
| `build.cmd` | Rebuilds everything, stops on first error |
