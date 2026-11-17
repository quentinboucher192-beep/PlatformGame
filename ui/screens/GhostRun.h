#pragma once

#include <functional>
#include <future>
#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/rendering/Renderer.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/GhostStore.h"
#include "saves/Settings.h"
#include "ui/screens/CosmeticFx.h"

namespace cosmic {

class ContentDatabase;
class GameWorld;
class UIContext;
struct LevelData;

/// The ghosts of a run (Ghost Racing): loads the chosen replays off the main thread, checks
/// them, races them against the player on the level timer and draws them (translucent,
/// tinted, outlined, named) with the live gap and the segments panel. Display only: the
/// ghosts never touch the simulation (no collision, no trigger, no checkpoint).
class GhostRun {
public:
    /// Starts loading `choices` for this level version (`levelHash`). `offset`: training
    /// from a checkpoint (the ghost starts that far into its run); `splitBase`: the full
    /// run's index of the first checkpoint split of the training run.
    void begin(std::vector<GhostEntry> choices, const LevelData& level, const ContentDatabase& content,
               const std::string& levelHash, const GhostSettings& settings, bool training);
    bool active() const { return !race_.empty() || loading_.valid(); }
    bool loading() const { return loading_.valid(); }
    /// Personal references for gold segments and "ahead of the record".
    void setPersonal(std::vector<float> bestSegments, std::vector<float> pbSplits, float pbTime) {
        race_.setPersonal(std::move(bestSegments), std::move(pbSplits), pbTime);
    }
    /// 2.60.3: the Sum of Best shown under the panel, full runs and section trainings together
    /// (-1: the sum of the gold segments given to setPersonal, the full runs' only).
    void setSumOfBest(float seconds) { sumOfBest_ = seconds; }
    /// Training from the checkpoint at `feet`: the ghosts start where they passed it.
    void setTrainingFrom(Vec2 feet) {
        trainingFrom_ = feet;
        hasTrainingFrom_ = true;
    }

    /// Every frame (main thread): collects what finished loading, follows the run.
    void update(const GameWorld& w, float dt);
    void finish(float runTime) { race_.finish(runTime); }

    /// Sheet of the suit a ghost wears (2.23), nullptr for the normal one: asked once per
    /// ghost, the first time it is drawn. Display only.
    using SuitSheet = std::function<TexturePtr(const GhostMeta&)>;
    void setSuitSheet(SuitSheet f) {
        suitSheet_ = std::move(f);
        sheets_.clear();
    }
    /// 2.59: the look of the trail a ghost leaves when it runs or dashes (its player's, as the
    /// game draws it: « none », a catalogue look, "srvtrail:<n>"), asked once per ghost. Drawn
    /// as translucent as the ghost. Display only.
    using TrailLook = std::function<std::string(const GhostMeta&)>;
    void setTrailLook(TrailLook f) {
        trailLook_ = std::move(f);
        trailLooks_.clear();
    }
    void drawWorld(Renderer& r, const WorldView& view, const AnimationSetPtr& set);
    /// The gap and the segments panel inside `area` (the HUD place « Fantômes », size factor
    /// `k`; `boxShown` false: hidden in the HUD settings), then the names above the ghosts,
    /// kept clear of the HUD (`keepOut`: its elements' areas) and of each other. 2.60: the box
    /// at `boxOpacity` (faded with the HUD over the action, Hud::opacity), the names never.
    void drawHud(UIContext& ui, const WorldView& view, const Rect& area, float k, bool boxShown, const Rect* keepOut,
                 int keepOutCount, float boxOpacity = 1.0f);
    /// What the box (gap, panel, status) really covered on screen the last time (empty: nothing).
    const Rect& boxDrawn() const { return boxDrawn_; }
    /// HUD editor: the place of the comparison box, without any time.
    static void drawPreview(UIContext& ui, const Rect& area, float k);

    /// Quick toggle in game: ghosts and their HUD shown or hidden.
    void setVisible(bool on) { visible_ = on; }
    bool visible() const { return visible_; }
    /// New look (colours, opacity, HUD) at once; the number of ghosts applies to the next run.
    void setSettings(const GhostSettings& s);

    const GhostRace& race() const { return race_; }
    /// Screen area of the comparison box drawn last (other HUD pieces keep clear of it).
    const Rect& hudRect() const { return hudRect_; }
    /// Ghosts refused while loading ("Astrid : Autre version du niveau.").
    const std::vector<std::string>& refused() const { return refused_; }
    /// Ghosts made on another version of the level (raced, never as an official comparison).
    int otherVersions() const;

private:
    struct Loaded {
        GhostEntry entry;
        GhostTrack track;
        bool sameVersion = true;
        std::string refused;
    };
    struct Prepared {
        std::vector<Loaded> ghosts;
        LevelProgressTrack route;
    };
    void collect();
    Color colorFor(const GhostMeta& m, int sameKindBefore) const;
    void drawNames(UIContext& ui, const WorldView& view, const Rect* keepOut, int keepOutCount);
    void drawStatus(UIContext& ui, const Rect& area, float k);
    /// The box of drawHud(): the gap and the segments panel, or the loading status.
    void drawBox(UIContext& ui, const Rect& area, float k, bool boxShown);
    /// Name of the checkpoint nearest to `feet` (its label in the level), or "Point n".
    std::string checkpointName(Vec2 feet, int fallbackIndex) const;

    GhostRace race_;
    float sumOfBest_ = -1.0f;
    GhostSettings settings_;
    std::future<Prepared> loading_;
    std::vector<Animator> anims_;
    SuitSheet suitSheet_;
    std::vector<std::pair<bool, TexturePtr>> sheets_;  // per ghost: asked yet, its suit's sheet
    TrailLook trailLook_;
    std::vector<std::pair<bool, std::string>> trailLooks_;  // per ghost: asked yet, its trail's look
    std::vector<TrailFx> trails_;                            // per ghost
    std::vector<std::string> labels_;  // "Astrid · 2:05,12" (built once)
    std::vector<std::string> refused_;
    struct Checkpoint {
        Vec2 at;
        std::string label;
    };
    std::vector<Checkpoint> checkpoints_;
    size_t seenSplits_ = 0;
    bool training_ = false;
    Vec2 trainingFrom_;
    bool hasTrainingFrom_ = false;
    bool visible_ = true;
    float recordFlash_ = 0.0f;  // "NOUVEAU RECORD" animation
    Rect hudRect_;
    Rect boxDrawn_;
};

}  // namespace cosmic
