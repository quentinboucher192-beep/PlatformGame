#include "ui/screens/TrainingSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "engine/input/InputManager.h"
#include "game/GameServices.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/StartPosStore.h"
#include "game/levels/LevelData.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

long long msOf(float s) { return std::llround(static_cast<double>(s) * 1000.0); }

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGold{255, 214, 90, 255};
const Color kGate{255, 200, 80, 255};

/// 2.95: Nova was beyond a checkpoint when she was further than this along the route (pixels):
/// a fall right next to it, before lighting it, does not make it « passed ».
constexpr float kBeyond = 48.0f;

/// The name of an action's key (keyboard) or « Vue + croix » (gamepad), for the hints.
std::string keyOf(InputManager& in, Action a) {
    const auto& keys = in.bindings().keys[static_cast<int>(a)];
    const SDL_Scancode k = keys[0] != SDL_SCANCODE_UNKNOWN ? keys[0] : keys[1];
    return k != SDL_SCANCODE_UNKNOWN ? keyName(k) : std::string("—");
}

}  // namespace

// ------------------------------------------------------------------ the attempt

void TrainingSession::begin(GameServices& g, const LevelData& level, Setup s) {
    g_ = &g;
    level_ = &level;
    s_ = std::move(s);
    crossed_.clear();
    pending_.clear();
    seen_.clear();
    beyond_.clear();
    started_ = false;
    deaths_ = 0;
    exitAt_ = -1.0f;
    segmentsOut_.clear();
    committed_ = false;
    goldGain_ = 0.0f;
    for (const LevelObject& o : level.objects)
        if (o.type == "checkpoint") labels_[o.id] = o.propStr("label");
    if (auto r = g.bests.route(s_.levelKey)) route_ = *r;
    sobAtStart_ = s_.levelKey.empty() ? -1.0f : g.bests.sob(s_.levelKey);
    // The route of the level (the checkpoints' order, where a StartPos lies): off the main thread.
    trackFuture_ = std::async(std::launch::async, [l = level, &content = g.content] { return buildProgressTrack(l, content); });
}

void TrainingSession::collectTrack() {
    if (!trackFuture_.valid() || trackFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    track_ = trackFuture_.get();
    if (!track_.valid() || !level_) return;
    std::vector<std::pair<float, std::string>> cps;
    for (const LevelObject& o : level_->objects)
        if (o.type == "checkpoint") {
            const float at = track_.fractionAt(o.rect.bottomCenter());
            cpAt_[o.id] = at;
            cps.push_back({at, o.id});
        }
    std::sort(cps.begin(), cps.end());
    std::vector<std::string> ids;
    for (const auto& [at, id] : cps) ids.push_back(id);
    // The level's own order: the route of its personal Sum of Best (its old gold segments too).
    if (!s_.levelKey.empty() && g_) {
        auto known = g_->bests.route(s_.levelKey);
        if (!known || *known != ids) g_->bests.setRoute(s_.levelKey, ids);
    }
    route_ = std::move(ids);
}

BestSource TrainingSession::source() const {
    if (!s_.training) return BestSource::Run;
    if (fromStartPos()) return BestSource::StartPos;
    return chain() ? BestSource::Chain : BestSource::Training;
}

bool TrainingSession::chain() const {
    if (!s_.training || fromStartPos()) return false;
    const int n = static_cast<int>(route_.size());
    auto pos = [&](const std::string& id, bool from) -> int {
        if (id.empty()) return from ? 0 : n + 1;
        auto it = std::find(route_.begin(), route_.end(), id);
        return it == route_.end() ? -1 : static_cast<int>(it - route_.begin()) + 1;
    };
    const int a = pos(s_.from, true), b = pos(s_.to, false);
    return a >= 0 && b >= 0 && b - a > 1;
}

void TrainingSession::onCheckpoint(const std::string& id, float t) {
    // Its first crossing only (a checkpoint lights once; the start's, lit before, is not one).
    if (committed_ || id.empty() || !seen_.insert(id).second) return;
    pending_.push_back({id, t, beyond_.size()});
    resolve(false);
}

void TrainingSession::resolve(bool wait) {
    if (pending_.empty()) return;
    if (!track_.valid() && trackFuture_.valid()) {
        if (!wait) return;  // checked once the route is known
        trackFuture_.wait();
        collectTrack();
    }
    for (const Lit& c : pending_) {
        // Nova was further than it on the route before it was lit (a fall beyond it, then back from
        // the checkpoint before it; or the attempt started beyond it): she passed it unseen, its
        // first crossing is unknown. Not a boundary: a respawn never starts a segment.
        bool passed = false;
        if (auto at = cpAt_.find(c.id); track_.valid() && at != cpAt_.end()) {
            const float margin = kBeyond / std::max(1.0f, track_.length);
            for (size_t k = 0; k < c.before && k < beyond_.size() && !passed; ++k) passed = track_.fractionAt(beyond_[k]) > at->second + margin;
        }
        if (passed) continue;
        crossed_.push_back({c.id, c.t});
        onCrossed(c.id, c.t, false);
    }
    pending_.clear();
}

BestPassage TrainingSession::passage(float exitTime) const {
    BestPassage p;
    // Where it started: a StartPos, a training's run-up or range start, the level's start.
    const std::string start = s_.runUp ? *s_.runUp : s_.training ? s_.from : std::string{};
    if (fromStartPos()) {
        p.from = BestPassage::From::Elsewhere;
    } else if (!start.empty()) {
        p.from = BestPassage::From::Checkpoint;
        p.startCheckpoint = start;
    } else {
        p.from = BestPassage::From::LevelStart;
    }
    if (hasRunUp()) p.countFrom = s_.from;  // the run-up never counts
    p.checkpoints = crossed_;
    p.exitTime = exitTime > 0.0f ? exitTime : -1.0f;
    return p;
}

void TrainingSession::onCrossed(const std::string& id, float t, bool exit) {
    if (!g_ || s_.levelKey.empty() || !s_.counts) return;
    // The segment that ends here, when the rules count it (entered by playing the one before it
    // whole, never in a run-up): the shortest range that ends here.
    const std::vector<PersonalBests::Span> spans = g_->bests.counted(s_.levelKey, passage(exit ? t : -1.0f));
    const PersonalBests::Span* seg = nullptr;
    for (const PersonalBests::Span& sp : spans)
        if (sp.range.second == (exit ? std::string{} : id) && std::abs(sp.end - t) < 1e-4f && (!seg || sp.start > seg->start)) seg = &sp;
    if (!seg) return;
    const float secs = seg->end - seg->start;
    if (segmentsOut_.size() < 256) segmentsOut_.push_back({seg->range, secs});
    if (!s_.training) return;
    const PersonalBests::Record* rec = g_->bests.record(s_.levelKey, seg->range);
    if (!rec || rec->best <= 0.0f || msOf(secs) >= msOf(rec->best)) return;
    // A gold segment: counted at once in the Sum of Best shown (the store takes it at the end).
    const float gain = rec->best - secs;
    goldGain_ += gain;
    std::string line = pointName(seg->range.first, true) + " → " + pointName(seg->range.second, false) + " : " + formatGhostTime(secs);
    if (sobNow() > 0.0f) line += " · SOB : " + formatGhostTime(sobNow());
    toast("SEGMENT D'OR · " + formatGhostDelta(-gain), line, true);
    g_->uiSound("medal", 0.5f);
}

std::vector<std::pair<PersonalBests::Range, float>> TrainingSession::takeSegments() { return std::exchange(segmentsOut_, {}); }

bool TrainingSession::inRunUp() const { return hasRunUp() && !seen_.count(s_.from) && exitAt_ < 0.0f; }

float TrainingSession::entryTime() const {
    if (!hasRunUp()) return 0.0f;
    for (const auto& [id, t] : crossed_)
        if (id == s_.from) return t;
    return -1.0f;
}

float TrainingSession::rangeTime(float endT) {
    resolve(true);
    const float entry = entryTime();
    return entry < 0.0f || endT < entry ? -1.0f : endT - entry;
}

float TrainingSession::sobNow() const { return sobAtStart_ > 0.0f ? sobAtStart_ - goldGain_ : -1.0f; }

float TrainingSession::bestAtStart(const std::string& from, const std::string& to) const {
    if (!g_ || s_.levelKey.empty()) return -1.0f;
    const PersonalBests::Record* r = g_->bests.record(s_.levelKey, from, to);
    return r ? r->best : -1.0f;
}

PersonalBests::Added TrainingSession::commit(float exitTime, const std::vector<uint8_t>* replay) {
    PersonalBests::Added out;
    if (committed_ || !g_) return out;
    resolve(true);  // every checkpoint lit is checked first (the route computed)
    committed_ = true;
    if (measuresDirty_) g_->measures.save();
    measuresDirty_ = false;
    if (!s_.counts || s_.levelKey.empty()) return out;
    BestAttempt a;
    a.passage = passage(exitTime);
    a.source = source();
    a.date = FileUtil::nowString();
    a.replay = replay;
    a.setup = s_.start;
    out = g_->bests.add(s_.levelKey, a);
    g_->bests.save();
    return out;
}

// ------------------------------------------------------------------ marks and measures

bool TrainingSession::update(InputManager& in, float dt) {
    for (Toast& t : toasts_) t.t += dt;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [](const Toast& t) { return t.t > 3.2f; }), toasts_.end());
    if (!s_.training) return false;
    // 2.95: the SOB path (where a StartPos may lie), read off the main thread; the StartPos of
    // before placed on it when they lie near it.
    if (g_ && !s_.levelKey.empty()) {
        path_.update(g_->bests, s_.levelKey);
        if (path_.changed() && path_.path() && reanchorStartPos(g_->startPositions, s_.levelKey, *path_.path()) > 0) g_->startPositions.save();
    }
    if (in.menuPressed(Action::StartPosPlace)) flags_ |= InputFrame::PlaceStartPos;
    if (in.menuPressed(Action::StartPosPrev)) flags_ |= InputFrame::PrevStartPos;
    if (in.menuPressed(Action::StartPosNext)) flags_ |= InputFrame::NextStartPos;
    if (in.menuPressed(Action::MeasurePlace)) flags_ |= InputFrame::PlaceMeasure;
    // The gamepad: View held + the d-pad (down: place, left / right: previous / next, up: a
    // measure point); View released alone restarts the attempt. 2.93: View is the button of
    // « Recommencer » (Paramètres › Contrôles), whichever the player chose.
    const bool back = in.padHeld(Action::Restart);
    static constexpr SDL_GamepadButton kDirs[4] = {SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                                                   SDL_GAMEPAD_BUTTON_DPAD_UP};
    static constexpr uint8_t kFlags[4] = {InputFrame::PlaceStartPos, InputFrame::PrevStartPos, InputFrame::NextStartPos, InputFrame::PlaceMeasure};
    for (int i = 0; i < 4; ++i) {
        const bool down = in.rawPadButton(kDirs[i]);
        if (back && down && !dpadWas_[i]) {
            flags_ |= kFlags[i];
            chordUsed_ = true;
        }
        dpadWas_[i] = down;
    }
    bool restart = false;
    if (!back && backHeld_) {
        restart = !chordUsed_;
        chordUsed_ = false;
    }
    backHeld_ = back;
    return restart;
}

uint8_t TrainingSession::takeFlags() { return std::exchange(flags_, uint8_t{0}); }

void TrainingSession::toast(std::string title, std::string line, bool gold) {
    toasts_.push_back({std::move(title), std::move(line), 0.0f, gold});
    if (toasts_.size() > 2) toasts_.erase(toasts_.begin());
}

void TrainingSession::afterStep(GameWorld& w) {
    if (!g_) return;
    collectTrack();
    chrono_ = w.timer().elapsed();
    // 2.95: where Nova appeared without crossing what lies behind her (the attempt's start, the
    // place of each fall: she comes back from a checkpoint before it), then the checkpoints lit
    // meanwhile checked against it, the exit noted.
    if (!started_ || w.stats().deaths > deaths_) {
        if (beyond_.size() < 4096) beyond_.push_back(w.player().feet());
        started_ = true;
        deaths_ = w.stats().deaths;
    }
    resolve(false);
    if (exitAt_ < 0.0f && !committed_ && (w.state() == WorldState::Outro || w.state() == WorldState::Completed)) {
        exitAt_ = chrono_;  // the chronometer stops at the exit
        resolve(true);
        onCrossed({}, exitAt_, true);
    }
    const std::string& key = s_.levelKey;
    for (const TrainingMark& m : w.takeMarks()) {
        if (!s_.training) continue;
        if (key.empty()) {  // 2.95: said, never silently dropped
            toast("Startpos et mesures indisponibles", "Ce niveau n'a pas de version enregistrée (un test de l'éditeur).");
            continue;
        }
        const std::string now = FileUtil::nowString();
        if (m.flag == InputFrame::PlaceStartPos) {
            if (!m.valid) {
                toast("Startpos impossible ici", "Nova doit être en jeu, visible et seule.");
                continue;
            }
            // 2.95: on the SOB path only, at its point nearest to Nova (an attempt from it plays the
            // replay that holds that segment's best up to there): never where it was never crossed.
            const SobPath* path = path_.path();
            if (!path) {
                toast("Tracé de la SOB en préparation", "Réessayez dans un instant.");
                continue;
            }
            const auto pt = path->nearest(m.state.feet, kSobPathSnap);
            if (!pt) {
                toast("Pas de startpos ici", path->empty() ? "Aucun tracé de la SOB : terminez le niveau ou entraînez ses segments d'abord."
                                                           : "Elle se pose sur le tracé de la SOB : ce passage n'a jamais été parcouru, ou Nova en est trop loin.");
                continue;
            }
            if (const int other = g_->startPositions.occupied(key, pt->at, kSobPathSame)) {
                toast("Une startpos est déjà là", "SP " + std::to_string(g_->startPositions.rank(key, other) + 1) + " : une seule startpos par endroit.");
                continue;
            }
            const SobPathSegment& seg = path->segments[static_cast<size_t>(pt->segment)];
            const int id = g_->startPositions.addOnPath(key, pt->at, seg.range.first, seg.range.second, fractionAt(pt->at), now, false, kSobPathSame);
            if (id == 0) {
                toast("Trop de startpos", "32 au plus par version du niveau : effacez-en dans Pause › Startpos et mesures.");
                continue;
            }
            g_->startPositions.save();
            const std::string where = whereOnRoute(pt->at);
            toast("Startpos posée : SP " + std::to_string(g_->startPositions.rank(key, id) + 1),
                  (where.empty() ? std::string() : where + " · ") + "Recommencer repart d'ici");
        } else if (m.flag == InputFrame::PrevStartPos || m.flag == InputFrame::NextStartPos) {
            const int id = g_->startPositions.cycle(key, m.flag == InputFrame::NextStartPos ? 1 : -1);
            if (id == 0) {
                toast("Aucune startpos", "Posez-en une d'abord.");
                continue;
            }
            g_->startPositions.save();
            const StartPosEntry* e = g_->startPositions.find(key, id);
            toast("SP " + std::to_string(g_->startPositions.rank(key, id) + 1) + " active",
                  e && startPosUsable(*e) ? std::string("Le prochain essai part d'elle.")
                                          : std::string("À replacer sur le tracé de la SOB : le prochain essai part du départ de l'entraînement."));
        } else if (m.flag == InputFrame::PlaceMeasure) {
            if (!m.valid) {
                toast("Point de mesure impossible ici", "Nova doit être en jeu, visible et seule.");
                continue;
            }
            if (movePair_ > 0 && g_->measures.find(key, movePair_)) {
                g_->measures.move(key, movePair_, moveB_, m.state.feet);
                timers_.erase(movePair_);
                const std::string n = std::to_string(movePair_);
                if (!moveB_) {
                    moveB_ = true;
                    toast("A" + n + " déplacé", "Posez maintenant B" + n + ".");
                } else {
                    movePair_ = 0;
                    toast("B" + n + " déplacé", "Ses anciens temps ne se comparent plus : oubliés.");
                }
            } else {
                movePair_ = 0;
                const int id = g_->measures.place(key, m.state.feet, now);
                if (id == 0) {
                    toast("Trop de points de mesure", "16 paires au plus par version du niveau.");
                    continue;
                }
                const MeasurePair* p = g_->measures.find(key, id);
                const std::string n = std::to_string(id);
                if (p && p->complete()) toast("B" + n + " posé", "Franchir A" + n + " lance le chrono, B" + n + " l'arrête.");
                else toast("A" + n + " posé", "Posez B" + n + " plus loin (même touche).");
                timers_.erase(id);
            }
            g_->measures.save();
        }
    }
    // The A → B chronometers, in training only.
    if (!s_.training || key.empty()) return;
    const Vec2 feet = w.player().feet();
    for (const MeasurePair& p : g_->measures.list(key)) {
        if (!p.b) continue;
        auto it = timers_.find(p.id);
        if (it == timers_.end()) it = timers_.emplace(p.id, MeasureTimer(p.a, *p.b)).first;
        if (auto d = it->second.feed(chrono_, feet)) {
            const bool best = g_->measures.addTime(key, p.id, *d, FileUtil::nowString());
            measuresDirty_ = true;
            lastTime_ = *d;
            shownPair_ = p.id;
            if (best) g_->uiSound("medal", 0.4f);
        }
        if (it->second.running()) shownPair_ = p.id;
    }
}

float TrainingSession::fractionAt(Vec2 feet) const { return track_.valid() ? track_.fractionAt(feet) : -1.0f; }

bool TrainingSession::startPosUsable(const StartPosEntry& e) const {
    if (!g_ || !e.onPath) return false;
    const PersonalBests::Record* r = g_->bests.record(s_.levelKey, e.from, e.to);
    return r && r->replay.has();
}

std::string TrainingSession::pointName(const std::string& id, bool start) const {
    if (id.empty()) return start ? "Départ" : "Arrivée";
    auto it = labels_.find(id);
    if (it != labels_.end() && !it->second.empty()) return it->second;
    auto r = std::find(route_.begin(), route_.end(), id);
    return r == route_.end() ? std::string("Point de passage") : "Point " + std::to_string(r - route_.begin() + 1);
}

std::string TrainingSession::whereOnRoute(Vec2 feet) const {
    const float f = fractionAt(feet);
    if (f < 0.0f) return {};
    // The segment it lies in, and how far along it.
    std::string from, to;
    float a = 0.0f, b = 1.0f;
    for (const std::string& id : route_) {
        auto it = cpAt_.find(id);
        if (it == cpAt_.end()) continue;
        if (it->second <= f) {
            from = id;
            a = it->second;
        } else {
            to = id;
            b = it->second;
            break;
        }
    }
    const int pct = b > a ? std::clamp(static_cast<int>(std::lround((f - a) / (b - a) * 100.0f)), 0, 100) : 0;
    return pointName(from, true) + " → " + pointName(to, false) + " · " + std::to_string(pct) + " %";
}

std::string TrainingSession::keysLine(InputManager& in) const {
    if (!s_.training) return {};
    if (in.lastDevice() == InputDevice::Gamepad) {
        // 2.93: the button of « Recommencer » (View by default), held with the d-pad.
        const std::string v = actionPadName(in.bindings(), Action::Restart, in.gamepadStyle());
        return v + " + bas : poser une startpos · " + v + " + gauche / droite : choisir · " + v + " + haut : point de mesure";
    }
    return keyOf(in, Action::StartPosPlace) + " : poser une startpos · " + keyOf(in, Action::StartPosPrev) + " / " + keyOf(in, Action::StartPosNext) +
           " : choisir · " + keyOf(in, Action::MeasurePlace) + " : point de mesure (A puis B)";
}

// ------------------------------------------------------------------ drawing

void TrainingSession::drawWorld(Renderer& r, const WorldView& view, const AnimationSetPtr& set) {
    if (!g_ || !s_.training || s_.levelKey.empty()) return;
    const std::string& key = s_.levelKey;
    // The measure gates: dashed lines, A and B named at their top.
    if (g_->measures.showGates) {
        for (const MeasurePair& p : g_->measures.list(key)) {
            auto gate = [&](const MeasureGate& gt) {
                const Vec2 top = view.toScreen({gt.at.x, gt.top()}), bottom = view.toScreen({gt.at.x, gt.bottom()});
                if (top.x < -40.0f || top.x > static_cast<float>(view.screenW) + 40.0f) return;
                const float dash = std::max(4.0f, 8.0f * view.scale);
                const float th = std::max(1.5f, 2.0f * view.scale);
                for (float y = top.y; y < bottom.y; y += dash * 2.0f) r.drawLine({top.x, y}, {top.x, std::min(bottom.y, y + dash)}, kGate.withAlpha(210), th);
                const float h = std::max(14.0f, 16.0f * view.scale);
                const Rect tag{top.x - h * 0.75f, top.y - h * 1.1f, h * 1.5f, h};
                r.fillRoundedRect(tag, h * 0.25f, kGate.withAlpha(230));  // its name over it (drawHud)
            };
            gate(p.a);
            if (p.b) gate(*p.b);
        }
    }
    // 2.95: the SOB path (where a StartPos lies: F5 places one on its point nearest to Nova), a
    // faint golden line along it, what is on screen only.
    if (g_->startPositions.show)
        if (const SobPath* path = path_.path()) {
            const float W = static_cast<float>(view.screenW), H = static_cast<float>(view.screenH);
            auto seen = [&](Vec2 p) {
                const Vec2 s = view.toScreen(p);
                return s.x > -40.0f && s.x < W + 40.0f && s.y > -40.0f && s.y < H + 40.0f;
            };
            const float th = std::max(1.5f, 2.0f * view.scale);
            for (const SobPathSegment& s : path->segments)
                for (size_t i = 2; i < s.points.size(); i += 2)
                    if (seen(s.points[i - 2]) || seen(s.points[i])) r.drawLine(view.toScreen(s.points[i - 2]), view.toScreen(s.points[i]), kGold.withAlpha(90), th);
        }
    // The StartPos: a pennant and Nova's translucent silhouette (the one in use brighter).
    if (g_->startPositions.show && set && set->texture) {
        const int active = g_->startPositions.activeId(key);
        const std::vector<StartPosEntry> list = g_->startPositions.list(key);
        if (silhouettes_.size() < list.size()) silhouettes_.resize(list.size());
        r.setView(view);
        for (size_t i = 0; i < list.size(); ++i) {
            const StartPosEntry& e = list[i];
            const bool on = e.id == active;
            Animator& a = silhouettes_[i];
            if (!a.set()) {
                a.setSet(set);
                a.play("idle");
            }
            const Rect src = a.sourceRect(), dst = a.destRect(e.state.feet, e.state.faceLeft);
            r.drawWorldTexture(*set->texture, &src, dst, Color{170, 240, 255, static_cast<uint8_t>(on ? 120 : 70)}, BlendMode::Alpha, e.state.faceLeft);
            const Vec2 foot = e.state.feet, tip = foot + Vec2{0.0f, -46.0f};
            const Color pole = on ? Color{57, 240, 216, 230} : Color{57, 240, 216, 130};
            r.drawWorldLine(foot, tip, pole, 2.0f);
            r.drawWorldLine(tip, tip + Vec2{16.0f, 5.0f}, pole, 2.0f);
            r.drawWorldLine(tip + Vec2{16.0f, 5.0f}, tip + Vec2{0.0f, 10.0f}, pole, 2.0f);
        }
        r.setView(WorldView{});
    }
}

Rect TrainingSession::placeClear(const std::vector<Rect>& candidates, const std::vector<Rect>& keep, float margin) {
    Rect at = candidates.empty() ? Rect{} : candidates.front();
    float least = 1e30f;
    for (const Rect& c : candidates) {
        float overlap = 0.0f;
        for (const Rect& k : keep) {
            if (k.w <= 0.0f || k.h <= 0.0f) continue;
            const Rect e = k.expanded(margin);
            if (!c.intersects(e)) continue;
            const Rect i = c.intersection(e);
            overlap += std::max(0.0f, i.w) * std::max(0.0f, i.h);
        }
        if (overlap < least) {
            least = overlap;
            at = c;
        }
        if (overlap <= 0.0f) break;
    }
    return at;
}

void TrainingSession::drawHud(UIContext& ui, const GameWorld& w, const WorldView& view, std::vector<Rect> keep) {
    drawn_.clear();
    if (!g_) return;
    // The labels of the gates and of the StartPos pennants (screen text, over the level).
    if (s_.training && !s_.levelKey.empty()) {
        // A name that would go under the HUD is not written (its mark in the level stays).
        const float fs = fz(ui, 13);
        auto clear = [&](const Rect& r) {
            return std::none_of(keep.begin(), keep.end(), [&](const Rect& k) { return k.w > 0.0f && k.h > 0.0f && r.intersects(k); });
        };
        if (g_->measures.showGates)
            for (const MeasurePair& p : g_->measures.list(s_.levelKey)) {
                auto name = [&](const MeasureGate& gt, const std::string& n) {
                    const Vec2 top = view.toScreen({gt.at.x, gt.top()});
                    const float h = std::max(14.0f, 16.0f * view.scale);
                    const Vec2 c{top.x, top.y - h * 0.6f};
                    const Vec2 sz = ui.text().measure(n, FontFace::Bold, ui.s(fs));
                    if (!clear(Rect::fromCenter(c, sz))) return;
                    ui.label(ui.elide(n, FontFace::Bold, fs, h * 1.5f), c, fs, Color{40, 28, 8, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
                };
                name(p.a, "A" + std::to_string(p.id));
                if (p.b) name(*p.b, "B" + std::to_string(p.id));
            }
        if (g_->startPositions.show) {
            const std::vector<StartPosEntry> list = g_->startPositions.list(s_.levelKey);
            for (size_t i = 0; i < list.size(); ++i) {
                const Vec2 tip = view.toScreen(list[i].state.feet + Vec2{18.0f, -44.0f});
                const std::string n = "SP " + std::to_string(i + 1);
                const Vec2 sz = ui.text().measure(n, FontFace::Bold, ui.s(fs));
                if (!clear({tip.x, tip.y - sz.y * 0.5f, sz.x, sz.y})) continue;
                ui.label(n, tip, fs, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
            }
        }
    }
    if (s_.training && chain()) drawChainPanel(ui, w, keep);
    if (s_.training) drawStartPosPanel(ui, keep);
    if (s_.training) drawMeasurePanel(ui, keep);
    drawToast(ui, keep);
}

void TrainingSession::drawStartPosPanel(UIContext& ui, std::vector<Rect>& keep) {
    const std::string& key = s_.levelKey;
    const std::vector<StartPosEntry> list = g_->startPositions.list(key);
    if (list.empty()) return;
    const UITheme& th = ui.theme();
    const int active = g_->startPositions.activeId(key);
    const int rank = g_->startPositions.rank(key, active);
    const StartPosEntry* e = rank >= 0 ? &list[static_cast<size_t>(rank)] : nullptr;
    const float W = ui.screenW(), H = ui.screenH();
    const float fs = fz(ui, 15), small = fz(ui, 13);
    const float pad = ui.s(14), lineH = ui.s(fs + 10.0f);
    const float bw = std::min(ui.s(470), W * 0.36f), bh = pad * 2 + lineH * 4;
    const float m = ui.s(16);
    const Rect at = placeClear({{W - m - bw, ui.s(120), bw, bh}, {W - m - bw, H * 0.5f - bh * 0.5f, bw, bh}, {m, H * 0.42f, bw, bh},
                                {W - m - bw, H - m - bh - ui.s(70), bw, bh}},
                               keep, ui.s(6));
    keep.push_back(at);
    drawn_.push_back(at);
    ui.r().fillRoundedRect(at, ui.s(10), Color{8, 12, 26, 215});
    ui.r().drawRoundedRect(at, ui.s(10), th.accent.withAlpha(120), std::max(1.0f, ui.s(1.5f)));
    const float x = at.x + pad, room = at.w - pad * 2;
    float y = at.y + pad + lineH * 0.5f;
    // « STARTPOS 3 / 4 » and « active ».
    const std::string head = "STARTPOS " + (rank >= 0 ? std::to_string(rank + 1) : std::string("—")) + " / " + std::to_string(list.size());
    const std::string pill = rank >= 0 ? "active" : "aucune active";
    const float pw = ui.text().measure(pill, FontFace::Bold, ui.s(small)).x + ui.s(16);
    ui.label(ui.elide(head, FontFace::Bold, fs, room - pw - ui.s(10)), {x, y}, fs, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
    const Rect pr{at.right() - pad - pw, y - ui.s(small + 8.0f) * 0.5f, pw, ui.s(small + 8.0f)};
    ui.r().drawRoundedRect(pr, pr.h * 0.5f, (rank >= 0 ? th.accent : th.textDim).withAlpha(170), std::max(1.0f, ui.s(1.0f)));
    ui.label(pill, pr.center(), small, rank >= 0 ? th.accent : th.textDim, Align::Center, FontFace::Bold, VAlign::Middle);
    y += lineH;
    // Where it lies.
    std::string where = e ? whereOnRoute(e->state.feet) : std::string("Départ de l'entraînement");
    if (where.empty()) where = "Calcul du parcours…";
    ui.label(ui.elide(where, FontFace::Bold, fs + 2.0f, room), {x, y}, fs + 2.0f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
    y += lineH;
    // 2.95: how it starts: the replay that holds its segment's best, played up to it; or « à replacer ».
    if (e) {
        const bool usable = startPosUsable(*e);
        const std::string how = usable ? "Départ : le meilleur de ce segment rejoué jusqu'ici" : "À replacer sur le tracé de la SOB : inutilisable";
        ui.label(ui.elide(how, FontFace::Body, small, room), {x, y}, small, usable ? th.textDim : th.danger, Align::Left, FontFace::Body, VAlign::Middle);
    } else {
        ui.label(ui.elide("Le prochain essai part du départ de l'entraînement.", FontFace::Body, small, room), {x, y}, small, th.textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
    }
    y += lineH;
    // Previous / next.
    const float kh = ui.s(small + 10.0f);
    float lw = Glyphs::drawAction(ui, Action::StartPosPrev, {x, y}, kh);
    ui.label("Précédente", {x + lw + ui.s(8), y}, small, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    const float nw = Glyphs::measureAction(ui, Action::StartPosNext, kh);
    Glyphs::drawAction(ui, Action::StartPosNext, {at.right() - pad - nw, y}, kh);
    ui.label("Suivante", {at.right() - pad - nw - ui.s(8), y}, small, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
}

void TrainingSession::drawMeasurePanel(UIContext& ui, std::vector<Rect>& keep) {
    const std::string& key = s_.levelKey;
    const MeasurePair* p = shownPair_ > 0 ? g_->measures.find(key, shownPair_) : nullptr;
    if (!p) {
        for (const MeasurePair& q : g_->measures.list(key))
            if (q.complete()) {
                shownPair_ = q.id;
                p = g_->measures.find(key, q.id);
                break;
            }
    }
    if (!p || !p->b) return;
    const UITheme& th = ui.theme();
    const float W = ui.screenW(), H = ui.screenH();
    const float fs = fz(ui, 14), big = 40.0f;
    const float pad = ui.s(14);
    const float bw = std::min(ui.s(760), W * 0.56f), bh = ui.s(118);
    const Rect at = placeClear({{W * 0.5f - bw * 0.5f, H - ui.s(96) - bh, bw, bh}, {W * 0.5f - bw * 0.5f, ui.s(150), bw, bh},
                                {ui.s(16), H * 0.5f, bw, bh}},
                               keep, ui.s(6));
    keep.push_back(at);
    drawn_.push_back(at);
    ui.r().fillRoundedRect(at, ui.s(10), Color{8, 12, 26, 220});
    ui.r().drawRoundedRect(at, ui.s(10), kGate.withAlpha(140), std::max(1.0f, ui.s(1.5f)));
    auto timer = timers_.find(p->id);
    const bool running = timer != timers_.end() && timer->second.running();
    const std::string n = std::to_string(p->id);
    const float leftW = at.w * 0.42f;
    const float x = at.x + pad;
    const std::string head = "MESURE A" + n + " → B" + n + (running ? " · EN COURS" : lastTime_ > 0.0f ? " · DERNIER ESSAI" : " · PRÊTE");
    ui.label(ui.elide(head, FontFace::Bold, fs, leftW - pad), {x, at.y + pad + ui.s(fs) * 0.5f}, fs, kGate, Align::Left, FontFace::Bold, VAlign::Middle);
    const float shown = running ? timer->second.elapsed(chrono_) : (lastTime_ > 0.0f ? lastTime_ : 0.0f);
    const std::string value = formatGhostSeconds(shown);
    const float bf = ui.fitFont(value, FontFace::Title, big, leftW - pad, 0.6f);
    ui.label(value, {x, at.center().y + ui.s(4)}, bf, th.text, Align::Left, FontFace::Title, VAlign::Middle);
    const std::string name = p->name.empty() ? "Mesure " + n : p->name;
    {
        i18n::Verbatim words;  // as the player named it
        ui.label(ui.elide(name, FontFace::Body, fs, leftW - pad), {x, at.bottom() - pad - ui.s(fs) * 0.5f}, fs, th.textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
    }
    // The best, the last, the attempts; then the last times as bars.
    const float rx = at.x + leftW + ui.s(10), rroom = at.right() - pad - rx;
    const float ty = at.y + pad + ui.s(fs) * 0.5f;
    const std::string best = "Meilleur " + (p->best > 0.0f ? formatGhostSeconds(p->best) : std::string("—"));
    const std::string last = "Dernier " + (p->last.empty() ? std::string("—") : formatGhostSeconds(p->last.back()));
    const std::string tries = std::to_string(p->attempts) + (p->attempts > 1 ? " essais" : " essai");
    const float tw = ui.text().measure(tries, FontFace::Body, ui.s(fs)).x;
    const float half = (rroom - tw - ui.s(12)) * 0.5f;
    ui.label(ui.elide(best, FontFace::Bold, fs, half - ui.s(6)), {rx, ty}, fs, kGold, Align::Left, FontFace::Bold, VAlign::Middle);
    ui.label(ui.elide(last, FontFace::Bold, fs, half - ui.s(6)), {rx + half, ty}, fs, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
    ui.label(tries, {at.right() - pad, ty}, fs, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
    const size_t count = std::min<size_t>(p->last.size(), 8);
    if (count > 0) {
        const float top = ty + ui.s(fs) * 0.5f + ui.s(10), bottom = at.bottom() - pad;
        const float slot = rroom / 8.0f, bwid = slot * 0.7f;
        float lo = 1e9f, hi = 0.0f;
        for (size_t i = p->last.size() - count; i < p->last.size(); ++i) {
            lo = std::min(lo, p->last[i]);
            hi = std::max(hi, p->last[i]);
        }
        for (size_t k = 0; k < count; ++k) {
            const float v = p->last[p->last.size() - count + k];
            // The slowest the tallest; every bar at least a third of the height.
            const float f = hi > lo ? 0.35f + 0.65f * (v - lo) / (hi - lo) : 0.6f;
            const float h = (bottom - top) * f;
            const bool isBest = p->best > 0.0f && msOf(v) == msOf(p->best);
            ui.r().fillRect({rx + slot * static_cast<float>(k), bottom - h, bwid, h}, isBest ? kGold : th.textDim.withAlpha(150));
        }
    }
}

void TrainingSession::drawChainPanel(UIContext& ui, const GameWorld& w, std::vector<Rect>& keep) {
    const UITheme& th = ui.theme();
    const int n = static_cast<int>(route_.size());
    auto pos = [&](const std::string& id, bool from) -> int {
        if (id.empty()) return from ? 0 : n + 1;
        auto it = std::find(route_.begin(), route_.end(), id);
        return it == route_.end() ? -1 : static_cast<int>(it - route_.begin()) + 1;
    };
    const int a = pos(s_.from, true), b = pos(s_.to, false);
    if (a < 0 || b <= a + 1) return;
    auto boundary = [&](int k) { return k <= 0 || k > n ? std::string{} : route_[static_cast<size_t>(k - 1)]; };
    // The rows: 2.95, the run-up first (« élan », never counted), then the segments of the
    // enchaînement (the last six when there are more).
    struct Row {
        int index;
        std::string from, to;
        float time = -1.0f, gap = 0.0f;
        bool current = false, gold = false, has = false, runUp = false, counts = false;
    };
    const int a0 = hasRunUp() ? std::max(0, a - 1) : a;
    // What this attempt counts so far (the rules): only those rows compare with their best.
    const std::vector<PersonalBests::Span> spans = g_->bests.counted(s_.levelKey, passage(-1.0f));
    auto counted = [&](const std::string& f, const std::string& t) {
        return std::any_of(spans.begin(), spans.end(), [&](const PersonalBests::Span& sp) { return sp.range.first == f && sp.range.second == t; });
    };
    const float entry = entryTime();
    std::vector<Row> rows;
    float t0 = 0.0f, cumGap = 0.0f;
    bool t0Known = true, cumKnown = true;
    float lastPoint = 0.0f;
    int lastIndex = a0;
    for (int k = a0; k < b; ++k) {
        Row r;
        r.index = k + 1;
        r.from = boundary(k);
        r.to = boundary(k + 1);
        r.runUp = k < a;
        const std::string endId = k + 1 > n ? std::string{} : r.to;
        float at = -1.0f;  // its end's first crossing (a boundary of this attempt)
        for (const auto& [id, t] : crossed_)
            if (!endId.empty() && id == endId) at = t;
        if (at >= 0.0f) {
            r.has = true;
            r.time = t0Known ? at - t0 : -1.0f;
            t0 = at;
            t0Known = true;
            lastPoint = at;
            lastIndex = k + 1;
            const float best = bestAtStart(r.from, r.to);
            r.counts = !r.runUp && r.time >= 0.0f && counted(r.from, r.to);
            if (r.counts && best > 0.0f) {
                r.gap = r.time - best;
                r.gold = msOf(r.time) < msOf(best);
                cumGap += r.gap;
            } else if (!r.runUp) {
                cumKnown = false;
            }
        } else if (!endId.empty() && seen_.count(endId)) {
            // Lit, but passed unseen before (a fall beyond it): no time from or to it.
            r.has = true;
            t0Known = false;
            lastIndex = k + 1;
            if (!r.runUp) cumKnown = false;
        } else if (rows.empty() || rows.back().has) {
            r.current = true;
            r.time = t0Known ? std::max(0.0f, w.timer().elapsed() - t0) : -1.0f;
        }
        rows.push_back(r);
        if (!r.has) break;
    }
    if (rows.size() > 6) rows.erase(rows.begin(), rows.end() - 6);
    const float W = ui.screenW(), H = ui.screenH();
    const float fs = fz(ui, 14), pad = ui.s(14), lineH = ui.s(fs + 12.0f);
    const float bw = std::min(ui.s(560), W * 0.4f);
    const float bh = pad * 2 + lineH * (1.0f + static_cast<float>(rows.size()) + 3.0f) + ui.s(8);
    const float m = ui.s(16);
    const Rect at = placeClear({{W - m - bw, ui.s(110), bw, bh}, {W - m - bw, H * 0.5f - bh * 0.5f, bw, bh}, {m, ui.s(150), bw, bh}}, keep, ui.s(6));
    keep.push_back(at);
    drawn_.push_back(at);
    ui.r().fillRoundedRect(at, ui.s(10), Color{8, 12, 26, 220});
    ui.r().drawRoundedRect(at, ui.s(10), th.accent.withAlpha(120), std::max(1.0f, ui.s(1.5f)));
    const float x = at.x + pad, right = at.right() - pad, room = at.w - pad * 2;
    float y = at.y + pad + lineH * 0.5f;
    const std::string head = "TEMPS DE PASSAGE · ESSAI " + std::to_string(std::max(1, s_.attempt));
    const std::string sub = "écart au meilleur segment";
    const float sw = std::min(ui.text().measure(sub, FontFace::Body, ui.s(fs)).x, room * 0.45f);
    ui.label(ui.elide(head, FontFace::Bold, fs, room - sw - ui.s(10)), {x, y}, fs, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
    ui.label(ui.elide(sub, FontFace::Body, fs, sw), {right, y}, fs, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
    y += lineH;
    // The right column: a gap, « en cours », « élan » (each measured: none goes under the time).
    float gapW = ui.text().measure("+00,000", FontFace::Bold, ui.s(fs)).x;
    for (const char* word : {"en cours", "élan"}) gapW = std::max(gapW, ui.text().measure(word, FontFace::Body, ui.s(fs)).x);
    gapW += ui.s(10);
    const float timeW = ui.text().measure("0:00,00", FontFace::Bold, ui.s(fs)).x + ui.s(14);
    const float nameRoom = room - gapW - timeW;
    for (const Row& r : rows) {
        if (r.current) ui.r().fillRect({at.x + ui.s(4), y - lineH * 0.5f, at.w - ui.s(8), lineH}, Color{255, 255, 255, 14});
        const std::string name = std::to_string(r.index) + " · " + pointName(r.from, true) + " → " + pointName(r.to, false);
        ui.label(ui.elide(name, FontFace::Body, fs, nameRoom), {x, y}, fs, r.runUp ? th.textDim : th.text, Align::Left, FontFace::Body, VAlign::Middle);
        if (r.time >= 0.0f)
            ui.label(formatGhostTime(r.time), {right - gapW, y}, fs, r.gold ? kGold : r.runUp ? th.textDim : th.text, Align::Right, FontFace::Bold,
                     VAlign::Middle);
        else if (r.has)
            ui.label("—", {right - gapW, y}, fs, th.textDim, Align::Right, FontFace::Bold, VAlign::Middle);
        if (r.runUp) ui.label("élan", {right, y}, fs, th.warm, Align::Right, FontFace::Body, VAlign::Middle);
        else if (r.current) ui.label("en cours", {right, y}, fs, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
        else if (r.counts && bestAtStart(r.from, r.to) > 0.0f)
            ui.label(formatGhostDelta(r.gap, false), {right, y}, fs, r.gold ? kGold : (r.gap > 0.0005f ? th.danger : th.textDim), Align::Right, FontFace::Bold,
                     VAlign::Middle);
        y += lineH;
    }
    y += ui.s(8);
    // The cumulative gap, the best possible at the end (from the entry: the run-up never counts),
    // the enchaînement's record.
    float possible = -1.0f;
    if (entry < 0.0f && inRunUp()) {
        possible = g_->bests.sumOfBests(s_.levelKey, a, b);
    } else if (entry >= 0.0f && t0Known) {
        const float sumRest = lastIndex < b ? g_->bests.sumOfBests(s_.levelKey, std::max(lastIndex, a), b) : 0.0f;
        if (sumRest >= 0.0f) possible = std::max(0.0f, lastPoint - entry) + sumRest;
    }
    const float record = bestAtStart(s_.from, s_.to);
    const std::pair<std::string, std::string> foot[3] = {
        {"Écart cumulé à la SOB du début de l'essai", lastIndex > a && cumKnown ? formatGhostDelta(cumGap) : std::string("—")},
        {"Meilleur possible à l'arrivée", possible > 0.0f ? formatGhostTime(possible) : std::string("—")},
        {"Record de cet enchaînement", record > 0.0f ? formatGhostTime(record) : std::string("—")}};
    for (int i = 0; i < 3; ++i) {
        const float vw = ui.text().measure(foot[i].second, FontFace::Bold, ui.s(fs)).x;
        ui.label(ui.elide(foot[i].first, FontFace::Body, fs, room - vw - ui.s(12)), {x, y}, fs, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        Color c = th.text;
        if (i == 0 && lastIndex > a && cumKnown) c = cumGap > 0.0005f ? th.danger : cumGap < -0.0005f ? kGold : th.textDim;
        ui.label(foot[i].second, {right, y}, fs, c, Align::Right, FontFace::Bold, VAlign::Middle);
        y += lineH;
    }
}

void TrainingSession::drawToast(UIContext& ui, std::vector<Rect>& keep) {
    if (toasts_.empty()) return;
    const UITheme& th = ui.theme();
    const Toast& t = toasts_.back();
    const float W = ui.screenW(), H = ui.screenH();
    const float big = t.gold ? 24.0f : 20.0f, fs = fz(ui, 14), pad = ui.s(14);
    const float bw = std::min(ui.s(560), W * 0.42f);
    const float tf = ui.fitFont(t.title, FontFace::Title, big, bw - pad * 2, 0.6f);
    const float bh = pad * 2 + ui.s(tf) * 1.2f + ui.s(fs) * 1.5f;
    const float m = ui.s(16);
    const Rect at = placeClear({{m + ui.s(140), H * 0.3f, bw, bh}, {W * 0.5f - bw * 0.5f, H * 0.22f, bw, bh}, {m, H * 0.55f, bw, bh}}, keep, ui.s(6));
    keep.push_back(at);
    drawn_.push_back(at);
    const float alpha = std::clamp((3.2f - t.t) / 0.4f, 0.0f, 1.0f);
    const Color edge = t.gold ? kGold : th.accent;
    ui.r().fillRoundedRect(at, ui.s(10), Color{24, 18, 4, static_cast<uint8_t>((t.gold ? 230 : 200) * alpha)});
    ui.r().drawRoundedRect(at, ui.s(10), edge.withAlpha(static_cast<uint8_t>(220 * alpha)), std::max(1.0f, ui.s(2.0f)));
    ui.label(ui.elide(t.title, FontFace::Title, tf, bw - pad * 2), {at.x + pad, at.y + pad + ui.s(tf) * 0.55f}, tf,
             (t.gold ? kGold : th.text).withAlpha(static_cast<uint8_t>(255 * alpha)), Align::Left, FontFace::Title, VAlign::Middle);
    if (!t.line.empty())
        ui.label(ui.elide(t.line, FontFace::Body, fs, bw - pad * 2), {at.x + pad, at.bottom() - pad - ui.s(fs) * 0.5f}, fs,
                 th.text.withAlpha(static_cast<uint8_t>(230 * alpha)), Align::Left, FontFace::Body, VAlign::Middle);
}

}  // namespace cosmic
