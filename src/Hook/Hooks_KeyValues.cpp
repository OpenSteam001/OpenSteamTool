// Hook KeyValues::ReadAsBinary to override depot manifest gid/size in memory.
// After each app KV tree is deserialized (cold boot or runtime update), walk
// the depots subtree and patch matching depot nodes — no file writes.
//
// KV path:  root → "depots" → "<depotId>" → "manifests" → "<branch>" → "gid"/"size"

#include "Hooks_KeyValues.h"
#include "HookMacros.h"
#include "dllmain.h"

namespace {

    // ── forward declarations (used by ReadAsBinary hook body) ──────

    const char* GetKeyName(int symbol);
    KeyValues*  KV_FindKey(KeyValues* parent, const char* name);
    void        PatchDepotNode(KeyValues* depot, uint64_t depotId);

    // ── KeyValues::ReadAsBinary hook ───────────────────────────────

    HOOK_FUNC(ReadAsBinary, bool, KeyValues* root, void* buf, int depth,
              bool textMode, void* symTable) {
        bool ok = oReadAsBinary(root, buf, depth, textMode, symTable);
        if (!ok || !root) return ok;

        const auto& overrides = LuaConfig::GetManifestOverrides();
        if (overrides.empty()) return ok;

        KeyValues* depots = KV_FindKey(root, "depots");
        if (!depots) return ok;

        for (KeyValues* depot = depots->m_pSub; depot; depot = depot->m_pPeer) {
            const char* name = GetKeyName(depot->m_iKeyName);
            if (!name) continue;
            uint64_t depotId = strtoull(name, nullptr, 10);
            LOG_KEYVALUE_TRACE("Found depot node: name={} depotId={}", name, depotId);
            if (overrides.count(depotId))
                PatchDepotNode(depot, depotId);
        }
        return ok;
    }

    // ── FindOrCreateKey capture (not hooked, used by KV_FindKey) ────

    using FindOrCreateKey_t = KeyValues*(*)(KeyValues*, const char*, bool, KeyValues**);
    FindOrCreateKey_t oFindOrCreateKey = nullptr;

    // ── KeyValuesSystem — symbol ↔ string (from vstdlib_s64.dll) ───

    IKeyValuesSystem* GetKeyValuesSystem() {
        static IKeyValuesSystem* sys = []() -> IKeyValuesSystem* {
            HMODULE vstdlib = GetModuleHandleW(L"vstdlib_s64.dll");
            if (!vstdlib) return nullptr;
            auto pfn = (KeyValuesSystemSteam_t)GetProcAddress(vstdlib, "KeyValuesSystemSteam");
            return pfn ? pfn() : nullptr;
        }();
        return sys;
    }

    const char* GetKeyName(int symbol) {
        auto* sys = GetKeyValuesSystem();
        auto name = sys->GetStringForSymbol(symbol);
        LOG_KEYVALUE_TRACE("GetKeyName: symbol={} -> name={}", symbol, name);
        return name ? name : nullptr;
    }

    KeyValues* KV_FindKey(KeyValues* parent, const char* name) {
        return oFindOrCreateKey ? oFindOrCreateKey(parent, name, false, nullptr) : nullptr;
    }

    // ── depot node patching ────────────────────────────────────────
    // Walk manifests/<all branches>/  and rewrite gid + size to UINT64.
    // It will overwrite the appinfo.vdf 

    void PatchDepotNode(KeyValues* depot, uint64_t depotId) {
        const auto& overrides = LuaConfig::GetManifestOverrides();
        auto it = overrides.find(depotId);
        if (it == overrides.end()) return;

        KeyValues* manifests = KV_FindKey(depot, "manifests");
        if (!manifests) return;

        for (KeyValues* branch = manifests->m_pSub; branch; branch = branch->m_pPeer) {
            KeyValues* gidNode = KV_FindKey(branch, "gid");
            if (gidNode) {
                gidNode->m_iDataType       = KV_TYPE_UINT64;
                gidNode->m_bAllocatedValue = 0;
                gidNode->m_ullValue        = it->second.gid;
                gidNode->m_pChain          = nullptr;
            }
            KeyValues* sizeNode = KV_FindKey(branch, "size");
            if (sizeNode) {
                sizeNode->m_iDataType       = KV_TYPE_UINT64;
                sizeNode->m_bAllocatedValue = 0;
                sizeNode->m_ullValue        = it->second.size;
                sizeNode->m_pChain          = nullptr;
            }
            LOG_KEYVALUE_INFO("Patched depot {} manifest branch '{}': gid={} size={}", depotId, GetKeyName(branch->m_iKeyName),
                     gidNode ? std::to_string(gidNode->m_ullValue) : "N/A",
                     sizeNode ? std::to_string(sizeNode->m_ullValue) : "N/A");
            LOG_MANIFEST_INFO("Patched depot {} manifest branch '{}': gid={} size={}", depotId, GetKeyName(branch->m_iKeyName),
                     gidNode ? std::to_string(gidNode->m_ullValue) : "N/A",
                     sizeNode ? std::to_string(sizeNode->m_ullValue) : "N/A");
        }
    }

} // anonymous namespace

namespace Hooks_KeyValues {

    void Install() {
        RESOLVE_EX_D(FindOrCreateKey, KeyValues_FindOrCreateKeySigs);
        if (!oFindOrCreateKey) return;

        HOOK_BEGIN();
        INSTALL_HOOK_EX_D(ReadAsBinary, KeyValues_ReadAsBinarySigs);
        HOOK_END();
    }

    void Uninstall() {
        if (!oReadAsBinary) return;
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(ReadAsBinary);
        UNHOOK_END();
        oFindOrCreateKey = nullptr;
    }

} // namespace Hooks_KeyValues
