# msys2_wrappers

Tiny `.exe` shell wrappers for MSYS2 environments — drop them in `C:\msys64` and point your IDE or terminal at them.

No mintty, no new window, no `.bat`/`.cmd` scripts that IDEs refuse to accept. Just a proper console executable that launches `bash --login` with the correct MSYSTEM environment.

## Why?

MSYS2 ships with `ucrt64.exe`, `mingw64.exe`, etc., but those launch **mintty** (a new terminal window). Tools like VS Code, CLion, and agentic harnesses need a shell that runs in-place in their integrated terminal. They also won't accept a `.cmd` or `.bat` as a shell.

These wrappers are the equivalent of running:

```
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64
```

but as a proper console `.exe`.

## Usage

Drop `ucrt64_shell_wrapper.exe` and/or `msys_shell_wrapper.exe` into `C:\msys64`.

### VS Code

```jsonc
"terminal.integrated.profiles.windows": {
    "UCRT64": {
        "path": "C:\\msys64\\ucrt64_shell_wrapper.exe"
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
2. Sets `MSYSTEM=UCRT64` (or `MSYS`) and `CHERE_INVOKING=enabled_from_arguments`
3. Launches `bash --login` in the current console, passing through all arguments
4. Propagates the exit code

Failures write a diagnostic to stderr.

## Build

Requires MSVC (Visual Studio). Open a **Developer Command Prompt for VS 2022+** and run:

```
build.cmd
```

Or from PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /O1 /MD /GL /W4 ucrt64_shell_wrapper.c /Fe:ucrt64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
cl /nologo /O1 /MD /GL /W4 msys_shell_wrapper.c /Fe:msys_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
```

Each binary is ~12KB.

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
| `msys_shell_wrapper.c` | 3-line wrapper, sets `MSYSTEM=MSYS` |
| `build.cmd` | One-liner to rebuild both |
