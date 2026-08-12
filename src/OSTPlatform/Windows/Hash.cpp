#include "include/Hash.h"

#include "include/Log.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace OSTPlatform::Hash {

namespace {

uint32_t StatusCode(NTSTATUS status) {
    return static_cast<uint32_t>(status);
}

std::string HexEncode(const std::vector<uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        result += kHex[b >> 4];
        result += kHex[b & 0xF];
    }
    return result;
}

} // namespace

std::string Sha256OfFile(const std::filesystem::path& path) {
    HANDLE hFile = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        OSTP_LOG_WARN("Sha256OfFile: CreateFileW failed for '{}' (error={})",
                      path.string(), GetLastError());
        return {};
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        OSTP_LOG_WARN("Sha256OfFile: BCryptOpenAlgorithmProvider failed (status=0x{:08X})",
                      StatusCode(status));
        CloseHandle(hFile);
        return {};
    }

    BCRYPT_HASH_HANDLE hHash = nullptr;
    auto cleanup = [&] {
        if (hHash) BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        CloseHandle(hFile);
    };

    DWORD cbData = 0;
    DWORD hashObjSize = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjSize),
                               sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status) || hashObjSize == 0) {
        OSTP_LOG_WARN("Sha256OfFile: BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed (status=0x{:08X}, size={})",
                      StatusCode(status), hashObjSize);
        cleanup();
        return {};
    }
    std::vector<uint8_t> hashObj(hashObjSize);

    DWORD hashSize = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize),
                               sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status) || hashSize == 0) {
        OSTP_LOG_WARN("Sha256OfFile: BCryptGetProperty(BCRYPT_HASH_LENGTH) failed (status=0x{:08X}, size={})",
                      StatusCode(status), hashSize);
        cleanup();
        return {};
    }
    std::vector<uint8_t> hashBuf(hashSize);

    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        OSTP_LOG_WARN("Sha256OfFile: BCryptCreateHash failed (status=0x{:08X})", StatusCode(status));
        cleanup();
        return {};
    }

    constexpr DWORD kChunk = 65536;
    std::vector<uint8_t> buf(kChunk);
    for (;;) {
        DWORD bytesRead = 0;
        // A read failure must not be mistaken for EOF: hashing partial content
        // would yield a valid-looking but wrong digest.
        if (!ReadFile(hFile, buf.data(), kChunk, &bytesRead, nullptr)) {
            OSTP_LOG_WARN("Sha256OfFile: ReadFile failed for '{}' (error={})",
                          path.string(), GetLastError());
            cleanup();
            return {};
        }
        if (bytesRead == 0) break;
        status = BCryptHashData(hHash, buf.data(), bytesRead, 0);
        if (!BCRYPT_SUCCESS(status)) {
            OSTP_LOG_WARN("Sha256OfFile: BCryptHashData failed (status=0x{:08X})", StatusCode(status));
            cleanup();
            return {};
        }
    }

    status = BCryptFinishHash(hHash, hashBuf.data(), hashSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        OSTP_LOG_WARN("Sha256OfFile: BCryptFinishHash failed (status=0x{:08X})", StatusCode(status));
        cleanup();
        return {};
    }
    cleanup();

    return HexEncode(hashBuf);
}

std::string Sha256OfBuffer(const void* data, size_t size) {
    if (!data && size != 0) {
        OSTP_LOG_WARN("Sha256OfBuffer: null data with non-zero size ({})", size);
        return {};
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        OSTP_LOG_WARN("Sha256OfBuffer: BCryptOpenAlgorithmProvider failed (status=0x{:08X})",
                      StatusCode(status));
        return {};
    }

    BCRYPT_HASH_HANDLE hHash = nullptr;
    auto cleanup = [&] {
        if (hHash) BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
    };

    DWORD cbData = 0;
    DWORD hashObjSize = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjSize),
                               sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status) || hashObjSize == 0) {
        OSTP_LOG_WARN("Sha256OfBuffer: BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed (status=0x{:08X})",
                      StatusCode(status));
        cleanup();
        return {};
    }
    std::vector<uint8_t> hashObj(hashObjSize);

    DWORD hashSize = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize),
                               sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status) || hashSize == 0) {
        OSTP_LOG_WARN("Sha256OfBuffer: BCryptGetProperty(BCRYPT_HASH_LENGTH) failed (status=0x{:08X})",
                      StatusCode(status));
        cleanup();
        return {};
    }
    std::vector<uint8_t> hashBuf(hashSize);

    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        OSTP_LOG_WARN("Sha256OfBuffer: BCryptCreateHash failed (status=0x{:08X})", StatusCode(status));
        cleanup();
        return {};
    }

    // BCryptHashData takes a ULONG length; feed the buffer in bounded chunks so a
    // >4 GiB input can never truncate the count (also keeps parity with the file path).
    const auto* cursor = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    while (remaining != 0) {
        const ULONG chunk = static_cast<ULONG>(std::min<size_t>(remaining, 0x40000000));
        status = BCryptHashData(hHash, const_cast<PUCHAR>(cursor), chunk, 0);
        if (!BCRYPT_SUCCESS(status)) {
            OSTP_LOG_WARN("Sha256OfBuffer: BCryptHashData failed (status=0x{:08X})", StatusCode(status));
            cleanup();
            return {};
        }
        cursor    += chunk;
        remaining -= chunk;
    }

    status = BCryptFinishHash(hHash, hashBuf.data(), hashSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        OSTP_LOG_WARN("Sha256OfBuffer: BCryptFinishHash failed (status=0x{:08X})", StatusCode(status));
        cleanup();
        return {};
    }
    cleanup();

    return HexEncode(hashBuf);
}

} // namespace OSTPlatform::Hash
