#pragma once

#include "Utils/ByteSearch.h"

// Byte-pattern signatures for hooks against steamclient(64).dll and steamui.dll.
// Format: IDA-style hex, ?? = wildcard.  e.g. "48 8B C4 4C 89 48 20 89 50 10 48 89 48 08 55 ?? 48 8D"
//
// Multi-build support: each function can have a Signature array.
// To add a new build, append an entry.

/* -------------------------------------------------------------------------- */
/*                                   SteamUI                                  */
/* -------------------------------------------------------------------------- */
#define LoadModuleWithPathSig "48 89 5C 24 18 48 89 6C 24 20 56 41 54 41 57 48 83 EC 40"

/* -------------------------------------------------------------------------- */
/*                                 SteamClient                                */
/* -------------------------------------------------------------------------- */
#define LoadPackageSig             "48 89 5C 24 18 48 89 6C 24 20 56 57 41 54 41 55 41 57 48 81 EC 20 01"
#define CheckAppOwnershipSig       "48 8B C4 89 50 10 55 53 48 8D 68 D8"
#define CUtlMemoryGrowSig          "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30"
#define LoadDepotDecryptionKeySig  "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 30 48 63 FA 49 8B E9 8B D7 49 8B D8 48 8B F1"
#define GetManifestRequestCodeSig  "48 89 5C 24 18 55 56 57 41 55 41 57 48 8D 6C 24 A0"
#define ModifyStateFlagsSig        "48 89 5C 24 10 48 89 6C 24 18 48 89 7C 24 20 41 56 48 81 EC 90 04 00 00"
#define GetAppIDForCurrentPipeSig  "48 83 EC 08 8B 81 30 0D 00 00 4C 8B D9 44 8B 91 D8 00 00 00 83 F8 FF"
#define InitialRunningGameSig      "48 89 5C 24 18 4C 89 4C 24 20 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 20 FF"
#define GetConfigStringSig         "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 30 04 00 00 48 8B B4 24 60 04 00 00"
#define BuildDepotDependencySig    "48 8B C4 4C 89 48 20 89 50 10 48 89 48 08 55 ?? 48 8D"
#define AddAccessTokenSig          "89 48 20 48 8B 4B 18 89 50 10 48 89 48 18"
#define RecvMultiPktSig            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F2 48 8B F9 48 8B CE"
#define IPCProcessMessageSig       "48 89 5C 24 ?? 48 89 6C 24 ?? 56 41 54 41 55 41 56 41 57 48 83 EC ?? 49 8B D9"
#define GetPipeClientSig           "85 D2 74 ?? 44 0F B7 CA"
#define BBuildAndAsyncSendFrameSig "48 8B C4 55 48 8D 68 A1 48 81 EC C0 00 00 00"
#define PchMsgNameFromEMsgSig      "48 89 5C 24 08 57 48 83 EC 20 8B D9 E8"

/* -------------------------------------------------------------------------- */
/*                     KeyValues — multi-signature arrays                      */
/* -------------------------------------------------------------------------- */
#define KeyValues_ReadAsBinary_v1    "48 89 5C 24 08 44 88 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8B EC"
#define KeyValues_ReadAsBinary_v2    "48 8B C4 44 88 48 20 44 89 40 18 55 57 48 8D 68 A9 48 81 EC B8 00 00 00"

#define KeyValues_FindOrCreateKey_v1 "48 8B C4 56 57 41 54 41 55"
#define KeyValues_FindOrCreateKey_v2 "48 8B C4 4C 89 48 20 57 48 81 EC 60 04 00 00 48 89 70 E8 48 8B FA"

inline const Signature KeyValues_ReadAsBinarySigs[] = {
    {"3-10", KeyValues_ReadAsBinary_v1},
    {"4-29", KeyValues_ReadAsBinary_v2},
};

inline const Signature KeyValues_FindOrCreateKeySigs[] = {
    {"3-10", KeyValues_FindOrCreateKey_v1},
    {"4-29", KeyValues_FindOrCreateKey_v2},
};


