// ─────────────────────────────────────────────────────────────────
//  Module logger registry.
//  This file is included multiple times with different definitions
//  of OST_MOD(varName, fileName).
//
//  Usage:
//    #define OST_MOD(v, f) ...   // declare logger
//    #include "ost_log_modules.h"
//    #undef OST_MOD
//
//  Adding a new module:
//    1. Add  OST_MOD(NewMod, "newmod")  below.
//    2. Add  LOG_NEWMOD_* macros in Log.h (one 5-line block).
// ─────────────────────────────────────────────────────────────────

OST_MOD(IPC,           "ipc")
OST_MOD(NetPacket,     "netpacket")
OST_MOD(Manifest,      "manifest")
OST_MOD(KeyValue,      "keyvalue")
OST_MOD(DecryptionKey, "decryptionkey")
OST_MOD(Misc,          "misc")
OST_MOD(WinHttp,       "winhttp")
OST_MOD(Achievement,   "achievement")
OST_MOD(Pics,          "pics")
