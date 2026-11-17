#pragma once

// ADMINISTRATION › Calendrier des défis (2.41): the tab of the administration screen
// (AdminScene.cpp) that shows a month of challenges — the coming ones too, which no player
// sees — and edits the challenge of a period that has not begun: its level (campaign, or a
// published community level), its rules, its objective and its tiers, or puts back the
// server's draw. Everything is checked again by the server (PUT / DELETE
// /api/v1/admin/challenges/{id}); a period that has begun is read-only. 2.43: a third tab,
// « Récompenses »: the rewards of the period (an item of the game's Collection, or a title
// created here on the server), each with its condition (PUT …/{id}/rewards, POST /admin/titles).

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
#include "ui/screens/ChallengeRewardsUi.h"
#include "ui/screens/ChallengeUi.h"

namespace cosmic {

class GameServices;
class OnlineUi;

class AdminCalendar {
public:
    /// `say`: the screen's status line (message, error); `lostRole`: the server no longer
    /// knows this account as an administrator (the screen closes).
    AdminCalendar(GameServices& g, OnlineUi& ou, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// "YYYY-MM[:<challenge id>[:objectif|recompenses]]" (automated screenshots): the month, the
    /// period selected, the editor's second or third tab.
    void open(const std::string& arg);
    /// The tab is shown: its month (and the levels) are read once; `force`: again.
    void enter(bool force = false);
    void draw(const Rect& body);
    /// The powers' picker or (2.43) the game items' picker is open (it takes the input; drawn by
    /// drawModal over everything).
    bool modal() const { return picker_ || itemPicker_; }
    void drawModal();
    /// 2.60: the tooltip of the exclusive reward hovered or focused (the list of a challenge's
    /// rewards, the game items' picker), over everything: once the screen and its modal are drawn.
    void drawTooltip();

private:
    struct Period {
        std::string id, kind, period, date, endDate, state, drawsOn;
        bool editable = false;
        Json challenge;                         // null: none (not drawn yet, or never)
        std::optional<DailyChallenge> parsed;   // its level and rules, for the cells
        std::vector<ChallengePrize> rewards;    // 2.43: its rewards (set even before its draw)
        std::string rewardsGrantedAt;           // when the server granted them ("" not yet)
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
    // 2.43: « Récompenses »
    void savePrizes();
    void loadTitles();
    void createTitle();
    void addPrize(ChallengePrize p);
    void drawRewardsTab(const Period& p, const Rect& r, bool editable);
    /// One reward of the list of `per` in `row`; returns true when its × was used.
    bool drawPrizeRow(const Period& per, size_t i, const Rect& row, bool editable);
    /// 2.60: the creation of « Créations » with this reference (GET /admin/creations), nullptr when unknown.
    const Json* creationByRef(const std::string& ref) const;
    /// Bronze / Argent / Or / Top N / Niveau fini (and N), wrapping in `w`; returns its height
    /// (`draw` false: measured only).
    float conditionChooser(const std::string& id, float x, float y, float w, bool draw);
    /// The form under the list (a title to create, or a reward's condition); returns its height.
    float prizeFormHeight(float w);
    void drawPrizeForm(const Rect& r);
    void drawItemPicker();
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
    // 2.43: « Récompenses »: the draft's rewards (sent apart from the challenge, by « Enregistrer »).
    std::vector<ChallengePrize> prizes_;
    bool prizesDirty_ = false;
    enum class PrizeForm { None, Title, Condition };
    PrizeForm form_ = PrizeForm::None;
    int formIndex_ = -1;           // the reward whose condition is changed
    std::string titleText_;
    int titleColor_ = 0;           // 0 Or, 1 Argent, 2 Cyan, 3 Violet
    std::string condKind_ = "tier";
    int condValue_ = 3;
    bool titleBusy_ = false;
    bool itemPicker_ = false;      // the game items' picker (modal)
    int pickCat_ = 0;
    std::string pickRef_;          // the item (or server title) chosen in it
    Json titles_ = Json::array();  // the titles created on the server (GET /admin/titles)
    bool titlesLoaded_ = false;
    Json suits_ = Json::array();   // the creations of « Créations » (GET /admin/creations): suits, 2.59 trails, frames, victory effects
    std::string suitsError_;       // why that list could not be read ("" when it was)
    ExclusiveTip tip_;             // 2.60: the tooltip of the exclusive reward hovered or focused
};

}  // namespace cosmic
