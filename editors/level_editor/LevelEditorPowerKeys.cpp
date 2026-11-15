// Level editor: « Touches des pouvoirs » (2.31). The key and gamepad button of each power of
// the level used with a key of its own (start powers, tablets and stations, powers given by
// events), saved in the level (LevelData::powerKeys). A power the level gives no key keeps its
// suggested key; each player may still change them for themselves. Not part of the gameplay:
// the level's fingerprint (records, replays, ghosts) never changes with them.

#include <algorithm>

#include "editors/level_editor/LevelEditorScene.h"
#include "game/GameServices.h"
#include "game/powers/PowerKeys.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }
}  // namespace

void LevelEditorScene::modalPowerKeys(const Rect& c) {
    UIContext& ui = g_.ui;
    InputManager& in = ui.input();
    const UITheme& th = ui.theme();
    const InputBindings defaults = InputBindings::defaults();
    const std::vector<std::string> powers = levelKeyPowers(level_, g_.content);
    const PowerKeyMap keys = resolvePowerKeys(powers, g_.content, level_.powerKeys, nullptr, defaults);
    const GamepadStyle style = in.gamepadStyle();
    auto powerName = [&](const std::string& id) {
        const PowerDefinition* d = g_.content.power(id);
        return d ? d->name : id;
    };

    // A key or button just captured goes to the level.
    if (powerKeyCaptureTimer_ > 0.0f) {
        powerKeyCaptureTimer_ -= g_.app.frameTime() / 1000.0f;
        if (powerKeyCaptureTimer_ <= 0.0f) in.cancelCapture();
    }
    if (in.captureDone() && !powerKeyCapture_.empty()) {
        in.cancelCapture();
        const ResolvedPowerKey* r = keys.find(powerKeyCapture_);
        if (r && !powerKeyCapturePad_) {
            const SDL_Scancode sc = in.capturedKey();
            if (sc == SDL_SCANCODE_ESCAPE) {
                flash("Modification annulée");
            } else if (sc != SDL_SCANCODE_UNKNOWN) {
                level_.powerKeys[powerKeyCapture_] = powerKeyJson(sc, r->levelPad);
                markChanged("Touches des pouvoirs");
            }
        } else if (r) {
            const PadBinding b = in.capturedPad();
            if (b.kind != PadBinding::Kind::None) {
                level_.powerKeys[powerKeyCapture_] = powerKeyJson(r->levelKey, b);
                markChanged("Touches des pouvoirs");
            }
        }
        in.consumePlayerInput();
        powerKeyCapture_.clear();
        g_.uiSound("ui_confirm");
        modalPowerKeys(c);  // drawn with the new key
        return;
    }
    const bool capturing = in.capturing() && !powerKeyCapture_.empty();
    const bool inputWas = ui.inputEnabled;
    ui.inputEnabled = inputWas && !capturing;

    ui.label("Touches des pouvoirs", {c.x, c.y}, 30, th.text, Align::Left, FontFace::Bold);
    float y = c.y + ui.s(52);
    const std::string intro =
        "Une touche et un bouton de manette pour chaque pouvoir du niveau qui s'utilise avec une touche à lui : les mêmes quel que soit "
        "l'emplacement du pouvoir. Sans choix ici, un pouvoir garde sa touche suggérée. Chaque joueur peut changer les siennes (choix du "
        "niveau, pause).";
    y += ui.labelWrapped(intro, {c.x, y}, c.w, fz(ui, 17), th.textDim).y + ui.s(16);

    // Columns: power, keyboard / mouse, gamepad.
    const float gap = ui.s(16), rowW = c.w - ui.s(14);
    const float nameW = rowW * 0.34f, keyW = rowW * 0.30f - gap, padW = rowW - nameW - keyW - 2.0f * gap;
    const float keyX = c.x + nameW + gap, padX = keyX + keyW + gap;
    auto head = [&](const std::string& t, float hx, float hw) {
        float f = std::max(ui.fitFont(t, FontFace::Bold, fz(ui, 16), hw, 0.7f), fz(ui, 11.5f));
        ui.label(ui.elide(t, FontFace::Bold, f, hw), {hx, y + ui.s(12)}, f, th.textDim, Align::Left, FontFace::Bold, VAlign::Middle);
    };
    head("Pouvoir", c.x + ui.s(12), nameW - ui.s(12));
    head("Clavier / souris", keyX, keyW);
    head("Manette", padX, padW);
    y += ui.s(30);

    // What is flagged under a row: the same key as another power, a key of a command.
    auto flags = [&](const ResolvedPowerKey& r) {
        std::vector<std::string> out;
        for (const auto& o : keys.powers) {
            if (o.power == r.power) continue;
            if (r.levelKey != SDL_SCANCODE_UNKNOWN && o.levelKey == r.levelKey)
                out.push_back("Clavier : même touche que « " + powerName(o.power) + " » (" + keyName(r.levelKey) + ")");
            if (r.levelPad.kind != PadBinding::Kind::None && o.levelPad == r.levelPad)
                out.push_back("Manette : même bouton que « " + powerName(o.power) + " » (" + padBindingName(r.levelPad, style) + ")");
        }
        if (std::string cmd = baseCommandOfKey(defaults, r.levelKey); !cmd.empty())
            out.push_back("Clavier : « " + keyName(r.levelKey) + " » est la touche de « " + cmd + " » par défaut");
        if (std::string cmd = baseCommandOfPad(defaults, r.levelPad); !cmd.empty())
            out.push_back("Manette : « " + padBindingName(r.levelPad, style) + " » est le bouton de « " + cmd + " » par défaut");
        return out;
    };
    const float flagW = rowW - ui.s(24);
    std::vector<float> heights;
    float total = 0.0f;
    for (const auto& r : keys.powers) {
        float h = ui.s(84);
        for (const auto& f : flags(r)) h += ui.text().measureWrapped(f, FontFace::Body, ui.s(fz(ui, 15)), flagW).y + ui.s(4);
        heights.push_back(h + ui.s(6));
        total += heights.back() + ui.s(8);
    }
    const float by = c.bottom() - ui.s(58);
    Rect area{c.x, y, c.w, by - ui.s(14) - y};
    ui.beginScroll("pkrows", area, std::max(total, ui.s(40)));
    float ry = area.y - ui.scrollOffset();
    if (keys.powers.empty())
        ui.labelWrapped("Aucun pouvoir de ce niveau ne s'utilise avec une touche à lui : ajoutez des pouvoirs au départ (Niveau) ou des tablettes de pouvoir.",
                        {c.x, ry + ui.s(6)}, c.w - ui.s(14), fz(ui, 18), th.textDim);
    for (size_t i = 0; i < keys.powers.size(); ++i) {
        const ResolvedPowerKey& r = keys.powers[i];
        const auto fl = flags(r);
        Rect row{c.x, ry, rowW, heights[i]};
        ui.r().fillRoundedRect(row, ui.s(10), Color{14, 20, 40, 220});
        if (!fl.empty()) ui.r().drawRoundedRect(row, ui.s(10), th.warm.withAlpha(160), std::max(1.0f, ui.s(1.5f)));
        const PowerDefinition* def = g_.content.power(r.power);
        Rect icon{c.x + ui.s(12), ry + ui.s(14), ui.s(46), ui.s(46)};
        ui.r().fillRoundedRect(icon, ui.s(8), {7, 11, 24, 255});
        if (def) {
            ui.r().drawRoundedRect(icon, ui.s(8), def->color, std::max(1.0f, ui.s(1.5f)));
            ui.icon("ui/icons_powers.png", def->icon, 32, icon.expanded(-ui.s(6)));
        }
        const float tx = icon.right() + ui.s(12), troom = c.x + nameW - tx;
        const std::string name = powerName(r.power);
        float nf = std::max(ui.fitFont(name, FontFace::Bold, fz(ui, 20), troom, 0.7f), fz(ui, 11.5f));
        ui.label(ui.elide(name, FontFace::Bold, nf, troom), {tx, ry + ui.s(37)}, nf, def ? def->color : th.text, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        // A button per device: the level's key (or the suggested one), captured like in the settings.
        auto column = [&](bool pad, float bx, float bw) {
            Rect b{bx, ry + ui.s(10), bw, ui.s(46)};
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !readOnly_;
            const bool chosen = (pad ? r.levelPadSource : r.levelKeySource) == PowerKeySource::Level;
            st.selected = chosen;
            const bool icon = pad && r.levelPad.kind != PadBinding::Kind::None && glyphIndex(r.levelPad) >= 0;
            std::string label = pad ? padBindingName(r.levelPad, style) : keyName(r.levelKey);
            if (!pad)
                if (std::string detail = mouseBindingDetail(r.levelKey); !detail.empty()) {
                    std::string full = label + " · " + detail;
                    if (ui.text().measure(full, FontFace::Bold, ui.s(18)).x <= b.w - ui.s(20)) label = full;
                }
            const float room = b.w - ui.s(20) - (icon ? 2.0f * (ui.s(8) + ui.s(30) + ui.s(4)) : 0.0f);
            st.fontSize = std::max(ui.fitFont(label, FontFace::Bold, 20, std::max(ui.s(24), room), 0.6f), fz(ui, 11.5f));
            if (ui.button(std::string(pad ? "pkp" : "pkk") + r.power, b, label, st) && !readOnly_) {
                powerKeyCapture_ = r.power;
                powerKeyCapturePad_ = pad;
                powerKeyCaptureTimer_ = 6.0f;
                in.beginCapture(pad ? InputDevice::Gamepad : InputDevice::Keyboard);
            }
            if (icon) Glyphs::drawPadButton(ui, r.levelPad, {b.x + ui.s(8), b.center().y}, ui.s(30));
            const std::string note = chosen ? "choisie pour ce niveau" : "suggérée";
            float f = std::max(ui.fitFont(note, FontFace::Body, fz(ui, 14), b.w, 0.8f), fz(ui, 11.5f));
            ui.label(ui.elide(note, FontFace::Body, f, b.w), {b.x, b.bottom() + ui.s(13)}, f, chosen ? th.accent : th.textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        };
        column(false, keyX, keyW);
        column(true, padX, padW);
        float fy = ry + ui.s(84);
        for (const auto& f : fl) {
            Vec2 sz = ui.labelWrapped(f, {c.x + ui.s(12), fy}, flagW, fz(ui, 15), th.warm);
            fy += sz.y + ui.s(4);
        }
        ry += heights[i] + ui.s(8);
    }
    ui.endScroll();

    // « Proposer »: every power on its suggestion, the conflicts resolved (as a level without
    // choice); « Fermer ».
    UIContext::ButtonStyle p;
    p.primary = true;
    p.enabled = !readOnly_ && !keys.powers.empty();
    if (ui.button("pkpropose", {c.x, by, ui.s(240), ui.s(56)}, "Proposer", p) && p.enabled) {
        level_.powerKeys = planLevelPowerKeys(level_, g_.content);
        markChanged("Touches des pouvoirs");
        flash("Touches proposées : les suggestions des pouvoirs, sans conflit");
    }
    const float hintX = c.x + ui.s(256), hintRoom = c.right() - ui.s(200) - ui.s(16) - hintX;
    if (readOnly_) {
        const std::string t = "Niveau en lecture seule : dupliquez-le pour choisir ses touches.";
        float f = std::max(ui.fitFont(t, FontFace::Body, fz(ui, 16), hintRoom, 0.75f), fz(ui, 11.5f));
        ui.label(ui.elide(t, FontFace::Body, f, hintRoom), {hintX, by + ui.s(28)}, f, th.warm, Align::Left, FontFace::Body, VAlign::Middle);
    } else {
        const std::string t = "Cliquez sur une touche puis appuyez sur la nouvelle (Échap : annuler).";
        float f = std::max(ui.fitFont(t, FontFace::Body, fz(ui, 16), hintRoom, 0.75f), fz(ui, 11.5f));
        ui.label(ui.elide(t, FontFace::Body, f, hintRoom), {hintX, by + ui.s(28)}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    if (ui.button("pkclose", {c.right() - ui.s(200), by, ui.s(200), ui.s(56)}, "Fermer") && !capturing) modal_ = Modal::None;
    ui.inputEnabled = inputWas;

    if (capturing) {
        // Waiting for the key: over the dialog.
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, {0, 0, 0, 150});
        Rect m = Rect::fromCenter({ui.screenW() * 0.5f, ui.screenH() * 0.5f}, {std::min(ui.s(760), ui.screenW() - ui.s(40)), ui.s(240)});
        ui.panel(m, true, 16);
        const std::string title = (powerKeyCapturePad_ ? "Bouton de manette : " : "Touche : ") + powerName(powerKeyCapture_);
        const float room = m.w - ui.s(60);
        float tf = std::max(ui.fitFont(title, FontFace::Bold, 28, room, 0.6f), fz(ui, 11.5f));
        ui.label(ui.elide(title, FontFace::Bold, tf, room), {m.center().x, m.y + ui.s(40)}, tf, th.text, Align::Center, FontFace::Bold);
        const std::string ask = powerKeyCapturePad_ ? "Appuyez sur un bouton ou une gâchette de la manette"
                                                    : "Appuyez sur une touche ou un bouton de la souris, ou tournez la molette (Échap : annuler)";
        ui.labelWrapped(ask, {m.x + ui.s(30), m.y + ui.s(84)}, room, std::max(ui.fitFont(ask, FontFace::Body, 22, room * 2.0f, 0.75f), fz(ui, 11.5f)),
                        th.textDim);
        float k = std::max(0.0f, powerKeyCaptureTimer_ / 6.0f);
        ui.r().fillRect({m.x + ui.s(40), m.bottom() - ui.s(50), (m.w - ui.s(80)) * k, ui.s(6)}, th.accent);
    }
}

}  // namespace cosmic
