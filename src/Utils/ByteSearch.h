#pragma once

// Memory patching helper and the FIND_SIG convenience macro.
//
// Signature scanning has been replaced by PatternLoader, which downloads
// per-DLL TOML files keyed on the SHA-256 of the DLL on disk.
// FIND_SIG now delegates directly to PatternLoader::FindPattern.
#include "PatternLoader.h"

// FIND_SIG(module, FuncName)
//   → PatternLoader::FindPattern(module, "FuncName")
// Drop-in replacement for the old ByteSearch / Patterns.h approach.
#define FIND_SIG(module, name) PatternLoader::FindPattern(module, #name)
