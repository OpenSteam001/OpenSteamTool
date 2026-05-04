#pragma once

// Layout of the in-memory structures we manipulate from hooks. Most of
// these mirror Valve internal classes (CUtlVector, PackageInfo, etc.) and
// are kept in lockstep with steamclient(64).dll.

#include "Types.h"
#include "Enums.h"

#include <string>

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
		return "m_Get:" + std::to_string(m_Get) + " , m_Put:" + std::to_string(m_Put) + " , m_nOffset:" + std::to_string(m_nOffset)+ " , m_flags:" + std::to_string(m_flags);
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
	int32 foo;
	EGameIDType GameIDType;
	int32 bar;
	int32 baz;

};

struct CSteamApp{
	void** vfptr;
	int32 StateFlags;
	AppId_t AppID;
	// ...
};

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