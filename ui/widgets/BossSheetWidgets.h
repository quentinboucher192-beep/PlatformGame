#pragma once
// The pieces of a boss's sheet shared by Statistiques des boss › Fiche (2.37, 2.60) and the raid
// sheets (2.72, CLANS › RAIDS › Fiches, ui/screens/RaidSheetsScene):
//
// * the difficulty bar: 1 to 10 stars, the number, where it comes from (when it fits);
// * the list of the attacks, each folded or unfolded (a click, Entrée or A; « Tout déplier »,
//   « Tout replier »). Folded: its name, where it comes and its line of details. Unfolded: what it
//   does, how to avoid it (when the sheet says), its characteristics, and its example beside them
//   (under them when the room is short): an attack of the engine played by
//   ui/screens/BossAttackPreview (the boss in its arena giving that attack again and again), or a
//   drawing given by the caller (the raid's mechanisms). One example at a time: the unfolded
//   attack that has the focus (after a moment), else the last unfolded (game/enemies/
//   BossAttackSheet.h: AttackFold). Every text is measured into its room.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/enemies/BossAttackSheet.h"
#include "ui/screens/BossAttackPreview.h"

namespace cosmic {

class GameServices;
class UIContext;
struct CreatureAtlas;
struct EnemyDefinition;

/// 1 to 10 stars, « n / 10 » at the right, and `hint` between them when there is room.
void drawBossDifficulty(UIContext& ui, const Rect& r, int stars, const std::string& hint = "calculée d'après ses PV, ses phases et ses attaques");

/// One row of the list.
struct AttackListItem {
    std::string id;  // its fold key, unique in the list (an engine attack: its id)
    std::string name, where, summary, description;
    std::string avoid;  // « Comment l'éviter » ("" none)
    std::vector<AttackFact> facts;
    bool final = false;          // of the last stand: `where` in red
    std::string engineAttack;    // an attack of the boss given to draw() ("" : a drawn example or none)
    bool showable = false;       // its example can be shown
    std::string unshown;         // why not
    std::string caption;         // the caption of a drawn example
};

/// The rows of a boss's attacks (game/enemies/BossAttackSheet.h), as the boss sheet shows them.
std::vector<AttackListItem> attackListItems(const std::vector<BossAttackEntry>& entries);

class AttackList {
public:
    using DrawExample = std::function<void(const Rect& frame, const AttackListItem& item, float time)>;
    struct Options {
        std::string scroll;           // the scroll region the list is drawn in (an attack brought to its top)
        std::string feature;          // « NOUVEAU » of its buttons and rows ("" none)
        const EnemyDefinition* boss = nullptr;  // whose engine attacks play
        bool engineShown = true;      // the engine's examples may play (a campaign boss: once met)
        std::string engineHidden;     // why they may not
        DrawExample drawExample;      // the other examples (null: none)
        float alpha = 1.0f;
    };

    explicit AttackList(GameServices& g);
    /// Draws the title, the two buttons and every row of `items` from `y` (`top`: the scroll
    /// region's content top, `w`: the room) under the fold of `key` (a boss: its unfolded attacks
    /// kept for the visit). Returns the y under the list.
    float draw(const Rect& view, float top, float y, float w, const std::string& key, const std::vector<AttackListItem>& items, const Options& o);
    /// 120 Hz, and each frame: the engine's example; the drawn example's clock.
    void fixedUpdate();
    void update(float dt);
    void stop();
    void setAtlas(std::shared_ptr<const CreatureAtlas> atlas) { atlas_ = std::move(atlas); }
    AttackFold& fold(const std::string& key) { return folds_[key]; }
    /// Brought to the top of the scroll region on the next draw: an item; the list itself.
    void scrollTo(const std::string& id) { scrollToItem_ = id; }
    void scrollToList() { scrollToList_ = true; }
    /// Rows listed by the last draw(), and those on screen (tests: a sheet lists its attacks).
    int rows() const { return rows_; }
    int rowsShown() const { return shown_; }

private:
    float factsBlock(const std::vector<AttackFact>& facts, float x, float y, float w, bool draw);
    void chevron(Vec2 c, float r, bool open, Color col);

    GameServices& g_;
    std::shared_ptr<const CreatureAtlas> atlas_;
    std::map<std::string, AttackFold> folds_;
    BossAttackPreview preview_;
    struct Layout {  // an unfolded row's texts, measured for a width
        float width = -1.0f;
        bool canShow = false;
        float descH = 0.0f, avoidH = 0.0f, factsH = 0.0f, reasonH = 0.0f;
    };
    std::string layoutKey_;
    std::vector<Layout> layouts_;
    std::string scrollToItem_;
    bool scrollToList_ = false;
    float dt_ = 0.0f;
    // The drawn example: its clock (paused by a click on it), the item it belongs to.
    std::string drawnId_;
    float drawnT_ = 0.0f;
    bool drawnPaused_ = false;
    int rows_ = 0, shown_ = 0;
};

}  // namespace cosmic
