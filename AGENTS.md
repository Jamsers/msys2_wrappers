# msys2_wrappers

Tiny `.exe` wrappers that launch MSYS2 `bash --login` with the correct `MSYSTEM` env var — no mintty, no new window, no `.bat`/`.cmd` script.

## Architecture

Each wrapper is a 3-line `.c` stub (`#define PROGRAM_NAME` + `#define MSYSTEM_VALUE` + `#include "shell_wrapper.c"`). The shared `shell_wrapper.c` finds `<exe_dir>\usr\bin\bash.exe`, sets `MSYSTEM` and `CHERE_INVOKING`, and launches `bash --login` via `CreateProcessW`, skipping argv0 with `skip_argv0` from `quoting.h`, parsing the tail with `parse_arg_tail` (dummy-`x` argv0 prefix, since `CommandLineToArgvW` decodes element 0 with different argv0 grammar; empty tail = zero user args), and re-quoting every arg for bash's parser. Exit code is propagated. A Job Object with kill-on-close kills bash and its descendants if the wrapper is killed; on normal bash exit the limit is cleared first so `nohup ... &`/background children survive. A `-c` command over MSYS2's 8186-character per-argument cap is staged in `%TEMP%` and evaluated with `eval "$(<file)"` (`MSYS2_WRAPPER_SCRIPT`); overlong non-command args are rejected, never truncated.

Wrappers exist for: `UCRT64`, `CLANG64`, `MSYS`.

## Quoting dialect (the hard part)

The emitted command line is parsed by the **MSYS runtime's command-line tokenizer** (bash's argv source on the native-parent path) — NOT by `CommandLineToArgvW`, even though the wrapper's own input is CTA-parsed. Tokenizer rules inside double quotes (verified empirically against `C:\msys64\usr\bin\bash.exe`): `\\` → `\` (**pairs are halved**), `\"` → `"`, `\X` → `\X` literal; unquoted backslashes are literal.

- `quoting.h` emits mid-word backslash runs **doubled** so the halving restores them — output is deliberately non-CTA there. Runs before a quote (`2n` backslashes + `\"`) and at end-of-string (`2n`) decode identically under both dialects and must stay as they are.
- The input side (`parse_arg_tail`) stays CTA: all known parents quote CTA-style, including the MSYS spawner when its child is a native exe.
- MSYS→MSYS spawns hand the child its argv directly (no command-line round-trip), so dialect bugs are **invisible when testing from a bash script**. Compare wrapper output against direct bash with backslashes built inside bash (`bs=$(printf '\134')`) or drive from a native parent. Each wrapper hop halves un-doubled pairs — including pairs in your own test literals in transit.
- Roundtrip tests must target the consumer's parser: `msys_decode_next` in `test_quoting.c` models the tokenizer. A CTA roundtrip oracle is self-consistent by construction and cannot see this bug class.

## Adding a new environment

Create a 3-line `.c` file following the existing pattern (e.g. `clang64_shell_wrapper.c`), then add a `cl` line to `build.cmd`.

## Build

- **MSVC required**. Run `build.cmd` from a VS Developer Command Prompt (or plain prompt — it locates `vcvars64.bat` itself, `VCVARS64` env var overrides). From the MSYS bash shell: `cmd /c build.cmd`.
- Flags: `cl /nologo /utf-8 /O1 /MT /GL /W4 /WX /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib` (static CRT, no VC redist needed)
- `test_quoting.exe` is built without `/O1 /MT /GL /LTCG`.
- `build.cmd` stops on first failed compile (`exit /b 1`).
- Outputs are gitignored (`*.exe`, `*.obj`, `*.ilk`, `*.pdb`, `*.ipdb`, `*.iobj`).

## Tests

```shell
test_quoting.exe           # quoting.h unit tests: MSYS-tokenizer roundtrip (primary), CTA agreement, argv0/tail parsing
bash test_wrapper.sh       # integration: env, exit codes, args, background, long commands
```

- Integration tests deploy the wrappers next to `$MSYS2_ROOT/usr/bin/bash.exe` (default `/c/msys64`) before running. Deployment is copy-to-`.new` then `mv`, so it works even while a wrapper exe is busy (the normal case when this shell was launched through one). `MSYS2_ROOT` still exists for a scratch tree (copy `usr/bin/bash.exe` in; its DLLs resolve via the real tree's `PATH` entry). `test_wrapper.sh` includes regressions for long `-c` commands and for background children surviving a normal exit.
- To extend: `check_passthrough "desc" 'arg'` in `test_wrapper.sh` (compares wrapper vs direct bash and guards against vacuous delivery), `test_roundtrip(L"...", L"label")` in `test_quoting.c`.
- Quoting logic lives in `quoting.h`, shared by `shell_wrapper.c` and `test_quoting.c` — no manual sync.

## Gotchas

- Never emit bare args from `quoting.h`: MSYS mangles unquoted `*?[]'`, newlines, and `~`. If you touch quoting, run both suites.
- MSYS2 silently truncates any **single** command-line argument at 8186 chars, regardless of quoting. The wrapper transports overlong `-c` commands via `%TEMP%` + `eval`; do not "fix" a long-command bug by adding more quoting. Overlong positional args must stay a clear error.
- The Job Object uses kill-on-close only for abnormal wrapper death. `shell_wrapper.c` clears the limit after `WaitForSingleObject` on the normal path; removing that clears the `nohup`/background-survival behavior tested in `test_wrapper.sh`.
- Known limitation, deliberately unfixed: with a renamed wrapper (argv0 lacks the wrapper name), a *user arg* that is a path ending in the wrapper name is swallowed as argv0 by `skip_argv0`, silently degrading the invocation to a bare login shell. Repro shape: `other.exe -c '...' _ 'C:\fake\ucrt64_shell_wrapper.exe'`.
- No CI. Always build and run tests locally to verify.
