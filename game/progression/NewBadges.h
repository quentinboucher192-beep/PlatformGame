#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cosmic {

/// « NOUVEAU » pills (2.25). Every novelty of an update is listed in
/// data/news/new_features.json: an id, the version that brought it, and the ids of the
/// screens and buttons that lead to it (« parents »: the main menu entry, the tab…).
///
/// A pill shows on its widget while the novelty is newer than the version the player had
/// before updating (the baseline) and has not been seen; it goes as soon as the widget gets
/// the focus or a click. A parent shows a pill while one of its novelties is still unseen
/// (until the parent itself is focused). Saved in saves/new_badges.json.
class NewBadges {
public:
    struct Feature {
        std::string id;
        std::string version;
        std::vector<std::string> parents;
    };

    /// The catalogue (data/news/new_features.json); a missing or damaged file: no novelty.
    void loadCatalog(const std::filesystem::path& file);
    void setCatalog(const std::vector<Feature>& features);
    /// `previousVersion`: the version the player ran before this one ("" on a fresh install:
    /// nothing is new to a new player). Kept from the first run with pills on, so that a
    /// novelty stays new across later updates until it is seen.
    void init(const std::filesystem::path& saveFile, const std::string& previousVersion, const std::string& currentVersion);

    bool isNew(const std::string& id) const;
    /// The pill of `id` goes (and its parents' pills when nothing else under them is new).
    void markSeen(const std::string& id);
    bool save() const;

    const std::string& baseline() const { return baseline_; }

private:
    /// The newest version of a still unseen novelty at or under `id` ("" if none).
    std::string newestUnseen(const std::string& id, int depth = 0) const;
    bool ownNew(const std::string& id) const;

    std::map<std::string, Feature> features_;
    std::map<std::string, std::vector<std::string>> children_;
    std::set<std::string> seen_;  // "id@version"
    std::string baseline_;
    std::filesystem::path file_;
};

}  // namespace cosmic
