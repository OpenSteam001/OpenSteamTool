#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace OSTPlatform::PE {

// Owning byte buffer for bulk file reads. Unlike std::vector<uint8_t>, sizing
// it does NOT initialize the storage — it uses std::make_unique_for_overwrite,
// which is a single uninitialized array allocation (one operator new[]). So a
// buffer about to be fully overwritten by a file read pays neither a zero-fill
// nor a per-element construct loop. Move-only; converts to a const span for
// scanning/parsing.
class ByteBuffer {
public:
    ByteBuffer() = default;
    explicit ByteBuffer(size_t size)
        : data_(size ? std::make_unique_for_overwrite<uint8_t[]>(size) : nullptr),
          size_(size) {}

    uint8_t* data() { return data_.get(); }
    const uint8_t* data() const { return data_.get(); }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    operator std::span<const uint8_t>() const { return {data_.get(), size_}; }

private:
    std::unique_ptr<uint8_t[]> data_;
    size_t size_ = 0;
};

struct Section {
    std::string name;
    uint32_t virtualAddress = 0;
    uint32_t virtualSize = 0;
    uint32_t rawOffset = 0;
    uint32_t rawSize = 0;
    uint32_t characteristics = 0;  // IMAGE_SECTION_HEADER::Characteristics

    bool ContainsRva(uint32_t rva) const;
    // IMAGE_SCN_MEM_EXECUTE / IMAGE_SCN_MEM_WRITE — a section that is both is a
    // W^X violation (self-modifying code), the hallmark of a runtime-decrypting
    // protector.
    bool IsExecutable() const { return (characteristics & 0x20000000u) != 0; }
    bool IsWritable() const { return (characteristics & 0x80000000u) != 0; }
};

struct Export {
    uint32_t rva = 0;
    std::string forwarder;

    bool IsForwarder() const { return !forwarder.empty(); }
};

class Image {
public:
    explicit Image(const std::filesystem::path& path);

    explicit operator bool() const { return valid_; }
    const std::filesystem::path& Path() const { return path_; }
    uint64_t FileSize() const { return fileSize_; }
    uint32_t EntryPointRva() const { return entryPointRva_; }
    std::span<const Section> Sections() const { return sections_; }

    bool HasSection(std::string_view name) const;
    const Section* SectionContainingRva(uint32_t rva) const;
    std::optional<size_t> RvaToOffset(uint32_t rva) const;
    std::optional<uint32_t> RawOffsetToRva(size_t rawOffset) const;
    ByteBuffer ReadRawBytes(size_t offset, size_t size) const;
    ByteBuffer ReadAllBytes() const;
    std::optional<Export> FindExport(std::string_view symbolName) const;

private:
    std::filesystem::path path_;
    ByteBuffer headerBytes_;
    std::vector<Section> sections_;
    uint64_t fileSize_ = 0;
    uint32_t entryPointRva_ = 0;
    uint32_t exportDirectoryRva_ = 0;
    uint32_t exportDirectorySize_ = 0;
    bool valid_ = false;
};

} // namespace OSTPlatform::PE
