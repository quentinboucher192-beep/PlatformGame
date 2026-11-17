#pragma once

// « Histoire des giga-boss » (2.51): before the first fight against a giga boss (a boss of 8
// phases or more, never an echo), a short story tells who it was before it became a boss, shot
// after shot (a painting with a slow zoom and a line of narration), then reveals its name on a
// title card. The stories are data (data/stories/<boss id>.json: the lines, the scene of each
// shot, its painting and its colours), so that a new giga boss only needs its file and its
// paintings (tools/assetgen/environment/stories.py).
//
// Display only: the run is paused while it is told (an overlay scene that does not update the
// scene below, as the pause menu), nothing of the simulation, of the replays or of the records
// depends on it. « Story seen » is kept per boss with the boss statistics (BossStats).

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

struct EnemyDefinition;
class BossStats;

/// One shot: its painting (a texture under assets/), its line of narration (French: translated
/// when drawn), the slow zoom towards a point of the painting, the live particles over it.
struct StoryShot {
    std::string scene;            // what it shows (« young_star », « nest »…)
    std::string image;            // assets/<image>
    std::string text;             // the narration
    Vec2 focus{0.5f, 0.5f};       // the point the zoom goes to (0..1 of the painting)
    float zoom = 1.1f;            // the painting's scale at the end of the shot (1 at its start)
    std::string particles = "dust";  // over the painting: dust, embers, sparks, snow, stars, none
    Color glow = Color::white();  // the particles' colour
};

/// The story of a giga boss.
struct BossStory {
    std::string bossId;
    std::string title;      // the boss's subtitle on the title card (« Le Cœur effondré »)
    Color color = Color::white();  // its colour (title card, progress bar)
    Color accent = Color::white(); // a second colour (glows of the title card)
    std::string clip = "roar";     // the boss's clip on the title card (its idle one if missing)
    std::vector<StoryShot> shots;

    static std::optional<BossStory> fromJson(const Json& j, std::string* error = nullptr);
};

namespace story {
constexpr int kGigaPhases = 8;        // a giga boss has at least this many phases
constexpr float kTitleCard = 3.0f;    // seconds of the title card
constexpr float kTypeTime = 0.8f;     // the narration is typed within this time, then stays
constexpr float kCrossFade = 0.5f;    // between two shots
constexpr float kFade = 0.3f;         // in at the start, out at the end or when skipped
constexpr float kMinShot = 4.5f;      // a shot lasts at least this…
constexpr float kShotBase = 1.2f;     // …else this plus the time to read its line:
constexpr float kReadRate = 16.0f;    // characters read per second (a calm reading)
/// Characters of a text (UTF-8 code points).
size_t characters(std::string_view text);
/// How long a shot stays: max(4.5 s, 1.2 s + characters / 16), long enough to read its line.
float shotDuration(std::string_view text);
/// The whole story with its title card (texts as given: the French ones).
float duration(const BossStory& s);
}  // namespace story

/// A giga boss: a boss of 8 phases or more that is not a variant (an echo) of another one.
bool isGigaBoss(const EnemyDefinition& d);

/// The stories of the game (data/stories/*.json).
class BossStories {
public:
    /// Reads every story of `dir` (default: <data>/stories). Damaged files are skipped (and
    /// named in `error`); false when one could not be read.
    bool load(const std::filesystem::path& dir, std::string* error = nullptr);
    bool loadDefault(std::string* error = nullptr);
    void add(BossStory s);
    const BossStory* find(const std::string& bossId) const;
    const std::map<std::string, BossStory>& all() const { return stories_; }

private:
    std::map<std::string, BossStory> stories_;
};

/// Paramètres › Jeu › « Histoire des giga-boss ».
enum class StorySetting { BeforeFirstFight = 0, Never = 1 };

/// Where the fight starts: a story is told in normal levels, challenges and practice runs, never
/// in a boss training, an editor test or an automated run (scripted input). Replays, spectating
/// and the race views never play a run (they have no story at all).
struct StoryContext {
    bool bossTraining = false;
    bool editorTest = false;
    bool scripted = false;
};

/// Whether the story of `boss` is told now, its fight starting: a giga boss with a story whose
/// story this player has not seen yet, the setting asking for it, in a run that allows it.
bool storyDue(const EnemyDefinition& boss, const BossStories& stories, const BossStats& stats, int setting, const StoryContext& ctx);

}  // namespace cosmic
