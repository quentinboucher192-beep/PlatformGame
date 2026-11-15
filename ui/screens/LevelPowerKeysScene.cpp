// « Touches de ce niveau » (2.31): the keys of the level's powers for this player.
//
// Each power used with a key of its own keeps that key wherever it sits in the HUD. The level
// chose it (editor, « Touches des pouvoirs »), or it is the power's suggestion; the player may
// change it for this level only (saves/power_keys.json) and go back with « Par défaut ».

#include "ui/screens/LevelPowerKeysScene.h"

#include <algorithm>

#include "game/GameServices.h"
#include "game/powers/Power.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

PowerKeyMap playerPowerKeys(GameServices& g, const LevelData& level, const std::vector<std::string>& extra) {
    return levelPowerKeys(level, g.content, g.settings.input, &g.powerKeyOverrides, extra);
}

ResolvedPowerKey playerPowerKey(GameServices& g, const LevelData& level, const std::string& power, const std::vector<std::string>& extra) {
    return levelPowerKey(power, level, g.content, g.settings.input, &g.powerKeyOverrides, extra);
}

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

class LevelPowerKeysScene : public Scene {
public:
    LevelPowerKeysScene(GameServices& g, LevelData level, std::vector<std::string> extra)
        : g_(g), level_(std::move(level)), extra_(std::move(extra)) {}
    bool isOverlay() const override { return true; }

    void onEnter() override {
        refresh();
        if (!keys_.powers.empty()) g_.ui.setFocus("lpk/k" + keys_.powers.front().power);
        else g_.ui.setFocus("lpk/back");
        // Automated screenshots: --online-open powerkeys[:<power>=<key>,…] (keys set first, as
        // the player would).
        if (g_.onlineOpen.rfind("powerkeys", 0) == 0) {
            std::string spec = g_.onlineOpen.size() > 10 ? g_.onlineOpen.substr(10) : std::string{};
            g_.onlineOpen.clear();
            while (!spec.empty()) {
                size_t comma = spec.find(',');
                std::string one = spec.substr(0, comma);
                spec = comma == std::string::npos ? std::string{} : spec.substr(comma + 1);
                size_t eq = one.find('=');
                if (eq == std::string::npos) continue;
                SDL_Scancode sc = bindingFromName(one.substr(eq + 1));
                if (sc != SDL_SCANCODE_UNKNOWN) setKey(one.substr(0, eq), sc);
            }
        }
    }

    void onExit() override { g_.app.input().cancelCapture(); }

    void update(float dt) override {
        t_ += dt;
        if (captureTimer_ > 0.0f) {
            captureTimer_ -= dt;
            if (captureTimer_ <= 0.0f) g_.app.input().cancelCapture();
        }
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        InputManager& in = g_.app.input();
        handleCapture();
        const bool capturing = in.capturing();
        ui.inputEnabled = !capturing;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{3, 5, 12, 235});
        const UITheme& th = ui.theme();

        // Sizes first: the panel is as tall as what it holds (the rows scroll beyond).
        const float W = std::min(ui.s(1320), ui.screenW() - ui.s(80));
        const float pad = ui.s(34), innerW = W - 2.0f * pad;
        const std::string intro =
            "Chaque pouvoir garde sa touche dans tout le niveau, quel que soit son emplacement. Vos choix ne valent que pour vous et pour ce "
            "niveau ; « Par défaut » reprend la touche du niveau.";
        const float introH = ui.text().measureWrapped(intro, FontFace::Body, ui.s(fz(ui, 17)), innerW).y;
        const float headH = ui.s(26) + ui.s(48) + ui.s(30) + introH + ui.s(18) + ui.s(30);
        const float footH = ui.s(96);
        const float rowsW = innerW - ui.s(14);  // the scroll bar
        float rowsH = 0.0f;
        std::vector<float> heights;
        for (const auto& r : keys_.powers) {
            heights.push_back(rowHeight(r, rowsW));
            rowsH += heights.back() + ui.s(8);
        }
        if (keys_.powers.empty()) rowsH = ui.s(60);
        const float H = std::min(ui.screenH() - ui.s(60), headH + rowsH + footH);
        Rect panel{(ui.screenW() - W) * 0.5f, (ui.screenH() - H) * 0.5f, W, H};
        ui.panel(panel, true, 18);
        ui.pushId("lpk");
        const float x = panel.x + pad;
        float y = panel.y + ui.s(26);
        const std::string title = "TOUCHES DE CE NIVEAU";
        ui.label(title, {x, y}, ui.fitFont(title, FontFace::Title, 38, innerW, 0.6f), th.text, Align::Left, FontFace::Title);
        y += ui.s(48);
        const std::string name = level_.name.empty() ? level_.id : level_.name;
        ui.label(ui.elide(name, FontFace::Bold, fz(ui, 19), innerW), {x, y}, fz(ui, 19), th.accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        ui.labelWrapped(intro, {x, y}, innerW, fz(ui, 17), th.textDim);
        y += introH + ui.s(18);
        // Column titles.
        const Cols c = cols(x, rowsW);
        auto head = [&](const std::string& t, float cx, float cw) {
            float f = std::max(ui.fitFont(t, FontFace::Bold, fz(ui, 16), cw, 0.7f), fz(ui, 11.5f));
            ui.label(ui.elide(t, FontFace::Bold, f, cw), {cx, y + ui.s(12)}, f, th.textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        };
        head("Pouvoir", c.nameX, c.nameW);
        head("Clavier / souris", c.keyX, c.keyW);
        head("Manette", c.padX, c.padW);
        y += ui.s(30);

        Rect area{x, y, innerW, panel.bottom() - footH - y};
        ui.beginScroll("rows", area, rowsH);
        float ry = area.y - ui.scrollOffset();
        if (keys_.powers.empty())
            ui.labelWrapped("Aucun pouvoir de ce niveau ne s'utilise avec une touche à lui.", {x, ry + ui.s(10)}, innerW, fz(ui, 18), th.textDim);
        for (size_t i = 0; i < keys_.powers.size(); ++i) {
            drawRow(keys_.powers[i], {x, ry, rowsW, heights[i]}, c);
            ry += heights[i] + ui.s(8);
        }
        ui.endScroll();

        // Footer: what just happened (or the hints), Retour.
        const float by = panel.bottom() - footH + ui.s(22);
        UIContext::ButtonStyle back;
        back.primary = true;
        Rect backR{panel.right() - pad - ui.s(240), by, ui.s(240), ui.s(54)};
        const bool leave = !capturing && ui.button("back", backR, "Retour", back);
        const float room = backR.x - ui.s(24) - x;
        if (msgTimer_ > 0.0f) {
            float f = std::max(ui.fitFont(msg_, FontFace::Body, fz(ui, 17), room * 2.0f, 0.8f), fz(ui, 11.5f));
            ui.labelWrapped(msg_, {x, by + ui.s(4)}, room, f, msgWarn_ ? th.warm : th.success);
        } else {
            float hx = x;
            hx += Glyphs::drawHint(ui, Action::MenuConfirm, "Changer", {hx, backR.center().y}, ui.s(30)) + ui.s(36);
            Glyphs::drawHint(ui, Action::MenuBack, "Retour", {hx, backR.center().y}, ui.s(30));
        }
        ui.popId();
        ui.inputEnabled = true;
        if (capturing) drawCaptureModal();
        if (!capturing && (leave || ui.backPressed())) g_.app.popScene();
        ui.end();
    }

private:
    struct Cols {
        float nameX, nameW, keyX, keyW, padX, padW, resetX, resetW;
    };

    Cols cols(float x, float w) const {
        const UIContext& ui = g_.ui;
        const float gap = ui.s(16);
        Cols c{};
        c.nameX = x + ui.s(12);
        c.nameW = w * 0.30f - ui.s(12);
        c.keyX = x + w * 0.30f + gap;
        c.keyW = w * 0.25f - gap;
        c.padX = c.keyX + c.keyW + gap;
        c.padW = w * 0.25f - gap;
        c.resetX = c.padX + c.padW + gap;
        c.resetW = x + w - ui.s(12) - c.resetX;
        return c;
    }

    std::string powerName(const std::string& id) const {
        const PowerDefinition* d = g_.content.power(id);
        return d ? d->name : id;
    }

    /// What is said under a row: why a power has no key or another key than asked.
    std::vector<std::string> warnings(const ResolvedPowerKey& r) const {
        std::vector<std::string> out;
        const GamepadStyle style = g_.app.input().gamepadStyle();
        if (!r.keyCommand.empty())
            out.push_back("Clavier : « " + keyName(r.wantedKey) + " » est la touche de « " + r.keyCommand +
                          " » : la commande l'emporte, ce pouvoir n'a pas de touche.");
        else if (!r.keyPower.empty())
            out.push_back("Clavier : « " + keyName(r.wantedKey) + " » est déjà prise par « " + powerName(r.keyPower) + " » : « " + keyName(r.key) +
                          " » à la place.");
        if (!r.padCommand.empty())
            out.push_back("Manette : « " + padBindingName(r.wantedPad, style) + " » est le bouton de « " + r.padCommand +
                          " » : la commande l'emporte, ce pouvoir n'a pas de bouton.");
        else if (!r.padPower.empty())
            out.push_back("Manette : « " + padBindingName(r.wantedPad, style) + " » est déjà pris par « " + powerName(r.padPower) + " » : « " +
                          padBindingName(r.pad, style) + " » à la place.");
        return out;
    }

    float rowHeight(const ResolvedPowerKey& r, float w) const {
        UIContext& ui = g_.ui;
        float h = ui.s(88);
        for (const auto& t : warnings(r)) h += ui.text().measureWrapped(t, FontFace::Body, ui.s(fz(ui, 15)), w - ui.s(24)).y + ui.s(4);
        return h + ui.s(6);
    }

    void drawRow(const ResolvedPowerKey& r, const Rect& row, const Cols& c) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const PowerDefinition* def = g_.content.power(r.power);
        const bool mine = g_.powerKeyOverrides.has(level_.id, r.power);
        ui.r().fillRoundedRect(row, ui.s(10), Color{14, 20, 40, 200});
        if (r.keyConflict() || r.padConflict()) ui.r().drawRoundedRect(row, ui.s(10), th.warm.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
        // Icon and name; under the name, whose keys these are.
        Rect icon{c.nameX, row.y + ui.s(16), ui.s(48), ui.s(48)};
        ui.r().fillRoundedRect(icon, ui.s(8), {7, 11, 24, 255});
        if (def) {
            ui.r().drawRoundedRect(icon, ui.s(8), def->color, std::max(1.0f, ui.s(1.5f)));
            ui.icon("ui/icons_powers.png", def->icon, 32, icon.expanded(-ui.s(6)));
        }
        const float tx = icon.right() + ui.s(12), troom = c.nameX + c.nameW - tx;
        const std::string name = powerName(r.power);
        float nf = std::max(ui.fitFont(name, FontFace::Bold, fz(ui, 20), troom, 0.7f), fz(ui, 11.5f));
        ui.label(ui.elide(name, FontFace::Bold, nf, troom), {tx, row.y + ui.s(30)}, nf, def ? def->color : th.text, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        const bool suggested = r.levelKeySource != PowerKeySource::Level && r.levelPadSource != PowerKeySource::Level;
        const std::string whose = mine ? "Vos touches" : suggested ? "Touches suggérées" : "Touches du niveau";
        float wf = std::max(ui.fitFont(whose, FontFace::Body, fz(ui, 15), troom, 0.75f), fz(ui, 11.5f));
        ui.label(ui.elide(whose, FontFace::Body, wf, troom), {tx, row.y + ui.s(58)}, wf, mine ? th.accent : th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);

        const GamepadStyle style = g_.app.input().gamepadStyle();
        const float by = row.y + ui.s(12), bh = ui.s(46);
        // Keyboard / mouse: the key used, the level's below.
        {
            Rect b{c.keyX, by, c.keyW, bh};
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = r.keySource == PowerKeySource::Player;
            std::string label = keyName(r.key);
            if (std::string detail = mouseBindingDetail(r.key); !detail.empty()) {
                std::string full = label + " · " + detail;
                if (ui.text().measure(full, FontFace::Bold, ui.s(18)).x <= b.w - ui.s(20)) label = full;
            }
            st.fontSize = std::max(ui.fitFont(label, FontFace::Bold, 20, b.w - ui.s(20), 0.6f), fz(ui, 11.5f));
            if (ui.button("k" + r.power, b, label, st)) beginCapture(r.power, false);
            const std::string lv = r.levelKeySource == PowerKeySource::Level ? "Niveau : " + keyName(r.levelKey) : "Suggérée : " + keyName(r.levelKey);
            float lf = std::max(ui.fitFont(lv, FontFace::Body, fz(ui, 14), b.w, 0.8f), fz(ui, 11.5f));
            ui.label(ui.elide(lv, FontFace::Body, lf, b.w), {b.x, b.bottom() + ui.s(14)}, lf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        // Gamepad: the button used (its icon on the left), the level's below.
        {
            Rect b{c.padX, by, c.padW, bh};
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = r.padSource == PowerKeySource::Player;
            const bool icon = r.pad.kind != PadBinding::Kind::None && glyphIndex(r.pad) >= 0;
            const std::string label = padBindingName(r.pad, style);
            const float room = b.w - ui.s(20) - (icon ? 2.0f * (ui.s(8) + ui.s(30) + ui.s(4)) : 0.0f);
            st.fontSize = std::max(ui.fitFont(label, FontFace::Bold, 20, std::max(ui.s(24), room), 0.6f), fz(ui, 11.5f));
            if (ui.button("p" + r.power, b, label, st)) beginCapture(r.power, true);
            if (icon) Glyphs::drawPadButton(ui, r.pad, {b.x + ui.s(8), b.center().y}, ui.s(30));
            const std::string lv =
                r.levelPadSource == PowerKeySource::Level ? "Niveau : " + padBindingName(r.levelPad, style) : "Suggéré : " + padBindingName(r.levelPad, style);
            float lf = std::max(ui.fitFont(lv, FontFace::Body, fz(ui, 14), b.w, 0.8f), fz(ui, 11.5f));
            ui.label(ui.elide(lv, FontFace::Body, lf, b.w), {b.x, b.bottom() + ui.s(14)}, lf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        // « Par défaut »: the level's key and button again (only when the player changed them).
        {
            Rect b{c.resetX, by, c.resetW, bh};
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = mine;
            const std::string label = "Par défaut";
            st.fontSize = std::max(ui.fitFont(label, FontFace::Bold, 17, b.w - ui.s(14), 0.6f), fz(ui, 11.5f));
            if (ui.button("r" + r.power, b, label, st) && mine) {
                g_.powerKeyOverrides.reset(level_.id, r.power);
                g_.powerKeyOverrides.save();
                refresh();
                say("« " + powerName(r.power) + " » reprend les touches du niveau.", false);
                g_.uiSound("ui_confirm");
            }
        }
        // Why a key is not the one asked.
        float wy = row.y + ui.s(88);
        for (const auto& t : warnings(r)) {
            Vec2 sz = ui.labelWrapped(t, {row.x + ui.s(12), wy}, row.w - ui.s(24), fz(ui, 15), th.warm);
            wy += sz.y + ui.s(4);
        }
    }

    void refresh() {
        keys_ = playerPowerKeys(g_, level_, extra_);
    }

    void say(const std::string& m, bool warn) {
        msg_ = m;
        msgWarn_ = warn;
        msgTimer_ = 4.0f;
    }

    void beginCapture(const std::string& power, bool pad) {
        capPower_ = power;
        capPad_ = pad;
        captureTimer_ = 6.0f;
        g_.app.input().beginCapture(pad ? InputDevice::Gamepad : InputDevice::Keyboard);
    }

    void handleCapture() {
        InputManager& in = g_.app.input();
        if (!in.captureDone()) return;
        in.cancelCapture();
        if (capPower_.empty()) return;
        if (!capPad_) {
            SDL_Scancode sc = in.capturedKey();
            if (sc == SDL_SCANCODE_ESCAPE) say("Modification annulée", true);
            else if (sc != SDL_SCANCODE_UNKNOWN) setKey(capPower_, sc);
        } else {
            PadBinding b = in.capturedPad();
            if (b.kind != PadBinding::Kind::None) setPad(capPower_, b);
        }
        in.consumePlayerInput();
        capPower_.clear();
        g_.uiSound("ui_confirm");
    }

    /// The player's key for a power; a power of the level that had it takes the old key of
    /// this one (they swap). The level's own key is no choice to keep.
    void setKey(const std::string& power, SDL_Scancode sc) {
        const ResolvedPowerKey* me = keys_.find(power);
        if (!me) return;
        const SDL_Scancode old = me->key;
        std::string swapped;
        for (const auto& o : keys_.powers)
            if (o.power != power && o.key == sc && old != SDL_SCANCODE_UNKNOWN && old != sc) {
                storeKey(o, old);
                swapped = o.power;
            }
        storeKey(*me, sc);
        g_.powerKeyOverrides.save();
        refresh();
        if (std::string cmd = baseCommandOfKey(g_.settings.input, sc); !cmd.empty())
            say("« " + keyName(sc) + " » est la touche de « " + cmd + " » : la commande l'emporte, choisissez une autre touche.", true);
        else if (!swapped.empty())
            say("« " + powerName(swapped) + " » prend « " + keyName(old) + " » : les deux pouvoirs échangent leurs touches.", false);
        else
            say("« " + powerName(power) + " » : « " + keyName(sc) + " » dans ce niveau.", false);
    }

    void setPad(const std::string& power, const PadBinding& b) {
        const ResolvedPowerKey* me = keys_.find(power);
        if (!me) return;
        const PadBinding old = me->pad;
        const GamepadStyle style = g_.app.input().gamepadStyle();
        std::string swapped;
        for (const auto& o : keys_.powers)
            if (o.power != power && o.pad == b && old.kind != PadBinding::Kind::None && !(old == b)) {
                storePad(o, old);
                swapped = o.power;
            }
        storePad(*me, b);
        g_.powerKeyOverrides.save();
        refresh();
        if (std::string cmd = baseCommandOfPad(g_.settings.input, b); !cmd.empty())
            say("« " + padBindingName(b, style) + " » est le bouton de « " + cmd + " » : la commande l'emporte, choisissez un autre bouton.", true);
        else if (!swapped.empty())
            say("« " + powerName(swapped) + " » prend « " + padBindingName(old, style) + " » : les deux pouvoirs échangent leurs boutons.", false);
        else
            say("« " + powerName(power) + " » : « " + padBindingName(b, style) + " » dans ce niveau.", false);
    }

    void storeKey(const ResolvedPowerKey& r, SDL_Scancode sc) {
        g_.powerKeyOverrides.setKey(level_.id, r.power, sc == r.levelKey ? SDL_SCANCODE_UNKNOWN : sc);
    }
    void storePad(const ResolvedPowerKey& r, const PadBinding& b) {
        g_.powerKeyOverrides.setPad(level_.id, r.power, b == r.levelPad ? PadBinding{} : b);
    }

    void drawCaptureModal() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, {0, 0, 0, 150});
        Rect m = Rect::fromCenter({ui.screenW() * 0.5f, ui.screenH() * 0.5f}, {std::min(ui.s(760), ui.screenW() - ui.s(40)), ui.s(240)});
        ui.panel(m, true, 16);
        const std::string title = (capPad_ ? "Nouveau bouton : " : "Nouvelle touche : ") + powerName(capPower_);
        const float room = m.w - ui.s(60);
        float tf = std::max(ui.fitFont(title, FontFace::Bold, 28, room, 0.6f), fz(ui, 11.5f));
        ui.label(ui.elide(title, FontFace::Bold, tf, room), {m.center().x, m.y + ui.s(40)}, tf, ui.theme().text, Align::Center, FontFace::Bold);
        const std::string ask = capPad_ ? "Appuyez sur un bouton ou une gâchette de la manette"
                                        : "Appuyez sur une touche ou un bouton de la souris, ou tournez la molette (Échap : annuler)";
        ui.labelWrapped(ask, {m.x + ui.s(30), m.y + ui.s(84)}, room, std::max(ui.fitFont(ask, FontFace::Body, 22, room * 2.0f, 0.75f), fz(ui, 11.5f)),
                        ui.theme().textDim);
        float k = std::max(0.0f, captureTimer_ / 6.0f);
        ui.r().fillRect({m.x + ui.s(40), m.bottom() - ui.s(50), (m.w - ui.s(80)) * k, ui.s(6)}, ui.theme().accent);
    }

    GameServices& g_;
    LevelData level_;
    std::vector<std::string> extra_;
    PowerKeyMap keys_;
    float t_ = 0.0f;
    std::string capPower_;
    bool capPad_ = false;
    float captureTimer_ = 0.0f;
    std::string msg_;
    bool msgWarn_ = false;
    float msgTimer_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeLevelPowerKeys(GameServices& g, LevelData level, std::vector<std::string> extra) {
    return std::make_unique<LevelPowerKeysScene>(g, std::move(level), std::move(extra));
}

}  // namespace cosmic
