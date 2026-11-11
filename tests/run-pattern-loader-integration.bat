@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "ROOT=%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 2
set "VSROOT="
for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%V"
if not defined VSROOT exit /b 2
if not defined OST_DEPS_DIR set "OST_DEPS_DIR=%ROOT%\.deps"
if not exist "%OST_DEPS_DIR%\tomlplusplus-src\include\toml++\toml.hpp" (
  echo CMake dependency cache missing. Set OST_DEPS_DIR or configure the project first.
  exit /b 2
) 
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 2
set "TESTDIR=%TEMP%\ost-pattern-%RANDOM%-%RANDOM%"
mkdir "%TESTDIR%"
if errorlevel 1 exit /b 2
pushd "%TESTDIR%"
cl /nologo /std:c++20 /EHsc /I"%ROOT%\src" /I"%ROOT%\src\OSTPlatform" /I"%ROOT%\build\generated" /I"%OST_DEPS_DIR%\tomlplusplus-src\include" "%ROOT%\tests\pattern_loader_recovery_integration_test.cpp" "%ROOT%\src\Utils\SteamMetadata\PatternLoader.cpp" "%ROOT%\src\OSTPlatform\Numbers.cpp" "%ROOT%\src\OSTPlatform\Encoding.cpp" /Fe:"%TESTDIR%\pattern_loader_test.exe"
if errorlevel 1 (set "RESULT=2") else (
  "%TESTDIR%\pattern_loader_test.exe"
  set "RESULT=!ERRORLEVEL!"
)
popd
rmdir /s /q "%TESTDIR%"
exit /b %RESULT%
