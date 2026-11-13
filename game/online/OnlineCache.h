#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

/// Last known answers of the server (leaderboards, globe, profiles), in memory and on disk,
/// so that the online screens still show something without network. Entries are never
/// presented as fresh: their age is always available.
class OnlineCache {
public:
    struct Entry {
        Json data;
        Json meta;
        std::chrono::system_clock::time_point fetchedAt;
        double ageSeconds() const {
            return std::chrono::duration<double>(std::chrono::system_clock::now() - fetchedAt).count();
        }
    };

    /// `dir` empty: memory only.
    explicit OnlineCache(fs::path dir = {}, size_t maxEntries = 400) : dir_(std::move(dir)), max_(maxEntries) {}

    void put(const std::string& key, const Json& data, const Json& meta = Json());
    std::optional<Entry> get(const std::string& key);
    /// Removes every entry (memory and disk). Returns the number of files deleted.
    size_t clear();
    size_t memoryEntries() const { return mem_.size(); }
    const fs::path& directory() const { return dir_; }

private:
    fs::path fileFor(const std::string& key) const;
    void prune();

    fs::path dir_;
    size_t max_;
    std::map<std::string, Entry> mem_;
};

}  // namespace cosmic
