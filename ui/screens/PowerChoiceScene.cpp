#include "ui/screens/PowerChoiceScene.h"

#include <algorithm>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "game/gameplay/InputFrame.h"
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
    float cardW = ui.s(220), gap = ui.s(18);
    float w0 = std::max(ui.s(1000), n * cardW + (n - 1) * gap + ui.s(80));
    float h0 = ui.s(600);
    Rect panel{(ui.screenW() - w0) * 0.5f, (ui.screenH() - h0) * 0.5f + (1.0f - applyEase(Ease::OutCubic, k)) * ui.s(30), w0, h0};
    ui.panel(panel, true, 18);
    ui.r().drawRoundedRect(panel, ui.s(18), def->color.withAlpha(170), ui.s(2));

    // The new power
    float x = panel.x + ui.s(40), y = panel.y + ui.s(34);
    Rect icon{x, y, ui.s(84), ui.s(84)};
    ui.r().fillRoundedRect(icon, ui.s(14), {7, 11, 24, 255});
    ui.r().drawRoundedRect(icon, ui.s(14), def->color, ui.s(2));
    ui.icon("ui/icons_powers.png", def->icon, 32, icon.expanded(-ui.s(10)));
    ui.label("NOUVEAU POUVOIR : " + def->name, {icon.right() + ui.s(20), y + ui.s(6)}, 30, def->color, Align::Left, FontFace::Title);
    ui.labelWrapped(def->description, {icon.right() + ui.s(20), y + ui.s(50)}, panel.right() - icon.right() - ui.s(70), 20,
                    ui.theme().textDim);
    y += ui.s(110);
    ui.separator({panel.x + ui.s(40), y}, panel.w - ui.s(80));
    y += ui.s(18);
    std::string question;
    if (offer->conflictSlot >= 0 && offer->conflictSlot < n)
        question = powers[static_cast<size_t>(offer->conflictSlot)]->def().name + " utilise le même bouton : le remplacer ?";
    else
        question = "Vous portez déjà " + std::to_string(n) + " pouvoirs, le maximum de ce niveau. Lequel remplacer ?";
    ui.label(question, {panel.x + ui.s(40), y}, 22, ui.theme().text);
    y += ui.s(48);

    // Carried powers
    ui.pushId("pchoice");
    float total = n * cardW + (n - 1) * gap;
    float cx = panel.center().x - total * 0.5f;
    float cardH = ui.s(190);
    for (int i = 0; i < n; ++i) {
        const PowerDefinition& pd = powers[static_cast<size_t>(i)]->def();
        Rect card{cx + i * (cardW + gap), y, cardW, cardH};
        UIContext::ButtonStyle st;
        st.enabled = offer->conflictSlot < 0 || offer->conflictSlot == i;
        bool chosen = ui.button("slot" + std::to_string(i), card, "", st);
        Rect ic{card.center().x - ui.s(30), card.y + ui.s(22), ui.s(60), ui.s(60)};
        ui.r().fillRoundedRect(ic, ui.s(10), {7, 11, 24, 255});
        ui.r().drawRoundedRect(ic, ui.s(10), pd.color.withAlpha(st.enabled ? 255 : 90), ui.s(2));
        ui.icon("ui/icons_powers.png", pd.icon, 32, ic.expanded(-ui.s(8)), st.enabled ? Color::white() : Color{110, 120, 140, 255});
        float fs = ui.fitFont(pd.name, FontFace::Bold, 21, cardW - ui.s(20));
        ui.label(pd.name, {card.center().x, ic.bottom() + ui.s(12)}, fs, st.enabled ? ui.theme().text : ui.theme().textDim, Align::Center,
                 FontFace::Bold);
        bool focusedNow = ui.focused("slot" + std::to_string(i));
        ui.label(st.enabled ? (focusedNow ? "Remplacer" : "Laisser ici en échange") : "Autre bouton", {card.center().x, card.bottom() - ui.s(38)}, 17,
                 focusedNow ? ui.theme().accent : ui.theme().textDim, Align::Center);
        if (chosen && st.enabled) answer(static_cast<uint8_t>(i + 1));
    }
    y += cardH + ui.s(28);
    UIContext::ButtonStyle leave;
    bool doLeave = ui.button("leave", {panel.x + ui.s(40), y, ui.s(380), ui.s(56)}, "Laisser " + def->name + " ici", leave);
    ui.popId();
    ui.label("Le jeu attend votre choix. Un pouvoir remplacé reste sur la tablette : revenez le reprendre quand vous voulez.",
             {panel.x + ui.s(40), panel.bottom() - ui.s(46)}, 17, ui.theme().textDim);
    float hx = panel.x + ui.s(40) + ui.s(380) + ui.s(36);
    hx += Glyphs::drawHint(ui, Action::MenuConfirm, "remplacer", {hx, y + ui.s(28)}, ui.s(28)) + ui.s(28);
    Glyphs::drawHint(ui, Action::MenuBack, "laisser", {hx, y + ui.s(28)}, ui.s(28));
    if (doLeave || (ui.backPressed() && t_ > 0.15f)) answer(InputFrame::kChoiceLeave);
    ui.end();
}

}  // namespace cosmic
