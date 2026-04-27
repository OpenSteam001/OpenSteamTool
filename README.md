# OpenSteamTool

OpenSteamTool is a Windows DLL project built with CMake.

## Feature

### Core Unlocks
- Unlock an unlimited number of unowned games.
- Unlock all DLCs for unowned games.
- Support auto load depot decryption keys from Lua config, no need to manually input them in `config.vdf` anymore.
- Support auto manifest download thanks to RoGoing's manifest API (https://manifest.steam.run/).
- Supports downloading protected games or DLCs that require an access token.
  
### Family Sharing and Remote Play
- Bypass Steam Family Sharing restrictions, allowing shared games to be played without limitations.

## Future
- Enable stats and achievements for unowned games.
- Compatible with games protected by Denuvo and SteamStub.
- Steam Cloud synchronization support.

## Usage
1. Run `build.bat` from the project root to build the project.
2. Copy generated `dwmapi.dll` and `OpenSteamTool.dll` to the Steam root directory.
3. Create Lua directory (for example `C:\steam\config\lua`) and place Lua scripts there. The DLL will automatically load and execute them.
4. Lua example:
```lua
addappid(1361510) -- unlock game with appid 1361510

addappid(1361511, 0,"5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0") -- unlock game with appid 1361511 depotKey is "5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0" 

addtoken(1361510,"2764735786934684318") -- add access token ("2764735786934684318") for game with appid 1361510 
``` 

## Build

### Requirements
- Windows 10/11
- CMake 3.20+
- Visual Studio 2022 with MSVC (x64 toolchain)

### Quick build
```powershell
build.bat
```

### Output
- Debug: `build/Debug/OpenSteamTool.dll` and `build/Debug/dwmapi.dll`
- Release: `build/Release/OpenSteamTool.dll` and `build/Release/dwmapi.dll`

## Disclaimer
This project is provided for research and educational purposes only. You are responsible for complying with local laws, platform terms of service, and software licenses.
