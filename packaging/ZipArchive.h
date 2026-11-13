#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Paths.h"

namespace cosmic {

/// Writes a ZIP archive (deflate) in memory and saves it atomically.
class ZipWriter {
public:
    ZipWriter();
    ~ZipWriter();
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    bool add(const std::string& name, const void* data, size_t size, bool compress = true);
    bool add(const std::string& name, const std::vector<uint8_t>& data, bool compress = true) {
        return add(name, data.data(), data.size(), compress);
    }
    bool add(const std::string& name, const std::string& text) { return add(name, text.data(), text.size(), true); }
    /// Finalizes and writes the archive to `path`.
    bool save(const fs::path& path, std::string* error = nullptr);

private:
    struct Impl;
    Impl* impl_;
};

struct ZipEntry {
    std::string name;
    uint64_t size = 0;
    uint64_t compressedSize = 0;
    bool directory = false;
    bool symlink = false;
    uint32_t index = 0;
};

/// Reads a ZIP archive from memory. Extraction verifies CRCs.
class ZipReader {
public:
    ZipReader();
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    bool open(const fs::path& path, std::string* error = nullptr);
    bool openMemory(std::vector<uint8_t> data, std::string* error = nullptr);
    const std::vector<ZipEntry>& entries() const { return entries_; }
    const ZipEntry* find(const std::string& name) const;
    std::optional<std::vector<uint8_t>> read(const ZipEntry& e, uint64_t maxSize) const;
    uint64_t archiveSize() const { return data_.size(); }

private:
    struct Impl;
    Impl* impl_;
    std::vector<uint8_t> data_;
    std::vector<ZipEntry> entries_;
};

}  // namespace cosmic
