#include "packaging/ZipArchive.h"

#include <miniz.h>

#include <cstring>

#include "engine/core/FileUtil.h"

namespace cosmic {

struct ZipWriter::Impl {
    mz_zip_archive zip{};
    bool ok = false;
};

ZipWriter::ZipWriter() : impl_(new Impl) {
    std::memset(&impl_->zip, 0, sizeof(impl_->zip));
    impl_->ok = mz_zip_writer_init_heap(&impl_->zip, 0, 1 << 20);
}

ZipWriter::~ZipWriter() {
    if (impl_->ok) mz_zip_writer_end(&impl_->zip);
    delete impl_;
}

bool ZipWriter::add(const std::string& name, const void* data, size_t size, bool compress) {
    if (!impl_->ok) return false;
    mz_uint level = compress ? MZ_DEFAULT_LEVEL : MZ_NO_COMPRESSION;
    return mz_zip_writer_add_mem(&impl_->zip, name.c_str(), data, size, level);
}

bool ZipWriter::save(const fs::path& path, std::string* error) {
    if (!impl_->ok) {
        if (error) *error = "archive invalide";
        return false;
    }
    void* buf = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&impl_->zip, &buf, &size)) {
        if (error) *error = "finalisation de l'archive impossible";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + size);
    mz_free(buf);  // ownership of the heap buffer was transferred to us
    mz_zip_writer_end(&impl_->zip);
    impl_->ok = false;
    // Write to a temporary file then rename: never leave a truncated package.
    fs::path tmp = path;
    tmp += ".part";
    if (!FileUtil::writeBinary(tmp, bytes)) {
        if (error) *error = "écriture impossible : " + Paths::toUtf8(path);
        return false;
    }
    std::error_code ec;
    fs::remove(path, ec);
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "renommage impossible : " + ec.message();
        return false;
    }
    return true;
}

struct ZipReader::Impl {
    mz_zip_archive zip{};
    bool ok = false;
};

ZipReader::ZipReader() : impl_(new Impl) { std::memset(&impl_->zip, 0, sizeof(impl_->zip)); }

ZipReader::~ZipReader() {
    if (impl_->ok) mz_zip_reader_end(&impl_->zip);
    delete impl_;
}

bool ZipReader::open(const fs::path& path, std::string* error) {
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        if (error) *error = "fichier introuvable";
        return false;
    }
    if (size > (1ull << 30)) {
        if (error) *error = "fichier trop volumineux (> 1 Go)";
        return false;
    }
    auto data = FileUtil::readBinary(path);
    if (!data) {
        if (error) *error = "lecture impossible";
        return false;
    }
    return openMemory(std::move(*data), error);
}

bool ZipReader::openMemory(std::vector<uint8_t> data, std::string* error) {
    data_ = std::move(data);
    entries_.clear();
    if (!mz_zip_reader_init_mem(&impl_->zip, data_.data(), data_.size(), 0)) {
        if (error) *error = "ce n'est pas une archive ZIP valide";
        return false;
    }
    impl_->ok = true;
    mz_uint n = mz_zip_reader_get_num_files(&impl_->zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&impl_->zip, i, &st)) {
            if (error) *error = "entrée d'archive illisible";
            return false;
        }
        ZipEntry e;
        e.name = st.m_filename;
        e.size = st.m_uncomp_size;
        e.compressedSize = st.m_comp_size;
        e.directory = st.m_is_directory;
        // Unix symlink: file type bits in the high word of the external attributes.
        uint32_t mode = (st.m_external_attr >> 16) & 0170000u;
        e.symlink = mode == 0120000u;
        e.index = i;
        entries_.push_back(e);
    }
    return true;
}

const ZipEntry* ZipReader::find(const std::string& name) const {
    for (const auto& e : entries_)
        if (e.name == name) return &e;
    return nullptr;
}

std::optional<std::vector<uint8_t>> ZipReader::read(const ZipEntry& e, uint64_t maxSize) const {
    if (!impl_->ok || e.size > maxSize || e.directory) return std::nullopt;
    std::vector<uint8_t> out(static_cast<size_t>(e.size));
    if (e.size == 0) return out;
    // Extraction is bounded by the declared size and checks the CRC32.
    if (!mz_zip_reader_extract_to_mem(&impl_->zip, e.index, out.data(), out.size(), 0)) return std::nullopt;
    return out;
}

}  // namespace cosmic
