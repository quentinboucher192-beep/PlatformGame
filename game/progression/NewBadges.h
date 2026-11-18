#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
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
///
/// The assets (2.69, ADMINISTRATION › Assets) are novelties too, without a line each in that
/// catalogue: data/assets/asset_versions.json (written by tools/assets/asset_versions.py) gives
/// the version in which each file of assets/ first appeared, and each one from its « pillsFrom »
/// on is the novelty "asset:<path>" under the Assets tab, by the same rule (newer than the
/// baseline, not seen). Its folders join the trail once the tab has read its catalogue (link).
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
    /// The assets' versions (data/assets/asset_versions.json), added to the catalogue already
    /// set: "asset:<path>" for each asset from « pillsFrom » on (older ones never carry a pill:
    /// they were never announced), under `parent` (the Assets tab). A missing or damaged file:
    /// no asset is new. Returns the number of assets that can carry a pill.
    int loadAssetVersions(const std::filesystem::path& file, const std::string& parent);
    int addAssetVersions(const std::map<std::string, std::string>& versions, const std::string& pillsFrom, const std::string& parent);
    /// `id` gets `parents` too (an id not in the catalogue yet becomes a step of the trail); a
    /// parent it already has: no change. For the trails known late (the asset tree).
    void link(const std::string& id, const std::vector<std::string>& parents);
    bool has(const std::string& id) const { return features_.count(id) > 0; }
    static std::string assetFeature(std::string_view path) { return "asset:" + std::string(path); }
    /// A folder of the asset tree, by its stable key ("": the root, « Tous les assets »).
    static std::string assetFolderFeature(std::string_view key) { return "asset.folder:" + (key.empty() ? std::string("/") : std::string(key)); }
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
