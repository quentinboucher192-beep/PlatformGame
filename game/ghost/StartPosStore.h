#pragma once

// 2.91 — the StartPos placed in training, kept per level version (« rémanence »: from one
// session to the next, saves/startpos.json). See game/gameplay/StartPos.h for what a StartPos
// is and how it is taken (through the inputs) and applied (a start of the attempt).
//
// Local only: an attempt from a StartPos is never a full run, never a record, never sent online;
// only the segments it then crosses whole join the personal Sum of Best (PersonalBests, source
// StartPos). Another version of the level has its own list (the old ones stay with their version).

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Paths.h"
#include "game/gameplay/StartPos.h"

namespace cosmic {

struct StartPosEntry {
    int id = 0;             // stable in the level version (never reused)
    StartPosState state;
    float at = -1.0f;       // where it lies along the route (0 the start .. 1 the exit; -1: unknown)
    std::string date;       // when it was placed (FileUtil::nowString)
    int attempts = 0;       // attempts started from it
    /// 2.92: placed from the menu (ENTRAÎNEMENT › « Startpos et mesures »): its state is the one of
    /// a section training of its segment (segmentStartPos), not one taken in a run.
    bool menu = false;
};

class StartPosStore {
public:
    static constexpr int kVersion = 1;
    static constexpr size_t kPerLevel = 32;
    static constexpr size_t kLevelsKept = 200;

    /// `file` empty: saves/startpos.json.
    explicit StartPosStore(fs::path file = {});
    void load();
    bool save();

    /// The StartPos of a level version in the order of the route (then from left to right): the
    /// order of « SP 1 », « SP 2 »… and of « Précédente » / « Suivante ».
    std::vector<StartPosEntry> list(const std::string& levelKey) const;
    /// Its rank in list() (0-based), -1 when it is not there.
    int rank(const std::string& levelKey, int id) const;
    /// The one the next attempt starts from (0: none, the training's own start).
    int activeId(const std::string& levelKey) const;
    const StartPosEntry* find(const std::string& levelKey, int id) const;
    /// A new one (made active): its id; 0 when the level already has kPerLevel of them (the
    /// oldest is not dropped behind the player's back) or the key is not valid.
    int add(const std::string& levelKey, const StartPosState& s, float at, const std::string& date, bool menu = false);
    /// 2.92: moved from the menu: its new state and place along the route (its id, date and
    /// attempts kept). False when it is not there.
    bool update(const std::string& levelKey, int id, const StartPosState& s, float at);
    void setActive(const std::string& levelKey, int id);
    /// The previous (-1) or next (+1) one in list() becomes active, around the ends; from none,
    /// the last / the first. Its id (0: none placed).
    int cycle(const std::string& levelKey, int delta);
    bool remove(const std::string& levelKey, int id);
    void clear(const std::string& levelKey);
    void noteAttempt(const std::string& levelKey, int id);

    /// « Montrer les startpos dans le niveau (rémanence) ».
    bool show = true;
    bool readOnly() const { return readOnly_; }

private:
    struct Level {
        std::string touched;
        std::vector<StartPosEntry> entries;  // in the order placed
        int active = 0;
        int nextId = 1;
    };
    fs::path file_;
    std::map<std::string, Level> levels_;
    bool readOnly_ = false;
};

}  // namespace cosmic
