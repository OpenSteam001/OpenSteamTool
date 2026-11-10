@echo off
setlocal EnableExtensions
echo Checking Windows C++ build prerequisites...
set "CMAKE_EXE="
for %%C in (cmake.exe) do if not "%%~$PATH:C"=="" set "CMAKE_EXE=%%~$PATH:C"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%V"
if not defined CMAKE_EXE if defined VSROOT if exist "%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
  echo [ERROR] CMake was not found. Install CMake 3.20+ or add its bin folder to PATH.
  exit /b 1
)
echo [OK] CMake: "%CMAKE_EXE%"
"%CMAKE_EXE%" --version
if not defined VSROOT (
  echo [ERROR] Visual Studio Build Tools not found. Install the Desktop development with C++ workload.
  exit /b 1
)
echo [OK] Visual Studio: "%VSROOT%"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
  echo [ERROR] x64 MSVC tools missing. Add the Desktop development with C++ workload.
  exit /b 1
)
echo [OK] x64 C++ environment available.
echo This check does not compile, install, or change Steam files.
exit /b 0
