#include "game/gameplay/Hud.h"

#include <algorithm>
#include <cmath>

#include "engine/core/Application.h"
#include "game/enemies/Enemy.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/BossBadge.h"
#include "game/gameplay/EnemyArrows.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/HudFade.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/Powers.h"
#include "game/records/Records.h"
#include "engine/resources/ResourceManager.h"
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
    if (track_.valid() && w.coop()) {
        // 2.62: the team's progress is its most advanced player's (a soul counts where it waits).
        teamHints_.resize(static_cast<size_t>(w.teamSize()), 0);
        teamAt_.assign(static_cast<size_t>(w.teamSize()), 0.0f);
        progress_ = 0.0f;
        for (int i = 0; i < w.teamSize(); ++i) {
            const GameWorld::TeamMember& m = w.member(i);
            if (m.presence == GameWorld::Presence::Gone) continue;
            const Vec2 at = m.presence == GameWorld::Presence::Down ? m.soul : w.playerAt(i).box.bottomCenter();
            teamAt_[static_cast<size_t>(i)] = track_.project(at, teamHints_[static_cast<size_t>(i)]);
            progress_ = std::max(progress_, teamAt_[static_cast<size_t>(i)]);
        }
        if (w.state() == WorldState::Completed || w.state() == WorldState::Outro) progress_ = 1.0f;
        bestProgress_ = std::max(bestProgress_, progress_);
        shownProgress_ = lerpf(shownProgress_, bestProgress_, damp(6.0f, dt));
    } else if (track_.valid()) {
        progress_ = track_.project(p.box.bottomCenter(), trackHint_);
        if (w.state() == WorldState::Completed || w.state() == WorldState::Outro) progress_ = 1.0f;
        bestProgress_ = std::max(bestProgress_, progress_);
        shownProgress_ = lerpf(shownProgress_, bestProgress_, damp(6.0f, dt));
    }
    if (Enemy* b = w.activeBoss()) bossBarDelayed_ = approach(bossBarDelayed_, b->hpRatio(), dt * 0.35f);
    else bossBarDelayed_ = 1.0f;
    // 2.60: each block toward its opacity (faded while something important is under it).
    for (size_t i = 0; i < fade_.size(); ++i) fade_[i] = HudFade::step(fade_[i], covered_[i], dt);

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

namespace {
/// Where an animated sprite shows on screen: the visible pixels of the frame drawn now.
Rect spriteOnScreen(ResourceManager& res, const Animator& a, Vec2 feet, bool flip, float scale, const WorldView& view) {
    const AnimationSetPtr& set = a.set();
    if (!set) return {};
    const Rect src = a.sourceRect();
    const Rect seen = res.visibleBox(*set, src);
    const Vec2 frame{static_cast<float>(set->frameWidth), static_cast<float>(set->frameHeight)};
    return view.toScreen(HudFade::visiblePart(a.destRect(feet, flip, scale), frame, seen, flip));
}
}  // namespace

Rect Hud::novaOnScreen(GameWorld& w, const WorldView& view) {
    const Player& p = w.player();
    if (p.state() == PlayerState::Hidden) return {};
    return spriteOnScreen(w.resources(), p.anim, p.box.bottomCenter(), p.facing < 0.0f, 1.0f, view);
}

Rect Hud::bossOnScreen(GameWorld& w, const WorldView& view) {
    const Enemy* b = w.activeBoss();
    if (!b || !b->alive() || b->dormant()) return {};
    return spriteOnScreen(w.resources(), b->animator(), b->box.bottomCenter(), b->facing < 0.0f, b->def().scale, view);
}

void Hud::warmUp(GameWorld& w) {
    // The sheets are read once (a few milliseconds each): while the level loads, not in the fight.
    ResourceManager& res = w.resources();
    auto read = [&](const AnimationSetPtr& set) {
        if (set && !set->clips.empty()) res.visibleBox(*set, set->frameRect(set->clips.begin()->second, 0));
    };
    read(w.player().anim.set());
    for (const auto& o : w.level().objects) {
        if (o.type != "enemy" || !o.prop<bool>("boss", false)) continue;
        const EnemyDefinition* def = w.enemyDefinition(o.propStr("enemy"));
        if (!def) continue;
        if (!def->assetRoot.empty()) res.pushMount(def->assetRoot);
        read(res.animation(def->animation));
        if (!def->assetRoot.empty()) res.popMount();
    }
}

CoverageProbe* Hud::threatProbe(const GameWorld& w, const GameplaySettings& s) {
    probeArmed_ = false;
    const Enemy* b = w.activeBoss();
    if (!s.fadeHud || !fadeAllowed_ || !b || !b->alive()) return nullptr;
    probe_.watch(drawn_.data(), drawn_.size());
    probe_.ignore(static_cast<size_t>(HudElement::Boss));  // the boss's bar gives way to Nova only
    probeArmed_ = true;
    return &probe_;
}

template <class F>
void Hud::drawBlock(UIContext& ui, int block, F&& draw) {
    DrawGroup group(ui.r(), fade_[static_cast<size_t>(block)]);
    draw();
    drawn_[static_cast<size_t>(block)] = group.area();
}

void Hud::render(UIContext& ui, GameWorld& w, const WorldView& view, const LevelRecord* record,
                 const GameplaySettings& settings) {
    bool cinematic = w.cinematic().active() || w.state() == WorldState::Intro || w.state() == WorldState::Outro;
    {
        // 2.60: what is under each block, where it was drawn the last frame: Nova, the boss, an
        // attack of the boss (the probe armed before the world was drawn). update() fades them.
        const bool fading = settings.fadeHud && fadeAllowed_ && !cinematic;
        // 2.62: in a team, every player in play counts as Nova does alone.
        Rect novas[GameWorld::kMaxTeam];
        int seen = 1;
        if (fading && w.coop()) {
            seen = 0;
            for (int k = 0; k < w.teamSize(); ++k) {
                if (w.member(k).presence != GameWorld::Presence::Playing) continue;
                GameWorld::PlayerScope who(w, k);
                novas[seen++] = novaOnScreen(w, view);
            }
        } else if (fading) {
            novas[0] = novaOnScreen(w, view);
        }
        const Rect boss = fading ? bossOnScreen(w, view) : Rect{};
        for (int i = 0; i < kFadeBlocks; ++i) {
            const bool attack = probeArmed_ && probe_.touched(static_cast<size_t>(i));
            const bool bar = i == static_cast<int>(HudElement::Boss);
            bool hidden = HudFade::covered(drawn_[static_cast<size_t>(i)], seen > 0 ? novas[0] : Rect{}, boss, attack, bar);
            for (int k = 1; k < seen && !hidden; ++k) hidden = HudFade::covered(drawn_[static_cast<size_t>(i)], novas[k], boss, attack, bar);
            covered_[static_cast<size_t>(i)] = fading && hidden;
        }
        probeArmed_ = false;
        drawn_.fill(Rect{});
    }
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
        auto id = [](HudElement e) { return static_cast<int>(e); };
        teamPanel_ = {};
        if (w.coop()) {
            // 2.62: the team panel (every player's hearts, energy, powers and state) in place of
            // the vitals and the powers of one player (it fades as the vitals would).
            drawBlock(ui, id(HudElement::Vitals), [&] { drawTeamPanel(ui, w); });
        } else if (hudVisible(settings, HudElement::Vitals)) {
            Place p = at(HudElement::Vitals);
            drawBlock(ui, id(HudElement::Vitals), [&] { drawVitals(ui, w, p.o, p.k); });
        }
        if (!w.coop() && hudVisible(settings, HudElement::Powers)) {
            Place p = at(HudElement::Powers);
            drawBlock(ui, id(HudElement::Powers), [&] { drawPowers(ui, w, p.o, p.k); });
        }
        if (hudVisible(settings, HudElement::Timer)) {
            Place p = at(HudElement::Timer);
            drawBlock(ui, id(HudElement::Timer), [&] { drawTimer(ui, w, record, p.o, p.k); });
        }
        if (hudVisible(settings, HudElement::Collectibles)) {
            Place p = at(HudElement::Collectibles);
            drawBlock(ui, id(HudElement::Collectibles), [&] { drawCollectibles(ui, w, p.o, p.k); });
        }
        if (hudVisible(settings, HudElement::Boss)) {
            Place p = at(HudElement::Boss);
            drawBlock(ui, id(HudElement::Boss), [&] { drawBoss(ui, w, p.o, p.k); });
        }
        if (hudVisible(settings, HudElement::Progress)) {
            Place p = at(HudElement::Progress);
            drawBlock(ui, id(HudElement::Progress), [&] { drawProgress(ui, w, p.o, p.k, p.width); });
        }
        if (hudVisible(settings, HudElement::Enemies)) drawBlock(ui, id(HudElement::Enemies), [&] { drawEnemies(ui, w, at(HudElement::Enemies)); });
        if (hudVisible(settings, HudElement::Ambush) && (w.activeAmbush() || (editorPreview_ && !w.ambushes().empty())))
            drawBlock(ui, id(HudElement::Ambush), [&] { drawAmbush(ui, w, at(HudElement::Ambush)); });
        if (w.coop()) {
            drawTeamLabels(ui, w, view);  // names over the players, souls and their revival
            if (settings.showInputHints && team_.promptPlayer >= 0 && team_.promptPlayer < w.teamSize() &&
                w.member(team_.promptPlayer).presence == GameWorld::Presence::Playing) {
                GameWorld::PlayerScope local(w, team_.promptPlayer);
                drawInteractPrompt(ui, w, view);
            }
        } else {
            drawBlock(ui, kStatusBlock, [&] { drawStatus(ui, w, settings); });
            if (settings.showInputHints) drawInteractPrompt(ui, w, view);
        }
    }
    if (messagesVisible(settings)) drawMessages(ui, w);
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
        case HudElement::Splits: {
            // Right-aligned under the timer (wherever the timer is).
            Rect t = area(ui, w, HudElement::Timer, place(ui, w, s, HudElement::Timer));
            p.o = {t.right(), t.bottom() + ui.s(8)};
            if (p.o.y + ui.s(300) * p.k > H) p.o = {W - ui.s(36), ui.s(220)};
            break;
        }
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
        case HudElement::Enemies: {
            // On the left, under the power icons: the first of these places where it covers no
            // other element shown (in either mode) and stays on screen.
            GameplaySettings shown = s;
            shown.focusMode = false;
            auto freeAt = [&](Vec2 o) {
                Place q = p;
                q.o = o;
                Rect a = area(ui, w, e, q);
                if (a.right() > W || a.bottom() > H) return false;
                for (int i = 0; i < kHudElements; ++i) {
                    auto other = static_cast<HudElement>(i);
                    // The ambush's counter takes its place after this one (never a loop).
                    if (other == e || other == HudElement::Ambush || !hudVisible(shown, other)) continue;
                    Rect b = area(ui, w, other, place(ui, w, s, other));
                    if (b.w > 0.0f && a.intersects(b)) return false;
                }
                return true;
            };
            const Vec2 spots[] = {{ui.s(40), ui.s(262)}, {ui.s(40), ui.s(352)}, {ui.s(40), H * 0.42f}, {ui.s(40), H * 0.56f}};
            p.o = spots[0];
            for (const Vec2& o : spots)
                if (freeAt(o)) {
                    p.o = o;
                    break;
                }
            break;
        }
        case HudElement::Ambush: {
            // 2.62.2: on the left too, under « Ennemis vaincus »: the first of these places where
            // it covers no other element shown (in either mode) and stays on screen.
            GameplaySettings shown = s;
            shown.focusMode = false;
            auto freeAt = [&](Vec2 o) {
                Place q = p;
                q.o = o;
                Rect a = area(ui, w, e, q);
                if (a.right() > W || a.bottom() > H) return false;
                for (int i = 0; i < kHudElements; ++i) {
                    auto other = static_cast<HudElement>(i);
                    if (other == e || !hudVisible(shown, other)) continue;
                    Rect b = area(ui, w, other, place(ui, w, s, other));
                    if (b.w > 0.0f && a.intersects(b.expanded(ui.s(4)))) return false;
                }
                return true;
            };
            const Vec2 spots[] = {{ui.s(40), ui.s(262)}, {ui.s(40), ui.s(316)}, {ui.s(40), ui.s(370)}, {ui.s(40), H * 0.42f},
                                  {ui.s(40), H * 0.50f}, {ui.s(40), H * 0.58f}, {W * 0.5f - ui.s(260), ui.s(222)}};
            p.o = spots[0];
            for (const Vec2& o : spots)
                if (freeAt(o)) {
                    p.o = o;
                    break;
                }
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
        case HudElement::Splits: return {o.x - S(460), o.y, S(460), S(300)};
        case HudElement::Enemies: {
            const int total = w.stats().totalEnemies;
            if (total <= 0) return {};  // a level without enemies: nothing to count
            // As wide as the longest numbers of this level (the kills get one digit more:
            // split slimes count too), so that it never grows while the run goes on.
            int digits = 1;
            for (int t = total; t >= 10; t /= 10) ++digits;
            auto eights = [](int n) {
                int v = 0;
                for (int i = 0; i < n; ++i) v = v * 10 + 8;
                return v;
            };
            const float tw = ui.text().measure(enemiesText(eights(digits + 1), eights(digits)), FontFace::Bold, S(20)).x;
            return {o.x, o.y, S(14) + S(28) + S(10) + tw + S(10) + S(24) + S(14), S(42)};
        }
        case HudElement::Ambush: {
            // As wide as the longest text of this level's ambushes (most enemies, most waves),
            // so that it never grows during a fight; nothing for a level without an ambush.
            size_t most = 0, waves = 0;
            for (const CombatZoneEntity* z : w.ambushes()) {
                most = std::max(most, z->enemyCount());
                waves = std::max(waves, z->waveCount());
            }
            if (most == 0) return {};
            auto eights = [](size_t n) {
                int digits = 1;
                for (; n >= 10; n /= 10) ++digits;
                int v = 0;
                for (int i = 0; i < digits; ++i) v = v * 10 + 8;
                return v;
            };
            const int many = std::max(2, eights(most)), n = static_cast<int>(waves);
            const float tw = ui.text().measure(ambushText(many, n > 1 ? eights(waves) : 1, n > 1 ? eights(waves) : 1), FontFace::Bold, S(20)).x;
            return {o.x, o.y, S(14) + S(28) + S(10) + tw + S(18), S(42)};
        }
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
    const float pitch = size + S(18);
    float x = o.x, y = o.y;
    Renderer& r = ui.r();
    for (size_t i = 0; i < p.powers().size(); ++i) {
        const auto& pw = p.powers()[i];
        Rect box{x, y, size, size};
        bool ready = pw->ready(p);
        bool enough = p.energy + 0.01f >= pw->activationCost(p);
        // Powers that do not use the key of their slot (passive, or on a move of the
        // character) wear the dashed gold frame: the slot key does nothing for them.
        const PowerTrigger trigger = pw->trigger();
        const bool free = trigger != PowerTrigger::SlotKey;
        r.fillRoundedRect(box.expanded(S(4)), S(10), {8, 12, 26, 210});
        ui.icon("ui/icons_powers.png", pw->def().icon, 32, box, ready ? Color::white() : Color{110, 120, 140, 255});
        float cd = pw->cooldownRatio();
        if (cd > 0.0f) r.fillRect({box.x, box.y + box.h * (1.0f - cd), box.w, box.h * cd}, {0, 0, 0, 140});
        Rect state = free ? box.expanded(S(1)) : box.expanded(S(4));  // inside the dashed frame
        if (!enough) r.drawRoundedRect(state, S(10), {255, 77, 109, 160}, S(2));
        else if (pw->active()) {
            r.drawRoundedRect(state, S(10), pw->def().color, S(free ? 2.0f : 3.0f));
            r.drawGlow(box.center(), size, pw->def().color.withAlpha(60));
        } else if (ready && !free) {
            r.drawRoundedRect(state, S(10), {57, 240, 216, 90}, S(1.5f));
        }
        if (free) Glyphs::drawFreePowerFrame(ui, box.expanded(S(4)), S(10), S(2));
        // Below: the key of its slot, the move that uses it, or « passif » — never wider
        // than its column (a long key name is drawn smaller).
        const float gy = box.bottom() + S(22), room = pitch - S(6);
        float gh = S(28);
        if (trigger == PowerTrigger::Passive) {
            float fs = k * 15.0f;
            while (fs > k * 9.0f && ui.text().measure("PASSIF", FontFace::Bold, ui.s(fs)).x > room) fs -= 0.5f;
            ui.text().draw("PASSIF", {box.center().x, gy}, style(ui, fs, Glyphs::freePowerColor(), FontFace::Bold, Align::Center, VAlign::Middle));
        } else {
            std::vector<Action> keys = trigger == PowerTrigger::SlotKey ? std::vector<Action>{powerSlotAction(static_cast<int>(i))} : pw->standardKeys();
            float kw = Glyphs::drawKeys(ui, keys, {0.0f, 0.0f}, gh, false);
            if (kw > room) {
                gh *= room / kw;
                kw = Glyphs::drawKeys(ui, keys, {0.0f, 0.0f}, gh, false);
            }
            Glyphs::drawKeys(ui, keys, {box.center().x - kw * 0.5f, gy}, gh);
        }
        x += pitch;
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
    // A challenge: its next tier still reachable (the level's medals do not apply).
    if (tierFinish_) {
        // « Finir le niveau » (2.41): Or at the finish (without damage when it says so).
        TextStyle ms = small;
        bool gold = !tierGoldNoDamage_ || w.stats().damageTaken == 0;
        ms.color = gold ? Color{255, 204, 84, 255} : Color{198, 210, 228, 255};
        ui.text().draw(gold ? (tierGoldNoDamage_ ? "Or : finir sans dégât" : "Or : finir le niveau") : "Argent : finir le niveau", {x, line}, ms);
        line += S(26);
    } else if (tierSilver_ > 0.0f || tierGold_ > 0.0f) {
        TextStyle ms = small;
        float e = t.elapsed();
        if (tierGold_ > 0.0f && e <= tierGold_ && (!tierGoldNoDamage_ || w.stats().damageTaken == 0)) {
            ms.color = {255, 204, 84, 255};
            ui.text().draw("Or < " + LevelTimer::format(tierGold_, false), {x, line}, ms);
            line += S(26);
        } else if (tierSilver_ > 0.0f && e <= tierSilver_) {
            ms.color = {198, 210, 228, 255};
            ui.text().draw("Argent < " + LevelTimer::format(tierSilver_, false), {x, line}, ms);
            line += S(26);
        }
    }
    // Next medal objective still reachable
    for (int i = 3; i >= 1 && tierSilver_ <= 0.0f && tierGold_ <= 0.0f && !tierFinish_; --i) {
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

void Hud::drawEnemies(UIContext& ui, GameWorld& w, const Place& p) {
    const Rect a = area(ui, w, HudElement::Enemies, p);
    if (a.w <= 0.0f) return;  // no enemy in this level
    auto S = [&](float v) { return ui.s(v) * p.k; };
    const RunStats& s = w.stats();
    // Every enemy of the level beaten (the rule of « Aucun survivant »: kills >= enemies).
    const bool all = enemiesCleared(s.kills, s.totalEnemies);
    const Color green{120, 230, 140, 255};
    ui.r().fillRoundedRect(a, a.h * 0.5f, {6, 10, 22, 160});
    ui.r().drawRoundedRect(a, a.h * 0.5f, all ? green.withAlpha(200) : Color{255, 255, 255, 40}, S(1.5f));
    const float cy = a.center().y;
    ui.icon(kHudIcons, EnemyIcon, 16, {a.x + S(14), cy - S(14), S(28), S(28)}, all ? green : Color::white());
    TextStyle st = style(ui, p.k * 20, all ? green : ui.theme().text, FontFace::Bold, Align::Left, VAlign::Middle);
    ui.text().draw(enemiesText(s.kills, s.totalEnemies), {a.x + S(14) + S(28) + S(10), cy}, st);
    if (all) ui.icon(kHudIcons, Check, 16, {a.right() - S(14) - S(24), cy - S(12), S(24), S(24)}, green);
}

std::string Hud::ambushText(int left, int wave, int waves) {
    const std::string n = std::to_string(left);
    if (waves > 1) {
        const std::string k = std::to_string(wave), t = std::to_string(waves);
        if (left == 1) return "Embuscade : " + n + " ennemi restant · vague " + k + " / " + t;
        return "Embuscade : " + n + " ennemis restants · vague " + k + " / " + t;
    }
    if (left == 1) return "Embuscade : " + n + " ennemi restant";
    return "Embuscade : " + n + " ennemis restants";
}

void Hud::drawAmbush(UIContext& ui, GameWorld& w, const Place& p) {
    CombatZoneEntity* z = w.activeAmbush();
    if (!z && editorPreview_ && !w.ambushes().empty()) z = w.ambushes().front();
    const Rect a = area(ui, w, HudElement::Ambush, p);
    if (!z || a.w <= 0.0f) return;
    auto S = [&](float v) { return ui.s(v) * p.k; };
    const Color violet = EnemyArrows::kAmbushColor;  // the colour of the ambush's arrows
    ui.r().fillRoundedRect(a, a.h * 0.5f, {6, 10, 22, 170});
    ui.r().drawRoundedRect(a, a.h * 0.5f, violet.withAlpha(z->suspended() ? 110 : 200), S(1.5f));
    const float cy = a.center().y;
    ui.icon(kHudIcons, Skull, 16, {a.x + S(14), cy - S(14), S(28), S(28)}, violet);
    const std::string text = ambushText(z->enemiesLeft(w), static_cast<int>(z->currentWave()) + 1, static_cast<int>(z->waveCount()));
    // Measured into the room left (the area is that of the longest text: a safety only).
    const float x = a.x + S(14) + S(28) + S(10), room = a.right() - S(12) - x;
    float size = 20.0f * p.k;
    const float tw = ui.text().measure(text, FontFace::Bold, ui.s(size)).x;
    if (tw > room && tw > 0.0f) size *= room / tw;
    ui.text().draw(text, {x, cy}, style(ui, size, ui.theme().text, FontFace::Bold, Align::Left, VAlign::Middle));
}

void Hud::drawStatus(UIContext& ui, GameWorld& w, const GameplaySettings& settings) {
    struct Chip {
        std::string text;
        Color color;
        float gauge = -1.0f;  // 2.58: >= 0, a gauge after the text (the Titan's heat)
    };
    std::vector<Chip> chips;
    const Player& pl = w.player();
    if (pl.alive() && pl.cursed())
        chips.push_back({"Maudite : sauts alourdis " + std::to_string(static_cast<int>(std::ceil(pl.curseTime()))) + " s", Color{200, 140, 255, 255}});
    if (const Enemy* b = w.activeBoss(); b && b->alive() && b->warded())
        chips.push_back({"Intouchable : brisez le phylactère", Color{170, 240, 255, 255}});
    if (const Enemy* b = w.activeBoss(); b && b->alive() && b->isTitan() && !b->lastStand()) {
        // 2.58, the Titan Mécanique: stalled (its valves, the time before it restarts), venting (its
        // core bare), or the heat of its phase.
        const int ph = b->phaseIndex();
        const bool gauge = ph >= 0 && ph < static_cast<int>(b->def().phases.size()) && b->def().phases[static_cast<size_t>(ph)].heat > 0.0f;
        if (b->stalled()) {
            const int left = b->valvesLeft();
            const int secs = static_cast<int>(std::ceil(b->stallLeft()));
            chips.push_back({"Calé : " + std::to_string(left) + (left > 1 ? " vannes à tourner · " : " vanne à tourner · ") + std::to_string(secs) + " s",
                             Color{255, 170, 90, 255}});
        } else if (b->venting()) {
            chips.push_back({"Surchauffe : son cœur est à nu · " + std::to_string(static_cast<int>(std::ceil(b->ventLeft()))) + " s",
                             Color{255, 120, 80, 255}});
        } else if (gauge) {
            chips.push_back({"Chaleur du Titan", Color{255, 194, 60, 255}, b->heat()});
        }
    }
    if (chips.empty()) return;
    const float W = ui.screenW(), H = ui.screenH();
    const float h = ui.s(40), pad = ui.s(14), dot = ui.s(10), maxW = std::min(ui.s(440), W - ui.s(80));
    // Each chip as wide as its text (smaller type for a long one), never wider than maxW.
    std::vector<float> sizes, widths;
    float total = 0.0f;
    const float barW = ui.s(120);
    for (const Chip& c : chips) {
        const float bar = c.gauge >= 0.0f ? barW + pad : 0.0f;
        const float room = maxW - pad * 3.0f - dot - bar;
        const float fs = ui.fitFont(c.text, FontFace::Bold, 20, room);
        sizes.push_back(fs);
        widths.push_back(std::min(maxW, pad * 3.0f + dot + bar + ui.text().measure(c.text, FontFace::Bold, ui.s(fs)).x));
        total += h + ui.s(8);
    }
    // Where they cover nothing of the HUD shown: under the left column first, then lower down.
    std::vector<Rect> taken;
    for (int i = 0; i < kHudElements; ++i) {
        auto e = static_cast<HudElement>(i);
        if (e == HudElement::Splits || !hudVisible(settings, e)) continue;
        if (e == HudElement::Boss && !(w.activeBoss() && w.activeBoss()->alive())) continue;
        if (e == HudElement::Ambush && !w.activeAmbush()) continue;
        Rect a = area(ui, w, e, place(ui, w, settings, e));
        if (a.w > 0.0f) taken.push_back(a.expanded(ui.s(6)));
    }
    float below = ui.s(34);
    for (HudElement e : {HudElement::Vitals, HudElement::Powers, HudElement::Enemies, HudElement::Ambush}) {
        if (!hudVisible(settings, e) || (e == HudElement::Ambush && !w.activeAmbush())) continue;
        Rect a = area(ui, w, e, place(ui, w, settings, e));
        if (a.w > 0.0f && a.x < W * 0.5f) below = std::max(below, a.bottom() + ui.s(12));
    }
    float widest = 0.0f;
    for (float wd : widths) widest = std::max(widest, wd);
    const Vec2 spots[] = {{ui.s(40), below}, {ui.s(40), H * 0.42f}, {ui.s(40), H * 0.56f}, {W - ui.s(40) - widest, H * 0.42f}};
    Vec2 at = spots[0];
    for (const Vec2& o : spots) {
        const Rect all{o.x, o.y, widest, total};
        if (all.bottom() > H || all.right() > W) continue;
        bool free = true;
        for (const Rect& t : taken) free = free && !all.intersects(t);
        if (free) {
            at = o;
            break;
        }
    }
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.0f);
    for (size_t i = 0; i < chips.size(); ++i) {
        const Rect r{at.x, at.y, widths[i], h};
        ui.r().fillRoundedRect(r, h * 0.5f, Color{14, 8, 26, 215});
        ui.r().drawRoundedRect(r, h * 0.5f, chips[i].color.withAlpha(static_cast<uint8_t>(120 + 100 * pulse)), ui.s(1.5f));
        ui.r().drawGlow({r.x + pad + dot * 0.5f, r.center().y}, dot * 1.6f, chips[i].color.withAlpha(static_cast<uint8_t>(90 + 80 * pulse)));
        ui.r().fillCircle({r.x + pad + dot * 0.5f, r.center().y}, dot * 0.5f, chips[i].color);
        const float bar = chips[i].gauge >= 0.0f ? barW + pad : 0.0f;
        const float room = r.w - pad * 3.0f - dot - bar;
        TextStyle st = style(ui, sizes[i], chips[i].color, FontFace::Bold, Align::Left, VAlign::Middle);
        ui.text().draw(ui.elide(chips[i].text, FontFace::Bold, sizes[i], room), {r.x + pad * 2.0f + dot, r.center().y}, st);
        if (chips[i].gauge >= 0.0f) {
            // The gauge, at the right end of the chip: ten segments, amber to red; nearly full, it pulses.
            const float g = std::clamp(chips[i].gauge, 0.0f, 1.0f);
            const Rect gr{r.right() - pad - barW, r.center().y - ui.s(7), barW, ui.s(14)};
            ui.r().fillRoundedRect(gr, ui.s(3), Color{40, 30, 26, 230});
            const int seg = 10;
            const float sw = (gr.w - ui.s(2) * (seg + 1)) / seg;
            for (int k = 0; k < seg; ++k) {
                const float u = static_cast<float>(k) / (seg - 1);
                const bool lit = g * seg >= static_cast<float>(k) + 0.5f;
                Color c{255, static_cast<uint8_t>(210 - 150 * u), static_cast<uint8_t>(70 - 40 * u), 255};
                if (!lit) c = Color{58, 44, 36, 255};
                else if (g > 0.75f) c = c.withAlpha(static_cast<uint8_t>(170 + 85 * pulse));
                ui.r().fillRect({gr.x + ui.s(2) + (sw + ui.s(2)) * k, gr.y + ui.s(2), sw, gr.h - ui.s(4)}, c);
            }
        }
        at.y += h + ui.s(8);
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
    const bool racing = !racers_.empty();
    float fill = racing ? std::clamp(racers_.front().at, 0.0f, 1.0f) : shownProgress_;
    float reached = bestProgress_;
    if (racing) {
        reached = 0.0f;
        for (const auto& rc : racers_) reached = std::max(reached, rc.at);
    }
    float headX = xAt(fill);
    // 2.62, a team: each player's place, a dot of its colour on the rail (drawn at the end).
    const bool teamMarks = w.coop() && teamAt_.size() == static_cast<size_t>(w.teamSize());

    // No frame around the bar (the player's choice): only a halo that breathes along the rail,
    // so it stays readable over any background.
    float breathe = 0.75f + 0.25f * std::sin(t * 1.6f);
    for (float x = rail.x; x <= rail.right(); x += S(48)) {
        float k = (x - rail.x) / std::max(1.0f, rail.w);
        r.drawGlow({x, rail.center().y}, S(46), Color::lerp(cyan, violet, k).withAlpha(static_cast<uint8_t>(34 * breathe)));
    }
    r.drawGlow({rail.right() + S(30), rail.center().y}, S(70), violet.withAlpha(static_cast<uint8_t>(60 * breathe)));

    // Header: section on the left, percentage on the right (not with runners: they stand there).
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
    if (!racing) ui.text().draw(pctText, {rail.right() + S(62), headerY + S(2)}, ps);
    float room = rail.w + S(62) - pctW - S(30);
    TextStyle ss = style(ui, k * ui.fitFont(section, FontFace::Bold, 16, room / k, 0.6f), Color{214, 226, 242, 255}, FontFace::Bold, Align::Left,
                         VAlign::Bottom);
    ss.shadow = true;
    if (!racing) ui.text().draw(section, {rail.x, headerY}, ss);

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
        if (racing) return;
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
        bool on = (!racing && w.checkpointId() == m.objectId) || m.at <= reached + 0.002f;
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
                const EnemyDefinition* def = w.enemyDefinition(m.enemyId);
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
        bool done = (racing ? reached : shownProgress_) >= 0.995f;
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

    // Runners (replay comparison): each one's character on the rail, the first above it,
    // the second below it, so they never cover each other; a letter in their colour.
    if (racing) {
        for (size_t i = 0; i < racers_.size() && i < 2; ++i) {
            const ProgressRacer& rc = racers_[i];
            float x = xAt(rc.at);
            bool above = i == 0;
            float bodyH = S(72);
            r.drawGlow({x, rail.center().y}, S(30), rc.color.withAlpha(200));
            r.fillCircle({x, rail.center().y}, S(5), rc.color);
            float top = above ? rail.y - S(2) - bodyH : rail.bottom() + S(2);
            if (rc.texture && rc.src.w > 0.0f && rc.src.h > 0.0f) {
                float bw = bodyH * rc.src.w / rc.src.h;
                Rect dst{x - bw * 0.5f, top, bw, bodyH};
                r.drawGlow(dst.center(), S(26), rc.color.withAlpha(90));
                const float o = std::max(1.0f, S(1.4f));
                const Vec2 offs[4] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}};
                for (Vec2 d : offs)
                    r.drawTexture(*rc.texture, &rc.src, {dst.x + d.x, dst.y + d.y, dst.w, dst.h}, rc.color.withAlpha(150), BlendMode::Additive,
                                  rc.flip);
                r.drawTexture(*rc.texture, &rc.src, dst, Color::lerp(Color{255, 255, 255, 255}, rc.color, 0.35f), BlendMode::Alpha, rc.flip);
            }
            // The letter: above the first one's head, under the second one's feet.
            Vec2 bc{x, above ? top - S(9) : top + bodyH + S(9)};
            r.fillCircle(bc, S(10), rc.color);
            r.drawRing(bc, S(10), S(1.5f), Color{8, 12, 26, 230});
            TextStyle ls = style(ui, k * 13, Color{8, 12, 26, 255}, FontFace::Bold, Align::Center, VAlign::Middle);
            ui.text().draw(rc.letter, bc, ls);
        }
        return;
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
    if (teamMarks) {
        // 2.62: every player on the rail, a dot of its colour ringed in white (a soul: dimmed).
        for (int i = w.teamSize(); i-- > 0;) {
            const GameWorld::TeamMember& m = w.member(i);
            if (m.presence == GameWorld::Presence::Gone) continue;
            const Vec2 c{xAt(teamAt_[static_cast<size_t>(i)]), rail.center().y};
            const bool inPlay = m.presence == GameWorld::Presence::Playing;
            r.fillCircle(c, S(8), Color{255, 255, 255, static_cast<uint8_t>(inPlay ? 235 : 120)});
            r.fillCircle(c, S(6), teamColor(i).withAlpha(static_cast<uint8_t>(inPlay ? 255 : 110)));
        }
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

void Hud::drawPowerUse(UIContext& ui, const PowerDefinition& def, int slot, Vec2 center, float h, float alpha) {
    const PowerTriggerInfo t = powerTriggerInfo(def);
    if (t.kind == PowerTrigger::SlotKey) {
        // The key of the slot it went to.
        const std::string text = "pour l'utiliser";
        Action key = powerSlotAction(slot);
        float w = Glyphs::measureAction(ui, key, h) + h * 0.3f + ui.text().measure(text, FontFace::Body, h * 0.62f).x;
        Glyphs::drawHint(ui, key, text, {center.x - w * 0.5f, center.y}, h);
        return;
    }
    // Not on the key of its slot: its move, or « passif », in the dashed gold frame.
    const std::string text = t.kind == PowerTrigger::Passive ? "Passif : toujours actif" : t.how;
    float w = Glyphs::drawFreePowerUse(ui, t.keys, text, {0.0f, 0.0f}, h, alpha, false);
    Glyphs::drawFreePowerUse(ui, t.keys, text, {center.x - w * 0.5f, center.y}, h, alpha);
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
                // Below the title, one after the other: subtitle, characteristics, button.
                float yy = y + ui.s(44);
                if (!m.subtitle.empty()) {
                    TextStyle sub = style(ui, 26, ui.theme().text.withAlphaF(a), FontFace::Body, Align::Center, VAlign::Top);
                    float maxW = ui.s(1100);
                    Vec2 sz = ui.text().measureWrapped(m.subtitle, FontFace::Body, sub.size, maxW);
                    sub.align = Align::Left;
                    ui.text().drawWrapped(m.subtitle, {ui.screenW() * 0.5f - sz.x * 0.5f, yy}, maxW, sub);
                    yy += sz.y + ui.s(12);
                }
                if (!m.detail.empty()) {
                    // A power picked up: its characteristics on a band.
                    float fs = ui.fitFont(m.detail, FontFace::Bold, 22, ui.s(1100));
                    float tw = ui.text().measure(m.detail, FontFace::Bold, ui.s(fs)).x;
                    Rect band{ui.screenW() * 0.5f - tw * 0.5f - ui.s(18), yy, tw + ui.s(36), ui.s(40)};
                    ui.r().fillRoundedRect(band, ui.s(20), Color{8, 14, 30, 210}.withAlphaF(a));
                    ui.r().drawRoundedRect(band, ui.s(20), ui.theme().warm.withAlphaF(0.6f * a), std::max(1.0f, ui.s(1.2f)));
                    ui.label(m.detail, band.center(), fs, ui.theme().warm.withAlphaF(a), Align::Center, FontFace::Bold, VAlign::Middle);
                    yy += band.h + ui.s(12);
                }
                // Power unlock: how to use the new power, centred below the rest.
                if (const PowerDefinition* def = m.power.empty() ? nullptr : w.content().power(m.power))
                    drawPowerUse(ui, *def, m.powerSlot, {ui.screenW() * 0.5f, std::max(y + ui.s(130), yy + ui.s(30))}, ui.s(40), a);
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
