#pragma once

#include <array>
#include <string>
#include <vector>

#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShapeProbe.h"
#include "game/gameplay/LevelProgress.h"
#include "saves/HudLayout.h"

namespace cosmic {

class GameWorld;
class UIContext;
struct PowerDefinition;
struct LevelRecord;
struct GameplaySettings;

/// In-game heads-up display: health, energy, powers with cooldowns and button prompts,
/// chronometer (with medal targets and split comparison), collectibles, boss gauge,
/// messages, dialogues and cinematic overlays.
class Hud {
public:
    void update(GameWorld& w, float dt);
    void render(UIContext& ui, GameWorld& w, const WorldView& view, const LevelRecord* record,
                const GameplaySettings& settings);

    /// Where an element is drawn: its anchor on screen, its size factor and, for the
    /// progress bar, the length of the rail.
    struct Place {
        Vec2 o;
        float k = 1.0f;
        float width = 0.0f;
    };
    Place place(UIContext& ui, const GameWorld& w, const GameplaySettings& s, HudElement e) const;
    /// Screen area an element covers at a place (HUD editor, overlap checks); empty when
    /// the element has nothing to show in this world.
    Rect area(UIContext& ui, const GameWorld& w, HudElement e, const Place& p) const;
    /// « Ennemis vaincus : X / Y » (2.51): the text of the element, and whether the run has
    /// beaten every enemy of the level as « Aucun survivant » counts them (kills >= enemies).
    static std::string enemiesText(int kills, int total) { return "Ennemis vaincus : " + std::to_string(kills) + " / " + std::to_string(total); }
    static bool enemiesCleared(int kills, int total) { return total > 0 && kills >= total; }
    /// « Embuscade : N ennemis restants » (2.62.2), with « · vague k / n » when it has several
    /// waves: the enemies of the ambush still to beat (CombatZoneEntity::enemiesLeft).
    static std::string ambushText(int left, int wave, int waves);
    /// Anchor of a place whose area has its top-left corner at `topLeft` (dragging).
    Vec2 anchorForArea(UIContext& ui, const GameWorld& w, HudElement e, const Place& p, Vec2 topLeft) const;

    /// A daily challenge run (2.32): under the timer, the next tier still reachable (Or,
    /// Argent) instead of the level's medals. Seconds; `goldNoDamage`: Or is lost with a hit.
    /// `finish` (2.41, « Finir le niveau »): no time tier, Or is the finish (the progress bar
    /// shows the share of the route).
    void setChallengeTiers(float silver, float gold, bool goldNoDamage, bool finish = false) {
        tierSilver_ = silver;
        tierGold_ = gold;
        tierGoldNoDamage_ = goldNoDamage;
        tierFinish_ = finish;
    }

    /// 2.60, « Effacer le HUD quand il cache l'action » (GameplaySettings::fadeHud): a block
    /// that hides the boss, Nova or an attack of the boss fades (HudFade). The blocks: the
    /// elements of the HUD (HudElement order), then the status chips (drawStatus). The phase
    /// announcements and the level's messages never fade; the boss's bar only gives way to Nova.
    static constexpr int kFadeBlocks = kHudElements + 1;
    static constexpr int kStatusBlock = kHudElements;
    /// Before the world is drawn: the probe telling which blocks (where they were drawn the last
    /// frame) the boss's attacks touch — its projectiles, telegraphs and lasting effects
    /// (RenderOptions::threats). nullptr when the option is off or no boss is fought. render()
    /// reads it back.
    CoverageProbe* threatProbe(const GameWorld& w, const GameplaySettings& s);
    /// The opacity of a block now (1: not faded).
    float opacity(HudElement e) const { return fade_[static_cast<size_t>(e)]; }
    /// Where a block drawn outside render() was drawn this frame (the ghosts' box: Splits).
    void setDrawn(HudElement e, const Rect& r) { drawn_[static_cast<size_t>(e)] = r; }
    /// Where each block was drawn the last frame (empty: not drawn).
    const Rect& drawn(int block) const { return drawn_[static_cast<size_t>(block)]; }
    /// The HUD editor: its blocks never fade (they are what is being placed).
    void setFadeAllowed(bool on) { fadeAllowed_ = on; }
    /// The HUD editor (2.62.2): the ambush's counter shows the level's first ambush, fought or not.
    void setEditorPreview(bool on) { editorPreview_ = on; }
    /// Where Nova and the active boss show on screen: the visible pixels of the frame drawn
    /// (ResourceManager::visibleBox). Empty when not shown.
    static Rect novaOnScreen(GameWorld& w, const WorldView& view);
    static Rect bossOnScreen(GameWorld& w, const WorldView& view);
    /// Reads the visible boxes of Nova's and the level's bosses' frames before they are needed.
    static void warmUp(GameWorld& w);

private:
    std::array<float, kFadeBlocks> fade_ = [] {
        std::array<float, kFadeBlocks> a{};
        a.fill(1.0f);
        return a;
    }();
    std::array<bool, kFadeBlocks> covered_{};
    std::array<Rect, kFadeBlocks> drawn_{};
    CoverageProbe probe_;
    bool probeArmed_ = false;
    bool fadeAllowed_ = true;
    bool editorPreview_ = false;
    /// Draws a block at its opacity and keeps where it was drawn.
    template <class F>
    void drawBlock(UIContext& ui, int block, F&& draw);

    float tierSilver_ = 0.0f, tierGold_ = 0.0f;
    bool tierGoldNoDamage_ = false;
    bool tierFinish_ = false;
    void drawVitals(UIContext& ui, GameWorld& w, Vec2 o, float k);
    void drawPowers(UIContext& ui, GameWorld& w, Vec2 o, float k);
    void drawTimer(UIContext& ui, GameWorld& w, const LevelRecord* record, Vec2 o, float k);
    void drawCollectibles(UIContext& ui, GameWorld& w, Vec2 o, float k);
    void drawBoss(UIContext& ui, GameWorld& w, Vec2 o, float k);
    /// « Ennemis vaincus : X / Y » (2.51): the run's kills (RunStats::kills) against the
    /// level's enemies (RunStats::totalEnemies), as « Aucun survivant » counts them. Display only.
    void drawEnemies(UIContext& ui, GameWorld& w, const Place& p);
    /// The ambush being fought (2.62.2): its enemies left and its wave. Display only.
    void drawAmbush(UIContext& ui, GameWorld& w, const Place& p);
    /// 2.57: what the Liche does to the fight, as long as it lasts: Nova cursed (her jumps
    /// heavy, the seconds left), the boss's soul in its phylactery (it cannot be hurt). Chips
    /// on the left of the screen, one under the other, measured and placed where they cover no
    /// other element of the HUD (drawn after the world: never under a sprite).
    void drawStatus(UIContext& ui, GameWorld& w, const GameplaySettings& settings);
    void drawMessages(UIContext& ui, GameWorld& w);
    /// How to use a power just picked up, centred on `center`: the key of its slot, or its
    /// move / « passif » in the dashed gold frame of the powers off their slot key.
    void drawPowerUse(UIContext& ui, const PowerDefinition& def, int slot, Vec2 center, float h, float alpha);
    void drawCinematic(UIContext& ui, GameWorld& w);
    void drawInteractPrompt(UIContext& ui, GameWorld& w, const WorldView& view);
    /// Level progress bar (placement 1 top, 2 bottom): route %, checkpoints, ambushes,
    /// bosses and the exit portal.
    void drawProgress(UIContext& ui, GameWorld& w, Vec2 o, float k, float width);

    float displayedHealth_ = 5.0f;
    float displayedEnergy_ = 100.0f;
    float bossBarDelayed_ = 1.0f;
    float shardPulse_ = 0.0f;
    int lastShards_ = 0;
    int lastSecond_ = -1;
    float time_ = 0.0f;
    float skipHold_ = 0.0f;

    LevelProgressTrack track_;
    size_t trackHint_ = 0;
    float progress_ = 0.0f;      // current place on the route (0..1)
    float bestProgress_ = 0.0f;  // furthest place reached
    float shownProgress_ = 0.0f;

public:
    void setProgressTrack(LevelProgressTrack t) {
        track_ = std::move(t);
        trackHint_ = 0;
    }
    bool hasProgressTrack() const { return track_.valid(); }
    /// Runners shown on the progress bar instead of the player's head (replay comparison):
    /// the first one stands on the rail, the second one below it, each with its letter.
    struct ProgressRacer {
        float at = 0.0f;  // 0..1 along the route
        Color color{57, 240, 216, 255};
        std::string letter;
        const Texture* texture = nullptr;  // its current animation frame
        Rect src;
        bool flip = false;
    };
    void setProgressRacers(std::vector<ProgressRacer> r) { racers_ = std::move(r); }
    /// The progress bar alone, centred on `o` (`width` px of rail), e.g. outside the HUD.
    void drawProgressAt(UIContext& ui, GameWorld& w, Vec2 o, float k, float width) { drawProgress(ui, w, o, k, width); }
    /// Room the progress bar needs around its rail (above, below), runners included.
    static float progressAbove(float scaledUnit, bool racing) { return scaledUnit * (racing ? 96.0f : 40.0f); }
    static float progressBelow(float scaledUnit, bool racing) { return scaledUnit * (racing ? 96.0f : 46.0f); }

private:
    std::vector<ProgressRacer> racers_;

public:
    /// 2.62, a team (docs/COOP.md): who plays (names, colours, links) for the team panel, the
    /// labels over the souls and the marks of the progress bar. Display only.
    struct TeamInfo {
        std::vector<std::string> names;  // one per player
        std::vector<Color> colors;
        std::vector<int> pingMs;         // online: each player's link (-1: unknown / local)
        int promptPlayer = 0;            // whose interaction prompt is shown (the local player)
        std::string waitingFor;          // online: « En attente de … » (the lockstep waits)
        std::vector<std::string> deviceNotes;  // 2.65: a pad's low battery (when SDL says it), a pad unplugged online
    };
    void setTeam(TeamInfo t) { team_ = std::move(t); }
    const TeamInfo& team() const { return team_; }
    /// Where the team panel was drawn last (empty when none).
    const Rect& teamPanelRect() const { return teamPanel_; }
    /// 2.79: where the names over the players (and their souls) were drawn last (display: what else is
    /// written over the world keeps out of them — the thoughts of a raid test's simulated players).
    const std::vector<Rect>& teamLabelRects() const { return teamLabels_; }
    /// Team panel's height for `players` rows (layout checks).
    static float teamPanelHeight(float unit, int players, int notices);

private:
    TeamInfo team_;
    bool raidView_ = false;
    Rect teamPanel_;
    std::vector<Rect> teamLabels_;  // 2.79
    std::vector<size_t> teamHints_;
    std::vector<float> teamAt_;  // each player's place on the route (0..1)
    void drawTeamPanel(UIContext& ui, GameWorld& w);
    void drawTeamLabels(UIContext& ui, GameWorld& w, const WorldView& view);
    std::string teamName(int i) const;
    Color teamColor(int i) const;

public:
    float skipProgress() const { return skipHold_; }
    void setSkipHold(float v) { skipHold_ = v; }
    /// 2.65 (raids, docs/RAIDS.md): the vitals and powers of the current player (the scene draws
    /// it in a GameWorld::PlayerScope of this computer's player) in place of the team panel; the
    /// raid scene draws the group panel of 8 itself. Names over the players stay.
    void setRaidView(bool on) { raidView_ = on; }
};

}  // namespace cosmic
