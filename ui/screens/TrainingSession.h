#pragma once

// 2.91 — what a run gives the personal Sum of Best, and training's StartPos and measure points
// in the level (docs/GHOST_RACING.md § « Une seule somme des meilleurs… »). Owned by GameScene.
//
//  * Every attempt (a run from the level's start, a section training, an enchaînement, an attempt
//    from a StartPos) notes the checkpoints it crosses with the chronometer; when it ends (the exit,
//    the training's end, a fall, a restart, leaving), its passage joins PersonalBests once, with
//    its exact replay.
//  * 2.95 (docs/GHOST_RACING.md § « Règles d'un segment »): a boundary is the FIRST crossing of
//    its checkpoint in the attempt. A checkpoint lit only after Nova was beyond it on the route
//    (she passed it without lighting it, then fell further on and came back from the checkpoint
//    before: a respawn) or behind where she appeared (the attempt's start) is not a boundary of
//    this attempt: a respawn never starts a segment. A training of a range starting at a
//    checkpoint starts at the segment before it, its run-up (« élan », never counted, shown so):
//    the range is entered as in a run, its time starting at that entry.
//  * In training: F5 to F8 (or View held + the d-pad) ask for a mark at the next step (the
//    frame's mark flags: InputFrame::PlaceStartPos…); after the step the world gives Nova's state
//    back (GameWorld::takeMarks) and the StartPos or the measure point is kept (StartPosStore,
//    MeasureStore). The A → B chronometers follow Nova; their times are kept.
//  * Drawn: the StartPos (a pennant and Nova's translucent silhouette) and the measure gates in
//    the level; the panels (the StartPos in use, the measure, an enchaînement's splits, a gold
//    segment), always placed clear of the HUD and of each other.
//
// Display and saves only: nothing here changes the simulation (the marks are read after the
// step; the start of an attempt is SectionTrainingStart, applied by the run and its replay).

#include <future>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/rendering/Renderer.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/MeasurePoints.h"
#include "game/ghost/PersonalBests.h"
#include "game/ghost/SobPath.h"

namespace cosmic {

class GameServices;
class GameWorld;
struct StartPosEntry;
class InputManager;
class UIContext;
struct LevelData;

class TrainingSession {
public:
    struct Setup {
        std::string levelKey;        // GhostStore::levelKey (empty: nothing is kept)
        bool counts = false;         // its passages join the personal Sum of Best
        bool training = false;       // a section training (a segment, an enchaînement, from a StartPos)
        std::string from, to;        // the training's range ("" = the level's start / its exit)
        int startPosId = 0;          // the attempt starts from this StartPos (0: none)
        SectionTrainingStart start;  // how it started (its replay's start)
        int attempt = 0;             // its number this session (« ESSAI n »)
        /// 2.95: the attempt starts at this boundary ("" = the level's start), the segment before
        /// `from` played as its run-up (« élan », never counted). nullopt: it starts at `from` (or
        /// at the StartPos).
        std::optional<std::string> runUp;
    };
    void begin(GameServices& g, const LevelData& level, Setup s);
    const Setup& setup() const { return s_; }
    /// The source this attempt gives its bests: Partie, Entraînement (one segment), Enchaînement,
    /// StartPos.
    BestSource source() const;
    /// An enchaînement: a training over more than one segment of the route.
    bool chain() const;

    /// The run reached checkpoint `id` at chronometer `t` (the world's « checkpoint » event): its
    /// first crossing, once the route tells it was not passed before (else it is no boundary).
    void onCheckpoint(const std::string& id, float t);
    /// 2.95: the segments this attempt counts, resolved since the last call ((from, to), seconds):
    /// the statistics' segment times (a run of a classic level), by the same rules.
    std::vector<std::pair<PersonalBests::Range, float>> takeSegments();
    /// 2.95: in the run-up (the segment before the range trained), the range not entered yet.
    bool inRunUp() const;
    /// 2.95: the run-up's start ("" the level's start); only when inRunUp() or after it.
    std::string runUpFrom() const { return s_.runUp.value_or(std::string{}); }
    bool hasRunUp() const { return s_.runUp.has_value() && !fromStartPos(); }
    /// An attempt from a StartPos (2.95: on the SOB path, its replay played up to it).
    bool fromStartPos() const { return s_.startPosId > 0 || s_.start.startPos.has_value(); }
    /// 2.95: the chronometer when the attempt entered the range trained (0 without a run-up; -1
    /// while not entered, or when that entry was not seen: the range then does not count).
    float entryTime() const;
    /// 2.95: the range trained ended at chronometer `endT`: its time from its entry (-1 when the
    /// entry was not seen). Waits for the route when it is still computed.
    float rangeTime(float endT);
    /// Every frame (training): the keys and the gamepad's chord (View held + the d-pad). True when
    /// View was released alone (restart the attempt).
    bool update(InputManager& in, float dt);
    /// The mark flags asked for since the last step (InputFrame), for the next frame.
    uint8_t takeFlags();
    /// After each step: the world's marks (kept), the measure chronometers.
    void afterStep(GameWorld& w);
    /// The attempt ends: its passage joins the personal bests (once). `exitTime` > 0: the level's
    /// exit reached then; `replay`: its exact replay (encoded), nullptr: none.
    PersonalBests::Added commit(float exitTime, const std::vector<uint8_t>* replay);
    bool committed() const { return committed_; }
    /// The Sum of Best when the attempt started, and now with the gold segments of this attempt.
    float sobAtStart() const { return sobAtStart_; }
    float sobNow() const;
    /// The best of a range when the attempt started (-1: none).
    float bestAtStart(const std::string& from, const std::string& to) const;

    /// The StartPos and the measure gates in the level (world view).
    void drawWorld(Renderer& r, const WorldView& view, const AnimationSetPtr& set);
    /// The names of the gates and of the StartPos (over the level, `view`), then the panels, placed
    /// clear of `keep` (the HUD's areas) and of each other.
    void drawHud(UIContext& ui, const GameWorld& w, const WorldView& view, std::vector<Rect> keep);
    /// Where the panels were drawn last (others keep clear of them).
    const std::vector<Rect>& drawn() const { return drawn_; }
    /// The keys' line for the training tag (« F5 poser · F8 mesure · … »), empty out of training.
    std::string keysLine(InputManager& in) const;
    /// « Déplacer A / B » (pause panel): the next measure marks place this pair's A, then its B.
    void moveNext(int pairId) { movePair_ = pairId, moveB_ = false; }
    int movingPair() const { return movePair_; }
    /// The route's progress along the level (0..1) at `feet`; -1 while it is computed.
    float fractionAt(Vec2 feet) const;
    /// « Clairière → Sous-bois · 3 % »: where `feet` lies on the route ("" while it is computed).
    std::string whereOnRoute(Vec2 feet) const;
    /// A boundary's name: "" (`start` true) the level's start, "" (false) the exit, else the
    /// checkpoint's label (« Point n » without one).
    std::string pointName(const std::string& id, bool start) const;
    const std::vector<std::string>& route() const { return route_; }
    /// 2.95: the SOB path of this level version (nullptr while it is read): where a StartPos lies.
    const SobPath* sobPath() const { return path_.path(); }
    /// 2.95: an attempt can start from it (on the path, its segment's best has its replay).
    bool startPosUsable(const StartPosEntry& e) const;

private:
    struct Toast {
        std::string title, line;
        float t = 0.0f;
        bool gold = false;
    };
    void toast(std::string title, std::string line, bool gold = false);
    void collectTrack();
    /// The checkpoints lit, checked against where Nova was before (`wait`: the route computed first).
    void resolve(bool wait);
    /// A boundary of this attempt (a checkpoint, the exit when `id` is empty and `exit`): the
    /// segment it ends, when it counts, for the statistics and the live gold.
    void onCrossed(const std::string& id, float t, bool exit);
    /// What the attempt crossed so far (its passage for PersonalBests).
    BestPassage passage(float exitTime) const;
    /// The first free place among `candidates` (the least covered when none is free).
    static Rect placeClear(const std::vector<Rect>& candidates, const std::vector<Rect>& keep, float margin);
    void drawStartPosPanel(UIContext& ui, std::vector<Rect>& keep);
    void drawMeasurePanel(UIContext& ui, std::vector<Rect>& keep);
    void drawChainPanel(UIContext& ui, const GameWorld& w, std::vector<Rect>& keep);
    void drawToast(UIContext& ui, std::vector<Rect>& keep);

    GameServices* g_ = nullptr;
    const LevelData* level_ = nullptr;
    Setup s_;
    std::vector<std::string> route_;  // the checkpoints in the route's order
    std::map<std::string, std::string> labels_;
    std::future<LevelProgressTrack> trackFuture_;
    LevelProgressTrack track_;
    std::map<std::string, float> cpAt_;  // checkpoint → its place along the route (0..1)
    // The attempt.
    std::vector<std::pair<std::string, float>> crossed_;  // its boundaries: first crossings seen, in order
    struct Lit {
        std::string id;
        float t = 0.0f;
        size_t before = 0;  // the places of beyond_ that came before it
    };
    std::vector<Lit> pending_;           // lit, to be checked once the route is known
    std::set<std::string> seen_;         // every checkpoint lit in this attempt (a boundary or not)
    std::vector<Vec2> beyond_;           // where Nova appeared without crossing what lies behind: its start, each fall
    bool started_ = false;
    int deaths_ = 0;
    float exitAt_ = -1.0f;               // the exit reached (its chronometer)
    std::vector<std::pair<PersonalBests::Range, float>> segmentsOut_;
    bool committed_ = false;
    float sobAtStart_ = -1.0f;
    float goldGain_ = 0.0f;                         // gold segments of this attempt (seconds gained on the SOB)
    std::map<PersonalBests::Range, float> atStart_;  // the bests the attempt compares with
    // Marks.
    uint8_t flags_ = 0;
    bool backHeld_ = false, chordUsed_ = false;
    bool dpadWas_[4] = {false, false, false, false};
    int movePair_ = 0;
    bool moveB_ = false;
    // Measures.
    std::map<int, MeasureTimer> timers_;
    int shownPair_ = 0;      // the pair the panel shows (running, else the last timed)
    float lastTime_ = -1.0f;  // its last A → B time
    float chrono_ = 0.0f;     // the run's chronometer after the last step
    bool measuresDirty_ = false;
    std::vector<Toast> toasts_;
    std::vector<Rect> drawn_;
    SobPathCache path_;  // 2.95
    std::vector<Animator> silhouettes_;
};

}  // namespace cosmic
