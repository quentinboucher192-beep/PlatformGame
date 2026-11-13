#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cosmic {

/// SHA-256 (FIPS 180-4) used to fingerprint every file of a level package.
class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t len);
    std::array<uint8_t, 32> finish();

    static std::string hex(const void* data, size_t len);
    static std::string hex(const std::vector<uint8_t>& v) { return hex(v.data(), v.size()); }
    static std::string hex(const std::string& s) { return hex(s.data(), s.size()); }

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t bufLen_ = 0;
    uint64_t total_ = 0;
};

}  // namespace cosmic
