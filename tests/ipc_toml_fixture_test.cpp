// Test synthetic TOML parsing and the production zero-method acceptance rule.
// This is not an end-to-end test of IPCLoader::Load or Steam.
#include "Utils/SteamMetadata/IPCMetadataValidation.h"
#include <toml++/toml.hpp>
#include <cassert>
#include <string_view>
#include <vector>

struct SyntheticInterface { std::vector<int> methods; };

// Fixture adapter deliberately recognizes only one invented interface and
// syntactically valid hexadecimal funcHash values; the production loader has
// additional checks, which require their own integration tests.
static bool Accepted(std::string_view text) {
    toml::table root;
    try { root = toml::parse(text); }
    catch (const toml::parse_error&) { return false; }
    std::vector<SyntheticInterface> interfaces;
    for (auto&& [name, entry] : root) {
        if (name.str() != "SyntheticExample" || !entry.is_table()) continue;
        SyntheticInterface iface;
        for (auto&& [methodName, value] : *entry.as_table()) {
            if (!value.is_table()) continue;
            auto hash = value.as_table()->get("funcHash");
            if (!hash) continue;
            auto hex = hash->value<std::string>();
            if (!hex || hex->size() < 3 || hex->substr(0, 2) != "0x") continue;
            try {
                size_t parsed = 0;
                (void)std::stoul(*hex, &parsed, 16);
                if (parsed == hex->size()) iface.methods.push_back(1);
            } catch (...) {}
        }
        interfaces.push_back(std::move(iface));
    }
    return IPCMetadataValidation::HasUsableMethods(interfaces);
}

int main() {
    assert(!Accepted(""));
    assert(!Accepted("[Unknown]\n[Unknown.Method]\nfuncHash = '0x12'\n"));
    assert(!Accepted("[SyntheticExample]\nlabel = 'empty'\n"));
    assert(!Accepted("[SyntheticExample.Method]\nfuncHash = 'invalid'\n"));
    assert(!Accepted("[SyntheticExample\n"));
    assert(Accepted("[SyntheticExample.Method]\nfuncHash = '0x12'\n"));
    return 0;
}
