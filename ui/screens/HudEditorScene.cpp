// HUD editor (Paramètres › Jeu › Personnaliser le HUD): the player places and sizes each
// element of the in-game HUD on a live preview (a real level drawn by the game, with the
// real HUD). Elements can never overlap each other nor leave the screen.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/GameServices.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/LevelProgress.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

constexpr const char* kPreviewLevel = "sylvanox_01";

class HudEditorScene : public Scene {
public:
    HudEditorScene(GameServices& g, GameplaySettings& target) : g_(g), target_(target), work_(target) {}

    void onEnter() override {
        buildWorld();
        g_.ui.setFocus("hud/el0");
        // Automated screenshots (--hud-editor --online-open hud-demo): the editor's own
        // operations, as the mouse and the sliders use them.
        demo_ = g_.onlineOpen == "hud-demo";
        if (demo_) g_.onlineOpen.clear();
    }

    void fixedUpdate(float dt) override {
        if (!world_) return;
        world_->fixedUpdate(PlayerInput{}, dt);
        if (!bossShown_)  // enemies join the world on its first step: then a boss fight starts
            for (const auto& e : world_->enemies())
                if (e->def().isBoss && e->alive()) {
                    world_->startBoss(*e);
                    bossShown_ = true;
                    break;
                }
        if (demo_ && ++demoStep_ == 30) runDemo();
    }

    void update(float dt) override {
        bg_.update(dt);
        if (statusTimer_ > 0.0f) statusTimer_ -= dt;
        if (track_.valid() && track_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) hud_.setProgressTrack(track_.get());
        if (world_) {
            world_->update(dt);
            world_->messages().clear();  // no boss titles over the preview
            hud_.update(*world_, dt);
        }
    }

    void render(float alpha) override {
        UIContext& ui = g_.ui;
        Renderer& r = g_.app.renderer();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        float W = ui.screenW(), H = ui.screenH();
        renderPreviewTarget(alpha);
        bg_.render(g_, 0.6f);

        float margin = ui.s(40);
        ui.label("PERSONNALISER LE HUD", {margin, ui.s(26)}, 36, ui.theme().text, Align::Left, FontFace::Title);
        ui.pushId("hud");
        // Top right: cancel / save.
        float bw = ui.s(220), bh = ui.s(52);
        UIContext::ButtonStyle primary;
        primary.primary = true;
        std::string conflict = firstConflict();
        primary.enabled = conflict.empty();  // never save a layout where elements overlap
        bool save = ui.button("save", {W - margin - bw, ui.s(24), bw, bh}, "Enregistrer", primary) && conflict.empty();
        bool cancel = ui.button("cancel", {W - margin - bw * 2 - ui.s(16), ui.s(24), bw, bh}, "Annuler") || ui.backPressed();

        // Preview (16:9, the left part of the screen).
        Rect P{margin, ui.s(104), W * 0.6f, W * 0.6f * H / W};
        previewScale_ = P.w / W;
        preview_ = P;
        r.fillRect(P.expanded(ui.s(3)), {6, 10, 22, 255});
        if (target_tex_) r.drawTexture(*target_tex_, nullptr, P);
        r.drawRoundedRect(P.expanded(ui.s(3)), ui.s(6), ui.theme().border, ui.s(2));
        drawOutlines(P);
        handlePointer(P);

        // Below the preview: status, then how to use it.
        float y = P.bottom() + ui.s(18);
        Vec2 used = ui.labelWrapped("Souris : glissez un élément pour le déplacer, molette pour changer sa taille. Clavier et "
                                    "manette : choisissez un élément dans la liste, puis réglez sa taille et sa position avec "
                                    "les curseurs. Deux éléments ne peuvent jamais se chevaucher.",
                                    {P.x, y}, P.w, 18, ui.theme().textDim);
        y += used.y + ui.s(16);
        if (ui.button("resetall", {P.x, y, ui.s(300), ui.s(50)}, "Tout réinitialiser")) resetAll();
        if (!conflict.empty())
            ui.labelWrapped(conflict, {P.x + ui.s(324), y + ui.s(4)}, P.w - ui.s(324), 18, ui.theme().danger);
        else if (statusTimer_ > 0.0f)
            ui.labelWrapped(status_, {P.x + ui.s(324), y + ui.s(4)}, P.w - ui.s(324), 18, ui.theme().warm);

        // Right panel: elements, then the settings of the selected one.
        drawPanel({P.right() + ui.s(28), P.y, W - margin - (P.right() + ui.s(28)), H - P.y - ui.s(30)});
        ui.popId();
        ui.end();

        if (save) {
            target_ = work_;
            if (&target_ == &g_.settings.gameplay) g_.saveSettings();  // opened on its own (not from Paramètres)
            g_.app.popScene();
        } else if (cancel) {
            g_.app.popScene();
        }
    }

private:
    void runDemo() {
        float W = g_.ui.screenW(), H = g_.ui.screenH();
        selected_ = static_cast<int>(HudElement::Timer);
        Rect t = areaOf(work_, HudElement::Timer);
        moveTo(HudElement::Timer, {g_.ui.s(40), H - t.h - g_.ui.s(150)});  // bottom left
        setScale(HudElement::Vitals, 1.25f);
        Rect c = areaOf(work_, HudElement::Collectibles);
        moveTo(HudElement::Collectibles, {W - c.w - g_.ui.s(40), g_.ui.s(30)});  // top right, where the timer was
        // The progress bar dropped on the timer: refused, and said why.
        selected_ = static_cast<int>(HudElement::Progress);
        moveTo(HudElement::Progress, areaOf(work_, HudElement::Timer).pos());
    }

    // ------------------------------------------------------------ preview world

    void buildWorld() {
        auto level = g_.content.loadLevel(kPreviewLevel);
        if (!level) return;
        level->intro.steps.clear();
        level->bossMusic.clear();
        level->music.clear();
        level->ambience.clear();
        WorldServices s{g_.app.resources(), g_.app.audio(), g_.content, nullptr, 0.0f, 0.6f};
        world_ = std::make_unique<GameWorld>(s, WorldMode::Arena);
        world_->silent = true;
        world_->load(*level);
        // A representative HUD: four powers carried (and a boss fight, see fixedUpdate).
        for (const char* id : {"dash", "wave", "shield", "time_slow"})
            if (const PowerDefinition* d = g_.content.power(id)) world_->player().grantPower(*d);
        renderer_.build(g_.app.renderer(), g_.app.resources(), *world_);
        track_ = std::async(std::launch::async, [l = *level, &content = g_.content] { return buildProgressTrack(l, content); });
    }

    void renderPreviewTarget(float alpha) {
        Renderer& r = g_.app.renderer();
        if (!world_) return;
        if (!target_tex_ || target_tex_->width() != r.width() || target_tex_->height() != r.height())
            target_tex_ = r.createTarget(r.width(), r.height(), true);
        if (!target_tex_) return;
        r.setTarget(target_tex_);
        SDL_SetRenderDrawColor(r.sdl(), 0, 0, 0, 255);
        SDL_RenderClear(r.sdl());
        WorldView view = world_->camera().view(r.width(), r.height(), alpha);
        RenderOptions opt;
        opt.lighting = false;
        renderer_.render(r, g_.app.resources(), *world_, view, alpha, opt);
        r.setView(WorldView{});
        hud_.render(g_.ui, *world_, view, nullptr, work_);
        r.setTarget(nullptr);
    }

    // ------------------------------------------------------------ layout rules

    Rect areaOf(const GameplaySettings& s, HudElement e) {
        if (!world_ || !hudVisible(s, e)) return {};
        return hud_.area(g_.ui, *world_, e, hud_.place(g_.ui, *world_, s, e));
    }

    /// Elements that `e` overlaps in `s`.
    std::vector<int> overlapsOf(const GameplaySettings& s, HudElement e) {
        std::vector<int> out;
        Rect a = areaOf(s, e);
        if (a.w <= 0.0f) return out;
        for (int i = 0; i < kHudElements; ++i) {
            auto o = static_cast<HudElement>(i);
            if (o == e) continue;
            Rect b = areaOf(s, o);
            if (b.w > 0.0f && a.intersects(b)) out.push_back(i);
        }
        return out;
    }

    /// Empty when `e` fits at its place in `s`, else what is in the way. A change never adds an
    /// overlap; one that already existed (layout made for another screen) can be worked out
    /// step by step.
    std::string blocker(const GameplaySettings& s, HudElement e) {
        Rect a = areaOf(s, e);
        if (a.w <= 0.0f) return {};
        float W = g_.ui.screenW(), H = g_.ui.screenH();
        if (a.x < -0.5f || a.y < -0.5f || a.right() > W + 0.5f || a.bottom() > H + 0.5f) return "le bord de l'écran";
        std::vector<int> before = overlapsOf(work_, e);
        for (int i : overlapsOf(s, e))
            if (std::find(before.begin(), before.end(), i) == before.end())
                return std::string("« ") + HudLayout::label(static_cast<HudElement>(i)) + " »";
        return {};
    }

    /// Applies a change if the element still fits, else explains why not.
    bool tryApply(const GameplaySettings& candidate, HudElement e) {
        std::string why = blocker(candidate, e);
        if (!why.empty()) {
            status_ = std::string("Impossible : « ") + HudLayout::label(e) + " » toucherait " + why + ".";
            statusTimer_ = 2.5f;
            return false;
        }
        work_ = candidate;
        return true;
    }

    /// Candidate with the element's area moved to `topLeft` (kept inside the screen).
    GameplaySettings moved(const GameplaySettings& s, HudElement e, Vec2 topLeft) {
        GameplaySettings c = s;
        if (!world_) return c;
        UIContext& ui = g_.ui;
        Hud::Place p = hud_.place(ui, *world_, c, e);
        // A custom place keeps the rail length of the default one for the progress bar.
        HudPlace& hp = c.hud[e];
        if (!hp.custom || (e == HudElement::Progress && c.progressBar != 3)) {
            hp.custom = true;
            hp.x = p.o.x / ui.screenW();
            hp.y = p.o.y / ui.screenH();
            if (e == HudElement::Progress) c.progressBar = 3;
            p = hud_.place(ui, *world_, c, e);
        }
        Rect a = hud_.area(ui, *world_, e, p);
        topLeft.x = std::clamp(topLeft.x, 0.0f, std::max(0.0f, ui.screenW() - a.w));
        topLeft.y = std::clamp(topLeft.y, 0.0f, std::max(0.0f, ui.screenH() - a.h));
        Vec2 anchor = hud_.anchorForArea(ui, *world_, e, p, topLeft);
        hp.x = std::clamp(anchor.x / ui.screenW(), 0.0f, 1.0f);
        hp.y = std::clamp(anchor.y / ui.screenH(), 0.0f, 1.0f);
        return c;
    }

    void moveTo(HudElement e, Vec2 topLeft) {
        GameplaySettings c = moved(work_, e, topLeft);
        if (blocker(c, e).empty()) {
            work_ = c;
            return;
        }
        // Blocked: slide along the obstacle (one axis at a time) before giving up.
        Rect cur = areaOf(work_, e);
        GameplaySettings cx = moved(work_, e, {topLeft.x, cur.y});
        if (blocker(cx, e).empty()) {
            work_ = cx;
            return;
        }
        GameplaySettings cy = moved(work_, e, {cur.x, topLeft.y});
        if (blocker(cy, e).empty()) {
            work_ = cy;
            return;
        }
        tryApply(c, e);  // explains what is in the way
    }

    void setScale(HudElement e, float scale) {
        scale = std::clamp(scale, HudLayout::kMinScale, HudLayout::kMaxScale);
        Rect before = areaOf(work_, e);
        GameplaySettings c = work_;
        c.hud[e].scale = scale;
        Rect after = areaOf(c, e);
        if (before.w > 0.0f && after.w > 0.0f) {
            // Grows around its centre, kept inside the screen.
            Vec2 tl = before.center() - after.size() * 0.5f;
            c = moved(c, e, tl);
        }
        tryApply(c, e);
    }

    void resetElement(HudElement e) {
        GameplaySettings c = work_;
        c.hud[e] = HudPlace{};
        c.hud[e].visible = work_.hud[e].visible;
        if (e == HudElement::Progress && c.progressBar == 3) c.progressBar = 1;
        tryApply(c, e);
    }

    void resetAll() {
        GameplaySettings c = work_;
        bool timer = hudVisible(c, HudElement::Timer), bar = hudVisible(c, HudElement::Progress);
        c.hud = HudLayout{};
        c.showTimer = timer;
        c.progressBar = bar ? 1 : 0;
        work_ = c;
        status_ = "Disposition par défaut rétablie.";
        statusTimer_ = 2.0f;
    }

    /// Elements overlapping each other (a layout from another screen size, or edited by hand).
    std::vector<std::pair<int, int>> conflicts() {
        std::vector<std::pair<int, int>> out;
        for (int i = 0; i < kHudElements; ++i) {
            Rect a = areaOf(work_, static_cast<HudElement>(i));
            if (a.w <= 0.0f) continue;
            for (int j = i + 1; j < kHudElements; ++j) {
                Rect b = areaOf(work_, static_cast<HudElement>(j));
                if (b.w > 0.0f && a.intersects(b)) out.push_back({i, j});
            }
        }
        return out;
    }

    std::string firstConflict() {
        auto c = conflicts();
        if (c.empty()) return {};
        return std::string("« ") + HudLayout::label(static_cast<HudElement>(c[0].first)) + " » et « " +
               HudLayout::label(static_cast<HudElement>(c[0].second)) + " » se chevauchent : déplacez ou réduisez l'un des deux pour enregistrer.";
    }

    // ------------------------------------------------------------ drawing / input

    Rect toPreview(const Rect& a) const {
        return {preview_.x + a.x * previewScale_, preview_.y + a.y * previewScale_, a.w * previewScale_, a.h * previewScale_};
    }

    void drawOutlines(const Rect& P) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.pushClip(P);
        auto bad = conflicts();
        for (int i = 0; i < kHudElements; ++i) {
            auto e = static_cast<HudElement>(i);
            Rect a = areaOf(work_, e);
            if (a.w <= 0.0f) continue;
            Rect pr = toPreview(a);
            bool sel = i == selected_;
            bool clash = std::any_of(bad.begin(), bad.end(), [&](const auto& p) { return p.first == i || p.second == i; });
            Color c = clash ? ui.theme().danger : sel ? ui.theme().accent : Color{200, 214, 232, 120};
            if (sel) r.fillRect(pr, ui.theme().accent.withAlpha(28));
            r.drawRect(pr, c, sel ? ui.s(2.5f) : ui.s(1.0f));
        }
        r.popClip();
    }

    void handlePointer(const Rect& P) {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        Vec2 m = in.mousePos();
        if (in.mousePressed(SDL_BUTTON_LEFT) && P.contains(m)) {
            // Topmost element under the pointer (the smallest when nested).
            int hit = -1;
            float best = 1e30f;
            for (int i = 0; i < kHudElements; ++i) {
                Rect pr = toPreview(areaOf(work_, static_cast<HudElement>(i)));
                if (pr.w > 0.0f && pr.contains(m) && pr.w * pr.h < best) {
                    best = pr.w * pr.h;
                    hit = i;
                }
            }
            if (hit >= 0) {
                selected_ = hit;
                dragging_ = true;
                Rect a = areaOf(work_, static_cast<HudElement>(hit));
                grab_ = (m - P.pos()) / previewScale_ - a.pos();
            }
        }
        if (dragging_) {
            if (!in.mouseDown(SDL_BUTTON_LEFT)) {
                dragging_ = false;
            } else if (in.mouseMovedThisFrame()) {
                moveTo(static_cast<HudElement>(selected_), (m - P.pos()) / previewScale_ - grab_);
            }
        }
        if (in.wheel() != 0.0f && P.contains(m) && ui.inputEnabled) {
            auto e = static_cast<HudElement>(selected_);
            setScale(e, work_.hud[e].scale + (in.wheel() > 0.0f ? 0.05f : -0.05f));
        }
    }

    void drawPanel(const Rect& c) {
        UIContext& ui = g_.ui;
        float y = c.y;
        ui.label("Éléments", {c.x, y}, 22, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(38);
        float rowH = ui.s(52), gap = ui.s(6);
        for (int i = 0; i < kHudElements; ++i) {
            auto e = static_cast<HudElement>(i);
            char sub[96];
            if (hudVisible(work_, e))
                std::snprintf(sub, sizeof sub, "Affiché · %d %%%s", static_cast<int>(std::lround(work_.hud[e].scale * 100.0f)),
                              work_.hud[e].custom && (e != HudElement::Progress || work_.progressBar == 3) ? " · déplacé" : "");
            else
                std::snprintf(sub, sizeof sub, "Masqué");
            if (ui.listItem("el" + std::to_string(i), {c.x, y, c.w, rowH}, HudLayout::label(e), i == selected_, sub)) selected_ = i;
            y += rowH + gap;
        }
        y += ui.s(14);
        auto e = static_cast<HudElement>(selected_);
        std::string title = std::string("Réglages : ") + HudLayout::label(e);
        ui.label(title, {c.x, y}, ui.fitFont(title, FontFace::Bold, 22, c.w), ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(38);
        Rect r{c.x, y, c.w, ui.s(50)};
        float step = ui.s(58);
        bool vis = hudVisible(work_, e);
        if (ui.toggle("visible", r, "Afficher", vis)) {
            GameplaySettings cand = work_;
            setHudVisible(cand, e, vis);
            if (!vis) work_ = cand;
            else tryApply(cand, e);
        }
        r.y += step;
        Rect a = areaOf(work_, e);
        bool shown = a.w > 0.0f;
        float pct = work_.hud[e].scale * 100.0f;
        if (ui.slider("size", r, "Taille", pct, HudLayout::kMinScale * 100.0f, HudLayout::kMaxScale * 100.0f, 5.0f, "%.0f %%"))
            setScale(e, pct / 100.0f);
        r.y += step;
        float W = ui.screenW(), H = ui.screenH();
        float px = shown && W > a.w ? a.x / (W - a.w) * 100.0f : 0.0f;
        float py = shown && H > a.h ? a.y / (H - a.h) * 100.0f : 0.0f;
        if (ui.slider("posx", r, "Position horizontale", px, 0.0f, 100.0f, 1.0f, "%.0f %%") && shown)
            moveTo(e, {px / 100.0f * (W - a.w), a.y});
        r.y += step;
        if (ui.slider("posy", r, "Position verticale", py, 0.0f, 100.0f, 1.0f, "%.0f %%") && shown)
            moveTo(e, {a.x, py / 100.0f * (H - a.h)});
        r.y += step;
        if (ui.button("reset", r, "Place et taille par défaut")) resetElement(e);
    }

    GameServices& g_;
    GameplaySettings& target_;
    GameplaySettings work_;
    MenuBackground bg_;
    std::unique_ptr<GameWorld> world_;
    GameRenderer renderer_;
    Hud hud_;
    std::future<LevelProgressTrack> track_;
    TexturePtr target_tex_;
    Rect preview_;
    float previewScale_ = 0.6f;
    int selected_ = 0;
    bool dragging_ = false;
    bool bossShown_ = false;
    bool demo_ = false;
    int demoStep_ = 0;
    Vec2 grab_;
    std::string status_;
    float statusTimer_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeHudEditor(GameServices& g, GameplaySettings& target) {
    return std::make_unique<HudEditorScene>(g, target);
}

}  // namespace cosmic
