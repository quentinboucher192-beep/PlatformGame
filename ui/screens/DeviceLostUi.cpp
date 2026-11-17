// 2.65: « MANETTE DÉBRANCHÉE », the pause of a run whose player lost its gamepad (see
// DeviceLostUi.h). Every text is measured first: the panel is as tall as what it holds, the
// lines are wrapped or fitted, nothing overlaps.

#include "ui/screens/DeviceLostUi.h"

#include <algorithm>

#include "game/GameServices.h"
#include "ui/screens/GameScene.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// A keycap's width as Glyphs::drawKeycap draws it.
float keycapWidth(UIContext& ui, const std::string& label, float h) { return std::max(h, ui.text().measure(label, FontFace::Bold, h * 0.55f).x + h * 0.6f); }

}  // namespace

DeviceLostChoice drawDeviceLost(UIContext& ui, const DeviceLostView& v) {
    Renderer& r = ui.r();
    const UITheme& th = ui.theme();
    const Color orange{255, 138, 76, 255}, gold{255, 210, 122, 255}, dim{154, 160, 200, 255}, body{228, 230, 250, 255};
    r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{8, 10, 26, 175});
    const float W = std::min(ui.s(780), ui.screenW() - ui.s(40));
    const float pad = ui.s(30), innerW = W - pad * 2.0f, gap = ui.s(16);
    DeviceLostChoice choice = DeviceLostChoice::None;

    if (v.countdown > 0) {
        // Every player has a device again: 3, 2, 1.
        const std::string title = "LA PARTIE REPREND";
        const std::string sub = "Le chrono repart en même temps que la partie.";
        const float tf = ui.fitFont(title, FontFace::Title, 30, innerW, 0.6f);
        const float sf = ui.fitFont(sub, FontFace::Body, 18, innerW, 0.7f);
        const float th1 = ui.text().measure(title, FontFace::Title, ui.s(tf)).y;
        const float digitH = ui.s(120);
        const float sh = ui.text().measure(sub, FontFace::Body, ui.s(sf)).y;
        const float H = pad + th1 + gap + digitH + gap + sh + pad;
        const Rect box{(ui.screenW() - W) * 0.5f, (ui.screenH() - H) * 0.5f, W, H};
        r.fillRoundedRect(box, ui.s(22), Color{18, 24, 54, 245});
        r.drawRoundedRect(box, ui.s(22), th.success, ui.s(3));
        float y = box.y + pad;
        ui.label(title, {box.center().x, y}, tf, th.text, Align::Center, FontFace::Title);
        y += th1 + gap;
        ui.label(std::to_string(v.countdown), {box.center().x, y + digitH * 0.5f}, 96, th.success, Align::Center, FontFace::Title, VAlign::Middle);
        y += digitH + gap;
        ui.label(sub, {box.center().x, y}, sf, dim, Align::Center, FontFace::Body);
        return choice;
    }

    // ---- sizes first
    const float iconSz = ui.s(84);
    const std::string title = "MANETTE DÉBRANCHÉE";
    std::string who = v.device;
    if (v.coop) who = v.seat + " · " + v.name + " · " + v.device;
    const float headRoom = innerW - iconSz - ui.s(18);
    const float tf = ui.fitFont(title, FontFace::Title, 28, headRoom, 0.55f);
    const float wf = ui.fitFont(who, FontFace::Body, 18, headRoom, 0.7f);
    const std::string whoShown = ui.elide(who, FontFace::Body, wf, headRoom);
    const std::string intro = "La partie et le chrono sont en pause. Rebranchez la manette (ou réveillez-la) : la partie reprend après un compte à "
                              "rebours de 3 secondes.";
    const float introH = ui.text().measureWrapped(intro, FontFace::Body, ui.s(18), innerW).y;
    const float chipH = ui.s(30);
    const std::string chipA = "A / Croix", chipEnter = "Entrée";
    const float chipW = std::max(keycapWidth(ui, chipA, chipH), keycapWidth(ui, chipEnter, chipH));
    const float boxIn = ui.s(14), lineRoom = innerW - boxIn * 2.0f - chipW - ui.s(12);
    const std::string lineA = v.coop ? "sur une autre manette libre : elle prend la place de " + v.name + ", avec ses touches"
                                     : "sur une autre manette : la partie reprend avec elle";
    const std::string lineEnter = !v.keyboardFree ? "au clavier : impossible, le clavier et la souris sont déjà à un autre joueur"
                                  : v.coop        ? "au clavier : " + v.name + " joue au clavier et à la souris, avec ses touches"
                                                  : "au clavier : vous continuez au clavier et à la souris";
    const float hA = std::max(chipH, ui.text().measureWrapped(lineA, FontFace::Body, ui.s(15), lineRoom).y);
    const float hE = std::max(chipH, ui.text().measureWrapped(lineEnter, FontFace::Body, ui.s(15), lineRoom).y);
    const std::string orTitle = "Ou bien";
    const float orH = ui.text().measure(orTitle, FontFace::Bold, ui.s(15)).y;
    const float orBoxH = boxIn + orH + ui.s(8) + hA + ui.s(8) + hE + boxIn;
    const int nb = v.coop ? 3 : 2;
    const float bgap = ui.s(10), bh = ui.s(52);
    const float bw = (innerW - bgap * static_cast<float>(nb - 1)) / static_cast<float>(nb);
    const std::string resumeLabel = v.coop ? "J'ai rebranché · Reprendre" : "Reprendre";
    const std::string withoutLabel = "Continuer sans " + v.name;
    const std::string quitLabel = v.confirmQuit ? "Confirmer : quitter" : "Quitter la partie";
    float bf = 17.0f;
    bf = std::min(bf, ui.fitFont(resumeLabel, FontFace::Bold, 17, bw - ui.s(18), 0.55f));
    bf = std::min(bf, ui.fitFont(quitLabel, FontFace::Bold, 17, bw - ui.s(18), 0.55f));
    if (v.coop) bf = std::min(bf, ui.fitFont(withoutLabel, FontFace::Bold, 17, bw - ui.s(18), 0.55f));
    const float msgH = v.message.empty() ? 0.0f : ui.text().measureWrapped(v.message, FontFace::Body, ui.s(15), innerW).y + ui.s(10);
    const std::string note = v.coop ? "Seuls les joueurs dont le périphérique est encore branché peuvent choisir. En ligne, chacun gère le sien : la règle "
                                      "des 60 s de reconnexion ne change pas."
                                    : "Le chrono reste arrêté tant que la partie est en pause.";
    const float noteH = ui.text().measureWrapped(note, FontFace::Body, ui.s(13), innerW).y;
    const float H = pad + iconSz + gap + introH + gap + orBoxH + gap + bh + ui.s(10) + msgH + noteH + pad;
    const float top = std::max(ui.s(10), (ui.screenH() - H) * 0.5f);
    const Rect box{(ui.screenW() - W) * 0.5f, top, W, H};

    // ---- drawing, one block under the other
    r.fillRoundedRect(box, ui.s(22), Color{18, 24, 54, 248});
    r.drawRoundedRect(box, ui.s(22), orange, ui.s(3));
    const float x = box.x + pad;
    float y = box.y + pad;
    const Rect icon{x, y, iconSz, iconSz};
    r.fillRoundedRect(icon, ui.s(20), Color{29, 37, 80, 255});
    r.fillCircle(icon.center(), iconSz * 0.28f, v.color);
    r.fillCircle(icon.center(), iconSz * 0.12f, Color{29, 37, 80, 255});
    const Vec2 bang{icon.right() - ui.s(16), icon.bottom() - ui.s(16)};
    r.fillCircle(bang, ui.s(14), Color{255, 90, 90, 255});
    ui.label("!", bang, 18, Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
    const float hx = icon.right() + ui.s(18);
    const float t1 = ui.text().measure(title, FontFace::Title, ui.s(tf)).y;
    const float t2 = ui.text().measure(whoShown, FontFace::Body, ui.s(wf)).y;
    float ty = icon.center().y - (t1 + ui.s(6) + t2) * 0.5f;
    ui.label(title, {hx, ty}, tf, th.text, Align::Left, FontFace::Title);
    ty += t1 + ui.s(6);
    ui.label(whoShown, {hx, ty}, wf, Color{216, 219, 245, 255}, Align::Left, FontFace::Body);
    y += iconSz + gap;
    ui.labelWrapped(intro, {x, y}, innerW, 18, body);
    y += introH + gap;
    const Rect orBox{x, y, innerW, orBoxH};
    r.fillRoundedRect(orBox, ui.s(14), Color{13, 20, 48, 255});
    float oy = y + boxIn;
    ui.label(orTitle, {x + boxIn, oy}, 15, gold, Align::Left, FontFace::Bold);
    oy += orH + ui.s(8);
    auto option = [&](const std::string& chip, const std::string& text, float h, bool on) {
        Glyphs::drawKeycap(ui, chip, {x + boxIn, oy + chipH * 0.5f}, chipH);
        const float textH = ui.text().measureWrapped(text, FontFace::Body, ui.s(15), lineRoom).y;
        ui.labelWrapped(text, {x + boxIn + chipW + ui.s(12), oy + std::max(0.0f, (chipH - textH) * 0.5f)}, lineRoom, 15, on ? body : dim);
        oy += h + ui.s(8);
    };
    option(chipA, lineA, hA, true);
    option(chipEnter, lineEnter, hE, v.keyboardFree);
    y += orBoxH + gap;
    ui.pushId("unplug");
    UIContext::ButtonStyle rs;
    rs.primary = true;
    rs.fontSize = bf;
    float bx = x;
    if (ui.button("resume", {bx, y, bw, bh}, resumeLabel, rs)) choice = DeviceLostChoice::Resume;
    bx += bw + bgap;
    if (v.coop) {
        UIContext::ButtonStyle ws;
        ws.fontSize = bf;
        if (ui.button("without", {bx, y, bw, bh}, withoutLabel, ws)) choice = DeviceLostChoice::ContinueWithout;
        bx += bw + bgap;
    }
    UIContext::ButtonStyle qs;
    qs.danger = true;
    qs.fontSize = bf;
    if (ui.button("quit", {bx, y, bw, bh}, quitLabel, qs)) choice = DeviceLostChoice::Quit;
    ui.popId();
    y += bh + ui.s(10);
    if (!v.message.empty()) {
        ui.labelWrapped(v.message, {x, y}, innerW, 15, th.warm);
        y += msgH;
    }
    ui.labelWrapped(note, {x, y}, innerW, 13, dim);
    return choice;
}

// ---------------------------------------------------------------------- the solo game

namespace {

class DeviceLostScene : public Scene {
public:
    DeviceLostScene(GameServices& g, GameScene& game, int pad) : g_(g), game_(game), pad_(pad) {}
    bool isOverlay() const override { return true; }

    void onEnter() override {
        pause_.reset(1);
        pause_.lost(0);
        g_.ui.setFocus("unplug/resume");
        if (const PadEntry* p = g_.app.input().devices().byNumber(pad_)) device_ = padLabel(*p);
        else device_ = "Manette";
    }

    void update(float dt) override {
        InputManager& in = g_.app.input();
        if (pause_.state() == UnplugPause::State::Waiting) {
            // The same pad plugged back (or woken up): 3, 2, 1.
            if (in.devices().connected(pad_)) pause_.restored(0);
        } else if (int lost = in.lostPad(); lost != 0) {
            // The pad used to resume went away during the countdown: waiting again.
            in.clearLostPad();
            pad_ = lost;
            if (const PadEntry* p = in.devices().byNumber(pad_)) device_ = padLabel(*p);
            pause_.lost(0);
            g_.ui.setFocus("unplug/resume");
        }
        pause_.update(dt);
        if (pause_.state() == UnplugPause::State::Running) {
            in.consumePlayerInput();  // what was pressed during the pause does nothing
            in.clearLostPad();
            g_.app.popScene();
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        DeviceLostView v;
        v.device = device_;
        v.countdown = pause_.countdownNumber();
        v.message = message_;
        v.confirmQuit = confirmQuit_;
        switch (drawDeviceLost(ui, v)) {
            case DeviceLostChoice::Resume:
                // With the device that pressed it: the keyboard (Entrée, a click) or another pad (A).
                pause_.restored(0);
                message_.clear();
                break;
            case DeviceLostChoice::Quit:
                if (!confirmQuit_) {
                    confirmQuit_ = true;
                    break;
                }
                g_.app.input().clearLostPad();
                g_.app.popScene();
                game_.quitToMenu();
                return;
            default: break;
        }
        ui.end();
    }

private:
    GameServices& g_;
    GameScene& game_;
    int pad_ = 0;
    std::string device_;
    std::string message_;
    UnplugPause pause_;
    bool confirmQuit_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeDeviceLost(GameServices& g, GameScene& game, int padNumber) { return std::make_unique<DeviceLostScene>(g, game, padNumber); }

}  // namespace cosmic
