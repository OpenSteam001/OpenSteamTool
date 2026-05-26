#ifndef DLLMAIN_H
#define DLLMAIN_H

#include <windows.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <array>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <regex>
#include <memory>
#include <atomic>
#include <format>

#include "Steam/Types.h"
#include "Steam/Enums.h"
#include "Steam/Structs.h"
#include "Steam/Callback.h"
#include "Utils/LuaConfig.h"
#include "Utils/Log.h"
#include "Utils/Config.h"


inline HMODULE diversion_hMdoule = nullptr;
inline std::atomic<bool> g_HooksInstalled{false};
inline char SteamInstallPath[MAX_PATH] = {};
inline char SteamclientPath[MAX_PATH] = {};
inline char SteamUIPath[MAX_PATH]      = {};
inline char DiversionPath[MAX_PATH]    = {};
inline char LuaDir[MAX_PATH]           = {};
inline char ConfigPath[MAX_PATH]       = {};

// The fake AppId used by -onlinefix (SpaceWar).
constexpr AppId_t kOnlineFixAppId = 480;

#endif // DLLMAIN_H
