# IPC metadata validation: reject empty method registries

## Problem

The `IPCLoader::Load` function returned success after parsing any syntactically valid TOML, even when every interface was unrecognized or no usable IPC methods were present. This could make an invalid compatibility metadata file appear successfully loaded in diagnostics.

## Change

After parsing the metadata, `Load` now checks `MethodCount()`. When the count is zero it logs a warning, clears the registry, and returns `false`. The existing behavior for metadata containing usable methods is unchanged.

## How to verify

- Inspect `src/Utils/SteamMetadata/IPCLoader.cpp` for the zero-method guard before the success log.
- Build the Release configuration using `beginner-build.bat`; the change must compile without errors.
- Run `tests/ipc_metadata_policy_test.cpp` with MSVC and verify empty synthetic registries fail while nonempty registries pass. Full TOML loader fixtures are future work.

## Limits

A standalone C++ policy test exists at `tests/ipc_metadata_policy_test.cpp` and covers empty and nonempty synthetic registries. This verifies the production guard policy, not the full TOML parsing or live Steam initialization path. The successful Windows build verifies compilation only. No Steam runtime, authorization or license behavior was altered or tested. This change does not generate missing signatures, repair unrelated CloudRedirect metadata, or guarantee compatibility with a particular Steam client release.

## Synthetic TOML fixture coverage

The file 	ests/ipc_toml_fixture_test.cpp contains six C++ cases that parse invented TOML with the same TOML++ library as the project, then apply the shared production zero-method validation rule. Cases include empty input, unrecognized interface, empty interface, bad method hash, malformed TOML, and a valid method. The test uses a deliberately simplified fixture adapter and does **not** execute the full production IPCLoader::Load path. Do not present it as an end-to-end Steam or loader compatibility test.
