# Real IPC loader integration regression

Tested November 10, 2026 against untouched upstream commit `2a08b0b` and the corrected fork branch.

The test `tests/ipc_loader_integration_test.cpp` compiles and executes **production `src/Utils/SteamMetadata/IPCLoader.cpp`** together with the project's numeric and text conversion modules. Only network metadata retrieval (`RemoteToml::Fetch`) and the warning UI (`SteamDiagnostics::ShowWarning`) are replaced by local synthetic test doubles. No Steam files or authorization data are accessed.

| Synthetic input | Original upstream result | Corrected result | Expected |
|---|---|---|---|
| Empty TOML | Incorrect success | Rejected | Rejected |
| Unknown interface | Incorrect success | Rejected | Rejected |
| Known interface without methods | Incorrect success | Rejected | Rejected |
| Invalid method hash | Incorrect success | Rejected | Rejected |
| Malformed TOML | Rejected | Rejected | Rejected |
| Valid method | Accepted | Accepted | Accepted |
| Mixed known and unknown interfaces | Accepted | Accepted | Accepted |
| Metadata download unavailable | Rejected | Rejected | Rejected |

**Results:** original source passed **4/8**, corrected source passed **8/8**. Both binaries were built on the same Windows PC with the same MSVC toolchain and TOML++ dependency. This is a reproducible behavioral fix for the zero-method metadata defect.

## Reproduce

After compiling a Release build on Windows with Visual Studio C++ Build Tools, run from the fork root:

```bat
tests\run-ipc-loader-integration.bat
```

The test executable is built in a temporary directory and removed after testing. Success exit code: `0`.

## Scope

This is a real C++ loader test with injected offline metadata, not a Steam runtime test. It does **not** establish that missing Steam signatures, license validation, ownership tickets, or other users' overall compatibility problems are fixed. It isolates one specific defensive metadata-validation flaw; broader runtime reports require independent evidence.
