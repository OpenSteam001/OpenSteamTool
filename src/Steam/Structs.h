#pragma once

// Layout of the in-memory structures we manipulate from hooks. Most of
// these mirror Valve internal classes (CUtlVector, PackageInfo, etc.) and
// are kept in lockstep with steamclient(64).dll.

#include "Types.h"
#include "Enums.h"


#include <string>
#include <format>

template<typename T>
struct CUtlMemory{
	T* m_pMemory;
	uint32 m_nAllocationCount;
	uint32 m_nGrowSize;
};

template<typename T>
struct CUtlVector {
	CUtlMemory<T> m_Memory;
	uint32 m_Size;

	// Swap with last element and shrink. Does not preserve order.
	void FastRemove(uint32 elem) {
		if (elem < m_Size) {
			if (elem != m_Size - 1)
				m_Memory.m_pMemory[elem] = m_Memory.m_pMemory[m_Size - 1];
			--m_Size;
		}
	}

	bool FindAndFastRemove(const T& src) {
		for (uint32 i = 0; i < m_Size; ++i) {
			if (m_Memory.m_pMemory[i] == src) {
				FastRemove(i);
				return true;
			}
		}
		return false;
	}
};

struct CUtlBuffer{
	CUtlMemory<uint8> m_Memory;
	int32 m_Get;
	int32 m_Put;
	int32 m_nOffset;
	int32 m_flags;

	typedef bool (CUtlBuffer::*UtlBufferOverflowFunc_t)( int32 nSize );
	UtlBufferOverflowFunc_t m_GetOverflowFunc;
	UtlBufferOverflowFunc_t m_PutOverflowFunc;

	// Direct base/size helpers — match the names the original Valve
	// CUtlBuffer exposes so call sites read naturally.
	uint8* Base()             { return m_Memory.m_pMemory; }
	const uint8* Base() const { return m_Memory.m_pMemory; }
	int32 TellPut() const     { return m_Put; }
	int32 TellGet() const     { return m_Get; }
	// Debug helper
	std::string DebugString() const{
      return std::format("m_Memory:0x{:X} m_AllocCnt:{} m_Grow:{} m_Get:{} m_Put:{} m_nOffset:{} m_flags:{}",
          reinterpret_cast<uintptr_t>(m_Memory.m_pMemory),
          m_Memory.m_nAllocationCount, m_Memory.m_nGrowSize,
          m_Get, m_Put, m_nOffset, m_flags);
  }
};

struct PackageInfo
{
	AppId_t PackageId;
	int32 ChangeNumber;
	uint64 PICS_token;
	BillingType BillingType;
	ELicenseType LicenseType;
	EPackageStatus Status;
	byte SHA_1_Hash[20];
	void* pPackageInfoNodeBegin;
	void* pExtendNodeBegin;

	CUtlVector<AppId_t> AppIdVec;
	CUtlVector<AppId_t> DepotIdVec;
};

struct AppOwnership
{
	PackageId_t PackageId;
	EAppReleaseState ReleaseState; 
	AccountID_t SteamId32; 
	AppId_t MasterSubscriptionAppID; 
	uint32 TrialSeconds; 
	uint32 ExistInPackageNums; 
	char PurchaseCountryCode[4];
	uint32 TimeStamp;
	uint32 TimeExpire;
	bool bOwnsLicense;
	bool bLicenseExpired;
	bool bIsPermanent;
	bool bLowViolence;
	bool bFreeLicense;
	bool bRegionRestricted;
	bool bFromFreeWeekend;
	bool bLicenseLocked;
	bool bLicensePending;
	bool bRetailLicense;
	bool bAutoGrant;
	bool bLicensePermanent;
	bool bGuestPass;
	bool bBorrowed;
	bool bAnySiteLicense;
	bool bAllSiteLicenses;
	bool bAllActivationRequired;
	bool bFamilyShared;

	std::string DebugString() const {
		return std::format("PackageId={} ReleaseState={} SteamId32={} MasterSubscriptionAppID={} TrialSeconds={} ExistInPackageNums={} \
			CountryCode={} TimeStamp={} TimeExpire={} OwnsLicense={} LicenseExpired={} IsPermanent={} LowViolence={} \
			FreeLicense={} RegionRestricted={} FromFreeWeekend={} LicenseLocked={} LicensePending={} RetailLicense={} \
			AutoGrant={} LicensePermanent={} GuestPass={} Borrowed={} AnySiteLicense={} AllSiteLicenses={} AllActivationRequired={} FamilyShared={}",
			PackageId, static_cast<int>(ReleaseState), SteamId32, MasterSubscriptionAppID, TrialSeconds, ExistInPackageNums, PurchaseCountryCode, TimeStamp, TimeExpire,
			bOwnsLicense, bLicenseExpired, bIsPermanent, bLowViolence, bFreeLicense, bRegionRestricted, bFromFreeWeekend, bLicenseLocked, bLicensePending,
			bRetailLicense, bAutoGrant, bLicensePermanent, bGuestPass, bBorrowed, bAnySiteLicense, bAllSiteLicenses, bAllActivationRequired, bFamilyShared);
	}
};

// Single depot manifest entry (0x20 bytes) produced by BuildDepotDependency.
struct DepotEntry
{
	uint32  DepotId;        // 0x00
	uint32  AppId;          // 0x04
	uint64  ManifestGid;    // 0x08 — from depots/<id>/manifests/<branch>/gid
	uint64  ManifestSize;   // 0x10 — from depots/<id>/manifests/<branch>/size
	uint32  DlcAppId;       // 0x18 — associated DLC AppID, 0 if none
	uint8   LcsRequired;    // 0x1C — branches/<branch>/lcsrequired
	uint8   bNotNewTarget;  // 0x1D — carried over from active list (not newly activated this call)
	uint8   SharedInstall;  // 0x1E — sharedinstall / depotfromapp redirect
	uint8   Padding;        // 0x1F
};
static_assert(sizeof(DepotEntry) == 0x20, "DepotEntry must be 32 bytes");

struct KeyValues
{
	union                                   // +0x00 (8B) — value storage OR first child
	{
		KeyValues*      m_pSub;             // TYPE_NONE  → pointer to first child
		char*           m_sValue;           // TYPE_STRING
		wchar_t*        m_wsValue;          // TYPE_WSTRING
		int             m_iValue;           // TYPE_INT
		float           m_flValue;          // TYPE_FLOAT
		void*           m_pValue;           // TYPE_PTR
		uint64			m_ullValue;         // TYPE_UINT64
		int64           m_llValue;          // TYPE_INT64
		byte   			m_Color[8];         // TYPE_COLOR
	};

	KeyValues*          m_pChain;           // +0x08 (8B) — chained KeyValues for fallback lookup

	// +0x10 (4B) — packed bitfield
	//   bit[0:24]  m_iKeyName              (25 bits)  key symbol assigned by KeyValuesSystem
	//   bit[25:28] m_iDataType             (4 bits)   value type enum (see EKeyValuesType)
	//   bit[29]    m_bHasEscapeSequences              escape sequences enabled during parse
	//   bit[30]    m_bEvaluateConditionals            conditional blocks evaluated during parse
	//   bit[31]    m_bAllocatedValue                  value allocated on heap (must deref offset 0)
	union
	{
		struct
		{
			unsigned int m_iKeyName              : 25;
			unsigned int m_iDataType             : 4;
			unsigned int m_bHasEscapeSequences   : 1;
			unsigned int m_bEvaluateConditionals : 1;
			unsigned int m_bAllocatedValue       : 1;
		};
		unsigned int m_iPackedKeyAndType;    // raw DWORD access
	};

	unsigned int        m_unFlags;          // +0x14 (4B) — additional flags

	KeyValues*          m_pPeer;            // +0x18 (8B) — next sibling in linked list

};
static_assert(sizeof(KeyValues) == 0x20, "KeyValues must be 32 bytes");

// ============================================================
// IKeyValuesSystem
//   Exported as "KeyValuesSystemSteam" (ord 103) from vstdlib_s64.dll
// ============================================================
struct IKeyValuesSystem {
	// vtable[0] (+0x00) — records max KeyValues size for memory pooling
	virtual void        RegisterSizeofKeyValues(int size) = 0;

	// vtable[1] (+0x08) — hash string → return symbol (create if not found)
	//   Symbol = (pool_index << 15) | byte_offset_within_pool
	//   Returns 0 if name is null/empty and bCreate=false
	virtual int         GetSymbolForString(const char* name, bool bCreate) = 0;

	// vtable[2] (+0x10) — O(1) reverse lookup: symbol → string pointer
	//   Returns "" (empty string) for invalid/zero symbols
	virtual const char* GetStringForSymbol(int symbol) = 0;

	// vtable[3..11] — Alloc/Free memory, leak tracking, file cache, etc.
	//   (not needed for depot override, omitted)

	// ── helpers ──
	int  GetSymbol(const char* name)        { return GetSymbolForString(name, false); }
	int  GetOrCreateSymbol(const char* name) { return GetSymbolForString(name, true); }
	const char* GetKeyName(int symbol)       { return GetStringForSymbol(symbol); }
};
using KeyValuesSystemSteam_t = IKeyValuesSystem* (*)();

struct CNetPacket
{
	HCONNECTION m_hConnection;
	uint8* m_pubData;
	uint32 m_cubData;
	int32 m_cRef;
	uint8* m_pubNetworkBuffer;
	CNetPacket* m_pNext;
};

struct MsgHdr
{
	EMsg eMsg;               // actual_eMsg | 0x80000000
	uint32 headerLength;
};

// High bit of eMsg discriminates protobuf vs extended header.
constexpr uint32 kMsgHdrProtoFlag = 0x80000000;

#pragma pack(push,1)
struct ExtendedMsgHdr
{
	EMsg eMsg;
	uint8 m_nCubHdr;
	uint16 m_nHdrVersion;
	JobID_t m_JobIDTarget;
	JobID_t m_JobIDSource;
	uint8 m_nHdrCanary;
	uint64 m_ulSteamID;
	int32 m_nSessionID;
};
#pragma pack(pop)

// ── CSteamPipeClient ────────────────────────────────────────────
struct CSteamPipeClient {
    void*    m_pServer;         // +0
    void*    m_pClient;         // +8
    uint32   m_hSteamPipe;      // +16
    uint8    _pad0[12];         // +20
    uint32   m_clientPID;       // +32
    uint8    _pad1[4];          // +36
    char*    m_szProcessName;   // +40
    uint8    _pad2[80];         // +48
    int32    m_nActiveRefs;     // +128
    uint8    _pad3[188];        // +132
    void*    m_pLocalIPCServer; // +320
    bool     m_bInProcess;      // +328
    uint8    _pad4[31];         // +329

	std::string DebugString() const {
		return std::format("pipe=0x{:08X} pid={} proc={} ",
			m_hSteamPipe, m_clientPID, m_szProcessName ? m_szProcessName : "?");
	}
};

struct CGameID{
	enum EGameIDType
	{
		k_EGameIDTypeApp		= 0,
		k_EGameIDTypeGameMod	= 1,
		k_EGameIDTypeShortcut	= 2,
		k_EGameIDTypeP2P		= 3,
	};
	
	bool IsSteamApp() const
	{
		return ( m_gameID.m_nType == k_EGameIDTypeApp );
	}

	AppId_t AppID(bool checkType = false) const
	{
		if(checkType && !IsSteamApp())
			return 0;
		return m_gameID.m_nAppID;
	}

	void SetAppID(AppId_t nAppID)
	{
		m_gameID.m_nAppID = nAppID;
	}

	struct GameID_t
	{
		unsigned int m_nAppID : 24;
		unsigned int m_nType : 8;
		unsigned int m_nModID : 32;
	};

	union
	{
		uint64 m_ulGameID;
		GameID_t m_gameID;
	};
};

struct CAppData
{
	void** fptr;
	AppId_t nAppID;
	uint32 ChangeNumber;
	uint32 LastChangeTimeStamp;
	bool bSkipFlag;
	bool bDeniedToken;
	bool bMissingToken;
	uint64 accessToken;
	uint8 sha1Hash[20];

	bool HasEmptyAppInfoSha() const {
		static constexpr uint8 kEmptySha1[sizeof(sha1Hash)] = {};
		return memcmp(sha1Hash, kEmptySha1, sizeof(sha1Hash)) == 0;
	}

	bool IsUnresolvedAppInfo() const {
		return HasEmptyAppInfoSha() && !bSkipFlag;
	}

	std::string DebugString() const {
		return std::format("AppID={} ChangeNumber={} LastChangeTimeStamp={} SkipFlag={} DeniedToken={} MissingToken={} AccessToken={}",
			nAppID, ChangeNumber, LastChangeTimeStamp, bSkipFlag, bDeniedToken, bMissingToken, accessToken);
	}
};

struct CSteamApp
{
	void** fptr;
	CGameID GameID;
	AppId_t nAppID;
	uint16 _unknown1;
	uint16 _unknown2;
	EAppReleaseState ReleaseState;
	EAppOwnershipFlags OwnershipFlags;
	uint32 _unknown3;
	uint64 SteamID;
	uint32 PurchasedTime;
};