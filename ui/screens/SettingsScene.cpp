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
    SettingsScene(GameServices& g, bool overlay, int tab, int player) : g_(g), overlay_(overlay), startTab_(tab), player_(player) {}

    bool isOverlay() const override { return overlay_; }

    void onEnter() override {
        working_ = g_.settings;
        // The window as it is now (full screen by F11, Alt+Entrée or the command line).
        if (!g_.app.headless()) working_.graphics.fullscreen = g_.app.fullscreen();
        // Automated screenshots: --settings-tab <n>, <n>:slots for the list of the controls
        // scrolled to its first note (where the keys of the powers are set), <n>:end to its end.
        if (g_.onlineOpen.rfind("settings-tab:", 0) == 0) {
            tab_ = std::clamp(std::atoi(g_.onlineOpen.c_str() + 13), 0, 4);
            scrollTo_ = g_.onlineOpen.find(":slots") != std::string::npos ? 1 : g_.onlineOpen.find(":end") != std::string::npos ? 2 : 0;
            g_.onlineOpen.clear();
        }
        g_.ui.setFocus("settings/tabs");
        if (startTab_ >= 0) tab_ = std::clamp(startTab_, 0, 4);
        if (!team()) player_ = 0;
        player_ = std::clamp(player_, 0, team() ? static_cast<int>(g_.localTeam.size()) - 1 : 0);
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
        // « NOUVEAU » on the tabs leading to a novelty (data/news/new_features.json): it stays,
        // the tab open or not, until everything new in it has been seen.
        {
            static const char* const kTabFeatures[5] = {"settings.tab.graphics", "settings.tab.audio", "settings.tab.keyboard",
                                                        "settings.tab.gamepad", "settings.tab.game"};
            float tw = tabsR.w / 5.0f;
            for (int i = 0; i < 5; ++i) {
                if (!ui.isNew(kTabFeatures[i])) continue;
                if (tab_ == i && ui.featureSeen) ui.featureSeen(kTabFeatures[i]);  // a novelty of its own goes once opened
                if (ui.isNew(kTabFeatures[i])) ui.drawNewPill(tabsR.x + (i + 1) * tw - ui.s(10), tabsR.y);
            }
        }
        Rect content{panel.x + ui.s(40), tabsR.bottom() + ui.s(24), panel.w - ui.s(80), panel.bottom() - tabsR.bottom() - ui.s(110)};
        ui.pushId("tab" + std::to_string(tab_));
        if (team() && (tab_ == 2 || tab_ == 3)) {
            // 2.65: one PC with a team: whose controls (each player has its own).
            const float barH = drawPlayerBar({content.x, content.y, content.w, ui.s(46)}, tab_ == 3);
            content.y += barH;
            content.h -= barH;
        }
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

    // ------------------------------------------------------------ 2.65: the players of one PC
    bool team() const { return g_.localTeam.size() >= 2; }
    /// The controls shown: the chosen player's (player 1's alone).
    InputBindings& cur() { return working_.playerInput(player_); }
    /// Player 1's controls drive the menus and the solo game: given to the input at once.
    void apply() {
        if (player_ == 0) g_.app.input().setBindings(working_.input);
    }
    /// What is held now: the keyboard and the mouse, and the pad of the player shown (alone:
    /// the pad in use).
    HeldInputs heldNow() {
        InputManager& in = g_.app.input();
        if (!team() || !g_.localTeam[static_cast<size_t>(player_)].device.isPad()) return in.heldInputs();
        const PadEntry* p = in.devices().byNumber(g_.localTeam[static_cast<size_t>(player_)].device.pad);
        return in.heldInputs(p && p->connected ? p->id : 0, cur().gamepad.triggerThreshold);
    }
    GamepadStyle padStyle() const { return team() ? g_.playerPadStyle(player_) : g_.app.input().gamepadStyle(); }

    /// « J1 · Nova », « J2 · Lyra »…: whose controls; « Copier les touches de Jn » (this tab's
    /// device: the keys, or the buttons and the gamepad settings). Returns the height taken.
    float drawPlayerBar(const Rect& r, bool pad) {
        UIContext& ui = g_.ui;
        const int n = static_cast<int>(g_.localTeam.size());
        std::vector<std::string> labels;
        for (int i = 0; i < n; ++i) labels.push_back("J" + std::to_string(i + 1) + " · " + g_.localTeam[static_cast<size_t>(i)].name);
        for (int i = 0; i < n; ++i)
            if (i != player_) labels.push_back("Copier les touches de J" + std::to_string(i + 1));
        const float gap = ui.s(8);
        const float bw = (r.w - gap * static_cast<float>(labels.size() - 1)) / static_cast<float>(labels.size());
        float fs = 18.0f;
        for (const auto& l : labels) fs = std::min(fs, ui.fitFont(l, FontFace::Bold, 18, bw - ui.s(16), 0.55f));
        ui.pushId("players");
        for (int k = 0; k < static_cast<int>(labels.size()); ++k) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = fs;
            st.selected = k == player_;
            const Rect b{r.x + static_cast<float>(k) * (bw + gap), r.y, bw, r.h};
            if (!ui.button("p" + std::to_string(k), b, labels[static_cast<size_t>(k)], st)) continue;
            if (k < n) {
                player_ = k;
                continue;
            }
            // Copy: the other players in order, skipping this one.
            int from = k - n;
            if (from >= player_) ++from;
            const InputBindings& src = working_.playerInput(from);
            if (pad) {
                cur().pad = src.pad;
                cur().gamepad = src.gamepad;
            } else {
                cur().keys = src.keys;
            }
            apply();
            flash("Touches de J" + std::to_string(from + 1) + " copiées pour J" + std::to_string(player_ + 1));
            g_.uiSound("ui_confirm");
        }
        ui.newBadge("p0", {r.x, r.y, bw, r.h}, pad ? "settings.players.gamepad" : "settings.players.keyboard");
        ui.popId();
        return r.h + ui.s(14);
    }

    void graphics(const Rect& c) {
        UIContext& ui = g_.ui;
        float y = c.y, w = std::min(c.w, ui.s(900));
        Rect r{c.x, y, w, ui.s(54)};
        auto next = [&]() { r.y += row(); };
        if (ui.toggle("fullscreen", r, "Plein écran sans bordure (F11)", working_.graphics.fullscreen)) {
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
        // The name never reaches the buttons (long names are drawn smaller).
        const std::string name = actionLabel(a);
        const float nameRoom = r.w * 0.42f - ui.s(20);
        ui.label(name, {r.x + ui.s(10), r.center().y}, ui.fitFont(name, FontFace::Body, 22, nameRoom), ui.theme().text, Align::Left,
                 FontFace::Body, VAlign::Middle);
        // One key / one button per command (2.29): a single column.
        for (int slot = 0; slot < 1; ++slot) {
            Rect b{r.x + r.w * 0.42f, r.y, r.w * 0.38f, r.h};
            std::string label;
            const PadBinding& pb = cur().pad[i][slot];
            // A gamepad button with its icon (drawn on the left): the name stays clear of it.
            const bool icon = pad && pb.kind != PadBinding::Kind::None && glyphIndex(pb) >= 0;
            if (pad) label = padBindingName(pb, padStyle());
            else {
                // A mouse binding says which button it is when there is room (« Souris 4 · bouton
                // latéral arrière »), else its short name, the one the HUD shows.
                const SDL_Scancode sc = cur().keys[i][slot];
                label = keyName(sc);
                if (std::string detail = mouseBindingDetail(sc); !detail.empty()) {
                    std::string full = label + " · " + detail;
                    if (ui.text().measure(full, FontFace::Bold, ui.s(20)).x <= b.w - ui.s(24)) label = full;
                }
            }
            std::string id = std::string(pad ? "p" : "k") + actionId(a) + std::to_string(slot);
            UIContext::ButtonStyle st;
            st.small = true;
            // The key / button held right now is lit (a check, outside any rebinding).
            {
                const InputManager& in = g_.app.input();
                const HeldInputs held = heldNow();
                st.lit = !in.capturing() && (pad ? padBindingHeld(pb, held) : keyBindingHeld(cur().keys[i][slot], held));
            }
            float labelRoom = b.w - ui.s(16) - (icon ? 2.0f * (ui.s(8) + ui.s(32) + ui.s(4)) : 0.0f);
            st.fontSize = ui.fitFont(label, FontFace::Bold, 20, std::max(ui.s(30), labelRoom));
            if (ui.button(id, b, label, st)) {
                captureAction_ = a;
                captureSlot_ = slot;
                capturePad_ = pad;
                captureTimer_ = 6.0f;
                g_.app.input().beginCapture(pad ? InputDevice::Gamepad : InputDevice::Keyboard);
            }
            if (icon) Glyphs::drawPadButton(ui, pb, {b.x + ui.s(8), b.center().y}, ui.s(32), padStyle());
        }
        // « Par défaut »: this command alone back to its key / button of origin (active only when
        // it was changed). A command that used that key loses it, as when a key is chosen.
        {
            const InputBindings d = InputBindings::defaults();
            const bool same = pad ? cur().pad[i] == d.pad[i] : cur().keys[i] == d.keys[i];
            Rect rb{r.x + r.w * 0.82f, r.y, r.w * 0.18f, r.h};
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !same;
            const std::string label = "Par défaut";
            st.fontSize = ui.fitFont(label, FontFace::Bold, 16, std::max(ui.s(30), rb.w - ui.s(14)), 0.6f);
            const std::string resetId = std::string(pad ? "pr" : "kr") + actionId(a);
            const bool resetClicked = ui.button(resetId, rb, label, st);
            // « NOUVEAU » (2.29.1): seen as soon as any « Par défaut » gets the focus or a click (a
            // disabled one too); the pill itself only on the first row, not on every line.
            ui.newBadge(resetId, rb, pad ? "settings.reset_one.gamepad" : "settings.reset_one.keyboard", a == Action::MoveLeft, true);
            if (resetClicked && !same) {
                if (pad) {
                    const PadBinding def = d.pad[i][0];
                    for (int o = 0; o < kFirstMenuAction; ++o)
                        for (auto& p : cur().pad[o])
                            if (o != i && def.kind != PadBinding::Kind::None && p == def) {
                                p = PadBinding{};
                                flash(std::string("Bouton retiré de « ") + actionLabel(static_cast<Action>(o)) + " »");
                            }
                    cur().pad[i] = d.pad[i];
                } else {
                    const SDL_Scancode def = d.keys[i][0];
                    for (int o = 0; o < kFirstMenuAction; ++o)
                        for (auto& k : cur().keys[o])
                            if (o != i && def != SDL_SCANCODE_UNKNOWN && k == def) {
                                k = SDL_SCANCODE_UNKNOWN;
                                flash(std::string("Touche retirée de « ") + actionLabel(static_cast<Action>(o)) + " »");
                            }
                    cur().keys[i] = d.keys[i];
                }
                cur().keepOneBinding();
                apply();
                g_.uiSound("ui_confirm");
            }
        }
    }

    /// A line of the binding list: an action, or a note.
    struct ListItem {
        Action action = Action::Count;  // Count: a text
        std::string title;              // bold, accent (may be empty)
        std::string text;               // dim, wrapped
        float h = 0.0f;
    };

    std::vector<ListItem> bindingItems(float width) {
        UIContext& ui = g_.ui;
        std::vector<ListItem> items;
        auto text = [&](std::string title, std::string body) {
            ListItem it;
            it.title = std::move(title);
            it.text = std::move(body);
            float h = ui.s(10);
            if (!it.title.empty()) h += ui.text().measure(it.title, FontFace::Bold, ui.s(20)).y + ui.s(4);
            if (!it.text.empty()) h += ui.text().measureWrapped(it.text, FontFace::Body, ui.s(16), width).y;
            it.h = h + ui.s(10);
            items.push_back(std::move(it));
        };
        for (int i = 0; i < kFirstMenuAction; ++i) {
            Action a = static_cast<Action>(i);
            if (!actionIsRebindable(a)) continue;  // the power buttons before 2.24, the power slots
            if (a == Action::Jump)
                text("", "Les flèches du clavier et la croix de la manette déplacent aussi Nova, en plus des commandes choisies ci-dessus.");
            if (a == Action::Pause)
                text("Pouvoirs",
                     "Les touches des pouvoirs se règlent pour chaque niveau : éditeur, choix du niveau › Touches…, pause › Touches de ce niveau.");
            if (a == Action::Pause) text("Partie", "");
            ListItem it;
            it.action = a;
            it.h = ui.s(52);
            items.push_back(std::move(it));
            // 2.69: the attack upwards and downwards (no key of its own: a direction held).
            if (a == Action::Attack)
                text("", "Attaque vers le haut : Haut + Attaque, au sol ou en l'air. Attaque vers le bas : Bas + Attaque, en l'air "
                         "seulement (au sol, Bas reste « s'accroupir » ; avec la Frappe sismique, Bas + Attaque en l'air reste la frappe).");
        }
        return items;
    }

    void bindingList(const Rect& c, bool pad) {
        UIContext& ui = g_.ui;
        float gap = ui.s(8);
        Rect list{c.x, c.y, std::min(c.w, ui.s(900)), c.h - ui.s(70)};
        const float rowW = list.w - ui.s(16);
        auto items = bindingItems(rowW - ui.s(20));
        float total = 0.0f, slotsAt = 0.0f;
        for (const auto& it : items) {
            if (it.action == Action::Count && !it.title.empty() && slotsAt == 0.0f) slotsAt = total;
            total += it.h + gap;
        }
        if (scrollTo_ != 0) {
            ui.setScroll(pad ? "padscroll" : "kbscroll", scrollTo_ == 1 ? slotsAt : total);
            scrollTo_ = 0;
        }
        ui.beginScroll(pad ? "padscroll" : "kbscroll", list, total);
        float y = list.y - ui.scrollOffset();
        for (const auto& it : items) {
            if (it.action != Action::Count) {
                bindingRow({list.x, y, rowW, it.h}, it.action, pad);
            } else {
                float ty = y + ui.s(10);
                if (!it.title.empty()) {
                    Vec2 sz = ui.label(it.title, {list.x + ui.s(10), ty}, 20, ui.theme().accent, Align::Left, FontFace::Bold);
                    ty += sz.y + ui.s(4);
                }
                if (!it.text.empty()) ui.labelWrapped(it.text, {list.x + ui.s(10), ty}, rowW - ui.s(20), 16, ui.theme().textDim);
            }
            y += it.h + gap;
        }
        ui.endScroll();
        Rect reset{c.x, c.bottom() - ui.s(54), ui.s(360), ui.s(54)};
        UIContext::ButtonStyle d;
        d.danger = true;
        if (ui.button(pad ? "padreset" : "kbreset", reset, "Réinitialiser", d)) {
            if (pad) cur().resetGamepad();
            else cur().resetKeyboard();
            apply();
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
        const GamepadStyle shown = padStyle();
        std::string type = shown == GamepadStyle::Xbox          ? "Xbox / XInput"
                           : shown == GamepadStyle::PlayStation ? "PlayStation"
                           : shown == GamepadStyle::Switch      ? "Switch"
                                                                : "Générique (SDL)";
        // 2.65: a team on this PC: the pad of the player shown (its number), else the one in use.
        const PadEntry* mine = nullptr;
        if (team() && g_.localTeam[static_cast<size_t>(player_)].device.isPad())
            mine = in.devices().byNumber(g_.localTeam[static_cast<size_t>(player_)].device.pad);
        if (team() && !mine) {
            const std::string who = g_.localTeam[static_cast<size_t>(player_)].device.isKeyboard() ? "J" + std::to_string(player_ + 1) + " joue au clavier"
                                                                                                   : "J" + std::to_string(player_ + 1) + " n'a pas de manette";
            const float wf = ui.fitFont(who, FontFace::Bold, 24, rw, 0.6f);
            ui.label(ui.elide(who, FontFace::Bold, wf, rw), {rx, y}, wf, ui.theme().textDim, Align::Left, FontFace::Bold);
            ui.label(ui.elide("Ses boutons servent dès qu'il prend une manette.", FontFace::Body, 19, rw), {rx, y + ui.s(32)}, 19, ui.theme().textDim);
        } else if (mine) {
            ui.label(ui.elide(padLabel(*mine), FontFace::Bold, 24, rw), {rx, y}, 24, ui.theme().accent, Align::Left, FontFace::Bold);
            ui.label(ui.elide("Type détecté : " + type, FontFace::Body, 19, rw), {rx, y + ui.s(32)}, 19, ui.theme().textDim);
        } else if (in.hasGamepad()) {
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
        ui.slider("deadzone", r, "Zone morte", cur().gamepad.stickDeadzone, 0.0f, 0.6f, 0.02f, "%.2f");
        r.y += step;
        ui.slider("trigger", r, "Seuil gâchettes", cur().gamepad.triggerThreshold, 0.1f, 0.9f, 0.05f, "%.2f");
        r.y += step;
        ui.slider("sens", r, "Sensibilité", cur().gamepad.sensitivity, 0.5f, 2.5f, 0.1f, "%.1f");
        r.y += step;
        ui.toggle("invx", r, "Inverser l'axe X", cur().gamepad.invertX);
        r.y += step;
        ui.toggle("invy", r, "Inverser l'axe Y", cur().gamepad.invertY);
        r.y += step;
        ui.toggle("swap", r, "Inverser les joysticks", cur().gamepad.swapSticks);
        r.y += step;
        if (ui.toggle("rumble", r, "Vibrations", cur().gamepad.rumble) && cur().gamepad.rumble && player_ == 0) {
            g_.app.input().setBindings(working_.input);
            g_.app.input().rumble(0.5f, 0.5f, 200);
        }
        apply();
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
            float dz = cur().gamepad.stickDeadzone * rad;
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
        // Twelve rows, then where the saves are: they scroll when the window is too short for them.
        const float total = row() * 12.0f + ui.s(20) + ui.s(28) + ui.s(28);
        const bool scrolls = total > c.h;
        ui.beginScroll("gamescroll", c, total);
        Rect r{c.x, c.y - ui.scrollOffset(), std::min(c.w - (scrolls ? ui.s(16) : 0.0f), ui.s(900)), ui.s(54)};
        // Language of every text, applied at once (the texts are translated when drawn).
        // The caption and the names stay in both languages, so that anyone finds them.
        int lang = settingsLanguage(working_) == Language::English ? 1 : 0;
        if (ui.cycle("language", r, "Langue / Language", lang,
                     {i18n::nativeName(Language::French), i18n::nativeName(Language::English)})) {
            const Language l = lang == 1 ? Language::English : Language::French;
            working_.language = i18n::code(l);
            i18n::setLanguage(l);
            flash(l == Language::English ? "Langue : anglais, appliquée immédiatement." : "Langue : français, appliquée immédiatement.");
        }
        r.y += row();
        ui.toggle("timer", r, "Afficher le chronomètre", working_.gameplay.showTimer);
        r.y += row();
        ui.toggle("hints", r, "Afficher les touches à l'écran", working_.gameplay.showInputHints);
        r.y += row();
        // 2.52: an arrow on the border towards the enemies already met that are out of view.
        ui.toggle("enemyarrows", r, "Flèches vers les ennemis hors écran", working_.gameplay.enemyArrows);
        ui.newBadge("enemyarrows", r, "settings.game.enemy_arrows", true, true);
        r.y += row();
        std::vector<std::string> bars = {"Masquée", "En haut de l'écran", "En bas de l'écran"};
        if (working_.gameplay.hud[HudElement::Progress].custom) bars.push_back("Placée dans l'éditeur du HUD");
        ui.cycle("progress", r, "Barre de progression du niveau", working_.gameplay.progressBar, bars);
        r.y += row();
        // 2.51: the story told before the first fight against each giga boss.
        int story = std::clamp(working_.gameplay.bossStories, 0, 1);
        if (ui.cycle("bossstories", r, "Histoire des giga-boss", story, {"Avant le premier combat", "Jamais"}))
            working_.gameplay.bossStories = story;
        ui.newBadge("bossstories", r, "settings.boss_stories", true, true);
        r.y += row();
        // 2.63: the trailer on the main menu after a minute without any input.
        ui.toggle("attract", r, "Bande-annonce à l'accueil après 1 min d'inactivité", working_.gameplay.attractTrailer);
        ui.newBadge("attract", r, "settings.game.attract_trailer", true, true);
        r.y += row();
        bool openHud = ui.button("hudeditor", r, "Personnaliser le HUD (place et taille de chaque élément)");
        ui.newBadge("hudeditor", r, "settings.hud_editor", true, true);  // « NOUVEAU » (2.52: « Ennemis vaincus »)
        if (openHud) g_.app.pushScene(makeHudEditor(g_, working_.gameplay));
        r.y += row();
        // 2.60: a block of the HUD fades while the boss, Nova or an attack of the boss is under it.
        ui.toggle("hudfade", r, "Effacer le HUD quand il cache l'action", working_.gameplay.fadeHud);
        ui.newBadge("hudfade", r, "settings.game.hud_fade", true, true);
        r.y += row();
        ui.toggle("debug", r, "Mode debug (F3 en jeu)", working_.gameplay.debugMode);
        r.y += row();
        ui.toggle("unlockall", r, "Tout débloquer : planètes et niveaux (mode test)", working_.gameplay.unlockAll);
        r.y += row();
        ui.toggle("autosave", r, "Sauvegarde automatique de l'éditeur", working_.gameplay.editorAutosave);
        r.y += row();
        ui.sliderInt("autosavesec", r, "Intervalle de sauvegarde (s)", working_.gameplay.editorAutosaveSeconds, 10, 600);
        // 2.54: the player's statistics, in the column at the right (the left one is full).
        const float lane = scrolls ? ui.s(16) : 0.0f;
        if (c.w - lane - r.w - ui.s(40) >= ui.s(360)) {
            Rect sr{r.right() + ui.s(40), c.y - ui.scrollOffset(), c.w - lane - r.w - ui.s(40), ui.s(54)};
            ui.toggle("recordstats", sr, "Enregistrer mes statistiques", working_.gameplay.recordStats);
            ui.newBadge("recordstats", sr, "settings.record_stats");
            const std::string about = "Menu principal › Statistiques : calculées à partir de vos parties, gardées avec vos sauvegardes et, avec un compte, "
                                      "dans la copie privée de votre progression.";
            ui.labelWrapped(about, {sr.x + ui.s(4), sr.bottom() + ui.s(10)}, sr.w - ui.s(8), 17, ui.theme().textDim);
        }
        r.y += row() + ui.s(10);  // eleven rows (2.60): the folder stays above the « Retour » button
        ui.label("Dossier des sauvegardes :", {r.x, r.y}, 20, ui.theme().textDim);
        const std::string dir = Paths::toUtf8(Paths::userDir());
        {
            i18n::Verbatim asIs;  // a path, never translated
            ui.label(ui.elide(dir, FontFace::Body, 18, r.w), {r.x, r.y + ui.s(28)}, 18, ui.theme().text);
        }
        ui.endScroll();
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
                    for (auto& k : cur().keys[a])
                        if (k == sc && !(a == i)) {
                            k = SDL_SCANCODE_UNKNOWN;
                            flash(std::string("Touche retirée de « ") + actionLabel(static_cast<Action>(a)) + " »");
                        }
                cur().keys[i][captureSlot_] = sc;
            }
        } else {
            PadBinding b = in.capturedPad();
            for (int a = 0; a < kFirstMenuAction; ++a)
                for (auto& p : cur().pad[a])
                    if (p == b && a != i) {
                        p = PadBinding{};
                        flash(std::string("Bouton retiré de « ") + actionLabel(static_cast<Action>(a)) + " »");
                    }
            cur().pad[i][captureSlot_] = b;
        }
        cur().keepOneBinding();  // one key per command, the arrows / d-pad still move
        apply();
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
        // Keyboard / mouse: any key, any mouse button (side buttons and the extra buttons of
        // gaming mice included) or a notch of the wheel.
        const std::string ask = capturePad_ ? "Appuyez sur un bouton ou une gâchette de la manette"
                                            : "Appuyez sur une touche ou un bouton de la souris, ou tournez la molette (Échap : annuler)";
        const float askW = m.w - ui.s(60);
        ui.labelWrapped(ask, {m.x + ui.s(30), m.y + ui.s(84)}, askW, ui.fitFont(ask, FontFace::Body, 22, askW * 2.0f, 0.75f), ui.theme().textDim);
        float k = std::max(0.0f, captureTimer_ / 6.0f);
        ui.r().fillRect({m.x + ui.s(40), m.bottom() - ui.s(50), (m.w - ui.s(80)) * k, ui.s(6)}, ui.theme().accent);
    }

    void flash(const std::string& msg) {
        message_ = msg;
        messageTimer_ = 3.0f;
    }

    GameServices& g_;
    bool overlay_;
    int startTab_ = -1;
    int player_ = 0;  // 2.65: whose controls (a team on this PC)
    Settings working_;
    MenuBackground bg_;
    int tab_ = 0;
    int scrollTo_ = 0;  // automated screenshots: 1 the power slots, 2 the end
    float t_ = 0.0f;
    Action captureAction_ = Action::Count;
    int captureSlot_ = 0;
    bool capturePad_ = false;
    float captureTimer_ = 0.0f;
    std::string message_;
    float messageTimer_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeSettingsScene(GameServices& g, bool overlay, int tab, int player) {
    return std::make_unique<SettingsScene>(g, overlay, tab, player);
}

}  // namespace cosmic
