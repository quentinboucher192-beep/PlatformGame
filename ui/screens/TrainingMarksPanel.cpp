#include "ui/screens/TrainingMarksPanel.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/StartPosStore.h"
#include "game/levels/LevelData.h"
#include "game/player/PlayerConfig.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGate{255, 200, 80, 255};

/// « 21/11 » from a date « 2026-11-21 10:00 » ("" when it is not one).
std::string dayMonth(const std::string& date) { return date.size() >= 10 ? date.substr(8, 2) + "/" + date.substr(5, 2) : std::string{}; }

/// « 77,1 »: one decimal, written the French way (the English text shows « 77.1 »).
std::string decimal1(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

/// Nova's body (data/player.json): what must be free where a StartPos starts.
Vec2 bodySize(GameServices& g) { return PlayerConfig::fromJson(g.content.playerConfig()).size; }

/// The gamepad's 8-pixel step: LB or RB held.
bool shoulderHeld(InputManager& in) {
    return in.rawPadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || in.rawPadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
}

/// A StartPos pennant: its pole from `foot` up `h` pixels, its flag at the top.
void pennant(Renderer& r, Vec2 foot, float h, Color c, float thick) {
    const Vec2 tip = foot - Vec2{0.0f, h};
    const float f = h * 0.35f;
    r.drawLine(foot, tip, c, thick);
    r.fillTriangle(tip, tip + Vec2{f, f * 0.32f}, tip + Vec2{0.0f, f * 0.64f}, c);
}

/// A measure gate: a dashed vertical line.
void dashed(Renderer& r, float x, float y0, float y1, float dash, Color c, float thick) {
    for (float y = y0; y < y1; y += dash * 2.0f) r.drawLine({x, y}, {x, std::min(y1, y + dash)}, c, thick);
}

/// An arrow (a triangle) pointing `d` in the middle of `box`.
void arrow(Renderer& r, const Rect& box, Vec2 d, Color c) {
    const Vec2 m = box.center();
    const float k = std::min(box.w, box.h) * 0.22f;
    const Vec2 side{-d.y, d.x};
    r.fillTriangle(m + d * k, m - d * k + side * k, m - d * k - side * k, c);
}

}  // namespace

TrainingMarksPanel::~TrainingMarksPanel() {
    if (!g_) return;
    if (dirtySp_) g_->startPositions.save();
    if (dirtyMeasures_) g_->measures.save();
}

bool TrainingMarksPanel::holdsShoulders(UIContext& ui) const { return nudgeFocus_ && shoulderHeld(ui.input()); }

void TrainingMarksPanel::draw(GameServices& g, const Rect& area, const Context& c) {
    UIContext& ui = g.ui;
    g_ = &g;
    backUsed_ = false;
    if (!wantFocus_.empty()) {
        ui.setFocus(wantFocus_);
        wantFocus_.clear();
    }
    const bool edit = c.edit;
    // 2.95: the SOB path (read again off the main thread when a segment's best replay changes);
    // the StartPos of before that lie near it placed on it.
    path_.update(g.bests, c.levelKey);
    if (path_.changed() && path_.path() && reanchorStartPos(g.startPositions, c.levelKey, *path_.path()) > 0) g.startPositions.save();
    // A placement is given up with Échap (B) (a gate A placed stays: its B comes later, as in a run).
    if (placing_ != Place::None && ui.backPressed()) {
        wantFocus_ = placing_ == Place::StartPos ? "spadd" : "madd";
        placing_ = Place::None;
        backUsed_ = true;
    }
    // The one chosen is gone (deleted, cleared): nothing chosen.
    if (pick_ != Pick::None && !draft_ && !pickedAt(g, c)) choose(Pick::None, 0);
    const float stripH = ui.s(116);
    drawStrip(g, {area.x, area.y, area.w, stripH}, c);
    float top = area.y + stripH + ui.s(14);
    if (edit) {
        const float eh = ui.s(260);
        drawEditor(g, {area.x, top, area.w, eh}, c);
        top += eh + ui.s(14);
    }
    const float gap = ui.s(24);
    const float colW = (area.w - gap) * 0.5f;
    drawStartPos(g, {area.x, top, colW, area.bottom() - top}, c);
    drawMeasures(g, {area.x + colW + gap, top, colW, area.bottom() - top}, c);
    // What was moved goes to the saves once it rests (a held arrow writes once).
    if ((dirtySp_ || dirtyMeasures_) && ui.time() - editedAt_ > 0.6f) {
        if (dirtySp_) g.startPositions.save();
        if (dirtyMeasures_) g.measures.save();
        dirtySp_ = dirtyMeasures_ = false;
    }
}

// ------------------------------------------------------------------ the one chosen

std::optional<Vec2> TrainingMarksPanel::pickedAt(GameServices& g, const Context& c) const {
    if (pick_ == Pick::None) return std::nullopt;
    if (work_) return work_;
    if (pick_ == Pick::StartPos) {
        if (const StartPosEntry* e = g.startPositions.find(c.levelKey, pickId_)) return e->state.feet;
        return std::nullopt;
    }
    if (const MeasurePair* p = g.measures.find(c.levelKey, pickId_)) {
        if (pick_ == Pick::GateA) return p->a.at;
        if (p->b) return p->b->at;
    }
    return std::nullopt;
}

void TrainingMarksPanel::choose(Pick p, int id) {
    pick_ = p;
    pickId_ = id;
    work_.reset();
    refused_.reset();
    draft_ = false;
}

std::string TrainingMarksPanel::nameOf(const Context& c, const std::string& checkpointId) {
    for (const auto& [id, name] : c.names)
        if (id == checkpointId) return name.empty() ? std::string("Point de passage") : name;
    return "Point de passage";
}

float TrainingMarksPanel::routeAt(Vec2 feet) const {
    const statsart::LevelInfo* info = strip_.info();
    return info && info->track.valid() ? info->track.fractionAt(feet) : -1.0f;
}

bool TrainingMarksPanel::usable(GameServices& g, const std::string& levelKey, const StartPosEntry& e) {
    if (!e.onPath) return false;
    const PersonalBests::Record* r = g.bests.record(levelKey, e.from, e.to);
    return r && r->replay.has();
}

void TrainingMarksPanel::setOnPath(GameServices& g, const Context& c, Vec2 world, float radius) {
    // 2.95: on the point of the SOB path nearest to the place (an attempt from it plays that
    // segment's best replay up to there); never where it was never crossed, never on another one.
    const SobPath* path = path_.path();
    const std::optional<SobPathPoint> pt = path ? path->nearest(world, radius) : std::nullopt;
    const bool fresh = draft_ || pickId_ <= 0;
    auto refuse = [&](Refusal why, Vec2 where) {
        if (fresh) {
            choose(Pick::StartPos, 0);
            draft_ = true;  // kept aside, in red, until it is brought onto the path
        }
        work_ = where;
        refused_ = why;
    };
    if (!pt) return refuse(Refusal::NoPath, world);
    const SobPathSegment& s = path->segments[static_cast<size_t>(pt->segment)];
    if (g.startPositions.occupied(c.levelKey, pt->at, kSobPathSame, fresh ? 0 : pickId_) != 0) return refuse(Refusal::Taken, pt->at);
    if (fresh) {
        const int id = g.startPositions.addOnPath(c.levelKey, pt->at, s.range.first, s.range.second, routeAt(pt->at), FileUtil::nowString(), true, kSobPathSame);
        if (id <= 0) return;  // the level has its 32 already
        g.startPositions.save();
        selectedSp_ = id;
        choose(Pick::StartPos, id);
        return;
    }
    g.startPositions.placeOnPath(c.levelKey, pickId_, pt->at, s.range.first, s.range.second, routeAt(pt->at), kSobPathSame);
    work_.reset();
    refused_.reset();
    dirtySp_ = true;
    editedAt_ = g.ui.time();
}

void TrainingMarksPanel::stepAlong(GameServices& g, const Context& c, int delta) {
    const SobPath* path = path_.path();
    const std::optional<Vec2> at = pickedAt(g, c);
    if (!path || !at || delta == 0) return;
    std::optional<SobPathPoint> pt = path->nearest(*at, kSobPathSnap);
    if (!pt) return setOnPath(g, c, *at, kSobPathSnap);  // off the path: refused, said so
    for (int k = 0; k < std::abs(delta); ++k)
        if (auto next = path->step(*pt, delta)) pt = next;
    setOnPath(g, c, pt->at, 0.5f);
}

void TrainingMarksPanel::placeAt(GameServices& g, const Context& c, Vec2 world) {
    std::shared_ptr<const LevelData> level = c.art ? c.art->level(c.levelId) : nullptr;
    if (!level || !c.edit) return;
    world = {std::round(world.x), std::round(world.y)};
    const Vec2 body = bodySize(g);
    // Standing on the ground under the place chosen (else above it), when there is one.
    const Vec2 at = standingSpot(*level, body, world).value_or(world);
    const std::string date = FileUtil::nowString();
    if (placing_ == Place::StartPos) {
        placing_ = Place::None;
        // The strip is small: the path's point nearest to the place, within a few of its pixels.
        choose(Pick::StartPos, 0);
        setOnPath(g, c, world, std::max(kSobPathSnap, strip_.worldPerPixel() * g.ui.s(24)));
        wantFocus_ = "preview";
    } else if (placing_ == Place::Gate) {
        const int id = g.measures.place(c.levelKey, at, date);
        if (id <= 0) {
            placing_ = Place::None;
            return;
        }
        g.measures.save();
        selectedPair_ = id;
        const MeasurePair* p = g.measures.find(c.levelKey, id);
        if (p && p->complete()) {
            placing_ = Place::None;
            choose(Pick::GateB, id);
            wantFocus_ = "preview";
        } else {
            choose(Pick::GateA, id);  // B next, on the strip again
        }
    }
}

void TrainingMarksPanel::moveTo(GameServices& g, const Context& c, Vec2 to) {
    if (pick_ == Pick::StartPos) {
        setOnPath(g, c, to, kSobPathSnap);  // 2.95: the path's point nearest to it
    } else if (pick_ == Pick::GateA || pick_ == Pick::GateB) {
        g.measures.move(c.levelKey, pickId_, pick_ == Pick::GateB, to);
        dirtyMeasures_ = true;
        editedAt_ = g.ui.time();
    }
}

// ------------------------------------------------------------------ the strip

void TrainingMarksPanel::drawStrip(GameServices& g, const Rect& r, const Context& c) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    InputManager& in = ui.input();
    const float fs = fz(ui, 12);
    const float rowH = ui.s(fs + 6.0f);
    const Rect pic{r.x, r.y, r.w, r.h - ui.s(4) - rowH * 2.0f};
    const bool picture = strip_.draw(g, c.art, c.levelId, c.levelBounds, pic, r);
    const bool edit = c.edit;
    // The strip takes clicks: the place of what is being added (its cursor with the arrows); else
    // a click chooses the mark nearest to it.
    if (edit && placing_ != Place::None) {
        bool hovered = false;
        const bool hit = ui.hotspot("strip", pic, &hovered);
        if (hovered && in.mouseMovedThisFrame()) cursor_ = std::clamp((in.mousePos().x - pic.x) / std::max(1.0f, pic.w), 0.0f, 1.0f);
        if (ui.focused("strip") && ui.acceptsInput()) {
            const float step = in.shift() || shoulderHeld(in) ? 0.04f : 0.005f;
            if (in.menuPressed(Action::MenuLeft)) cursor_ = std::max(0.0f, cursor_ - step);
            if (in.menuPressed(Action::MenuRight)) cursor_ = std::min(1.0f, cursor_ + step);
            ui.consumeNavigation(true, false);
        }
        if (hit) {
            // The mouse: where it clicked; the keys: the cursor, at the height of the route there.
            const bool mouse = hovered && in.mouseReleased(SDL_BUTTON_LEFT);
            placeAt(g, c, strip_.toWorld(mouse ? in.mousePos() : Vec2{pic.x + pic.w * cursor_, pic.y + pic.h * 0.62f}));
        }
    } else if (edit) {
        bool hovered = false;
        if (ui.mouseClick("strippick", pic, &hovered)) {
            const Vec2 m = in.mousePos();
            float best = ui.s(28);
            for (const StartPosEntry& e : g.startPositions.list(c.levelKey))
                if (const float d = std::abs(strip_.xOf(e.state.feet.x) - m.x); d < best) {
                    best = d;
                    selectedSp_ = e.id;
                    choose(Pick::StartPos, e.id);
                }
            for (const MeasurePair& p : g.measures.list(c.levelKey)) {
                if (const float d = std::abs(strip_.xOf(p.a.at.x) - m.x); d < best) {
                    best = d;
                    selectedPair_ = p.id;
                    choose(Pick::GateA, p.id);
                }
                if (p.b)
                    if (const float d = std::abs(strip_.xOf(p.b->at.x) - m.x); d < best) {
                        best = d;
                        selectedPair_ = p.id;
                        choose(Pick::GateB, p.id);
                    }
            }
        }
    }
    // The checkpoints: thin lines.
    for (const auto& [x, name] : c.checkpoints) {
        const Rect t{strip_.xOf(x) - 1.0f, pic.y + ui.s(3), 2.0f, pic.h - ui.s(6)};
        ui.r().fillRect(t, th.textDim.withAlpha(picture ? 170 : 110));
        strip_.note(t);
    }
    // 2.95: the SOB path (where a StartPos lies), a golden line kept inside the strip.
    if (const SobPath* path = path_.path()) {
        auto inStrip = [&](Vec2 w) {
            const Vec2 s = strip_.toScreen(w);
            return Vec2{std::clamp(s.x, pic.x + 1.0f, pic.right() - 1.0f), std::clamp(s.y, pic.y + 1.0f, pic.bottom() - 1.0f)};
        };
        const float pt = std::max(1.5f, ui.s(2.0f));
        for (const SobPathSegment& s : path->segments) {
            const size_t step = std::max<size_t>(1, s.points.size() / 300);
            Vec2 prev = inStrip(s.points.front());
            Rect box{prev.x, prev.y, 0.0f, 0.0f};
            for (size_t i = step; i < s.points.size(); i += step) {
                const Vec2 cur = inStrip(s.points[i]);
                ui.r().drawLine(prev, cur, Color{255, 214, 90, 200}, pt);
                box = box.united({cur.x, cur.y, 0.0f, 0.0f});
                prev = cur;
            }
            strip_.note({box.x, box.y, std::max(1.0f, box.w), std::max(1.0f, box.h)});
        }
    }
    // The gates and the pennants where they are in the level (kept inside the strip), a dark
    // outline under each (seen on any ground); the one chosen thicker.
    const float thin = std::max(1.5f, ui.s(2.0f)), thick = std::max(2.5f, ui.s(3.5f));
    for (const MeasurePair& p : g.measures.list(c.levelKey)) {
        auto gate = [&](const MeasureGate& gt, bool isB) {
            const bool on = pick_ == (isB ? Pick::GateB : Pick::GateA) && pickId_ == p.id;
            const float t = on ? thick : thin;
            const float x = std::clamp(strip_.xOf(gt.at.x), pic.x + t, pic.right() - t);
            float y0 = std::clamp(strip_.toScreen({gt.at.x, gt.top()}).y, pic.y + ui.s(2), pic.bottom() - ui.s(12));
            float y1 = std::clamp(strip_.toScreen({gt.at.x, gt.bottom()}).y, y0 + ui.s(10), pic.bottom() - ui.s(2));
            if (picture) ui.r().fillRect({x - t, y0, t * 2.0f, y1 - y0}, Color{4, 6, 14, 150});
            dashed(ui.r(), x, y0, y1, ui.s(4), kGate, t);
            strip_.note({x - t, y0, t * 2.0f, y1 - y0});
        };
        gate(p.a, false);
        if (p.b) gate(*p.b, true);
    }
    const std::vector<StartPosEntry> sps = g.startPositions.list(c.levelKey);
    const float poleH = ui.s(18);
    for (const StartPosEntry& e : sps) {
        const bool on = pick_ == Pick::StartPos && pickId_ == e.id;
        const float t = on ? thick : thin;
        Vec2 f = strip_.toScreen(e.state.feet);
        f.x = std::clamp(f.x, pic.x + t, pic.right() - poleH * 0.35f - t);
        f.y = std::clamp(f.y, pic.y + poleH + ui.s(2), pic.bottom() - ui.s(2));
        if (picture) pennant(ui.r(), f + Vec2{1.0f, 1.0f}, poleH, Color{4, 6, 14, 190}, t + 1.0f);
        pennant(ui.r(), f, poleH, on ? Color::white() : th.accent, t);
        strip_.note({f.x - t, f.y - poleH, poleH * 0.35f + t * 2.0f, poleH});
    }
    // The cursor of a placement.
    if (edit && placing_ != Place::None) {
        const float x = pic.x + pic.w * cursor_;
        const Rect line{x - thin * 0.5f, pic.y, thin, pic.h};
        ui.r().fillRect(line, Color::white());
        strip_.note(line);
    }
    // Their names under the strip, never one over another: a name that would cover the previous
    // one goes down a row (a third one in the same place is left out; its mark stays).
    struct Tag {
        float x;
        std::string text;
        Color color;
        bool on;
    };
    std::vector<Tag> tags;
    for (size_t i = 0; i < sps.size(); ++i)
        tags.push_back({strip_.xOf(sps[i].state.feet.x), "SP " + std::to_string(i + 1), th.accent, pick_ == Pick::StartPos && pickId_ == sps[i].id});
    for (const MeasurePair& p : g.measures.list(c.levelKey)) {
        tags.push_back({strip_.xOf(p.a.at.x), "A" + std::to_string(p.id), kGate, pick_ == Pick::GateA && pickId_ == p.id});
        if (p.b) tags.push_back({strip_.xOf(p.b->at.x), "B" + std::to_string(p.id), kGate, pick_ == Pick::GateB && pickId_ == p.id});
    }
    std::stable_sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.x < b.x; });
    float rowEnd[2] = {-1e9f, -1e9f};
    for (const Tag& t : tags) {
        const float w = ui.text().measure(t.text, FontFace::Bold, ui.s(fs)).x + ui.s(10);
        const float left = std::clamp(t.x - w * 0.5f, r.x, r.right() - w);
        const int row = rowEnd[0] <= left ? 0 : rowEnd[1] <= left ? 1 : -1;
        if (row < 0) continue;
        const Rect pill{left, pic.bottom() + ui.s(4) + static_cast<float>(row) * rowH, w, rowH - ui.s(1)};
        ui.r().fillRoundedRect(pill, ui.s(4), t.on ? t.color : Color{8, 12, 26, 235});
        ui.r().drawRoundedRect(pill, ui.s(4), t.color.withAlpha(200), 1.0f);
        ui.label(t.text, pill.center(), fs, t.on ? Color{8, 12, 26, 255} : t.color, Align::Center, FontFace::Bold, VAlign::Middle);
        strip_.note(pill);
        rowEnd[row] = pill.right() + ui.s(4);
    }
}

// ------------------------------------------------------------------ « Réglage fin »

void TrainingMarksPanel::drawPreview(GameServices& g, const Rect& pr, const Context& c, Vec2 at) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    Renderer& r = ui.r();
    // What is shown: the level around it, `zoom_` design pixels per world pixel, the place a
    // little under the middle (room above for the pennant and the jumps).
    const float z = static_cast<float>(zoom_) * ui.s(1.0f);
    const Rect vw{at.x - pr.w / z * 0.5f, at.y - pr.h / z * 0.72f, pr.w / z, pr.h / z};
    // The picture: a little more than what is shown, its place on a 64-pixel grid (a step of a
    // few pixels draws nothing again); its neighbours made ahead once it is there.
    const float grid = 64.0f;
    auto windowAt = [&](float cx, float cy) {
        return Rect{cx - std::ceil(vw.w * 0.5f + grid), cy - std::ceil(vw.h * 0.72f + grid), std::ceil(vw.w + grid * 2.0f), std::ceil(vw.h + grid * 2.0f)};
    };
    const Vec2 c0{std::round(at.x / grid) * grid, std::round(at.y / grid) * grid};
    const Rect pw = windowAt(c0.x, c0.y);
    const Texture* t = c.art && !c.levelId.empty() ? c.art->view(c.levelId, pw) : nullptr;
    r.fillRect(pr, Color{8, 11, 24, 255});
    // Until it is made: what the last one shown has of it (a long move between two pictures).
    const Texture* last = !t && c.art && lastView_.w > 0.0f && lastView_.intersects(vw) ? c.art->view(c.levelId, lastView_) : nullptr;
    if (t) {
        const Rect src{vw.x - pw.x, vw.y - pw.y, vw.w, vw.h};
        g.app.renderer().drawTexture(*t, &src, pr);
        lastView_ = pw;
        for (Vec2 d : {Vec2{grid, 0.0f}, Vec2{-grid, 0.0f}, Vec2{0.0f, grid}, Vec2{0.0f, -grid}}) c.art->view(c.levelId, windowAt(c0.x + d.x, c0.y + d.y));
    } else if (last) {
        const Rect in = lastView_.intersection(vw);
        const Rect src{in.x - lastView_.x, in.y - lastView_.y, in.w, in.h};
        g.app.renderer().drawTexture(*last, &src, {pr.x + (in.x - vw.x) * z, pr.y + (in.y - vw.y) * z, in.w * z, in.h * z});
    } else {
        const std::string wait = "Image du niveau en préparation…";
        ui.label(ui.elide(wait, FontFace::Body, fz(ui, 13), pr.w - ui.s(20)), {pr.center().x, pr.y + ui.s(16)}, fz(ui, 13), th.textDim, Align::Center,
                 FontFace::Body, VAlign::Middle);
    }
    // Over it, at its exact place: the pennant, Nova's silhouette and her body; or the gate.
    WorldView v;
    v.scale = z;
    v.screenW = static_cast<int>(pr.w);
    v.screenH = static_cast<int>(pr.h);
    v.topLeft = {vw.x - pr.x / z, vw.y - pr.y / z};
    v.origin = pr.pos();
    r.pushClip(pr);
    // 2.95: the SOB path around it (a StartPos lies on it).
    if (const SobPath* path = path_.path()) {
        const Rect near = vw.expanded(16.0f);
        for (const SobPathSegment& s : path->segments)
            for (size_t i = 1; i < s.points.size(); ++i)
                if (near.contains(s.points[i - 1]) || near.contains(s.points[i]))
                    r.drawLine(v.toScreen(s.points[i - 1]), v.toScreen(s.points[i]), Color{255, 214, 90, 170}, std::max(1.5f, z * 0.75f));
    }
    const Vec2 foot = v.toScreen(at);
    if (pick_ == Pick::StartPos) {
        const bool bad = refused_.has_value();
        const Color col = bad ? th.danger : th.accent;
        bool faceLeft = false;
        if (const StartPosEntry* e = g.startPositions.find(c.levelKey, pickId_)) faceLeft = e->state.faceLeft;
        AnimationSetPtr set = g.app.resources().animation("animations/nova.json");
        if (set && set->texture) {
            Animator a;
            a.setSet(set);
            a.play("idle");
            const Rect src = a.sourceRect(), dst = a.destRect(at, faceLeft);
            r.setView(v);
            r.drawWorldTexture(*set->texture, &src, dst, bad ? Color{255, 120, 140, 170} : Color{170, 240, 255, 150}, BlendMode::Alpha, faceLeft);
            r.setView(WorldView{});
        }
        const Rect body = v.toScreen(Rect::fromBottomCenter(at, bodySize(g)));
        r.drawRect(body, col, std::max(1.0f, ui.s(1.5f)));
        pennant(r, foot, 46.0f * z, col, std::max(2.0f, z));
    } else {
        const float x = foot.x;
        const float y0 = v.toScreen({at.x, at.y - MeasureGate::kAbove}).y, y1 = v.toScreen({at.x, at.y + MeasureGate::kBelow}).y;
        dashed(r, x, y0, y1, 6.0f * z, kGate, std::max(2.0f, z));
    }
    // The exact pixel: a small cross.
    const float cr = std::max(ui.s(6), z * 3.0f);
    r.drawLine(foot - Vec2{cr, 0.0f}, foot + Vec2{cr, 0.0f}, Color::white(), std::max(1.0f, ui.s(1.0f)));
    r.drawLine(foot - Vec2{0.0f, cr}, foot + Vec2{0.0f, cr}, Color::white(), std::max(1.0f, ui.s(1.0f)));
    r.popClip();
}

void TrainingMarksPanel::drawEditor(GameServices& g, const Rect& R, const Context& c) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    InputManager& in = ui.input();
    ui.r().fillRoundedRect(R, ui.s(12), Color{10, 14, 30, 200});
    ui.r().drawRoundedRect(R, ui.s(12), th.accent.withAlpha(50), 1.0f);
    const float pad = ui.s(14), bh = ui.s(44);
    const float pw = std::min((R.w - pad * 2.0f) * 0.46f, ui.s(640));
    const Rect pr{R.x + pad, R.y + pad, pw, R.h - pad * 2.0f - bh - ui.s(10)};
    const std::optional<Vec2> at = pickedAt(g, c);
    const bool fast = in.shift() || shoulderHeld(in);
    const float step = fast ? 8.0f : 1.0f;
    // The preview: its arrows move the one chosen while it has the focus, a click brings it there.
    bool hovered = false;
    const bool hit = ui.hotspot("preview", pr, &hovered);
    previewFocused_ = ui.focused("preview");
    if (at) {
        drawPreview(g, pr, c, *at);
    } else {
        ui.r().fillRect(pr, Color{8, 11, 24, 255});
        const std::string none = placing_ != Place::None ? "Choisissez l'endroit sur la bande du niveau, au-dessus."
                                                         : "Rien de choisi : une startpos ou une paire de mesure des listes, ou une nouvelle.";
        const float f = fz(ui, 13);
        const Vec2 sz = ui.text().measureWrapped(none, FontFace::Body, ui.s(f), pr.w - ui.s(30));
        if (sz.y <= pr.h - ui.s(10)) ui.labelWrapped(none, {pr.x + ui.s(15), pr.center().y - sz.y * 0.5f}, pr.w - ui.s(30), f, th.textDim);
    }
    ui.r().drawRoundedRect(pr, ui.s(4), previewFocused_ ? th.accent : th.accent.withAlpha(70), previewFocused_ ? std::max(1.5f, ui.s(2.0f)) : 1.0f);
    if (at && hit && hovered && in.mouseReleased(SDL_BUTTON_LEFT)) {
        const float z = static_cast<float>(zoom_) * ui.s(1.0f);
        const Vec2 m = in.mousePos();
        const Vec2 world{at->x + (m.x - pr.center().x) / z, at->y + (m.y - (pr.y + pr.h * 0.72f)) / z};
        moveTo(g, c, {std::round(world.x), std::round(world.y)});
    }
    if (previewFocused_ && ui.acceptsInput()) {
        if (at) {
            Vec2 d;
            if (in.menuPressed(Action::MenuLeft)) d.x -= step;
            if (in.menuPressed(Action::MenuRight)) d.x += step;
            if (in.menuPressed(Action::MenuUp)) d.y -= step;
            if (in.menuPressed(Action::MenuDown)) d.y += step;
            ui.consumeNavigation(true, true);
            // 2.95: a StartPos goes along the SOB path (← ↑ the point before, → ↓ the next one).
            if (pick_ == Pick::StartPos) {
                const float k = d.x + d.y;
                if (k != 0.0f) stepAlong(g, c, (k > 0.0f ? 1 : -1) * static_cast<int>(step));
            } else if (d.x != 0.0f || d.y != 0.0f) {
                moveTo(g, c, *at + d);
            }
        }
        if (ui.backPressed() && !backUsed_) {
            // Out of the preview: back to the one chosen in its list.
            backUsed_ = true;
            wantFocus_ = pick_ == Pick::StartPos && pickId_ > 0 ? "sp" + std::to_string(pickId_)
                         : pick_ == Pick::GateA || pick_ == Pick::GateB ? "m" + std::to_string(pickId_)
                                                                         : std::string("spadd");
        }
    }
    // Under it: the arrows (1 pixel, 8 with Maj or LB / RB held), the zoom, the gate A or B.
    float bx = pr.x;
    const float by = pr.bottom() + ui.s(10), aw = ui.s(52), gap = ui.s(6);
    nudgeFocus_ = previewFocused_;
    const std::pair<const char*, Vec2> arrows[] = {{"nudge_left", {-1.0f, 0.0f}}, {"nudge_right", {1.0f, 0.0f}}, {"nudge_up", {0.0f, -1.0f}}, {"nudge_down", {0.0f, 1.0f}}};
    for (const auto& [name, d] : arrows) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = at.has_value();
        const Rect b{bx, by, aw, bh};
        if (ui.button(name, b, "", st) && at) {
            if (pick_ == Pick::StartPos) stepAlong(g, c, (d.x + d.y > 0.0f ? 1 : -1) * static_cast<int>(step));  // 2.95: along the path
            else moveTo(g, c, *at + d * step);
        }
        arrow(ui.r(), b, d, at ? th.text : th.textDim.withAlpha(120));
        nudgeFocus_ = nudgeFocus_ || ui.focused(name);
        bx += aw + gap;
    }
    bx += gap;
    {
        UIContext::ButtonStyle st;
        st.small = true;
        const std::string z = "Zoom ×" + std::to_string(zoom_);
        const float zw = std::min(ui.text().measure(z, FontFace::Bold, ui.s(15)).x + ui.s(28), pr.right() - bx);
        st.fontSize = ui.fitFont(z, FontFace::Bold, 15, zw - ui.s(16), 0.6f);
        if (zw > ui.s(40) && ui.button("zoom", {bx, by, zw, bh}, z, st)) zoom_ = zoom_ == 2 ? 4 : 2;
        bx += zw + gap * 2.0f;
    }
    if (pick_ == Pick::GateA || pick_ == Pick::GateB) {
        const MeasurePair* p = g.measures.find(c.levelKey, pickId_);
        for (int k = 0; k < 2 && p; ++k) {
            const std::string n = (k == 0 ? "A" : "B") + std::to_string(p->id);
            const float gw = ui.text().measure(n, FontFace::Bold, ui.s(15)).x + ui.s(28);
            if (bx + gw > pr.right()) break;
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = (k == 0) == (pick_ == Pick::GateA);
            st.enabled = k == 0 || p->complete();
            if (ui.button(k == 0 ? "ga" : "gb", {bx, by, gw, bh}, n, st) && st.enabled) {
                choose(k == 0 ? Pick::GateA : Pick::GateB, p->id);
                break;
            }
            bx += gw + gap;
        }
    }
    // At the right: what it is, where (pixels and metres), whether it holds there, how it starts,
    // the keys. Each line measured; what does not fit is left out (the keys first kept).
    const float ix = pr.right() + ui.s(24), iw = R.right() - pad - ix;
    float y = R.y + pad;
    const float bottom = R.bottom() - pad;
    const float sf = fz(ui, 13), f = fz(ui, 15);
    const float lh = ui.s(sf + 9.0f);
    const bool padShown = Glyphs::showsPad(ui);
    const bool alongPath = pick_ == Pick::StartPos;  // 2.95: a StartPos goes along the SOB path
    const std::string keys = alongPath ? (padShown ? "Aperçu choisi : croix point du tracé avant / après · LB / RB maintenu 8 points · B : en sortir"
                                                   : "Aperçu choisi : flèches point du tracé avant / après · Maj 8 points · Échap : en sortir")
                             : padShown ? "Aperçu choisi : croix 1 px · LB / RB maintenu + croix 8 px · B : en sortir"
                                        : "Aperçu choisi : flèches 1 px · Maj + flèches 8 px · Échap : en sortir · clic : y amener le repère";
    const float keysY = bottom - lh * 0.5f;
    ui.label(ui.elide(keys, FontFace::Body, sf, iw), {ix, keysY}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    const float textBottom = keysY - lh * 0.5f - ui.s(4);
    auto wrapped = [&](const std::string& text, Color col) {
        const Vec2 sz = ui.text().measureWrapped(text, FontFace::Body, ui.s(sf), iw);
        if (y + sz.y <= textBottom) {
            ui.labelWrapped(text, {ix, y}, iw, sf, col);
            y += sz.y + ui.s(8);
        } else if (y + lh <= textBottom) {
            ui.label(ui.elide(text, FontFace::Body, sf, iw), {ix, y + lh * 0.5f}, sf, col, Align::Left, FontFace::Body, VAlign::Middle);
            y += lh + ui.s(4);
        }
    };
    // The title: « RÉGLAGE FIN · SP 2 ».
    std::string name;
    const StartPosEntry* sp = pick_ == Pick::StartPos && pickId_ > 0 ? g.startPositions.find(c.levelKey, pickId_) : nullptr;
    if (pick_ == Pick::StartPos) name = sp ? "SP " + std::to_string(g.startPositions.rank(c.levelKey, sp->id) + 1) : std::string("Nouvelle startpos");
    else if (pick_ == Pick::GateA) name = "Porte A" + std::to_string(pickId_);
    else if (pick_ == Pick::GateB) name = "Porte B" + std::to_string(pickId_);
    const std::string title = name.empty() ? std::string("RÉGLAGE FIN") : "RÉGLAGE FIN · " + name;
    ui.label(ui.elide(title, FontFace::Bold, fz(ui, 14), iw), {ix, y + ui.s(10)}, fz(ui, 14), th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
    y += ui.s(28);
    if (placing_ != Place::None) {
        const MeasurePair* waiting = nullptr;
        for (const MeasurePair& p : g.measures.list(c.levelKey))
            if (!p.complete()) waiting = g.measures.find(c.levelKey, p.id);
        const std::string what = placing_ == Place::StartPos ? std::string("la startpos")
                                 : waiting                    ? "la porte B" + std::to_string(waiting->id)
                                                              : std::string("la porte A (puis B)");
        wrapped("Pose de " + what + " : un clic sur la bande du niveau, ou son curseur avec les flèches (la croix) puis Entrée (A). Échap (B) : annuler.",
                th.text);
    }
    if (!at) {
        if (placing_ == Place::None)
            wrapped("Choisissez une startpos ou une paire de mesure dans les listes (ou un repère de la bande), ou ajoutez-en une : elle apparaît ici, "
                    "agrandie, à régler au pixel près.",
                    th.textDim);
        return;
    }
    // Where: pixels from the level's top left corner, and metres (1 m = 1 tile = 16 px).
    const Rect& lb = strip_.info() ? strip_.info()->bounds : c.levelBounds;
    const float px = at->x - lb.x, py = at->y - lb.y;
    const std::string where = "x " + std::to_string(static_cast<int>(std::lround(px))) + " px · y " + std::to_string(static_cast<int>(std::lround(py))) + " px";
    const Vec2 ws = ui.label(ui.elide(where, FontFace::Bold, f, iw), {ix, y + ui.s(11)}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
    const std::string metres = "soit " + decimal1(metersOf(px)) + " m · " + decimal1(metersOf(py)) + " m";
    const float mx = ix + ws.x + ui.s(14);
    if (ix + iw - mx > ui.s(60))
        ui.label(ui.elide(metres, FontFace::Body, sf, ix + iw - mx), {mx, y + ui.s(11)}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    y += ui.s(30);
    if (pick_ == Pick::StartPos) {
        // 2.95: on the SOB path, or why not.
        if (refused_) {
            std::string why;
            if (*refused_ == Refusal::Taken) why = "Refusée ici : une autre startpos est déjà à cet endroit (une seule par endroit).";
            else if (draft_) why = "Refusée ici : pas de tracé de la SOB (passage jamais parcouru, ou trop loin du chemin des meilleurs segments). Pas encore "
                                   "enregistrée : amenez-la sur le tracé.";
            else why = "Refusée ici : hors du tracé de la SOB. Elle reste à sa dernière place.";
            wrapped(why, th.danger);
        } else if (sp && !usable(g, c.levelKey, *sp)) {
            wrapped("À replacer : elle n'est pas sur le tracé de la SOB (son segment n'a plus de meilleur temps avec son replay). Amenez-la sur le "
                    "tracé : flèches ou clic.",
                    th.danger);
        } else if (sp) {
            const std::string seg = "Sur le tracé de la SOB : " + (sp->from.empty() ? std::string("Départ") : nameOf(c, sp->from)) + " → " +
                                    (sp->to.empty() ? std::string("Arrivée") : nameOf(c, sp->to)) + ".";
            wrapped(seg, th.success);
        }
        // How an attempt from it starts.
        wrapped("Départ : le meilleur de ce segment rejoué jusqu'ici, Nova dans l'état exact de cette course (cœurs, énergie, recharges, élan, "
                "ennemis). Compté dès le segment suivant entré.",
                th.textDim);
    } else {
        wrapped("Une porte se règle librement. La déplacer efface ses temps : ils ne se comparent plus.", th.textDim);
    }
}

// ------------------------------------------------------------------ the lists

void TrainingMarksPanel::drawStartPos(GameServices& g, const Rect& r, const Context& c) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    const bool edit = c.edit;
    const std::vector<StartPosEntry> list = g.startPositions.list(c.levelKey);
    const int active = g.startPositions.activeId(c.levelKey);
    if (!list.empty() && std::none_of(list.begin(), list.end(), [&](const StartPosEntry& e) { return e.id == selectedSp_; }))
        selectedSp_ = active > 0 ? active : list.front().id;
    const float hf = fz(ui, 15), f = fz(ui, 15), sf = fz(ui, 13);
    float y = r.y;
    // Title, « Ajouter » (ENTRAÎNEMENT) and the key that places one in a run, when it holds.
    const std::string title = "STARTPOS (" + std::to_string(list.size()) + ")";
    const float kh = ui.s(edit ? 34 : 26);
    float right = r.right();
    if (edit) {
        const std::string add = placing_ == Place::StartPos ? "Annuler la pose" : "Ajouter une startpos";
        const float aw = std::min(ui.text().measure(add, FontFace::Bold, ui.s(14)).x + ui.s(28), r.w * 0.5f);
        UIContext::ButtonStyle st;
        st.small = true;
        st.selected = placing_ == Place::StartPos;
        st.enabled = c.art && c.art->level(c.levelId) && list.size() < StartPosStore::kPerLevel;
        st.fontSize = ui.fitFont(add, FontFace::Bold, 14, aw - ui.s(16), 0.6f);
        if (ui.button("spadd", {r.right() - aw, y, aw, kh}, add, st) && st.enabled) {
            placing_ = placing_ == Place::StartPos ? Place::None : Place::StartPos;
            if (placing_ != Place::None) wantFocus_ = "strip";
        }
        right -= aw + ui.s(12);
    }
    const std::string hint = "Poser ici";
    const float hintH = ui.s(26);
    const float hintW = Glyphs::measureAction(ui, Action::StartPosPlace, hintH) + ui.s(8) + ui.text().measure(hint, FontFace::Body, ui.s(sf)).x;
    const float titleW = ui.text().measure(title, FontFace::Bold, ui.s(hf)).x;
    const bool showHint = titleW + ui.s(16) + hintW <= right - r.x;
    ui.label(ui.elide(title, FontFace::Bold, hf, right - r.x - (showHint ? hintW + ui.s(12) : 0.0f)), {r.x, y + kh * 0.5f}, hf, th.accent, Align::Left,
             FontFace::Bold, VAlign::Middle);
    if (showHint) {
        const float hx = right - hintW;
        const float kw = Glyphs::drawAction(ui, Action::StartPosPlace, {hx, y + kh * 0.5f}, hintH);
        ui.label(hint, {hx + kw + ui.s(8), y + kh * 0.5f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    y += kh + ui.s(12);
    // The buttons and the notes at the bottom; the list takes the room left.
    const float notesH = ui.s(sf + 10.0f) * 2.0f;
    const float buttonsH = ui.s(46);
    const Rect listRect{r.x, y, r.w, std::max(ui.s(60), r.bottom() - y - buttonsH - notesH - ui.s(24))};
    const float rh = ui.s(56), gap = ui.s(6);
    ui.beginScroll("splist", listRect, std::max(1.0f, static_cast<float>(list.size())) * (rh + gap));
    float ry = listRect.y - ui.scrollOffset();
    if (list.empty())
        ui.labelWrapped(edit ? "Aucune startpos pour cette version du niveau. Ajoutez-en une ici sur le tracé de la SOB (en doré), ou posez-en une en "
                               "entraînement là où Nova le suit ; les essais suivants partiront de là."
                             : "Aucune startpos pour cette version du niveau. En entraînement, posez-en une sur le tracé de la SOB (en doré), là où Nova "
                               "le suit ; les essais suivants partiront de là.",
                        {listRect.x + ui.s(6), ry + ui.s(6)}, listRect.w - ui.s(20), sf, th.textDim);
    for (size_t i = 0; i < list.size(); ++i, ry += rh + gap) {
        const StartPosEntry& e = list[i];
        const Rect row{listRect.x, ry, listRect.w - ui.s(14), rh};
        UIContext::ButtonStyle st;
        st.selected = e.id == selectedSp_;
        if (ui.button("sp" + std::to_string(e.id), row, "", st)) {
            selectedSp_ = e.id;
            choose(Pick::StartPos, e.id);
        }
        if (row.bottom() < listRect.y || row.y > listRect.bottom()) continue;
        // « SP n » tag, the place, then the date, where it was placed and the attempts; how it
        // starts at the right (its powers and hearts).
        const std::string tag = "SP " + std::to_string(i + 1);
        const float tw = ui.text().measure(tag, FontFace::Bold, ui.s(f)).x + ui.s(14);
        const Rect tr{row.x + ui.s(10), row.center().y - ui.s(14), tw, ui.s(28)};
        ui.r().fillRoundedRect(tr, ui.s(5), e.id == active ? th.accent : th.accent.withAlpha(60));
        ui.label(tag, tr.center(), f, e.id == active ? Color{8, 12, 26, 255} : th.accent, Align::Center, FontFace::Bold, VAlign::Middle);
        // 2.95: on the SOB path (it can start an attempt) or « à replacer ».
        const bool ok = usable(g, c.levelKey, e);
        const std::string state = ok ? "sur le tracé" : "à replacer";
        const float rightW = std::max(ui.text().measure("sur le tracé", FontFace::Bold, ui.s(sf)).x, ui.text().measure("à replacer", FontFace::Bold, ui.s(sf)).x);
        const float tx = tr.right() + ui.s(12), room = row.right() - ui.s(12) - rightW - ui.s(10) - tx;
        std::string where = c.where ? c.where(e.state.feet) : std::string();
        if (where.empty()) where = "x " + std::to_string(static_cast<int>(std::lround(metersOf(e.state.feet.x - c.levelBounds.x)))) + " m";
        ui.label(ui.elide(where, FontFace::Bold, f, room), {tx, row.y + rh * 0.32f}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string sub = (e.id == active ? std::string("active · ") : std::string()) + "posée le " + dayMonth(e.date) +
                          (e.menu ? " depuis ce menu" : " en jeu") + " · " + std::to_string(e.attempts) + (e.attempts > 1 ? " essais depuis" : " essai depuis");
        ui.label(ui.elide(sub, FontFace::Body, sf, room), {tx, row.y + rh * 0.72f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(ui.elide(state, FontFace::Bold, sf, rightW), {row.right() - ui.s(12), row.center().y}, sf, ok ? th.success : th.danger, Align::Right,
                 FontFace::Bold, VAlign::Middle);
    }
    ui.endScroll();
    // Buttons.
    y = listRect.bottom() + ui.s(12);
    const StartPosEntry* sel = nullptr;
    for (const StartPosEntry& e : list)
        if (e.id == selectedSp_) sel = &e;
    if (!sel || sel->id != confirmDelSp_) confirmDelSp_ = 0;
    const int selRank = sel ? g.startPositions.rank(c.levelKey, sel->id) : -1;
    const float bgap = ui.s(10);
    const int buttons = c.startFrom ? 4 : 2;
    const float bw = (r.w - bgap * static_cast<float>(buttons - 1)) / static_cast<float>(buttons);
    float bx = r.x;
    auto fitted = [&](UIContext::ButtonStyle st, const std::string& label) {
        st.small = true;
        st.fontSize = ui.fitFont(label, FontFace::Bold, 16, bw - ui.s(16), 0.55f);
        return st;
    };
    if (c.startFrom) {
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = sel != nullptr && usable(g, c.levelKey, *sel);  // 2.95: « à replacer »: never
        const std::string go = sel ? "Partir de SP " + std::to_string(selRank + 1) : std::string("Partir d'une startpos");
        if (ui.button("spgo", {bx, y, bw, buttonsH}, go, fitted(p, go)) && p.enabled) c.startFrom(sel->id);
        bx += bw + bgap;
        const std::string none = "Départ de l'entraînement";
        if (ui.button("spnone", {bx, y, bw, buttonsH}, none, fitted({}, none))) c.startFrom(0);
        bx += bw + bgap;
    }
    // « Supprimer », then « Confirmer : supprimer » (a second press deletes it).
    UIContext::ButtonStyle del;
    del.enabled = sel != nullptr;
    del.danger = sel && confirmDelSp_ == sel->id;
    const std::string delLabel = del.danger ? "Confirmer : supprimer" : "Supprimer";
    if (ui.button("spdel", {bx, y, bw, buttonsH}, delLabel, fitted(del, delLabel)) && sel) {
        if (confirmDelSp_ == sel->id) {
            if (pick_ == Pick::StartPos && pickId_ == sel->id) choose(Pick::None, 0);
            g.startPositions.remove(c.levelKey, sel->id);
            g.startPositions.save();
            selectedSp_ = 0;
            confirmDelSp_ = 0;
        } else {
            confirmDelSp_ = sel->id;
        }
    }
    bx += bw + bgap;
    UIContext::ButtonStyle clr;
    clr.enabled = !list.empty();
    clr.danger = confirmClear_;
    const std::string clrLabel = confirmClear_ ? "Confirmer : tout effacer" : "Tout effacer";
    if (ui.button("spclear", {bx, y, bw, buttonsH}, clrLabel, fitted(clr, clrLabel)) && !list.empty()) {
        if (confirmClear_) {
            g.startPositions.clear(c.levelKey);
            g.startPositions.save();
            confirmClear_ = false;
        } else {
            confirmClear_ = true;
        }
    }
    if (list.empty()) confirmClear_ = false;
    // What a StartPos takes, and what starts again.
    y += buttonsH + ui.s(12);
    const float half = r.w * 0.5f;
    auto note = [&](float x, float yy, const std::string& lead, const std::string& text) {
        const float lw = std::min(ui.text().measure(lead, FontFace::Bold, ui.s(sf)).x, half * 0.45f);
        ui.label(ui.elide(lead, FontFace::Bold, sf, lw), {x, yy}, sf, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(ui.elide(text, FontFace::Body, sf, half - lw - ui.s(18)), {x + lw + ui.s(8), yy}, sf, th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
    };
    const float lh = ui.s(sf + 10.0f);
    note(r.x, y + lh * 0.5f, "Sur le tracé", "du meilleur de chaque segment");
    note(r.x + half, y + lh * 0.5f, "Départ", "l'état exact de cette course");
    note(r.x, y + lh * 1.5f, "Compté", "dès le segment suivant entré");
    note(r.x + half, y + lh * 1.5f, "Le niveau", "tel que dans ce replay");
}

void TrainingMarksPanel::drawMeasures(GameServices& g, const Rect& r, const Context& c) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    const bool edit = c.edit;
    const std::vector<MeasurePair> list = g.measures.list(c.levelKey);
    if (!list.empty() && std::none_of(list.begin(), list.end(), [&](const MeasurePair& p) { return p.id == selectedPair_; })) selectedPair_ = list.front().id;
    const float hf = fz(ui, 15), f = fz(ui, 15), sf = fz(ui, 13);
    float y = r.y;
    int complete = 0;
    for (const MeasurePair& p : list) complete += p.complete() ? 1 : 0;
    const std::string title = "POINTS DE MESURE (" + std::to_string(complete) + (complete > 1 ? " PAIRES)" : " PAIRE)");
    const float kh = ui.s(edit ? 34 : 26);
    float right = r.right();
    if (edit) {
        // « Ajouter A → B » (a new pair: A, then B on the strip), or « Poser B n » for a pair
        // still waiting for its B.
        const MeasurePair* waiting = nullptr;
        for (const MeasurePair& p : list)
            if (!p.complete()) waiting = &p;
        const std::string add = placing_ == Place::Gate ? std::string("Annuler la pose")
                                : waiting               ? "Poser B" + std::to_string(waiting->id)
                                                        : std::string("Ajouter A → B");
        const float aw = std::min(ui.text().measure(add, FontFace::Bold, ui.s(14)).x + ui.s(28), r.w * 0.5f);
        UIContext::ButtonStyle st;
        st.small = true;
        st.selected = placing_ == Place::Gate;
        st.enabled = c.art && c.art->level(c.levelId) && (waiting || list.size() < MeasureStore::kPerLevel);
        st.fontSize = ui.fitFont(add, FontFace::Bold, 14, aw - ui.s(16), 0.6f);
        if (ui.button("madd", {r.right() - aw, y, aw, kh}, add, st) && st.enabled) {
            placing_ = placing_ == Place::Gate ? Place::None : Place::Gate;
            if (placing_ != Place::None) wantFocus_ = "strip";
        }
        right -= aw + ui.s(12);
    }
    const float hintH = ui.s(26);
    const std::string hint = "Poser A, puis B";
    const float hintW = Glyphs::measureAction(ui, Action::MeasurePlace, hintH) + ui.s(8) + ui.text().measure(hint, FontFace::Body, ui.s(sf)).x;
    const float titleW = ui.text().measure(title, FontFace::Bold, ui.s(hf)).x;
    const bool showHint = titleW + ui.s(16) + hintW <= right - r.x;
    ui.label(ui.elide(title, FontFace::Bold, hf, right - r.x - (showHint ? hintW + ui.s(12) : 0.0f)), {r.x, y + kh * 0.5f}, hf, kGate, Align::Left,
             FontFace::Bold, VAlign::Middle);
    if (showHint) {
        const float hx = right - hintW;
        const float kw = Glyphs::drawAction(ui, Action::MeasurePlace, {hx, y + kh * 0.5f}, hintH);
        ui.label(hint, {hx + kw + ui.s(8), y + kh * 0.5f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    y += kh + ui.s(12);
    // The bottom: buttons, three options, the note; the list takes the room left.
    const float buttonsH = ui.s(46), toggleH = ui.s(44);
    const std::string warn = "Gardés sur cet ordinateur, pour cette version du niveau. Si le niveau change, les anciens repères restent avec "
                             "l'ancienne version et leurs temps ne sont plus comparés.";
    const float warnH = ui.text().measureWrapped(warn, FontFace::Body, ui.s(sf), r.w).y;
    const Rect listRect{r.x, y, r.w, std::max(ui.s(60), r.bottom() - y - buttonsH - toggleH * 3.0f - warnH - ui.s(40))};
    const float rh = ui.s(56), gap = ui.s(6);
    ui.beginScroll("mlist", listRect, std::max(1.0f, static_cast<float>(list.size())) * (rh + gap));
    float ry = listRect.y - ui.scrollOffset();
    if (list.empty())
        ui.labelWrapped(edit ? "Aucun point de mesure. Ajoutez A puis B ici ou en entraînement : franchir A lance un chrono, franchir B l'arrête."
                             : "Aucun point de mesure. En entraînement, posez A puis B : franchir A lance un chrono, franchir B l'arrête.",
                        {listRect.x + ui.s(6), ry + ui.s(6)}, listRect.w - ui.s(20), sf, th.textDim);
    for (const MeasurePair& p : list) {
        const Rect row{listRect.x, ry, listRect.w - ui.s(14), rh};
        ry += rh + gap;
        const std::string n = std::to_string(p.id);
        UIContext::ButtonStyle st;
        st.selected = p.id == selectedPair_;
        if (renaming_ == p.id) {
            // The new name, typed in place (Entrée validates).
            const Rect field{row.x + ui.s(8), row.y + ui.s(6), row.w - ui.s(16), row.h - ui.s(12)};
            ui.setFocus("mrename");
            if (ui.textField("mrename", field, renameText_, "Mesure " + n, MeasureStore::kNameMax)) {
                g.measures.rename(c.levelKey, p.id, renameText_);
                g.measures.save();
                renaming_ = 0;
            }
            continue;
        }
        if (ui.button("m" + n, row, "", st)) {
            selectedPair_ = p.id;
            if (!((pick_ == Pick::GateA || pick_ == Pick::GateB) && pickId_ == p.id)) choose(Pick::GateA, p.id);
        }
        if (row.bottom() < listRect.y || row.y > listRect.bottom()) continue;
        const std::string tag = p.complete() ? "A" + n + "→B" + n : "A" + n + "…";
        const float tw = ui.text().measure(tag, FontFace::Bold, ui.s(f)).x + ui.s(14);
        const Rect tr{row.x + ui.s(10), row.center().y - ui.s(14), tw, ui.s(28)};
        ui.r().fillRoundedRect(tr, ui.s(5), kGate.withAlpha(p.complete() ? 230 : 90));
        ui.label(tag, tr.center(), f, Color{40, 28, 8, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        const std::string best = p.best > 0.0f ? formatGhostSeconds(p.best) : std::string("—");
        const float bestW = ui.text().measure(best, FontFace::Bold, ui.s(f)).x;
        const float barsW = ui.s(70);
        const float tx = tr.right() + ui.s(12), room = row.right() - ui.s(12) - barsW - ui.s(14) - bestW - ui.s(14) - tx;
        {
            i18n::Verbatim words;  // as the player named it
            const std::string name = p.name.empty() ? std::string() : p.name;
            if (!name.empty())
                ui.label(ui.elide(name, FontFace::Bold, f, room), {tx, row.y + rh * 0.32f}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        }
        if (p.name.empty())
            ui.label(ui.elide("Mesure " + n, FontFace::Bold, f, room), {tx, row.y + rh * 0.32f}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        const std::string sub = !p.complete()          ? std::string("B pas encore posé")
                                : p.last.empty()       ? std::string("pas encore d'essai")
                                                       : std::to_string(p.attempts) + (p.attempts > 1 ? " essais" : " essai") + " · dernier " +
                                                       formatGhostSeconds(p.last.back());
        ui.label(ui.elide(sub, FontFace::Body, sf, room), {tx, row.y + rh * 0.72f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(best, {row.right() - ui.s(12) - barsW - ui.s(14), row.center().y}, f, p.best > 0.0f ? Color{255, 214, 90, 255} : th.textDim,
                 Align::Right, FontFace::Bold, VAlign::Middle);
        // The last times as small bars (the best in gold).
        const size_t count = std::min<size_t>(p.last.size(), 7);
        if (count > 0) {
            float lo = 1e9f, hi = 0.0f;
            for (size_t i = p.last.size() - count; i < p.last.size(); ++i) lo = std::min(lo, p.last[i]), hi = std::max(hi, p.last[i]);
            const float slot = barsW / 7.0f, bh = rh - ui.s(22);
            for (size_t k = 0; k < count; ++k) {
                const float v = p.last[p.last.size() - count + k];
                const float h = bh * (hi > lo ? 0.35f + 0.65f * (v - lo) / (hi - lo) : 0.6f);
                const bool isBest = p.best > 0.0f && std::llround(v * 1000.0f) == std::llround(p.best * 1000.0f);
                ui.r().fillRect({row.right() - ui.s(12) - barsW + slot * static_cast<float>(k), row.bottom() - ui.s(11) - h, slot * 0.7f, h},
                                isBest ? Color{255, 214, 90, 255} : th.textDim.withAlpha(150));
            }
        }
    }
    ui.endScroll();
    // Rename, move, delete.
    y = listRect.bottom() + ui.s(12);
    const MeasurePair* sel = g.measures.find(c.levelKey, selectedPair_);
    if (!sel || sel->id != confirmDelPair_) confirmDelPair_ = 0;
    const float bgap = ui.s(10);
    const int buttons = c.movePair ? 3 : 2;
    const float bw = (r.w - bgap * static_cast<float>(buttons - 1)) / static_cast<float>(buttons);
    float bx = r.x;
    auto fitted = [&](UIContext::ButtonStyle st, const std::string& label) {
        st.small = true;
        st.fontSize = ui.fitFont(label, FontFace::Bold, 16, bw - ui.s(16), 0.55f);
        return st;
    };
    UIContext::ButtonStyle on;
    on.enabled = sel != nullptr && renaming_ == 0;
    const std::string ren = "Renommer";
    if (ui.button("mren", {bx, y, bw, buttonsH}, ren, fitted(on, ren)) && on.enabled) {
        renaming_ = sel->id;
        renameText_ = sel->name;
    }
    bx += bw + bgap;
    if (c.movePair) {
        const std::string mv = "Déplacer A / B";
        if (ui.button("mmove", {bx, y, bw, buttonsH}, mv, fitted(on, mv)) && on.enabled) c.movePair(sel->id);
        bx += bw + bgap;
    }
    // « Supprimer », then « Confirmer : supprimer ».
    UIContext::ButtonStyle del = on;
    del.danger = sel && confirmDelPair_ == sel->id;
    const std::string delLabel = del.danger ? "Confirmer : supprimer" : "Supprimer";
    if (ui.button("mdel", {bx, y, bw, buttonsH}, delLabel, fitted(del, delLabel)) && on.enabled) {
        if (confirmDelPair_ == sel->id) {
            if ((pick_ == Pick::GateA || pick_ == Pick::GateB) && pickId_ == sel->id) choose(Pick::None, 0);
            g.measures.remove(c.levelKey, sel->id);
            g.measures.save();
            selectedPair_ = 0;
            confirmDelPair_ = 0;
        } else {
            confirmDelPair_ = sel->id;
        }
    }
    // The options (kept with the measures and the StartPos).
    y += buttonsH + ui.s(10);
    bool showSp = g.startPositions.show, showGates = g.measures.showGates, inReplays = g.measures.inReplays;
    if (ui.toggle("optsp", {r.x, y, r.w, toggleH - ui.s(4)}, "Montrer les startpos dans le niveau (rémanence)", showSp)) {
        g.startPositions.show = showSp;
        g.startPositions.save();
    }
    y += toggleH;
    if (ui.toggle("optgates", {r.x, y, r.w, toggleH - ui.s(4)}, "Montrer les portes de mesure", showGates)) {
        g.measures.showGates = showGates;
        g.measures.save();
    }
    y += toggleH;
    if (ui.toggle("optreplays", {r.x, y, r.w, toggleH - ui.s(4)}, "Mesurer aussi dans les replays (les vôtres et ceux en ligne)", inReplays)) {
        g.measures.inReplays = inReplays;
        g.measures.save();
    }
    y += toggleH + ui.s(6);
    ui.labelWrapped(warn, {r.x, y}, r.w, sf, th.warm);
    if (renaming_ > 0 && ui.input().menuPressed(Action::MenuBack)) {
        renaming_ = 0;
        backUsed_ = true;
    }
}

}  // namespace cosmic
