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
    if (s_.startPosId > 0 || s_.start.startPos) return BestSource::StartPos;
    return chain() ? BestSource::Chain : BestSource::Training;
}

bool TrainingSession::chain() const {
    if (!s_.training || s_.start.startPos) return false;
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
    if (committed_ || id.empty()) return;
    // Where the segment that ends here started: the previous checkpoint, the training's start
    // checkpoint, the level's start (none after a StartPos).
    std::string from;
    float t0 = 0.0f;
    bool known = true;
    if (!crossed_.empty()) {
        from = crossed_.back().first;
        t0 = crossed_.back().second;
    } else if (s_.start.startPos) {
        known = false;
    } else if (s_.training) {
        from = s_.from;
    }
    crossed_.push_back({id, t});
    if (!known || !s_.training || s_.levelKey.empty() || !g_) return;
    const float seg = t - t0;
    const PersonalBests::Record* rec = g_->bests.record(s_.levelKey, from, id);
    if (!rec || rec->best <= 0.0f || msOf(seg) >= msOf(rec->best)) return;
    // A gold segment: counted at once in the Sum of Best shown (the store takes it at the end).
    const float gain = rec->best - seg;
    goldGain_ += gain;
    std::string line = pointName(from, true) + " → " + pointName(id, false) + " : " + formatGhostTime(seg);
    if (sobNow() > 0.0f) line += " · SOB : " + formatGhostTime(sobNow());
    toast("SEGMENT D'OR · " + formatGhostDelta(-gain), line, true);
    if (g_) g_->uiSound("medal", 0.5f);
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
    committed_ = true;
    if (measuresDirty_) g_->measures.save();
    measuresDirty_ = false;
    if (!s_.counts || s_.levelKey.empty()) return out;
    BestAttempt a;
    if (s_.start.startPos) a.passage.from = BestPassage::From::Elsewhere;
    else if (s_.training && !s_.from.empty()) {
        a.passage.from = BestPassage::From::Checkpoint;
        a.passage.startCheckpoint = s_.from;
    } else {
        a.passage.from = BestPassage::From::LevelStart;
    }
    a.passage.checkpoints = crossed_;
    a.passage.exitTime = exitTime > 0.0f ? exitTime : -1.0f;
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
    if (in.menuPressed(Action::StartPosPlace)) flags_ |= InputFrame::PlaceStartPos;
    if (in.menuPressed(Action::StartPosPrev)) flags_ |= InputFrame::PrevStartPos;
    if (in.menuPressed(Action::StartPosNext)) flags_ |= InputFrame::NextStartPos;
    if (in.menuPressed(Action::MeasurePlace)) flags_ |= InputFrame::PlaceMeasure;
    // The gamepad: View held + the d-pad (down: place, left / right: previous / next, up: a
    // measure point); View released alone restarts the attempt.
    const bool back = in.rawPadButton(SDL_GAMEPAD_BUTTON_BACK);
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
    const std::string& key = s_.levelKey;
    for (const TrainingMark& m : w.takeMarks()) {
        if (!s_.training || key.empty()) continue;
        const std::string now = FileUtil::nowString();
        if (m.flag == InputFrame::PlaceStartPos) {
            if (!m.valid) {
                toast("Startpos impossible ici", "Nova doit être en jeu, visible et seule.");
                continue;
            }
            const int id = g_->startPositions.add(key, m.state, fractionAt(m.state.feet), now);
            if (id == 0) {
                toast("Trop de startpos", "32 au plus par version du niveau : effacez-en dans Pause › Startpos et mesures.");
                continue;
            }
            g_->startPositions.save();
            const std::string where = whereOnRoute(m.state.feet);
            toast("Startpos posée : SP " + std::to_string(g_->startPositions.rank(key, id) + 1),
                  (where.empty() ? std::string() : where + " · ") + "Recommencer repart d'ici");
        } else if (m.flag == InputFrame::PrevStartPos || m.flag == InputFrame::NextStartPos) {
            const int id = g_->startPositions.cycle(key, m.flag == InputFrame::NextStartPos ? 1 : -1);
            if (id == 0) {
                toast("Aucune startpos", "Posez-en une d'abord.");
                continue;
            }
            g_->startPositions.save();
            toast("SP " + std::to_string(g_->startPositions.rank(key, id) + 1) + " active", "Le prochain essai part d'elle.");
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
    if (in.lastDevice() == InputDevice::Gamepad)
        return "Vue + bas : poser une startpos · Vue + gauche / droite : choisir · Vue + haut : point de mesure";
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
    // What it took: the powers, the hearts and the energy.
    if (e) {
        const std::string lead = "Pris à la pose :";
        float cx = x + ui.label(lead, {x, y}, small, th.textDim, Align::Left, FontFace::Body, VAlign::Middle).x + ui.s(8);
        const float ic = ui.s(small + 8.0f);
        for (const std::string& id : e->state.powers) {
            const PowerDefinition* d = g_->content.power(id);
            if (!d || cx + ic > at.right() - pad) break;
            ui.icon("ui/icons_powers.png", d->icon, 32, {cx, y - ic * 0.5f, ic, ic});
            cx += ic + ui.s(4);
        }
        const std::string rest = std::to_string(e->state.health) + (e->state.health > 1 ? " cœurs" : " cœur") + " · énergie " +
                                 std::to_string(static_cast<int>(std::lround(e->state.energy))) + " %";
        const float rroom = at.right() - pad - (cx + ui.s(6));
        if (rroom > ui.s(40)) ui.label(ui.elide(rest, FontFace::Body, small, rroom), {cx + ui.s(6), y}, small, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
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
    // The rows: the segments of the enchaînement (the last six when there are more).
    struct Row {
        int index;
        std::string from, to;
        float time = -1.0f, gap = 0.0f;
        bool current = false, gold = false, has = false;
    };
    std::vector<Row> rows;
    float t0 = 0.0f, cumGap = 0.0f;
    bool cumKnown = true;
    size_t ci = 0;
    float lastPoint = 0.0f;
    int lastIndex = a;
    for (int k = a; k < b; ++k) {
        Row r;
        r.index = k + 1;
        r.from = boundary(k);
        r.to = boundary(k + 1);
        const std::string endId = k + 1 > n ? std::string{} : r.to;
        if (!endId.empty() && ci < crossed_.size() && crossed_[ci].first == endId) {
            r.time = crossed_[ci].second - t0;
            t0 = crossed_[ci].second;
            ++ci;
            r.has = true;
            lastPoint = t0;
            lastIndex = k + 1;
            const float best = bestAtStart(r.from, r.to);
            if (best > 0.0f) {
                r.gap = r.time - best;
                r.gold = msOf(r.time) < msOf(best);
                cumGap += r.gap;
            } else {
                cumKnown = false;
            }
        } else if (rows.empty() || rows.back().has) {
            r.current = true;
            r.time = std::max(0.0f, w.timer().elapsed() - t0);
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
    const float gapW = ui.text().measure("+00,000", FontFace::Bold, ui.s(fs)).x + ui.s(10);
    const float timeW = ui.text().measure("0:00,00", FontFace::Bold, ui.s(fs)).x + ui.s(14);
    const float nameRoom = room - gapW - timeW;
    for (const Row& r : rows) {
        if (r.current) ui.r().fillRect({at.x + ui.s(4), y - lineH * 0.5f, at.w - ui.s(8), lineH}, Color{255, 255, 255, 14});
        const std::string name = std::to_string(r.index) + " · " + pointName(r.from, true) + " → " + pointName(r.to, false);
        ui.label(ui.elide(name, FontFace::Body, fs, nameRoom), {x, y}, fs, th.text, Align::Left, FontFace::Body, VAlign::Middle);
        if (r.time >= 0.0f)
            ui.label(formatGhostTime(r.time), {right - gapW, y}, fs, r.gold ? kGold : th.text, Align::Right, FontFace::Bold, VAlign::Middle);
        if (r.current) ui.label("en cours", {right, y}, fs, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
        else if (r.has && bestAtStart(r.from, r.to) > 0.0f)
            ui.label(formatGhostDelta(r.gap, false), {right, y}, fs, r.gold ? kGold : (r.gap > 0.0005f ? th.danger : th.textDim), Align::Right, FontFace::Bold,
                     VAlign::Middle);
        y += lineH;
    }
    y += ui.s(8);
    // The cumulative gap, the best possible at the end, the enchaînement's record.
    const float sumRest = lastIndex < b ? g_->bests.sumOfBests(s_.levelKey, lastIndex, b) : 0.0f;
    const float possible = sumRest >= 0.0f ? lastPoint + sumRest : -1.0f;
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
