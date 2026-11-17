#pragma once
// The attacks of a boss as its sheet shows them (2.60, Statistiques des boss › Fiche): each
// attack folds and unfolds; unfolded, it says what it does, gives its characteristics read
// from its definition (data/enemies) and plays an example of it with the game engine
// (game/gameplay/BossAttackDemo.h). Nothing here is estimated: every value comes from the
// definition, or from the engine's own rules where the definition leaves a value to the type
// (0 = « the type's default », as the attacks themselves read it), each one named where it is.
//
// Pure: no world, no screen. The fold of the attacks and the choice of the example shown
// (AttackFold) are pure too, and tested (tests/test_boss_attack_sheet.cpp).

#include <functional>
#include <string>
#include <vector>

#include "game/enemies/EnemyDefinition.h"

namespace cosmic {

/// « 0,6 », « 12 »: at most `decimals` decimals, French decimal comma, no useless zero.
std::string sheetNumber(float v, int decimals = 1);
/// « ×1,25 »: a multiplier as the sheet shows it.
std::string sheetTimes(float v);

/// One characteristic of an attack, read from its definition.
struct AttackFact {
    std::string label;
    std::string value;
};

/// Where (and whether) the example of an attack can be played.
struct AttackPreviewPlan {
    bool showable = false;
    int phase = 0;           // the phase it is shown in (0 = the first): the first that uses it
    bool lastStand = false;  // only in its last stand: shown there (the boss out of health)
    std::string reason;      // not showable: why, in words
};

/// One attack of the sheet.
struct BossAttackEntry {
    const AttackDef* attack = nullptr;
    std::vector<int> phases;      // the phases that use it (0 = the first)
    bool final = false;           // one of its last stand's attacks
    std::string summary;          // the folded line: type, damage, wind-up, cooldown and its own details
    std::string where;            // « Phases 1 et 3 · Dernier souffle »
    std::string description;      // what it does, in words (its type, its own parameters)
    std::vector<AttackFact> facts;
    AttackPreviewPlan plan;
};

/// The name of another enemy (the reinforcements of a « Appel de renforts »); nullptr: its id.
using EnemyNameFn = std::function<std::string(const std::string& id)>;

/// Every attack of `d`, in the order of its definition.
std::vector<BossAttackEntry> bossAttackEntries(const EnemyDefinition& d, const EnemyNameFn& nameOf = nullptr);

/// The folded line of an attack (as the sheet showed it since 2.37).
std::string attackSummary(const AttackDef& a);
/// What the attack does, in words: its type, then what its definition sets.
std::string attackDescription(const AttackDef& a);
/// What its type does (one or two sentences).
std::string attackTypeDescription(AttackType t);
/// Its characteristics, read from the definition (only those its type uses).
std::vector<AttackFact> attackFacts(const EnemyDefinition& d, const AttackDef& a, const EnemyNameFn& nameOf = nullptr);
/// The phases using `attackId` (0 = the first); a boss without phases uses every attack in its
/// only phase (0).
std::vector<int> attackPhases(const EnemyDefinition& d, const std::string& attackId);
/// « Phases 1 et 3 · Dernier souffle ».
std::string attackWhere(const EnemyDefinition& d, const std::string& attackId);
/// Where the example of the attack is played, or why it cannot be.
AttackPreviewPlan attackPreviewPlan(const EnemyDefinition& d, const AttackDef& a);
/// The wind-up of `a` in `phase` (seconds, as the boss counts it, without rage nor frost).
float attackWindUp(const EnemyDefinition& d, const AttackDef& a, int phase);
/// Its cooldown in `phase` (seconds, without rage).
float attackCooldown(const EnemyDefinition& d, const AttackDef& a, int phase);

/// Which attacks of a boss's sheet are unfolded, and which one plays its example: only one at
/// a time, the unfolded attack that has the focus (once it has kept it a moment: a pointer
/// passing over the list does not restart the example at each row), else the last unfolded.
class AttackFold {
public:
    /// How long the focus stays on an unfolded attack before its example replaces the other.
    static constexpr float kDwell = 0.3f;

    bool expanded(const std::string& id) const;
    bool anyExpanded() const { return !order_.empty(); }
    bool allExpanded(const std::vector<std::string>& ids) const;
    /// Unfolds a folded attack (its example becomes the one shown), folds an unfolded one.
    void toggle(const std::string& id);
    void expand(const std::string& id);
    void collapse(const std::string& id);
    /// « Tout déplier »: every attack of `ids`; the example shown stays, or the first one.
    void expandAll(const std::vector<std::string>& ids);
    /// « Tout replier »: none, and no example.
    void collapseAll();
    /// The focus this frame (the attack whose row or example has it, "" for anything else).
    void update(const std::string& focused, float dt);
    /// The attack whose example plays ("" = none).
    const std::string& preview() const { return preview_; }
    /// The unfolded attacks, the last unfolded last.
    const std::vector<std::string>& order() const { return order_; }

private:
    void forget(const std::string& id);
    std::vector<std::string> order_;
    std::string preview_;
    std::string focus_;
    float focusTime_ = 0.0f;
};

}  // namespace cosmic
