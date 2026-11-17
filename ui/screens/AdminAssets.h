#pragma once
// ADMINISTRATION › Assets (2.63): every file the game installs under assets/, sorted in a
// logical tree (Personnages, Ennemis, Boss et giga-boss par monde, Pouvoirs, Décors et tuiles
// par planète, Interface et icônes, Effets et particules, Histoires, Musiques par groupe du
// juke-box, Sons, Polices), each folder with its number of assets and its weight. A search
// (name, path, folder, type, « utilisé par ») narrows the list as it is typed, with filters by
// type and « Inutilisés » (what nothing in the game uses). The results are a grid of thumbnails
// or a list; only the visible rows are drawn and their thumbnails read (LRU of textures).
// The asset chosen is previewed (an image on a checkerboard, an animation that plays, a sound
// or a music heard, a font's specimen, a text) with what is known of it, who uses it (each user
// narrows the list to its own assets; « Fiche » opens its sheet in the game when it has one:
// boss, level, planet, enemy, suit, story, power) and « Ouvrir le dossier ». Read only.
// The catalogue is read once per session off the main thread (game/content/AssetCatalog.h).

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/content/AssetCatalog.h"
#include "ui/widgets/AssetPreview.h"

namespace cosmic {

class GameServices;

class AdminAssets {
public:
    /// `openEnemy`: shows a native enemy in the administration's « Ennemis » tab.
    AdminAssets(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<void(const std::string&)> openEnemy);

    /// The tab is shown (the catalogue starts being read the first time).
    void enter();
    /// The tab is left: its sound stops.
    void leave();
    /// That asset selected (the way back from a sheet opened here, automated screenshots).
    void open(const std::string& path);
    void draw(const Rect& inner);

    // What the tab shows (checks, automated runs).
    void setSearch(const std::string& text) { search_ = text; }
    void setGrid(bool grid) { grid_ = grid; }
    const std::string& selected() const { return selected_; }
    const std::vector<int>& results() const { return results_; }
    const std::shared_ptr<const AssetCatalog>& catalog() const { return cat_; }
    AssetPreview& preview() { return preview_; }
    /// Results drawn in the last frame (only the visible ones are drawn).
    int drawnItems() const { return drawnItems_; }

private:
    struct User {
        AssetUseKind kind = AssetUseKind::Code;
        std::string id, name;
        bool literal = false;
    };
    void refilter();
    void select(int index, bool reveal);
    void drawToolbar(const Rect& r);
    void drawTypes(const Rect& r);
    void drawTree(const Rect& r);
    void drawResults(const Rect& r);
    void drawGrid(const Rect& r);
    void drawList(const Rect& r);
    void drawThumb(const AssetEntry& e, const Rect& box);
    void drawDetail(const Rect& r);
    void drawUses(const AssetEntry& e, const Rect& r);
    /// The sheet of a user in the game, if it has one: opens it.
    bool hasSheet(const AssetUse& u) const;
    void openSheet(const AssetUse& u);
    void openFolder(const AssetEntry& e);
    std::string assetLine(const AssetEntry& e) const;

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<void(const std::string&)> openEnemy_;
    std::shared_ptr<const AssetCatalog> cat_;
    AssetPreview preview_;
    // Filters
    std::string search_, applied_;
    int type_ = 0;  // 0: every type, then AssetType + 1
    bool unusedOnly_ = false;
    bool grid_ = true;
    int folder_ = 0;
    std::set<int> expanded_;
    std::optional<User> user_;
    bool dirty_ = true;
    std::vector<int> results_;
    uint64_t resultBytes_ = 0;
    // Selection
    std::string selected_;  // its path (kept across filters)
    std::string pendingOpen_;
    bool scrollToSelected_ = false;
    int drawnItems_ = 0;
};

}  // namespace cosmic
