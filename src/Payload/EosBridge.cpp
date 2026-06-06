// ─────────────────────────────────────────────────────────────────
//  Not every game sold on Steam runs its multiplayer through Steam. Some
//  ship a third-party online layer. Most such games use EOS which loads
//  inside the game process and talks to its own servers; Steam is only
//  the launcher. The Steam AppId 480 trick doesn't work since it never sees that traffic,
//  so the only way we can make these games work is in-process, i.e. right next to the SDK.
//  That's what this DLL is for.
//
//  Regular EOS works like this: the game logs into EOS with a Steam session ticket
//  (credential type 18). The server validates that ticket against a genuine
//  Steam license for the app, and so it fails the moment ownership isn't
//  real. We try to sidestep ownership entirely by redirecting the Connect login
//  to an anonymous Device ID auth, which carries no account or entitlement check.
//  The display name is still read from Steam so friends see a real name.
//
//  Device ID auth has no Epic presence behind it, so the lobby hooks
//  below drop the presence requirement that would otherwise reject us.
// ─────────────────────────────────────────────────────────────────

#include "EosBridge.h"
#include "EosTypes.h"
#include "PayloadLog.h"

#include <atomic>
#include <cstring>
#include <detours.h>

namespace {
    std::atomic_bool g_installed{false};

    EOS_Connect_Login_t          oLogin          = nullptr;
    EOS_Connect_CreateDeviceId_t oCreateDeviceId = nullptr;
    EOS_IPOContainer_Add_t       oIPOAdd         = nullptr;
    EOS_Lobby_OpFn_t             oCreateLobby    = nullptr;
    EOS_Lobby_OpFn_t             oJoinLobby      = nullptr;
    EOS_Lobby_OpFn_t             oJoinLobbyById  = nullptr;

    // Spans Connect_Login -> CreateDeviceId -> Login. Freed in the final
    // Login callback (success) or the device-id callback (failure path).
    struct LoginCtx {
        EOS_HConnect            handle;
        EOS_Connect_OnLoginCb   cb;
        void*                   cbData;
        std::string             displayName;
    };

    // EOS Device ID login requires a display name. Steam is already loaded
    // in the game process by the time EOS comes up, so walk steam_api.
    std::string SteamPersonaName() {
        HMODULE sa = GetModuleHandleW(L"steam_api64.dll");
        if (!sa) sa = GetModuleHandleW(L"steam_api.dll");

        auto pFriends = sa ? reinterpret_cast<void* (*)()>(GetProcAddress(sa, "SteamFriends")) : nullptr;
        auto pName    = sa ? reinterpret_cast<const char* (*)(void*)>(GetProcAddress(sa, "SteamAPI_ISteamFriends_GetPersonaName")) : nullptr;

        void* friends = pFriends ? pFriends() : nullptr;
        const char* name = (pName && friends) ? pName(friends) : nullptr;
        return (name && *name) ? name : "Unknown Player";
    }

    void OnLoginDone(const EOS_Connect_LoginCallbackInfo* info) {
        auto* ctx = static_cast<LoginCtx*>(info->ClientData);
        EOS_Connect_LoginCallbackInfo out = *info;
        out.ClientData = ctx->cbData;
        if (ctx->cb) ctx->cb(&out);
        delete ctx;
    }

    void OnCreateDeviceIdDone(const EOS_Connect_CreateDeviceIdCallbackInfo* info) {
        auto* ctx = static_cast<LoginCtx*>(info->ClientData);
        const bool ready = info->ResultCode == EOS_Success
                        || info->ResultCode == EOS_DuplicateNotAllowed;
        if (!ready) {
            EOS_Connect_LoginCallbackInfo fail = {};
            fail.ResultCode = info->ResultCode;
            fail.ClientData = ctx->cbData;
            if (ctx->cb) ctx->cb(&fail);
            delete ctx;
            return;
        }

        EOS_Connect_Credentials   creds{ 1, nullptr, EOS_ECT_DEVICEID_ACCESS_TOKEN };
        EOS_Connect_UserLoginInfo who  { 1, ctx->displayName.c_str() };
        EOS_Connect_LoginOptions  opts { 2, &creds, &who };
        oLogin(ctx->handle, &opts, ctx, OnLoginDone);
    }

    void hkLogin(EOS_HConnect h, const EOS_Connect_LoginOptions*,
                 void* cbData, EOS_Connect_OnLoginCb cb)
    {
        auto* ctx = new LoginCtx{ h, cb, cbData, SteamPersonaName() };
        EOS_Connect_CreateDeviceIdOptions create{ 1, "PC" };
        oCreateDeviceId(h, &create, ctx, OnCreateDeviceIdDone);
    }

    EOS_EResult hkIPOAdd(EOS_HIntegratedPlatformOptionsContainer, const void*) {
        return EOS_Success;
    }

    // bPresenceEnabled requires an Epic account we don't have. The field
    // offset differs per struct because the preceding members differ.
    void StripPresence(const void* opts, size_t flagOffset, int32_t minApiVer) {
        if (!opts) return;
        if (*reinterpret_cast<const int32_t*>(opts) < minApiVer) return;
        auto* flag = reinterpret_cast<EOS_Bool*>(
            reinterpret_cast<uintptr_t>(opts) + flagOffset);
        if (*flag) *flag = 0;
    }

    void hkCreateLobby(EOS_HLobby h, const void* opts, void* cd, void* cb) {
        StripPresence(opts, offsetof(EOS_Lobby_CreateLobbyOptions_Partial, bPresenceEnabled), 2);
        oCreateLobby(h, opts, cd, cb);
    }
    void hkJoinLobby(EOS_HLobby h, const void* opts, void* cd, void* cb) {
        StripPresence(opts, offsetof(EOS_Lobby_JoinLobbyOptions_Partial, bPresenceEnabled), 2);
        oJoinLobby(h, opts, cd, cb);
    }
    void hkJoinLobbyById(EOS_HLobby h, const void* opts, void* cd, void* cb) {
        StripPresence(opts, offsetof(EOS_Lobby_JoinLobbyByIdOptions_Partial, bPresenceEnabled), 1);
        oJoinLobbyById(h, opts, cd, cb);
    }

    template <typename Fn>
    bool Resolve(HMODULE m, const char* name, Fn& slot) {
        slot = reinterpret_cast<Fn>(GetProcAddress(m, name));
        if (!slot) PayloadLog::Write(std::string("missing EOS export: ") + name);
        return slot != nullptr;
    }
}

namespace EosBridge {
    void InstallOn(HMODULE eos) {
        bool expected = false;
        if (!eos || !g_installed.compare_exchange_strong(expected, true)) return;

        bool ok = Resolve(eos, "EOS_Connect_Login",                          oLogin)
                & Resolve(eos, "EOS_Connect_CreateDeviceId",                 oCreateDeviceId)
                & Resolve(eos, "EOS_IntegratedPlatformOptionsContainer_Add", oIPOAdd)
                & Resolve(eos, "EOS_Lobby_CreateLobby",                      oCreateLobby)
                & Resolve(eos, "EOS_Lobby_JoinLobby",                        oJoinLobby)
                & Resolve(eos, "EOS_Lobby_JoinLobbyById",                    oJoinLobbyById);
        if (!ok) { g_installed.store(false); return; }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&oLogin),         reinterpret_cast<PVOID>(hkLogin));
        DetourAttach(reinterpret_cast<PVOID*>(&oIPOAdd),        reinterpret_cast<PVOID>(hkIPOAdd));
        DetourAttach(reinterpret_cast<PVOID*>(&oCreateLobby),   reinterpret_cast<PVOID>(hkCreateLobby));
        DetourAttach(reinterpret_cast<PVOID*>(&oJoinLobby),     reinterpret_cast<PVOID>(hkJoinLobby));
        DetourAttach(reinterpret_cast<PVOID*>(&oJoinLobbyById), reinterpret_cast<PVOID>(hkJoinLobbyById));
        LONG err = DetourTransactionCommit();
        if (err != NO_ERROR) {
            PayloadLog::Write("DetourTransactionCommit failed err=" + std::to_string(err));
            g_installed.store(false);
            return;
        }
        PayloadLog::Write("EOS hooks installed");
    }
}
