#include "Hooks_NetPacket.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/AppTicket.h"
#include "Utils/Hash.h"
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

    // ── Incoming (RecvMultiPkt) packet pool ─────────────────────
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
    using PchMsgNameFromEMsg_t = char*(*)(EMsg);
    PchMsgNameFromEMsg_t oPchMsgNameFromEMsg = nullptr;

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

        case k_EMsgClientRequestEncryptedAppTicketResponse:     // 5527
            Hooks_NetPacket_ETicket::HandleEncryptedAppTicketResponse(pBody, cbBody);
            return;

        case k_EMsgClientGetUserStatsResponse:     // 819
            g_NeedReplaceBody = Hooks_NetPacket_UserStats::HandleRecv_ClientGetUserStatsResponse(
                pBody, cbBody);
            return;

        case k_EMsgClientSharedLibraryStopPlaying:     // 9406
            Hooks_NetPacket_FamilySharing::ClearBody(pBody, cbBody);
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
        if (UnpackRaw(pubData, cubData, eMsg, pHdr, cbHdr, pBody, cbBody)) {
            SendJob(eMsg, pBody, cbBody, pHdr, cbHdr);

            if (g_NeedReplaceSend) {
                uint32 newSize = 0;
                uint8* buf = ReplaceSendPacket(pubData, cbHdr, pHdr,
                                               g_SendNewBody, g_cbSendNewBody, &newSize);
                if (buf)
                    return oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, buf, newSize);
            }
        }
        return oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, pubData, cubData);
    }

    HOOK_FUNC(RecvMultiPkt, void*, void* pThis, CNetPacket* pPacket)
    {
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

        return oRecvMultiPkt(pThis, pPacket);
    }

} // anonymous namespace


namespace Hooks_NetPacket {
    void Install() {
        RESOLVE_D(PchMsgNameFromEMsg);
        HOOK_BEGIN();
        INSTALL_HOOK_D(BBuildAndAsyncSendFrame);
        INSTALL_HOOK_D(RecvMultiPkt);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(BBuildAndAsyncSendFrame);
        UNINSTALL_HOOK(RecvMultiPkt);
        UNHOOK_END();
        oPchMsgNameFromEMsg = nullptr;
    }
}
