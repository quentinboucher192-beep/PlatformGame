#pragma once

// Main menu → Statistiques (2.54; redesigned in 2.91): the scene, shared by its files —
// StatisticsScene.cpp (the frame, Vue d'ensemble, Combat, Données et sauvegarde),
// StatisticsBoss.cpp (Boss) and StatisticsLevels.cpp (Niveaux, Passages difficiles). Internal to
// the screen: the other screens open it with makeStatistics (Navigation.h).

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Scene.h"
#include "game/records/PlayStats.h"
#include "game/records/PlayStatsViews.h"
#include "ui/screens/StatisticsArt.h"
#include "ui/widgets/Charts.h"
#include "ui/widgets/CosmicFrame.h"

namespace cosmic {

class GameServices;
class UIContext;
struct CreatureAtlas;

namespace statsui {

// The look of the screen (the validated mock-up): one card style, a categorical palette checked for
// colour-blind readers on the dark background, values in the text colours.
inline constexpr Color kCard{14, 20, 40, 235};
inline constexpr Color kCardLine{57, 240, 216, 46};
inline constexpr Color kText2{200, 211, 226, 255};
inline constexpr Color kTrack{26, 36, 66, 255};
inline constexpr Color kGold{255, 214, 90, 255};
inline constexpr Color kSeries[4] = {{57, 135, 229, 255}, {217, 89, 38, 255}, {25, 158, 112, 255}, {160, 92, 240, 255}};
inline constexpr Color kSrcRun = kSeries[0], kSrcTraining = kSeries[2], kSrcOnline = kSeries[1];

/// « 12,35 », « 1,0 »: a French number.
std::string frNum(double v, int decimals);
/// « 45 % ».
std::string pct(double v);
/// « 18 h 42 », « 25 min », « 40 s ».
std::string duration(double sec);
/// « 12,35 s », « 1:02,35 » (« — » when unknown).
std::string clockTime(float sec);
/// « 0:31,20 »: always with its minutes (the segments' times, as in the mock-up).
std::string clockLong(float sec);
/// « 22/11 ».
std::string dayShort(int day);
/// « 22 nov. 2026 ».
std::string dayLong(int day);
/// « Samedi 22/11 ».
std::string dayNamed(int day);
/// « AUJOURD'HUI », « HIER », else « 22/11 ».
std::string dayAgo(int day, int today);
/// A line's height at `px` (screen pixels).
float lineHeight(UIContext& ui, FontFace f, float px);

}  // namespace statsui

class StatisticsScene : public Scene {
public:
    StatisticsScene(GameServices& g, int tab);
    ~StatisticsScene() override;

    void onEnter() override;
    void update(float dt) override;
    void render(float alpha) override;

    static constexpr int kTabCount = 6;
    static constexpr int kTabOverview = 0, kTabCombat = 1, kTabBoss = 2, kTabLevels = 3, kTabHard = 4, kTabData = 5;

private:
    using Tip = charts::Tip;

    const PlayStatsData& data() const;
    bool noData() const { return data().empty(); }
    float fz(float size) const;

    // ------------------------------------------------------------------ building blocks
    /// A card: its glass, its frame, a coloured stripe at the top (`top`).
    void cardFrame(const Rect& r, Color top = {0, 0, 0, 0}, bool selected = false);
    /// A legend: a short line, a dot or a picture per item, then its name (measured; wraps).
    struct LegendItem {
        Color color;
        std::string text;
        int kind = 0;  // 0 line, 1 dot, 2 band (a wide swatch), 3 picture (`icon`)
        std::function<void(const Rect&)> icon;
    };
    /// A card with its title, its subtitle and, at its right, `right` (the room it takes, its
    /// drawing) and the « Courbe / Tableau » switch when `table` is given. Returns the room under it.
    struct CardHead {
        std::string title, sub, table;
        std::string tableOn = "Tableau", tableOff = "Courbe";  // the switch's two words
        Color top{0, 0, 0, 0};
        bool fresh = false;  // a « NOUVEAU » pill after the title
        std::function<float(float right, float y, bool draw)> extra;  // drawn left of the switch: returns its width
        /// Its legend: at the right of the header when it fits beside the title (measured), else on a
        /// line of its own under the header.
        std::vector<LegendItem> legend;
    };
    Rect card(const Rect& r, const CardHead& h);
    /// « Courbe | Tableau » at the right of `right`: returns its width (measured; drawn when `draw`).
    float tableSwitch(const std::string& id, float right, float y, bool draw, const std::string& on, const std::string& off);
    float legend(float x, float y, float w, const std::vector<LegendItem>& items, bool draw = true);
    float legendWidth(const std::vector<LegendItem>& items);
    /// A note: wrapped, measured, with an accent bar.
    float note(float x, float y, float w, const std::string& text);
    /// A kicker (small capitals): returns its width. `upperCase`: `t` translated, then in capitals (a name).
    float kicker(const std::string& t, Vec2 pos, Color c, float size = 13.0f, float room = 0.0f, bool upperCase = false);
    /// 7 j / 30 j / 90 j, then what the period is compared to.
    float periodRow(float x, float y, float w);
    /// A text in pieces of two faces (« **Par jour** : temps joué… »): the bold ones between ** **.
    /// Translated whole first, then wrapped word by word. Returns its height.
    float richText(const std::string& text, float x, float y, float w, float size, Color c, bool draw = true);
    /// A big value (the title face for its digits, the body face for signs it lacks), fitted to `room`.
    float bigValue(const std::string& v, Vec2 pos, float size, float room, Color c, VAlign va = VAlign::Top);
    /// The height a small tile needs (measured): its kicker, its value, its line.
    float miniTileHeight(bool value, bool detail);
    /// A small tile: its kicker, its value, a line under it.
    void miniTile(const Rect& r, const std::string& key, const std::string& value, const std::string& detail, Color detailColor = {0, 0, 0, 0},
                  Color keySwatch = {0, 0, 0, 0});
    /// The comparison of a period with the previous one: its words and whether it is good news.
    struct Delta {
        std::string text;
        int mood = 0;  // 1 better, -1 worse, 0 neutral
    };
    Delta deltaText(const Comparison& c, bool percentPoints);
    /// A pill: its text in a rounded box (green for good news, warm for bad, neutral else).
    float pill(const std::string& t, Vec2 pos, int mood, float size, float room);
    /// The table instead of a chart.
    void tableIn(const std::string& id, const Rect& r, const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows);
    /// A chart's empty state.
    void emptyIn(const std::string& id, const Rect& r, const std::string& text);
    /// A picture's frame while it is being made (or when it cannot be).
    void picturePending(const Rect& r, bool failed);
    /// A section's title outside the cards (« RÉCEMMENT JOUÉ »), its pill, its dim remark: returns its height.
    float sectionTitle(float x, float y, float w, const std::string& title, const std::string& remark, const std::string& feature,
                       const std::string& badgeWidget);
    float drawEmpty(float x, float y, float w);

    // ------------------------------------------------------------------ the tabs
    float drawOverview(float x, float y, float w);
    float drawCombat(float x, float y, float w);
    float drawBoss(float x, float y, float w);
    float drawLevels(float x, float y, float w);
    float drawHard(float x, float y, float w);
    float drawData(float x, float y, float w);
    void drawConfirm();

    // Vue d'ensemble.
    void fillRateCard(charts::LineSpec& spec, std::vector<std::vector<std::string>>& rows, std::vector<std::string>& head,
                      const std::vector<SeriesPoint>& se, Color color, bool percent);
    void heroCard(const Rect& r, const PeriodTotals& cur);
    void kpiTile(const Rect& r, const std::string& key, const std::string& value, const Delta& d, const std::vector<float>& spark, Color color);
    float recentRow(float x, float y, float w);
    // Combat.
    void drawHurtIcon(int origin, const Rect& box);
    void drawPowerIcon(const std::string& power, const Rect& box);
    // Boss.
    std::string bossName(const std::string& id) const;
    void bossBanner(const Rect& r, const std::string& id, Color stripe, bool selected, int index);
    void bossSheet(const Rect& r, const std::string& id, Color swatch);
    void bossPortrait(const std::string& id, const Rect& box, float t);
    // Niveaux and Passages difficiles.
    float chooseLevel(float x, float y, float w, const std::string& id, std::string& want, const std::function<int(const std::string&)>& lastDay,
                      const std::string& info);
    std::string levelName(const std::string& id) const;
    /// « Aethra 1 », « Autres niveaux ».
    std::string levelShort(const std::string& id) const;
    void trainSegment(const std::string& levelId, const std::string& fromCheckpoint);
    /// The level's picture between `a` and `b` in `r` (its frame while it is being made).
    void levelPicture(const std::string& levelId, float a, float b, const Rect& r);
    void segmentChips(const statsart::LevelInfo* li, float a, float b, float x, float y, float w, float h);
    void hardCard(const Rect& r, const HardPassage& h, int rank);

    GameServices& g_;
    cframe::Backdrop bg_;
    int tab_ = 0;
    int period_ = 30;
    int today_ = 0;
    float contentH_[kTabCount] = {};
    Tip tip_;
    std::map<std::string, bool> tables_;
    std::map<std::string, int> pages_, cursors_;
    /// The keyboard cursor of a chart: -1 until it moves (the chart then starts on its last point).
    int& cursorOf(const std::string& id) { return cursors_.try_emplace(id, -1).first->second; }
    statsart::LevelArt art_;
    std::shared_ptr<const CreatureAtlas> atlas_;
    int group_ = 0;
    std::string selBoss_, wantSlices_, wantRuns_, focusChart_;
    bool confirmReset_ = false;
    bool dataFresh_ = false;  // « Données et sauvegarde » was new when opened (2.55)
    std::string status_;
    float anim_ = 0.0f;  // seconds since the screen opened (the sprites' clips)
    // « Répartition du fichier »: stats.json read off the main thread, once per visit of the tab.
    std::optional<std::future<playstats::FileBreakdown>> breakdownJob_;
    std::optional<playstats::FileBreakdown> breakdown_;
};

}  // namespace cosmic
