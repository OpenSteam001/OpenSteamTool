#include "ByteSearch.h"
#include "Log.h"
#include <cstdint>
#include <psapi.h>
#include <string>
#include <vector>

// Populated by dllmain.cpp / InitThread from steam.exe!GetBootstrapperVersion.
// Empty before init or when the export is unavailable; in that case we
// fall back to the legacy try-all-in-order behaviour.
extern std::string g_steamBuildId;

// ---- parse "48 8B ?? C4" → bytes + mask ----
static bool ParseSignature(const char* str, std::vector<uint8_t>& bytes, std::vector<uint8_t>& mask)
{
    bytes.clear();
    mask.clear();

    for (const char* p = str; *p; ) {
        // skip delimiters
        if (*p == ' ' || *p == '\t' || *p == ',') { ++p; continue; }

        if (p[0] == '?' && p[1] == '?') {
            bytes.push_back(0);
            mask.push_back(0);       // 0 = wildcard
            p += 2;
            continue;
        }

        // expect two hex digits
        char hi = p[0], lo = p[1];
        if (!hi || !lo) return false;

        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = nib(hi), l = nib(lo);
        if (h < 0 || l < 0) return false;

        bytes.push_back((uint8_t)((h << 4) | l));
        mask.push_back(1);           // 1 = must match
        p += 2;
    }
    return !bytes.empty();
}

// ---- internal: single-sig scan with parsed bytes ----
static void* ScanOne(HMODULE module, const std::vector<uint8_t>& bytes,
                     const std::vector<uint8_t>& mask, int matchIndex)
{
    MODULEINFO modInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &modInfo, sizeof(MODULEINFO)))
        return nullptr;

    auto* base = static_cast<uint8_t*>(modInfo.lpBaseOfDll);
    SIZE_T size = modInfo.SizeOfImage;
    SIZE_T patLen = bytes.size();

    if (size < patLen) return nullptr;

    int currentMatch = 0;
    for (SIZE_T i = 0; i <= size - patLen; ++i) {
        bool found = true;
        for (SIZE_T j = 0; j < patLen; ++j) {
            if (mask[j] && base[i + j] != bytes[j]) {
                found = false;
                break;
            }
        }
        if (found && ++currentMatch == matchIndex) {
            return base + i;
        }
    }
    return nullptr;
}

// ---- try a single Signature against the module ----
static void* TrySig(HMODULE module, const char* funcName, const Signature& sig)
{
    std::vector<uint8_t> bytes, mask;
    if (!ParseSignature(sig.signature, bytes, mask)) {
        LOG_WARN("ByteSearch: {} — bad signature '{}'", funcName ? funcName : "", sig.label);
        return nullptr;
    }
    return ScanOne(module, bytes, mask, sig.matchIndex);
}

// ---- core multi-sig dispatcher ----
// Patterns.h labels are now Steam build ids (e.g. "1778803745"). When
// g_steamBuildId is populated, we try the entry whose label matches the
// currently running Steam build FIRST. If that fails (or no exact match
// exists in the array), fall through to the legacy try-all-in-order so
// any close match still wins. Empty g_steamBuildId (pre-init or older
// Steam without the export) collapses to the pure legacy behaviour.
static void* ByteSearchImpl(HMODULE module, const char* funcName,
                            const Signature* sigs, size_t count)
{
    // 1. Preferred-label fast path.
    if (!g_steamBuildId.empty()) {
        for (size_t i = 0; i < count; ++i) {
            if (sigs[i].label && g_steamBuildId == sigs[i].label) {
                if (void* addr = TrySig(module, funcName, sigs[i])) {
                    if (funcName)
                        LOG_DEBUG("ByteSearch: {} matched build-id '{}'",
                                  funcName, sigs[i].label);
                    return addr;
                }
                if (funcName)
                    LOG_DEBUG("ByteSearch: {} build-id '{}' did NOT match, "
                              "falling back to try-all", funcName, sigs[i].label);
                break;  // at most one entry per build id; stop searching the array
            }
        }
    }

    // 2. Fallback: legacy try-all-in-declared-order.
    for (size_t i = 0; i < count; ++i) {
        // Skip the preferred entry we already tried (no point retrying it).
        if (!g_steamBuildId.empty() && sigs[i].label && g_steamBuildId == sigs[i].label)
            continue;
        if (void* addr = TrySig(module, funcName, sigs[i])) {
            if (funcName)
                LOG_DEBUG("ByteSearch: {} matched fallback '{}'", funcName, sigs[i].label);
            return addr;
        }
    }

    // 3. All failed.
    if (!funcName) return nullptr;

    std::string failedList;
    for (size_t i = 0; i < count; ++i) {
        if (!failedList.empty()) failedList += ", ";
        failedList += "'";
        failedList += sigs[i].label;
        failedList += "'";
    }
    LOG_WARN("ByteSearch FAILED: {} (build={}) — tried: {}",
             funcName, g_steamBuildId.empty() ? "unknown" : g_steamBuildId.c_str(),
             failedList);
    return nullptr;
}

// ---- multi-signature search (initializer_list) ----
void* ByteSearch(HMODULE module, const char* funcName, std::initializer_list<Signature> sigs)
{
    return ByteSearchImpl(module, funcName, sigs.begin(), sigs.size());
}

// ---- pointer + count overload ----
void* ByteSearch(HMODULE module, const char* funcName, const Signature* sigs, size_t count)
{
    return ByteSearchImpl(module, funcName, sigs, count);
}

// ---- memory patching ----
int PatchMemoryBytes(void* pAddress, const void* pNewBytes, SIZE_T nSize)
{
    if (!pAddress || !pNewBytes || nSize == 0) return 0;

    DWORD oldProtect = 0;
    if (!VirtualProtect(pAddress, nSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        return 0;

    memcpy(pAddress, pNewBytes, nSize);
    FlushInstructionCache(GetCurrentProcess(), pAddress, nSize);

    DWORD tmp = 0;
    VirtualProtect(pAddress, nSize, oldProtect, &tmp);
    return 1;
}
