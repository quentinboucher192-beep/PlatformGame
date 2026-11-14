#include "game/gameplay/Hud.h"

#include <algorithm>
#include <cmath>

#include "engine/core/Application.h"
#include "game/enemies/Enemy.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/BossBadge.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/Powers.h"
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
    if (track_.valid()) {
        progress_ = track_.project(p.box.bottomCenter(), trackHint_);
        if (w.state() == WorldState::Completed || w.state() == WorldState::Outro) progress_ = 1.0f;
        bestProgress_ = std::max(bestProgress_, progress_);
        shownProgress_ = lerpf(shownProgress_, bestProgress_, damp(6.0f, dt));
    }
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
    if (float slow = w.timeSlowRemaining(); slow > 0.0f && !cinematic) {
        // Time slow: violet veil on the edges, and the seconds left under the energy bar.
        float k = std::min(1.0f, slow / 0.4f);
        Color v{150, 90, 255, 255};
        ui.r().fillGradientV({0, 0, ui.screenW(), ui.s(120)}, v.withAlpha(static_cast<uint8_t>(60 * k)), v.withAlpha(0));
        ui.r().fillGradientV({0, ui.screenH() - ui.s(120), ui.screenW(), ui.s(120)}, v.withAlpha(0), v.withAlpha(static_cast<uint8_t>(60 * k)));
        ui.r().fillGradientH({0, 0, ui.s(140), ui.screenH()}, v.withAlpha(static_cast<uint8_t>(50 * k)), v.withAlpha(0));
        ui.r().fillGradientH({ui.screenW() - ui.s(140), 0, ui.s(140), ui.screenH()}, v.withAlpha(0), v.withAlpha(static_cast<uint8_t>(50 * k)));
    }
    if (!cinematic) {
        auto at = [&](HudElement e) { return place(ui, w, settings, e); };
        if (hudVisible(settings, HudElement::Vitals)) {
            Place p = at(HudElement::Vitals);
            drawVitals(ui, w, p.o, p.k);
        }
        if (hudVisible(settings, HudElement::Powers)) {
            Place p = at(HudElement::Powers);
            drawPowers(ui, w, p.o, p.k);
        }
        if (hudVisible(settings, HudElement::Timer)) {
            Place p = at(HudElement::Timer);
            drawTimer(ui, w, record, p.o, p.k);
        }
        if (hudVisible(settings, HudElement::Collectibles)) {
            Place p = at(HudElement::Collectibles);
            drawCollectibles(ui, w, p.o, p.k);
        }
        if (hudVisible(settings, HudElement::Boss)) {
            Place p = at(HudElement::Boss);
            drawBoss(ui, w, p.o, p.k);
        }
        if (hudVisible(settings, HudElement::Progress)) {
            Place p = at(HudElement::Progress);
            drawProgress(ui, w, p.o, p.k, p.width);
        }
        if (settings.showInputHints) drawInteractPrompt(ui, w, view);
    }
    drawMessages(ui, w);
    drawCinematic(ui, w);
}

// ------------------------------------------------------------------ layout

namespace {
int heartsOf(const GameWorld& w) { return w.player().maxHealth(); }
float heartSize(int hearts) { return hearts > 12 ? 30.0f : (hearts > 8 ? 36.0f : 44.0f); }
int powerBoxes(const GameWorld& w) {
    return std::max(static_cast<int>(w.player().powers().size()), std::max(0, w.player().powerSlots()));
}
}  // namespace

Hud::Place Hud::place(UIContext& ui, const GameWorld& w, const GameplaySettings& s, HudElement e) const {
    const HudPlace& hp = s.hud[e];
    Place p;
    p.k = std::clamp(hp.scale, HudLayout::kMinScale, HudLayout::kMaxScale);
    float W = ui.screenW(), H = ui.screenH();
    bool custom = hp.custom && (e != HudElement::Progress || s.progressBar == 3);
    if (e == HudElement::Progress) p.width = std::min(W * 0.44f, ui.s(820)) * p.k;
    if (custom) {
        p.o = {hp.x * W, hp.y * H};
        return p;
    }
    switch (e) {
        case HudElement::Vitals: p.o = {ui.s(40), ui.s(34)}; break;
        case HudElement::Powers: p.o = {ui.s(40), ui.s(140)}; break;
        case HudElement::Collectibles: p.o = {W * 0.5f, ui.s(34)}; break;
        case HudElement::Timer: p.o = {W - ui.s(40), ui.s(30)}; break;
        case HudElement::Boss: p.o = {W * 0.5f, H - ui.s(86) * p.k}; break;
        case HudElement::Progress: {
            // Centred, never under the power icons (left) nor the timer (right).
            Place pw = place(ui, w, s, HudElement::Powers);
            Rect powers = hudVisible(s, HudElement::Powers) ? area(ui, w, HudElement::Powers, pw) : Rect{};
            float powersRight = powers.w > 0.0f ? powers.right() + ui.s(70) : 0.0f;
            float left = (W - p.width) * 0.5f;
            if (s.progressBar == 1 && left < powersRight) {
                float right = left + p.width;
                left = powersRight;
                p.width = std::max(ui.s(300), right - left);
            }
            float y = s.progressBar == 2 ? H - ui.s(w.activeBoss() ? 178.0f : 82.0f) : ui.s(150);
            p.o = {left + p.width * 0.5f, y};
            break;
        }
        default: break;
    }
    return p;
}

Rect Hud::area(UIContext& ui, const GameWorld& w, HudElement e, const Place& p) const {
    float k = p.k;
    auto S = [&](float v) { return ui.s(v) * k; };
    Vec2 o = p.o;
    switch (e) {
        case HudElement::Vitals: {
            int hearts = heartsOf(w);
            float hs = S(heartSize(hearts));
            float heartsW = static_cast<float>(hearts) * (hs + S(4)) - S(4);
            float barW = S(4) + S(32) + S(260.0f * std::clamp(w.player().config().maxEnergy / 100.0f, 1.0f, 1.8f)) + S(2);
            return {o.x - S(4), o.y - S(4), std::max(heartsW, barW) + S(8), hs + S(12) + S(16) + S(10)};
        }
        case HudElement::Powers: {
            int n = powerBoxes(w);
            if (n <= 0) return {};
            float textW = w.player().powerSlots() > 0 ? S(10) + ui.text().measure("8 / 8", FontFace::Bold, S(18)).x : 0.0f;
            return {o.x - S(6), o.y - S(6), static_cast<float>(n) * S(72) - S(18) + textW + S(12), S(54) + S(36) + S(10)};
        }
        case HudElement::Collectibles: return {o.x - S(153), o.y - S(8), S(306), S(50)};
        case HudElement::Timer: return {o.x - S(274), o.y - S(4), S(278), S(182)};
        case HudElement::Progress: return {o.x - p.width * 0.5f - S(36), o.y - S(50), p.width + S(36) + S(104), S(108)};
        case HudElement::Boss: return {o.x - S(456), o.y - S(44), S(912), S(78)};
        default: return {};
    }
}

Vec2 Hud::anchorForArea(UIContext& ui, const GameWorld& w, HudElement e, const Place& p, Vec2 topLeft) const {
    Rect a = area(ui, w, e, p);
    return p.o + (topLeft - a.pos());
}

// ------------------------------------------------------------------ elements

void Hud::drawVitals(UIContext& ui, GameWorld& w, Vec2 o, float k) {
    auto S = [&](float v) { return ui.s(v) * k; };
    const Player& p = w.player();
    float x = o.x, y = o.y;
    // Smaller hearts when the player has many of them (10 by default + heart containers).
    int hearts = p.maxHealth();
    float hs = S(heartSize(hearts));
    for (int i = 0; i < hearts; ++i) {
        bool full = i < p.health;
        Rect r{x + i * (hs + S(4)), y, hs, hs};
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
    // The bar grows with the energy reserve (150 since 2.5).
    float bw = S(260.0f * std::clamp(p.config().maxEnergy / 100.0f, 1.0f, 1.8f)), bh = S(16);
    Rect frame{x + S(4), y + hs + S(12), bw, bh};
    ui.icon(kHudIcons, Energy, 16, {frame.x - S(2), frame.center().y - S(14), S(28), S(28)});
    frame.x += S(32);
    Renderer& r = ui.r();
    r.fillRoundedRect(frame.expanded(S(2)), S(6), {6, 10, 22, 200});
    float fillK = std::clamp(displayedEnergy_ / p.config().maxEnergy, 0.0f, 1.0f);
    float kReal = std::clamp(p.energy / p.config().maxEnergy, 0.0f, 1.0f);
    Rect fill{frame.x, frame.y, frame.w * fillK, frame.h};
    r.fillRoundedRectGradient(fill, S(5), {143, 255, 240, 255}, {31, 184, 176, 255});
    if (kReal < 0.25f) r.fillRoundedRect(fill, S(5), Color{255, 77, 109, static_cast<uint8_t>(100 + 80 * std::sin(time_ * 8))});
    r.drawRoundedRect(frame.expanded(S(2)), S(6), {57, 240, 216, 120}, S(1.5f));
    // Power cost markers
    for (const auto& pw : p.powers()) {
        float c = pw->def().energyCost / p.config().maxEnergy;
        if (c > 0.02f && c < 1.0f) r.fillRect({frame.x + frame.w * c, frame.y, S(1.5f), frame.h}, {255, 255, 255, 60});
    }
}

void Hud::drawPowers(UIContext& ui, GameWorld& w, Vec2 o, float k) {
    auto S = [&](float v) { return ui.s(v) * k; };
    const Player& p = w.player();
    if (p.powers().empty() && p.powerSlots() <= 0) return;
    float size = S(54);
    float x = o.x, y = o.y;
    Renderer& r = ui.r();
    for (const auto& pw : p.powers()) {
        Rect box{x, y, size, size};
        bool ready = pw->ready(p);
        bool enough = p.energy + 0.01f >= pw->def().energyCost;
        r.fillRoundedRect(box.expanded(S(4)), S(10), {8, 12, 26, 210});
        ui.icon("ui/icons_powers.png", pw->def().icon, 32, box, ready ? Color::white() : Color{110, 120, 140, 255});
        float cd = pw->cooldownRatio();
        if (cd > 0.0f) r.fillRect({box.x, box.y + box.h * (1.0f - cd), box.w, box.h * cd}, {0, 0, 0, 140});
        if (!enough) r.drawRoundedRect(box.expanded(S(4)), S(10), {255, 77, 109, 160}, S(2));
        else if (pw->active()) {
            r.drawRoundedRect(box.expanded(S(4)), S(10), pw->def().color, S(3));
            r.drawGlow(box.center(), size, pw->def().color.withAlpha(60));
        } else if (ready) {
            r.drawRoundedRect(box.expanded(S(4)), S(10), {57, 240, 216, 90}, S(1.5f));
        }
        if (!pw->passive())
            Glyphs::drawAction(ui, pw->def().input, {box.center().x - S(14), box.bottom() + S(22)}, S(28));
        x += size + S(18);
    }
    // Power slots of the level: the free ones are shown empty, with the count.
    int slots = p.powerSlots();
    if (slots <= 0) return;
    int carried = static_cast<int>(p.powers().size());
    for (int i = carried; i < slots; ++i) {
        Rect box{x, y, size, size};
        r.fillRoundedRect(box.expanded(S(4)), S(10), {8, 12, 26, 120});
        r.drawRoundedRect(box.expanded(S(4)), S(10), {140, 160, 185, 70}, S(1.5f));
        r.fillRect({box.center().x - S(8), box.center().y - S(1), S(16), S(2)}, {140, 160, 185, 70});
        r.fillRect({box.center().x - S(1), box.center().y - S(8), S(2), S(16)}, {140, 160, 185, 70});
        x += size + S(18);
    }
    TextStyle cs = style(ui, k * 18, carried >= slots ? ui.theme().warm : ui.theme().textDim, FontFace::Bold, Align::Left);
    ui.text().draw(std::to_string(carried) + " / " + std::to_string(slots), {x, y + size * 0.5f - S(11)}, cs);
}

void Hud::drawTimer(UIContext& ui, GameWorld& w, const LevelRecord* record, Vec2 o, float k) {
    auto S = [&](float v) { return ui.s(v) * k; };
    const LevelTimer& t = w.timer();
    const LevelData& lvl = w.level();
    float x = o.x, y = o.y;
    bool limited = t.hasLimit();
    float shown = limited ? t.remaining() : t.elapsed();
    Color c = ui.theme().text;
    if (limited && t.remaining() < 30.0f) {
        float pulse = 0.5f + 0.5f * std::sin(time_ * (t.remaining() < 10.0f ? 14.0f : 6.0f));
        c = Color::lerp(ui.theme().warm, ui.theme().danger, pulse);
    }
    TextStyle st = style(ui, k * 46, c, FontFace::Title, Align::Right);
    st.glow = S(10);
    st.glowColor = c.withAlpha(40);
    ui.text().draw(LevelTimer::format(shown), {x, y}, st);
    ui.icon(kHudIcons, Clock, 16, {x - S(270), y + S(10), S(32), S(32)}, Color{255, 255, 255, 200});
    float line = y + S(62);
    TextStyle small = style(ui, k * 20, ui.theme().textDim, FontFace::Body, Align::Right);
    if (limited) {
        ui.text().draw("Temps écoulé " + LevelTimer::format(t.elapsed(), false), {x, line}, small);
        line += S(26);
    }
    if (record && record->bestTime > 0.0f) {
        ui.text().draw("Record " + LevelTimer::format(record->bestTime), {x, line}, small);
        line += S(26);
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
            line += S(26);
            break;
        }
    }
    if (w.splitFlashTimer() > 0.0f) {
        float d = w.lastSplitDelta();
        TextStyle ds = style(ui, k * 28, d <= 0 ? ui.theme().success : ui.theme().danger, FontFace::Bold, Align::Right);
        ds.color.a = static_cast<uint8_t>(255 * std::min(1.0f, w.splitFlashTimer()));
        ui.text().draw(LevelTimer::formatDelta(d), {x, line + S(4)}, ds);
    }
}

void Hud::drawCollectibles(UIContext& ui, GameWorld& w, Vec2 o, float k) {
    auto S = [&](float v) { return ui.s(v) * k; };
    const RunStats& s = w.stats();
    if (s.totalShards == 0 && s.totalRelics == 0) return;  // nothing to collect in this level
    float cx = o.x;
    float y = o.y;
    float is = S(34) * (1.0f + 0.25f * shardPulse_);
    std::string shards = std::to_string(s.shards) + " / " + std::to_string(s.totalShards);
    std::string relics = std::to_string(s.relics) + " / " + std::to_string(s.totalRelics);
    TextStyle st = style(ui, k * 26, ui.theme().text, FontFace::Bold, Align::Left, VAlign::Middle);
    float total = S(250);
    float x = cx - total * 0.5f;
    ui.r().fillRoundedRect({x - S(14), y - S(6), total + S(28), S(46)}, S(23), {6, 10, 22, 150});
    ui.icon(kHudIcons, Shard, 16, Rect::fromCenter({x + S(17), y + S(17)}, {is, is}));
    ui.text().draw(shards, {x + S(40), y + S(17)}, st);
    if (s.totalRelics > 0) {
        ui.icon(kHudIcons, Relic, 16, {x + S(140), y, S(34), S(34)});
        st.color = s.relics > 0 ? Color{255, 217, 138, 255} : ui.theme().textDim;
        ui.text().draw(relics, {x + S(180), y + S(17)}, st);
    }
}

void Hud::drawProgress(UIContext& ui, GameWorld& w, Vec2 o, float k, float width) {
    auto S = [&](float v) { return ui.s(v) * k; };
    if (!track_.valid()) return;
    Renderer& r = ui.r();
    const Color gold{255, 217, 138, 255}, cyan{57, 240, 216, 255}, ember{255, 138, 92, 255}, violet{176, 76, 255, 255};
    const Color red{255, 77, 109, 255}, green{120, 230, 140, 255};
    constexpr float kPi = 3.14159265f;
    float t = time_;
    // Rail centred on the element's anchor (its place and width come from the HUD layout).
    float railH = S(14);
    Rect rail{o.x - width * 0.5f, o.y - railH * 0.5f, width, railH};
    auto xAt = [&](float f) { return rail.x + rail.w * std::clamp(f, 0.0f, 1.0f); };
    float fill = shownProgress_;
    float headX = xAt(fill);

    // No frame around the bar (the player's choice): only a halo that breathes along the rail,
    // so it stays readable over any background.
    float breathe = 0.75f + 0.25f * std::sin(t * 1.6f);
    for (float x = rail.x; x <= rail.right(); x += S(48)) {
        float k = (x - rail.x) / std::max(1.0f, rail.w);
        r.drawGlow({x, rail.center().y}, S(46), Color::lerp(cyan, violet, k).withAlpha(static_cast<uint8_t>(34 * breathe)));
    }
    r.drawGlow({rail.right() + S(30), rail.center().y}, S(70), violet.withAlpha(static_cast<uint8_t>(60 * breathe)));

    // Header: section on the left, percentage on the right.
    std::string section = w.level().name;
    for (const auto& m : track_.markers)
        if (m.kind == LevelProgressTrack::Marker::Kind::Checkpoint && m.at <= bestProgress_ + 0.002f && !m.label.empty()) section = m.label;
    int pct = static_cast<int>(std::floor(shownProgress_ * 100.0f + 0.001f));
    TextStyle ps = style(ui, k * 26, gold, FontFace::Title, Align::Right, VAlign::Bottom);
    ps.glow = S(16);
    ps.glowColor = gold.withAlpha(140);
    ps.shadow = true;
    std::string pctText = std::to_string(pct) + " %";
    float pctW = ui.text().measure(pctText, FontFace::Title, ps.size).x;
    float headerY = rail.y - S(14);
    ui.text().draw(pctText, {rail.right() + S(62), headerY + S(2)}, ps);
    float room = rail.w + S(62) - pctW - S(30);
    TextStyle ss = style(ui, k * ui.fitFont(section, FontFace::Bold, 16, room / k, 0.6f), Color{214, 226, 242, 255}, FontFace::Bold, Align::Left,
                         VAlign::Bottom);
    ss.shadow = true;
    ui.text().draw(section, {rail.x, headerY}, ss);

    // Aura of the filled part: a wide bloom brighter towards the head, and sparks rising off it.
    for (float x = rail.x; x <= headX; x += S(18)) {
        float k = (x - rail.x) / std::max(1.0f, headX - rail.x);
        float flick = 0.85f + 0.15f * std::sin(t * 6.0f + x * 0.05f);
        r.drawGlow({x, rail.center().y}, S(54), Color::lerp(cyan, gold, k).withAlpha(static_cast<uint8_t>((30 + 60 * k) * flick)));
    }
    for (int i = 0; i < 14; ++i) {
        float seed = static_cast<float>(i) * 0.6180339f;
        float life = std::fmod(t * (0.55f + 0.25f * std::fmod(seed * 7.0f, 1.0f)) + seed, 1.0f);
        float px = rail.x + (headX - rail.x) * std::fmod(seed * 3.7f + 0.13f, 1.0f);
        if (px > headX) continue;
        float py = rail.center().y - life * S(34);
        Color c = Color::lerp(cyan, gold, (px - rail.x) / std::max(1.0f, headX - rail.x));
        r.fillCircle({px + std::sin(t * 3.0f + seed * 9.0f) * S(3), py}, S(1.8f) * (1.0f - life * 0.6f),
                     c.withAlpha(static_cast<uint8_t>(230 * (1.0f - life))));
    }
    // Rail
    r.fillRoundedRect(rail.expanded(S(5)), S(11), {5, 8, 20, 225});
    r.drawRoundedRect(rail.expanded(S(5)), S(11), gold.withAlpha(150), S(2));
    for (int k = 1; k < 10; ++k) {
        float x = rail.x + rail.w * static_cast<float>(k) / 10.0f;
        if (x > headX) r.fillRect({x - S(0.75f), rail.y + S(3), S(1.5f), railH - S(6)}, violet.withAlpha(110));
    }
    r.fillGradientH({headX, rail.y + S(2), std::max(0.0f, rail.right() - headX), railH - S(4)}, violet.withAlpha(30), violet.withAlpha(90));
    // Fill: cyan -> gold -> ember, rounded start, light running along it.
    if (headX > rail.x + railH * 0.5f) {
        float mid = rail.x + (headX - rail.x) * 0.6f;
        r.fillGradientH({rail.x + railH * 0.5f, rail.y, std::max(0.0f, mid - rail.x - railH * 0.5f), railH}, cyan, gold);
        r.fillGradientH({mid, rail.y, std::max(0.0f, headX - mid), railH}, gold, ember);
        r.fillCircle({rail.x + railH * 0.5f, rail.center().y}, railH * 0.5f, cyan);
        r.fillRect({rail.x + railH * 0.5f, rail.y + S(2), std::max(0.0f, headX - rail.x - railH), S(2)}, Color{255, 255, 255, 70},
                   BlendMode::Additive);
        for (int i = 0; i < 2; ++i) {
            float run = std::fmod(t * 0.4f + 0.5f * static_cast<float>(i), 1.0f);
            float rx = rail.x + (headX - rail.x) * run;
            r.drawGlow({rx, rail.center().y}, S(24), Color{255, 255, 255, 110});
        }
    }

    using Kind = LevelProgressTrack::Marker::Kind;
    // Icons of ambushes and bosses never cover each other: nudged apart along the rail.
    std::vector<float> iconX(track_.markers.size(), 0.0f);
    {
        float minGap = S(34), last = -1e9f;
        for (size_t i = 0; i < track_.markers.size(); ++i) {
            float x = xAt(track_.markers[i].at);
            if (track_.markers[i].kind == Kind::Checkpoint) {
                iconX[i] = x;
                continue;
            }
            x = std::max(x, last + minGap);
            iconX[i] = x;
            last = x;
        }
        float overflow = last - (rail.right() - S(6));
        if (overflow > 0.0f)
            for (size_t i = track_.markers.size(); i-- > 0;) {
                if (track_.markers[i].kind == Kind::Checkpoint) continue;
                iconX[i] -= overflow;
                float limit = iconX[i] - minGap;
                overflow = 0.0f;
                for (size_t j = i; j-- > 0;)
                    if (track_.markers[j].kind != Kind::Checkpoint) {
                        overflow = std::max(0.0f, iconX[j] - limit);
                        break;
                    }
                if (overflow <= 0.0f) break;
            }
    }
    std::vector<std::pair<float, float>> labelSpans;  // no two labels overlap
    auto label = [&](const std::string& text, float x, Color c) {
        float fs = 14.0f;
        float tw = ui.text().measure(text, FontFace::Bold, S(fs)).x;
        float a = x - tw * 0.5f, b = x + tw * 0.5f;
        for (const auto& [l, rr] : labelSpans)
            if (a < rr + S(10) && b > l - S(10)) return;
        labelSpans.push_back({a, b});
        TextStyle ls = style(ui, k * fs, c, FontFace::Bold, Align::Center, VAlign::Top);
        ls.glow = S(10);
        ls.glowColor = c.withAlpha(110);
        ls.shadow = true;
        ui.text().draw(text, {x, rail.center().y + S(34)}, ls);
    };
    auto nearMajor = [&](float x) {
        for (size_t i = 0; i < track_.markers.size(); ++i)
            if (track_.markers[i].kind != Kind::Checkpoint && std::abs(iconX[i] - x) < S(20)) return true;
        return false;
    };
    // Checkpoints first (small), then ambushes and bosses on top; boss labels win.
    for (size_t i = 0; i < track_.markers.size(); ++i) {
        const auto& m = track_.markers[i];
        if (m.kind != Kind::Checkpoint || nearMajor(iconX[i])) continue;
        Vec2 c{iconX[i], rail.center().y};
        bool on = w.checkpointId() == m.objectId || m.at <= bestProgress_ + 0.002f;
        float s = S(8);
        if (on) r.drawGlow(c, S(22), cyan.withAlpha(140));
        std::vector<Vec2> o = {{c.x, c.y - s - S(2)}, {c.x + s + S(2), c.y}, {c.x, c.y + s + S(2)}, {c.x - s - S(2), c.y}};
        std::vector<Vec2> d = {{c.x, c.y - s}, {c.x + s, c.y}, {c.x, c.y + s}, {c.x - s, c.y}};
        r.fillConvexPolygon(o, on ? cyan : Color{90, 106, 144, 255});
        r.fillConvexPolygon(d, on ? Color{210, 255, 250, 255} : Color{10, 14, 30, 255});
    }
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < track_.markers.size(); ++i) {
            const auto& m = track_.markers[i];
            if (m.kind == Kind::Checkpoint || (pass == 0) != (m.kind == Kind::Boss)) continue;
            float x = iconX[i];
            Vec2 c{x, rail.center().y};
            if (std::abs(x - xAt(m.at)) > S(2))  // nudged: a thin tick shows the real place
                r.fillRect({xAt(m.at) - S(1), rail.y - S(6), S(2), railH + S(12)}, Color{255, 255, 255, 110});
            if (m.kind == Kind::Ambush) {
                auto* zone = dynamic_cast<CombatZoneEntity*>(w.entity(m.objectId));
                bool cleared = zone && zone->cleared();
                bool active = zone && zone->started() && !cleared;
                Color col = m.color;
                if (cleared) col = Color{150, 160, 180, 255};
                float pulse = active ? 1.0f + 0.3f * std::sin(t * 9.0f) : (cleared ? 1.0f : 1.0f + 0.15f * std::sin(t * 3.0f));
                if (!cleared) {
                    r.drawGlow(c, S(50) * pulse, col.withAlpha(active ? 230 : 170));
                    r.drawGlow(c, S(22), Color{255, 255, 255, 60});
                }
                // Skull: cranium, cheekbones, jaw with teeth, glowing eye sockets.
                float k = S(1.45f) * (active ? 1.0f + 0.08f * std::sin(t * 9.0f) : 1.0f);
                Vec2 hc = c - Vec2{0, 2.5f * k};
                Color bone = cleared ? Color{170, 178, 196, 255} : Color::lerp(col, Color::white(), 0.55f);
                r.fillCircle(hc, 10.5f * k, col);
                r.fillCircle(hc, 9.0f * k, bone);
                r.fillRoundedRect({c.x - 6.5f * k, c.y + 3.0f * k, 13.0f * k, 7.5f * k}, 2.5f * k, bone);
                Color hole{16, 6, 14, 255};
                r.fillCircle(hc + Vec2{-3.6f * k, 0.8f * k}, 2.9f * k, hole);
                r.fillCircle(hc + Vec2{3.6f * k, 0.8f * k}, 2.9f * k, hole);
                if (!cleared) {
                    Color eye = Color::lerp(col, Color::white(), 0.25f);
                    r.fillCircle(hc + Vec2{-3.6f * k, 0.8f * k}, 1.3f * k * pulse, eye);
                    r.fillCircle(hc + Vec2{3.6f * k, 0.8f * k}, 1.3f * k * pulse, eye);
                }
                r.fillTriangle({c.x, c.y + 0.4f * k}, {c.x - 1.4f * k, c.y + 3.2f * k}, {c.x + 1.4f * k, c.y + 3.2f * k}, hole);
                for (int tooth = -1; tooth <= 1; ++tooth) r.fillRect({c.x + tooth * 2.8f * k - 0.5f * k, c.y + 5.6f * k, 1.0f * k, 4.0f * k}, hole);
                if (cleared) {
                    // Green badge with a check mark.
                    Vec2 b = c + Vec2{9.0f * k, 7.0f * k};
                    r.drawGlow(b, S(14), green.withAlpha(150));
                    r.fillCircle(b, 6.0f * k, green);
                    r.drawLine(b + Vec2{-3.0f * k, 0}, b + Vec2{-0.8f * k, 2.4f * k}, {12, 30, 18, 255}, S(1.8f));
                    r.drawLine(b + Vec2{-0.8f * k, 2.4f * k}, b + Vec2{3.2f * k, -2.6f * k}, {12, 30, 18, 255}, S(1.8f));
                }
                Color lc = cleared ? green : Color::lerp(col, Color::white(), 0.35f);
                label(cleared ? "nettoyée" : active ? "en cours !" : m.label, x, lc);
            } else {
                bool alive = false;
                for (const auto& e : w.enemies())
                    if (e->objectId() == m.objectId && e->alive()) alive = true;
                const EnemyDefinition* def = w.content().enemy(m.enemyId);
                Color col = !alive ? Color{140, 150, 175, 255} : def ? BossBadge::color(*def) : gold;
                float rad = S(17);
                if (alive) {
                    float pulse = 1.0f + 0.2f * std::sin(t * 4.0f);
                    r.drawGlow(c, S(80) * pulse, Color::lerp(col, ember, 0.4f).withAlpha(140));
                    r.drawGlow(c, S(40), col.withAlpha(190));
                    // Rays turning around the boss.
                    for (int k = 0; k < 8; ++k) {
                        float a = t * 0.8f + static_cast<float>(k) * kPi / 4.0f;
                        Vec2 dir{std::cos(a), std::sin(a)};
                        r.drawLine(c + dir * (rad + S(6)), c + dir * (rad + S(16 + 5 * std::sin(t * 3.0f + k))), col.withAlpha(190),
                                   S(2.5f));
                    }
                }
                if (def) BossBadge::draw(r, w.resources(), *def, c, rad, !alive, t);
                label(alive ? m.label : m.label + " — vaincu", x, col);
            }
        }

    // Exit portal at the end of the rail: turning rings in a violet glow.
    {
        Vec2 c{rail.right() + S(22), rail.center().y};
        bool done = shownProgress_ >= 0.995f;
        float pulse = 1.0f + 0.12f * std::sin(t * 2.0f);
        r.drawGlow(c, S(84) * pulse, violet.withAlpha(done ? 240 : 180));
        r.drawGlow(c, S(36), cyan.withAlpha(170));
        r.drawRing(c, S(18), S(3.5f), violet);
        r.drawRing(c, S(22), S(1.5f), violet.withAlpha(140));
        for (int k = 0; k < 3; ++k) {
            float a0 = t * 2.4f + static_cast<float>(k) * 2.0943951f;
            r.drawArc(c, S(13), S(3), a0, a0 + 1.3f, cyan);
            float a1 = -t * 1.6f + static_cast<float>(k) * 2.0943951f;
            r.drawArc(c, S(8), S(2), a1, a1 + 1.0f, Color{230, 200, 255, 255});
        }
        r.fillCircle(c, S(4), Color{240, 255, 252, 255});
        label("portail", c.x, Color{212, 166, 255, 255});
    }

    // The player: bright head, pulsing halo, expanding ring, orbiting sparks and a trail.
    {
        Vec2 h{xAt(std::clamp(progress_, 0.0f, 1.0f)), rail.center().y};
        for (int i = 1; i <= 8; ++i) {
            float bx = h.x - static_cast<float>(i) * S(8);
            if (bx < rail.x) break;
            float k = 1.0f - static_cast<float>(i) / 9.0f;
            r.fillCircle({bx, h.y + std::sin(t * 7.0f + static_cast<float>(i)) * S(2)}, S(3.2f) * k,
                         gold.withAlpha(static_cast<uint8_t>(220 * k)));
        }
        float pulse = 1.0f + 0.15f * std::sin(t * 5.0f);
        r.drawGlow(h, S(72) * pulse, gold.withAlpha(190));
        r.drawGlow(h, S(28), Color{255, 255, 255, 170});
        // Lens flare: a thin horizontal streak through the head.
        float fl = S(110) * pulse;
        r.fillGradientH({h.x - fl, h.y - S(1.5f), fl, S(3)}, Color{255, 240, 200, 0}, Color{255, 250, 235, 230}, BlendMode::Additive);
        r.fillGradientH({h.x, h.y - S(1.5f), fl, S(3)}, Color{255, 250, 235, 230}, Color{255, 240, 200, 0}, BlendMode::Additive);
        float wave = std::fmod(t, 1.2f) / 1.2f;
        r.drawRing(h, S(12) + S(22) * wave, S(2.5f), gold.withAlpha(static_cast<uint8_t>(220 * (1.0f - wave))));
        for (int k = 0; k < 4; ++k) {
            float a = t * 3.0f + static_cast<float>(k) * kPi * 0.5f;
            r.fillCircle(h + Vec2{std::cos(a), std::sin(a) * 0.6f} * S(17), S(2.2f), Color{255, 240, 200, 230});
        }
        r.fillCircle(h, S(11), gold);
        r.fillCircle(h, S(8), Color{255, 252, 236, 255});
    }
}

void Hud::drawBoss(UIContext& ui, GameWorld& w, Vec2 o, float k) {
    auto S = [&](float v) { return ui.s(v) * k; };
    Enemy* b = w.activeBoss();
    if (!b || !b->alive()) return;
    float bw = S(900), bh = S(22);
    Rect bar{o.x - bw * 0.5f, o.y, bw, bh};
    Renderer& r = ui.r();
    r.fillRoundedRect(bar.expanded(S(4)), S(8), {6, 8, 18, 220});
    r.fillRoundedRect({bar.x, bar.y, bar.w * bossBarDelayed_, bar.h}, S(6), {255, 230, 230, 200});
    r.fillRoundedRectGradient({bar.x, bar.y, bar.w * b->hpRatio(), bar.h}, S(6), {255, 120, 140, 255}, {163, 18, 58, 255});
    // Phase thresholds
    for (const auto& ph : b->def().phases)
        if (ph.hpThreshold < 0.999f) r.fillRect({bar.x + bar.w * ph.hpThreshold - S(1), bar.y - S(4), S(3), bar.h + S(8)}, {255, 217, 138, 220});
    r.drawRoundedRect(bar.expanded(S(4)), S(8), {255, 77, 109, 160}, S(2));
    TextStyle st = style(ui, k * 26, {255, 230, 235, 255}, FontFace::Title, Align::Center, VAlign::Bottom);
    std::string title = b->def().bossTitle.empty() ? b->def().name : b->def().bossTitle;
    ui.text().draw(title, {bar.center().x, bar.y - S(10)}, st);
}

void Hud::drawInteractPrompt(UIContext& ui, GameWorld& w, const WorldView& view) {
    Player& player = w.player();
    if (!player.alive()) return;
    const LevelEntity* e = w.nearestInteractable();
    const char* prompt = e ? e->interactPrompt() : "";
    // Grapple anchors: prompt when nothing else can be used (or in the air).
    if ((!e || !player.grounded()) && !player.movementPower()) {
        if (const Power* g = player.power("grapple"))
            if (const LevelEntity* t = GrapplePower::findTarget(player, w, g->def().param("range", 170.0f))) {
                e = t;
                prompt = "Grappin";
            }
    }
    if (!e) return;
    Vec2 sp = view.toScreen({e->box.center().x, e->box.y - 10});
    float h = ui.s(34);
    TextStyle st = style(ui, 22, ui.theme().text, FontFace::Bold, Align::Left, VAlign::Middle);
    float tw = ui.text().measure(prompt, FontFace::Bold, st.size).x;
    float total = h + ui.s(10) + tw;
    float bob = std::sin(time_ * 4.0f) * ui.s(3);
    Vec2 pos{sp.x - total * 0.5f, sp.y - h + bob};
    ui.r().fillRoundedRect({pos.x - ui.s(10), pos.y - h * 0.5f - ui.s(6), total + ui.s(20), h + ui.s(12)}, ui.s(10), {6, 10, 22, 190});
    Glyphs::drawHint(ui, Action::Interact, prompt, pos, h);
}

void Hud::drawMessages(UIContext& ui, GameWorld& w) {
    float hintY = ui.screenH() - ui.s(w.activeBoss() ? 210 : 150);
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
