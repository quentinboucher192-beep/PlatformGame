#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace cosmic {

/// « NOUVEAU » pills (2.25). Every novelty of an update is listed in
/// data/news/new_features.json: an id, the version that brought it, and the ids of the
/// screens and buttons that lead to it (« parents »: the main menu entry, the tab, the
/// category…). An entry without a version is a pure step of the trail (a tab, a category):
/// it only links what is under it to its own parents, as deep as needed.
///
/// A novelty (an entry with a version) is new while it is newer than the version the player
/// had before updating (the baseline) and has not been seen; it goes as soon as its widget
/// gets the focus or a click. A parent is new while something under it is still new, whatever
/// focus it had: its pill goes only once everything deeper has been seen (and, if it is a
/// novelty itself, once it has been seen too). Saved in saves/new_badges.json.
class NewBadges {
public:
    struct Feature {
        std::string id;
        std::string version;  // "": a step of the trail only
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
    /// The novelty `id` has been seen (its pill goes, and its parents' once nothing else under
    /// them is new). A step of the trail has nothing of its own to see: no change. Returns
    /// true when something changed (to be saved).
    bool markSeen(const std::string& id);
    bool save() const;

    const std::string& baseline() const { return baseline_; }

private:
    bool ownNew(const std::string& id) const;
    /// Something new at `id` or under it (a cycle in a badly written catalogue is cut).
    bool newAtOrBelow(const std::string& id, std::set<std::string>& visited) const;

    std::map<std::string, Feature> features_;
    std::map<std::string, std::vector<std::string>> children_;
    std::set<std::string> seen_;  // "id@version"
    std::string baseline_;
    std::filesystem::path file_;
    mutable std::unordered_map<std::string, bool> cache_;  // isNew() of this state
};

}  // namespace cosmic
