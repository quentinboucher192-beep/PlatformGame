// Settings: graphics, audio, keyboard / gamepad controls (independent rebinding), gameplay.

#include <cmath>
#include <format>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

const std::vector<std::pair<int, int>> kResolutions = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

class SettingsScene : public Scene {
public:
    SettingsScene(GameServices& g, bool overlay) : g_(g), overlay_(overlay) {}

    bool isOverlay() const override { return overlay_; }

    void onEnter() override {
        working_ = g_.settings;
        g_.ui.setFocus("settings/tabs");
    }

    void onExit() override {
        g_.app.input().cancelCapture();
        g_.settings = working_;
        g_.applySettings();
        g_.saveSettings();
    }

    void update(float dt) override {
        t_ += dt;
        if (!overlay_) bg_.update(dt);
        if (captureTimer_ > 0.0f) {
            captureTimer_ -= dt;
            if (captureTimer_ <= 0.0f) g_.app.input().cancelCapture();
        }
        if (messageTimer_ > 0.0f) messageTimer_ -= dt;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        if (overlay_)
            ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, {4, 6, 14, 220});
        else
            bg_.render(g_, 0.55f);
        InputManager& in = g_.app.input();
        handleCapture();
        bool capturing = in.capturing();
        ui.inputEnabled = !capturing;

        float w = std::min(ui.s(1500), ui.screenW() - ui.s(80)), h = ui.screenH() - ui.s(140);
        Rect panel{(ui.screenW() - w) * 0.5f, ui.s(60), w, h};
        ui.panel(panel, false, 18);
        ui.label("PARAMÈTRES", {panel.x + ui.s(40), panel.y + ui.s(26)}, 40, ui.theme().text, Align::Left, FontFace::Title);
        Rect tabsR{panel.x + ui.s(40), panel.y + ui.s(90), panel.w - ui.s(80), ui.s(54)};
        ui.tabs("settings/tabs", tabsR, {"Graphismes", "Audio", "Clavier / souris", "Manette", "Jeu"}, tab_);
        Rect content{panel.x + ui.s(40), tabsR.bottom() + ui.s(24), panel.w - ui.s(80), panel.bottom() - tabsR.bottom() - ui.s(110)};
        ui.pushId("tab" + std::to_string(tab_));
        switch (tab_) {
            case 0: graphics(content); break;
            case 1: audio(content); break;
            case 2: keyboard(content); break;
            case 3: gamepad(content); break;
            case 4: gameplay(content); break;
        }
        ui.popId();
        ui.inputEnabled = true;

        float by = panel.bottom() - ui.s(80);
        UIContext::ButtonStyle p;
        p.primary = true;
        if (!capturing && (ui.button("settings/back", {panel.right() - ui.s(340), by, ui.s(300), ui.s(58)}, "Retour", p) ||
                           ui.backPressed())) {
            g_.app.popScene();
        }
        if (messageTimer_ > 0.0f)
            ui.label(message_, {panel.x + ui.s(40), by + ui.s(29)}, 22, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        float fy = ui.screenH() - ui.s(40);
        float x = panel.x;
        x += Glyphs::drawHint(ui, Action::MenuTabPrev, "/", {x, fy}, ui.s(30));
        x += Glyphs::drawHint(ui, Action::MenuTabNext, "Onglets", {x + ui.s(6), fy}, ui.s(30)) + ui.s(40);
        Glyphs::drawHint(ui, Action::MenuBack, "Retour", {x, fy}, ui.s(30));

        if (capturing) drawCaptureModal();
        ui.end();
    }

private:
    float row() const { return g_.ui.s(62); }

    void graphics(const Rect& c) {
        UIContext& ui = g_.ui;
        float y = c.y, w = std::min(c.w, ui.s(900));
        Rect r{c.x, y, w, ui.s(54)};
        auto next = [&]() { r.y += row(); };
        if (ui.toggle("fullscreen", r, "Plein écran", working_.graphics.fullscreen)) {
            g_.app.setFullscreen(working_.graphics.fullscreen);
        }
        next();
        if (ui.toggle("vsync", r, "Synchronisation verticale", working_.graphics.vsync)) g_.app.setVSync(working_.graphics.vsync);
        next();
        std::vector<std::string> res;
        int cur = 1;
        for (size_t i = 0; i < kResolutions.size(); ++i) {
            res.push_back(std::format("{} x {}", kResolutions[i].first, kResolutions[i].second));
            if (kResolutions[i].first == working_.graphics.windowWidth) cur = static_cast<int>(i);
        }
        if (ui.cycle("resolution", r, "Taille de la fenêtre", cur, res)) {
            working_.graphics.windowWidth = kResolutions[cur].first;
            working_.graphics.windowHeight = kResolutions[cur].second;
            g_.app.setWindowSize(working_.graphics.windowWidth, working_.graphics.windowHeight);
        }
        next();
        ui.toggle("lighting", r, "Éclairage dynamique", working_.graphics.dynamicLighting);
        next();
        ui.cycle("particles", r, "Qualité des particules", working_.graphics.particleQuality, {"Basse", "Moyenne", "Haute"});
        next();
        ui.slider("shake", r, "Tremblements d'écran", working_.graphics.screenShake, 0.0f, 1.5f, 0.1f, "%.1f");
        next();
        ui.toggle("fps", r, "Afficher les FPS", working_.graphics.showFps);
    }

    void audio(const Rect& c) {
        UIContext& ui = g_.ui;
        float w = std::min(c.w, ui.s(900));
        Rect r{c.x, c.y, w, ui.s(54)};
        bool changed = false;
        changed |= ui.slider("master", r, "Volume général", working_.audio.master, 0, 1, 0.05f, "%.2f");
        r.y += row();
        changed |= ui.slider("music", r, "Musique", working_.audio.music, 0, 1, 0.05f, "%.2f");
        r.y += row();
        bool fx = ui.slider("effects", r, "Effets sonores", working_.audio.effects, 0, 1, 0.05f, "%.2f");
        changed |= fx;
        r.y += row();
        changed |= ui.slider("ambience", r, "Ambiance", working_.audio.ambience, 0, 1, 0.05f, "%.2f");
        if (changed) g_.app.audio().setVolumes(working_.audio);
        if (fx) g_.uiSound("collect_shard", 0.7f);
    }

    void bindingRow(const Rect& r, Action a, bool pad) {
        UIContext& ui = g_.ui;
        int i = static_cast<int>(a);
        ui.label(actionLabel(a), {r.x + ui.s(10), r.center().y}, 22, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
        for (int slot = 0; slot < 2; ++slot) {
            Rect b{r.x + r.w * 0.42f + slot * (r.w * 0.29f), r.y, r.w * 0.27f, r.h};
            std::string label;
            if (pad) label = padBindingName(working_.input.pad[i][slot], g_.app.input().gamepadStyle());
            else label = keyName(working_.input.keys[i][slot]);
            std::string id = std::string(pad ? "p" : "k") + actionId(a) + std::to_string(slot);
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 20;
            if (ui.button(id, b, label, st)) {
                captureAction_ = a;
                captureSlot_ = slot;
                capturePad_ = pad;
                captureTimer_ = 6.0f;
                g_.app.input().beginCapture(pad ? InputDevice::Gamepad : InputDevice::Keyboard);
            }
            if (pad && working_.input.pad[i][slot].kind != PadBinding::Kind::None) {
                Glyphs::drawPadButton(ui, working_.input.pad[i][slot], {b.x + ui.s(8), b.center().y}, ui.s(32));
            }
        }
    }

    void bindingList(const Rect& c, bool pad) {
        UIContext& ui = g_.ui;
        int n = kFirstMenuAction;
        float rh = ui.s(52), gap = ui.s(8);
        Rect list{c.x, c.y, std::min(c.w, ui.s(900)), c.h - ui.s(70)};
        ui.beginScroll(pad ? "padscroll" : "kbscroll", list, n * (rh + gap));
        float y = list.y - ui.scrollOffset();
        for (int i = 0; i < n; ++i) {
            bindingRow({list.x, y, list.w - ui.s(16), rh}, static_cast<Action>(i), pad);
            y += rh + gap;
        }
        ui.endScroll();
        Rect reset{c.x, c.bottom() - ui.s(54), ui.s(360), ui.s(54)};
        UIContext::ButtonStyle d;
        d.danger = true;
        if (ui.button(pad ? "padreset" : "kbreset", reset, "Réinitialiser", d)) {
            if (pad) working_.input.resetGamepad();
            else working_.input.resetKeyboard();
            g_.app.input().setBindings(working_.input);
            flash("Configuration par défaut restaurée");
        }
    }

    void keyboard(const Rect& c) { bindingList(c, false); }

    void gamepad(const Rect& c) {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        float leftW = std::min(c.w * 0.58f, ui.s(900));
        bindingList({c.x, c.y, leftW, c.h}, true);
        // Right column: device + live view + tuning
        float rx = c.x + leftW + ui.s(40), rw = c.right() - rx;
        float y = c.y;
        std::string type = in.gamepadStyle() == GamepadStyle::Xbox ? "Xbox / XInput"
                           : in.gamepadStyle() == GamepadStyle::PlayStation ? "PlayStation"
                                                                            : "Générique (SDL)";
        if (in.hasGamepad()) {
            ui.label(in.gamepadName(), {rx, y}, 24, ui.theme().accent, Align::Left, FontFace::Bold);
            ui.label("Type détecté : " + type, {rx, y + ui.s(32)}, 19, ui.theme().textDim);
        } else {
            ui.label("Aucune manette détectée", {rx, y}, 24, ui.theme().textDim, Align::Left, FontFace::Bold);
            ui.label("Branchez une manette : elle sera reconnue automatiquement.", {rx, y + ui.s(32)}, 19, ui.theme().textDim);
        }
        y += ui.s(76);
        drawPadVisual({rx, y, rw, ui.s(170)});
        y += ui.s(186);
        Rect r{rx, y, rw, ui.s(50)};
        float step = ui.s(58);
        ui.slider("deadzone", r, "Zone morte", working_.input.gamepad.stickDeadzone, 0.0f, 0.6f, 0.02f, "%.2f");
        r.y += step;
        ui.slider("trigger", r, "Seuil gâchettes", working_.input.gamepad.triggerThreshold, 0.1f, 0.9f, 0.05f, "%.2f");
        r.y += step;
        ui.slider("sens", r, "Sensibilité", working_.input.gamepad.sensitivity, 0.5f, 2.5f, 0.1f, "%.1f");
        r.y += step;
        ui.toggle("invx", r, "Inverser l'axe X", working_.input.gamepad.invertX);
        r.y += step;
        ui.toggle("invy", r, "Inverser l'axe Y", working_.input.gamepad.invertY);
        r.y += step;
        ui.toggle("swap", r, "Inverser les joysticks", working_.input.gamepad.swapSticks);
        r.y += step;
        if (ui.toggle("rumble", r, "Vibrations", working_.input.gamepad.rumble) && working_.input.gamepad.rumble) {
            g_.app.input().setBindings(working_.input);
            g_.app.input().rumble(0.5f, 0.5f, 200);
        }
        g_.app.input().setBindings(working_.input);
    }

    void drawPadVisual(const Rect& r) {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        Renderer& rr = ui.r();
        rr.fillRoundedRect(r, ui.s(40), {14, 20, 40, 230});
        rr.drawRoundedRect(r, ui.s(40), ui.theme().border, ui.s(1.5f));
        auto stick = [&](Vec2 c, Vec2 v) {
            float rad = ui.s(34);
            rr.drawRing(c, rad, ui.s(2), {120, 150, 190, 200});
            float dz = working_.input.gamepad.stickDeadzone * rad;
            rr.drawRing(c, dz, ui.s(1), {255, 120, 120, 120});
            rr.fillCircle(c + v * rad, ui.s(12), ui.theme().accent);
        };
        stick({r.x + r.w * 0.22f, r.center().y + ui.s(10)}, in.leftStick());
        stick({r.x + r.w * 0.62f, r.center().y + ui.s(30)}, in.rightStick());
        auto trig = [&](float x, float v, const char* lbl) {
            Rect tr{x, r.y + ui.s(14), ui.s(70), ui.s(14)};
            rr.fillRoundedRect(tr, ui.s(6), {40, 50, 70, 255});
            rr.fillRoundedRect({tr.x, tr.y, tr.w * v, tr.h}, ui.s(6), ui.theme().warm);
            ui.label(lbl, {tr.center().x, tr.bottom() + ui.s(4)}, 14, ui.theme().textDim, Align::Center);
        };
        bool ps = in.gamepadStyle() == GamepadStyle::PlayStation;
        trig(r.x + ui.s(30), in.leftTrigger(), ps ? "L2" : "LT");
        trig(r.right() - ui.s(100), in.rightTrigger(), ps ? "R2" : "RT");
        const char* sheet = Glyphs::sheetFor(in.gamepadStyle());
        Vec2 face{r.right() - r.w * 0.16f, r.center().y};
        const SDL_GamepadButton btns[4] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                                           SDL_GAMEPAD_BUTTON_NORTH};
        const Vec2 offs[4] = {{0, 1}, {1, 0}, {-1, 0}, {0, -1}};
        for (int i = 0; i < 4; ++i) {
            float s = ui.s(34);
            bool down = in.rawPadButton(btns[i]);
            Vec2 p = face + offs[i] * ui.s(30);
            if (down) rr.drawGlow(p, s, ui.theme().accent.withAlpha(150));
            ui.icon(sheet, i, 32, Rect::fromCenter(p, {s, s}), down ? Color::white() : Color{170, 180, 200, 220});
        }
    }

    void gameplay(const Rect& c) {
        UIContext& ui = g_.ui;
        Rect r{c.x, c.y, std::min(c.w, ui.s(900)), ui.s(54)};
        ui.toggle("timer", r, "Afficher le chronomètre", working_.gameplay.showTimer);
        r.y += row();
        ui.toggle("hints", r, "Afficher les touches à l'écran", working_.gameplay.showInputHints);
        r.y += row();
        static const std::vector<std::string> bars = {"Masquée", "En haut de l'écran", "En bas de l'écran"};
        ui.cycle("progress", r, "Barre de progression du niveau", working_.gameplay.progressBar, bars);
        r.y += row();
        ui.toggle("debug", r, "Mode debug (F3 en jeu)", working_.gameplay.debugMode);
        r.y += row();
        ui.toggle("unlockall", r, "Tout débloquer : planètes et niveaux (mode test)", working_.gameplay.unlockAll);
        r.y += row();
        ui.toggle("autosave", r, "Sauvegarde automatique de l'éditeur", working_.gameplay.editorAutosave);
        r.y += row();
        ui.sliderInt("autosavesec", r, "Intervalle de sauvegarde (s)", working_.gameplay.editorAutosaveSeconds, 10, 600);
        r.y += row() + ui.s(20);
        ui.label("Dossier des sauvegardes :", {r.x, r.y}, 20, ui.theme().textDim);
        ui.label(Paths::toUtf8(Paths::userDir()), {r.x, r.y + ui.s(28)}, 18, ui.theme().text);
    }

    void handleCapture() {
        InputManager& in = g_.app.input();
        if (!in.captureDone()) return;
        in.cancelCapture();
        int i = static_cast<int>(captureAction_);
        if (captureAction_ == Action::Count) return;
        if (!capturePad_) {
            SDL_Scancode sc = in.capturedKey();
            if (sc == SDL_SCANCODE_ESCAPE && captureAction_ != Action::Pause) {
                flash("Modification annulée");
            } else if (sc != SDL_SCANCODE_UNKNOWN) {
                for (int a = 0; a < kFirstMenuAction; ++a)
                    for (auto& k : working_.input.keys[a])
                        if (k == sc && !(a == i)) {
                            k = SDL_SCANCODE_UNKNOWN;
                            flash(std::string("Touche retirée de « ") + actionLabel(static_cast<Action>(a)) + " »");
                        }
                working_.input.keys[i][captureSlot_] = sc;
            }
        } else {
            PadBinding b = in.capturedPad();
            for (int a = 0; a < kFirstMenuAction; ++a)
                for (auto& p : working_.input.pad[a])
                    if (p == b && a != i) {
                        p = PadBinding{};
                        flash(std::string("Bouton retiré de « ") + actionLabel(static_cast<Action>(a)) + " »");
                    }
            working_.input.pad[i][captureSlot_] = b;
        }
        g_.app.input().setBindings(working_.input);
        g_.app.input().consumePlayerInput();
        captureAction_ = Action::Count;
        g_.uiSound("ui_confirm");
    }

    void drawCaptureModal() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, {0, 0, 0, 150});
        Rect m = Rect::fromCenter({ui.screenW() * 0.5f, ui.screenH() * 0.5f}, {ui.s(760), ui.s(240)});
        ui.panel(m, true, 16);
        ui.label(std::string("Nouvelle commande : ") + actionLabel(captureAction_), {m.center().x, m.y + ui.s(40)}, 28,
                 ui.theme().text, Align::Center, FontFace::Bold);
        ui.label(capturePad_ ? "Appuyez sur un bouton ou une gâchette de la manette" : "Appuyez sur une touche (Échap : annuler)",
                 {m.center().x, m.y + ui.s(110)}, 22, ui.theme().textDim, Align::Center);
        float k = std::max(0.0f, captureTimer_ / 6.0f);
        ui.r().fillRect({m.x + ui.s(40), m.bottom() - ui.s(50), (m.w - ui.s(80)) * k, ui.s(6)}, ui.theme().accent);
    }

    void flash(const std::string& msg) {
        message_ = msg;
        messageTimer_ = 3.0f;
    }

    GameServices& g_;
    bool overlay_;
    Settings working_;
    MenuBackground bg_;
    int tab_ = 0;
    float t_ = 0.0f;
    Action captureAction_ = Action::Count;
    int captureSlot_ = 0;
    bool capturePad_ = false;
    float captureTimer_ = 0.0f;
    std::string message_;
    float messageTimer_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeSettingsScene(GameServices& g, bool overlay) { return std::make_unique<SettingsScene>(g, overlay); }

}  // namespace cosmic
