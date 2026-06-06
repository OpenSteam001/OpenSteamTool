#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────
//  Minimal mirror of the EOS SDK structs, holding only the fields
//  we actually read or write. Each struct starts with an ApiVersion we
//  check at runtime, ensuring compatibility across SDK versions.
// ─────────────────────────────────────────────────────────────────

using EOS_EResult = int32_t;
using EOS_Bool    = int32_t;
using EOS_HConnect = void*;
using EOS_HLobby   = void*;
using EOS_HIntegratedPlatformOptionsContainer = void*;
using EOS_ProductUserId    = void*;
using EOS_ContinuanceToken = void*;

constexpr EOS_EResult EOS_Success             = 0;
constexpr EOS_EResult EOS_DuplicateNotAllowed = 24;  // device id already exists
constexpr int32_t EOS_ECT_DEVICEID_ACCESS_TOKEN = 10;

#pragma pack(push, 8)

struct EOS_Connect_Credentials {
    int32_t  ApiVersion;
    const char* Token;
    int32_t  Type;
};
struct EOS_Connect_UserLoginInfo {
    int32_t  ApiVersion;
    const char* DisplayName;
};
struct EOS_Connect_LoginOptions {
    int32_t  ApiVersion;
    const EOS_Connect_Credentials*   Credentials;
    const EOS_Connect_UserLoginInfo* UserLoginInfo;
};
struct EOS_Connect_LoginCallbackInfo {
    EOS_EResult ResultCode;
    void*       ClientData;
    EOS_ProductUserId    LocalUserId;
    EOS_ContinuanceToken ContinuanceToken;
};
struct EOS_Connect_CreateDeviceIdOptions {
    int32_t  ApiVersion;
    const char* DeviceModel;
};
struct EOS_Connect_CreateDeviceIdCallbackInfo {
    EOS_EResult ResultCode;
    void*       ClientData;
};

// Lobby option prefixes up to and including bPresenceEnabled; everything
// past that we leave untouched by passing the original pointer through.
struct EOS_Lobby_CreateLobbyOptions_Partial {
    int32_t           ApiVersion;
    EOS_ProductUserId LocalUserId;
    uint32_t          MaxLobbyMembers;
    int32_t           PermissionLevel;
    EOS_Bool          bPresenceEnabled;  // v2+
};
struct EOS_Lobby_JoinLobbyOptions_Partial {
    int32_t           ApiVersion;
    void*             LobbyDetailsHandle;
    EOS_ProductUserId LocalUserId;
    EOS_Bool          bPresenceEnabled;  // v2+
};
struct EOS_Lobby_JoinLobbyByIdOptions_Partial {
    int32_t           ApiVersion;
    const char*       LobbyId;
    EOS_ProductUserId LocalUserId;
    EOS_Bool          bPresenceEnabled;  // v1+
};

#pragma pack(pop)

using EOS_Connect_OnLoginCb          = void(*)(const EOS_Connect_LoginCallbackInfo*);
using EOS_Connect_OnCreateDeviceIdCb = void(*)(const EOS_Connect_CreateDeviceIdCallbackInfo*);

using EOS_Connect_Login_t          = void(*)(EOS_HConnect, const EOS_Connect_LoginOptions*, void*, EOS_Connect_OnLoginCb);
using EOS_Connect_CreateDeviceId_t = void(*)(EOS_HConnect, const EOS_Connect_CreateDeviceIdOptions*, void*, EOS_Connect_OnCreateDeviceIdCb);
using EOS_IPOContainer_Add_t       = EOS_EResult(*)(EOS_HIntegratedPlatformOptionsContainer, const void*);
// CreateLobby / JoinLobby / JoinLobbyById share shape (handle, opts, cd, completion).
using EOS_Lobby_OpFn_t             = void(*)(EOS_HLobby, const void*, void*, void*);
