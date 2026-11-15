#include "ui/screens/PowerChoiceScene.h"

#include <algorithm>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "game/gameplay/InputFrame.h"
#include "game/powers/Power.h"
#include "ui/screens/GameScene.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

void PowerChoiceScene::onEnter() {
    g_.app.audio().setMusicDuck(0.45f);
    g_.ui.setFocus("pchoice/slot0");
    if (GameWorld* w = game_.world())
        if (const auto& offer = w->powerOffer(); offer && offer->conflictSlot >= 0)
            g_.ui.setFocus("pchoice/slot" + std::to_string(offer->conflictSlot));
}

void PowerChoiceScene::onExit() { g_.app.audio().setMusicDuck(1.0f); }

void PowerChoiceScene::answer(uint8_t choice) {
    if (answered_) return;
    answered_ = true;
    game_.answerPowerOffer(choice);
    g_.uiSound(choice == InputFrame::kChoiceLeave ? "ui_back" : "ui_confirm", 0.6f);
    g_.app.popScene();
}

void PowerChoiceScene::render(float /*alpha*/) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    GameWorld* w = game_.world();
    const GameWorld::PowerOffer* offer = w && w->powerOffer() ? &*w->powerOffer() : nullptr;
    const PowerDefinition* def = offer ? g_.content.power(offer->powerId) : nullptr;
    if (!w || !offer || !def) {
        // Nothing to choose any more (level restarted...): just close.
        if (!answered_) {
            answered_ = true;
            g_.app.popScene();
        }
        ui.end();
        return;
    }
    float k = std::min(1.0f, t_ / 0.22f);
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{3, 5, 12, 255}.withAlphaF(0.72f * k));
    const auto& powers = w->player().powers();
    int n = static_cast<int>(powers.size());
    const PowerTriggerInfo newUse = powerTriggerInfo(*def);
    const bool newFree = newUse.kind != PowerTrigger::SlotKey;
    float cardW = ui.s(220), gap = ui.s(18);
    float w0 = std::max(ui.s(1000), n * cardW + (n - 1) * gap + ui.s(80));
    // Heights measured first: the panel is as tall as what it holds.
    const float textX0 = ui.s(40) + ui.s(84) + ui.s(20);  // from the panel's left edge
    const float textRoom = w0 - textX0 - ui.s(40);
    const float descH = ui.text().measureWrapped(def->description, FontFace::Body, ui.s(20), textRoom).y;
    const float useH = ui.s(36);
    const float headH = std::max(ui.s(84), ui.s(50) + descH + ui.s(12) + useH);
    const float cardH = ui.s(214);
    const float footText = 17.0f;
    const std::string waitText = "Le jeu attend votre choix. Un pouvoir remplacé reste sur la tablette : revenez le reprendre quand vous voulez.";
    const float footH = ui.text().measureWrapped(waitText, FontFace::Body, ui.s(footText), w0 - ui.s(80)).y;
    float h0 = ui.s(34) + headH + ui.s(16) + ui.s(18) + ui.s(48) + cardH + ui.s(28) + ui.s(56) + ui.s(22) + footH + ui.s(26);
    h0 = std::min(h0, ui.screenH() - ui.s(20));
    Rect panel{(ui.screenW() - w0) * 0.5f, (ui.screenH() - h0) * 0.5f + (1.0f - applyEase(Ease::OutCubic, k)) * ui.s(30), w0, h0};
    ui.panel(panel, true, 18);
    ui.r().drawRoundedRect(panel, ui.s(18), def->color.withAlpha(170), ui.s(2));

    // The new power, and how it is used: the key of the slot it takes, or (dashed gold
    // frame) its move / « passif ».
    float x = panel.x + ui.s(40), y = panel.y + ui.s(34);
    Rect icon{x, y, ui.s(84), ui.s(84)};
    ui.r().fillRoundedRect(icon, ui.s(14), {7, 11, 24, 255});
    if (newFree) Glyphs::drawFreePowerFrame(ui, icon, ui.s(14), ui.s(2.5f));
    else ui.r().drawRoundedRect(icon, ui.s(14), def->color, ui.s(2));
    ui.icon("ui/icons_powers.png", def->icon, 32, icon.expanded(-ui.s(10)));
    const float tx = panel.x + textX0;
    const std::string title = "NOUVEAU POUVOIR : " + def->name;
    ui.label(title, {tx, y + ui.s(6)}, ui.fitFont(title, FontFace::Title, 30, textRoom), def->color, Align::Left, FontFace::Title);
    ui.labelWrapped(def->description, {tx, y + ui.s(50)}, textRoom, 20, ui.theme().textDim);
    const float uy = y + ui.s(50) + descH + ui.s(12) + useH * 0.5f;
    if (newFree) {
        const std::string how = newUse.kind == PowerTrigger::Passive ? "Passif : toujours actif" : newUse.how;
        Glyphs::drawFreePowerUse(ui, newUse.keys, how, {tx, uy}, ui.s(24));
    } else {
        const std::string t = "Il prendra la touche de l'emplacement où vous le mettez.";
        ui.label(t, {tx, uy}, ui.fitFont(t, FontFace::Body, 18, textRoom), ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
    }
    y += headH + ui.s(16);
    ui.separator({panel.x + ui.s(40), y}, panel.w - ui.s(80));
    y += ui.s(18);
    std::string question;
    if (offer->conflictSlot >= 0 && offer->conflictSlot < n)
        question = powers[static_cast<size_t>(offer->conflictSlot)]->def().name + " utilise le même bouton : le remplacer ?";
    else
        question = "Vous portez déjà " + std::to_string(n) + " pouvoirs, le maximum de ce niveau. Lequel remplacer ?";
    ui.label(question, {panel.x + ui.s(40), y}, ui.fitFont(question, FontFace::Body, 22, panel.w - ui.s(80)), ui.theme().text);
    y += ui.s(48);

    // Carried powers, each in its slot with the key of that slot (or, dashed gold frame,
    // the move that uses it / « passif »).
    ui.pushId("pchoice");
    float total = n * cardW + (n - 1) * gap;
    float cx = panel.center().x - total * 0.5f;
    for (int i = 0; i < n; ++i) {
        const Power& pw = *powers[static_cast<size_t>(i)];
        const PowerDefinition& pd = pw.def();
        Rect card{cx + i * (cardW + gap), y, cardW, cardH};
        UIContext::ButtonStyle st;
        st.enabled = offer->conflictSlot < 0 || offer->conflictSlot == i;
        bool chosen = ui.button("slot" + std::to_string(i), card, "", st);
        Rect ic{card.center().x - ui.s(30), card.y + ui.s(22), ui.s(60), ui.s(60)};
        const PowerTriggerInfo use = powerTriggerInfo(pw);
        const bool free = use.kind != PowerTrigger::SlotKey;
        ui.r().fillRoundedRect(ic, ui.s(10), {7, 11, 24, 255});
        if (free) Glyphs::drawFreePowerFrame(ui, ic, ui.s(10), ui.s(2), st.enabled ? 1.0f : 0.4f);
        else ui.r().drawRoundedRect(ic, ui.s(10), pd.color.withAlpha(st.enabled ? 255 : 90), ui.s(2));
        ui.icon("ui/icons_powers.png", pd.icon, 32, ic.expanded(-ui.s(8)), st.enabled ? Color::white() : Color{110, 120, 140, 255});
        float fs = ui.fitFont(pd.name, FontFace::Bold, 21, cardW - ui.s(20));
        ui.label(pd.name, {card.center().x, ic.bottom() + ui.s(12)}, fs, st.enabled ? ui.theme().text : ui.theme().textDim, Align::Center,
                 FontFace::Bold);
        // How it is used, centred, never wider than the card.
        const float kh = ui.s(26), ky = card.y + ui.s(146), room = cardW - ui.s(24);
        if (free) {
            const std::string t = use.kind == PowerTrigger::Passive ? "Passif" : "";
            float bw = Glyphs::drawFreePowerUse(ui, use.keys, t, {0.0f, 0.0f}, kh, 1.0f, false);
            float h = bw > room ? kh * room / bw : kh;
            bw = Glyphs::drawFreePowerUse(ui, use.keys, t, {0.0f, 0.0f}, h, 1.0f, false);
            Glyphs::drawFreePowerUse(ui, use.keys, t, {card.center().x - bw * 0.5f, ky}, h, st.enabled ? 1.0f : 0.5f);
        } else {
            const Action key = powerSlotAction(i);
            const std::string t = key == Action::Count ? "sans touche" : actionLabel(key);
            const float kw = Glyphs::measureAction(ui, key, kh), gap2 = ui.s(8);
            float lf = ui.fitFont(t, FontFace::Body, 16, std::max(ui.s(20), room - kw - gap2));
            float tw = ui.text().measure(t, FontFace::Body, ui.s(lf)).x;
            float kx = card.center().x - (kw + gap2 + tw) * 0.5f;
            Glyphs::drawAction(ui, key, {kx, ky}, kh);
            ui.label(t, {kx + kw + gap2, ky}, lf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        bool focusedNow = ui.focused("slot" + std::to_string(i));
        const std::string action = st.enabled ? (focusedNow ? "Remplacer" : "Laisser ici en échange") : "Autre bouton";
        ui.label(action, {card.center().x, card.bottom() - ui.s(34)}, ui.fitFont(action, FontFace::Body, 17, cardW - ui.s(20)),
                 focusedNow ? ui.theme().accent : ui.theme().textDim, Align::Center);
        if (chosen && st.enabled) answer(static_cast<uint8_t>(i + 1));
    }
    y += cardH + ui.s(28);
    UIContext::ButtonStyle leave;
    bool doLeave = ui.button("leave", {panel.x + ui.s(40), y, ui.s(380), ui.s(56)}, "Laisser " + def->name + " ici", leave);
    ui.popId();
    ui.labelWrapped(waitText, {panel.x + ui.s(40), y + ui.s(56) + ui.s(22)}, panel.w - ui.s(80), footText, ui.theme().textDim);
    float hx = panel.x + ui.s(40) + ui.s(380) + ui.s(36);
    hx += Glyphs::drawHint(ui, Action::MenuConfirm, "remplacer", {hx, y + ui.s(28)}, ui.s(28)) + ui.s(28);
    Glyphs::drawHint(ui, Action::MenuBack, "laisser", {hx, y + ui.s(28)}, ui.s(28));
    if (doLeave || (ui.backPressed() && t_ > 0.15f)) answer(InputFrame::kChoiceLeave);
    ui.end();
}

}  // namespace cosmic
