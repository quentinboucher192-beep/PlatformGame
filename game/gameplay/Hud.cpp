#include "game/gameplay/Hud.h"

#include <cmath>

#include "engine/core/Application.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/records/Records.h"
#include "saves/Settings.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {
constexpr const char* kHudIcons = "ui/icons_hud.png";
enum HudIcon { HeartFull = 0, HeartEmpty, Energy, Shard, Relic, Skull, Clock, EnemyIcon, Star, Lock, Check };

TextStyle style(UIContext& ui, float size, Color c, FontFace f = FontFace::Bold, Align a = Align::Left,
                VAlign v = VAlign::Top) {
    TextStyle s;
    s.size = ui.s(size);
    s.color = c;
    s.face = f;
    s.align = a;
    s.valign = v;
    s.shadow = true;
    s.shadowOffset = ui.s(2);
    return s;
}
}  // namespace

void Hud::update(GameWorld& w, float dt) {
    time_ += dt;
    const Player& p = w.player();
    displayedHealth_ = approach(displayedHealth_, static_cast<float>(p.health), dt * 6.0f);
    displayedEnergy_ = lerpf(displayedEnergy_, p.energy, damp(10.0f, dt));
    if (w.stats().shards != lastShards_) {
        lastShards_ = w.stats().shards;
        shardPulse_ = 1.0f;
    }
    shardPulse_ = std::max(0.0f, shardPulse_ - dt * 3.0f);
    if (Enemy* b = w.activeBoss()) bossBarDelayed_ = approach(bossBarDelayed_, b->hpRatio(), dt * 0.35f);
    else bossBarDelayed_ = 1.0f;

    // Countdown warnings during the last 10 seconds.
    const LevelTimer& t = w.timer();
    if (t.hasLimit() && t.running()) {
        int sec = static_cast<int>(std::ceil(t.remaining()));
        if (sec != lastSecond_) {
            lastSecond_ = sec;
            if (sec <= 10 && sec > 0) w.playSound("sounds/timer_warning.wav", 0.5f, sec <= 3 ? 1.2f : 1.0f);
            else if (sec <= 30 && sec > 10) w.playSound("sounds/timer_tick.wav", 0.25f);
        }
    }
}

void Hud::render(UIContext& ui, GameWorld& w, const WorldView& view, const LevelRecord* record,
                 const GameplaySettings& settings) {
    bool cinematic = w.cinematic().active() || w.state() == WorldState::Intro || w.state() == WorldState::Outro;
    if (!cinematic) {
        drawVitals(ui, w);
        drawPowers(ui, w);
        if (settings.showTimer) drawTimer(ui, w, record);
        drawCollectibles(ui, w);
        drawBoss(ui, w);
        if (settings.showInputHints) drawInteractPrompt(ui, w, view);
    }
    drawMessages(ui, w);
    drawCinematic(ui, w);
}

void Hud::drawVitals(UIContext& ui, GameWorld& w) {
    const Player& p = w.player();
    float x = ui.s(40), y = ui.s(34);
    float hs = ui.s(44);
    for (int i = 0; i < p.config().maxHealth; ++i) {
        bool full = i < p.health;
        Rect r{x + i * (hs + ui.s(4)), y, hs, hs};
        if (full && p.health == 1) {
            float pulse = 1.0f + 0.12f * std::sin(time_ * 10.0f);
            r = Rect::fromCenter(r.center(), r.size() * pulse);
        }
        if (!full && i < static_cast<int>(std::ceil(displayedHealth_))) {
            // Losing a heart: brief white flash.
            ui.icon(kHudIcons, HeartFull, 16, r, Color{255, 255, 255, 160});
        } else {
            ui.icon(kHudIcons, full ? HeartFull : HeartEmpty, 16, r);
        }
    }
    // Energy bar
    float bw = ui.s(260), bh = ui.s(16);
    Rect frame{x + ui.s(4), y + hs + ui.s(12), bw, bh};
    ui.icon(kHudIcons, Energy, 16, {frame.x - ui.s(2), frame.center().y - ui.s(14), ui.s(28), ui.s(28)});
    frame.x += ui.s(32);
    Renderer& r = ui.r();
    r.fillRoundedRect(frame.expanded(ui.s(2)), ui.s(6), {6, 10, 22, 200});
    float k = std::clamp(displayedEnergy_ / p.config().maxEnergy, 0.0f, 1.0f);
    float kReal = std::clamp(p.energy / p.config().maxEnergy, 0.0f, 1.0f);
    Rect fill{frame.x, frame.y, frame.w * k, frame.h};
    r.fillRoundedRectGradient(fill, ui.s(5), {143, 255, 240, 255}, {31, 184, 176, 255});
    if (kReal < 0.25f) r.fillRoundedRect(fill, ui.s(5), Color{255, 77, 109, static_cast<uint8_t>(100 + 80 * std::sin(time_ * 8))});
    r.drawRoundedRect(frame.expanded(ui.s(2)), ui.s(6), {57, 240, 216, 120}, ui.s(1.5f));
    // Power cost markers
    for (const auto& pw : p.powers()) {
        float c = pw->def().energyCost / p.config().maxEnergy;
        if (c > 0.02f && c < 1.0f) r.fillRect({frame.x + frame.w * c, frame.y, ui.s(1.5f), frame.h}, {255, 255, 255, 60});
    }
}

void Hud::drawPowers(UIContext& ui, GameWorld& w) {
    const Player& p = w.player();
    if (p.powers().empty()) return;
    float size = ui.s(54);
    float x = ui.s(40), y = ui.s(140);
    Renderer& r = ui.r();
    for (const auto& pw : p.powers()) {
        Rect box{x, y, size, size};
        bool ready = pw->ready(p);
        bool enough = p.energy + 0.01f >= pw->def().energyCost;
        r.fillRoundedRect(box.expanded(ui.s(4)), ui.s(10), {8, 12, 26, 210});
        ui.icon("ui/icons_powers.png", pw->def().icon, 32, box, ready ? Color::white() : Color{110, 120, 140, 255});
        float cd = pw->cooldownRatio();
        if (cd > 0.0f) r.fillRect({box.x, box.y + box.h * (1.0f - cd), box.w, box.h * cd}, {0, 0, 0, 140});
        if (!enough) r.drawRoundedRect(box.expanded(ui.s(4)), ui.s(10), {255, 77, 109, 160}, ui.s(2));
        else if (pw->active()) {
            r.drawRoundedRect(box.expanded(ui.s(4)), ui.s(10), pw->def().color, ui.s(3));
            r.drawGlow(box.center(), size, pw->def().color.withAlpha(60));
        } else if (ready) {
            r.drawRoundedRect(box.expanded(ui.s(4)), ui.s(10), {57, 240, 216, 90}, ui.s(1.5f));
        }
        Glyphs::drawAction(ui, pw->def().input, {box.center().x - ui.s(14), box.bottom() + ui.s(22)}, ui.s(28));
        x += size + ui.s(18);
    }
}

void Hud::drawTimer(UIContext& ui, GameWorld& w, const LevelRecord* record) {
    const LevelTimer& t = w.timer();
    const LevelData& lvl = w.level();
    float x = ui.screenW() - ui.s(40), y = ui.s(30);
    bool limited = t.hasLimit();
    float shown = limited ? t.remaining() : t.elapsed();
    Color c = ui.theme().text;
    if (limited && t.remaining() < 30.0f) {
        float pulse = 0.5f + 0.5f * std::sin(time_ * (t.remaining() < 10.0f ? 14.0f : 6.0f));
        c = Color::lerp(ui.theme().warm, ui.theme().danger, pulse);
    }
    TextStyle st = style(ui, 46, c, FontFace::Title, Align::Right);
    st.glow = ui.s(10);
    st.glowColor = c.withAlpha(40);
    ui.text().draw(LevelTimer::format(shown), {x, y}, st);
    ui.icon(kHudIcons, Clock, 16, {x - ui.s(270), y + ui.s(10), ui.s(32), ui.s(32)}, Color{255, 255, 255, 200});
    float line = y + ui.s(62);
    TextStyle small = style(ui, 20, ui.theme().textDim, FontFace::Body, Align::Right);
    if (limited) {
        ui.text().draw("Temps écoulé " + LevelTimer::format(t.elapsed(), false), {x, line}, small);
        line += ui.s(26);
    }
    if (record && record->bestTime > 0.0f) {
        ui.text().draw("Record " + LevelTimer::format(record->bestTime), {x, line}, small);
        line += ui.s(26);
    }
    // Next medal objective still reachable
    for (int i = 3; i >= 1; --i) {
        const MedalRule& m = lvl.medals[i];
        if (m.maxTime > 0 && t.elapsed() <= m.maxTime) {
            static const char* names[] = {"Bronze", "Argent", "Or", "Platine"};
            static const Color cols[] = {{205, 127, 50, 255}, {200, 210, 225, 255}, {255, 210, 90, 255}, {170, 240, 255, 255}};
            TextStyle ms = small;
            ms.color = cols[i];
            ui.text().draw(std::string(names[i]) + " < " + LevelTimer::format(m.maxTime, false), {x, line}, ms);
            line += ui.s(26);
            break;
        }
    }
    if (w.splitFlashTimer() > 0.0f) {
        float d = w.lastSplitDelta();
        TextStyle ds = style(ui, 28, d <= 0 ? ui.theme().success : ui.theme().danger, FontFace::Bold, Align::Right);
        ds.color.a = static_cast<uint8_t>(255 * std::min(1.0f, w.splitFlashTimer()));
        ui.text().draw(LevelTimer::formatDelta(d), {x, line + ui.s(4)}, ds);
    }
}

void Hud::drawCollectibles(UIContext& ui, GameWorld& w) {
    const RunStats& s = w.stats();
    float cx = ui.screenW() * 0.5f;
    float y = ui.s(34);
    float is = ui.s(34) * (1.0f + 0.25f * shardPulse_);
    std::string shards = std::to_string(s.shards) + " / " + std::to_string(s.totalShards);
    std::string relics = std::to_string(s.relics) + " / " + std::to_string(s.totalRelics);
    TextStyle st = style(ui, 26, ui.theme().text, FontFace::Bold, Align::Left, VAlign::Middle);
    float total = ui.s(250);
    float x = cx - total * 0.5f;
    ui.r().fillRoundedRect({x - ui.s(14), y - ui.s(6), total + ui.s(28), ui.s(46)}, ui.s(23), {6, 10, 22, 150});
    ui.icon(kHudIcons, Shard, 16, Rect::fromCenter({x + ui.s(17), y + ui.s(17)}, {is, is}));
    ui.text().draw(shards, {x + ui.s(40), y + ui.s(17)}, st);
    if (s.totalRelics > 0) {
        ui.icon(kHudIcons, Relic, 16, {x + ui.s(140), y, ui.s(34), ui.s(34)});
        st.color = s.relics > 0 ? Color{255, 217, 138, 255} : ui.theme().textDim;
        ui.text().draw(relics, {x + ui.s(180), y + ui.s(17)}, st);
    }
}

void Hud::drawBoss(UIContext& ui, GameWorld& w) {
    Enemy* b = w.activeBoss();
    if (!b || !b->alive()) return;
    float bw = ui.s(900), bh = ui.s(22);
    Rect bar{(ui.screenW() - bw) * 0.5f, ui.screenH() - ui.s(86), bw, bh};
    Renderer& r = ui.r();
    r.fillRoundedRect(bar.expanded(ui.s(4)), ui.s(8), {6, 8, 18, 220});
    r.fillRoundedRect({bar.x, bar.y, bar.w * bossBarDelayed_, bar.h}, ui.s(6), {255, 230, 230, 200});
    r.fillRoundedRectGradient({bar.x, bar.y, bar.w * b->hpRatio(), bar.h}, ui.s(6), {255, 120, 140, 255}, {163, 18, 58, 255});
    // Phase thresholds
    for (const auto& ph : b->def().phases)
        if (ph.hpThreshold < 0.999f) r.fillRect({bar.x + bar.w * ph.hpThreshold - ui.s(1), bar.y - ui.s(4), ui.s(3), bar.h + ui.s(8)}, {255, 217, 138, 220});
    r.drawRoundedRect(bar.expanded(ui.s(4)), ui.s(8), {255, 77, 109, 160}, ui.s(2));
    TextStyle st = style(ui, 26, {255, 230, 235, 255}, FontFace::Title, Align::Center, VAlign::Bottom);
    std::string title = b->def().bossTitle.empty() ? b->def().name : b->def().bossTitle;
    ui.text().draw(title, {bar.center().x, bar.y - ui.s(10)}, st);
}

void Hud::drawInteractPrompt(UIContext& ui, GameWorld& w, const WorldView& view) {
    LevelEntity* e = w.nearestInteractable();
    if (!e || !w.player().alive()) return;
    Vec2 sp = view.toScreen({e->box.center().x, e->box.y - 10});
    float h = ui.s(34);
    TextStyle st = style(ui, 22, ui.theme().text, FontFace::Bold, Align::Left, VAlign::Middle);
    float tw = ui.text().measure(e->interactPrompt(), FontFace::Bold, st.size).x;
    float total = h + ui.s(10) + tw;
    float bob = std::sin(time_ * 4.0f) * ui.s(3);
    Vec2 pos{sp.x - total * 0.5f, sp.y - h + bob};
    ui.r().fillRoundedRect({pos.x - ui.s(10), pos.y - h * 0.5f - ui.s(6), total + ui.s(20), h + ui.s(12)}, ui.s(10), {6, 10, 22, 190});
    Glyphs::drawHint(ui, Action::Interact, e->interactPrompt(), pos, h);
}

void Hud::drawMessages(UIContext& ui, GameWorld& w) {
    float hintY = ui.screenH() - ui.s(150);
    for (const auto& m : w.messages()) {
        float a = std::min({1.0f, m.age / 0.3f, (m.duration - m.age) / 0.5f});
        if (a <= 0.0f) continue;
        switch (m.style) {
            case WorldMessage::Style::Hint: {
                TextStyle st = style(ui, 26, ui.theme().text.withAlphaF(a), FontFace::Body, Align::Center, VAlign::Middle);
                Vec2 sz = ui.text().measure(m.text, FontFace::Body, st.size);
                Rect bg = Rect::fromCenter({ui.screenW() * 0.5f, hintY}, {sz.x + ui.s(60), sz.y + ui.s(22)});
                ui.r().fillRoundedRect(bg, bg.h * 0.5f, Color{6, 10, 24, 200}.withAlphaF(0.8f * a));
                ui.r().drawRoundedRect(bg, bg.h * 0.5f, ui.theme().accent.withAlphaF(0.4f * a), ui.s(1.5f));
                ui.text().draw(m.text, {ui.screenW() * 0.5f, hintY}, st);
                hintY -= ui.s(64);
                break;
            }
            case WorldMessage::Style::Title:
            case WorldMessage::Style::Boss: {
                bool boss = m.style == WorldMessage::Style::Boss;
                float y = ui.screenH() * (boss ? 0.36f : 0.26f);
                float slide = (1.0f - std::min(1.0f, m.age / 0.4f)) * ui.s(30);
                TextStyle st = style(ui, boss ? 64 : 52, (boss ? Color{255, 120, 140, 255} : Color::white()).withAlphaF(a),
                                     FontFace::Title, Align::Center, VAlign::Middle);
                st.glow = ui.s(24);
                st.glowColor = (boss ? ui.theme().danger : ui.theme().accent).withAlphaF(0.35f * a);
                ui.r().fillGradientH({0, y - ui.s(60), ui.screenW() * 0.5f, ui.s(150)}, {0, 0, 0, 0}, Color{4, 8, 18, 170}.withAlphaF(a));
                ui.r().fillGradientH({ui.screenW() * 0.5f, y - ui.s(60), ui.screenW() * 0.5f, ui.s(150)}, Color{4, 8, 18, 170}.withAlphaF(a), {0, 0, 0, 0});
                ui.text().draw(m.text, {ui.screenW() * 0.5f + slide, y}, st);
                if (!m.subtitle.empty()) {
                    TextStyle sub = style(ui, 26, ui.theme().text.withAlphaF(a), FontFace::Body, Align::Center, VAlign::Top);
                    float maxW = ui.s(1100);
                    Vec2 sz = ui.text().measureWrapped(m.subtitle, FontFace::Body, sub.size, maxW);
                    sub.align = Align::Left;
                    ui.text().drawWrapped(m.subtitle, {ui.screenW() * 0.5f - sz.x * 0.5f, y + ui.s(44)}, maxW, sub);
                }
                // Power unlock: show the button for the new power.
                if (!m.speaker.empty()) {
                    Action act = actionFromId(m.speaker);
                    if (act != Action::Count) {
                        float h = ui.s(40);
                        Glyphs::drawHint(ui, act, "pour l'utiliser", {ui.screenW() * 0.5f - ui.s(110), y + ui.s(130)}, h);
                    }
                }
                break;
            }
            case WorldMessage::Style::Secret: {
                TextStyle st = style(ui, 34, Color{255, 217, 138, 255}.withAlphaF(a), FontFace::Title, Align::Center, VAlign::Middle);
                st.glow = ui.s(16);
                st.glowColor = Color{255, 217, 138, 60};
                ui.text().draw(m.text, {ui.screenW() * 0.5f, ui.screenH() * 0.2f}, st);
                break;
            }
            case WorldMessage::Style::Dialogue: {
                float bw = ui.s(1100), bh = ui.s(150);
                Rect box{(ui.screenW() - bw) * 0.5f, ui.screenH() - bh - ui.s(50), bw, bh};
                ui.r().fillRoundedRect(box, ui.s(14), Color{8, 12, 28, 230}.withAlphaF(a));
                ui.r().drawRoundedRect(box, ui.s(14), ui.theme().accent.withAlphaF(0.6f * a), ui.s(2));
                TextStyle sp = style(ui, 24, ui.theme().accent.withAlphaF(a), FontFace::Bold);
                ui.text().draw(m.speaker, {box.x + ui.s(28), box.y + ui.s(18)}, sp);
                TextStyle tx = style(ui, 24, ui.theme().text.withAlphaF(a), FontFace::Body);
                ui.text().drawWrapped(m.text, {box.x + ui.s(28), box.y + ui.s(56)}, box.w - ui.s(56), tx);
                break;
            }
        }
    }
}

void Hud::drawCinematic(UIContext& ui, GameWorld& w) {
    const CinematicPlayer& c = w.cinematic();
    float lb = c.letterbox();
    if (lb > 0.0f) {
        float h = ui.screenH() * 0.11f * applyEase(Ease::InOutCubic, lb);
        ui.r().fillRect({0, 0, ui.screenW(), h}, {0, 0, 0, 255});
        ui.r().fillRect({0, ui.screenH() - h, ui.screenW(), h}, {0, 0, 0, 255});
    }
    float ta = c.titleAlpha();
    if (ta > 0.0f && !c.title().empty()) {
        if (c.titleIsCaption()) {
            TextStyle st = style(ui, 30, ui.theme().text.withAlphaF(ta), FontFace::Body, Align::Center, VAlign::Middle);
            ui.text().draw(c.title(), {ui.screenW() * 0.5f, ui.screenH() * 0.82f}, st);
        } else {
            TextStyle st = style(ui, 74, Color::white().withAlphaF(ta), FontFace::Title, Align::Center, VAlign::Middle);
            st.glow = ui.s(30);
            st.glowColor = ui.theme().accent.withAlphaF(0.3f * ta);
            ui.text().draw(c.title(), {ui.screenW() * 0.5f, ui.screenH() * 0.42f}, st);
            if (!c.subtitle().empty()) {
                TextStyle sub = style(ui, 30, ui.theme().accent.withAlphaF(ta), FontFace::Body, Align::Center, VAlign::Middle);
                ui.text().draw(c.subtitle(), {ui.screenW() * 0.5f, ui.screenH() * 0.42f + ui.s(70)}, sub);
            }
        }
    }
    float da = c.dialogueAlpha();
    if (da > 0.0f && !c.dialogue().empty()) {
        float bw = ui.s(1100);
        Rect box{(ui.screenW() - bw) * 0.5f, ui.screenH() * 0.89f - ui.s(150), bw, ui.s(130)};
        ui.r().fillRoundedRect(box, ui.s(14), Color{8, 12, 28, 220}.withAlphaF(da));
        ui.r().drawRoundedRect(box, ui.s(14), ui.theme().accent.withAlphaF(0.5f * da), ui.s(2));
        TextStyle sp = style(ui, 24, ui.theme().accent.withAlphaF(da), FontFace::Bold);
        ui.text().draw(c.speaker(), {box.x + ui.s(28), box.y + ui.s(16)}, sp);
        TextStyle tx = style(ui, 24, ui.theme().text.withAlphaF(da), FontFace::Body);
        ui.text().drawWrapped(c.dialogue(), {box.x + ui.s(28), box.y + ui.s(52)}, box.w - ui.s(56), tx);
    }
    if (c.active() && c.skippable()) {
        float h = ui.s(30);
        Vec2 pos{ui.screenW() - ui.s(330), ui.screenH() - ui.s(40)};
        Glyphs::drawHint(ui, Action::MenuConfirm, "Maintenir pour passer", pos, h);
        if (skipHold_ > 0.0f)
            ui.r().fillRect({pos.x, pos.y + h * 0.7f, ui.s(290) * std::min(1.0f, skipHold_), ui.s(3)}, ui.theme().accent);
    }
    if (c.fade() > 0.0f)
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 255}.withAlphaF(c.fade()));
}

}  // namespace cosmic
