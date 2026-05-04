#include "AppTicket.h"

#include <cstdlib>

namespace AppTicket {

    static uint64_t GetSteamIDFromRegistryString(AppId_t appId) {
        HKEY hKey;
        const std::string regPath = "Software\\Valve\\Steam\\Apps\\" + std::to_string(appId);
        if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return 0;
        }

        DWORD valueType = 0;
        DWORD valueSize = 0;
        if (RegQueryValueExA(hKey, "SteamID", nullptr, &valueType, nullptr, &valueSize) != ERROR_SUCCESS
            || valueType != REG_SZ || valueSize == 0) {
            RegCloseKey(hKey);
            return 0;
        }

        std::vector<char> value(valueSize);
        if (RegQueryValueExA(hKey, "SteamID", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(value.data()), &valueSize) != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return 0;
        }
        RegCloseKey(hKey);

        if (value.back() != '\0') {
            value.push_back('\0');
        }

        const std::string steamIdStr(value.data());
        if (steamIdStr.empty()) {
            return 0;
        }
        for (char c : steamIdStr) {
            if (c < '0' || c > '9') {
                return 0;
            }
        }

        const uint64_t steamID = std::strtoull(steamIdStr.c_str(), nullptr, 10);
        if (steamID != 0) {
            LOG_DEBUG("GetSpoofSteamID for AppId {}: SteamID REG_SZ -> 0x{:X}({})", appId, steamID, steamID);
        }
        return steamID;
    }

    std::vector<uint8_t> GetAppOwnershipTicketFromRegistry(AppId_t appId) {
        LOG_TRACE("AppId={}", appId);
        // exclude those appids that are not in addappid
        if (!LuaConfig::HasDepot(appId)) {
            LOG_DEBUG("GetAppOwnershipTicketFromRegistry for AppId {}: not in addappid, skip", appId);
            return {};
        }
        std::vector<uint8_t> empty{};
        HKEY hKey;
        const std::string regPath = "Software\\Valve\\Steam\\Apps\\" + std::to_string(appId);
        if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return empty;
        }

        std::vector<uint8_t> value(1024);
        DWORD valueSize = static_cast<DWORD>(value.size());
        DWORD valueType = 0;
        if (RegQueryValueExA(hKey, "AppTicket", nullptr, &valueType,value.data(), &valueSize) != ERROR_SUCCESS
            || valueType != REG_BINARY) {
            RegCloseKey(hKey);
            return empty;
        }
        RegCloseKey(hKey);

        value.resize(valueSize);
        LOG_INFO("Successfully retrieved App Ownership Ticket from Registry, AppId: {}, Ticket Size: {}", appId, valueSize);
        return value;
    }

    std::vector<uint8_t> GetEncryptedTicketFromRegistry(AppId_t appId) {
        LOG_DEBUG("appid={}", appId);    
        // exclude those appids that are not in addappid
        if (!LuaConfig::HasDepot(appId)) {
            LOG_DEBUG("GetAppOwnershipTicketFromRegistry for AppId {}: not in addappid, skip", appId);
            return {};
        }
        std::vector<uint8_t> empty{};
        HKEY hKey;
        const std::string regPath = "Software\\Valve\\Steam\\Apps\\" + std::to_string(appId);
        if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return empty;
        }

        std::vector<uint8_t> value(1024);
        DWORD valueSize = static_cast<DWORD>(value.size());
        DWORD valueType = 0;
        if (RegQueryValueExA(hKey, "ETicket", nullptr, &valueType,value.data(), &valueSize) != ERROR_SUCCESS
            || valueType != REG_BINARY) {
            RegCloseKey(hKey);
            return empty;
        }
        RegCloseKey(hKey);

        value.resize(valueSize);
        LOG_INFO("Successfully retrieved Encrypted App Ticket from Registry, AppId: {}, Ticket Size: {}", appId, valueSize);
        return value;
    }

    bool WriteAppOwnershipTicket(AppId_t appId, const std::vector<uint8_t>& data) {
        // we can't execlude appids here 
        HKEY hKey;
        const std::string regPath = "Software\\Valve\\Steam\\Apps\\" + std::to_string(appId);
        DWORD disposition;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, regPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, &disposition) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to create/open registry key: {}", regPath);
            return false;
        }
        LSTATUS result = RegSetValueExA(hKey, "AppTicket", 0, REG_BINARY, data.data(), static_cast<DWORD>(data.size()));
        RegCloseKey(hKey);
        if (result != ERROR_SUCCESS) {
            LOG_ERROR("Failed to write AppTicket for AppId {}: {}", appId, result);
            return false;
        }
        LOG_INFO("Wrote AppTicket for AppId {} ({} bytes)", appId, data.size());
        return true;
    }

    bool WriteEncryptedTicket(AppId_t appId, const std::vector<uint8_t>& data) {
        // we can't execlude appids here 
        HKEY hKey;
        const std::string regPath = "Software\\Valve\\Steam\\Apps\\" + std::to_string(appId);
        DWORD disposition;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, regPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, &disposition) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to create/open registry key: {}", regPath);
            return false;
        }
        LSTATUS result = RegSetValueExA(hKey, "ETicket", 0, REG_BINARY, data.data(), static_cast<DWORD>(data.size()));
        RegCloseKey(hKey);
        if (result != ERROR_SUCCESS) {
            LOG_ERROR("Failed to write ETicket for AppId {}: {}", appId, result);
            return false;
        }
        LOG_INFO("Wrote ETicket for AppId {} ({} bytes)", appId, data.size());
        return true;
    }

    uint64_t GetSpoofSteamID(AppId_t appId) {
        // exclude those appids that are not in addappid
        if (!LuaConfig::HasDepot(appId)) {
            LOG_DEBUG("GetSpoofSteamID for AppId {}: not in addappid, skip spoofing", appId);
            return 0;
        }
        const uint64_t registrySteamID = GetSteamIDFromRegistryString(appId);
        if (registrySteamID != 0) {
            return registrySteamID;
        }

        // The SteamID baked into the cached AppOwnershipTicket is the same
        // one Steam itself uses for this app — pull it straight out of the
        // ticket so spoofed responses match what the DRM layer expects.
        // Layout: ticket bytes start with [uint32 Size][uint32 Version][uint64 SteamID][...].
        std::vector<uint8_t> ticket = GetAppOwnershipTicketFromRegistry(appId);
        if (ticket.size() >= 16) {
            const uint64_t steamID = reinterpret_cast<const uint64_t*>(ticket.data())[1];
            LOG_DEBUG("GetSpoofSteamID for AppId {}: -> 0x{:X}({})", appId, steamID, steamID);
            return steamID;
        }
        return 0;
    }
}
