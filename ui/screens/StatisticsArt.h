#pragma once

// 2.91 — the pictures of the « Statistiques » screen, made from the game's own art (display only):
//  * levels: what the screen knows of a level (its route, checkpoints, relief, what is met along
//    it), read off the main thread; pictures of a stretch of its route (a segment, a hard passage)
//    and the strip of its 40 slices under the heat map, drawn by the game's renderer (its terrain
//    baked a few milliseconds per frame, the pictures drawn a few per frame, kept, the level let go
//    once drawn): opening a tab never waits for them;
//  * sprites: a frame of a creature, of Nova or of an object, cut to its visible pixels and fitted
//    in a box; a planet's banner covering a box.

#include <array>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"
#include "game/gameplay/LevelProgress.h"
#include "game/records/PlayStats.h"

namespace cosmic {

class GameServices;
class GameWorld;
class GameRenderer;
struct LevelData;
struct CreatureAtlas;

namespace statsart {

/// What the screen knows of a level (read off the main thread).
struct LevelInfo {
    bool ok = false;
    std::string name;
    bool extreme = false;
    std::vector<float> relief;                               // heights 0..1 along the route
    std::vector<std::pair<float, std::string>> checkpoints;  // (position 0..1, id), in route order
    std::map<std::string, std::string> labels;               // checkpoint id → its label
    /// What is met along the route (« ce qu'on y trouve »).
    struct Thing {
        enum class Kind { Enemy, Boss, Tablet, Station, Crumble };
        Kind kind = Kind::Enemy;
        std::string id;  // the enemy, or the power
        float at = 0.0f;  // 0..1 along the route
    };
    std::vector<Thing> things;
    LevelProgressTrack track;
    Rect bounds;
    /// The things between `a` and `b` (0..1), grouped: each kind and id once, with its count, in
    /// the order met.
    struct Group {
        Thing::Kind kind;
        std::string id;
        int count = 0;
    };
    std::vector<Group> between(float a, float b) const;
    /// The checkpoint's label ("" = the start or the finish, `end` telling which).
    std::string checkpointName(const std::string& id, bool end) const;
    /// The position 0..1 of a checkpoint (0 for "" at the start, 1 at the end).
    float at(const std::string& id, bool end) const;
};

/// The world box a stretch `a`..`b` of the route covers, with room around it, at the proportions of
/// `w` × `h`, inside the level.
Rect stretchWindow(const LevelInfo& info, float a, float b, float w, float h);

struct ReadLevel;

/// The levels' pictures of one screen: read, drawn and kept while the screen is open.
class LevelArt {
public:
    explicit LevelArt(GameServices& g);
    ~LevelArt();
    LevelArt(const LevelArt&) = delete;
    LevelArt& operator=(const LevelArt&) = delete;

    /// What the screen knows of the level (starts reading it): nullptr until it is read.
    const LevelInfo* info(const std::string& levelId);
    /// The level from `a` to `b` (0..1 along its route) in a `w` × `h` picture: nullptr until drawn
    /// (asked now, drawn over the next frames).
    const Texture* stretch(const std::string& levelId, float a, float b, int w, int h);
    /// The 40 slices of the route side by side, each in a cell `cellW` × `h`: nullptr until drawn.
    const Texture* sliceStrip(const std::string& levelId, int cellW, int h);
    /// The level could not be drawn (not found, unreadable).
    bool failed(const std::string& levelId) const;
    /// Main thread, before the frame is drawn: one level loaded at most, a few milliseconds of
    /// terrain, a few pictures.
    void update();

private:
    struct Picture {
        std::string key, level;
        bool strip = false;
        float a = 0.0f, b = 1.0f;
        int w = 0, h = 0, done = 0;  // strip: cells drawn so far
        TexturePtr tex;
        bool ready = false;
        uint64_t used = 0;
    };
    struct Loaded;
    Picture* ask(const std::string& key, const std::string& level, bool strip, float a, float b, int w, int h);
    void drawSome(Picture& p);
    void drawView(Picture& p, const Rect& window, const Rect& dst);
    const ReadLevel* readyRead(const std::string& levelId) const;
    GameServices& g_;
    std::map<std::string, std::shared_future<std::shared_ptr<const ReadLevel>>> reads_;
    std::list<Picture> pictures_;
    std::unique_ptr<Loaded> loaded_;
    std::map<std::string, bool> failed_;
    uint64_t frame_ = 0, idleSince_ = 0;
};

// ------------------------------------------------------------------ sprites

/// The frame `frame` of the clip `clip` of the animation `anim` (its idle clip, else its first, when
/// it has no such clip), cut to its visible pixels, fitted in `box` (whole multiples when enlarged),
/// centred, or standing on the box's bottom (`stand`). Returns where it was drawn (empty: nothing).
Rect drawSprite(GameServices& g, const std::string& anim, const std::string& clip, int frame, const Rect& box, Color tint = Color::white(),
                bool flipX = false, bool stand = false);
/// The same, the frame at `t` seconds of the clip (looping).
Rect drawSpriteAt(GameServices& g, const std::string& anim, const std::string& clip, float t, const Rect& box, Color tint = Color::white(),
                  bool flipX = false, bool stand = false);
/// A creature of the content (its animation and tint), its `clip` at `t` seconds.
Rect drawEnemy(GameServices& g, const std::string& enemyId, const Rect& box, float t = 0.0f, const std::string& clip = "idle", bool stand = false,
               Color tintMul = Color::white());
/// A picture covering `box` (cut to the box, its proportions kept), `focusX` 0..1 the part kept.
void drawCover(GameServices& g, const Texture& tex, const Rect& box, Color tint = Color::white(), float focusX = 0.5f);
/// The banner of a planet covering `box` (nothing when it has none).
bool drawPlanetBanner(GameServices& g, const std::string& planetId, const Rect& box, Color tint = Color::white());

/// The planet of the campaign where the level is ("" when none) and its number there (1 = the first).
std::pair<std::string, int> levelPlace(const GameServices& g, const std::string& levelId);
/// Where a boss is fought first (its planet, the level and its number there), from the creatures'
/// atlas (nullptr while it is being read): empty when unknown.
struct BossPlace {
    std::string planetId, levelId;
    int levelNumber = 0;
};
BossPlace bossPlace(const GameServices& g, const CreatureAtlas* atlas, const std::string& bossId);

}  // namespace statsart
}  // namespace cosmic
