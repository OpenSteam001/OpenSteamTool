#include "Hooks_NetPacket.h"
#include "Hooks_Manifest.h"
#include "Hooks_Misc.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/AppTicket.h"
#include "Utils/Hash.h"
#include <chrono>
#include <future>
#include <unordered_map>

#include "steam_messages.pb.h"

// ════════════════════════════════════════════════════════════════
//  Shared infrastructure
// ════════════════════════════════════════════════════════════════
namespace {

    constexpr uint32 kMaxBodySize   = 8092;
    constexpr uint32 kMaxHdrSize    = 1024;
    constexpr uint32 kMaxPacketSize = 8 + kMaxHdrSize + kMaxBodySize;
    constexpr int    kPacketPoolSize = 8;

    // ── Incoming (RecvPkt) packet pool ─────────────────────
    uint8  g_NewBody[kMaxBodySize];
    uint32 g_cbNewBody   = 0;
    uint8  g_NewHdr[kMaxHdrSize];
    uint32 g_cbNewHdr    = 0;
    bool   g_NeedReplaceBody = false;
    bool   g_NeedReplaceHdr  = false;
    bool   g_ResizedInPlace = false;
    uint32 g_NewBodySize    = 0;
    uint8  g_RecvPacketPool[kPacketPoolSize][kMaxPacketSize];
    int    g_RecvPacketPoolIdx = 0;

    // ── Outgoing (BBuildAndAsyncSendFrame) — same pattern ───────
    uint8  g_SendNewBody[kMaxBodySize];
    uint32 g_cbSendNewBody = 0;
    bool   g_NeedReplaceSend = false;
    uint8  g_SendPacketPool[kPacketPoolSize][kMaxPacketSize];
    int    g_SendPacketPoolIdx = 0;

    // ── EMsg -> name lookup  ─────────────────────────
    RESOLVE_FUNC(PchMsgNameFromEMsg, char*, EMsg eMsg);
    inline const char* MsgName(EMsg eMsg) {
        if (oPchMsgNameFromEMsg) return oPchMsgNameFromEMsg(eMsg);
        return "?";
    }


    // ── Packet layout ──────────────────────────────────────────
    inline bool UnpackRaw(const uint8* data, uint32 size,
                          EMsg& eMsg, const uint8*& pHdr, uint32& cbHdr,
                          const uint8*& pBody, uint32& cbBody)
    {
        if (!data || size < sizeof(MsgHdr)) {
        fail:
            eMsg = static_cast<EMsg>(0);
            cbHdr = 0;
            pHdr = nullptr;
            pBody = nullptr;
            cbBody = 0;
            return false;
        }
        const MsgHdr* hdr = reinterpret_cast<const MsgHdr*>(data);
        if (!(hdr->eMsg & kMsgHdrProtoFlag)) goto fail;

        eMsg  = static_cast<EMsg>(hdr->eMsg & ~kMsgHdrProtoFlag);
        cbHdr = hdr->headerLength;
        uint32 off = sizeof(MsgHdr) + cbHdr;
        if (off > size) goto fail;
        pHdr   = data + sizeof(MsgHdr);
        pBody  = data + off;
        cbBody = size - off;
        return true;
    }

    // ── Incoming: replace header and/or body (ring-buffer pool) ──
    inline void ReplaceRecvPacket(CNetPacket* p,
                                  const uint8* pNewHdr, uint32 cbNewHdr,
                                  const uint8* pNewBody, uint32 cbNewBody)
    {
        uint32 newSize = sizeof(MsgHdr) + cbNewHdr + cbNewBody;
        if (newSize > sizeof(g_RecvPacketPool[0])) return;

        uint8* buf = g_RecvPacketPool[g_RecvPacketPoolIdx];
        const MsgHdr* orig = reinterpret_cast<const MsgHdr*>(p->m_pubData);
        MsgHdr* out = reinterpret_cast<MsgHdr*>(buf);
        out->eMsg         = orig->eMsg;
        out->headerLength = cbNewHdr;
        memcpy(buf + sizeof(MsgHdr), pNewHdr, cbNewHdr);
        if (cbNewBody)
            memcpy(buf + sizeof(MsgHdr) + cbNewHdr, pNewBody, cbNewBody);
        p->m_pubData = buf;
        p->m_cubData = newSize;

        g_RecvPacketPoolIdx = (g_RecvPacketPoolIdx + 1) % kPacketPoolSize;
    }

    // ── Outgoing: assemble modified packet (ring-buffer pool) ────
    inline uint8* ReplaceSendPacket(const uint8* pubData,
                                    uint32 cbHdr, const uint8* pHdr,
                                    const uint8* pNewBody, uint32 cbNewBody,
                                    uint32* pNewSize)
    {
        *pNewSize = sizeof(MsgHdr) + cbHdr + cbNewBody;
        if (*pNewSize > sizeof(g_SendPacketPool[0])) return nullptr;

        uint8* buf = g_SendPacketPool[g_SendPacketPoolIdx];
        const MsgHdr* orig = reinterpret_cast<const MsgHdr*>(pubData);
        MsgHdr* out = reinterpret_cast<MsgHdr*>(buf);
        out->eMsg         = orig->eMsg;
        out->headerLength = cbHdr;
        memcpy(buf + sizeof(MsgHdr), pHdr, cbHdr);
        memcpy(buf + sizeof(MsgHdr) + cbHdr, pNewBody, cbNewBody);
        g_SendPacketPoolIdx = (g_SendPacketPoolIdx + 1) % kPacketPoolSize;
        return buf;
    }

    // ── Hash constants for target_job_name dispatch ─────────────
    constexpr uint32 HASH_JOB_NotifyRunningApps = Fnv1aHash("FamilyGroupsClient.NotifyRunningApps#1");
    constexpr uint32 HASH_JOB_GetUserStats = Fnv1aHash("Player.GetUserStats#1");
    constexpr uint32 HASH_JOB_GetManifestRequestCode = Fnv1aHash("ContentServerDirectory.GetManifestRequestCode#1");

} // anonymous namespace


// ════════════════════════════════════════════════════════════════
//  Hooks_NetPacket_AccessToken
//
//  Outgoing: CMsgClientPICSProductInfoRequest (eMsg 8903)
// ════════════════════════════════════════════════════════════════
namespace Hooks_NetPacket_AccessToken {

    bool HandleSend(const uint8* pBody, uint32 cbBody)
    {
        CMsgClientPICSProductInfoRequest req;
        if (!req.ParseFromArray(pBody, cbBody)) {
            LOG_PICS_WARN("Failed to ParseFromArray CMsgClientPICSProductInfoRequest");
            return false;
        }
        LOG_PICS_DEBUG("CMsgClientPICSProductInfoRequest original body:\n{}", req.DebugString());

        bool needsPatch = false;
        for (const auto& app : req.apps()) {
            if (LuaConfig::HasDepot(app.appid()) && LuaConfig::GetAccessToken(app.appid())) {
                needsPatch = true;
                LOG_PICS_DEBUG("CMsgClientPICSProductInfoRequest: found appid {} with access_token, need patching", app.appid());
                break;
            }
        }
        if (!needsPatch) {
            LOG_PICS_TRACE("CMsgClientPICSProductInfoRequest: no apps need token injection, skip");
            return false;
        }

        int injected = 0, noToken = 0, notAddAppId = 0;
        for (auto& app : *req.mutable_apps()) {
            if (LuaConfig::HasDepot(app.appid())) {
                uint64_t token = LuaConfig::GetAccessToken(app.appid());
                if (token) {
                    LOG_PICS_DEBUG("CMsgClientPICSProductInfoRequest: inject appid={}: {} -> {}", app.appid(),
                               app.has_access_token() ? std::to_string(app.access_token()) : "absent",
                               token);
                    app.set_access_token(token);
                    ++injected;
                } else {
                    LOG_PICS_WARN("CMsgClientPICSProductInfoRequest: skip appid={}: in depot, no token configured", app.appid());
                    ++noToken;
                }
            } else {
                ++notAddAppId;
            }
        }
        LOG_PICS_DEBUG("CMsgClientPICSProductInfoRequest: injected={} no_token={} not_in_add_appid={} total={}",
                   injected, noToken, notAddAppId, req.apps_size());

        g_cbSendNewBody = static_cast<uint32>(req.ByteSizeLong());
        if (g_cbSendNewBody > kMaxBodySize) {
            LOG_PICS_WARN("CMsgClientPICSProductInfoRequest: encoded size {} exceeds buffer", g_cbSendNewBody);
            return false;
        }
        if (!req.SerializeToArray(g_SendNewBody, kMaxBodySize)) {
            LOG_PICS_WARN("CMsgClientPICSProductInfoRequest: Failed to encode modified request");
            return false;
        }

        LOG_PICS_DEBUG("CMsgClientPICSProductInfoRequest: modified body: {}", req.DebugString());
        return true;
    }

} // namespace Hooks_NetPacket_AccessToken


// ════════════════════════════════════════════════════════════════
//  Hooks_NetPacket_UserStats
//
//  Outgoing: CPlayer_GetUserStats_Request  (eMsg 151 -> target: Player.GetUserStats#1)
//            CMsgClientGetUserStats        (eMsg 818)
//  Incoming: CPlayer_GetUserStats_Response (eMsg 147 ← target: Player.GetUserStats#1)
//            CMsgClientGetUserStatsResponse(eMsg 819)
// ════════════════════════════════════════════════════════════════
namespace Hooks_NetPacket_UserStats {

    // jobid_source -> appid mapping (eMsg 151 request -> eMsg 147 response)
    std::unordered_map<uint64, AppId_t> g_JobIdToAppId;

    // ── Send: CPlayer_GetUserStats_Request (eMsg 151) ──────────
    bool HandleSend_GetUserStats(const uint8* pBody, uint32 cbBody,
                                 const uint8* pHdr, uint32 cbHdr)
    {

        CPlayer_GetUserStats_Request req;
        if (!req.ParseFromArray(pBody, cbBody)) {
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats request: failed to ParseFromArray");
            return false;
        }
        if (!req.has_appid()) {
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats request: missing appid");
            return false;
        }

        LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats request: original body:\n{}", req.DebugString());
        
        AppId_t appId = req.appid();
        bool hasShaSchema = req.has_sha_schema() && !req.sha_schema().empty();

        if (hasShaSchema) {
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats request: sha_schema is present, do not spoof");
            return false;
        }
        if (!LuaConfig::HasDepot(appId)) {
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats request: appid={} is not in addappid", appId);
            return false;
        }

        // Save jobid_source -> appid for the response handler
        CMsgProtoBufHeader hdr;
        if (hdr.ParseFromArray(pHdr, cbHdr) && hdr.has_jobid_source()) {
            uint64 jobId = hdr.jobid_source();
            g_JobIdToAppId[jobId] = appId;
            LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats request: stored jobid={} -> appid={}", jobId, appId);
        }

        uint64_t newSteamId = LuaConfig::GetStatSteamId(appId);
        req.set_steamid(newSteamId);

        g_cbSendNewBody = static_cast<uint32>(req.ByteSizeLong());
        if (!req.SerializeToArray(g_SendNewBody, kMaxBodySize)) {
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats request: failed to encode");
            return false;
        }

        LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats request: modified body:\n{}", req.DebugString());
        return true;
    }

    // ── Recv: CPlayer_GetUserStats_Response (eMsg 147) ─────────
    //     Header: set eresult=OK.  Body: strip stats (field 4).
    void HandleRecv_GetUserStatsResponse(const uint8* pHdr, uint32 cbHdr,
                                    const uint8* pBody, uint32 cbBody)
    {
        // Header: set eresult=OK
        CMsgProtoBufHeader hdrMsg;
        if (!hdrMsg.ParseFromArray(pHdr, cbHdr)){
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats response: failed to ParseFromArray original header");
            return;
        }
        LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats response: original header:\n{}", hdrMsg.DebugString());

        // Look up appid via jobid_target -> jobid_source match
        AppId_t appId = 0;
        bool hasAppId = false;
        if (hdrMsg.has_jobid_target()) {
            uint64 jobId = hdrMsg.jobid_target();
            auto it = g_JobIdToAppId.find(jobId);
            if (it != g_JobIdToAppId.end()) {
                appId = it->second;
                hasAppId = true;
                LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats response: matched jobid={} -> appid={}", jobId, appId);
                g_JobIdToAppId.erase(it);
            }
        }

        hdrMsg.set_eresult(static_cast<int32_t>(k_EResultOK));
        g_cbNewHdr = static_cast<uint32>(hdrMsg.ByteSizeLong());
        if (g_cbNewHdr > kMaxHdrSize || !hdrMsg.SerializeToArray(g_NewHdr, kMaxHdrSize))
            return;
        LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats response: modified header:\n{}", hdrMsg.DebugString());
        g_NeedReplaceHdr = true;

        // Body: strip stats (only if appid was matched and is in our config)
        CPlayer_GetUserStats_Response resp;
        if (!resp.ParseFromArray(pBody, cbBody)){
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats response: failed to ParseFromArray original response");
            return;
        }
        LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats response: original body:\n{}", resp.DebugString());

        if (!hasAppId || !LuaConfig::HasDepot(appId)) {
            LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats response: no appid match, skip body strip");
            return;
        }

        resp.clear_stats();
        g_NewBodySize = static_cast<uint32>(resp.ByteSizeLong());
        if (!resp.SerializeToArray(const_cast<uint8*>(pBody), cbBody)){
            LOG_ACHIEVEMENT_WARN("Player::GetUserStats response: failed to SerializeToArray modified response");
            return;
        }
        g_ResizedInPlace = true;

        LOG_ACHIEVEMENT_DEBUG("Player::GetUserStats response: modified body:\n{}", resp.DebugString());
    }

    // ── Send: CMsgClientGetUserStats (eMsg 818) ────────────────
    bool HandleSend_ClientGetUserStats(const uint8* pBody, uint32 cbBody)
    {
        CMsgClientGetUserStats req;
        if (!req.ParseFromArray(pBody, cbBody)) {
            LOG_ACHIEVEMENT_WARN("ClientGetUserStats request: failed to ParseFromArray");
            return false;
        }
        LOG_ACHIEVEMENT_DEBUG("ClientGetUserStats request: original body:\n{}", req.DebugString());

        if (!req.has_game_id()) {
            LOG_ACHIEVEMENT_WARN("ClientGetUserStats request: missing game_id");
            return false;
        }
        AppId_t appId = static_cast<AppId_t>(req.game_id());
        if (!LuaConfig::HasDepot(appId)) {
            LOG_ACHIEVEMENT_WARN("ClientGetUserStats request: appid={} is not in addappid", appId);
            return false;
        }
        if (!req.has_schema_local_version() || req.schema_local_version() != -1) {
            LOG_ACHIEVEMENT_WARN("ClientGetUserStats request: schema_local_version is not -1");
            return false;
        }

        uint64_t newSteamId = LuaConfig::GetStatSteamId(appId);
        req.set_steam_id_for_user(newSteamId);

        g_cbSendNewBody = static_cast<uint32>(req.ByteSizeLong());
        if (!req.SerializeToArray(g_SendNewBody, kMaxBodySize)) {
            LOG_ACHIEVEMENT_WARN("ClientGetUserStats request: failed to SerializeToArray");
            return false;
        }

        LOG_ACHIEVEMENT_DEBUG("ClientGetUserStats request: modified body:\n{}", req.DebugString());
        return true;
    }

    // ── Recv: CMsgClientGetUserStatsResponse (eMsg 819) ────────
    //     Strip stats(5) + achievement_blocks(6), patch eresult->OK.
    bool HandleRecv_ClientGetUserStatsResponse(const uint8* pBody, uint32 cbBody)
    {
        CMsgClientGetUserStatsResponse resp;
        if (!resp.ParseFromArray(pBody, cbBody))
            return false;
        LOG_ACHIEVEMENT_DEBUG("ClientGetUserStats response: original body:\n{}", resp.DebugString());
        if(!resp.has_game_id() || !LuaConfig::HasDepot(static_cast<AppId_t>(resp.game_id()))) {
            LOG_ACHIEVEMENT_DEBUG("ClientGetUserStats response: no modification needed");
            return false;
        }
        resp.clear_stats();
        resp.clear_achievement_blocks();
        resp.set_eresult(1);  // k_EResultOK
        LOG_ACHIEVEMENT_DEBUG("ClientGetUserStats response: clear stats and achievement_blocks, set eresult=OK");

        g_NewBodySize = static_cast<uint32>(resp.ByteSizeLong());
        if (!resp.SerializeToArray(const_cast<uint8*>(pBody), cbBody))
            return false;

        g_ResizedInPlace = true;
        LOG_ACHIEVEMENT_DEBUG("ClientGetUserStats response: modified body:\n{}", resp.DebugString());
        return true;
    }

} // namespace Hooks_NetPacket_UserStats


// ════════════════════════════════════════════════════════════════
//  Hooks_NetPacket_ETicket
//
//  Incoming: CMsgClientRequestEncryptedAppTicketResponse (eMsg 5527)
// ════════════════════════════════════════════════════════════════
namespace Hooks_NetPacket_ETicket {

    void HandleEncryptedAppTicketResponse(const uint8* pBody, uint32 cbBody)
    {
        CMsgClientRequestEncryptedAppTicketResponse resp;
        if (!resp.ParseFromArray(pBody, cbBody)) {
            LOG_NETPACKET_WARN("ClientRequestEncryptedAppTicketResponse: failed to ParseFromArray");
            return;
        }
        LOG_NETPACKET_DEBUG("ClientRequestEncryptedAppTicketResponse: original body:\n{}", resp.DebugString());

        if (resp.eresult() == k_EResultOK) return;
        if (!LuaConfig::HasDepot(resp.app_id())) return;

        auto ticket = AppTicket::GetEncryptedTicketFromRegistry(resp.app_id());
        if (ticket.empty()) return;

        if (!resp.mutable_encrypted_app_ticket()->ParseFromArray(
                ticket.data(), static_cast<int>(ticket.size()))) {
            LOG_NETPACKET_WARN("ClientRequestEncryptedAppTicketResponse: failed to ParseFromArray EncryptedAppTicket");
            return;
        }

        resp.set_eresult(k_EResultOK);

        auto encSize = resp.ByteSizeLong();
        if (encSize > sizeof(g_NewBody)) {
            LOG_NETPACKET_WARN("ClientRequestEncryptedAppTicketResponse: modified message too large");
            return;
        }
        if (!resp.SerializeToArray(g_NewBody, sizeof(g_NewBody))) {
            LOG_NETPACKET_WARN("ClientRequestEncryptedAppTicketResponse: failed to SerializeToArray modified response");
            return;
        }
        
        LOG_NETPACKET_DEBUG("ClientRequestEncryptedAppTicketResponse: modified body:\n{}", resp.DebugString());

        g_cbNewBody = static_cast<uint32>(encSize);
        g_NeedReplaceBody = true;
    }

} // namespace Hooks_NetPacket_ETicket


// ════════════════════════════════════════════════════════════════
//  Hooks_NetPacket_FamilySharing
// ════════════════════════════════════════════════════════════════
namespace Hooks_NetPacket_FamilySharing {

    void ClearBody(const uint8*, uint32)
    {
        LOG_NETPACKET_DEBUG("Clearing family sharing message...");
        g_cbNewBody = 0;
        g_NeedReplaceBody = true;
    }

} // namespace Hooks_NetPacket_FamilySharing


// ════════════════════════════════════════════════════════════════
//  Hooks_NetPacket_Manifest
//
//  Outgoing: ContentServerDirectory.GetManifestRequestCode#1  (eMsg 151)
//  Incoming: ContentServerDirectory.GetManifestRequestCode#1  (eMsg 147)
//
//  Launches an async HTTP fetch on send; the recv handler waits up to
//  12 s for the result and patches both header (eresult=OK) and body
//  (manifest_request_code).  On timeout or failure the original
//  response passes through unmodified.
// ════════════════════════════════════════════════════════════════
namespace Hooks_NetPacket_Manifest {

    std::unordered_map<uint64, std::shared_future<uint64>> g_CodeFutures;
    std::mutex g_CodeMutex;
    constexpr uint32 kMaxWaitSeconds = 12;

    bool HandleSend(const uint8* pBody, uint32 cbBody,
                    const uint8* pHdr, uint32 cbHdr)
    {
        CContentServerDirectory_GetManifestRequestCode_Request req;
        if (!req.ParseFromArray(pBody, cbBody)) {
            LOG_MANIFEST_WARN("GetManifestRequestCode: failed to parse request");
            return false;
        }
        if (!req.has_depot_id() || !req.has_manifest_id()) return false;
        if (!LuaConfig::HasDepot(req.depot_id())) return false;

        CMsgProtoBufHeader hdr;
        if (!hdr.ParseFromArray(pHdr, cbHdr) || !hdr.has_jobid_source()) {
            LOG_MANIFEST_WARN("GetManifestRequestCode: missing jobid_source in header");
            return false;
        }

        uint64 jobId       = hdr.jobid_source();
        uint64 manifestGid = req.manifest_id();
        uint32 depotId     = req.depot_id();
        uint32 appId       = req.has_app_id() ? req.app_id() : 0;

        LOG_MANIFEST_DEBUG("GetManifestRequestCode send: depot={} gid={} jobid={} app_id={}",
                            depotId, manifestGid, jobId, appId);

        auto task = std::async(std::launch::async,
            [manifestGid, depotId, appId]() -> uint64 {
                uint64 code = 0;
                Hooks_Manifest::FetchManifestRequestCode(manifestGid, &code, appId, depotId);
                return code;
            });

        {
            std::lock_guard<std::mutex> lock(g_CodeMutex);
            g_CodeFutures[jobId] = task.share();
        }

        return false; // Don't modify the outgoing request body
    }

    void HandleRecv(const uint8* pBody, uint32 cbBody,
                    const uint8* pHdr, uint32 cbHdr)
    {
        CMsgProtoBufHeader hdr;
        if (!hdr.ParseFromArray(pHdr, cbHdr)){
            LOG_MANIFEST_WARN("GetManifestRequestCode recv: failed to ParseFromArray original header");
            return;
        }

        uint64 jobId = hdr.jobid_target();
        std::shared_future<uint64> future;

        {
            std::lock_guard<std::mutex> lock(g_CodeMutex);
            auto it = g_CodeFutures.find(jobId);
            if (it == g_CodeFutures.end()) return;
            future = it->second;
            g_CodeFutures.erase(it); // Always clean up immediately
        }
        // Wait up to kMaxWaitSeconds seconds for the HTTP fetch to complete
        auto status = future.wait_for(std::chrono::seconds(kMaxWaitSeconds));
        if (status != std::future_status::ready) {
            LOG_MANIFEST_WARN("GetManifestRequestCode recv: HTTP timed out for jobid={}", jobId);
            return;
        }

        uint64 code = future.get();
        if (!code) {
            LOG_MANIFEST_WARN("GetManifestRequestCode recv: HTTP returned 0 for jobid={}", jobId);
            return;
        }

        LOG_MANIFEST_DEBUG("GetManifestRequestCode recv: injecting code={} for jobid={}",
                            code, jobId);

        // Header: set eresult=OK
        hdr.set_eresult(static_cast<int32_t>(k_EResultOK));
        g_cbNewHdr = static_cast<uint32>(hdr.ByteSizeLong());
        if (g_cbNewHdr > kMaxHdrSize || !hdr.SerializeToArray(g_NewHdr, kMaxHdrSize)){
            LOG_MANIFEST_WARN("GetManifestRequestCode recv: failed to SerializeToArray modified header,"
                    "g_cbNewHdr: {}, kMaxHdrSize: {}", g_cbNewHdr, kMaxHdrSize);
            return;
        }
        g_NeedReplaceHdr = true;

        // Body: set manifest_request_code
        CContentServerDirectory_GetManifestRequestCode_Response resp;
        resp.set_manifest_request_code(code);

        g_cbNewBody = static_cast<uint32>(resp.ByteSizeLong());
        if (g_cbNewBody > kMaxBodySize || !resp.SerializeToArray(g_NewBody, kMaxBodySize)){
            LOG_MANIFEST_WARN("GetManifestRequestCode recv: failed to SerializeToArray modified body,"
                "g_cbNewBody:{}, kMaxBodySize:{}", g_cbNewBody, kMaxBodySize);
            return;
        }
        g_NeedReplaceBody = true;
    }

} // namespace Hooks_NetPacket_Manifest


// ════════════════════════════════════════════════════════════════
//  Hooks_NetPacket_RichPresence
//
//  Outgoing: CMsgClientGamesPlayed   (eMsg 742 / 5410)
//  Incoming: CMsgClientPersonaState  (eMsg 766)
//
//  The server drops friend-side broadcasts for unowned AppIds, so the
//  banner stays on the last cached state.  Cache real self-pushes and
//  re-deliver a patched copy through oRecvPkt by borrowing the next
//  carrier packet's data pointer.
// ════════════════════════════════════════════════════════════════
namespace Hooks_NetPacket_RichPresence {

    AppId_t g_PlayingAppId = 0;
    uint64  g_LocalSteamId = 0;

    // Most recent self-PersonaState bytes captured from a real server push.
    // Reused as the template every game launch.
    uint8   g_SelfHdr [kMaxHdrSize];
    uint32  g_cbSelfHdr      = 0;
    uint8   g_SelfBody[kMaxBodySize];
    uint32  g_cbSelfBody     = 0;
    bool    g_HaveSelfCached = false;

    // Manufactured PersonaState packet (eMsg 766) ready to inject.
    uint8   g_InjectPkt[kMaxPacketSize];
    uint32  g_cbInjectPkt   = 0;
    bool    g_InjectPending = false;

    // Rich presence KVs per AppId, captured from outbound
    // CMsgClientRichPresenceUpload.  Per-AppId so a multi-game stack
    // does not conflate KV state.
    std::unordered_map<AppId_t, std::vector<std::pair<std::string, std::string>>> g_RPKvsByAppId;

    // Walk Steam's binary KV1 stream (a top-level "RP" struct around
    // string KVs) and collect every string KV at any depth.  Type 0x00
    // starts a struct, 0x01 a string KV, 0x08 ends a struct.  String KVs
    // are null-terminated key + null-terminated value.
    static void ExtractStringKVs(const uint8* data, uint32 size,
                                 std::vector<std::pair<std::string, std::string>>& out)
    {
        uint32 pos = 0;
        int depth = 0;
        auto readCStr = [&](std::string& s) -> bool {
            uint32 start = pos;
            while (pos < size && data[pos] != 0) ++pos;
            if (pos >= size) return false;
            s.assign(reinterpret_cast<const char*>(data + start), pos - start);
            ++pos;
            return true;
        };
        while (pos < size) {
            uint8 type = data[pos++];
            if (type == 0x08) {
                if (depth > 0) { --depth; continue; }
                break;
            }
            if (type == 0x00) {
                std::string name;
                if (!readCStr(name)) return;
                ++depth;
            } else if (type == 0x01) {
                std::string key, value;
                if (!readCStr(key) || !readCStr(value)) return;
                out.emplace_back(std::move(key), std::move(value));
            } else {
                return;
            }
        }
    }

    // Patch the self Friend entry with the appid (0 = stopped) and per-app
    // KVs.  Mask status_flags's RichPresence bit (0x1000) on appid + empty
    // KVs so a freshly launched game's first inject does not wipe the UI's
    // m_mapRichPresence, which is rebuilt from rich_presence() whenever
    // that bit is set.
    static void ApplyGameFields(CMsgClientPersonaState& msg,
                                CMsgClientPersonaState::Friend* entry,
                                AppId_t appid)
    {
        // EClientPersonaStateFlag::k_EClientPersonaStateFlagRichPresence
        constexpr uint32 kStatusFlagRichPresence = 0x1000;

        if (appid) {
            entry->set_game_played_app_id(appid);
            entry->set_gameid(static_cast<uint64>(appid));
            std::string name = Hooks_Misc::GetGameNameByAppID(appid);
            if (!name.empty()) entry->set_game_name(name);
            entry->clear_rich_presence();
            auto it = g_RPKvsByAppId.find(appid);
            const bool hasKvs = (it != g_RPKvsByAppId.end()) && !it->second.empty();
            if (hasKvs) {
                for (const auto& [k, v] : it->second) {
                    auto* kv = entry->add_rich_presence();
                    kv->set_key(k);
                    kv->set_value(v);
                }
                msg.set_status_flags(msg.status_flags() | kStatusFlagRichPresence);
            } else {
                msg.set_status_flags(msg.status_flags() & ~kStatusFlagRichPresence);
            }
        } else {
            entry->clear_game_played_app_id();
            entry->clear_gameid();
            entry->clear_game_name();
            entry->clear_rich_presence();
            msg.set_status_flags(msg.status_flags() | kStatusFlagRichPresence);
        }
    }

    static bool BuildInject(AppId_t appid)
    {
        if (!g_HaveSelfCached) return false;

        CMsgClientPersonaState msg;
        if (!msg.ParseFromArray(g_SelfBody, g_cbSelfBody)) return false;

        // Find our entry (a self-push always contains it).
        CMsgClientPersonaState::Friend* entry = nullptr;
        for (int i = 0; i < msg.friends_size(); ++i) {
            auto* f = msg.mutable_friends(i);
            if (f->has_friendid() && f->friendid() == g_LocalSteamId) {
                entry = f;
                break;
            }
        }
        if (!entry) return false;

        ApplyGameFields(msg, entry, appid);

        uint32 hdrSize  = g_cbSelfHdr;
        uint32 bodySize = static_cast<uint32>(msg.ByteSizeLong());
        uint32 total    = sizeof(MsgHdr) + hdrSize + bodySize;
        if (total > sizeof(g_InjectPkt) || bodySize > kMaxBodySize) {
            LOG_RICHPRESENCE_WARN("Inject packet too large ({} bytes)", total);
            return false;
        }

        auto* mhdr = reinterpret_cast<MsgHdr*>(g_InjectPkt);
        mhdr->eMsg = static_cast<EMsg>(
            static_cast<uint32>(k_EMsgClientPersonaState) | kMsgHdrProtoFlag);
        mhdr->headerLength = hdrSize;
        memcpy(g_InjectPkt + sizeof(MsgHdr), g_SelfHdr, hdrSize);
        if (!msg.SerializeToArray(g_InjectPkt + sizeof(MsgHdr) + hdrSize, bodySize))
            return false;

        g_cbInjectPkt = total;
        LOG_RICHPRESENCE_INFO("Built inject for appid {} ({} bytes)", appid, total);
        return true;
    }

    // Decode outbound CMsgClientRichPresenceUpload into per-AppId KVs
    // and stage a fresh PersonaState inject.
    void TrackRPSend(const uint8* pBody, uint32 cbBody)
    {
        if (g_LocalSteamId == 0 || g_PlayingAppId == 0) return;

        CMsgClientRichPresenceUpload up;
        if (!up.ParseFromArray(pBody, cbBody)) return;
        if (!up.has_rich_presence_kv()) return;

        const std::string& kv = up.rich_presence_kv();
        auto& kvs = g_RPKvsByAppId[g_PlayingAppId];
        kvs.clear();
        ExtractStringKVs(reinterpret_cast<const uint8*>(kv.data()),
                         static_cast<uint32>(kv.size()), kvs);
        LOG_RICHPRESENCE_DEBUG("RP upload appid={}: kv_bytes={} extracted={} pairs",
            g_PlayingAppId, kv.size(), kvs.size());

        if (BuildInject(g_PlayingAppId)) g_InjectPending = true;
    }

    void TrackSend(const CMsgClientGamesPlayed& msg, const uint8* pHdr, uint32 cbHdr)
    {
        if (g_LocalSteamId == 0) {
            CMsgProtoBufHeader hdr;
            if (hdr.ParseFromArray(pHdr, cbHdr) && hdr.has_steamid() && hdr.steamid()) {
                g_LocalSteamId = hdr.steamid();
                LOG_RICHPRESENCE_DEBUG("Captured local SteamID 0x{:X}", g_LocalSteamId);
            }
        }

        // Steam stacks running games in games_played; the banner follows
        // the tail (most recently launched).  Mirror that — only the
        // tail's appid drives our inject.
        AppId_t topmost = 0;
        if (msg.games_played_size() > 0) {
            topmost = static_cast<AppId_t>(
                msg.games_played(msg.games_played_size() - 1).game_id() & UINT32_MAX);
        }

        // Only track when the topmost is an unlocked AppId we can inject for.
        // Owned games on top let the server's natural broadcast paint the
        // cache; -onlinefix games are already handled by the OnlineFix path.
        AppId_t newTracked = 0;
        if (topmost != 0 && topmost != kOnlineFixAppId && LuaConfig::HasDepot(topmost))
            newTracked = topmost;

        if (g_PlayingAppId == newTracked) return;
        g_PlayingAppId = newTracked;

        if (newTracked != 0) {
            LOG_RICHPRESENCE_INFO("Tracking topmost appid {}", newTracked);
            if (BuildInject(newTracked)) g_InjectPending = true;
        } else if (topmost == 0) {
            // Stack went empty — inject a clear so the cache reverts.
            LOG_RICHPRESENCE_DEBUG("GamesPlayed empty, scheduling cache clear");
            if (BuildInject(0)) g_InjectPending = true;
        } else {
            // Topmost is owned (or -onlinefix); let the server's broadcast
            // paint it.  Skipping the clear-inject here avoids a brief
            // "Online" flicker between our drop and the server's push.
            LOG_RICHPRESENCE_DEBUG("Topmost is appid {} (owned or onlinefix); deferring to server", topmost);
        }
    }

    // Cache real self-pushes as the inject template; if we are tracking
    // an unowned game, patch the live message in place so a periodic
    // refresh does not overwrite the injected game info.
    bool HandleRecv(const uint8* pBody, uint32 cbBody, const uint8* pHdr, uint32 cbHdr)
    {
        CMsgClientPersonaState msg;
        if (!msg.ParseFromArray(pBody, cbBody)) return false;

        CMsgClientPersonaState::Friend* selfEntry = nullptr;
        for (int i = 0; i < msg.friends_size(); ++i) {
            auto* f = msg.mutable_friends(i);
            if (f->has_friendid() && f->friendid() == g_LocalSteamId) {
                selfEntry = f;
                break;
            }
        }
        if (!selfEntry) return false;

        LOG_RICHPRESENCE_DEBUG(
            "Recv self PersonaState: status_flags=0x{:X} friends_size={}",
            msg.status_flags(), msg.friends_size());

        if (cbHdr <= sizeof(g_SelfHdr) && cbBody <= sizeof(g_SelfBody)) {
            memcpy(g_SelfHdr,  pHdr,  cbHdr);
            memcpy(g_SelfBody, pBody, cbBody);
            g_cbSelfHdr      = cbHdr;
            g_cbSelfBody     = cbBody;
            g_HaveSelfCached = true;
        }

        if (g_PlayingAppId == 0) return false;

        ApplyGameFields(msg, selfEntry, g_PlayingAppId);
        g_cbNewBody = static_cast<uint32>(msg.ByteSizeLong());
        if (g_cbNewBody > kMaxBodySize) {
            LOG_RICHPRESENCE_WARN("In-place patch too large ({} bytes)", g_cbNewBody);
            return false;
        }
        if (!msg.SerializeToArray(g_NewBody, kMaxBodySize)) {
            LOG_RICHPRESENCE_WARN("In-place patch SerializeToArray failed");
            return false;
        }
        LOG_RICHPRESENCE_INFO("Patched live self push with appid {}", g_PlayingAppId);
        return true;
    }

    // Deliver the pending manufactured PersonaState by borrowing the
    // carrier's data pointer for one oRecvPkt call, then restore.
    void TryInject(void* pThis, CNetPacket* pCarrier,
                   bool (*invokeOriginal)(void*, CNetPacket*))
    {
        if (!g_InjectPending || g_cbInjectPkt == 0) return;
        g_InjectPending = false;

        uint8* origData = pCarrier->m_pubData;
        uint32 origSize = pCarrier->m_cubData;
        pCarrier->m_pubData = g_InjectPkt;
        pCarrier->m_cubData = g_cbInjectPkt;
        invokeOriginal(pThis, pCarrier);
        pCarrier->m_pubData = origData;
        pCarrier->m_cubData = origSize;
        LOG_RICHPRESENCE_INFO("Delivered manufactured self-PersonaState ({} bytes)", g_cbInjectPkt);
    }

} // namespace Hooks_NetPacket_RichPresence


// ════════════════════════════════════════════════════════════════
//  Hooks_NetPacket_OnlineFix
//
//  Outgoing: CMsgClientGamesPlayed (eMsg 742 / 5410)
//
//  When a game launched with -onlinefix reports appid 480, replace
//  game_extra_info with the real game's localized name so friends
//  see the correct title.
// ════════════════════════════════════════════════════════════════
namespace Hooks_NetPacket_OnlineFix {

    bool HandleSend(const uint8* pBody, uint32 cbBody,
                    const uint8* pHdr, uint32 cbHdr)
    {
        CMsgClientGamesPlayed msg;
        if (!msg.ParseFromArray(pBody, cbBody)) {
            LOG_ONLINEFIX_WARN("OnlineFix: failed to parse CMsgClientGamesPlayed");
            return false;
        }
        LOG_ONLINEFIX_DEBUG("OnlineFix: original body:\n{}", msg.DebugString());

        Hooks_NetPacket_RichPresence::TrackSend(msg, pHdr, cbHdr);

        bool patched = false;
        for (int i = 0; i < msg.games_played_size(); ++i) {
            auto* game = msg.mutable_games_played(i);
            AppId_t appid = static_cast<AppId_t>(game->game_id() & UINT32_MAX);

            // SpawnProcess rewrites pGameID to 480, so game_id is already 480.
            // Fill game_extra_info with the real game name.
            if (appid == kOnlineFixAppId) {
                AppId_t realAppId = Hooks_Misc::ResolveAppId();
                if (realAppId && LuaConfig::HasDepot(realAppId)) {
                    std::string name = Hooks_Misc::GetGameNameByAppID(realAppId);
                    if (!name.empty()) {
                        game->set_game_extra_info(name);
                        patched = true;
                        LOG_ONLINEFIX_INFO("OnlineFix: 480 -> name '{}' (real appid {})",
                            name, realAppId);
                    }
                }
            }
        }

        if (!patched) return false;

        g_cbSendNewBody = static_cast<uint32>(msg.ByteSizeLong());
        if (g_cbSendNewBody > kMaxBodySize) {
            LOG_ONLINEFIX_WARN("OnlineFix: encoded size {} exceeds buffer", g_cbSendNewBody);
            return false;
        }
        if (!msg.SerializeToArray(g_SendNewBody, kMaxBodySize)) {
            LOG_ONLINEFIX_WARN("OnlineFix: failed to SerializeToArray");
            return false;
        }

        LOG_ONLINEFIX_DEBUG("OnlineFix: modified body:\n{}", msg.DebugString());
        return true;
    }

} // namespace Hooks_NetPacket_OnlineFix


// ════════════════════════════════════════════════════════════════
//  Dispatch
// ════════════════════════════════════════════════════════════════
namespace {

    bool SendServiceJob(const char* targetJobName,
                        const uint8* pBody, uint32 cbBody,
                        const uint8* pHdr, uint32 cbHdr)
    {
        LOG_NETPACKET_DEBUG("Send target_job_name: {}", targetJobName);
        switch (Fnv1aHash(targetJobName)) {

        case HASH_JOB_GetUserStats:
            return Hooks_NetPacket_UserStats::HandleSend_GetUserStats(pBody, cbBody, pHdr, cbHdr);

        case HASH_JOB_GetManifestRequestCode:
            return Hooks_NetPacket_Manifest::HandleSend(pBody, cbBody, pHdr, cbHdr);

        // ---- add new 151 service methods here ----
        }
        return false;
    }

    void SendJob(EMsg eMsg, const uint8* pBody, uint32 cbBody,
                 const uint8* pHdr, uint32 cbHdr)
    {
        g_NeedReplaceSend = false;

        LOG_NETPACKET_DEBUG("Send eMsg {}({}) (cbBody={}, cbHdr={})",
                        MsgName(eMsg), static_cast<uint32>(eMsg), cbBody, cbHdr);

        switch (eMsg) {

        case k_EMsgServiceMethodCallFromClient: {   // 151
            CMsgProtoBufHeader hdr;
            if (hdr.ParseFromArray(pHdr, cbHdr) && hdr.has_target_job_name()) {
                g_NeedReplaceSend = SendServiceJob(hdr.target_job_name().c_str(), pBody, cbBody, pHdr, cbHdr);
            }
            return;
        }

        case k_EMsgClientPICSProductInfoRequest:     // 8903
            g_NeedReplaceSend = Hooks_NetPacket_AccessToken::HandleSend(pBody, cbBody);
            return;

        case k_EMsgClientGamesPlayed:                 // 742
        case k_EMsgClientGamesPlayedWithDataBlob:     // 5410
            g_NeedReplaceSend = Hooks_NetPacket_OnlineFix::HandleSend(pBody, cbBody, pHdr, cbHdr);
            return;

        case k_EMsgClientRichPresenceUpload:           // 7501
            Hooks_NetPacket_RichPresence::TrackRPSend(pBody, cbBody);
            return;

        case k_EMsgClientGetUserStats:               // 818
            g_NeedReplaceSend = Hooks_NetPacket_UserStats::HandleSend_ClientGetUserStats(pBody, cbBody);
            return;

        default:
            return;
        }
    }

    void RecvServiceJob(const char* targetJobName,
                        const uint8* pBody, uint32 cbBody,
                        const uint8* pHdr, uint32 cbHdr)
    {
        LOG_NETPACKET_DEBUG("Recv target_job_name: {}", targetJobName);
        g_NeedReplaceBody = false;
        g_NeedReplaceHdr  = false;

        switch (Fnv1aHash(targetJobName)) {

        case HASH_JOB_NotifyRunningApps:
            Hooks_NetPacket_FamilySharing::ClearBody(pBody, cbBody);
            return;

        case HASH_JOB_GetUserStats:
            Hooks_NetPacket_UserStats::HandleRecv_GetUserStatsResponse(pHdr, cbHdr, pBody, cbBody);
            return;

        case HASH_JOB_GetManifestRequestCode:
            Hooks_NetPacket_Manifest::HandleRecv(pBody, cbBody, pHdr, cbHdr);
            return;

        // ---- add new 147 service methods here ----
        }
    }

    void RecvJob(EMsg eMsg, const uint8* pBody, uint32 cbBody,
                 const uint8* pHdr, uint32 cbHdr)
    {
        g_NeedReplaceBody = false;
        g_NeedReplaceHdr  = false;

        if(eMsg == k_EMsgMulti) {
            LOG_NETPACKET_TRACE("Received k_EMsgMulti, skipping dispatch");
            return;
        }
        LOG_NETPACKET_DEBUG("Recv eMsg {}({}) (cbBody={}, cbHdr={})",
                        MsgName(eMsg), static_cast<uint32>(eMsg), cbBody, cbHdr);

        switch (eMsg) {

        case k_EMsgServiceMethodResponse: {     // 147
            CMsgProtoBufHeader hdr;
            if (hdr.ParseFromArray(pHdr, cbHdr) && hdr.has_target_job_name())
                RecvServiceJob(hdr.target_job_name().c_str(), pBody, cbBody, pHdr, cbHdr);
            return;
        }

        // migrated to IPC Layer Hooks_IPC_ISteamUser::GetEncryptedAppTicketResponse
        // case k_EMsgClientRequestEncryptedAppTicketResponse:     // 5527
        //     Hooks_NetPacket_ETicket::HandleEncryptedAppTicketResponse(pBody, cbBody);
        //     return;

        case k_EMsgClientGetUserStatsResponse:     // 819
            g_NeedReplaceBody = Hooks_NetPacket_UserStats::HandleRecv_ClientGetUserStatsResponse(
                pBody, cbBody);
            return;

        case k_EMsgClientSharedLibraryStopPlaying:     // 9406
            Hooks_NetPacket_FamilySharing::ClearBody(pBody, cbBody);
            return;

        case k_EMsgClientPersonaState:                 // 766
            g_NeedReplaceBody = Hooks_NetPacket_RichPresence::HandleRecv(pBody, cbBody, pHdr, cbHdr);
            return;

        default:
            return;
        }
    }

    // ════════════════════════════════════════════════════════════
    //  Hooks
    // ════════════════════════════════════════════════════════════

    HOOK_FUNC(BBuildAndAsyncSendFrame, bool,
              void* pObject, EWebSocketOpCode eWebSocketOpCode,
              uint8* pubData, uint32 cubData)
    {
        if (eWebSocketOpCode != k_eWebSocketOpCode_Binary)
            return oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, pubData, cubData);

        EMsg eMsg;
        const uint8 *pHdr, *pBody;
        uint32 cbHdr, cbBody;
        bool result;
        if (UnpackRaw(pubData, cubData, eMsg, pHdr, cbHdr, pBody, cbBody)) {
            SendJob(eMsg, pBody, cbBody, pHdr, cbHdr);

            if (g_NeedReplaceSend) {
                uint32 newSize = 0;
                uint8* buf = ReplaceSendPacket(pubData, cbHdr, pHdr,
                                               g_SendNewBody, g_cbSendNewBody, &newSize);
                result = buf
                    ? oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, buf, newSize)
                    : oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, pubData, cubData);
            } else {
                result = oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, pubData, cubData);
            }
        } else {
            result = oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, pubData, cubData);
        }

        return result;
    }

    HOOK_FUNC(RecvPkt, void*, void* pThis, CNetPacket* pPacket)
    {
        Hooks_NetPacket_RichPresence::TryInject(
            pThis, pPacket,
            [](void* pT, CNetPacket* pP) -> bool { return oRecvPkt(pT, pP) != nullptr; });

        EMsg eMsg;
        const uint8 *pBody, *pHdr;
        uint32 cbBody, cbHdr;
        if (UnpackRaw(pPacket->m_pubData, pPacket->m_cubData,
                     eMsg, pHdr, cbHdr, pBody, cbBody)) {
            g_ResizedInPlace = false;
            RecvJob(eMsg, pBody, cbBody, pHdr, cbHdr);

            if (g_ResizedInPlace && g_NeedReplaceHdr) {
                // Body shrunk in-place + header changed -> full replace via pool
                ReplaceRecvPacket(pPacket,
                    g_NewHdr, g_cbNewHdr,
                    pBody, g_NewBodySize);
            } else if (g_ResizedInPlace) {
                pPacket->m_cubData = sizeof(MsgHdr) + cbHdr + g_NewBodySize;
            } else if (g_NeedReplaceHdr || g_NeedReplaceBody) {
                ReplaceRecvPacket(pPacket,
                    g_NeedReplaceHdr  ? g_NewHdr  : pHdr,
                    g_NeedReplaceHdr  ? g_cbNewHdr : cbHdr,
                    g_NeedReplaceBody ? g_NewBody : pBody,
                    g_NeedReplaceBody ? g_cbNewBody : cbBody);
            }
        }

        return oRecvPkt(pThis, pPacket);
    }

} // anonymous namespace


namespace Hooks_NetPacket {
    void Install() {
        RESOLVE_C(PchMsgNameFromEMsg);
        HOOK_BEGIN();
        INSTALL_HOOK_C(BBuildAndAsyncSendFrame);
        INSTALL_HOOK_C(RecvPkt);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(BBuildAndAsyncSendFrame);
        UNINSTALL_HOOK(RecvPkt);
        UNHOOK_END();
    }
}
