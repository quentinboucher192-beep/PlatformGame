#pragma once

// ADMINISTRATION › Calendrier des défis (2.41): the tab of the administration screen
// (AdminScene.cpp) that shows a month of challenges — the coming ones too, which no player
// sees — and edits the challenge of a period that has not begun: its level (campaign, or a
// published community level), its rules, its objective and its tiers, or puts back the
// server's draw. Everything is checked again by the server (PUT / DELETE
// /api/v1/admin/challenges/{id}); a period that has begun is read-only.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/DailyChallenges.h"
#include "game/online/OnlineTypes.h"
#include "ui/screens/ChallengeUi.h"

namespace cosmic {

class GameServices;
class OnlineUi;

class AdminCalendar {
public:
    /// `say`: the screen's status line (message, error); `lostRole`: the server no longer
    /// knows this account as an administrator (the screen closes).
    AdminCalendar(GameServices& g, OnlineUi& ou, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// "YYYY-MM[:<challenge id>[:objectif]]" (automated screenshots): the month, the period
    /// selected, the editor's second tab.
    void open(const std::string& arg);
    /// The tab is shown: its month (and the levels) are read once; `force`: again.
    void enter(bool force = false);
    void draw(const Rect& body);
    /// The powers' picker is open (it takes the input; drawn by drawModal over everything).
    bool modal() const { return picker_; }
    void drawModal();

private:
    struct Period {
        std::string id, kind, period, date, endDate, state, drawsOn;
        bool editable = false;
        Json challenge;                         // null: none (not drawn yet, or never)
        std::optional<DailyChallenge> parsed;   // its level and rules, for the cells
    };
    struct Draft {
        std::string periodId;
        bool level = false;  // a level is chosen
        std::string levelSource, levelId, levelTitle, authorName;
        int levelVersion = 0;
        bool hasRace = false;
        int hearts = 10, slots = 4;
        std::vector<std::string> powers;
        bool stations = true, checkpoints = true;
        bool finish = false;
        int64_t silverMs = 0, goldMs = 0;
        bool goldNoDamage = true;
        int bronzePct = 50, silverPct = 80;
    };

    void loadMonth(const std::string& month);
    void loadCampaign();
    void loadCommunity(int offset);
    void select(const std::string& periodId);
    void draftFrom(const Period& p);
    const Period* find(const std::string& id) const;
    void save();
    void resetToDraw();
    Json body() const;
    /// Why « Enregistrer » cannot be used yet ("" = it can).
    std::string draftProblem() const;
    void pickCampaign(const Json& l);
    void pickCommunity(const Json& l);

    void drawCalendar(const Rect& r);
    void drawMonthBand(const Rect& r);
    void drawCell(const std::string& name, const Rect& r, const Period* p, const std::string& head, bool today);
    void drawPill(const Period* p, float x, float cy, float h, float room);
    void drawLegend(float x, float y, float w);
    void drawEditor(const Rect& r);
    float drawEditorHead(const Period& p, float x, float y, float w);
    void drawLevelTab(const Period& p, const Rect& r, bool editable);
    void drawLevelList(const Rect& r, bool editable);
    void drawRules(float x, float y, float w, bool editable);
    void drawObjectiveTab(const Period& p, const Rect& r, bool editable);
    /// "−  value  +" at the right of a row; returns true when the value changed.
    bool stepper(const std::string& id, float right, float cy, float h, const std::string& value, int& v, int step, int lo, int hi, bool enabled);
    bool timeStepper(const std::string& id, float right, float cy, float h, int64_t& ms, bool enabled);
    void rowLabel(const std::string& t, float x, float cy, float w);

    GameServices& g_;
    OnlineUi& ou_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    challenge_ui::CalendarArt art_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    std::string month_;         // "2026-11"
    std::string firstMonth_, lastMonth_, today_;
    std::vector<Period> periods_;
    bool loaded_ = false, loading_ = false;
    std::string loadError_;
    int monthGen_ = 0;
    std::string selected_;      // the period in the editor
    std::string wantSelect_;    // to select once its month is read
    Draft draft_;
    bool dirty_ = false;
    int subTab_ = 0;            // 0: Niveau et règles, 1: Objectif et paliers
    bool busy_ = false;         // a change on its way
    std::string editorMsg_;
    bool editorError_ = false;
    bool editorWarn_ = false;   // a warning of the server (saved all the same)
    // Levels
    int levelSource_ = 0;       // 0: campaign, 1: community
    Json campaign_ = Json::array();
    bool campaignLoaded_ = false;
    int playerMaxHearts_ = 10;
    std::string search_, searched_;
    float searchT_ = 0.0f;
    bool searchDirty_ = false;
    Json community_ = Json::array();
    bool communityLoaded_ = false, communityMore_ = false;
    int communityOffset_ = 0, communityGen_ = 0;
    bool picker_ = false;       // the powers' picker
    bool scrollToChosen_ = false;  // the level list brings the chosen level into view
    std::string pendingSwitch_;    // a period chosen while the draft had changes (chosen again: they go)
    bool entered_ = false;
};

}  // namespace cosmic
