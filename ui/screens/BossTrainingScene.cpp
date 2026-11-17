// Boss statistics → « Entraînement »: the settings of a training against a boss (hearts,
// power slots, the powers in them, all its phases or one of them), then its own arena
// (game/gameplay/BossTraining.h), and the end of a training (time, damage, phase reached;
// Recommencer, Changer les réglages, Retour). A training never counts: nothing of it reaches
// the achievements, the statistics, the records, the progression or Cosmic Online.
// 2.41: « Lieu d'apparition »: the usual start, or a place of the arena picked on its map
// (mouse, arrows, gamepad) among the safe ones (probeTrainingArena).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

#include "engine/audio/AudioSystem.h"
#include "engine/core/I18n.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/player/Player.h"
#include "game/player/PlayerConfig.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/MenuBackground.h"
#include "ui/widgets/Glyphs.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// The size (from `size`, never below 11.5 pixels) at which `t` fits `room`; what still does
/// not fit is shortened with « … » by the caller.
float fit(UIContext& ui, const std::string& t, FontFace face, float size, float room, float minScale = 0.8f) {
    return std::max(ui.fitFont(t, face, fz(ui, size), room, minScale), 11.5f / ui.s(1.0f));
}

const char* const kNotice = "Entraînement — ne compte pas pour les succès ni les statistiques";

/// A name as shown, lower case and without accents, to sort names alphabetically.
std::string sortKey(const std::string& name) {
    // The Latin-1 letters (U+00C0..U+00FF) without their accents.
    static const char kBase[64] = {'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                                   'd', 'n', 'o', 'o', 'o', 'o', 'o', ' ', 'o', 'u', 'u', 'u', 'u', 'y', ' ', 's',
                                   'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                                   'd', 'n', 'o', 'o', 'o', 'o', 'o', ' ', 'o', 'u', 'u', 'u', 'u', 'y', ' ', 'y'};
    const std::string shown = i18n::tr(name);
    std::string k;
    for (size_t i = 0; i < shown.size(); ++i) {
        auto c = static_cast<unsigned char>(shown[i]);
        auto next = i + 1 < shown.size() ? static_cast<unsigned char>(shown[i + 1]) : 0;
        if (c == 0xC3 && next >= 0x80 && next <= 0xBF) {
            k += kBase[next - 0x80];
            ++i;
        } else if (c == 0xC5 && (next == 0x92 || next == 0x93)) {  // Œ œ
            k += 'o';
            k += 'e';
            ++i;
        } else {
            k += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        }
    }
    return k;
}

std::string fightTime(float s) {
    int t = static_cast<int>(std::lround(std::max(0.0f, s) * 10.0f));
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d,%d", t / 600, (t / 10) % 60, t % 10);
    return b;
}

/// The notice, in a warm pill sized to its text (`room` at most); returns its height.
float drawNotice(UIContext& ui, Vec2 leftMid, float room) {
    float fs = fit(ui, kNotice, FontFace::Bold, 17, room - ui.s(36), 0.7f);
    float tw = ui.text().measure(kNotice, FontFace::Bold, ui.s(fs)).x;
    Rect pill{leftMid.x, leftMid.y - ui.s(fs + 18) * 0.5f, std::min(room, tw + ui.s(36)), ui.s(fs + 18)};
    ui.r().fillRoundedRect(pill, pill.h * 0.5f, ui.theme().warm.withAlpha(30));
    ui.r().drawRoundedRect(pill, pill.h * 0.5f, ui.theme().warm.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
    ui.label(ui.elide(kNotice, FontFace::Bold, fs, pill.w - ui.s(36)), {pill.x + ui.s(18), pill.center().y}, fs, ui.theme().warm, Align::Left,
             FontFace::Bold, VAlign::Middle);
    return pill.h;
}

// =================================================================== « Lieu d'apparition » (2.41)

/// The start of the run, in words: a place of the arena (its side of the boss, on the ground or
/// on a platform).
std::string spotText(const TrainingArena& a, const TrainingSpot& s) {
    const bool left = s.feet.x < a.boss.center().x;
    if (left) return s.ledge ? "Départ : dans l'arène, à gauche du boss, sur une plateforme" : "Départ : dans l'arène, à gauche du boss, au sol";
    return s.ledge ? "Départ : dans l'arène, à droite du boss, sur une plateforme" : "Départ : dans l'arène, à droite du boss, au sol";
}

/// The places of a training's arena (probeTrainingArena) and the level drawn small around it:
/// a world of the level set up as the training starts it (silent, never played nor shown as a
/// run), kept to draw the arena as the phase chosen leaves it (Singularis's floor gone…).
class SpawnArena {
public:
    explicit SpawnArena(GameServices& g) : g_(g) {}

    /// The arena of `s` (its phase), computed now when it is not yet (a few milliseconds).
    /// Null when the boss or its arena is not in the level.
    const TrainingArena* arena(const LevelData& level, const BossTrainingSetup& s) {
        if (!done_ || phase_ != s.phase) compute(level, s);
        return arena_ ? &*arena_ : nullptr;
    }
    /// The arena last computed (null: none yet, or none in the level).
    const TrainingArena* last() const { return arena_ ? &*arena_ : nullptr; }
    bool computed(int phase) const { return done_ && phase_ == phase; }

    /// A few milliseconds of the level's terrain baked, the part shown first (each frame).
    void bake() {
        if (!world_ || terrainLeft_ == 0) return;
        Mount m(*this);
        terrainLeft_ = renderer_.bakeTerrain(g_.app.renderer(), 6.0, lastWindow_.w > 0.0f ? &lastWindow_ : nullptr);
    }

    /// Where the arena is drawn: the frame, the level's area shown, the scale.
    struct Map {
        Rect dst, win;
        float k = 0.0f;
        Vec2 toScreen(Vec2 p) const { return {dst.x + (p.x - win.x) * k, dst.y + (p.y - win.y) * k}; }
        Vec2 toWorld(Vec2 p) const { return {win.x + (p.x - dst.x) / k, win.y + (p.y - dst.y) / k}; }
        bool valid() const { return k > 0.0f; }
    };

    /// The size of the player's body drawn on a map (never too small to be seen).
    static Vec2 bodySize(const UIContext& ui, float k) { return {std::max(ui.s(8), 10.0f * k), std::max(ui.s(18), 22.0f * k)}; }

    /// The arena in `frame`: the level around it, dimmed outside the arena; the safe ground (a
    /// green line, a dot per place when `dots`); the boss (`known`: in its colours, else its
    /// shadow) in a red frame; the usual start (grey) when it is in view; `hover` (a translucent
    /// body) and `chosen` (the player's body, facing the boss; -1: the usual start is chosen).
    Map draw(UIContext& ui, const Rect& frame, int chosen, int hover, bool dots, const EnemyDefinition* def, bool known, float t) {
        Renderer& r = ui.r();
        const UITheme& th = ui.theme();
        r.fillRoundedRect(frame, ui.s(8), Color{5, 8, 18, 255});
        if (!arena_ || !world_) {
            const char* msg = done_ ? "Aucune arène trouvée pour ce boss." : "Calcul de l'arène…";
            float f = fit(ui, msg, FontFace::Body, 16, frame.w - ui.s(20), 0.75f);
            ui.label(ui.elide(msg, FontFace::Body, f, frame.w - ui.s(20)), frame.center(), f, th.textDim, Align::Center, FontFace::Body,
                     VAlign::Middle);
            return {};
        }
        const TrainingArena& a = *arena_;
        const Map m = layout(frame);
        lastWindow_ = m.win;
        {
            WorldView v;
            v.scale = m.k;
            v.screenW = static_cast<int>(m.dst.w);
            v.screenH = static_cast<int>(m.dst.h);
            v.topLeft = {m.win.x - m.dst.x / m.k, m.win.y - m.dst.y / m.k};
            v.origin = m.dst.pos();
            RenderOptions opt;
            opt.lighting = false;
            opt.mapView = true;
            Mount mount(*this);
            r.pushClip(m.dst);
            renderer_.render(r, g_.app.resources(), *world_, v, 1.0f, opt);
            r.setView(WorldView{});
            r.popClip();
        }
        r.pushClip(m.dst);
        // Outside the arena: dimmed; the arena framed.
        const Vec2 z0 = m.toScreen(a.zone.pos()), z1 = m.toScreen({a.zone.right(), a.zone.bottom()});
        const Color dim{4, 6, 14, 150};
        r.fillRect({m.dst.x, m.dst.y, m.dst.w, z0.y - m.dst.y}, dim);
        r.fillRect({m.dst.x, z1.y, m.dst.w, m.dst.bottom() - z1.y}, dim);
        r.fillRect({m.dst.x, z0.y, z0.x - m.dst.x, z1.y - z0.y}, dim);
        r.fillRect({z1.x, z0.y, m.dst.right() - z1.x, z1.y - z0.y}, dim);
        r.drawRect({z0.x, z0.y, z1.x - z0.x, z1.y - z0.y}, th.accent.withAlpha(110), std::max(1.0f, ui.s(1.5f)));
        const Color ink{6, 8, 16, 230};
        // The safe ground: a line under each run of places, a dot per place.
        const float lw = std::max(2.0f, ui.s(3.0f));
        const Color safe = th.success.withAlpha(225);
        for (const auto& [y, xs] : runs_) {
            Vec2 p0 = m.toScreen({xs.first - TrainingArena::kStep * 0.5f + 1.0f, y}), p1 = m.toScreen({xs.second + TrainingArena::kStep * 0.5f - 1.0f, y});
            r.drawLine(p0 + Vec2{0.0f, lw * 0.5f}, p1 + Vec2{0.0f, lw * 0.5f}, ink, lw + 3.0f);
            r.drawLine(p0 + Vec2{0.0f, lw * 0.5f}, p1 + Vec2{0.0f, lw * 0.5f}, safe, lw);
        }
        if (dots && TrainingArena::kStep * m.k >= ui.s(7))
            for (const TrainingSpot& s : a.spots) r.fillCircle(m.toScreen(s.feet) - Vec2{0.0f, lw * 0.5f + std::max(2.0f, ui.s(3))}, std::max(1.5f, ui.s(2.2f)), safe);
        // The boss: its body framed in red (kept clear), its look over it.
        const Vec2 b0 = m.toScreen(a.boss.pos());
        const Rect bb{b0.x, b0.y, a.boss.w * m.k, a.boss.h * m.k};
        if (def) drawCreature(g_, *def, Rect::fromCenter(bb.center(), Vec2{1.0f, 1.0f} * std::max(bb.w, bb.h) * 2.2f), t, known ? Color::white() : Color{0, 0, 0, 150});
        r.drawRoundedRect(bb.expanded(ui.s(3)), ui.s(4), th.danger.withAlpha(220), std::max(1.5f, ui.s(2)));
        // A body at `feet`, facing the boss (a notch on its side).
        const Vec2 bs = bodySize(ui, m.k);
        auto body = [&](Vec2 feet, Color fill, bool facing) {
            const Vec2 f = m.toScreen(feet);
            const Rect b{f.x - bs.x * 0.5f, f.y - bs.y, bs.x, bs.y};
            r.fillRoundedRect(b.expanded(2.0f), bs.x * 0.3f + 2.0f, ink);
            r.fillRoundedRect(b, bs.x * 0.3f, fill);
            if (facing) {
                const float dir = a.boss.center().x < feet.x ? -1.0f : 1.0f;
                const float tip = b.center().x + dir * (bs.x * 0.5f + bs.x * 0.8f), base = b.center().x + dir * (bs.x * 0.5f + 1.0f);
                const float cy = b.y + bs.y * 0.3f, hh = bs.x * 0.45f;
                r.fillTriangle({base, cy - hh - 1.5f}, {tip + dir * 1.5f, cy}, {base, cy + hh + 1.5f}, ink);
                r.fillTriangle({base, cy - hh}, {tip, cy}, {base, cy + hh}, fill);
            }
            return b;
        };
        // The usual start, when in view.
        const Vec2 us = m.toScreen(a.usualStart);
        const bool usualShown = m.dst.contains(us - Vec2{0.0f, 2.0f});
        if (usualShown) {
            Rect ub = body(a.usualStart, Color{150, 160, 180, 235}, false);
            if (chosen < 0) r.drawRing(ub.center(), std::max(bs.y * 0.9f, ui.s(16)) + std::sin(t * 5.0f) * ui.s(2), std::max(1.5f, ui.s(2.5f)), Color::white());
        }
        if (hover >= 0 && hover < static_cast<int>(a.spots.size()) && hover != chosen) body(a.spots[static_cast<size_t>(hover)].feet, th.accent.withAlpha(120), true);
        if (chosen >= 0 && chosen < static_cast<int>(a.spots.size())) {
            Rect cb = body(a.spots[static_cast<size_t>(chosen)].feet, th.accent, true);
            r.drawRing(cb.center(), std::max(bs.y * 0.9f, ui.s(16)) + std::sin(t * 5.0f) * ui.s(2), std::max(1.5f, ui.s(2.5f)), th.accent);
        }
        r.popClip();
        return m;
    }

    /// Where draw() puts the arena in `frame` (to read the mouse before drawing).
    Map layout(const Rect& frame) const {
        Map m;
        if (!arena_ || !world_ || frame.w <= 1.0f || frame.h <= 1.0f) return m;
        m.win = window(frame);
        if (m.win.w <= 0.0f || m.win.h <= 0.0f) return m;
        m.k = std::min(frame.w / m.win.w, frame.h / m.win.h);
        m.dst = {std::floor(frame.center().x - m.win.w * m.k * 0.5f), std::floor(frame.center().y - m.win.h * m.k * 0.5f), std::floor(m.win.w * m.k),
                 std::floor(m.win.h * m.k)};
        return m;
    }

    /// The level's area shown in `frame`: the arena with some room around, at the frame's
    /// proportions, inside the level.
    Rect window(const Rect& frame) const {
        if (!arena_ || !world_) return {};
        const Rect& z = arena_->zone;
        const float mx = std::max(64.0f, z.w * 0.06f);
        Rect win{z.x - mx, z.y - 96.0f, z.w + 2.0f * mx, z.h + 96.0f + 48.0f};
        const float aspect = frame.w / std::max(1.0f, frame.h);
        if (win.w / win.h < aspect) {
            const float nw = win.h * aspect;
            win.x -= (nw - win.w) * 0.5f;
            win.w = nw;
        } else {
            const float nh = win.w / aspect;
            win.y -= (nh - win.h) * 0.5f;
            win.h = nh;
        }
        const Rect& lb = world_->level().bounds;
        if (lb.w > 0.0f && lb.h > 0.0f) {
            if (win.w >= lb.w) {
                win.x = lb.x;
                win.w = lb.w;
            } else {
                win.x = std::clamp(win.x, lb.x, lb.right() - win.w);
            }
            if (win.h >= lb.h) {
                win.y = lb.y;
                win.h = lb.h;
            } else {
                win.y = std::clamp(win.y, lb.y, lb.bottom() - win.h);
            }
        }
        return win;
    }

private:
    /// The level's own assets (a level of a package) while it is read or drawn.
    struct Mount {
        explicit Mount(SpawnArena& s) : res(s.g_.app.resources()), on(!s.mount_.empty()) {
            if (on) res.pushMount(Paths::fromUtf8(s.mount_));
        }
        ~Mount() {
            if (on) res.popMount();
        }
        ResourceManager& res;
        bool on;
    };

    void compute(const LevelData& level, const BossTrainingSetup& s) {
        const auto t0 = std::chrono::steady_clock::now();
        if (const LevelEntry* e = g_.content.level(level.id)) mount_ = Paths::toUtf8(e->assetRoot);
        Mount mount(*this);
        ResourceManager& res = g_.app.resources();
        // Its own silent audio: nothing of this world is ever heard (music, sounds).
        WorldServices ws{res, silence_, g_.content, nullptr, 0.0f, 0.6f};
        auto w = std::make_unique<GameWorld>(ws, WorldMode::Play);
        w->silent = true;
        w->setKeysPerSlot(true);
        w->load(level);
        w->seedGameplay(levelSeed(level.id));
        arena_ = probeTrainingArena(*w, s);
        w->player().setHidden(true);  // the map shows the places, not a player
        if (!rendererBuilt_) {
            renderer_.buildProgressive(res, *w);
            terrainLeft_ = renderer_.terrain().pendingChunks();
            rendererBuilt_ = true;
        }
        world_ = std::move(w);
        done_ = true;
        phase_ = s.phase;
        // The runs of places along each surface (one line each on the map).
        runs_.clear();
        if (arena_) {
            std::map<float, std::vector<float>> rows;
            for (const TrainingSpot& p : arena_->spots) rows[p.feet.y].push_back(p.feet.x);
            for (auto& [y, xs] : rows) {
                std::sort(xs.begin(), xs.end());
                float from = xs.front(), to = xs.front();
                for (size_t i = 1; i <= xs.size(); ++i) {
                    if (i < xs.size() && xs[i] - to <= TrainingArena::kStep + 0.5f) {
                        to = xs[i];
                        continue;
                    }
                    runs_.push_back({y, {from, to}});
                    if (i < xs.size()) from = to = xs[i];
                }
            }
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        Log::info("Entraînement : arène de {} (phase {}) calculée en {} ms, {} lieux d'apparition", s.bossId, s.phase, ms,
                  arena_ ? arena_->spots.size() : 0);
    }

    GameServices& g_;
    AudioSystem silence_;  // never initialised: silent
    std::string mount_;
    std::unique_ptr<GameWorld> world_;
    GameRenderer renderer_;
    bool rendererBuilt_ = false;
    size_t terrainLeft_ = 0;
    Rect lastWindow_;
    std::optional<TrainingArena> arena_;
    std::vector<std::pair<float, std::pair<float, float>>> runs_;  // y, first and last x
    bool done_ = false;
    int phase_ = -2;
};

/// « Lieu d'apparition »: the arena of the training, big, to pick where the player appears.
/// Mouse: a click on a safe place; keyboard / gamepad: the arrows move the marker (along the
/// ground, from a platform to another), Valider confirms; « Point habituel » goes back to the
/// usual start.
class SpawnPickerScene : public Scene {
public:
    struct Request {
        SpawnArena* arena = nullptr;
        const LevelData* level = nullptr;
        BossTrainingSetup setup;
        const EnemyDefinition* def = nullptr;
        bool known = false;
        std::string who, phase;  // the boss, the phase trained (subtitle)
        std::string usualText;   // the usual start in words
        std::string returnFocus;  // the widget of the screen below focused once closed
        std::function<void(std::optional<Vec2>)> done;
    };
    SpawnPickerScene(GameServices& g, Request req) : g_(g), req_(std::move(req)) {}
    bool isOverlay() const override { return true; }

    void onEnter() override {
        g_.ui.setFocus("bspawn/map");
        if (req_.arena->computed(req_.setup.phase)) start();
    }

    void update(float dt) override {
        t_ += dt;
        if (!started_ && shown_) start();  // computed a frame after its « Calcul » line is seen
        req_.arena->bake();
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const UITheme& th = ui.theme();
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{3, 5, 12, 225});
        ui.pushId("bspawn");
        const float margin = std::min(ui.s(48), W * 0.04f);
        const Rect p{margin, std::min(ui.s(30), H * 0.04f), W - 2 * margin, H - 2 * std::min(ui.s(30), H * 0.04f)};
        ui.panel(p, true, 18);
        const float pad = ui.s(28), x = p.x + pad, w = p.w - 2 * pad;
        float y = p.y + ui.s(20);
        // Title, then the boss and the phase.
        const float tf = ui.fitFont("LIEU D'APPARITION", FontFace::Title, 36, w, 0.6f);
        const float titleH = ui.text().measure("LIEU D'APPARITION", FontFace::Title, ui.s(tf)).y;
        ui.label("LIEU D'APPARITION", {x, y + titleH * 0.5f}, tf, th.text, Align::Left, FontFace::Title, VAlign::Middle);
        y += titleH + ui.s(8);
        const std::string sub = req_.who + "  ·  " + req_.phase;
        const float sf = fit(ui, sub, FontFace::Bold, 19, w, 0.75f);
        const float subH = ui.text().measure(sub, FontFace::Bold, ui.s(sf)).y;
        ui.label(ui.elide(sub, FontFace::Bold, sf, w), {x, y + subH * 0.5f}, sf, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        y += subH + ui.s(14);
        // From the bottom: the buttons, the keys, the legend, the place chosen in words.
        const float bh = ui.s(56), by = p.bottom() - pad - bh;
        const float keyH = ui.s(26), hf = fz(ui, 15);
        const float keysY = by - ui.s(14) - keyH * 0.5f;
        const float lf = fz(ui, 15), legendH = std::max(ui.s(lf + 8), ui.s(20));
        const float legendY = keysY - keyH * 0.5f - ui.s(10) - legendH * 0.5f;
        const float cf = fz(ui, 19), chosenH = ui.s(cf + 10);
        const float chosenY = legendY - legendH * 0.5f - ui.s(8) - chosenH * 0.5f;
        const Rect frame{x, y, w, chosenY - chosenH * 0.5f - ui.s(12) - y};
        const TrainingArena* a = started_ ? req_.arena->last() : nullptr;
        // The map: a stop of the keyboard / gamepad; its arrows move the marker.
        bool overMap = false;
        const bool act = ui.hotspot("map", frame, &overMap);
        int hover = -1;
        if (a) {
            const SpawnArena::Map m = req_.arena->layout(frame);
            if (m.valid() && overMap && ui.usingPointer()) hover = a->nearest(m.toWorld(ui.input().mousePos()), std::max(24.0f, ui.s(28) / m.k));
        }
        req_.arena->draw(ui, frame, a ? chosen_ : -1, hover, true, req_.def, req_.known, t_);
        shown_ = true;
        const bool mapFocus = ui.focused("map");
        ui.r().drawRoundedRect(frame, ui.s(8), mapFocus ? th.accent : th.border, std::max(1.0f, ui.s(mapFocus ? 2.5f : 1.5f)));
        bool validate = false;
        if (a && ui.acceptsInput()) {
            InputManager& in = ui.input();
            if (mapFocus) {
                const int dx = in.menuPressed(Action::MenuLeft) ? -1 : in.menuPressed(Action::MenuRight) ? 1 : 0;
                const int dy = dx != 0 ? 0 : in.menuPressed(Action::MenuUp) ? -1 : in.menuPressed(Action::MenuDown) ? 1 : 0;
                if (dx != 0 || dy != 0) {
                    // From the usual start, the first arrow takes the place proposed first.
                    const int n = chosen_ < 0 ? a->suggested() : a->step(chosen_, dx, dy);
                    if (n >= 0) {
                        chosen_ = n;
                        g_.uiSound("ui_move", 0.5f);
                        ui.consumeNavigation(true, true);
                    } else if (dx != 0) {
                        ui.consumeNavigation(true, false);  // the end of the arena that way
                    }
                }
            }
            if (act) {
                if (overMap && ui.usingPointer()) {
                    if (hover >= 0) {
                        chosen_ = hover;
                        g_.uiSound("ui_confirm", 0.5f);
                    } else {
                        g_.uiSound("ui_error");
                        say("Pas de lieu sûr ici : choisissez sur une ligne verte.");
                    }
                } else {
                    validate = true;  // Valider on the map
                }
            }
        }
        // The place chosen, in words.
        std::string chosenText = chosen_ >= 0 && a && chosen_ < static_cast<int>(a->spots.size()) ? spotText(*a, a->spots[static_cast<size_t>(chosen_)])
                                                                                                    : req_.usualText;
        if (msgTimer_ > 0.0f) chosenText = msg_;
        const float ctf = fit(ui, chosenText, FontFace::Bold, cf, w, 0.75f);
        ui.label(ui.elide(chosenText, FontFace::Bold, ctf, w), {x, chosenY}, ctf, msgTimer_ > 0.0f ? th.warm : th.text, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        drawLegend(ui, x, legendY, w, legendH, lf, a && a->spots.empty());
        drawKeys(ui, x, keysY, w, keyH, hf);
        // Point habituel, Annuler, Valider.
        const float gap = ui.s(16), bw = std::min(ui.s(330), (w - 2 * gap) / 3.0f);
        UIContext::ButtonStyle primary;
        primary.primary = true;
        primary.enabled = a != nullptr;
        UIContext::ButtonStyle plain;
        plain.enabled = a != nullptr;
        const bool usual = ui.button("usual", {x, by, bw, bh}, "Point habituel", plain);
        const bool cancel = ui.button("cancel", {x + w - 2 * bw - gap, by, bw, bh}, "Annuler");
        validate = ui.button("ok", {x + w - bw, by, bw, bh}, "Valider", primary) || validate;
        ui.popId();
        const bool back = cancel || ui.backPressed();
        ui.end();  // before the focus goes back to the screen below (closing sets it)
        if (usual) {
            finish(std::nullopt);
        } else if (validate && a) {
            finish(chosen_ >= 0 && chosen_ < static_cast<int>(a->spots.size()) ? std::optional<Vec2>(a->spots[static_cast<size_t>(chosen_)].feet)
                                                                                : std::nullopt);
        } else if (back) {
            close();
        }
    }

private:
    void close() {
        g_.app.popScene();
        if (!req_.returnFocus.empty()) g_.ui.setFocus(req_.returnFocus);
    }

    void start() {
        started_ = true;
        const TrainingArena* a = req_.arena->arena(*req_.level, req_.setup);
        chosen_ = a && req_.setup.arenaSpawn ? a->find(*req_.setup.arenaSpawn) : -1;
    }

    void finish(std::optional<Vec2> v) {
        auto done = req_.done;
        close();
        if (done) done(v);
    }

    void say(const std::string& m) {
        msg_ = m;
        msgTimer_ = 3.0f;
    }

    /// What the marks of the map are, on one line (shortened together when the room lacks).
    void drawLegend(UIContext& ui, float x, float cy, float w, float h, float size, bool none) {
        const UITheme& th = ui.theme();
        Renderer& r = ui.r();
        if (none) {
            const char* t = "Aucun lieu sûr dans l'arène à cette phase : seul le point habituel est possible.";
            float f = fit(ui, t, FontFace::Body, size, w, 0.75f);
            ui.label(ui.elide(t, FontFace::Body, f, w), {x, cy}, f, th.warm, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        struct Item {
            int kind;
            const char* text;
        };
        const Item items[] = {{0, "Lieux sûrs"}, {1, "Lieu choisi"}, {2, "Boss (gardez vos distances)"}, {3, "Point habituel (s'il est dans la vue)"}};
        const float icon = h * 0.9f, igap = ui.s(8), sep = ui.s(26);
        float f = size, total = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {
            total = 0.0f;
            for (const Item& it : items) total += icon + igap + ui.text().measure(it.text, FontFace::Body, ui.s(f)).x + sep;
            total -= sep;
            if (total <= w || pass == 1) break;
            f = std::max(11.5f / ui.s(1.0f), f * std::max(0.7f, w / total));
        }
        float cx = x;
        for (const Item& it : items) {
            const float tw = ui.text().measure(it.text, FontFace::Body, ui.s(f)).x;
            if (cx + icon + igap + tw > x + w + 0.5f) break;  // never past the panel
            const Rect ic{cx, cy - icon * 0.5f, icon, icon};
            switch (it.kind) {
                case 0:
                    r.drawLine({ic.x, ic.center().y + ui.s(3)}, {ic.right(), ic.center().y + ui.s(3)}, th.success, std::max(2.0f, ui.s(3)));
                    r.fillCircle({ic.center().x, ic.center().y - ui.s(2)}, std::max(1.5f, ui.s(2.2f)), th.success);
                    break;
                case 1:
                    r.fillRoundedRect({ic.center().x - icon * 0.2f, ic.y + icon * 0.05f, icon * 0.4f, icon * 0.9f}, icon * 0.12f, th.accent);
                    break;
                case 2:
                    r.drawRoundedRect(ic.expanded(-icon * 0.08f), ui.s(3), th.danger, std::max(1.5f, ui.s(2)));
                    break;
                default:
                    r.fillRoundedRect({ic.center().x - icon * 0.2f, ic.y + icon * 0.05f, icon * 0.4f, icon * 0.9f}, icon * 0.12f, Color{150, 160, 180, 235});
                    break;
            }
            ui.label(it.text, {ic.right() + igap, cy}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            cx = ic.right() + igap + tw + sep;
        }
    }

    /// The keys of the map (following the device in use) and the mouse, on one line.
    void drawKeys(UIContext& ui, float x, float cy, float w, float h, float size) {
        const UITheme& th = ui.theme();
        struct Hint {
            std::vector<Action> keys;
            const char* text;
        };
        const Hint hints[] = {{{Action::MenuLeft, Action::MenuRight}, "le long du sol"},
                              {{Action::MenuUp, Action::MenuDown}, "d'une plateforme à l'autre"},
                              {{Action::MenuConfirm}, "valider"},
                              {{}, "Souris : cliquez sur un lieu sûr"}};
        const float kgap = h * 0.18f, tgap = h * 0.3f, sep = ui.s(28);
        auto keysW = [&](const Hint& hnt) {
            float kw = 0.0f;
            for (size_t i = 0; i < hnt.keys.size(); ++i) kw += Glyphs::measureAction(ui, hnt.keys[i], h) + (i > 0 ? kgap : 0.0f);
            return kw;
        };
        float f = size, total = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {
            total = 0.0f;
            for (const Hint& hnt : hints) total += keysW(hnt) + (hnt.keys.empty() ? 0.0f : tgap) + ui.text().measure(hnt.text, FontFace::Body, ui.s(f)).x + sep;
            total -= sep;
            if (total <= w || pass == 1) break;
            f = std::max(11.5f / ui.s(1.0f), f * std::max(0.7f, w / total));
        }
        float cx = x;
        for (const Hint& hnt : hints) {
            const float kw = keysW(hnt), tw = ui.text().measure(hnt.text, FontFace::Body, ui.s(f)).x;
            const float need = kw + (hnt.keys.empty() ? 0.0f : tgap) + tw;
            if (cx + need > x + w + 0.5f) break;  // never past the panel (the least useful go first)
            float kx = cx;
            for (size_t i = 0; i < hnt.keys.size(); ++i) kx += Glyphs::drawAction(ui, hnt.keys[i], {kx, cy}, h) + kgap;
            if (!hnt.keys.empty()) kx += tgap - kgap;
            ui.label(hnt.text, {kx, cy}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            cx += need + sep;
        }
    }

    GameServices& g_;
    Request req_;
    bool started_ = false, shown_ = false;
    int chosen_ = -1;
    float t_ = 0.0f;
    std::string msg_;
    float msgTimer_ = 0.0f;
};

// =================================================================== settings

class BossTrainingScene : public Scene {
public:
    BossTrainingScene(GameServices& g, std::string bossId, int phase) : g_(g), bossId_(std::move(bossId)), wantPhase_(phase) {}

    void onEnter() override {
        bg_.init(g_);
        def_ = g_.content.enemy(bossId_);
        prefs_.load();
        atlas_ = officialCreatureAtlas(g_.content);
        for (const PowerDefinition* p : g_.content.powers())
            if (g_.content.isNativePower(p->id)) powers_.push_back(p);
        // In the alphabetical order of their names as shown (accents aside).
        std::sort(powers_.begin(), powers_.end(), [](const PowerDefinition* a, const PowerDefinition* b) { return sortKey(a->name) < sortKey(b->name); });
        if (def_) clips_ = creatureClips(g_, *def_);
        // Automated screenshots: « train[-go][;spawn=<usual | suggested | x,y>[:pick]] »
        // (--boss-training-go, --boss-training-spawn).
        if (g_.onlineOpen.rfind("train", 0) == 0) {
            autoGo_ = g_.onlineOpen.rfind("train-go", 0) == 0;
            if (auto at = g_.onlineOpen.find(";spawn="); at != std::string::npos) spawnSpec_ = g_.onlineOpen.substr(at + 7);
            if (spawnSpec_.size() >= 4 && spawnSpec_.compare(spawnSpec_.size() - 4, 4, "pick") == 0) {
                autoPick_ = true;
                spawnSpec_.erase(spawnSpec_.size() - 4);
                if (!spawnSpec_.empty() && spawnSpec_.back() == ':') spawnSpec_.pop_back();
            }
            g_.onlineOpen.clear();
        }
        g_.ui.setFocus("btrain/go");
    }

    void update(float dt) override {
        t_ += dt;
        clipT_ += dt;
        bg_.update(dt);
        if (!atlas_) atlas_ = officialCreatureAtlas(g_.content);
        if (atlas_ && !placeRead_) readPlace();
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
        // « Lieu d'apparition »: the arena of the phase chosen (computed again when the phase
        // changes, a few milliseconds), the place chosen checked against it.
        if (place_ && level_ && lockReason().empty()) {
            if (!spawnArena_) spawnArena_ = std::make_unique<SpawnArena>(g_);
            if (!spawnArena_->computed(phase_)) spawnArena_->arena(*level_, setup());
            if (spawnCheck_ != SpawnCheck::None) checkSpawn();
            spawnArena_->bake();
            if (autoPick_) {
                autoPick_ = false;
                openPicker();
                return;
            }
        }
        if (autoGo_ && placeRead_ && lockReason().empty()) {
            autoGo_ = false;
            launch();
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.7f);
        ui.pushId("btrain");
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(48);
        UIContext::ButtonStyle small;
        small.small = true;
        Rect back{W - m - ui.s(200), ui.s(30), ui.s(200), ui.s(46)};
        bool leave = ui.button("back", back, "Retour", small);
        float tf = ui.fitFont("ENTRAÎNEMENT", FontFace::Title, 40, back.x - m - ui.s(24), 0.6f);
        ui.label("ENTRAÎNEMENT", {m, ui.s(52)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        std::string who = !def_ ? bossId_ : def_->bossTitle.empty() ? def_->name : def_->bossTitle;
        ui.label(ui.elide(who, FontFace::Bold, fz(ui, 20), W - 2 * m), {m, ui.s(100)}, fz(ui, 20), ui.theme().accent, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        drawNotice(ui, {m, ui.s(146)}, W - 2 * m);
        float top = ui.s(184), bottom = H - ui.s(108);
        Rect left{m, top, std::min(ui.s(560), (W - 2 * m) * 0.36f), bottom - top};
        Rect right{left.right() + ui.s(24), top, W - m - left.right() - ui.s(24), bottom - top};
        drawBoss(left);
        drawSettings(right);
        // Bottom bar: why it cannot start (if so), Retour, Entrer dans l'arène.
        float by = H - ui.s(84);
        UIContext::ButtonStyle go;
        go.primary = true;
        std::string reason = lockReason();
        go.enabled = reason.empty();
        Rect goR{W - m - ui.s(400), by, ui.s(400), ui.s(60)};
        bool start = ui.button("go", goR, "Entrer dans l'arène", go);
        float note = goR.x - m - ui.s(24);
        if (!reason.empty() && note > ui.s(120)) {
            float f = fit(ui, reason, FontFace::Body, 17, note, 0.75f);
            ui.label(ui.elide(reason, FontFace::Body, f, note), {m, goR.center().y}, f, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        } else if (msgTimer_ > 0.0f && note > ui.s(120)) {
            float f = fit(ui, msg_, FontFace::Body, 17, note, 0.75f);
            ui.label(ui.elide(msg_, FontFace::Body, f, note), {m, goR.center().y}, f, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.popId();
        if (start && reason.empty()) launch();
        else if (start) g_.uiSound("ui_error");
        if (leave || ui.backPressed()) g_.app.switchScene(makeBossStats(g_, bossId_), 0.3f);
        ui.end();
    }

private:
    /// Why the training cannot start ("" = it can).
    std::string lockReason() const {
        if (!def_ || !def_->isBoss) return "Ce boss n'existe plus.";
        if (!atlas_) return "Recherche de son arène dans les niveaux…";
        if (!place_) return "Ce boss n'est placé dans aucun niveau du jeu : pas d'arène où s'entraîner.";
        const BossRecord* rec = g_.bossStats.find(bossId_);
        if (!g_.collection.met.count(bossId_) && !(rec && rec->fights > 0)) return "Rencontrez-le d'abord dans son niveau : l'entraînement s'ouvre ensuite.";
        if (!level_) return "Son niveau ne peut pas être lu.";
        return {};
    }

    /// Its arena (from the levels), and the choices: the last ones for this boss, else the
    /// player's own hearts, the level's slots and the powers found on the way.
    void readPlace() {
        placeRead_ = true;
        place_ = bossTrainingPlace(*atlas_, g_.content, bossId_);
        if (!place_) return;
        level_ = g_.content.loadLevel(place_->levelId);
        int maxHearts = PlayerConfig::fromJson(g_.content.playerConfig()).maxHealth;
        hearts_ = std::clamp(maxHearts, kTrainingMinHearts, kTrainingMaxHearts);
        slots_ = std::clamp(level_ && level_->powerSlots() > 0 ? level_->powerSlots() : 2, 1, LevelData::kMaxPowerSlots);
        chosen_ = place_->powersBefore;
        if (static_cast<int>(chosen_.size()) > slots_) chosen_.erase(chosen_.begin(), chosen_.end() - slots_);
        if (const BossTrainingChoice* c = prefs_.find(bossId_)) {
            if (c->hearts > 0) hearts_ = std::clamp(c->hearts, kTrainingMinHearts, kTrainingMaxHearts);
            if (c->slots > 0) slots_ = std::clamp(c->slots, 1, LevelData::kMaxPowerSlots);
            chosen_ = c->powers;
            phase_ = c->phase;
            spawn_ = c->spawn;  // checked against the arena as it is now (checkSpawn)
        }
        chosen_ = fitTrainingPowers(g_.content, chosen_, slots_);
        if (wantPhase_ >= -1) phase_ = wantPhase_;  // 2.54: opened on a phase (« Passages difficiles »)
        int phases = def_ ? static_cast<int>(def_->phases.size()) : 0;
        if (phase_ >= phases) phase_ = -1;
        // Automated screenshots (--boss-training-spawn): the usual start, the place proposed
        // first, or a place given as « x,y » (checked as a saved one).
        if (spawnSpec_ == "usual") {
            spawn_.reset();
        } else if (spawnSpec_ == "suggested") {
            suggestSpawn_ = true;
        } else if (auto comma = spawnSpec_.find(','); comma != std::string::npos) {
            spawn_ = Vec2{static_cast<float>(std::atof(spawnSpec_.substr(0, comma).c_str())), static_cast<float>(std::atof(spawnSpec_.substr(comma + 1).c_str()))};
        }
        spawnCheck_ = spawn_ || suggestSpawn_ ? SpawnCheck::Saved : SpawnCheck::None;
    }

    /// The training as set up now (what « Entrer dans l'arène » starts).
    BossTrainingSetup setup() const {
        BossTrainingSetup s;
        s.bossId = bossId_;
        s.levelId = place_->levelId;
        s.bossObject = place_->objectId;
        // The checkpoint just before its arena, unless other bosses come first (then in front
        // of its arena).
        if (place_->bossesBetween == 0) {
            s.checkpointId = place_->checkpointId;
            s.spawn = place_->checkpointFeet;
        }
        s.hearts = hearts_;
        s.slots = slots_;
        s.powers = chosen_;
        s.phase = phase_;
        s.arenaSpawn = spawn_;
        return s;
    }

    /// The place chosen, checked against the arena of the phase chosen: a saved place gone from
    /// it (the level changed) gives the usual start back; after a change of phase, the nearest
    /// safe place of the arena as that phase leaves it (none: the usual start).
    void checkSpawn() {
        const SpawnCheck why = spawnCheck_;
        spawnCheck_ = SpawnCheck::None;
        const TrainingArena* a = spawnArena_ ? spawnArena_->arena(*level_, setup()) : nullptr;
        if (suggestSpawn_) {
            suggestSpawn_ = false;
            if (a && a->suggested() >= 0) spawn_ = a->spots[static_cast<size_t>(a->suggested())].feet;
        }
        if (!spawn_ || (a && a->find(*spawn_) >= 0)) return;
        if (why == SpawnCheck::Phase && a) {
            const int n = a->nearest(*spawn_ - Vec2{0.0f, a->body.y * 0.5f});
            if (n >= 0) {
                spawn_ = a->spots[static_cast<size_t>(n)].feet;
                say("Lieu d'apparition déplacé : l'arène n'est plus la même à cette phase.");
                return;
            }
            spawn_.reset();
            say("Aucun lieu sûr dans l'arène à cette phase : départ au point habituel.");
            return;
        }
        spawn_.reset();
        say("Le lieu d'apparition choisi n'existe plus dans l'arène : retour au point habituel.");
    }

    /// The start of the run in words: the usual one (the checkpoint before the arena, or in
    /// front of it), or the place picked in the arena.
    std::string usualStartText() const {
        bool front = place_->checkpointId.empty() || place_->bossesBetween > 0;
        return !front && !place_->checkpointLabel.empty() ? "Départ : point de contrôle « " + place_->checkpointLabel + " »"
               : !front                                   ? std::string("Départ : le point de contrôle avant son arène")
                                                          : std::string("Départ : devant son arène");
    }
    std::string startText() const {
        const TrainingArena* a = spawnArena_ && spawnArena_->computed(phase_) ? spawnArena_->last() : nullptr;
        const int k = a && spawn_ ? a->find(*spawn_) : -1;
        return k >= 0 ? spotText(*a, a->spots[static_cast<size_t>(k)]) : usualStartText();
    }

    /// The phase trained, in words (« toutes les phases », « Phase 2 : Colère des ronces »).
    std::string phaseText() const {
        if (!def_ || phase_ < 0 || phase_ >= static_cast<int>(def_->phases.size())) return "Toutes les phases";
        const std::string& n = def_->phases[static_cast<size_t>(phase_)].name;
        return "Phase " + std::to_string(phase_ + 1) + " : " + (n.empty() ? std::string("sans nom") : n);
    }

    /// The map of the arena, big, to pick the place (SpawnPickerScene).
    void openPicker() {
        if (!place_ || !level_ || !lockReason().empty()) {
            g_.uiSound("ui_error");
            return;
        }
        if (!spawnArena_) spawnArena_ = std::make_unique<SpawnArena>(g_);
        SpawnPickerScene::Request req;
        req.arena = spawnArena_.get();
        req.level = &*level_;
        req.setup = setup();
        req.def = def_;
        const BossRecord* rec = g_.bossStats.find(bossId_);
        req.known = rec && rec->kills > 0;
        req.who = !def_ ? bossId_ : def_->bossTitle.empty() ? def_->name : def_->bossTitle;
        req.phase = phaseText();
        req.usualText = usualStartText();
        req.returnFocus = "btrain/pick";
        req.done = [this](std::optional<Vec2> v) { spawn_ = v; };
        g_.app.pushScene(std::make_unique<SpawnPickerScene>(g_, std::move(req)));
    }

    void drawBoss(const Rect& area) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        ui.panel(area, false, 16);
        if (!def_) return;
        float x = area.x + ui.s(20), w = area.w - ui.s(40), y = area.y + ui.s(20);
        Rect box{x, y, w, std::min(ui.s(300), area.h * 0.42f)};
        r.fillRoundedRect(box, ui.s(12), Color{11, 18, 38, 255});
        // As in the statistics and the gallery: its colours and its clips once beaten.
        const BossRecord* rec = g_.bossStats.find(bossId_);
        bool known = rec && rec->kills > 0;
        std::string clip = "idle";
        if (known && !clips_.empty()) {
            clip_ %= static_cast<int>(clips_.size());
            float loop = std::max(0.05f, creatureClipDuration(g_, *def_, clips_[static_cast<size_t>(clip_)]));
            if (clipT_ >= loop * std::max(1.0f, std::ceil(2.4f / loop))) {
                clip_ = (clip_ + 1) % static_cast<int>(clips_.size());
                clipT_ = 0.0f;
            }
            clip = clips_[static_cast<size_t>(clip_)];
        }
        r.pushClip(box);
        drawCreature(g_, *def_, box.expanded(-ui.s(16)), known ? clipT_ : t_, known ? Color::white() : Color{0, 0, 0, 110}, clip);
        r.popClip();
        if (!known) ui.label("?", box.center(), 48, Color{90, 106, 130, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        y = box.bottom() + ui.s(22);
        auto line = [&](const std::string& text, float size, Color c, FontFace face) {
            Vec2 sz = ui.labelWrapped(text, {x, y}, w, fz(ui, size), c);
            y += sz.y + ui.s(8);
            (void)face;
        };
        if (!atlas_) {
            line("Recherche de son arène dans les niveaux…", 16, ui.theme().textDim, FontFace::Body);
            return;
        }
        if (!place_) return;
        const PlanetData* planet = g_.content.planet(place_->planetId);
        ui.label("Arène", {x, y}, fz(ui, 18), ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        float nf = fit(ui, place_->levelName, FontFace::Bold, 20, w, 0.75f);
        ui.label(ui.elide(place_->levelName, FontFace::Bold, nf, w), {x, y}, nf, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(32);
        std::string where = planet ? planet->name : std::string("Hors campagne");
        if (place_->levelNumber > 0) where += " · " + std::string("niveau ") + std::to_string(place_->levelNumber);
        line(where, 16, ui.theme().textDim, FontFace::Body);
        line(startText(), 16, spawn_ ? ui.theme().accent : Color{185, 200, 218, 255}, FontFace::Body);
        y += ui.s(6);
        // Its phases, with the life it has when each begins.
        if (def_->phases.size() > 1 && y + ui.s(30) < area.bottom()) {
            ui.label("Phases", {x, y}, fz(ui, 18), ui.theme().accent, Align::Left, FontFace::Bold);
            y += ui.s(30);
            for (size_t i = 0; i < def_->phases.size(); ++i) {
                if (y + ui.s(24) > area.bottom() - ui.s(10)) break;
                const BossPhase& ph = def_->phases[i];
                // 2.55: after a rebirth (the Phénix's « Cendres »), the health it is reborn with.
                const float reborn = i > 0 ? def_->phases[i - 1].rebirth : 0.0f;
                int pct = static_cast<int>(std::lround((reborn > 0.0f ? reborn : ph.hpThreshold) * 100.0f));
                std::string text = std::to_string(i + 1) + ". " + (ph.name.empty() ? "Phase " + std::to_string(i + 1) : ph.name) + " · " +
                                   std::to_string(pct) + " % de vie" + (reborn > 0.0f ? " (renaissance)" : "");
                float f = fit(ui, text, FontFace::Body, 16, w, 0.75f);
                ui.label(ui.elide(text, FontFace::Body, f, w), {x, y}, f, phase_ == static_cast<int>(i) ? ui.theme().text : ui.theme().textDim);
                y += ui.s(28);
            }
            y += ui.s(6);
        }
        // « Lieu d'apparition » (2.41): the arena small, with the place chosen (or the usual
        // start); a click opens its big map. Only where the panel has room left.
        const float capH = ui.s(30), room = area.bottom() - ui.s(18) - y - capH;
        if (spawnArena_ && spawnArena_->computed(phase_) && room >= ui.s(84)) {
            ui.label("Lieu d'apparition", {x, y}, fz(ui, 18), ui.theme().accent, Align::Left, FontFace::Bold);
            y += capH;
            const Rect mm{x, y, w, std::min(room, w * 0.5f)};
            const TrainingArena* a = spawnArena_->last();
            const int k = a && spawn_ ? a->find(*spawn_) : -1;
            bool over = false;
            if (ui.mouseClick("minimap", mm, &over)) openPicker();
            spawnArena_->draw(ui, mm, k, -1, false, def_, known, t_);
            ui.r().drawRoundedRect(mm, ui.s(8), over ? ui.theme().accent : ui.theme().border, std::max(1.0f, ui.s(over ? 2.0f : 1.5f)));
        }
    }

    void drawSettings(const Rect& area) {
        UIContext& ui = g_.ui;
        ui.panel(area, false, 16);
        float x = area.x + ui.s(24), w = area.w - ui.s(48), y = area.y + ui.s(22);
        if (!place_) {
            ui.labelWrapped(atlas_ ? "Pas d'arène : rien à régler." : "Recherche de son arène dans les niveaux…", {x, y}, w, fz(ui, 17),
                            ui.theme().textDim);
            return;
        }
        float gap = ui.s(20), rowH = ui.s(56);
        float half = (w - gap) * 0.5f;
        int hearts = hearts_, slots = slots_;
        ui.sliderInt("hearts", {x, y, half, rowH}, "Cœurs", hearts, kTrainingMinHearts, kTrainingMaxHearts);
        ui.sliderInt("slots", {x + half + gap, y, half, rowH}, "Emplacements de pouvoirs", slots, 1, LevelData::kMaxPowerSlots);
        hearts_ = hearts;
        if (slots != slots_) {
            slots_ = slots;
            chosen_ = fitTrainingPowers(g_.content, chosen_, slots_);
        }
        y += rowH + ui.s(14);
        std::vector<std::string> phases = {"Toutes les phases"};
        if (def_)
            for (size_t i = 0; i < def_->phases.size(); ++i)
                phases.push_back("Phase " + std::to_string(i + 1) + " : " + (def_->phases[i].name.empty() ? std::string("sans nom") : def_->phases[i].name));
        int idx = std::clamp(phase_ + 1, 0, static_cast<int>(phases.size()) - 1);
        if (ui.cycle("phase", {x, y, w, rowH}, "Phase", idx, phases)) {
            phase_ = idx - 1;
            if (spawn_) spawnCheck_ = SpawnCheck::Phase;  // the arena may differ at that phase
        }
        y += rowH + ui.s(14);
        // « Lieu d'apparition » (2.41): the usual start or a place of the arena (the one picked
        // last, else the one proposed first), and its map to pick one.
        const float pickW = std::clamp(w * 0.36f, ui.s(220), ui.s(380));
        const Rect spawnRow{x, y, w - pickW - gap, rowH}, pickRow{spawnRow.right() + gap, y, pickW, rowH};
        int where = spawn_ ? 1 : 0;
        if (ui.cycle("spawn", spawnRow, "Lieu d'apparition", where, {"Point habituel", "Dans l'arène"})) setInArena(where == 1);
        ui.newBadge("spawn", spawnRow, "stats.training.spawn");  // « NOUVEAU » (2.41)
        UIContext::ButtonStyle pick;
        pick.small = true;
        pick.enabled = lockReason().empty();
        if (ui.button("pick", pickRow, "Choisir sur la carte", pick)) openPicker();
        ui.newBadge("pick", pickRow, "stats.training.spawn", false);
        y += rowH + ui.s(22);
        // The powers: a grid of every power of the game, the chosen ones numbered by slot.
        std::string head = "Pouvoirs";
        Vec2 hs = ui.label(head, {x, y + ui.s(12)}, fz(ui, 20), ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string count = std::to_string(chosen_.size()) + " / " + std::to_string(slots_);
        Vec2 cs = ui.label(count, {x + hs.x + ui.s(14), y + ui.s(12)}, fz(ui, 20), chosen_.size() >= static_cast<size_t>(slots_) ? ui.theme().warm : ui.theme().text,
                           Align::Left, FontFace::Bold, VAlign::Middle);
        std::string hint = "Touches de son niveau (à changer au choix du niveau ou en pause)";
        float hx = x + hs.x + cs.x + ui.s(40), hroom = x + w - hx;
        if (hroom > ui.s(80)) {
            float f = fit(ui, hint, FontFace::Body, 15, hroom, 0.75f);
            ui.label(ui.elide(hint, FontFace::Body, f, hroom), {x + w, y + ui.s(12)}, f, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        }
        y += ui.s(34);
        const int cols = 3;
        float cgap = ui.s(10);
        float cw = (w - cgap * (cols - 1)) / cols;
        int rows = static_cast<int>((powers_.size() + cols - 1) / cols);
        float ch = std::clamp((area.bottom() - ui.s(18) - y - cgap * (rows - 1)) / std::max(1, rows), ui.s(40), ui.s(60));
        refreshKeys();
        for (size_t i = 0; i < powers_.size(); ++i) {
            const PowerDefinition& p = *powers_[i];
            Rect c{x + (cw + cgap) * static_cast<float>(i % cols), y + (ch + cgap) * static_cast<float>(i / cols), cw, ch};
            auto it = std::find(chosen_.begin(), chosen_.end(), p.id);
            bool on = it != chosen_.end();
            UIContext::ButtonStyle st;
            st.selected = on;
            st.small = true;
            if (ui.button("pw" + std::to_string(i), c, "", st)) toggle(p.id);
            Rect icon{c.x + ui.s(10), c.center().y - (ch - ui.s(16)) * 0.5f, ch - ui.s(16), ch - ui.s(16)};
            ui.icon("ui/icons_powers.png", p.icon, 32, icon, on ? Color::white() : Color{150, 160, 180, 255});
            // Its key in this level (2.31: the player's, the level's or the suggestion, keyboard
            // or gamepad as in use); a power without a key of its own: the dashed gold frame.
            const bool keyed = i < keyed_.size() && i < keys_.size() && keyed_[i];
            if (!keyed) Glyphs::drawFreePowerFrame(ui, icon.expanded(ui.s(2)), ui.s(6), std::max(1.5f, ui.s(2)), on ? 1.0f : 0.6f);
            float tx = icon.right() + ui.s(10);
            float badge = on ? ui.s(30) : 0.0f;
            float keyH = std::min(ui.s(28), ch - ui.s(18)), keyW = 0.0f;
            if (keyed) {
                keyW = Glyphs::drawPowerKey(ui, keys_[i], {0.0f, 0.0f}, keyH, false);
                const float keyRoom = (c.right() - ui.s(12) - badge - tx) * 0.45f;
                if (keyW > keyRoom) {
                    keyH *= keyRoom / keyW;
                    keyW = Glyphs::drawPowerKey(ui, keys_[i], {0.0f, 0.0f}, keyH, false);
                }
                Glyphs::drawPowerKey(ui, keys_[i], {c.right() - ui.s(12) - badge - (on ? ui.s(8) : 0.0f) - keyW, c.center().y}, keyH);
                keyW += ui.s(10) + (on ? ui.s(8) : 0.0f);
            }
            float room = c.right() - ui.s(12) - badge - keyW - tx;
            float f = fit(ui, p.name, FontFace::Bold, 17, room, 0.7f);
            ui.label(ui.elide(p.name, FontFace::Bold, f, room), {tx, c.center().y}, f, on ? ui.theme().text : ui.theme().textDim, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            if (on) {
                // Its slot.
                Vec2 bc{c.right() - ui.s(24), c.center().y};
                ui.r().fillCircle(bc, ui.s(12), ui.theme().accent);
                ui.label(std::to_string(it - chosen_.begin() + 1), bc, fz(ui, 15), Color{8, 12, 26, 255}, Align::Center, FontFace::Bold,
                         VAlign::Middle);
            }
        }
    }

    void toggle(const std::string& id) {
        auto it = std::find(chosen_.begin(), chosen_.end(), id);
        if (it != chosen_.end()) {
            chosen_.erase(it);
            return;
        }
        if (static_cast<int>(chosen_.size()) >= slots_) {
            g_.uiSound("ui_error");
            say("Tous les emplacements sont pris : retirez d'abord un pouvoir, ou ajoutez un emplacement.");
            return;
        }
        std::vector<std::string> wanted = chosen_;
        wanted.push_back(id);
        chosen_ = fitTrainingPowers(g_.content, wanted, slots_);
    }

    void say(const std::string& m) {
        msg_ = m;
        msgTimer_ = 4.0f;
    }

    /// The key of each power of the grid in the arena's level, for this player, with the powers
    /// chosen (as the training will have them); one not chosen: as it would be once added.
    void refreshKeys() {
        if (keysFor_ == chosen_ && keys_.size() == powers_.size()) return;
        keysFor_ = chosen_;
        keys_.clear();
        keyed_.clear();
        const PowerKeyMap chosenKeys = level_ ? playerPowerKeys(g_, *level_, chosen_) : PowerKeyMap{};
        for (const PowerDefinition* p : powers_) {
            const bool keyed = powerUsesKey(*p);
            keyed_.push_back(keyed);
            const ResolvedPowerKey* k = keyed ? chosenKeys.find(p->id) : nullptr;
            keys_.push_back(k ? *k : keyed && level_ ? playerPowerKey(g_, *level_, p->id, chosen_) : ResolvedPowerKey{});
        }
    }

    /// « Lieu d'apparition »: in the arena (the place picked last, else the one proposed first)
    /// or the usual start.
    void setInArena(bool on) {
        if (!on) {
            if (spawn_) lastArenaSpawn_ = spawn_;
            spawn_.reset();
            return;
        }
        const TrainingArena* a = spawnArena_ && level_ && lockReason().empty() ? spawnArena_->arena(*level_, setup()) : nullptr;
        if (!a || a->spots.empty()) {
            g_.uiSound("ui_error");
            say(a ? "Aucun lieu sûr dans l'arène à cette phase : seul le point habituel est possible."
                  : "L'arène n'est pas encore prête : réessayez dans un instant.");
            return;
        }
        int k = lastArenaSpawn_ ? a->find(*lastArenaSpawn_) : -1;
        if (k < 0) k = a->suggested();
        spawn_ = a->spots[static_cast<size_t>(k)].feet;
    }

    void launch() {
        if (!place_) return;
        // The place picked: checked against the arena of the phase chosen before the run.
        if (spawnCheck_ != SpawnCheck::None && spawnArena_ && level_) checkSpawn();
        if (spawn_ && (!spawnArena_ || !spawnArena_->computed(phase_) || !spawnArena_->last() || spawnArena_->last()->find(*spawn_) < 0)) spawn_.reset();
        BossTrainingChoice c;
        c.hearts = hearts_;
        c.slots = slots_;
        c.powers = chosen_;
        c.phase = phase_;
        c.spawn = spawn_;
        prefs_.set(bossId_, c);
        prefs_.save();
        PlayOptions o;
        o.bossTraining = setup();
        g_.app.switchScene(makeGameScene(g_, place_->levelId, std::move(o)), 0.4f);
    }

    GameServices& g_;
    std::string bossId_;
    const EnemyDefinition* def_ = nullptr;
    MenuBackground bg_;
    BossTrainingPrefs prefs_;
    std::shared_ptr<const CreatureAtlas> atlas_;
    bool placeRead_ = false;
    std::optional<CreaturePlace> place_;
    std::optional<LevelData> level_;
    std::vector<const PowerDefinition*> powers_;
    std::vector<std::string> clips_;
    int clip_ = 0;
    float clipT_ = 0.0f, t_ = 0.0f;
    int hearts_ = 10, slots_ = 2, phase_ = -1;
    int wantPhase_ = -2;
    std::vector<std::string> chosen_;
    std::vector<std::string> keysFor_;     // the choice keys_ was computed for
    std::vector<ResolvedPowerKey> keys_;   // one per power of the grid (2.31)
    std::vector<bool> keyed_;              // whether it uses a key of its own
    std::string msg_;
    float msgTimer_ = 0.0f;
    bool autoGo_ = false;
    // « Lieu d'apparition » (2.41).
    enum class SpawnCheck { None, Saved, Phase };  // why the place must be checked again
    std::unique_ptr<SpawnArena> spawnArena_;        // the arena of the phase chosen, its map
    std::optional<Vec2> spawn_;                     // the place picked (nullopt: the usual start)
    std::optional<Vec2> lastArenaSpawn_;            // the one before « Point habituel » was chosen
    SpawnCheck spawnCheck_ = SpawnCheck::None;
    std::string spawnSpec_;                         // automated screenshots (--boss-training-spawn)
    bool suggestSpawn_ = false, autoPick_ = false;
};

// =================================================================== end of a training

class BossTrainingEndScene : public Scene {
public:
    BossTrainingEndScene(GameServices& g, GameScene& game, BossTrainingResult r) : g_(g), game_(game), r_(std::move(r)) {}
    bool isOverlay() const override { return true; }
    bool updatesSceneBelow() const override { return true; }

    void onEnter() override { g_.ui.setFocus("btend/again"); }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{4, 6, 14, 170});
        ui.pushId("btend");
        // Title, subtitle, three rows, the notice, the buttons: the panel is as high as that.
        float pw = std::min(ui.s(860), W - ui.s(80)), ph = std::min(ui.s(30 + 66 + 40 + 3 * 60 + 6 + 36 + 34 + 60 + 30), H - ui.s(80));
        Rect p{(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
        ui.panel(p, true, 18);
        float x = p.x + ui.s(36), w = p.w - ui.s(72), y = p.y + ui.s(30);
        bool single = r_.setup.phase >= 0;
        std::string title = !r_.won ? "DÉFAITE" : single ? "PHASE RÉUSSIE" : "BOSS VAINCU";
        float tf = ui.fitFont(title, FontFace::Title, 44, w, 0.6f);
        ui.label(title, {p.center().x, y + ui.s(26)}, tf, r_.won ? ui.theme().success : ui.theme().danger, Align::Center, FontFace::Title,
                 VAlign::Middle);
        y += ui.s(66);
        std::string sub = r_.bossName + "  ·  " +
                          (single ? "Phase " + std::to_string(r_.setup.phase + 1) + (r_.phaseName.empty() || r_.phase != r_.setup.phase ? std::string{} : " : " + r_.phaseName)
                                  : std::string("toutes les phases"));
        float sf = fit(ui, sub, FontFace::Bold, 20, w, 0.7f);
        ui.label(ui.elide(sub, FontFace::Bold, sf, w), {p.center().x, y}, sf, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        y += ui.s(40);
        // The training's result.
        auto row = [&](const std::string& k, const std::string& v, Color c) {
            Rect rr{x, y, w, ui.s(52)};
            ui.r().fillRoundedRect(rr, ui.s(8), Color{12, 18, 38, 220});
            float vf = fit(ui, v, FontFace::Bold, 22, w * 0.45f, 0.7f);
            float vw = ui.text().measure(v, FontFace::Bold, ui.s(vf)).x;
            float kroom = w - ui.s(36) - vw - ui.s(16);
            float kf = fit(ui, k, FontFace::Body, 18, kroom, 0.75f);
            ui.label(ui.elide(k, FontFace::Body, kf, kroom), {rr.x + ui.s(18), rr.center().y}, kf, ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
            ui.label(v, {rr.right() - ui.s(18), rr.center().y}, vf, c, Align::Right, FontFace::Bold, VAlign::Middle);
            y += ui.s(60);
        };
        row("Temps du combat", r_.time > 0.0f ? fightTime(r_.time) : std::string("—"), ui.theme().accent);
        row("Dégâts reçus", r_.damage == 0 ? std::string("aucun") : std::to_string(r_.damage) + (r_.damage > 1 ? " cœurs" : " cœur"),
            r_.damage == 0 ? ui.theme().success : ui.theme().warm);
        std::string reached = std::to_string(r_.phase + 1) + " / " + std::to_string(std::max(1, r_.phases));
        if (!r_.phaseName.empty()) reached += "  ·  " + r_.phaseName;
        row("Phase atteinte", reached, ui.theme().text);
        y += ui.s(6);
        drawNotice(ui, {x, y + ui.s(18)}, w);
        // Recommencer, Changer les réglages, Retour.
        float by = p.bottom() - ui.s(30) - ui.s(60), gap = ui.s(16);
        float bw = (w - 2 * gap) / 3.0f;
        UIContext::ButtonStyle primary;
        primary.primary = true;
        bool again = ui.button("again", {x, by, bw, ui.s(60)}, "Recommencer", primary);
        bool settings = ui.button("settings", {x + bw + gap, by, bw, ui.s(60)}, "Changer les réglages");
        bool back = ui.button("back", {x + 2 * (bw + gap), by, bw, ui.s(60)}, "Retour");
        ui.popId();
        InputManager& in = g_.app.input();
        if (again || in.keyPressed(SDL_SCANCODE_R) || in.rawPadButton(SDL_GAMEPAD_BUTTON_BACK)) {
            g_.app.popScene();
            game_.restart();
        } else if (settings) {
            g_.app.switchScene(makeBossTraining(g_, r_.setup.bossId), 0.3f);
        } else if (back || ui.backPressed()) {
            g_.app.switchScene(makeBossStats(g_, r_.setup.bossId), 0.3f);
        }
        ui.end();
    }

private:
    GameServices& g_;
    GameScene& game_;
    BossTrainingResult r_;
};

}  // namespace

std::unique_ptr<Scene> makeBossTraining(GameServices& g, const std::string& bossId, int phase) {
    return std::make_unique<BossTrainingScene>(g, bossId, phase);
}

std::unique_ptr<Scene> makeBossTrainingEnd(GameServices& g, GameScene& game, const BossTrainingResult& result) {
    return std::make_unique<BossTrainingEndScene>(g, game, result);
}

}  // namespace cosmic
