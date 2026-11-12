#include "Hooks_Decryption.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/Tickets/AppTicket.h"
#include <string>

namespace {

    void* g_pConfigStoreLocal = nullptr;

    HOOK_FUNC(ConfigStoreGetBinary, int32, void* pObject, EConfigStore eConfigStore, const char* KeyName, char* Key, uint32 KeySize) {
        if (eConfigStore == k_EConfigStoreUserLocal && pObject && !g_pConfigStoreLocal) {
            g_pConfigStoreLocal = pObject;
            LOG_DECRYPTIONKEY_DEBUG("ConfigStoreGetBinary: captured local ConfigStore at {}", g_pConfigStoreLocal);

        }
        std::string name(KeyName);
        LOG_DECRYPTIONKEY_DEBUG("ConfigStore::GetBinary called for pObject={}, eConfigStore={}, KeyName='{}'", 
                                    pObject, static_cast<uint32>(eConfigStore), name);
        // Expected shape: ".../<DepotId>\DecryptionKey"
        if (size_t last = name.find("\\DecryptionKey"); last != std::string::npos) {
            if (size_t start = name.find_last_of("\\", last - 1); start != std::string::npos) {
                AppId_t depotId = std::stoul(name.substr(start + 1, last - start - 1));
                if (const auto& key = LuaConfig::GetDecryptionKey(depotId); !key.empty()) {
                    if (KeySize >= key.size()) {
                        LOG_DECRYPTIONKEY_INFO("Providing decryption key for depot {}: {}", depotId,
                                               spdlog::to_hex(key.data(), key.data() + key.size()));
                        memcpy(Key, key.data(), key.size());
                        return static_cast<int32>(key.size());
                    }
                    LOG_DECRYPTIONKEY_WARN("Decryption key for depot {} is too large ({} bytes) for buffer ({} bytes)",
                                            depotId, key.size(), KeySize);
                }
            }
        }

        // Expected shape: "apptickets\<AppId>"
        // steamclient serves the game's ownership ticket from its local cache and
        // only falls back to a CM round-trip when the cache misses. Games that go
        // through this path (no IPC AppOwnershipTicket call at all) never see the
        // forged response installed by Hooks_IPC_ISteamUser, so forge here too.
        if (size_t pos = name.rfind("apptickets\\"); pos != std::string::npos) {
            const std::string tail = name.substr(pos + sizeof("apptickets\\") - 1);
            if (!tail.empty() && tail.find('\\') == std::string::npos) {
                // A real ticket always wins: only forge when the cache misses.
                const int32 cached = oConfigStoreGetBinary(pObject, eConfigStore, KeyName, Key, KeySize);
                if (cached > 0) return cached;

                AppId_t appId = 0;
                try { appId = static_cast<AppId_t>(std::stoul(tail)); } catch (...) { appId = 0; }

                // HasDepot's default excludes apps already marked owned — pass false
                // so forge still applies after MarkOwned has run for this app.
                if (appId != 0 && LuaConfig::HasDepot(appId, /*excludeOwned=*/false)) {
                    AppTicket::AppOwnershipTicket ticket{};
                    if (AppTicket::GetAppOwnershipTicket(appId, ticket, AppTicket::AppTicketSource::ForgeOnly)) {
                        if (ticket.data.size() <= static_cast<size_t>(KeySize)) {
                            memcpy(Key, ticket.data.data(), ticket.data.size());
                            LOG_DECRYPTIONKEY_INFO("Forged appticket for AppId {}: {} bytes (declared {})",
                                                   appId, ticket.data.size(), ticket.totalSize);
                            return static_cast<int32>(ticket.data.size());
                        }
                        LOG_DECRYPTIONKEY_WARN("Forged appticket for AppId {} is too large ({} bytes) for buffer ({} bytes)",
                                               appId, ticket.data.size(), KeySize);
                    }
                }
                return cached;
            }
        }
        return oConfigStoreGetBinary(pObject, eConfigStore, KeyName, Key, KeySize);
    }

    std::vector<uint8_t> ReadConfigStoreLocalBinary(const std::string& keyName) {
        if (!g_pConfigStoreLocal || !oConfigStoreGetBinary) {
            LOG_DECRYPTIONKEY_WARN("GetConfigStoreLocalBinary: ConfigStoreGetBinary not ready, cannot get binary value");
            return {};
        }

        std::vector<uint8_t> value(1024);
        int32 result = oConfigStoreGetBinary(g_pConfigStoreLocal, k_EConfigStoreUserLocal,
                                             keyName.c_str(),
                                             reinterpret_cast<char*>(value.data()),
                                             static_cast<uint32>(value.size()));
        if (result <= 0) {
            LOG_DECRYPTIONKEY_DEBUG("GetConfigStoreLocalBinary: failed to read KeyName='{}'", keyName);
            return {};
        }

        value.resize(result);
        LOG_DECRYPTIONKEY_DEBUG("GetConfigStoreLocalBinary: got value for KeyName='{}' ({} bytes)",
                                keyName, value.size());
        return value;
    }
}

namespace Hooks_Decryption {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_C(ConfigStoreGetBinary);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(ConfigStoreGetBinary);
        UNHOOK_END();
    }

    std::vector<uint8_t> GetCacheAppOwnershipTicket(AppId_t appId) {
        std::vector<uint8_t> ticket = ReadConfigStoreLocalBinary(std::format("apptickets\\{}", appId));
        if (ticket.empty()) {
            LOG_DECRYPTIONKEY_DEBUG("no cached ticket for AppId {}", appId);
            return ticket;
        }
        LOG_DECRYPTIONKEY_DEBUG("got cached ticket for AppId {} ({} bytes)", appId, ticket.size());
        return ticket;
    }
}
