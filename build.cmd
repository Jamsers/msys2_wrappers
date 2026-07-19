@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /O1 /MD /GL /W4 ucrt64_shell_wrapper.c /Fe:ucrt64_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
cl /nologo /O1 /MD /GL /W4 msys_shell_wrapper.c /Fe:msys_shell_wrapper.exe /link /LTCG /SUBSYSTEM:CONSOLE shell32.lib
del *.obj 2>nul
echo Done.
