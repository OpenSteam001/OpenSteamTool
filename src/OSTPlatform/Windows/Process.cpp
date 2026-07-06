#include "include/Process.h"

#include "include/Encoding.h"
#include "include/Log.h"

#include "Windows/Handles.h"
#include "Windows/NtAbi.h"

#include <tlhelp32.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace OSTPlatform::Process {
namespace {

namespace NtAbi = OSTPlatform::Windows::NtAbi;

constexpr size_t kMaxEnvironmentBytes = 1024 * 1024;

std::string NormalizePathForCompare(std::string path) {
    for (char& ch : path) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

template <typename Fn>
Fn GetNtDllProc(const char* name) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return nullptr;
    return reinterpret_cast<Fn>(GetProcAddress(ntdll, name));
}

NtAbi::NtQueryInformationProcessFn NtQueryInformationProcessProc() {
    return GetNtDllProc<NtAbi::NtQueryInformationProcessFn>("NtQueryInformationProcess");
}

NtAbi::NtQueryVirtualMemoryFn NtQueryVirtualMemoryProc() {
    return GetNtDllProc<NtAbi::NtQueryVirtualMemoryFn>("NtQueryVirtualMemory");
}

Windows::UniqueHandle OpenProcessHandle(uint32_t pid, DWORD access) {
    if (pid == 0) return {};
    Windows::UniqueHandle process(::OpenProcess(access, FALSE, pid));
    if (!process) {
        OSTP_LOG_DEBUG("OpenProcess(pid={}, access=0x{:X}) failed (error={})", pid, access, GetLastError());
    }
    return process;
}

bool TryReadProcessMemory(HANDLE process, const void* address, void* buffer, size_t size, size_t* bytesRead = nullptr) {
    SIZE_T read = 0;
    const bool ok = ::ReadProcessMemory(process, address, buffer, size, &read) != FALSE;
    if (!ok) {
        OSTP_LOG_TRACE("ReadProcessMemory(addr={:#x}, size={}) failed (read={}, error={})",
                       reinterpret_cast<uintptr_t>(address), size, static_cast<size_t>(read), GetLastError());
    }
    if (bytesRead) *bytesRead = static_cast<size_t>(read);
    return ok;
}

template <typename T>
std::optional<T> ReadRemoteValue(HANDLE process, const void* address) {
    T value{};
    size_t bytesRead = 0;
    if (!TryReadProcessMemory(process, address, &value, sizeof(value), &bytesRead) ||
        bytesRead != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

const void* AddOffset(const void* address, size_t offset) {
    return reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(address) + offset);
}

std::optional<PVOID> QueryNativePebAddress(HANDLE process) {
    const auto ntQueryInformationProcess = NtQueryInformationProcessProc();
    if (!ntQueryInformationProcess) return std::nullopt;

    NtAbi::ProcessBasicInformation info{};
    const NTSTATUS status = ntQueryInformationProcess(
        process,
        NtAbi::ProcessInfoClass::BasicInformation,
        &info,
        sizeof(info),
        nullptr);
    if (!NtAbi::NtSuccess(status) || !info.pebBaseAddress) return std::nullopt;

    return info.pebBaseAddress;
}

std::optional<ULONG_PTR> QueryWow64PebAddress(HANDLE process) {
    const auto ntQueryInformationProcess = NtQueryInformationProcessProc();
    if (!ntQueryInformationProcess) return std::nullopt;

    ULONG_PTR peb32 = 0;
    const NTSTATUS status = ntQueryInformationProcess(
        process,
        NtAbi::ProcessInfoClass::Wow64Information,
        &peb32,
        sizeof(peb32),
        nullptr);
    if (!NtAbi::NtSuccess(status) || peb32 == 0) return std::nullopt;
    return peb32;
}

std::optional<PVOID> QueryNativeEnvironmentAddress(HANDLE process) {
    const auto pebAddress = QueryNativePebAddress(process);
    if (!pebAddress) return std::nullopt;

    const auto processParameters = ReadRemoteValue<PVOID>(
        process,
        AddOffset(*pebAddress, offsetof(NtAbi::Peb, processParameters)));
    if (!processParameters || !*processParameters) return std::nullopt;

    const auto environment = ReadRemoteValue<PVOID>(
        process,
        AddOffset(*processParameters, offsetof(NtAbi::RtlUserProcessParameters, environment)));
    if (!environment || !*environment) return std::nullopt;

    return *environment;
}

std::optional<PVOID> QueryWow64EnvironmentAddress(HANDLE process) {
    const auto peb32 = QueryWow64PebAddress(process);
    if (!peb32) return std::nullopt;

    const auto processParameters32 = ReadRemoteValue<ULONG>(
        process,
        AddOffset(reinterpret_cast<const void*>(*peb32), offsetof(NtAbi::Peb32, processParameters)));
    if (!processParameters32 || *processParameters32 == 0) return std::nullopt;

    const auto environment32 = ReadRemoteValue<ULONG>(
        process,
        AddOffset(reinterpret_cast<const void*>(static_cast<uintptr_t>(*processParameters32)),
                  offsetof(NtAbi::RtlUserProcessParameters32, environment)));
    if (!environment32 || *environment32 == 0) return std::nullopt;

    return reinterpret_cast<PVOID>(static_cast<uintptr_t>(*environment32));
}

std::optional<std::wstring> ReadCommandLineNative(HANDLE process) {
    const auto pebAddress = QueryNativePebAddress(process);
    if (!pebAddress) return std::nullopt;

    const auto processParameters = ReadRemoteValue<PVOID>(
        process,
        AddOffset(*pebAddress, offsetof(NtAbi::Peb, processParameters)));
    if (!processParameters || !*processParameters) return std::nullopt;

    const auto commandLine = ReadRemoteValue<NtAbi::UnicodeString>(
        process,
        AddOffset(*processParameters, offsetof(NtAbi::RtlUserProcessParameters, commandLine)));
    if (!commandLine || !commandLine->buffer || commandLine->length == 0) return std::nullopt;

    const size_t chars = commandLine->length / sizeof(wchar_t);
    if (chars == 0 || chars > kMaxEnvironmentBytes / sizeof(wchar_t)) return std::nullopt;

    std::wstring value(chars, L'\0');
    size_t bytesRead = 0;
    if (!TryReadProcessMemory(process, commandLine->buffer, value.data(),
                              chars * sizeof(wchar_t), &bytesRead)) {
        return std::nullopt;
    }
    value.resize(bytesRead / sizeof(wchar_t));
    return value;
}

std::optional<std::wstring> ReadCommandLineWow64(HANDLE process) {
    const auto peb32 = QueryWow64PebAddress(process);
    if (!peb32) return std::nullopt;

    const auto processParameters32 = ReadRemoteValue<ULONG>(
        process,
        AddOffset(reinterpret_cast<const void*>(*peb32), offsetof(NtAbi::Peb32, processParameters)));
    if (!processParameters32 || *processParameters32 == 0) return std::nullopt;

    const auto commandLine = ReadRemoteValue<NtAbi::UnicodeString32>(
        process,
        AddOffset(reinterpret_cast<const void*>(static_cast<uintptr_t>(*processParameters32)),
                  offsetof(NtAbi::RtlUserProcessParameters32, commandLine)));
    if (!commandLine || commandLine->buffer == 0 || commandLine->length == 0) return std::nullopt;

    const size_t chars = commandLine->length / sizeof(wchar_t);
    if (chars == 0 || chars > kMaxEnvironmentBytes / sizeof(wchar_t)) return std::nullopt;

    std::wstring value(chars, L'\0');
    size_t bytesRead = 0;
    if (!TryReadProcessMemory(process,
                              reinterpret_cast<const void*>(static_cast<uintptr_t>(commandLine->buffer)),
                              value.data(), chars * sizeof(wchar_t), &bytesRead)) {
        return std::nullopt;
    }
    value.resize(bytesRead / sizeof(wchar_t));
    return value;
}

std::optional<size_t> QueryReadableRegionBytes(HANDLE process, PVOID address) {
    const auto ntQueryVirtualMemory = NtQueryVirtualMemoryProc();
    if (!ntQueryVirtualMemory) return std::nullopt;

    MEMORY_BASIC_INFORMATION info{};
    const NTSTATUS status = ntQueryVirtualMemory(
        process,
        address,
        NtAbi::MemoryInfoClass::BasicInformation,
        &info,
        sizeof(info),
        nullptr);
    if (!NtAbi::NtSuccess(status) || info.RegionSize == 0) return std::nullopt;

    const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t env = reinterpret_cast<uintptr_t>(address);
    if (env < base) return std::nullopt;

    const size_t offset = static_cast<size_t>(env - base);
    if (offset >= info.RegionSize) return std::nullopt;

    return (std::min)(info.RegionSize - offset, kMaxEnvironmentBytes);
}

std::optional<std::vector<wchar_t>> ReadEnvironmentAt(HANDLE process, PVOID environmentAddress) {
    const auto bytesToRead = QueryReadableRegionBytes(process, environmentAddress);
    if (!bytesToRead || *bytesToRead < sizeof(wchar_t) * 2) return std::nullopt;

    std::vector<wchar_t> environment(*bytesToRead / sizeof(wchar_t));
    size_t bytesRead = 0;
    if (!TryReadProcessMemory(
            process,
            environmentAddress,
            environment.data(),
            environment.size() * sizeof(wchar_t),
            &bytesRead)) {
        return std::nullopt;
    }

    environment.resize(bytesRead / sizeof(wchar_t));
    const auto end = std::adjacent_find(
        environment.begin(),
        environment.end(),
        [](wchar_t lhs, wchar_t rhs) { return lhs == L'\0' && rhs == L'\0'; });
    if (end == environment.end()) return std::nullopt;

    environment.erase(std::next(end, 2), environment.end());
    return environment;
}

std::optional<std::vector<wchar_t>> ReadEnvironmentBlock(HANDLE process) {
    if (!process) return std::nullopt;

    auto environmentAddress = QueryWow64EnvironmentAddress(process);
    if (!environmentAddress) {
        environmentAddress = QueryNativeEnvironmentAddress(process);
    }
    if (!environmentAddress) return std::nullopt;

    return ReadEnvironmentAt(process, *environmentAddress);
}

std::optional<std::string> FindEnvironmentVariable(const std::vector<wchar_t>& environment, std::wstring_view name) {
    if (name.empty()) return std::nullopt;

    size_t offset = 0;
    while (offset < environment.size() && environment[offset] != L'\0') {
        const wchar_t* entry = environment.data() + offset;
        const size_t entryLength = wcslen(entry);
        if (entryLength > name.size() &&
            entry[name.size()] == L'=' &&
            _wcsnicmp(entry, name.data(), name.size()) == 0) {
            return Encoding::WideToUtf8(std::wstring_view(entry + name.size() + 1, entryLength - name.size() - 1));
        }
        offset += entryLength + 1;
    }

    return std::nullopt;
}

} // namespace

std::optional<uint64_t> GetCreationTime(uint32_t pid) {
    Windows::UniqueHandle process = OpenProcessHandle(pid, PROCESS_QUERY_LIMITED_INFORMATION);
    if (!process) return std::nullopt;

    FILETIME creation{};
    FILETIME exitTime{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(process.get(), &creation, &exitTime, &kernel, &user)) {
        OSTP_LOG_DEBUG("GetProcessTimes(pid={}) failed (error={})", pid, GetLastError());
        return std::nullopt;
    }

    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

std::string FormatCreationTime(uint64_t fileTime) {
    if (fileTime == 0) return "-";

    ULARGE_INTEGER value{};
    value.QuadPart = fileTime;

    FILETIME utc{};
    utc.dwLowDateTime = value.LowPart;
    utc.dwHighDateTime = value.HighPart;

    FILETIME local{};
    if (!FileTimeToLocalFileTime(&utc, &local)) {
        OSTP_LOG_DEBUG("FileTimeToLocalFileTime(filetime={}) failed (error={})", fileTime, GetLastError());
        return std::format("filetime={}", fileTime);
    }

    SYSTEMTIME system{};
    if (!FileTimeToSystemTime(&local, &system)) {
        OSTP_LOG_DEBUG("FileTimeToSystemTime(filetime={}) failed (error={})", fileTime, GetLastError());
        return std::format("filetime={}", fileTime);
    }

    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                       system.wYear,
                       system.wMonth,
                       system.wDay,
                       system.wHour,
                       system.wMinute,
                       system.wSecond,
                       system.wMilliseconds);
}

std::optional<std::string> GetImagePath(uint32_t pid) {
    Windows::UniqueHandle process = OpenProcessHandle(pid, PROCESS_QUERY_LIMITED_INFORMATION);
    if (!process) return std::nullopt;

    for (DWORD capacity : {static_cast<DWORD>(MAX_PATH), DWORD{32768}}) {
        std::wstring path(capacity, L'\0');
        DWORD size = capacity;
        if (QueryFullProcessImageNameW(process.get(), 0, path.data(), &size)) {
            path.resize(size);
            return Encoding::WideToUtf8(path);
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            OSTP_LOG_DEBUG("QueryFullProcessImageNameW(pid={}) failed (error={})", pid, GetLastError());
            return std::nullopt;
        }
    }
    OSTP_LOG_DEBUG("QueryFullProcessImageNameW(pid={}) failed: path exceeds 32768 wchars", pid);
    return std::nullopt;
}

std::optional<std::string> GetEnvironmentVariableValue(uint32_t pid, std::wstring_view name) {
    Windows::UniqueHandle process =
        OpenProcessHandle(pid, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ);
    if (!process) return std::nullopt;

    const auto environment = ReadEnvironmentBlock(process.get());
    if (!environment) return std::nullopt;
    return FindEnvironmentVariable(*environment, name);
}

std::optional<std::string> GetProcessCommandLine(uint32_t pid) {
    Windows::UniqueHandle process =
        OpenProcessHandle(pid, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ);
    if (!process) return std::nullopt;

    auto commandLine = ReadCommandLineWow64(process.get());
    if (!commandLine) commandLine = ReadCommandLineNative(process.get());
    if (!commandLine) return std::nullopt;
    return Encoding::WideToUtf8(*commandLine);
}

std::vector<ModuleInfo> EnumerateModules(uint32_t pid) {
    std::vector<ModuleInfo> modules;
    Windows::UniqueFileHandle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot) {
        OSTP_LOG_DEBUG("CreateToolhelp32Snapshot(pid={}) failed (error={})", pid, GetLastError());
        return modules;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot.get(), &entry)) {
        do {
            std::filesystem::path nativePath(entry.szExePath);
            std::string path = Encoding::WideToUtf8(entry.szExePath);
            if (path.empty()) continue;

            modules.push_back(ModuleInfo{
                std::move(path),
                std::move(nativePath),
                entry.modBaseSize,
                false,
            });
        } while (Module32NextW(snapshot.get(), &entry));
    }
    return modules;
}

bool IsSystemModulePath(const std::string& path) {
    static const std::string systemDir = [] {
        const UINT needed = GetWindowsDirectoryW(nullptr, 0);
        if (needed == 0) {
            OSTP_LOG_WARN("GetWindowsDirectoryW(size query) failed (error={})", GetLastError());
            return std::string{};
        }
        std::wstring dir(needed, L'\0');
        const UINT written = GetWindowsDirectoryW(dir.data(), needed);
        if (written == 0 || written >= needed) {
            OSTP_LOG_WARN("GetWindowsDirectoryW failed (written={}, error={})", written, GetLastError());
            return std::string{};
        }
        dir.resize(written);
        std::string normalized = NormalizePathForCompare(Encoding::WideToUtf8(dir));
        if (!normalized.empty() && normalized.back() != '\\') normalized += '\\';
        return normalized;
    }();

    if (systemDir.empty()) return false;
    return NormalizePathForCompare(path).starts_with(systemDir);
}

} // namespace OSTPlatform::Process
