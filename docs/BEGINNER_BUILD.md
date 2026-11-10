# Beginner Windows build guide

This guide covers compiling the source in this fork. It does **not** install anything into Steam, and a successful build does not establish runtime compatibility.

## Requirements

- Windows 10 or 11
- Visual Studio 2022 or 2026 Build Tools with **Desktop development with C++** and the Windows SDK
- CMake 3.20 or newer (the Visual Studio bundled CMake is supported)
- Git and network access to fetch the project's open-source dependencies on the first build

## Check before building

From the repository root, open Command Prompt and run:

```bat
beginner-build.bat --check-only
```

The check locates CMake and the x64 Visual C++ environment, and selects the appropriate Visual Studio CMake generator. It does not compile or install anything.

## Compile a Release build

```bat
beginner-build.bat
```

The launcher sets `_CL_=/MP1` unless you have explicitly provided your own `_CL_` compiler options. This limits MSVC compiler-level parallelism, which resolved internal compiler failures observed while building the older Protobuf dependency with Visual Studio 2026. The build also uses one CMake build worker. This is a conservative compatibility default, not a guarantee for all machines.

On success, inspect these files under `build/Release/`:

- `OpenSteamTool.dll`
- `dwmapi.dll`
- `xinput1_4.dll`

If a build fails, record the first fatal compiler error and the final exit code. Do not copy partially built DLLs into a live Steam installation.

## Scope and credits

This fork preserves the original upstream project's history and GPLv3 licensing. The beginner scripts are maintenance improvements, not a claim of authorship of upstream code or a fix for Steam runtime signature warnings.
