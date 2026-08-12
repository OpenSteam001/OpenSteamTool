#include "include/PE.h"

#include "Windows/Handles.h"
#include "include/Log.h"
#include "include/Stopwatch.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace OSTPlatform::PE {
namespace {

constexpr size_t kHeaderReadBytes = 4 * 1024 * 1024;

struct FileRead {
    ByteBuffer bytes;
    uint64_t fileSize = 0;
};

// Open a file for buffered sequential reading. FILE_FLAG_SEQUENTIAL_SCAN asks
// the cache manager to read ahead aggressively and drop pages behind the
// cursor — the right hint for a single forward pass over a large module image.
// Sharing is permissive because the target module is usually open/executing.
Windows::UniqueFileHandle OpenForSequentialRead(const std::filesystem::path& path) {
    return Windows::UniqueFileHandle(::CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));
}

// ReadFile caps each call at a DWORD and may return fewer bytes than asked;
// loop until the whole buffer is filled. Returns false on error or premature EOF.
bool ReadFull(HANDLE file, uint8_t* dst, size_t total) {
    size_t done = 0;
    while (done < total) {
        const DWORD chunk = static_cast<DWORD>(
            (std::min)(total - done, static_cast<size_t>(0x40000000)));  // 1 GiB cap
        DWORD got = 0;
        if (!::ReadFile(file, dst + done, chunk, &got, nullptr)) return false;
        if (got == 0) break;  // unexpected EOF before `total`
        done += got;
    }
    return done == total;
}

FileRead ReadFileRange(const std::filesystem::path& path, uint64_t offset, size_t requestedSize) {
    const Stopwatch totalTimer;
    const Windows::UniqueFileHandle file = OpenForSequentialRead(path);
    if (!file) {
        OSTP_LOG_DEBUG("PE::ReadFileRange open failed path={} offset=0x{:X} requested={} err={}",
                       path.string(), offset, requestedSize, ::GetLastError());
        return {};
    }

    LARGE_INTEGER sizeLi{};
    if (!::GetFileSizeEx(file.get(), &sizeLi) || sizeLi.QuadPart <= 0) {
        OSTP_LOG_DEBUG("PE::ReadFileRange empty/invalid file path={} offset=0x{:X} requested={}",
                       path.string(), offset, requestedSize);
        return {};
    }

    const uint64_t fileSize = static_cast<uint64_t>(sizeLi.QuadPart);
    if (offset >= fileSize) {
        OSTP_LOG_DEBUG("PE::ReadFileRange offset past EOF path={} offset=0x{:X} file_size={} elapsed_ms={:.3f}",
                       path.string(), offset, fileSize, totalTimer.ElapsedMs());
        return {{}, fileSize};
    }

    const size_t readSize = static_cast<size_t>((std::min)(
        static_cast<uint64_t>(requestedSize),
        fileSize - offset));

    ByteBuffer bytes(readSize);  // default-init allocator: no redundant zero-fill before read

    LARGE_INTEGER seekTo{};
    seekTo.QuadPart = static_cast<LONGLONG>(offset);
    if (!::SetFilePointerEx(file.get(), seekTo, nullptr, FILE_BEGIN)) {
        OSTP_LOG_DEBUG("PE::ReadFileRange seek failed path={} offset=0x{:X} err={}",
                       path.string(), offset, ::GetLastError());
        return {};
    }

    const Stopwatch readTimer;
    if (!ReadFull(file.get(), bytes.data(), readSize)) {
        OSTP_LOG_DEBUG("PE::ReadFileRange read failed path={} offset=0x{:X} requested={} read_size={} err={} elapsed_ms={:.3f}",
                       path.string(), offset, requestedSize, readSize, ::GetLastError(), totalTimer.ElapsedMs());
        return {};
    }
    const double readMs = readTimer.ElapsedMs();

    OSTP_LOG_DEBUG("PE::ReadFileRange path={} offset=0x{:X} requested={} read={} file_size={} read_ms={:.3f} total_ms={:.3f}",
                   path.string(), offset, requestedSize, readSize, fileSize, readMs, totalTimer.ElapsedMs());

    return {std::move(bytes), fileSize};
}

ByteBuffer ReadFileBytes(const std::filesystem::path& path) {
    const Stopwatch totalTimer;
    const Windows::UniqueFileHandle file = OpenForSequentialRead(path);
    if (!file) {
        OSTP_LOG_DEBUG("PE::ReadFileBytes open failed path={} err={}", path.string(), ::GetLastError());
        return {};
    }

    LARGE_INTEGER sizeLi{};
    if (!::GetFileSizeEx(file.get(), &sizeLi) || sizeLi.QuadPart <= 0) {
        OSTP_LOG_DEBUG("PE::ReadFileBytes empty/invalid file path={}", path.string());
        return {};
    }

    const size_t size = static_cast<size_t>(sizeLi.QuadPart);
    ByteBuffer bytes(size);

    const Stopwatch readTimer;
    if (!ReadFull(file.get(), bytes.data(), size)) {
        OSTP_LOG_DEBUG("PE::ReadFileBytes read failed path={} size={} err={} elapsed_ms={:.3f}",
                       path.string(), static_cast<uint64_t>(size), ::GetLastError(), totalTimer.ElapsedMs());
        return {};
    }
    const double readMs = readTimer.ElapsedMs();

    OSTP_LOG_DEBUG("PE::ReadFileBytes path={} size={} read_ms={:.3f} total_ms={:.3f}",
                   path.string(), static_cast<uint64_t>(size), readMs, totalTimer.ElapsedMs());
    return bytes;
}

template <typename T>
const T* PtrAt(std::span<const uint8_t> bytes, size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return nullptr;
    return reinterpret_cast<const T*>(bytes.data() + offset);
}

const char* StringAt(std::span<const uint8_t> bytes, size_t offset) {
    if (offset >= bytes.size()) return nullptr;
    const auto* str = reinterpret_cast<const char*>(bytes.data() + offset);
    const void* terminator = std::memchr(str, '\0', bytes.size() - offset);
    return terminator ? str : nullptr;
}

std::string SectionName(const IMAGE_SECTION_HEADER& section) {
    char name[9] = {};
    std::memcpy(name, section.Name, 8);
    return name;
}

} // namespace

bool Section::ContainsRva(uint32_t rva) const {
    const uint32_t size = (std::max)(virtualSize, rawSize);
    const uint32_t end = virtualAddress + size;
    return end >= virtualAddress && rva >= virtualAddress && rva < end;
}

Image::Image(const std::filesystem::path& path) : path_(path) {
    const Stopwatch totalTimer;
    FileRead header = ReadFileRange(path, 0, kHeaderReadBytes);
    headerBytes_ = std::move(header.bytes);
    fileSize_ = header.fileSize;
    if (headerBytes_.empty()) {
        OSTP_LOG_DEBUG("PE::Image invalid empty header path={} elapsed_ms={:.3f}",
                       path.string(), totalTimer.ElapsedMs());
        return;
    }

    const auto* dos = PtrAt<IMAGE_DOS_HEADER>(headerBytes_, 0);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
        OSTP_LOG_DEBUG("PE::Image invalid DOS header path={} elapsed_ms={:.3f}",
                       path.string(), totalTimer.ElapsedMs());
        return;
    }

    const size_t ntOffset = static_cast<size_t>(dos->e_lfanew);
    const auto* signature = PtrAt<DWORD>(headerBytes_, ntOffset);
    if (!signature || *signature != IMAGE_NT_SIGNATURE) {
        OSTP_LOG_DEBUG("PE::Image invalid NT signature path={} nt_offset=0x{:X} elapsed_ms={:.3f}",
                       path.string(), ntOffset, totalTimer.ElapsedMs());
        return;
    }

    const size_t fileHeaderOffset = ntOffset + sizeof(DWORD);
    const auto* fileHeader = PtrAt<IMAGE_FILE_HEADER>(headerBytes_, fileHeaderOffset);
    if (!fileHeader || fileHeader->NumberOfSections == 0) {
        OSTP_LOG_DEBUG("PE::Image invalid file header path={} elapsed_ms={:.3f}",
                       path.string(), totalTimer.ElapsedMs());
        return;
    }

    const size_t optionalOffset = fileHeaderOffset + sizeof(IMAGE_FILE_HEADER);
    const auto* magic = PtrAt<WORD>(headerBytes_, optionalOffset);
    if (!magic) {
        OSTP_LOG_DEBUG("PE::Image missing optional header path={} elapsed_ms={:.3f}",
                       path.string(), totalTimer.ElapsedMs());
        return;
    }

    IMAGE_DATA_DIRECTORY exportDirectory{};
    if (*magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        const auto* optional = PtrAt<IMAGE_OPTIONAL_HEADER64>(headerBytes_, optionalOffset);
        if (!optional) return;
        entryPointRva_ = optional->AddressOfEntryPoint;
        exportDirectory = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else if (*magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        const auto* optional = PtrAt<IMAGE_OPTIONAL_HEADER32>(headerBytes_, optionalOffset);
        if (!optional) return;
        entryPointRva_ = optional->AddressOfEntryPoint;
        exportDirectory = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else {
        OSTP_LOG_DEBUG("PE::Image unsupported optional header path={} magic=0x{:X} elapsed_ms={:.3f}",
                       path.string(), *magic, totalTimer.ElapsedMs());
        return;
    }

    const size_t sectionsOffset = optionalOffset + fileHeader->SizeOfOptionalHeader;
    const size_t sectionsEnd = sectionsOffset +
        static_cast<size_t>(fileHeader->NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (sectionsEnd > headerBytes_.size()) {
        OSTP_LOG_DEBUG("PE::Image section headers outside header buffer path={} sections={} elapsed_ms={:.3f}",
                       path.string(), fileHeader->NumberOfSections, totalTimer.ElapsedMs());
        return;
    }

    sections_.reserve(fileHeader->NumberOfSections);
    for (WORD i = 0; i < fileHeader->NumberOfSections; ++i) {
        const auto* section = PtrAt<IMAGE_SECTION_HEADER>(
            headerBytes_,
            sectionsOffset + static_cast<size_t>(i) * sizeof(IMAGE_SECTION_HEADER));
        if (!section) {
            OSTP_LOG_DEBUG("PE::Image missing section header path={} index={} elapsed_ms={:.3f}",
                           path.string(), i, totalTimer.ElapsedMs());
            return;
        }

        sections_.push_back(Section{
            SectionName(*section),
            section->VirtualAddress,
            section->Misc.VirtualSize,
            section->PointerToRawData,
            section->SizeOfRawData,
            section->Characteristics,
        });
    }

    exportDirectoryRva_ = exportDirectory.VirtualAddress;
    exportDirectorySize_ = exportDirectory.Size;
    valid_ = true;
    OSTP_LOG_DEBUG("PE::Image parsed path={} file_size={} sections={} entry=0x{:X} elapsed_ms={:.3f}",
                   path.string(), fileSize_, sections_.size(), entryPointRva_, totalTimer.ElapsedMs());
}

bool Image::HasSection(std::string_view name) const {
    return std::ranges::any_of(sections_, [name](const Section& section) {
        return section.name == name;
    });
}

const Section* Image::SectionContainingRva(uint32_t rva) const {
    for (const Section& section : sections_) {
        if (section.ContainsRva(rva)) return &section;
    }
    return nullptr;
}

std::optional<size_t> Image::RvaToOffset(uint32_t rva) const {
    const Section* section = SectionContainingRva(rva);
    if (!section) return std::nullopt;

    const uint32_t delta = rva - section->virtualAddress;
    if (delta >= section->rawSize) return std::nullopt;

    const size_t offset = static_cast<size_t>(section->rawOffset) + delta;
    return offset < fileSize_ ? std::optional<size_t>(offset) : std::nullopt;
}

std::optional<uint32_t> Image::RawOffsetToRva(size_t rawOffset) const {
    for (const Section& section : sections_) {
        const size_t sectionRaw = section.rawOffset;
        const size_t sectionEnd = sectionRaw + section.rawSize;
        if (sectionEnd < sectionRaw || rawOffset < sectionRaw || rawOffset >= sectionEnd) continue;

        const size_t delta = rawOffset - sectionRaw;
        if (delta > UINT32_MAX) return std::nullopt;
        return section.virtualAddress + static_cast<uint32_t>(delta);
    }
    return std::nullopt;
}

ByteBuffer Image::ReadRawBytes(size_t offset, size_t size) const {
    return ReadFileRange(path_, offset, size).bytes;
}

ByteBuffer Image::ReadAllBytes() const {
    return ReadFileBytes(path_);
}

std::optional<Export> Image::FindExport(std::string_view symbolName) const {
    if (!valid_ || exportDirectoryRva_ == 0 || exportDirectorySize_ == 0) return std::nullopt;

    const auto exportOffset = RvaToOffset(exportDirectoryRva_);
    if (!exportOffset) return std::nullopt;

    const Section* exportSection = SectionContainingRva(exportDirectoryRva_);
    if (!exportSection) return std::nullopt;

    const ByteBuffer exportBytes = ReadRawBytes(exportSection->rawOffset, exportSection->rawSize);
    if (exportBytes.empty()) return std::nullopt;

    auto viewAtRva = [&](uint32_t rva, size_t minSize = 0) -> std::optional<std::span<const uint8_t>> {
        const auto raw = RvaToOffset(rva);
        if (!raw || *raw < exportSection->rawOffset) return std::nullopt;

        const size_t local = *raw - exportSection->rawOffset;
        if (local > exportBytes.size() || minSize > exportBytes.size() - local) return std::nullopt;
        return std::span<const uint8_t>(exportBytes).subspan(local);
    };

    const auto exportView = viewAtRva(exportDirectoryRva_, sizeof(IMAGE_EXPORT_DIRECTORY));
    if (!exportView) return std::nullopt;

    const auto* exports = PtrAt<IMAGE_EXPORT_DIRECTORY>(*exportView, 0);
    if (!exports || exports->NumberOfNames == 0 || exports->NumberOfFunctions == 0) return std::nullopt;

    const auto namesView = viewAtRva(exports->AddressOfNames, static_cast<size_t>(exports->NumberOfNames) * sizeof(DWORD));
    const auto ordinalsView = viewAtRva(exports->AddressOfNameOrdinals, static_cast<size_t>(exports->NumberOfNames) * sizeof(WORD));
    const auto functionsView = viewAtRva(exports->AddressOfFunctions, static_cast<size_t>(exports->NumberOfFunctions) * sizeof(DWORD));
    if (!namesView || !ordinalsView || !functionsView) return std::nullopt;

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const auto* nameRva = PtrAt<DWORD>(*namesView, static_cast<size_t>(i) * sizeof(DWORD));
        if (!nameRva) return std::nullopt;

        const auto nameView = viewAtRva(*nameRva);
        if (!nameView) continue;

        const char* exportedName = StringAt(*nameView, 0);
        if (!exportedName || symbolName != exportedName) continue;

        const auto* ordinal = PtrAt<WORD>(*ordinalsView, static_cast<size_t>(i) * sizeof(WORD));
        if (!ordinal || *ordinal >= exports->NumberOfFunctions) return std::nullopt;

        const auto* functionRva = PtrAt<DWORD>(*functionsView, static_cast<size_t>(*ordinal) * sizeof(DWORD));
        if (!functionRva || *functionRva == 0) return std::nullopt;

        Export result{};
        result.rva = *functionRva;

        const uint32_t exportEnd = exportDirectoryRva_ + exportDirectorySize_;
        if (exportEnd >= exportDirectoryRva_ &&
            *functionRva >= exportDirectoryRva_ &&
            *functionRva < exportEnd) {
            const auto forwarderView = viewAtRva(*functionRva);
            const char* forwarder = forwarderView ? StringAt(*forwarderView, 0) : nullptr;
            if (!forwarder) return std::nullopt;
            result.forwarder = forwarder;
        }

        return result;
    }

    return std::nullopt;
}

} // namespace OSTPlatform::PE
