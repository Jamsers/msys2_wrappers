@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

echo === Building ucrt64_shell_wrapper.exe ===
cl /nologo /O1 /MD /GL /W4 ucrt64_shell_wrapper.c /Fe:ucrt64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib

echo === Building msys_shell_wrapper.exe ===
cl /nologo /O1 /MD /GL /W4 msys_shell_wrapper.c /Fe:msys_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib

echo === Building clang64_shell_wrapper.exe ===
cl /nologo /O1 /MD /GL /W4 clang64_shell_wrapper.c /Fe:clang64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib

echo === Building test_quoting.exe ===
cl /nologo /W4 test_quoting.c /Fe:test_quoting.exe /link shell32.lib

del *.obj 2>nul

echo.
echo Done. Run tests:
echo   test_quoting.exe          -- C quoting unit tests
echo   bash test_wrapper.sh      -- integration tests
