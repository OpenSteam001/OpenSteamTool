#include "IPCLoader.h"
#include "IPCMessages.gen.h"
#include "OSTPlatform/include/Numbers.h"
#include "Utils/Logging/Log.h"
#include "Utils/SteamMetadata/RemoteToml.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"

#include <unordered_map>
#include <utility>

#include <toml++/toml.hpp>

namespace IPCLoader {

namespace {

    struct Registry {
        std::vector<Interface> interfaces;
        std::unordered_map<EIPCInterface, size_t> byID;
        std::unordered_map<std::string, size_t> byName;

        void Clear()
        {
            interfaces.clear();
            byID.clear();
            byName.clear();
        }

        void Add(Interface iface)
        {
            const size_t index = interfaces.size();
            byID[iface.id] = index;
            byName[iface.name] = index;
            interfaces.push_back(std::move(iface));
        }

        const Method* Find(EIPCInterface interfaceID, uint32_t funcHash) const
        {
            const auto it = byID.find(interfaceID);
            if (it == byID.end()) return nullptr;

            for (const auto& method : interfaces[it->second].methods) {
                if (method.funcHash == funcHash) return &method;
            }
            return nullptr;
        }

        const Method* Find(std::string_view interfaceName,
                           std::string_view methodName) const
        {
            const auto it = byName.find(std::string(interfaceName));
            if (it == byName.end()) return nullptr;

            for (const auto& method : interfaces[it->second].methods) {
                if (method.name == methodName) return &method;
            }
            return nullptr;
        }

        size_t MethodCount() const
        {
            size_t count = 0;
            for (const auto& iface : interfaces)
                count += iface.methods.size();
            return count;
        }
    };

    Registry g_registry;

    // ---- TOML helpers ----

    static bool ParseHexU32(std::string_view s, uint32_t& out)
    {
        const auto parsed = OSTPlatform::Numbers::ParseHexUInt32(s);
        if (!parsed) return false;
        out = *parsed;
        return true;
    }

    static bool ParseInterfaceTable(std::string_view name,
                                    const toml::table& tbl,
                                    Interface& out)
    {
        out.name = std::string(name);

        const auto expected = EIPCInterfaceFromName(name);
        if (!expected) {
            LOG_WARN("IPCLoader: [{}] is not declared in EIPCInterface", name);
            return false;
        }
        out.id = *expected;

        // Backward-compatible metadata: EIPCInterface is authoritative.
        if (auto v = tbl["interface_id"].value<int64_t>()) {
            if (*v < 0 || *v > 0xFF) {
                LOG_WARN("IPCLoader: [{}] interface_id out of range ({})", name, *v);
                return false;
            }
            if (static_cast<EIPCInterface>(*v) != out.id) {
                LOG_WARN("IPCLoader: [{}] interface_id {} disagrees with generated EIPCInterface value {}",
                         name, *v, static_cast<uint8_t>(out.id));
                return false;
            }
        }

        if (auto v = tbl["vtable_rva"].value<std::string>())
            ParseHexU32(*v, out.vtableRva);

        // Walk dotted sub-tables — each is a method.
        for (auto& [methodKey, methodVal] : tbl) {
            if (!methodVal.is_table()) continue;
            const auto& mtbl = *methodVal.as_table();

            Method m;
            m.interfaceID = out.id;
            m.name = std::string(methodKey.str());

            if (auto v = mtbl["funcHash"].value<std::string>()) {
                if (!ParseHexU32(*v, m.funcHash)) {
                    LOG_WARN("IPCLoader: [{}.{}] bad funcHash '{}'",
                             name, m.name, *v);
                    continue;
                }
            } else {
                LOG_WARN("IPCLoader: [{}.{}] missing funcHash", name, m.name);
                continue;
            }

            if (auto v = mtbl["fencepost"].value<std::string>())
                ParseHexU32(*v, m.fencepost);
            if (auto v = mtbl["argc"].value<int64_t>())
                m.argc = static_cast<uint32_t>(*v);
            out.methods.push_back(std::move(m));
        }
        return true;
    }

    static void ShowMissingPopup(const std::string& sha256)
    {
        SteamDiagnostics::ShowWarning(
            "OpenSteamTool - IPC spec missing",
            "OpenSteamTool: IPC spec file not found.\n\n"
            "IPC interception is disabled for this session; pattern-based "
            "hooks are unaffected.\n\n"
            "You can:\n"
            "  1. Wait for the next upstream publish and restart Steam.\n"
            "  2. Drop a matching TOML at:\n"
            "       <Steam>\\opensteamtool\\ipc\\steamclient\\" + sha256 + ".toml\n"
            "  3. Check upstream:\n"
            "       https://github.com/madoiscool/steam-monitor/tree/ipc/steamclient");
    }

} // namespace

constexpr const char* kIPCChannel = "ipc";

bool Load(const std::string& steamclientPath)
{
    g_registry.Clear();

    RemoteToml::Result r = RemoteToml::Fetch({
        kIPCChannel,
        "steamclient",
        steamclientPath,
    }, [](const std::string& body) {
        try {
            const toml::table root = toml::parse(body);
            for (const auto& [key, val] : root) {
                if (!val.is_table()) continue;
                Interface iface;
                if (ParseInterfaceTable(key.str(), *val.as_table(), iface) &&
                    !iface.methods.empty()) return true;
            }
        } catch (const toml::parse_error&) {
        }
        return false;
    });

    if (!r.ok) {
        ShowMissingPopup(r.sha256.empty() ? "(hash failed)" : r.sha256);
        return false;
    }

    toml::table root;
    try {
        root = toml::parse(r.body);
    } catch (const toml::parse_error& e) {
        LOG_WARN("IPCLoader: TOML parse error: {}", e.description());
        ShowMissingPopup(r.sha256);
        return false;
    }

    for (auto& [key, val] : root) {
        if (!val.is_table()) continue;
        Interface iface;
        if (!ParseInterfaceTable(key.str(), *val.as_table(), iface)) continue;

        g_registry.Add(std::move(iface));
    }

    LOG_INFO("IPCLoader: loaded {} methods across {} interfaces ({})",
             MethodCount(), InterfaceCount(),
             r.fromCache ? "local cache" : "remote");
    return true;
}

const Method* Find(EIPCInterface interfaceID, uint32_t funcHash)
{
    return g_registry.Find(interfaceID, funcHash);
}

const Method* Find(std::string_view ifaceName, std::string_view methodName)
{
    return g_registry.Find(ifaceName, methodName);
}

size_t InterfaceCount() 
{ 
    return g_registry.interfaces.size();
}

size_t MethodCount() {
    return g_registry.MethodCount();
}

} // namespace IPCLoader
