@echo off
setlocal

rem If cl is already on PATH (Developer Prompt), skip vcvars. Otherwise try
rem the standard VS install locations. Override with VCVARS64 env var.
where cl >nul 2>nul
if %errorlevel% equ 0 goto :have_cl

if defined VCVARS64 (
  if not exist "%VCVARS64%" (
    echo ERROR: VCVARS64="%VCVARS64%" not found.
    exit /b 1
  )
  call "%VCVARS64%" >nul
) else if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
) else if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
) else if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
)

:have_cl
where cl >nul 2>nul
if %errorlevel% neq 0 (
  echo ERROR: MSVC cl.exe not found. Run from a Developer Command Prompt,
  echo or set VCVARS64 to your vcvars64.bat path.
  exit /b 1
)

set CFLAGS=/nologo /utf-8 /O1 /MT /GL /W4 /WX
set LDFLAGS=/link /LTCG /SUBSYSTEM:CONSOLE shell32.lib

echo === Building ucrt64_shell_wrapper.exe ===
cl %CFLAGS% ucrt64_shell_wrapper.c /Fe:ucrt64_shell_wrapper.exe %LDFLAGS%
if %errorlevel% neq 0 exit /b 1

echo === Building msys_shell_wrapper.exe ===
cl %CFLAGS% msys_shell_wrapper.c /Fe:msys_shell_wrapper.exe %LDFLAGS%
if %errorlevel% neq 0 exit /b 1

echo === Building clang64_shell_wrapper.exe ===
cl %CFLAGS% clang64_shell_wrapper.c /Fe:clang64_shell_wrapper.exe %LDFLAGS%
if %errorlevel% neq 0 exit /b 1

echo === Building test_quoting.exe ===
cl /nologo /utf-8 /W4 /WX test_quoting.c /Fe:test_quoting.exe /link shell32.lib
if %errorlevel% neq 0 exit /b 1

del *.obj *.ilk *.pdb *.ipdb *.iobj 2>nul

echo.
echo Build succeeded. Run tests:
echo   test_quoting.exe          -- C quoting unit tests
echo   bash test_wrapper.sh      -- integration tests
