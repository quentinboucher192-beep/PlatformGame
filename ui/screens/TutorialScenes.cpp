#include "ui/screens/TutorialScenes.h"

#include <algorithm>
#include <cmath>

#include "engine/animation/Animation.h"
#include "engine/core/Easing.h"
#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/powers/PowerKeys.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

// The tutorial's soft palette (the validated mock-up): a cream card, ink text, mint and pink.
constexpr Color kCream{255, 248, 239, 255};
constexpr Color kInk{42, 33, 80, 255};
constexpr Color kInkDim{98, 84, 140, 255};  // 6.2:1 on the cream
constexpr Color kMint{143, 243, 234, 255};
constexpr Color kPink{255, 179, 217, 255};
constexpr Color kGold{255, 210, 122, 255};
constexpr Color kLilac{239, 232, 255, 255};
constexpr Color kGrass{127, 211, 139, 255};

/// A font size in pixels: `base` at the reference 1920×1080, never under `least` pixels.
float px(UIContext& ui, float base, float least = 12.0f) { return std::max(ui.s(base), least); }

/// A button of the tutorial's cards: drawn by hand (cream world), focus, mouse, keyboard and
/// gamepad through UIContext::hotspot. `ready` false: shown, not yet answering.
bool softButton(UIContext& ui, const std::string& name, const Rect& r, const std::string& text, float textPx, bool primary, bool ready = true,
                bool danger = false, Action glyph = Action::Count, float glyphH = 0.0f) {
    bool hovered = false;
    const bool hit = ui.hotspot(name, r, &hovered) && ready;
    const bool focus = ui.focused(name);
    Renderer& rr = ui.r();
    const float rad = std::min(r.h * 0.3f, ui.s(18));
    if (focus) rr.fillRoundedRect(r.expanded(ui.s(5)), rad + ui.s(5), kPink.withAlphaF(0.85f));
    Color fill = danger ? Color{255, 208, 216, 255} : primary ? kMint : kLilac;
    if (!ready) fill = fill.withAlphaF(0.55f);
    rr.fillRoundedRect(r, rad, fill);
    rr.drawRoundedRect(r, rad, kInk, std::max(1.5f, ui.s(focus || hovered ? 4.0f : 3.0f)));
    const float gw = glyph != Action::Count ? Glyphs::measureAction(ui, glyph, glyphH) : 0.0f;
    const float gap = gw > 0.0f ? ui.s(14) : 0.0f;
    const float room = r.w - ui.s(28) - gw - gap;
    const float fs = ui.fitFont(text, FontFace::Bold, textPx / ui.scale(), room, 0.6f);
    const float tw = std::min(room, ui.text().measure(text, FontFace::Bold, ui.s(fs)).x);
    const float x0 = r.center().x - (tw + gap + gw) * 0.5f;
    ui.label(ui.elide(text, FontFace::Bold, fs, room), {x0, r.center().y}, fs, kInk.withAlphaF(ready ? 1.0f : 0.6f), Align::Left, FontFace::Bold,
             VAlign::Middle);
    if (gw > 0.0f) Glyphs::drawAction(ui, glyph, {x0 + tw + gap, r.center().y}, glyphH);
    return hit;
}

/// A sprite of the previews (its own animator, clip changed only when asked).
struct Sprite {
    AnimationSetPtr set;
    Animator anim;
    std::string clip;
    void load(ResourceManager& res, const std::string& path, const std::string& c) {
        set = res.animation(path);
        if (set) anim.setSet(set);
        play(c);
    }
    void play(const std::string& c) {
        if (!set || c == clip || !anim.has(c)) return;
        clip = c;
        anim.play(c, true);
    }
    void draw(Renderer& r, Vec2 feet, float scale, bool flip = false, Color tint = Color::white()) const {
        if (!set || !set->texture) return;
        Rect src = anim.sourceRect();
        Rect dst = anim.destRect(feet, flip, scale);
        r.drawTexture(*set->texture, &src, dst, tint, BlendMode::Alpha, flip, false);
    }
};

/// A small « z » drawn with lines (sleeping creatures).
void drawZ(Renderer& r, Vec2 c, float s, Color col) {
    const float t = std::max(1.0f, s * 0.18f);
    r.drawLine({c.x - s * 0.5f, c.y - s * 0.5f}, {c.x + s * 0.5f, c.y - s * 0.5f}, col, t);
    r.drawLine({c.x + s * 0.5f, c.y - s * 0.5f}, {c.x - s * 0.5f, c.y + s * 0.5f}, col, t);
    r.drawLine({c.x - s * 0.5f, c.y + s * 0.5f}, {c.x + s * 0.5f, c.y + s * 0.5f}, col, t);
}

float pingPong(float t) {
    float f = t - std::floor(t);
    return f < 0.5f ? f * 2.0f : 2.0f - f * 2.0f;
}

}  // namespace

// ====================================================================== Bip

void drawBip(Renderer& r, Vec2 c, float size, float time, float alpha) {
    const float s = size * 0.5f;
    c.y += std::sin(time * 3.0f) * s * 0.12f;
    r.drawGlow({c.x, c.y + s * 1.05f}, s * 0.9f, kMint.withAlphaF(0.45f * alpha));  // its little engine
    r.drawGlow(c, s * 2.2f, kMint.withAlphaF(0.22f * alpha));
    // Antenna and its light.
    const Vec2 top{c.x, c.y - s * 0.85f};
    r.drawLine(top, {top.x, top.y - s * 0.45f}, kInk.withAlphaF(alpha), std::max(1.0f, s * 0.12f));
    const float blink = 0.6f + 0.4f * std::sin(time * 5.0f);
    r.fillCircle({top.x, top.y - s * 0.55f}, s * 0.18f, kGold.withAlphaF(alpha));
    r.drawGlow({top.x, top.y - s * 0.55f}, s * 0.5f, kGold.withAlphaF(0.5f * blink * alpha));
    // Body, face screen, eyes (they blink every few seconds).
    Rect body{c.x - s, c.y - s * 0.85f, s * 2.0f, s * 1.7f};
    r.fillRoundedRect(body.expanded(std::max(1.0f, s * 0.12f)), s * 0.55f, kInk.withAlphaF(alpha));
    r.fillRoundedRect(body, s * 0.48f, kMint.withAlphaF(alpha));
    Rect face{c.x - s * 0.72f, c.y - s * 0.52f, s * 1.44f, s * 0.95f};
    r.fillRoundedRect(face, s * 0.3f, kInk.withAlphaF(alpha));
    const bool shut = std::fmod(time, 3.6f) > 3.45f;
    for (float side : {-1.0f, 1.0f}) {
        Vec2 e{c.x + side * s * 0.32f, c.y - s * 0.05f};
        if (shut) r.drawLine({e.x - s * 0.13f, e.y}, {e.x + s * 0.13f, e.y}, kMint.withAlphaF(alpha), std::max(1.0f, s * 0.08f));
        else r.fillRoundedRect({e.x - s * 0.1f, e.y - s * 0.17f, s * 0.2f, s * 0.34f}, s * 0.1f, kMint.withAlphaF(alpha));
    }
    // Cheeks.
    r.fillCircle({c.x - s * 0.78f, c.y + s * 0.35f}, s * 0.12f, kPink.withAlphaF(0.9f * alpha));
    r.fillCircle({c.x + s * 0.78f, c.y + s * 0.35f}, s * 0.12f, kPink.withAlphaF(0.9f * alpha));
}

// ====================================================================== card layout

float tutorialKeyRowWidth(UIContext& ui, const tutorial::KeyRow& row, const PowerKeyMap* keys, float labelPx, float glyphH, bool draw, Vec2 at) {
    const std::string label = row.label + " :";
    float x = at.x;
    const float lw = ui.text().measure(label, FontFace::Bold, labelPx).x;
    if (draw) ui.label(label, {x, at.y}, labelPx / ui.scale(), kInk, Align::Left, FontFace::Bold, VAlign::Middle);
    x += lw + ui.s(10);
    if (!row.power.empty()) {
        static const PowerKeyMap none;
        const ResolvedPowerKey* k = (keys ? keys : &none)->find(row.power);
        const ResolvedPowerKey empty;
        x += Glyphs::drawPowerKey(ui, k ? *k : empty, {x, at.y}, glyphH, draw);
    }
    for (size_t i = 0; i < row.actions.size(); ++i) {
        if (i > 0) {
            const float sw = ui.text().measure("/", FontFace::Body, labelPx).x;
            if (draw) ui.label("/", {x + ui.s(6), at.y}, labelPx / ui.scale(), kInkDim, Align::Left, FontFace::Body, VAlign::Middle);
            x += sw + ui.s(12);
        }
        x += draw ? Glyphs::drawAction(ui, row.actions[i], {x, at.y}, glyphH) : Glyphs::measureAction(ui, row.actions[i], glyphH);
    }
    return x - at.x;
}

TutorialCardLayout layoutTutorialCard(UIContext& ui, const tutorial::Card& card, const std::string& kicker, const PowerKeyMap* keys) {
    TutorialCardLayout L;
    const float W = ui.screenW(), H = ui.screenH();
    const float pad = ui.s(40), gap = ui.s(24);
    const float panelW = std::min(W - ui.s(60), ui.s(1100));
    const float inner = panelW - 2.0f * pad;
    // Header: Bip, then the kicker (« BIP T'EXPLIQUE · 3 / 9 ») over the title.
    const float bip = ui.s(96);
    L.kickerPx = px(ui, 20);
    const float headRoom = inner - bip - ui.s(22);
    L.titlePx = ui.s(ui.fitFont(card.title, FontFace::Title, std::max(40.0f, 17.0f / ui.scale()), headRoom, 0.5f));
    const float kickerH = ui.text().measure(kicker, FontFace::Bold, L.kickerPx).y;
    const float titleH = ui.text().measure(card.title, FontFace::Title, L.titlePx).y;
    const float headH = std::max(bip, kickerH + ui.s(6) + titleH);
    // Body: the animated preview, the text at its right (the largest size that keeps it about as
    // tall as the preview).
    const float previewW = std::min(ui.s(360), inner * 0.4f), previewH = previewW * 0.6f;
    const float textRoom = inner - previewW - ui.s(30);
    float textH = 0.0f;
    for (float base : {27.0f, 25.0f, 23.0f, 21.0f, 19.0f}) {
        L.textPx = px(ui, base, 13.0f);
        textH = ui.text().measureWrapped(card.text, FontFace::Body, L.textPx, textRoom).y;
        if (textH <= previewH * 1.15f) break;
    }
    const float bodyH = std::max(previewH, textH);
    // Keys: each row « Sauter : [Espace] », side by side while they fit, else on the next line.
    L.keyPx = px(ui, 21);
    L.glyphH = std::max(ui.s(40), L.keyPx * 1.6f);
    const float keyLineH = L.glyphH + ui.s(10);
    std::vector<Vec2> keyPos;  // relative to the keys block
    float kx = 0.0f, ky = 0.0f;
    for (const auto& row : card.keys) {
        const float w = std::min(inner, tutorialKeyRowWidth(ui, row, keys, L.keyPx, L.glyphH));
        if (kx > 0.0f && kx + w > inner) {
            kx = 0.0f;
            ky += keyLineH;
        }
        keyPos.push_back({kx, ky});
        L.keys.push_back({0, 0, w, keyLineH - ui.s(10)});
        kx += w + ui.s(36);
    }
    const float keysH = card.keys.empty() ? 0.0f : ky + keyLineH - ui.s(10);
    // Footer: the note, then « Compris, je continue » and its key; side by side when they fit.
    L.notePx = px(ui, 19);
    L.buttonPx = px(ui, 26, 14.0f);
    const float glyphH = std::max(ui.s(36), L.buttonPx * 1.3f);
    L.buttonGlyphW = Glyphs::measureAction(ui, Action::MenuConfirm, glyphH);
    const float btnTextW = ui.text().measure(kTutorialContinue, FontFace::Bold, L.buttonPx).x;
    const float btnW = std::min(inner, btnTextW + ui.s(14) + L.buttonGlyphW + ui.s(56));
    const float btnH = std::max(ui.s(76), glyphH + ui.s(20));
    const float noteW = ui.text().measure(kTutorialPauseNote, FontFace::Body, L.notePx).x;
    const bool side = noteW + ui.s(24) + btnW <= inner;
    const float noteH = side ? ui.text().measure(kTutorialPauseNote, FontFace::Body, L.notePx).y
                             : ui.text().measureWrapped(kTutorialPauseNote, FontFace::Body, L.notePx, inner).y;
    const float footH = side ? btnH : noteH + ui.s(14) + btnH;
    const float panelH = pad + headH + gap + bodyH + (keysH > 0.0f ? gap + keysH : 0.0f) + gap + footH + pad;
    L.panel = {(W - panelW) * 0.5f, std::max(ui.s(10), (H - panelH) * 0.5f), panelW, panelH};
    const float x0 = L.panel.x + pad;
    float y = L.panel.y + pad;
    L.bip = {x0, y + (headH - bip) * 0.5f, bip, bip};
    const float tx = x0 + bip + ui.s(22);
    const float hy = y + (headH - kickerH - ui.s(6) - titleH) * 0.5f;
    L.kicker = {tx, hy, std::min(headRoom, ui.text().measure(kicker, FontFace::Bold, L.kickerPx).x), kickerH};
    L.title = {tx, hy + kickerH + ui.s(6), std::min(headRoom, ui.text().measure(card.title, FontFace::Title, L.titlePx).x), titleH};
    y += headH + gap;
    L.preview = {x0, y + (bodyH - previewH) * 0.5f, previewW, previewH};
    L.text = {x0 + previewW + ui.s(30), y + (bodyH - textH) * 0.5f, textRoom, textH};
    y += bodyH;
    if (keysH > 0.0f) {
        y += gap;
        for (size_t i = 0; i < L.keys.size(); ++i) {
            L.keys[i].x = x0 + keyPos[i].x;
            L.keys[i].y = y + keyPos[i].y;
        }
        y += keysH;
    }
    y += gap;
    if (side) {
        L.button = {x0 + inner - btnW, y, btnW, btnH};
        L.note = {x0, y + (btnH - noteH) * 0.5f, noteW, noteH};
    } else {
        L.note = {x0, y, inner, noteH};
        L.button = {x0 + inner - btnW, y + noteH + ui.s(14), btnW, btnH};
    }
    L.buttonText = {L.button.center().x - (btnTextW + ui.s(14) + L.buttonGlyphW) * 0.5f, L.button.y, btnTextW, btnH};
    return L;
}

// ====================================================================== the explanation card

namespace {

class TutorialCardScene : public Scene {
public:
    TutorialCardScene(GameServices& g, GameScene& game, std::string card) : g_(g), game_(game), id_(std::move(card)) {}

    void onEnter() override {
        card_ = tutorial::card(id_);
        g_.app.audio().setMusicDuck(0.45f);
        g_.uiSound("ui_confirm", 0.5f);
        ResourceManager& res = g_.app.resources();
        nova_.load(res, "animations/nova.json", "idle");
        other_.load(res, kOtherSprite(), "idle");
        shard_.load(res, "animations/shard.json", "spin");
        g_.ui.setFocus("tcard/ok");
    }
    void onExit() override { g_.app.audio().setMusicDuck(1.0f); }
    void update(float dt) override {
        t_ += dt;
        nova_.anim.update(dt);
        other_.anim.update(dt);
        shard_.anim.update(dt);
    }
    bool isOverlay() const override { return true; }
    void render(float) override;

private:
    const char* kOtherSprite() const {
        if (!card_) return "";
        switch (card_->preview) {
            case tutorial::Preview::Bounce: return "animations/bounce_pad.json";
            case tutorial::Preview::Attack: return "animations/gloop.json";
            case tutorial::Preview::Lantern: return "animations/checkpoint.json";
            case tutorial::Preview::Boss: return "animations/mycelia.json";
            default: return "";
        }
    }
    void close() {
        if (closed_) return;
        closed_ = true;
        g_.uiSound("ui_confirm", 0.6f);
        g_.app.popScene();
    }
    void drawPreview(const Rect& P);

    GameServices& g_;
    GameScene& game_;
    std::string id_;
    const tutorial::Card* card_ = nullptr;
    Sprite nova_, other_, shard_;
    float t_ = 0.0f;
    bool closed_ = false;
};

void TutorialCardScene::drawPreview(const Rect& P) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const float rad = ui.s(18);
    r.fillRoundedRectGradient(P, rad, Color{74, 58, 138, 255}, Color{122, 90, 176, 255});
    r.pushClip(P);
    const float groundH = P.h * 0.18f, gy = P.bottom() - groundH;
    const auto pv = card_->preview;
    if (pv == tutorial::Preview::Bubble) r.fillRect({P.x, gy + groundH * 0.3f, P.w, groundH}, Color{154, 215, 255, 255});
    else {
        r.fillRect({P.x, gy, P.w, groundH}, kGrass);
        r.fillRect({P.x, gy, P.w, std::max(2.0f, groundH * 0.18f)}, Color{168, 240, 168, 255});
    }
    const float k = P.h / 72.0f;  // Nova about 40 % of the picture's height
    const float t = t_;
    switch (pv) {
        case tutorial::Preview::Walk: {
            const float f = pingPong(t / 3.2f);
            const bool right = std::fmod(t / 3.2f, 1.0f) < 0.5f;
            nova_.play("run");
            nova_.draw(r, {P.x + P.w * (0.18f + 0.64f * f), gy}, k, !right);
            break;
        }
        case tutorial::Preview::Jump: {
            // A short tap, then a long press: the same jump, held longer, goes higher.
            const float cyc = std::fmod(t, 2.4f);
            const bool big = cyc >= 1.2f;
            const float ph = std::fmod(cyc, 1.2f) / 0.75f;
            const float h = ph < 1.0f ? std::sin(ph * kPi) * P.h * (big ? 0.5f : 0.22f) : 0.0f;
            nova_.play(ph >= 1.0f ? "idle" : ph < 0.5f ? "jump" : "fall");
            r.fillRoundedRect({P.x + P.w * 0.68f, gy - P.h * 0.36f, P.w * 0.2f, P.h * 0.36f}, ui.s(6), Color{168, 240, 168, 255});
            nova_.draw(r, {P.x + P.w * 0.42f, gy - h}, k);
            break;
        }
        case tutorial::Preview::Bounce: {
            const float ph = std::fmod(t, 1.3f) / 1.3f;
            const float h = std::sin(ph * kPi) * P.h * 0.6f;
            other_.play(ph < 0.12f ? "bounce" : "idle");
            other_.draw(r, {P.center().x, gy}, k * 1.2f);
            for (int i = 0; i < 3; ++i) shard_.draw(r, {P.x + P.w * (0.3f + 0.2f * i), P.y + P.h * 0.2f + ui.s(4) * std::sin(t * 2 + i)}, k * 1.3f);
            nova_.play(ph < 0.5f ? "jump" : "fall");
            nova_.draw(r, {P.center().x, gy - P.h * 0.1f - h}, k);
            break;
        }
        case tutorial::Preview::Shards: {
            const float f = std::fmod(t / 3.0f, 1.0f);
            for (int i = 0; i < 5; ++i) {
                const float sx = P.x + P.w * (0.15f + 0.17f * i);
                const bool taken = f * 1.1f > 0.08f + 0.17f * i + 0.05f;
                if (!taken) shard_.draw(r, {sx, gy - P.h * 0.32f + ui.s(4) * std::sin(t * 2 + i)}, k * 1.4f);
            }
            nova_.play("run");
            nova_.draw(r, {P.x + P.w * (0.05f + 0.95f * f), gy}, k);
            break;
        }
        case tutorial::Preview::Attack: {
            const float ph = std::fmod(t, 1.1f);
            nova_.play(ph < 0.35f ? "attack" : "idle");
            if (ph < 0.02f) nova_.anim.play("attack", true);
            const bool hit = ph > 0.12f && ph < 0.32f;
            other_.draw(r, {P.x + P.w * 0.64f, gy}, k * 1.6f, true, hit ? Color{255, 255, 255, 140} : Color{255, 196, 230, 255});
            drawZ(r, {P.x + P.w * 0.7f, gy - P.h * 0.45f - ui.s(6) * pingPong(t * 0.7f)}, ui.s(12), Color{255, 255, 255, 220});
            nova_.draw(r, {P.x + P.w * 0.38f, gy}, k);
            break;
        }
        case tutorial::Preview::Dash: {
            const float cyc = std::fmod(t, 1.6f);
            const float f = std::clamp(cyc / 0.45f, 0.0f, 1.0f);
            const float x = P.x + P.w * (0.15f + 0.65f * applyEase(Ease::OutCubic, f));
            nova_.play(f < 1.0f ? "dash" : "idle");
            if (f < 1.0f)
                for (int i = 1; i <= 4; ++i)
                    r.drawLine({x - ui.s(14) * i, gy - P.h * (0.1f + 0.06f * i)}, {x - ui.s(14) * i - ui.s(30), gy - P.h * (0.1f + 0.06f * i)},
                               kMint.withAlphaF(0.8f - 0.15f * i), std::max(1.5f, ui.s(3)));
            nova_.draw(r, {x, gy}, k);
            break;
        }
        case tutorial::Preview::Bubble: {
            const float f = std::fmod(t / 3.0f, 1.0f);
            const float bx = P.x + P.w * (0.1f + 0.8f * f), br = P.h * 0.16f;
            const Vec2 c{bx, gy - br * 0.2f};
            r.fillCircle(c, br, Color{150, 220, 255, 90});
            r.drawCircle(c, br, Color{230, 248, 255, 230}, std::max(1.5f, ui.s(2.5f)));
            r.fillCircle(c + Vec2{-br * 0.4f, -br * 0.45f}, br * 0.18f, Color{255, 255, 255, 220});
            nova_.play("idle");
            nova_.draw(r, {bx, c.y - br}, k);
            break;
        }
        case tutorial::Preview::Lantern: {
            const float glow = 0.6f + 0.4f * std::sin(t * 2.5f);
            r.drawGlow({P.center().x + P.w * 0.12f, gy - P.h * 0.3f}, P.h * 0.4f, kGold.withAlphaF(0.5f * glow));
            other_.play("active");
            other_.draw(r, {P.center().x + P.w * 0.12f, gy}, k * 0.9f);
            const float ph = std::fmod(t, 2.4f);
            nova_.play(ph < 0.8f ? "respawn" : "idle");
            if (ph < 0.02f) nova_.anim.play("respawn", true);
            nova_.draw(r, {P.x + P.w * 0.32f, gy}, k);
            break;
        }
        case tutorial::Preview::Boss: {
            const float cyc = std::fmod(t, 2.6f);
            const bool warn = cyc > 1.0f && cyc < 1.9f;
            other_.play(warn ? "telegraph" : "idle");
            if (warn) r.drawGlow({P.x + P.w * 0.72f, gy - P.h * 0.3f}, P.h * (0.35f + 0.1f * std::sin(t * 12.0f)), Color{255, 160, 200, 150});
            other_.draw(r, {P.x + P.w * 0.72f, gy}, k * 0.42f, true, Color{255, 196, 230, 255});
            const float jp = std::clamp((cyc - 1.6f) / 0.7f, 0.0f, 1.0f);
            const float h = jp > 0.0f && jp < 1.0f ? std::sin(jp * kPi) * P.h * 0.4f : 0.0f;
            nova_.play(h > 0.0f ? (jp < 0.5f ? "jump" : "fall") : "idle");
            nova_.draw(r, {P.x + P.w * 0.26f, gy - h}, k);
            break;
        }
    }
    r.popClip();
    r.drawRoundedRect(P, rad, kInk, std::max(1.5f, ui.s(3)));
}

void TutorialCardScene::render(float) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    if (!card_) {
        close();
        ui.end();
        return;
    }
    const float k = std::min(1.0f, t_ / 0.25f);
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{10, 8, 30, 255}.withAlphaF(0.6f * k));
    auto [rank, total] = game_.tutorialCardRank(id_);
    const std::string kicker = total > 0 && rank > 0 ? "BIP T'EXPLIQUE · " + std::to_string(rank) + " / " + std::to_string(total) : "BIP T'EXPLIQUE";
    TutorialCardLayout L = layoutTutorialCard(ui, *card_, kicker, &game_.powerKeys());
    const float lift = (1.0f - applyEase(Ease::OutCubic, k)) * ui.s(30);
    auto up = [&](Rect r) { return Rect{r.x, r.y + lift, r.w, r.h}; };
    Renderer& r = ui.r();
    const Rect panel = up(L.panel);
    r.fillRoundedRect(panel.expanded(ui.s(4)), ui.s(30), kPink);
    r.fillRoundedRect(panel, ui.s(26), kCream);
    drawBip(r, up(L.bip).center(), L.bip.w * 0.7f, t_);
    ui.label(kicker, up(L.kicker).pos(), L.kickerPx / ui.scale(), kInkDim, Align::Left, FontFace::Bold);
    ui.label(card_->title, up(L.title).pos(), L.titlePx / ui.scale(), kInk, Align::Left, FontFace::Title);
    drawPreview(up(L.preview));
    {
        TextStyle st;
        st.face = FontFace::Body;
        st.size = L.textPx;
        st.color = kInk;
        ui.text().drawWrapped(card_->text, up(L.text).pos(), L.text.w, st);
    }
    for (size_t i = 0; i < L.keys.size() && i < card_->keys.size(); ++i) {
        const Rect kr = up(L.keys[i]);
        r.fillRoundedRect(kr.expanded(ui.s(8)), ui.s(12), kLilac);
        tutorialKeyRowWidth(ui, card_->keys[i], &game_.powerKeys(), L.keyPx, L.glyphH, true, {kr.x, kr.center().y});
    }
    {
        TextStyle st;
        st.face = FontFace::Body;
        st.size = L.notePx;
        st.color = kInkDim;
        ui.text().drawWrapped(kTutorialPauseNote, up(L.note).pos(), L.note.w + 1.0f, st);
    }
    // Ready after a short moment: a jump pressed as the card opens never closes it unread.
    const bool ready = t_ > 0.6f;
    ui.pushId("tcard");
    const float glyphH = std::max(ui.s(36), L.buttonPx * 1.3f);
    bool ok = softButton(ui, "ok", up(L.button), kTutorialContinue, L.buttonPx, true, ready, false, Action::MenuConfirm, glyphH);
    ui.popId();
    if (ok || (ready && ui.backPressed())) close();
    ui.end();
}

}  // namespace

std::unique_ptr<Scene> makeTutorialCard(GameServices& g, GameScene& game, const std::string& card) {
    return std::make_unique<TutorialCardScene>(g, game, card);
}

// ====================================================================== the end card

namespace {
/// 2.81: the end card's texts for the player, or for an administrator's replay.
const char* endStartText(bool replay) { return replay ? "Retour à l'administration" : "Commencer l'aventure"; }
const char* endReminder(bool replay) {
    return replay ? "Didacticiel rejoué par un administrateur : rien n'a été enregistré, ni progression, ni souvenir, ni record, ni succès."
                  : "Le didacticiel ne se rejoue pas. Les pouvoirs restent expliqués dans le Guide des pouvoirs (menu Pause).";
}
}  // namespace

TutorialEndLayout layoutTutorialEnd(UIContext& ui, bool replay) {
    TutorialEndLayout L;
    const float W = ui.screenW(), H = ui.screenH();
    const float pad = ui.s(44), gap = ui.s(22);
    const float panelW = std::min(W - ui.s(60), ui.s(1100));
    const float inner = panelW - 2.0f * pad;
    const float bip = ui.s(100);
    const std::string title = "Bravo, Nova !";
    const std::string sub = "La gardienne câline est apaisée : tu es prête pour l'aventure.";
    L.titlePx = ui.s(ui.fitFont(title, FontFace::Title, std::max(52.0f, 20.0f / ui.scale()), inner - bip - ui.s(22), 0.5f));
    L.subtitlePx = px(ui, 22, 13.0f);
    const float titleH = ui.text().measure(title, FontFace::Title, L.titlePx).y;
    const float subH = ui.text().measureWrapped(sub, FontFace::Body, L.subtitlePx, inner - bip - ui.s(22)).y;
    const float headH = std::max(bip, titleH + ui.s(6) + subH);
    // What was learnt: two columns of chips.
    L.itemPx = px(ui, 21, 12.5f);
    const float colW = (inner - ui.s(14)) * 0.5f;
    const float itemH = std::max(ui.s(50), L.itemPx * 2.0f);
    const auto& items = tutorial::learnt();
    const int rows = static_cast<int>((items.size() + 1) / 2);
    const float learntTitleH = ui.text().measure("Ce que tu as appris", FontFace::Bold, L.itemPx).y;
    const float gridH = learntTitleH + ui.s(12) + rows * itemH + (rows - 1) * ui.s(10);
    // The souvenir: « Souvenir », the title's pill, where it is.
    L.souvenirPx = px(ui, 20, 12.5f);
    const float souvenirH = replay ? 0.0f : std::max(ui.s(64), L.souvenirPx * 2.6f);  // 2.81: a replay gives none
    // Footer: the reminder (wrapped, left), the buttons (right).
    L.reminderPx = px(ui, 18, 12.0f);
    L.buttonPx = px(ui, 24, 13.0f);
    const float btnH = std::max(ui.s(70), L.buttonPx * 2.4f);
    const float startW = std::min(inner, ui.text().measure(endStartText(replay), FontFace::Bold, L.buttonPx).x + ui.s(64));
    const float wearW = replay ? 0.0f
                               : std::min(inner, std::max(ui.text().measure("Porter ce titre", FontFace::Bold, L.buttonPx).x,
                                                          ui.text().measure("Titre porté", FontFace::Bold, L.buttonPx).x) +
                                                     ui.s(56));
    const std::string reminder = endReminder(replay);
    const float buttonsW = startW + (replay ? 0.0f : ui.s(16)) + wearW;
    const float remRoom = inner - buttonsW - ui.s(28);
    const bool side = remRoom >= ui.s(420);
    const float remW = side ? remRoom : inner;
    const float remH = ui.text().measureWrapped(reminder, FontFace::Body, L.reminderPx, remW).y;
    const float footH = side ? std::max(btnH, remH) : remH + ui.s(14) + btnH;
    const float panelH = pad + headH + gap + gridH + gap + (replay ? 0.0f : souvenirH + gap) + footH + pad;
    L.panel = {(W - panelW) * 0.5f, std::max(ui.s(10), (H - panelH) * 0.5f), panelW, panelH};
    const float x0 = L.panel.x + pad;
    float y = L.panel.y + pad;
    L.bip = {x0, y + (headH - bip) * 0.5f, bip, bip};
    const float tx = x0 + bip + ui.s(22);
    const float hy = y + (headH - titleH - ui.s(6) - subH) * 0.5f;
    L.title = {tx, hy, ui.text().measure(title, FontFace::Title, L.titlePx).x, titleH};
    L.subtitle = {tx, hy + titleH + ui.s(6), inner - bip - ui.s(22), subH};
    y += headH + gap;
    L.learntTitle = {x0, y, ui.text().measure("Ce que tu as appris", FontFace::Bold, L.itemPx).x, learntTitleH};
    y += learntTitleH + ui.s(12);
    for (size_t i = 0; i < items.size(); ++i) {
        const int row = static_cast<int>(i / 2), col = static_cast<int>(i % 2);
        L.learnt.push_back({x0 + col * (colW + ui.s(14)), y + row * (itemH + ui.s(10)), colW, itemH});
    }
    y += rows * itemH + (rows - 1) * ui.s(10) + gap;
    L.souvenirBox = {x0, y, inner, souvenirH};
    if (!replay) {
        const float sx = x0 + ui.s(20), cy = y + souvenirH * 0.5f;
        const float lw = ui.text().measure("Souvenir", FontFace::Bold, L.souvenirPx).x;
        const float lh = ui.text().measure("Souvenir", FontFace::Bold, L.souvenirPx).y;
        L.souvenirLabel = {sx, cy - lh * 0.5f, lw, lh};
        const std::string pill = "Titre « Premier envol »";
        const float pw = ui.text().measure(pill, FontFace::Bold, L.souvenirPx).x + ui.s(28);
        const float ph = lh + ui.s(14);
        L.souvenirPill = {sx + lw + ui.s(18), cy - ph * 0.5f, pw, ph};
        const float wx = L.souvenirPill.right() + ui.s(16);
        L.souvenirWhere = {wx, cy - lh * 0.5f, std::max(0.0f, x0 + inner - ui.s(20) - wx), lh};
    }
    if (!replay) y += souvenirH + gap;
    if (side) {
        L.reminder = {x0, y + (footH - remH) * 0.5f, remW, remH};
        L.start = {x0 + inner - startW, y + (footH - btnH) * 0.5f, startW, btnH};
        L.wear = {L.start.x - ui.s(16) - wearW, L.start.y, wearW, btnH};
    } else {
        L.reminder = {x0, y, remW, remH};
        L.start = {x0 + inner - startW, y + remH + ui.s(14), startW, btnH};
        L.wear = {L.start.x - ui.s(16) - wearW, L.start.y, wearW, btnH};
    }
    return L;
}

TutorialWelcomeLayout layoutTutorialWelcome(UIContext& ui, bool confirm) {
    TutorialWelcomeLayout L;
    const float W = ui.screenW(), H = ui.screenH();
    const float pad = ui.s(44), panelW = std::min(W - ui.s(60), ui.s(1000)), inner = panelW - 2 * pad;
    const float bip = ui.s(110);
    L.title = confirm ? "Passer le didacticiel ?" : "Bienvenue, Nova !";
    L.text = confirm ? "Il ne sera plus proposé, même plus tard. Les pouvoirs restent expliqués dans le Guide des pouvoirs (menu Pause) : tu "
                       "pourras t'y reporter à tout moment."
                     : "Je suis Bip, ton petit robot. Avant la grande aventure, je te propose un premier vol tout doux, d'environ six minutes, "
                       "pour découvrir le jeu. Rien n'y est compté, et il ne se joue qu'une fois.";
    L.startText = confirm ? "Non, je le fais" : "Commencer le didacticiel";
    L.skipText = confirm ? "Oui, passer" : "Passer le didacticiel";
    const float room = inner - bip - ui.s(24);
    L.titlePx = ui.s(ui.fitFont(L.title, FontFace::Title, std::max(48.0f, 19.0f / ui.scale()), room, 0.5f));
    L.textPx = px(ui, 23, 13.0f);
    const float titleH = ui.text().measure(L.title, FontFace::Title, L.titlePx).y;
    const float textH = ui.text().measureWrapped(L.text, FontFace::Body, L.textPx, room).y;
    const float headH = std::max(bip, titleH + ui.s(12) + textH);
    L.buttonPx = px(ui, 24, 13.0f);
    const float btnH = std::max(ui.s(70), L.buttonPx * 2.4f);
    float aw = ui.text().measure(L.startText, FontFace::Bold, L.buttonPx).x + ui.s(64);
    float bw = ui.text().measure(L.skipText, FontFace::Bold, L.buttonPx).x + ui.s(64);
    const bool side = aw + bw + ui.s(18) <= inner;
    if (!side) aw = bw = inner;
    const float buttonsH = side ? btnH : btnH * 2 + ui.s(14);
    const float panelH = pad + headH + ui.s(30) + buttonsH + pad;
    L.panel = {(W - panelW) * 0.5f, std::max(ui.s(10), (H - panelH) * 0.5f), panelW, panelH};
    const float x0 = L.panel.x + pad;
    float y = L.panel.y + pad;
    L.bip = {x0, y + (headH - bip) * 0.5f, bip, bip};
    const float tx = x0 + bip + ui.s(24);
    const float hy = y + (headH - titleH - ui.s(12) - textH) * 0.5f;
    L.titleRect = {tx, hy, std::min(room, ui.text().measure(L.title, FontFace::Title, L.titlePx).x), titleH};
    L.textRect = {tx, hy + titleH + ui.s(12), room, textH};
    y += headH + ui.s(30);
    if (side) {
        L.start = {x0 + inner - aw, y, aw, btnH};
        L.skip = {L.start.x - ui.s(18) - bw, y, bw, btnH};
    } else {
        L.start = {x0, y, inner, btnH};
        L.skip = {x0, y + btnH + ui.s(14), inner, btnH};
    }
    return L;
}

namespace {

class TutorialEndScene : public Scene {
public:
    explicit TutorialEndScene(GameServices& g, bool replay = false) : g_(g), replay_(replay) {}
    void onEnter() override { g_.ui.setFocus("tend/start"); }
    void update(float dt) override { t_ += dt; }
    bool isOverlay() const override { return true; }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const float k = std::min(1.0f, t_ / 0.4f);
        Renderer& r = ui.r();
        r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{27, 35, 80, 255}.withAlphaF(0.85f * k));
        r.drawGlow({ui.screenW() * 0.5f, ui.screenH() * 0.3f}, ui.screenH() * 0.7f, Color{90, 74, 176, 255}.withAlphaF(0.5f * k));
        const TutorialEndLayout L = layoutTutorialEnd(ui, replay_);
        r.fillRoundedRect(L.panel.expanded(ui.s(4)), ui.s(34), kGold);
        r.fillRoundedRect(L.panel, ui.s(30), kCream);
        drawBip(r, L.bip.center(), L.bip.w * 0.72f, t_);
        ui.label("Bravo, Nova !", L.title.pos(), L.titlePx / ui.scale(), kInk, Align::Left, FontFace::Title);
        TextStyle st;
        st.size = L.subtitlePx;
        st.color = kInkDim;
        ui.text().drawWrapped("La gardienne câline est apaisée : tu es prête pour l'aventure.", L.subtitle.pos(), L.subtitle.w, st);
        ui.label("Ce que tu as appris", L.learntTitle.pos(), L.itemPx / ui.scale(), kInk, Align::Left, FontFace::Bold);
        const auto& items = tutorial::learnt();
        for (size_t i = 0; i < items.size() && i < L.learnt.size(); ++i) {
            const Rect& c = L.learnt[i];
            r.fillRoundedRect(c, ui.s(14), kLilac);
            const float d = std::min(c.h * 0.5f, ui.s(26));
            const Vec2 mc{c.x + ui.s(14) + d * 0.5f, c.center().y};
            r.fillCircle(mc, d * 0.5f, Color{47, 168, 79, 255});
            r.drawLine({mc.x - d * 0.22f, mc.y}, {mc.x - d * 0.04f, mc.y + d * 0.2f}, Color::white(), std::max(1.5f, ui.s(3)));
            r.drawLine({mc.x - d * 0.04f, mc.y + d * 0.2f}, {mc.x + d * 0.26f, mc.y - d * 0.2f}, Color::white(), std::max(1.5f, ui.s(3)));
            const float tx = mc.x + d * 0.5f + ui.s(12), room = c.right() - ui.s(12) - tx;
            const float fs = ui.fitFont(items[i], FontFace::Body, L.itemPx / ui.scale(), room, 0.7f);
            ui.label(ui.elide(items[i], FontFace::Body, fs, room), {tx, c.center().y}, fs, kInk, Align::Left, FontFace::Body, VAlign::Middle);
        }
        if (!replay_) {  // 2.81: an administrator's replay gives no souvenir
            r.fillRoundedRect(L.souvenirBox, ui.s(16), Color{255, 241, 201, 255});
            r.drawRoundedRect(L.souvenirBox, ui.s(16), kGold, std::max(1.0f, ui.s(2)));
            ui.label("Souvenir", L.souvenirLabel.pos(), L.souvenirPx / ui.scale(), kInk, Align::Left, FontFace::Bold);
            r.drawGlow(L.souvenirPill.center(), L.souvenirPill.w * 0.6f, Color{255, 180, 84, 90});
            r.fillRoundedRect(L.souvenirPill, ui.s(12), Color::white());
            r.drawRoundedRect(L.souvenirPill, ui.s(12), Color{255, 180, 84, 255}, std::max(1.0f, ui.s(2)));
            ui.label("Titre « Premier envol »", L.souvenirPill.center(), L.souvenirPx / ui.scale(), kInk, Align::Center, FontFace::Bold, VAlign::Middle);
            if (L.souvenirWhere.w > ui.s(40)) {
                const std::string where = "dans le Vestiaire de la Collection";
                const float fs = ui.fitFont(where, FontFace::Body, L.souvenirPx / ui.scale(), L.souvenirWhere.w, 0.7f);
                ui.label(ui.elide(where, FontFace::Body, fs, L.souvenirWhere.w), {L.souvenirWhere.x, L.souvenirWhere.center().y}, fs, kInkDim, Align::Left,
                         FontFace::Body, VAlign::Middle);
            }
        }
        TextStyle rs;
        rs.size = L.reminderPx;
        rs.color = kInkDim;
        ui.text().drawWrapped(endReminder(replay_), L.reminder.pos(), L.reminder.w, rs);
        const bool ready = t_ > 0.8f;
        ui.pushId("tend");
        const bool worn = g_.collection.equippedId("title") == tutorial::kSouvenirTitle;
        if (!replay_ && softButton(ui, "wear", L.wear, worn ? "Titre porté" : "Porter ce titre", L.buttonPx, false, ready) && !worn) {
            g_.collection.equip("title", tutorial::kSouvenirTitle);
            g_.collection.save();
            g_.syncCosmetics();
            g_.uiSound("ui_confirm", 0.6f);
        }
        const bool start = softButton(ui, "start", L.start, endStartText(replay_), L.buttonPx, true, ready);
        ui.popId();
        if (start && !leaving_ && replay_) {  // 2.81: back to ADMINISTRATION › Films
            leaving_ = true;
            g_.app.switchScene(makeAdmin(g_, "films"), 0.6f);
        } else if (start && !leaving_) {
            // The adventure starts on Sylvanox, its levels' map.
            leaving_ = true;
            g_.app.switchScene(makeLevelSelect(g_, "sylvanox"), 0.6f);
        }
        ui.end();
    }

private:
    GameServices& g_;
    bool replay_ = false;
    float t_ = 0.0f;
    bool leaving_ = false;
};

// ====================================================================== the welcome

class TutorialWelcomeScene : public Scene {
public:
    explicit TutorialWelcomeScene(GameServices& g) : g_(g) {}
    void onEnter() override {
        bg_.init(g_);
        g_.playMusic("music/menu_theme.ogg");
        g_.ui.setFocus("twel/start");
    }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.45f);
        Renderer& r = ui.r();
        const TutorialWelcomeLayout L = layoutTutorialWelcome(ui, confirm_);
        r.fillRoundedRect(L.panel.expanded(ui.s(4)), ui.s(34), confirm_ ? kGold : kPink);
        r.fillRoundedRect(L.panel, ui.s(30), kCream);
        drawBip(r, L.bip.center(), L.bip.w * 0.72f, t_);
        ui.label(L.title, L.titleRect.pos(), L.titlePx / ui.scale(), kInk, Align::Left, FontFace::Title);
        TextStyle st;
        st.size = L.textPx;
        st.color = kInk;
        ui.text().drawWrapped(L.text, L.textRect.pos(), L.textRect.w, st);
        const std::string& a = L.startText;
        const std::string& b = L.skipText;
        const Rect ra = L.start, rb = L.skip;
        const float btnPx = L.buttonPx;
        const bool ready = t_ > 0.5f;
        ui.pushId("twel");
        const bool pa = softButton(ui, "start", ra, a, btnPx, true, ready);
        const bool pb = softButton(ui, "skip", rb, b, btnPx, false, ready, confirm_);
        ui.popId();
        if (!leaving_ && ready) {
            if (pa && !confirm_) {
                start();
            } else if (pa && confirm_) {
                confirm_ = false;  // « Non, je le fais »: back to the welcome
                g_.ui.setFocus("twel/start");
            } else if (pb && !confirm_) {
                confirm_ = true;
                g_.ui.setFocus("twel/start");  // the safe answer first
                g_.uiSound("ui_back", 0.5f);
            } else if (pb && confirm_) {
                skip();
            } else if (confirm_ && ui.backPressed()) {
                confirm_ = false;
            }
        }
        ui.end();
    }

private:
    void start() {
        leaving_ = true;
        g_.uiSound("ui_confirm", 0.6f);
        if (auto run = makeTutorialRun(g_)) {
            g_.app.switchScene(std::move(run), 0.6f);
        } else {
            g_.progression.tutorial = "skipped";  // unreadable: never in the way of the game
            g_.progression.save();
            g_.app.switchScene(makeMainMenu(g_), 0.5f);
        }
    }
    void skip() {
        leaving_ = true;
        g_.progression.tutorial = "skipped";
        g_.progression.save();
        g_.uiSound("ui_back", 0.6f);
        g_.app.switchScene(makeMainMenu(g_), 0.5f);
    }

    GameServices& g_;
    MenuBackground bg_;
    float t_ = 0.0f;
    bool confirm_ = false;
    bool leaving_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeTutorialEnd(GameServices& g, bool replay) { return std::make_unique<TutorialEndScene>(g, replay); }

std::unique_ptr<Scene> makeTutorialWelcome(GameServices& g) { return std::make_unique<TutorialWelcomeScene>(g); }

std::unique_ptr<Scene> makeTutorialRun(GameServices& g) {
    std::string error;
    auto level = tutorial::loadLevel(&error);
    if (!level) {
        Log::error("Didacticiel illisible : {}", error);
        return nullptr;
    }
    PlayOptions opt;
    opt.level = std::move(*level);
    opt.tutorial = true;
    const std::string id = opt.level->id;
    return makeGameScene(g, id, std::move(opt));
}

std::unique_ptr<Scene> makeTutorialReplay(GameServices& g) {
    std::string error;
    auto level = tutorial::loadLevel(&error);
    if (!level) {
        Log::error("Didacticiel illisible : {}", error);
        return nullptr;
    }
    PlayOptions opt;
    opt.level = std::move(*level);
    opt.tutorial = true;
    opt.tutorialReplay = true;  // nothing of it is kept (GameScene)
    const std::string id = opt.level->id;
    return makeGameScene(g, id, std::move(opt));
}

}  // namespace cosmic
