@echo off
setlocal EnableExtensions
cd /d "%~dp0"
call "%~dp0check-build-tools.bat"
if errorlevel 1 exit /b 1
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
set "VSMAJOR="
for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%V"
for /f "usebackq tokens=1 delims=." %%V in (`"%VSWHERE%" -latest -products * -property installationVersion`) do set "VSMAJOR=%%V"
if not defined VSMAJOR (
  echo [ERROR] Could not detect Visual Studio version.
  exit /b 1
)
if not defined GENERATOR (
  if "%VSMAJOR%"=="18" set "GENERATOR=Visual Studio 18 2026"
  if "%VSMAJOR%"=="17" set "GENERATOR=Visual Studio 17 2022"
)
if not defined GENERATOR (
  echo [ERROR] Unsupported Visual Studio version %VSMAJOR%. Set GENERATOR manually.
  exit /b 1
)
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
echo [INFO] Selected generator: %GENERATOR%
if /I "%~1"=="--check-only" (echo [OK] Build prerequisites and generator ready. & exit /b 0)
if not defined _CL_ set "_CL_=/MP1"
echo [INFO] Using compiler options: %_CL_%
echo [INFO] Configuring a Release build without installing into Steam.
cmake -S src -B build -G "%GENERATOR%" -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release --parallel 1
exit /b %ERRORLEVEL%
