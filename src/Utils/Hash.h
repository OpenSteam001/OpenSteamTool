#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

// ---- compile-time FNV-1a hash (32-bit) ----
constexpr uint32_t Fnv1aHash(const char* str)
{
    uint32_t hash = 0x811c9dc5;
    while (*str) {
        hash ^= static_cast<uint32_t>(*str++);
        hash *= 0x01000193;
    }
    return hash;
}

// ---- SHA-256 of an entire file (BCrypt) ----
// Returns lowercase hex string (64 chars), or empty string on any failure.
inline std::string Sha256OfFile(const std::string& path)
{
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        CloseHandle(hFile);
        return {};
    }

    DWORD cbData = 0;

    DWORD hashObjSize = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                      reinterpret_cast<PUCHAR>(&hashObjSize),
                      sizeof(DWORD), &cbData, 0);
    std::vector<uint8_t> hashObj(hashObjSize);

    DWORD hashSize = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                      reinterpret_cast<PUCHAR>(&hashSize),
                      sizeof(DWORD), &cbData, 0);
    std::vector<uint8_t> hashBuf(hashSize);

    BCRYPT_HASH_HANDLE hHash = nullptr;
    BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);

    // Stream in 64 KB chunks
    constexpr DWORD kChunk = 65536;
    std::vector<uint8_t> buf(kChunk);
    DWORD bytesRead = 0;
    while (ReadFile(hFile, buf.data(), kChunk, &bytesRead, nullptr) && bytesRead > 0)
        BCryptHashData(hHash, buf.data(), bytesRead, 0);

    BCryptFinishHash(hHash, hashBuf.data(), hashSize, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    CloseHandle(hFile);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(hashSize * 2);
    for (uint8_t b : hashBuf) {
        result += kHex[b >> 4];
        result += kHex[b & 0xF];
    }
    return result;
}
