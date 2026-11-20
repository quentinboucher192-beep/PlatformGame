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
    std::string_view clip;  // the clip playing (a view of its key's name; empty: the sheet's idle clip)
    int clipKey = -1;       // the key that started it (-1: none)
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
    Vec2 drift{};                  // a layer or sprite sliding by itself (units per second)
    bool repeat = false;           // a layer or sprite repeated along x (it drifts without end)
    float pulse = 0.0f;            // its alpha breathing: down to (1 - pulse) and back,
    float pulsePeriod = 2.0f;      // once per period (seconds),
    float pulsePhase = 0.0f;       // from this point of it (0..1)
    struct Shadow {
        bool on = false;
        float alpha = 0.4f;
        Vec2 size{60.0f, 12.0f};    // units, times the sprite's scale
        std::optional<Vec2> light;  // a light: the shadow is pushed away from it
    } shadow;                       // a sprite's soft shadow on the ground, at its point
    std::vector<StoryKey> keys;
    /// Its alpha multiplier at `t` (the pulse).
    float pulseAt(float t) const;
    StoryElementState at(float t) const { return storyStateAt(keys, t); }
};

struct StoryShake {
    float from = 0.0f, to = 0.0f, amount = 0.0f;  // seconds, units
};
/// 2.63: a sound of a scene, on the « effets » volume of the settings (and muted with it). A
/// one-shot plays once at `t`; a loop (an ambience) plays from `t` to `to` (the shot's end when 0),
/// fading in and out over `fade`. `at`: where it is heard from (a point of the stage: panned by
/// its place on the screen); `pan` a fixed panning instead (-1 left .. 1 right). `duck`: a big
/// sound — the music lowers a little while it plays. Its times are seconds of the French line,
/// stretched as the rest of the scene.
struct StorySfx {
    std::string sound;  // assets/<sound>
    float t = 0.0f;
    float volume = 0.7f;
    float pitch = 1.0f;
    std::optional<float> pan;
    std::optional<Vec2> at;
    bool loop = false;
    float to = 0.0f;
    float fade = 0.4f;
    bool duck = false;
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
    std::vector<StorySfx> sounds;  // 2.63: its sound effects (none: a silent scene, as before)
    /// 2.68 (a film's scenes): the seconds its keys were written for; a shot of another length
    /// stretches it (0: the shot's own length, as before).
    float length = 0.0f;

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

/// 2.68 (a film): a title drawn large in the picture from `t` to `to` (seconds of the shot as
/// written: stretched with its scene), its subtitle under it; centred on `pos` (fractions of the
/// picture), fitted to `width` of it, at most `size` pixels (at the reference scale), its glow of
/// `color`. One title at a time: the latest begun of those showing.
struct StoryTitle {
    float t = 0.0f, to = 0.0f;
    std::string text, subtitle;
    Vec2 pos{0.5f, 0.5f};
    float width = 0.86f;
    float size = 70.0f;
    Color color = Color::white();
};

/// 2.81 (« Après Asterion »): where a film writes the names of the clans present at Asterion's fall (given by
/// the server when it plays, never by the film's files), from `t` to `to` (seconds of the shot as written),
/// in `area` (fractions of the picture: x, y, width, height), at most `size` pixels (reference scale), their
/// glow `color`, the viewer's own clan in `own`; `generic` (French, translated when drawn) when there are no
/// names (offline, a test, an admin's preview).
struct StoryRoster {
    float t = 0.0f, to = 0.0f;
    Rect area{0.1f, 0.3f, 0.8f, 0.6f};
    float size = 34.0f;
    Color color = Color::white();
    Color own = Color::white();
    std::string generic;
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
    /// 2.63 (the trailer): the shot's length in seconds (0: its line's reading time, as a story's),
    /// and a smaller line under its title (a trailer's line is a title, drawn large).
    float duration = 0.0f;
    std::string subtitle;
    std::vector<StoryTitle> titles;  // 2.68 (a film): its titles in the picture
    std::optional<StoryRoster> roster;  // 2.81 (a film): the names of the clans of the fall
};

/// The story of a giga boss.
struct BossStory {
    std::string bossId;
    int version = 1;
    std::string title;      // the boss's subtitle on the title card (« Le Cœur effondré »)
    Color color = Color::white();  // its colour (title card, progress bar)
    Color accent = Color::white(); // a second colour (glows of the title card)
    std::string clip = "roar";     // the boss's clip on the title card (its idle one if missing)
    /// 2.63: « trailer »: the game's trailer (data/trailer/trailer.json), not a boss's story: its
    /// lines are titles, its last card the game's name, its music its own (`music`), its title
    /// card `titleCard` seconds long.
    std::string kind = "story";
    std::string music;
    float titleCard = 3.0f;
    bool isTrailer() const { return kind == "trailer"; }
    /// 2.68: « film »: a mini-film (data/raids/asterion_film.json): its shots have their own lengths,
    /// their captions at the bottom (none when empty) and their titles in the picture, its music
    /// its own, no title card (it ends on its last shot).
    bool isFilm() const { return kind == "film"; }
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
/// 2.63: a shot's length: its own `duration` when it has one (the trailer), else shotDuration.
float shotLength(const StoryShot& shot);
/// 2.63: the game's trailer (data/trailer/trailer.json); nullopt when it cannot be read.
std::optional<BossStory> loadTrailer(std::string* error = nullptr);
/// 2.68: a mini-film: its script (data/<file>: kind « film », each shot's caption, titles and
/// length, written by hand) with the scenes it names (the file of its « scenes », written by a
/// generator); nullopt when either cannot be read.
std::optional<BossStory> loadFilm(const std::filesystem::path& script, std::string* error = nullptr);
/// Asterion's film (data/raids/asterion_film.json), from `dataDir` (default: the game's).
std::optional<BossStory> loadAsterionFilm(std::string* error = nullptr);
std::optional<BossStory> loadAsterionFilm(const std::filesystem::path& dataDir, std::string* error);
/// 2.74 (phase 6): the film of Asterion's fall (data/raids/asterion_victoire.json, its scenes written by
/// tools/assetgen/environment/story_asterion_victoire.py), from `dataDir` (default: the game's).
std::optional<BossStory> loadAsterionVictory(std::string* error = nullptr);
std::optional<BossStory> loadAsterionVictory(const std::filesystem::path& dataDir, std::string* error);
/// 2.81: « Après Asterion », what the fall changes in the world (data/raids/asterion_apres.json, its scenes
/// written by tools/assetgen/environment/story_asterion_apres.py), from `dataDir` (default: the game's).
std::optional<BossStory> loadAsterionAftermath(std::string* error = nullptr);
std::optional<BossStory> loadAsterionAftermath(const std::filesystem::path& dataDir, std::string* error);
/// The whole story with its title card (texts as given: the French ones).
float duration(const BossStory& s);
constexpr int kVersion = 2;  // the newest format read (1: paintings, 2: animated scenes)
/// The files a story's scenes need besides its paintings: the layers' images and the sprites'
/// animations, each once, in the order of the stages.
struct StageAssets {
    std::vector<std::string> textures;
    std::vector<std::string> animations;
    std::vector<std::string> sounds;  // 2.63: the sounds of its scenes
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
