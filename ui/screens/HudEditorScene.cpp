// HUD editor (Paramètres › Jeu › Personnaliser le HUD): the player places and sizes each
// element of the in-game HUD on a live preview (a real level drawn by the game, with the
// real HUD). Elements can never overlap each other nor leave the screen.
//
// 2.71: a third tab, « Raids » (docs/RAIDS.md §15): the HUD of the clan raids, its own layout
// (GameplaySettings::raidHud, used by RaidGameScene): this player's vitals, powers and the
// gigaboss's bar, the minimap, the group panel, the objectives, the alerts, the danger band, the
// contributions, the network line / « TEST ADMIN » badge, and the off-screen arrows' toggle.
// Previewed with an example of a raid of 8 players (said as such on the preview: the editor's
// only, never in game), drawn by the raid's own code (ui/online/RaidHud).

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
#include "ui/online/RaidHud.h"
#include "ui/online/RaidWidgets.h"
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
        if (mode_ == 2) {
            // The preview is an example (the editor's only): said on it, measured into its corner.
            const std::string ex = "EXEMPLE · raid à 8 joueurs";
            const float ew = ui.text().measure(ex, FontFace::Bold, ui.s(15)).x + ui.s(20);
            const Rect pill{P.x + ui.s(8), P.y + ui.s(8), std::min(ew, P.w - ui.s(16)), ui.s(28)};
            ui.r().fillRoundedRect(pill, ui.s(8), Color{120, 20, 40, 235});
            ui.label(ui.elide(ex, FontFace::Bold, 15, pill.w - ui.s(16)), pill.center(), 15, Color{255, 210, 122, 255}, Align::Center, FontFace::Bold,
                     VAlign::Middle);
        }
        Vec2 used = ui.labelWrapped(mode_ == 2 ? withKey("Raids : la disposition des combats de raid, à part du HUD des niveaux. L'aperçu est un exemple de "
                                                         "raid à 8 joueurs (noms et objectifs d'exemple, jamais en jeu). La carte s'agrandit "
                                                         "en raid avec la touche {0}. Deux éléments ne peuvent jamais se chevaucher.",
                                                         g_.app.input().bindings(), Action::RaidMap)
                                    : mode_ == 1 ? withKey("Mode concentré (touche {0}, Partage à la manette, ou le menu pause) : choisissez les éléments "
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
        hud_.setEditorPreview(true);   // 2.62.2: the ambush's counter shown without a fight
        hud_.setRaidView(mode_ == 2);  // 2.71: the raids' HUD (vitals, powers, the gigaboss's bar)
        if (mode_ == 2) {
            hud_.render(g_.ui, *world_, view, nullptr, raidhud::hudSettings(work_));
            drawRaidExample();
            r.setTarget(nullptr);
            return;
        }
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
        if (mode_ == 2) {
            const bool arrows = work_.raidHud.arrows;
            work_.raidHud = RaidHudLayout{};
            work_.raidHud.arrows = arrows;
            status_ = "Disposition des raids par défaut rétablie.";
            statusTimer_ = 2.0f;
            return;
        }
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
        if (c.empty()) {
            // 2.71: the raids' layout is saved with the other one: it must fit too.
            const auto rc = raidConflicts();
            if (rc.empty()) return {};
            return std::string("Raids : « ") + RaidHudLayout::label(static_cast<RaidHudElement>(rc[0].first)) + " » et « " +
                   RaidHudLayout::label(static_cast<RaidHudElement>(rc[0].second)) + " » se chevauchent : déplacez ou réduisez l'un des deux pour enregistrer.";
        }
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
        if (mode_ == 2) {
            drawRaidOutlines(P);
            return;
        }
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
        if (mode_ == 2) {
            raidPointer(P);
            return;
        }
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
        if (ui.tabs("mode", {c.x, y, c.w, ui.s(46)}, {"HUD complet", "Mode concentré", "Raids"}, mode)) {
            mode_ = mode;
            work_.focusMode = mode_ == 1;
            selected_ = 0;
            dragging_ = false;
        }
        y += ui.s(58);
        if (mode_ == 2) {
            drawRaidPanel({c.x, y, c.w, c.bottom() - y});
            return;
        }
        ui.label(mode_ == 1 ? "Éléments gardés en mode concentré" : "Éléments", {c.x, y}, ui.fitFont(mode_ == 1 ? "Éléments gardés en mode concentré" : "Éléments", FontFace::Bold, 22, c.w),
                 ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(38);
        // The rows share what the settings below leave (2.62.2: nine elements): never under 44
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
            // « NOUVEAU » after the name of the elements added in 2.52 and 2.62.2 (seen once focused).
            const char* novelty = e == HudElement::Enemies ? "hud.enemies" : e == HudElement::Ambush ? "hud.ambush" : nullptr;
            if (novelty && ui.newBadge("el" + std::to_string(i), row, novelty, false, true)) {
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

    // ------------------------------------------------------------ 2.71: the raids' layout

    using RE = RaidHudElement;

    /// The game's HUD areas at the raid layout's places of `s`.
    raidhud::HudAreas raidHudAreas(const GameplaySettings& s) {
        raidhud::HudAreas a;
        if (!world_) return a;
        const GameplaySettings hs = raidhud::hudSettings(s);
        auto of = [&](HudElement e) { return hudVisible(hs, e) ? hud_.area(g_.ui, *world_, e, hud_.place(g_.ui, *world_, hs, e)) : Rect{}; };
        a.vitals = of(HudElement::Vitals);
        a.powers = of(HudElement::Powers);
        a.boss = of(HudElement::Boss);
        return a;
    }

    /// The area an element of the raids' layout takes in `s` (empty when hidden).
    Rect raidAreaOf(const GameplaySettings& s, RE e) {
        if (!world_) return {};
        if (!s.raidHud[e].visible && RaidHudLayout::canHide(e)) return {};
        return raidhud::area(g_.ui, s.raidHud, e, raidHudAreas(s));
    }

    std::vector<std::pair<int, int>> raidConflicts() {
        std::vector<std::pair<int, int>> out;
        for (int i = 0; i < kRaidHudElements; ++i) {
            const Rect a = raidAreaOf(work_, static_cast<RE>(i));
            if (a.w <= 0.0f) continue;
            for (int j = i + 1; j < kRaidHudElements; ++j) {
                const Rect b = raidAreaOf(work_, static_cast<RE>(j));
                if (b.w > 0.0f && a.intersects(b)) out.push_back({i, j});
            }
        }
        return out;
    }

    std::vector<int> raidOverlapsOf(const GameplaySettings& s, RE e) {
        std::vector<int> out;
        const Rect a = raidAreaOf(s, e);
        if (a.w <= 0.0f) return out;
        for (int i = 0; i < kRaidHudElements; ++i) {
            if (i == static_cast<int>(e)) continue;
            const Rect b = raidAreaOf(s, static_cast<RE>(i));
            if (b.w > 0.0f && a.intersects(b)) out.push_back(i);
        }
        return out;
    }

    /// As blocker(): empty when `e` fits at its place in `s`, else what is in the way.
    std::string raidBlocker(const GameplaySettings& s, RE e) {
        const Rect a = raidAreaOf(s, e);
        if (a.w <= 0.0f) return {};
        const float W = g_.ui.screenW(), H = g_.ui.screenH();
        if (a.x < -0.5f || a.y < -0.5f || a.right() > W + 0.5f || a.bottom() > H + 0.5f) return "le bord de l'écran";
        const std::vector<int> before = raidOverlapsOf(work_, e);
        for (int i : raidOverlapsOf(s, e))
            if (std::find(before.begin(), before.end(), i) == before.end()) return std::string("« ") + RaidHudLayout::label(static_cast<RE>(i)) + " »";
        return {};
    }

    bool raidTryApply(const GameplaySettings& c, RE e) {
        const std::string why = raidBlocker(c, e);
        if (!why.empty()) {
            status_ = std::string("Impossible : « ") + RaidHudLayout::label(e) + " » toucherait " + why + ".";
            statusTimer_ = 2.5f;
            return false;
        }
        work_ = c;
        return true;
    }

    /// `s` with the element's area moved to `topLeft` (kept inside the screen).
    GameplaySettings raidMoved(const GameplaySettings& s, RE e, Vec2 topLeft) {
        GameplaySettings c = s;
        if (!world_) return c;
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        HudPlace& hp = c.raidHud[e];
        const HudElement he = RaidHudLayout::hudElement(e);
        if (he != HudElement::Count) {
            // The game's HUD element: its anchor, as the solo layout keeps it.
            GameplaySettings hs = raidhud::hudSettings(c);
            Hud::Place p = hud_.place(ui, *world_, hs, he);
            if (!hp.custom) {
                hp.custom = true;
                hp.x = p.o.x / W;
                hp.y = p.o.y / H;
                hs = raidhud::hudSettings(c);
                p = hud_.place(ui, *world_, hs, he);
            }
            const Rect a = hud_.area(ui, *world_, he, p);
            topLeft.x = std::clamp(topLeft.x, 0.0f, std::max(0.0f, W - a.w));
            topLeft.y = std::clamp(topLeft.y, 0.0f, std::max(0.0f, H - a.h));
            const Vec2 anchor = hud_.anchorForArea(ui, *world_, he, p, topLeft);
            hp.x = std::clamp(anchor.x / W, 0.0f, 1.0f);
            hp.y = std::clamp(anchor.y / H, 0.0f, 1.0f);
            return c;
        }
        const Rect a = raidhud::area(ui, c.raidHud, e, raidHudAreas(c));
        hp.custom = true;
        hp.x = std::clamp(topLeft.x, 0.0f, std::max(0.0f, W - a.w)) / W;
        hp.y = std::clamp(topLeft.y, 0.0f, std::max(0.0f, H - a.h)) / H;
        return c;
    }

    void raidMoveTo(RE e, Vec2 topLeft) {
        GameplaySettings c = raidMoved(work_, e, topLeft);
        if (raidBlocker(c, e).empty()) {
            work_ = c;
            return;
        }
        // Blocked: slide along the obstacle (one axis at a time) before giving up.
        const Rect cur = raidAreaOf(work_, e);
        GameplaySettings cx = raidMoved(work_, e, {topLeft.x, cur.y});
        if (raidBlocker(cx, e).empty()) {
            work_ = cx;
            return;
        }
        GameplaySettings cy = raidMoved(work_, e, {cur.x, topLeft.y});
        if (raidBlocker(cy, e).empty()) {
            work_ = cy;
            return;
        }
        raidTryApply(c, e);  // explains what is in the way
    }

    void raidSetScale(RE e, float scale) {
        scale = std::clamp(scale, HudLayout::kMinScale, HudLayout::kMaxScale);
        const Rect before = raidAreaOf(work_, e);
        GameplaySettings c = work_;
        c.raidHud[e].scale = scale;
        const Rect after = raidAreaOf(c, e);
        if (before.w > 0.0f && after.w > 0.0f && (c.raidHud[e].custom || RaidHudLayout::hudElement(e) != HudElement::Count))
            c = raidMoved(c, e, before.center() - after.size() * 0.5f);  // grows around its centre
        raidTryApply(c, e);
    }

    void drawRaidOutlines(const Rect& P) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.pushClip(P);
        const auto bad = raidConflicts();
        for (int i = 0; i < kRaidHudElements; ++i) {
            const Rect a = raidAreaOf(work_, static_cast<RE>(i));
            if (a.w <= 0.0f) continue;
            const Rect pr = toPreview(a);
            const bool sel = i == selected_;
            const bool clash = std::any_of(bad.begin(), bad.end(), [&](const auto& p) { return p.first == i || p.second == i; });
            const Color c = clash ? ui.theme().danger : sel ? ui.theme().accent : Color{200, 214, 232, 120};
            if (sel) r.fillRect(pr, ui.theme().accent.withAlpha(28));
            r.drawRect(pr, c, sel ? ui.s(2.5f) : ui.s(1.0f));
        }
        r.popClip();
    }

    void raidPointer(const Rect& P) {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        const Vec2 m = in.mousePos();
        if (in.mousePressed(SDL_BUTTON_LEFT) && P.contains(m)) {
            int hit = -1;
            float best = 1e30f;
            for (int i = 0; i < kRaidHudElements; ++i) {
                const Rect pr = toPreview(raidAreaOf(work_, static_cast<RE>(i)));
                if (pr.w > 0.0f && pr.contains(m) && pr.w * pr.h < best) {
                    best = pr.w * pr.h;
                    hit = i;
                }
            }
            if (hit >= 0) {
                selected_ = hit;
                dragging_ = true;
                grab_ = (m - P.pos()) / previewScale_ - raidAreaOf(work_, static_cast<RE>(hit)).pos();
            }
        }
        if (dragging_) {
            if (!in.mouseDown(SDL_BUTTON_LEFT)) dragging_ = false;
            else if (in.mouseMovedThisFrame()) raidMoveTo(static_cast<RE>(selected_), (m - P.pos()) / previewScale_ - grab_);
        }
        if (in.wheel() != 0.0f && P.contains(m) && ui.inputEnabled) {
            const auto e = static_cast<RE>(selected_);
            raidSetScale(e, work_.raidHud[e].scale + (in.wheel() > 0.0f ? 0.05f : -0.05f));
        }
    }

    void drawRaidPanel(const Rect& c) {
        UIContext& ui = g_.ui;
        float y = c.y;
        const std::string head = "Éléments du HUD des raids";
        ui.label(head, {c.x, y}, ui.fitFont(head, FontFace::Bold, 22, c.w), ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(38);
        const float gap = ui.s(6);
        const float below = ui.s(48) + gap + ui.s(14) + ui.s(38) + ui.s(50) + 4.0f * ui.s(58);
        const float rowH = std::clamp((c.bottom() - y - below) / static_cast<float>(kRaidHudElements) - gap, ui.s(40), ui.s(52));
        selected_ = std::clamp(selected_, 0, kRaidHudElements - 1);
        for (int i = 0; i < kRaidHudElements; ++i) {
            const auto e = static_cast<RE>(i);
            const bool vis = work_.raidHud[e].visible || !RaidHudLayout::canHide(e);
            char sub[96];
            if (vis)
                std::snprintf(sub, sizeof sub, "Affiché · %d %%%s", static_cast<int>(std::lround(work_.raidHud[e].scale * 100.0f)),
                              work_.raidHud[e].custom ? " · déplacé" : "");
            else
                std::snprintf(sub, sizeof sub, "Masqué");
            if (ui.listItem("rel" + std::to_string(i), {c.x, y, c.w, rowH}, RaidHudLayout::label(e), i == selected_, sub)) selected_ = i;
            y += rowH + gap;
        }
        ui.toggle("raidarrows", {c.x, y, c.w, ui.s(48)}, "Flèches hors écran (ennemis, embuscade, boss, alliés)", work_.raidHud.arrows);
        y += ui.s(48) + gap + ui.s(14);
        const auto e = static_cast<RE>(selected_);
        const std::string title = std::string("Réglages : ") + RaidHudLayout::label(e);
        ui.label(title, {c.x, y}, ui.fitFont(title, FontFace::Bold, 22, c.w), ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(38);
        Rect r{c.x, y, c.w, ui.s(50)};
        const float step = ui.s(58);
        bool vis = work_.raidHud[e].visible || !RaidHudLayout::canHide(e);
        if (RaidHudLayout::canHide(e)) {
            if (ui.toggle("rvisible", r, "Afficher", vis)) {
                GameplaySettings cand = work_;
                cand.raidHud[e].visible = vis;
                if (!vis) work_ = cand;
                else raidTryApply(cand, e);
            }
        } else {
            ui.labelWrapped("Toujours affiché : le groupe doit le voir.", {r.x, r.y + ui.s(12)}, r.w, 17, ui.theme().textDim);
        }
        r.y += step;
        const Rect a = raidAreaOf(work_, e);
        const bool shown = a.w > 0.0f;
        float pct = work_.raidHud[e].scale * 100.0f;
        if (ui.slider("rsize", r, "Taille", pct, HudLayout::kMinScale * 100.0f, HudLayout::kMaxScale * 100.0f, 5.0f, "%.0f %%")) raidSetScale(e, pct / 100.0f);
        r.y += step;
        const float W = ui.screenW(), H = ui.screenH();
        float px = shown && W > a.w ? a.x / (W - a.w) * 100.0f : 0.0f;
        float py = shown && H > a.h ? a.y / (H - a.h) * 100.0f : 0.0f;
        if (ui.slider("rposx", r, "Position horizontale", px, 0.0f, 100.0f, 1.0f, "%.0f %%") && shown) raidMoveTo(e, {px / 100.0f * (W - a.w), a.y});
        r.y += step;
        if (ui.slider("rposy", r, "Position verticale", py, 0.0f, 100.0f, 1.0f, "%.0f %%") && shown) raidMoveTo(e, {a.x, py / 100.0f * (H - a.h)});
        r.y += step;
        if (ui.button("rreset", r, "Place et taille par défaut")) {
            GameplaySettings cand = work_;
            const bool keep = cand.raidHud[e].visible;
            cand.raidHud[e] = HudPlace{};
            cand.raidHud[e].visible = keep;
            raidTryApply(cand, e);
        }
    }

    /// The example of a raid of 8 players drawn into the preview (the editor's only, said on it).
    void drawRaidExample() {
        UIContext& ui = g_.ui;
        using namespace raidui;
        const RaidHudLayout& l = work_.raidHud;
        const raidhud::HudAreas ha = raidHudAreas(work_);
        auto at = [&](RE e) { return raidhud::area(ui, l, e, ha); };
        auto k = [&](RE e) { return raidhud::scaleOf(l, e); };
        auto shown = [&](RE e) { return l[e].visible || !RaidHudLayout::canHide(e); };
        static const Color kSeat[8] = {{255, 210, 122, 255}, {255, 122, 184, 255}, {124, 240, 168, 255}, {159, 216, 255, 255},
                                       {255, 176, 74, 255},  {201, 168, 255, 255}, {255, 236, 140, 255}, {255, 138, 138, 255}};
        if (shown(RE::Group)) {
            raidhud::Group g;
            g.title = "[EXEMPLE]·1 · 8 / 8";
            for (int i = 0; i < 8; ++i) {
                raidhud::GroupRow row;
                row.name = i == 0 ? "Vous" : "Joueur " + std::to_string(i + 1);
                row.color = kSeat[i];
                row.playing = i != 5;
                row.health = i == 5 ? 0.0f : 1.0f - 0.1f * static_cast<float>(i % 4);
                row.state = i == 5 ? "à terre" : "en jeu";
                row.stateColor = i == 5 ? Color{255, 74, 74, 255} : kGreen;
                g.rows.push_back(row);
            }
            g.footer = "Réanimations de la zone : 1 / 2";
            raidhud::drawGroup(ui, at(RE::Group), k(RE::Group), g);
        }
        if (shown(RE::Minimap) && world_) {
            using raidhud::Icon;
            std::vector<raidhud::Mark> marks;
            const Vec2 me = world_->player().center();
            for (const auto& e : world_->enemies())
                if (e->alive()) marks.push_back({e->def().isBoss ? Icon::Boss : Icon::Enemy, e->center()});
            for (int i = 1; i < 8; ++i) marks.push_back({Icon::Ally, me + Vec2{static_cast<float>(i) * 26.0f - 90.0f, 0.0f}, kSeat[i]});
            marks.push_back({Icon::Objective, me + Vec2{420.0f, -60.0f}});
            marks.push_back({Icon::Exit, {world_->level().bounds.right() - 120.0f, me.y}});
            marks.push_back({Icon::Me, me});
            minimap_.drawSmall(ui, g_.app.resources(), *world_, at(RE::Minimap), k(RE::Minimap), "MINIMAP · Exemple", marks, {}, me);
        }
        if (shown(RE::Objectives))
            raidhud::drawObjectives(ui, at(RE::Objectives), k(RE::Objectives), "OBJECTIFS · Exemple",
                                    {{"→", "Générateur du verrou : frappez-le avec un pouvoir de tir", Color{255, 176, 74, 255}},
                                     {"1 / 2", "Poignet-verrou : les deux générateurs", kGold},
                                     {"◆", "Relique de la grotte (facultatif)", Color{201, 168, 255, 255}}});
        if (shown(RE::Alerts))
            raidhud::drawAlerts(ui, at(RE::Alerts), k(RE::Alerts),
                                {{"", "GROUPE · Objectif accompli : générateur ouest", kGold}, {"", "BRANCHE · Le raccourci est ouvert", kGreen}});
        raidhud::drawDanger(ui, at(RE::Danger), k(RE::Danger), "Montée de magma dans 3 s · montez !");
        if (shown(RE::Contributions))
            raidhud::drawLineBox(ui, at(RE::Contributions), k(RE::Contributions), "Dégâts 120 · Mécanismes 2 · Sauvetages 1 · Dégâts évités 30", kText,
                                 false);
        raidhud::drawLineBox(ui, at(RE::Status), k(RE::Status), "TEST ADMIN · non classé · 8 joueurs (7 simulés)", kGold, true);
        if (shown(RE::Timer)) raidhud::drawTimer(ui, at(RE::Timer), k(RE::Timer), 754.36f, 201.07f);
    }

    raidhud::Minimap minimap_;  // 2.71: the example's minimap (the preview level's terrain)

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
    int mode_ = 0;  // 0 the full HUD, 1 the « mode concentré », 2 (2.71) the raids
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
