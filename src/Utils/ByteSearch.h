#pragma once

// Byte-pattern scanner over a loaded module's image.
// Signature format: IDA-style hex with ?? for wildcards, e.g.
//   "48 8B C4 4C 89 48 20 89 50 10 48 89 48 08 55 ?? 48 8D"

#include <windows.h>
#include <initializer_list>

struct Signature {
    const char* label;       // version label, e.g. "v1"
    const char* signature;  // "48 8B C4 ?? 56 57 41 54 41 55"
    int         matchIndex = 1;
};

// Multi-signature: tries each Signature in order. Logs all failures if nothing matches.
void* ByteSearch(HMODULE module, const char* funcName, std::initializer_list<Signature> sigs);

// Pointer + count overload for inline arrays.
void* ByteSearch(HMODULE module, const char* funcName, const Signature* sigs, size_t count);

// FIND_SIG(module, LoadModuleWithPath) → finds LoadModuleWithPathSigs, logs "LoadModuleWithPath"
#define FIND_SIG(module, name) ByteSearch(module, #name, name##Sigs, std::size(name##Sigs))

int PatchMemoryBytes(void* pAddress, const void* pNewBytes, SIZE_T nSize);
