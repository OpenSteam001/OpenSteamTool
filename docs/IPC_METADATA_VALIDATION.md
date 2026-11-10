# IPC metadata validation: reject empty method registries

## Problem

The `IPCLoader::Load` function returned success after parsing any syntactically valid TOML, even when every interface was unrecognized or no usable IPC methods were present. This could make an invalid compatibility metadata file appear successfully loaded in diagnostics.

## Change

After parsing the metadata, `Load` now checks `MethodCount()`. When the count is zero it logs a warning, clears the registry, and returns `false`. The existing behavior for metadata containing usable methods is unchanged.

## How to verify

- Inspect `src/Utils/SteamMetadata/IPCLoader.cpp` for the zero-method guard before the success log.
- Build the Release configuration using `beginner-build.bat`; the change must compile without errors.
- For future dedicated loader tests: use a syntactically valid empty TOML, a TOML with an unrecognized interface, and a valid metadata fixture with usable methods. The first two must return failure; the valid one must succeed.

## Limits

The repository currently lacks a standalone automated test harness for this loader, so the source-level regression cases above are not yet executed in isolation. The successful Windows build verifies compilation only. No Steam runtime, authorization or license behavior was altered or tested. This change does not generate missing signatures, repair unrelated CloudRedirect metadata, or guarantee compatibility with a particular Steam client release.
