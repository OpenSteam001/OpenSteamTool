@echo off
setlocal EnableDelayedExpansion

REM Always run from the script directory.
cd /d "%~dp0"

REM ---------------------------------------------------------------------------
REM Configurable build options
REM   GENERATOR  - CMake generator (default: auto-detect)
REM   ARCH       - Architecture for multi-config generators (default: x64)
REM   CONFIGS    - Configurations to build, space-separated (default: Release Debug)
REM ---------------------------------------------------------------------------
if "%GENERATOR%"=="" (
    where ninja >nul 2>nul
    if not errorlevel 1 (
        set "GENERATOR=Ninja Multi-Config"
    ) else (
        set "GENERATOR=Visual Studio 17 2022"
    )
)
if "%ARCH%"=="" set "ARCH=x64"
if "%CONFIGS%"=="" set "CONFIGS=Release Debug"

echo [INFO] Configuring with generator: %GENERATOR%
echo "%GENERATOR%" | findstr /I /C:"Visual Studio" >nul
if not errorlevel 1 (
    cmake -S src -B build -G "%GENERATOR%" -A %ARCH%
) else (
    cmake -S src -B build -G "%GENERATOR%"
)
if errorlevel 1 goto :fail

for %%C in (%CONFIGS%) do (
    echo [INFO] Building: %%C
    cmake --build build --config %%C
    if errorlevel 1 goto :fail

    REM extract_tickets is EXCLUDE_FROM_ALL, so build it explicitly. It lands in
    REM build\tools\%%C\ rather than the shipped output directory.
    echo [INFO] Building tool extract_tickets for %%C
    cmake --build build --config %%C --target extract_tickets
    if errorlevel 1 goto :fail
)

echo [OK] Build completed successfully.
exit /b 0

:fail
echo [ERROR] Build failed.
exit /b 1
