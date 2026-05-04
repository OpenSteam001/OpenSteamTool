#pragma once

// Primitive typedefs and ID handles used throughout steamclient internals.

#include <cstdint>

#include <cstdint>

typedef unsigned char byte;
typedef int32_t int32;
typedef int64_t int64;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef uint32 AppId_t;
typedef uint32 PackageId_t;
typedef uint32 PID_t;
typedef uint32 AccountID_t;
typedef uint32 HAuthTicket;
typedef uint32 HCONNECTION;

typedef int32 HSteamPipe;
typedef int32 HSteamUser;

typedef uint64 GID_t;
typedef GID_t JobID_t;

constexpr AppId_t k_uAppIdInvalid = 0x0;
constexpr PackageId_t k_uPackageIdFreeSub = 0x0;
constexpr PackageId_t k_uPackageIdInvalid = 0xFFFFFFFF;
constexpr PackageId_t k_uPackageIdWallet = -2;
constexpr PackageId_t k_uPackageIdMicroTxn = -3;
constexpr GID_t k_GIDNil = 0xffffffffffffffffull;