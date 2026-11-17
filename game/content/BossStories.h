#pragma once

// « Histoire des giga-boss » (2.51): before the first fight against a giga boss (a boss of 8
// phases or more, never an echo), a short story tells who it was before it became a boss, shot
// after shot (a painting with a slow zoom and a line of narration), then reveals its name on a
// title card. The stories are data (data/stories/<boss id>.json: the lines, the scene of each
// shot, its painting and its colours), so that a new giga boss only needs its file and its
// paintings (tools/assetgen/environment/stories.py).
//
// Version 2 (2.60, Pyrrhax first): a shot can be an animated scene instead of a fixed painting
// (StoryStage: parallax layers of scenery, sprites in the game's animation format moved by
// keyframes, glows and particles, a camera that pans and zooms, shakes and flashes). Its painting
// stays: the still rendered from the scene, shown when the scene cannot be played. The stories
// written in version 1 are read as before.
//
// Display only: the run is paused while it is told (an overlay scene that does not update the
// scene below, as the pause menu), nothing of the simulation, of the replays or of the records
// depends on it. « Story seen » is kept per boss with the boss statistics (BossStats).

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

struct EnemyDefinition;
class BossStats;

/// How a value of a key is reached from the key before.
enum class StoryEase : uint8_t { Linear, In, Out, InOut, Step };
float storyEase(StoryEase e, float u);

/// A key of an element (or of the camera): what it does not give, the element keeps from the key
/// before. `pos`, `scale`, `alpha` and `rot` go from the key before to this one with `ease`;
/// `flip` and `clip` change at `t` (a clip starts there, `offset` seconds in).
struct StoryKey {
    float t = 0.0f;
    std::optional<Vec2> pos;
    std::optional<float> scale;  // for the camera: its zoom
    std::optional<float> alpha;
    std::optional<float> rot;    // degrees, clockwise, about the element's point
    std::optional<bool> flip;
    std::string clip;
    float offset = 0.0f;
    StoryEase ease = StoryEase::InOut;
};

/// An element at a time of its shot.
struct StoryElementState {
    Vec2 pos{};
    float scale = 1.0f;
    float alpha = 1.0f;
    float rot = 0.0f;
    bool flip = false;
    std::string clip;       // empty: the sheet's idle clip
    float clipTime = 0.0f;  // seconds into it
};

/// Resolves keys (sorted by time) at time `t`: before the first one, its values; a sprite shows
/// the first frame of its first clip until that clip starts.
StoryElementState storyStateAt(const std::vector<StoryKey>& keys, float t);

/// A thing drawn in a scene, in the order of the stage's list.
enum class StoryElementKind : uint8_t { Layer, Sprite, Glow, Particles };
struct StoryElement {
    std::string id;
    StoryElementKind kind = StoryElementKind::Layer;
    std::string source;  // the layer's image, the sprite's animation (.json), the particles' kind
    float depth = 1.0f;  // parallax: 0 never moves, 1 moves with the camera, more is in front
    bool additive = false;
    Color tint = Color::white();
    Color color = Color::white();  // a glow's, the particles'
    float radius = 100.0f;         // a glow's (units, times its scale)
    Rect area;                     // the particles' (units)
    Vec2 drift{};                  // a layer sliding by itself (units per second)
    bool repeat = false;           // a layer repeated along x (it drifts without end)
    std::vector<StoryKey> keys;
    StoryElementState at(float t) const { return storyStateAt(keys, t); }
};

struct StoryShake {
    float from = 0.0f, to = 0.0f, amount = 0.0f;  // seconds, units
};
struct StoryFlash {
    float t = 0.0f;               // its peak
    float in = 0.1f, out = 0.5f;  // rising before it, fading after it
    Color color = Color::white();
    float alpha = 1.0f;
};

/// An animated shot: a stage of `size` units (960x540; one pixel of a sheet or a layer is two
/// units at scale 1) seen through a camera, covering the frame as a painting would. Its times
/// are seconds of the shot as written (its French line): the scene is stretched to the shot's
/// length in the language shown.
struct StoryStage {
    Vec2 size{960.0f, 540.0f};
    Color backdrop{0, 0, 0, 255};
    std::vector<StoryKey> camera;  // pos: the point in the middle of the view, scale: its zoom
    std::vector<StoryShake> shakes;
    std::vector<StoryFlash> flashes;
    std::vector<StoryElement> elements;

    static std::optional<StoryStage> fromJson(const Json& j, std::string* error = nullptr);
    /// The camera at `t`: its point and its zoom (before it is kept inside the stage).
    std::pair<Vec2, float> cameraAt(float t) const;
    /// The camera's point moved so that a view of `view` units shows nothing outside the stage.
    Vec2 clampCamera(Vec2 pos, Vec2 view) const;
    /// The screen's shake at `t` (units).
    Vec2 shakeAt(float t) const;
    /// The flashes' strength at `t` (0..1), their colour in `color`.
    float flashAt(float t, Color* color = nullptr) const;
    /// The last time its keys, shakes and flashes name (seconds of the shot as written).
    float lastTime() const;
};

/// One shot: its painting (a texture under assets/), its line of narration (French: translated
/// when drawn), the slow zoom towards a point of the painting, the live particles over it.
/// Version 2: `stage`, the shot played as an animated scene (its painting is the scene's still).
struct StoryShot {
    std::string scene;            // what it shows (« young_star », « nest »…)
    std::string image;            // assets/<image>
    std::string text;             // the narration
    Vec2 focus{0.5f, 0.5f};       // the point the zoom goes to (0..1 of the painting)
    float zoom = 1.1f;            // the painting's scale at the end of the shot (1 at its start)
    std::string particles = "dust";  // over the painting: dust, embers, sparks, snow, stars, ember_rain (2.60), none
    Color glow = Color::white();  // the particles' colour
    std::optional<StoryStage> stage;  // version 2: the animated scene
};

/// The story of a giga boss.
struct BossStory {
    std::string bossId;
    int version = 1;
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
constexpr int kVersion = 2;  // the newest format read (1: paintings, 2: animated scenes)
/// The files a story's scenes need besides its paintings: the layers' images and the sprites'
/// animations, each once, in the order of the stages.
struct StageAssets {
    std::vector<std::string> textures;
    std::vector<std::string> animations;
};
StageAssets stageAssets(const BossStory& s);
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
