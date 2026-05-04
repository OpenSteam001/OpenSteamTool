#pragma once

// Multi-file logger backed by spdlog (Debug only; Release → no-ops).
//
// Log::Init()        — creates main.log at trace level (before Config).
// Log::InitModules()  — creates per-module loggers + applies Config level
//                       to all loggers. Call after Config::Load().
//
// General macros  →  <steam>/opensteamtool/main.log
// Module macros   →  <steam>/opensteamtool/<module>.log
//
// Adding a new module logger:
//   1. Add  OST_MOD(NewMod, "newmod")  in ost_log_modules.h.
//   2. Copy the 5-line macro block below, rename prefix to LOG_NEWMOD.
//   3. Copy the 5-line ((void)0) block in the Release section.

#ifdef OPENSTEAMTOOL_LOGGING_ENABLED

#ifndef SPDLOG_ACTIVE_LEVEL
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <windows.h>
#include <memory>

namespace Log {
    void Init(HMODULE selfModule);
    void InitModules();

    inline std::shared_ptr<spdlog::logger> Main;

    // Module loggers — auto-generated from ost_log_modules.h
    #define OST_MOD(v, f) inline std::shared_ptr<spdlog::logger> v;
    #include "ost_log_modules.h"
    #undef OST_MOD
}

// ── General-purpose (main.log) ──────────────────────────────────────
#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(Log::Main, __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(Log::Main, __VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_LOGGER_INFO(Log::Main, __VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_LOGGER_WARN(Log::Main, __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(Log::Main, __VA_ARGS__)

// ── Per-module macros ───────────────────────────────────────────────
//   Template: copy this 5-line block for each new module
//   #define LOG_XXX_TRACE(...) SPDLOG_LOGGER_TRACE(Log::XXX, __VA_ARGS__)
//   #define LOG_XXX_DEBUG(...) SPDLOG_LOGGER_DEBUG(Log::XXX, __VA_ARGS__)
//   #define LOG_XXX_INFO(...)  SPDLOG_LOGGER_INFO(Log::XXX, __VA_ARGS__)
//   #define LOG_XXX_WARN(...)  SPDLOG_LOGGER_WARN(Log::XXX, __VA_ARGS__)
//   #define LOG_XXX_ERROR(...) SPDLOG_LOGGER_ERROR(Log::XXX, __VA_ARGS__)

#define LOG_IPC_TRACE(...)        SPDLOG_LOGGER_TRACE(Log::IPC, __VA_ARGS__)
#define LOG_IPC_DEBUG(...)        SPDLOG_LOGGER_DEBUG(Log::IPC, __VA_ARGS__)
#define LOG_IPC_INFO(...)         SPDLOG_LOGGER_INFO(Log::IPC, __VA_ARGS__)
#define LOG_IPC_WARN(...)         SPDLOG_LOGGER_WARN(Log::IPC, __VA_ARGS__)
#define LOG_IPC_ERROR(...)        SPDLOG_LOGGER_ERROR(Log::IPC, __VA_ARGS__)

#define LOG_NETPACKET_TRACE(...)  SPDLOG_LOGGER_TRACE(Log::NetPacket, __VA_ARGS__)
#define LOG_NETPACKET_DEBUG(...)  SPDLOG_LOGGER_DEBUG(Log::NetPacket, __VA_ARGS__)
#define LOG_NETPACKET_INFO(...)   SPDLOG_LOGGER_INFO(Log::NetPacket, __VA_ARGS__)
#define LOG_NETPACKET_WARN(...)   SPDLOG_LOGGER_WARN(Log::NetPacket, __VA_ARGS__)
#define LOG_NETPACKET_ERROR(...)  SPDLOG_LOGGER_ERROR(Log::NetPacket, __VA_ARGS__)

#define LOG_MANIFEST_TRACE(...)   SPDLOG_LOGGER_TRACE(Log::Manifest, __VA_ARGS__)
#define LOG_MANIFEST_DEBUG(...)   SPDLOG_LOGGER_DEBUG(Log::Manifest, __VA_ARGS__)
#define LOG_MANIFEST_INFO(...)    SPDLOG_LOGGER_INFO(Log::Manifest, __VA_ARGS__)
#define LOG_MANIFEST_WARN(...)    SPDLOG_LOGGER_WARN(Log::Manifest, __VA_ARGS__)
#define LOG_MANIFEST_ERROR(...)   SPDLOG_LOGGER_ERROR(Log::Manifest, __VA_ARGS__)

#define LOG_KEYVALUE_TRACE(...)   SPDLOG_LOGGER_TRACE(Log::KeyValue, __VA_ARGS__)
#define LOG_KEYVALUE_DEBUG(...)   SPDLOG_LOGGER_DEBUG(Log::KeyValue, __VA_ARGS__)
#define LOG_KEYVALUE_INFO(...)    SPDLOG_LOGGER_INFO(Log::KeyValue, __VA_ARGS__)
#define LOG_KEYVALUE_WARN(...)    SPDLOG_LOGGER_WARN(Log::KeyValue, __VA_ARGS__)
#define LOG_KEYVALUE_ERROR(...)   SPDLOG_LOGGER_ERROR(Log::KeyValue, __VA_ARGS__)

#define LOG_DECRYPTIONKEY_TRACE(...)  SPDLOG_LOGGER_TRACE(Log::DecryptionKey, __VA_ARGS__)
#define LOG_DECRYPTIONKEY_DEBUG(...)  SPDLOG_LOGGER_DEBUG(Log::DecryptionKey, __VA_ARGS__)
#define LOG_DECRYPTIONKEY_INFO(...)   SPDLOG_LOGGER_INFO(Log::DecryptionKey, __VA_ARGS__)
#define LOG_DECRYPTIONKEY_WARN(...)   SPDLOG_LOGGER_WARN(Log::DecryptionKey, __VA_ARGS__)
#define LOG_DECRYPTIONKEY_ERROR(...)  SPDLOG_LOGGER_ERROR(Log::DecryptionKey, __VA_ARGS__)

#define LOG_MISC_TRACE(...)       SPDLOG_LOGGER_TRACE(Log::Misc, __VA_ARGS__)
#define LOG_MISC_DEBUG(...)       SPDLOG_LOGGER_DEBUG(Log::Misc, __VA_ARGS__)
#define LOG_MISC_INFO(...)        SPDLOG_LOGGER_INFO(Log::Misc, __VA_ARGS__)
#define LOG_MISC_WARN(...)        SPDLOG_LOGGER_WARN(Log::Misc, __VA_ARGS__)
#define LOG_MISC_ERROR(...)       SPDLOG_LOGGER_ERROR(Log::Misc, __VA_ARGS__)

#define LOG_WINHTTP_TRACE(...)    SPDLOG_LOGGER_TRACE(Log::WinHttp, __VA_ARGS__)
#define LOG_WINHTTP_DEBUG(...)    SPDLOG_LOGGER_DEBUG(Log::WinHttp, __VA_ARGS__)
#define LOG_WINHTTP_INFO(...)     SPDLOG_LOGGER_INFO(Log::WinHttp, __VA_ARGS__)
#define LOG_WINHTTP_WARN(...)     SPDLOG_LOGGER_WARN(Log::WinHttp, __VA_ARGS__)
#define LOG_WINHTTP_ERROR(...)    SPDLOG_LOGGER_ERROR(Log::WinHttp, __VA_ARGS__)

#define LOG_ACHIEVEMENT_TRACE(...)  SPDLOG_LOGGER_TRACE(Log::Achievement, __VA_ARGS__)
#define LOG_ACHIEVEMENT_DEBUG(...)  SPDLOG_LOGGER_DEBUG(Log::Achievement, __VA_ARGS__)
#define LOG_ACHIEVEMENT_INFO(...)   SPDLOG_LOGGER_INFO(Log::Achievement, __VA_ARGS__)
#define LOG_ACHIEVEMENT_WARN(...)   SPDLOG_LOGGER_WARN(Log::Achievement, __VA_ARGS__)
#define LOG_ACHIEVEMENT_ERROR(...)  SPDLOG_LOGGER_ERROR(Log::Achievement, __VA_ARGS__)

#define LOG_PICS_TRACE(...)  SPDLOG_LOGGER_TRACE(Log::Pics, __VA_ARGS__)
#define LOG_PICS_DEBUG(...)  SPDLOG_LOGGER_DEBUG(Log::Pics, __VA_ARGS__)
#define LOG_PICS_INFO(...)   SPDLOG_LOGGER_INFO(Log::Pics, __VA_ARGS__)
#define LOG_PICS_WARN(...)   SPDLOG_LOGGER_WARN(Log::Pics, __VA_ARGS__)
#define LOG_PICS_ERROR(...)  SPDLOG_LOGGER_ERROR(Log::Pics, __VA_ARGS__)

#else  // OPENSTEAMTOOL_LOGGING_ENABLED

#include <windows.h>

namespace Log {
    inline void Init(HMODULE) {}
    inline void InitModules() {}
}

#define LOG_TRACE(...)           ((void)0)
#define LOG_DEBUG(...)           ((void)0)
#define LOG_INFO(...)            ((void)0)
#define LOG_WARN(...)            ((void)0)
#define LOG_ERROR(...)           ((void)0)

#define LOG_IPC_TRACE(...)       ((void)0)
#define LOG_IPC_DEBUG(...)       ((void)0)
#define LOG_IPC_INFO(...)        ((void)0)
#define LOG_IPC_WARN(...)        ((void)0)
#define LOG_IPC_ERROR(...)       ((void)0)

#define LOG_NETPACKET_TRACE(...) ((void)0)
#define LOG_NETPACKET_DEBUG(...) ((void)0)
#define LOG_NETPACKET_INFO(...)  ((void)0)
#define LOG_NETPACKET_WARN(...)  ((void)0)
#define LOG_NETPACKET_ERROR(...) ((void)0)

#define LOG_MANIFEST_TRACE(...)  ((void)0)
#define LOG_MANIFEST_DEBUG(...)  ((void)0)
#define LOG_MANIFEST_INFO(...)   ((void)0)
#define LOG_MANIFEST_WARN(...)   ((void)0)
#define LOG_MANIFEST_ERROR(...)  ((void)0)

#define LOG_KEYVALUE_TRACE(...)  ((void)0)
#define LOG_KEYVALUE_DEBUG(...)  ((void)0)
#define LOG_KEYVALUE_INFO(...)   ((void)0)
#define LOG_KEYVALUE_WARN(...)   ((void)0)
#define LOG_KEYVALUE_ERROR(...)  ((void)0)

#define LOG_DECRYPTIONKEY_TRACE(...)  ((void)0)
#define LOG_DECRYPTIONKEY_DEBUG(...)  ((void)0)
#define LOG_DECRYPTIONKEY_INFO(...)   ((void)0)
#define LOG_DECRYPTIONKEY_WARN(...)   ((void)0)
#define LOG_DECRYPTIONKEY_ERROR(...)  ((void)0)

#define LOG_MISC_TRACE(...)       ((void)0)
#define LOG_MISC_DEBUG(...)       ((void)0)
#define LOG_MISC_INFO(...)        ((void)0)
#define LOG_MISC_WARN(...)        ((void)0)
#define LOG_MISC_ERROR(...)       ((void)0)

#define LOG_WINHTTP_TRACE(...)    ((void)0)
#define LOG_WINHTTP_DEBUG(...)    ((void)0)
#define LOG_WINHTTP_INFO(...)     ((void)0)
#define LOG_WINHTTP_WARN(...)     ((void)0)
#define LOG_WINHTTP_ERROR(...)    ((void)0)

#define LOG_ACHIEVEMENT_TRACE(...)  ((void)0)
#define LOG_ACHIEVEMENT_DEBUG(...)  ((void)0)
#define LOG_ACHIEVEMENT_INFO(...)   ((void)0)
#define LOG_ACHIEVEMENT_WARN(...)   ((void)0)
#define LOG_ACHIEVEMENT_ERROR(...)  ((void)0)

#define LOG_PICS_TRACE(...)  ((void)0)
#define LOG_PICS_DEBUG(...)  ((void)0)
#define LOG_PICS_INFO(...)   ((void)0)
#define LOG_PICS_WARN(...)   ((void)0)
#define LOG_PICS_ERROR(...)  ((void)0)

#endif  // OPENSTEAMTOOL_LOGGING_ENABLED
