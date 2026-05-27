# OpenSteamTool

![cpp](https://img.shields.io/badge/cpp-20%2B-green?logo=cplusplus)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green?logo=cmake)
![OnlyWindows](https://img.shields.io/badge/windows%20only-red?style=for-the-badge)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/OpenSteam001/OpenSteamTool)

OpenSteamTool is a Windows DLL project built with CMake.

## Feature

### Core Unlocks
- Unlock an unlimited number of unowned games.
- Unlock all DLCs for unowned games.
- Support auto load depot decryption keys from Lua config.
- Support auto manifest download via `steamrun` / `wudrm` upstream APIs(default is `wudrm`), or a custom Lua endpoint (see [Manifest via Lua](#manifest-via-lua)).
- Support downloading protected games or DLCs that require an access token.
- Support binding manifest to prevent specific games from being updated.

### Hot Reload
- Adding, modifying, deleting, or overwriting `.lua` files in any watched directory automatically triggers a reload. No restart, no offline/online toggle needed.

### Family Sharing and Remote Play
- Bypass Steam Family Sharing restrictions, allowing shared games to be played without limitations.

### Compatible with games protected by Denuvo and SteamStub
- For AppTicket and ETicket: in `HKEY_CURRENT_USER\Software\Valve\Steam\Apps\{AppId}`, both `AppTicket` and `ETicket` are `REG_BINARY` values.
- Use `setAppTicket(appid, "hex")` and `setETicket(appid, "hex")` in Lua config to write these values to the registry automatically.
- SteamID priority: read `SteamID` as `REG_SZ` (numeric-only) first; if missing, parse from `AppTicket`.

### Stats and Achievements
- Enable stats and achievements for unowned games.
- Uses `setStat(appid, "steamid")` to configure which SteamID's achievement data to pull.
- If no `setStat` is configured for an app, falls back to the hardcoded default SteamID `76561198028121353`.

### Online Fix
- Add `-onlinefix` to the Steam launch parameters to enable 480-based online play in games that use lobby matchmaking. The current limitation is that only one such game can run at a time.To revert, simply remove -onlinefix from the launch parameters — online play returns to normal on the next launch.

## Future
- For games protected by Denuvo and SteamStub, find a safe timing to switch `GetSteamID` (see `src/Hook/Hooks_IPC.cpp#Handler_IClientUser_GetSteamID` TODO) so save files are not affected.(**Suggestions welcome — when is the earliest point after game initialization that we can safely switch the
  SteamID without affecting save file binding?**)
- Steam Cloud synchronization support.(This is a huge project)
- Add Auto Denuvo Authorization Sharing for Legitimate Accounts.

## Usage
1. Run `build.bat` from the project root to build the project.
2. Copy generated `dwmapi.dll`, `xinput1_4.dll` and `OpenSteamTool.dll` to the Steam root directory.
3. Create Lua directory (for example `C:\steam\config\lua`) and place Lua scripts there. The DLL will automatically load and execute them.
4. Lua example:
```lua
addappid(1361510) -- unlock game with appid 1361510

addappid(1361511, 0,"5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0") -- unlock game with appid 1361511 depotKey is "5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0" 

addtoken(1361510,"2764735786934684318") -- add access token ("2764735786934684318") for game with appid 1361510 
-- No Longer Supported:
--pinApp(1361510) -- pin game with appid 1361510 to prevent it from being updated

setManifestid(1361511,"5656605350306673283") -- pin depotid:1361511 manifest_gid:5656605350306673283, size defaults to 0
setManifestid(1361511,"5656605350306673283", 12345678) -- same but with explicit size

setAppTicket(1361510,"0100000000000000...") -- write AppTicket (REG_BINARY) to HKCU\Software\Valve\Steam\Apps\1361510\AppTicket

setETicket(1361510,"0100000000000000...") -- write ETicket (REG_BINARY) to HKCU\Software\Valve\Steam\Apps\1361510\ETicket

setStat(1361510, "76561197960287930") -- use the specified SteamID's achievement data for appid 1361510
-- If not configured, default SteamID 76561198028121353 is used.
```

All function names are **case-insensitive**. `setAppTicket`, `setappticket`, `SetAppticket`, `SETAPPTICKET` etc. are all equivalent. The same applies to every registered function (`addAppId`, `AddToken`, `SETManifestid`, etc.).

### Configuration (optional)

Rename `opensteamtool.example.toml` to `opensteamtool.toml` and place it in the Steam root directory (next to `steam.exe`).
If no config file is found, built-in defaults are used — no auto-creation.

```toml
[log]
# Debug build only.  Level: trace, debug, info, warn, error
level = "info"

[manifest]
# Upstream API for depot manifest request codes.  Options: "steamrun", "wudrm"
url = "steamrun"

# HTTP timeouts for manifest requests (milliseconds)
timeout_resolve_ms = 5000
timeout_connect_ms = 5000
timeout_send_ms    = 10000
timeout_recv_ms    = 10000

# Additional Lua config directories (optional).
# Files are loaded after the default <Steam>/config/lua folder.
# The default folder is always loaded last so user files take priority.
[lua]
paths = []

# Optional signature-file mirror. See "Steam version compatibility" below.
# Leave commented out for the built-in default (raw.githubusercontent.com).
[pattern]
# mirror = "https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@pattern"
```

### Manifest via Lua

Two manifest code functions are supported:

#### `fetch_manifest_code(gid)`

Basic function that receives only the manifest GID.

#### `fetch_manifest_code_ex(app_id, depot_id, gid)` *(recommended)*

Extended function that receives `app_id`, `depot_id`, and `gid`. Allows constructing API endpoints that require app identification.

The C++ runtime provides two Lua helpers:

| Function | Signature | Returns |
|----------|-----------|---------|
| `http_get`  | `http_get(url [, headers])`       | `body, status_code` |
| `http_post` | `http_post(url, body [, headers])` | `body, status_code` |

`headers` is an optional table: `{["Key"]="Value", ...}`.

### Steam version compatibility

OpenSteamTool no longer ships byte-pattern signatures inside the DLL. Instead, on each launch it computes the SHA-256 of `steamclient64.dll` and `steamui.dll` on disk and looks up a matching pattern file from the upstream tracker at [`OpenSteam001/steam-monitor`](https://github.com/OpenSteam001/steam-monitor) (`pattern` branch).

Lookup order (every launch):

1. **GitHub raw** — `https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern/...`. Canonical source.
2. **jsDelivr CDN** — automatic fallback if GitHub raw is unreachable (connection refused / timeout / 5xx). No configuration required. Useful in regions where `raw.githubusercontent.com` is blocked but jsDelivr is reachable (e.g. mainland China).
3. **Local cache** — `<Steam>\opensteamtool\pattern\<subdir>\<sha256>.toml`. Used **only** when remote is unreachable. The cache is overwritten after every successful remote fetch.

Remote is consulted on every launch so users automatically pick up upstream re-publications (e.g. the bot adding a new signature, or fixing an existing one) without having to clear any cache.

If a step returns **HTTP 404** the mirror loop stops immediately — all mirrors serve the same content, so a 404 means the upstream bot has not yet published a TOML for this Steam build. The code then falls back to the local cache if one exists; otherwise a one-shot popup appears with the unmatched DLL name, its SHA-256, the expected cache path, and the upstream URL. Only the hooks tied to that DLL are disabled — the rest of OpenSteamTool keeps working.

You can also drop a pattern TOML into the cache directory manually if you know the layout for a given build; the file name must be `<sha256>.toml`. The cache fallback will pick it up the next time remote is unreachable.

> A short outbound HTTPS request is performed at every launch (one per DLL: `steamclient64.dll`, `steamui.dll`). The downloaded bodies are tiny (~10 KB each) and the work runs on a worker thread, so it never blocks Steam's loader.

#### Using a different mirror

For most users, the built-in **GitHub → jsDelivr** automatic fallback is enough; you do not need to touch `opensteamtool.toml` at all.

If you want to force a specific source (private mirror, intranet server, or a CDN that's faster on your network than the defaults), set it explicitly in `opensteamtool.toml`. **Setting `mirror` disables the automatic GitHub→jsDelivr fallback** — only the URL you specify is tried, on the principle that an explicit user choice should win.

```toml
[pattern]
# Default if unset:
#   https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern
# Examples:
mirror = "https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@pattern"
# mirror = "https://ghproxy.com/https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern"
# mirror = "https://your.server.com/opensteamtool-patterns"
```

The full URL fetched at runtime is `<mirror>/steamclient/<sha256>.toml` and `<mirror>/steamui/<sha256>.toml`. Any HTTPS server that serves the same directory layout works. A trailing `/` is allowed but optional.

Resolved URL by config (example, for the `steamui` lookup):

| Config | Resulting URL |
|---|---|
| `[pattern]` omitted, or `mirror = ""` | `https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern/steamui/<sha>.toml` |
| `mirror = "https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@pattern"` | `https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@pattern/steamui/<sha>.toml` |
| `mirror = "https://your.server.com/p/"` (trailing slash) | `https://your.server.com/p/steamui/<sha>.toml` (slash stripped at parse) |

**Verifying a mirror in your browser:** paste a complete URL — base + subdir + a real SHA-256 + `.toml`. The base URL alone (without the file path) will return `Invalid URL` from most CDNs, which is expected behavior, not a sign the mirror is broken. Example URLs you can paste directly:

```
https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@pattern/steamui/7a72275b5efc6781a964f6a8e5414ea2226c4a0a64a82e79b9e7d501dfcc3b57.toml
https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern/steamui/7a72275b5efc6781a964f6a8e5414ea2226c4a0a64a82e79b9e7d501dfcc3b57.toml
```

Replace the hash with a real one from [the upstream `pattern` branch](https://github.com/OpenSteam001/steam-monitor/tree/pattern/steamui). If the browser returns `200` you're good; `404` means upstream hasn't published a file for that DLL yet (open an issue), and connect/timeout errors mean the mirror itself isn't reachable from your network — pick another.

### Debug logging

Debug builds write per-module log files under `<Steam>/opensteamtool/`:

| File | Source | Content |
|------|--------|---------|
| `main.log`          | General | Init, config loading, Lua parsing,Utils |
| `ipc.log`           | `LOG_IPC_*` | IPC commands, InterfaceCall dispatch, spoofing |
| `netpacket.log`     | `LOG_NETPACKET_*` | Network packet send/recv, eMsg dispatch |
| `manifest.log`      | `LOG_MANIFEST_*` | Manifest download, `fetch_manifest_code`,manifest binding |
| `decryptionkey.log` | `LOG_DECRYPTIONKEY_*` | Depot decryption key injection |
| `keyvalue.log`      | `LOG_KEYVALUE_*` | KeyValues patching (manifest binding) |
| `misc.log`          | `LOG_MISC_*` | Engine pointer capture, AppId hints |
| `winhttp.log`       | `LOG_WINHTTP_*` | HTTP requests  |
| `achievement.log`   | `LOG_ACHIEVEMENT_*` | UserStats requests/responses, steamid spoofing |
| `pics.log`          | `LOG_PICS_*` | PICS access token injection |
| `package.log`       | `LOG_PACKAGE_*` | Package injection, FileWatcher events |
| `onlinefix.log`     | `LOG_ONLINEFIX_*` | Online fix (480 AppId spoofing) |

The log level is controlled by `[log] level` in `opensteamtool.toml`.

## Build

### Requirements
- Windows 10/11
- CMake 3.20+
- Visual Studio 2022 with MSVC (x64 toolchain)

### Runtime requirements
- Outbound HTTPS access to `raw.githubusercontent.com` on first launch after a Steam update (see [Steam version compatibility](#steam-version-compatibility)). Cached afterwards.

### Quick build
```powershell
build.bat
```

### Output
- Debug: `build/Debug/OpenSteamTool.dll`, `build/Debug/dwmapi.dll`, `build/Debug/xinput1_4.dll`
- Release: `build/Release/OpenSteamTool.dll`, `build/Release/dwmapi.dll`, `build/Release/xinput1_4.dll`

## Disclaimer
This project is provided for research and educational purposes only. You are responsible for complying with local laws, platform terms of service, and software licenses.
