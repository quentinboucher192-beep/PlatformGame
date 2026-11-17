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
#include "game/powers/PowerKeys.h"
#include "ui/screens/GhostRun.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {
/// « {0} » replaced by the key bound to `a`, as the player's keyboard layout names it
/// (QWERTY, AZERTY…): never a letter written in the text.
std::string withKey(std::string text, const InputBindings& b, Action a) {
    const auto& keys = b.keys[static_cast<int>(a)];
    std::string name = keyName(keys[0] != SDL_SCANCODE_UNKNOWN ? keys[0] : keys[1]);
    if (size_t at = text.find("{0}"); at != std::string::npos) text.replace(at, 3, name);
    return text;
}
}  // namespace


namespace {

constexpr const char* kPreviewLevel = "sylvanox_01";

class HudEditorScene : public Scene {
public:
    HudEditorScene(GameServices& g, GameplaySettings& target) : g_(g), target_(target), work_(target), savedFocus_(target.focusMode) {
        work_.focusMode = false;
    }

    void onEnter() override {
        buildWorld();
        g_.ui.setFocus("hud/el0");
        // Automated screenshots (--hud-editor --online-open hud-demo): the editor's own
        // operations, as the mouse and the sliders use them.
        demo_ = g_.onlineOpen == "hud-demo";
        if (demo_) g_.onlineOpen.clear();
        if (g_.onlineOpen == "hud-focus") {  // automated screenshots: the focus mode's tab
            mode_ = 1;
            work_.focusMode = true;
            g_.onlineOpen.clear();
        }
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
            // The preview's powers on their suggested keys (2.31), as a level without its own.
            applyPowerSlotKeys(g_.app.input(), world_->player(), previewKeys_);
        }
    }

    void onExit() override { g_.app.input().clearPowerSlotKeys(); }

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
        Vec2 used = ui.labelWrapped(mode_ == 1 ? withKey("Mode concentré (touche {0}, Partage à la manette, ou le menu pause) : choisissez les éléments "
                                                         "gardés. Les places et les tailles sont les mêmes dans les deux modes ; deux éléments ne peuvent "
                                                         "jamais se chevaucher.",
                                                         g_.app.input().bindings(), Action::FocusToggle)
                                               : "Souris : glissez un élément pour le déplacer, molette pour changer sa taille. Clavier et "
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
            target_.focusMode = savedFocus_;  // the mode in use stays the player's choice in game
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
        world_->setKeysPerSlot(true);
        world_->load(*level);
        // A representative HUD: four powers carried, one of them on a move (the glider: its
        // frame says that the key of its slot does nothing), and a boss fight (fixedUpdate).
        for (const char* id : {"dash", "wave", "glide", "time_slow"})
            if (const PowerDefinition* d = g_.content.power(id)) world_->player().grantPower(*d);
        previewKeys_ = resolvePowerKeys({"dash", "wave", "time_slow"}, g_.content, Json::object(), nullptr, g_.settings.input);
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
        hud_.setFadeAllowed(false);  // 2.60: the blocks being placed never fade
        hud_.render(g_.ui, *world_, view, nullptr, work_);
        if (hudVisible(work_, HudElement::Splits)) {
            Hud::Place sp = hud_.place(g_.ui, *world_, work_, HudElement::Splits);
            GhostRun::drawPreview(g_.ui, hud_.area(g_.ui, *world_, HudElement::Splits, sp), sp.k);
        }
        r.setTarget(nullptr);
    }

    // ------------------------------------------------------------ layout rules

    /// The place an element takes: shown in either mode (places are common to both modes, so
    /// no layout can overlap in one of them).
    Rect areaOf(const GameplaySettings& s, HudElement e) {
        if (!world_) return {};
        GameplaySettings a = s;
        a.focusMode = false;
        bool shown = hudVisible(a, e);
        a.focusMode = true;
        shown = shown || hudVisible(a, e);
        if (!shown) return {};
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
        c.hud[e].focus = work_.hud[e].focus;
        if (e == HudElement::Progress && c.progressBar == 3) c.progressBar = 1;
        tryApply(c, e);
    }

    void resetAll() {
        GameplaySettings c = work_;
        bool timer = hudVisible(c, HudElement::Timer), bar = hudVisible(c, HudElement::Progress);
        HudLayout fresh;
        for (int i = 0; i < kHudElements; ++i) fresh.items[static_cast<size_t>(i)].focus = c.hud.items[static_cast<size_t>(i)].focus;
        c.hud = fresh;  // places and sizes; what each mode shows is kept
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
            if (!clash && !sel && !hudVisible(work_, e)) c = Color{200, 214, 232, 45};  // hidden in this mode
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
        // The two modes: the full HUD, and the « mode concentré » (toggled in game).
        int mode = mode_;
        if (ui.tabs("mode", {c.x, y, c.w, ui.s(46)}, {"HUD complet", "Mode concentré"}, mode)) {
            mode_ = mode;
            work_.focusMode = mode_ == 1;
        }
        y += ui.s(58);
        ui.label(mode_ == 1 ? "Éléments gardés en mode concentré" : "Éléments", {c.x, y}, ui.fitFont(mode_ == 1 ? "Éléments gardés en mode concentré" : "Éléments", FontFace::Bold, 22, c.w),
                 ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(38);
        // The rows share what the settings below leave (2.52: eight elements): never under 44
        // pixels at 100 %, never pushing the settings off the panel.
        float gap = ui.s(6);
        const float below = ui.s(48) + gap + ui.s(14) + ui.s(38) + ui.s(50) + 4.0f * ui.s(58);
        float rowH = std::clamp((c.bottom() - y - below) / static_cast<float>(kHudElements) - gap, ui.s(44), ui.s(52));
        for (int i = 0; i < kHudElements; ++i) {
            auto e = static_cast<HudElement>(i);
            char sub[96];
            if (hudVisible(work_, e))
                std::snprintf(sub, sizeof sub, "Affiché · %d %%%s", static_cast<int>(std::lround(work_.hud[e].scale * 100.0f)),
                              work_.hud[e].custom && (e != HudElement::Progress || work_.progressBar == 3) ? " · déplacé" : "");
            else
                std::snprintf(sub, sizeof sub, "Masqué");
            const Rect row{c.x, y, c.w, rowH};
            if (ui.listItem("el" + std::to_string(i), row, HudLayout::label(e), i == selected_, sub)) selected_ = i;
            // « NOUVEAU » after the name of the element added in 2.52 (seen once focused).
            if (e == HudElement::Enemies && ui.newBadge("el" + std::to_string(i), row, "hud.enemies", false, true)) {
                const float nameW = ui.text().measure(HudLayout::label(e), FontFace::Bold, ui.s(21)).x;
                if (row.x + ui.s(16) + nameW + ui.s(10) + ui.newPillWidth(true) <= row.right() - ui.s(10))
                    ui.drawNewPill(row.x + ui.s(16) + nameW + ui.s(10) + ui.newPillWidth(true), row.y + row.h * 0.3f, true);
            }
            y += rowH + gap;
        }
        // The level's messages (hints, signs, titles): not a place on screen, just shown or not.
        {
            bool& msg = mode_ == 1 ? work_.focusMessages : work_.showMessages;
            ui.toggle("messages", {c.x, y, c.w, ui.s(48)}, "Messages du niveau (indications, titres)", msg);
            y += ui.s(48) + gap;
        }
        y += ui.s(14);
        auto e = static_cast<HudElement>(selected_);
        std::string title = std::string("Réglages : ") + HudLayout::label(e);
        ui.label(title, {c.x, y}, ui.fitFont(title, FontFace::Bold, 22, c.w), ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(38);
        Rect r{c.x, y, c.w, ui.s(50)};
        float step = ui.s(58);
        bool vis = hudVisible(work_, e);
        if (ui.toggle("visible", r, mode_ == 1 ? "Garder en mode concentré" : "Afficher", vis)) {
            GameplaySettings cand = work_;
            if (mode_ == 1) {
                cand.hud[e].focus = vis;
                if (vis) {  // shown in focus mode needs the element itself to be shown
                    cand.focusMode = false;
                    setHudVisible(cand, e, true);
                    cand.focusMode = true;
                }
            } else {
                setHudVisible(cand, e, vis);
            }
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
    PowerKeyMap previewKeys_;
    GameRenderer renderer_;
    Hud hud_;
    std::future<LevelProgressTrack> track_;
    TexturePtr target_tex_;
    Rect preview_;
    float previewScale_ = 0.6f;
    int selected_ = 0;
    int mode_ = 0;  // 0 the full HUD, 1 the « mode concentré »
    bool savedFocus_ = false;
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
