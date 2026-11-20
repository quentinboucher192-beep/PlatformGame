// Hazards: spikes, liquids (acid, water, lava), damage / slow zones and lasers.

#include <algorithm>
#include <cmath>

#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "engine/core/DetMath.h"

namespace cosmic {

namespace {
int quarterTurns(float rotation) {
    int q = static_cast<int>(std::lround(rotation / 90.0f)) % 4;
    return q < 0 ? q + 4 : q;
}
}  // namespace

// ------------------------------------------------------------------- spikes

void SpikesEntity::init(GameWorld& w) {
    tex_ = w.resources().texture("sprites/objects/spikes.png");
    // Forgiving hitbox: only the pointy part hurts.
    int q = quarterTurns(obj_.rotation);
    hitbox_ = box;
    switch (q) {
        case 0: hitbox_ = {box.x + 2, box.y + box.h * 0.45f, box.w - 4, box.h * 0.55f}; break;
        case 1: hitbox_ = {box.x, box.y + 2, box.w * 0.55f, box.h - 4}; break;
        case 2: hitbox_ = {box.x + 2, box.y, box.w - 4, box.h * 0.55f}; break;
        case 3: hitbox_ = {box.x + box.w * 0.45f, box.y + 2, box.w * 0.55f, box.h - 4}; break;
    }
}

void SpikesEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    if (obj_.prop<bool>("instantKill", false)) {
        if (!w.player().invulnerable()) w.player().kill(w);
        return;
    }
    // Direction the points face: 0 up, 1 right (90°), 2 down, 3 left (270°).
    int q = quarterTurns(obj_.rotation);
    Vec2 out = q == 0 ? Vec2{0, -1} : q == 1 ? Vec2{1, 0} : q == 2 ? Vec2{0, 1} : Vec2{-1, 0};
    DamageInfo d;
    d.amount = static_cast<float>(obj_.prop<int>("damage", 1));
    d.type = DamageType::Hazard;
    d.source = q == 0 || q == 2 ? Vec2{w.player().center().x, box.center().y - out.y * 8.0f} : box.center() - out * 16.0f;
    d.knockback = 120.0f;
    if (w.damagePlayer(d)) {
        // Pop the player out of the spikes, away from their base (never into the wall).
        Vec2 push = q == 0 ? Vec2{0, -260} : q == 2 ? Vec2{0, 120} : Vec2{out.x * 200.0f, -150.0f};
        w.player().vel = push;
    }
}

void SpikesEntity::render(RenderContext& ctx) {
    if (!tex_) return;
    int q = quarterTurns(obj_.rotation);
    bool vertical = q == 1 || q == 3;
    float length = vertical ? box.h : box.w;
    int count = std::max(1, static_cast<int>(std::round(length / 16.0f)));
    for (int i = 0; i < count; ++i) {
        Rect src{static_cast<float>((i * 7 + static_cast<int>(box.x)) % 2) * 16.0f, 0, 16, 16};
        Rect dst;
        if (!vertical)
            dst = {box.x + i * 16.0f, box.bottom() - 16.0f, 16, 16};
        else
            dst = {box.x + (box.w - 16.0f) * 0.5f, box.y + i * 16.0f, 16, 16};
        if (q == 2) dst.y = box.y;
        ctx.r.drawWorldTexture(*tex_, &src, dst, Color::white(), BlendMode::Alpha, false, q * 90.0f);
    }
    ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.6f, {176, 76, 255, 255});
}

// ------------------------------------------------------------------- liquid

void LiquidEntity::init(GameWorld& w) {
    kind_ = obj_.propStr("kind", "acid");
    tex_ = w.resources().texture("sprites/objects/liquid_" + kind_ + ".png");
}

bool LiquidEntity::swimmable() const { return kind_ == "water" || kind_ == "icewater"; }

void LiquidEntity::onPlayerEnter(GameWorld& w) {
    w.particles().emit(swimmable() ? "splash_water" : "splash_acid", {w.player().center().x, box.y}, 1.0f);
    w.playSound("sounds/land.wav", {w.player().center().x, box.y}, 0.6f, 0.7f);
}

void LiquidEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    Player& p = w.player();
    if (swimmable()) {
        p.inWater = true;
        return;
    }
    if (p.alive() && !p.respawning()) {
        w.particles().emit(kind_ == "lava" ? "lava_burn" : "acid_dissolve", p.center(), 1.0f);
        p.kill(w);
    }
}

void LiquidEntity::fixedUpdate(GameWorld& w, float dt) {
    // Enemies (walkers) die in deadly liquids.
    if (!swimmable()) {
        for (auto& e : w.enemies())
            if (e->alive() && !e->def().flying && overlapBox().intersects(e->box)) e->kill(w);
    }
    bubbleTimer_ -= dt;
    if (bubbleTimer_ <= 0.0f) {
        Rect vis = w.camera().visibleArea(1920, 1080).expanded(64);
        float x0 = std::max(box.x + 4, vis.x), x1 = std::min(box.right() - 4, vis.right());
        bubbleTimer_ = std::max(0.08f, 1.2f / std::max(1.0f, (x1 - x0) / 64.0f));
        if (x1 <= x0) return;
        float x = w.fxRng().range(x0, x1);
        const char* fx = kind_ == "acid" ? "acid_bubbles" : kind_ == "lava" ? "lava_bubbles" : "bubbles";
        w.particles().emit(fx, {x, box.y + 4}, 1.0f);
    }
}

void LiquidEntity::render(RenderContext& ctx) {
    if (!tex_) return;
    float amp = obj_.prop<float>("waveHeight", 1.0f);
    int frame = static_cast<int>(ctx.time * 8.0f) % 8;
    BlendMode bm = BlendMode::Alpha;
    const Rect vis = ctx.r.view().visibleWorld().expanded(32);
    float startX = box.x + std::max(0.0f, std::floor((vis.x - box.x) / 16.0f) * 16.0f);
    float endX = std::min(box.right(), vis.right());
    float endY = std::min(box.bottom(), vis.bottom() + 16.0f);
    for (float x = startX; x < endX - 0.5f; x += 16.0f) {
        float w = std::min(16.0f, box.right() - x);
        float bob = dmath::sin(ctx.time * 2.0f + x * 0.05f) * amp;
        Rect src{static_cast<float>(frame) * 16.0f, 0, w, 16};
        ctx.r.drawWorldTexture(*tex_, &src, {x, box.y + bob - 2, w, 16}, Color::white(), bm);
        for (float y = box.y + 14 + bob; y < endY - 0.5f; y += 16.0f) {
            float h = std::min(16.0f, box.bottom() - y);
            Rect bsrc{static_cast<float>((frame + static_cast<int>(x / 16)) % 8) * 16.0f, 16, w, h};
            ctx.r.drawWorldTexture(*tex_, &bsrc, {x, y, w, h}, Color::white(), bm);
        }
    }
    Color glow = kind_ == "acid"       ? Color{157, 255, 58, 255}
                 : kind_ == "lava"     ? Color{255, 120, 40, 255}
                 : kind_ == "icewater" ? Color{95, 176, 255, 255}
                                       : Color{31, 120, 140, 255};
    for (float x = std::floor(startX / 96.0f) * 96.0f + 40.0f; x < endX + 96.0f; x += 96.0f)
        if (x > box.x && x < box.right()) ctx.lights.add({x, box.y}, 110.0f, glow);
}

// -------------------------------------------------------------- damage zone

void DamageZoneEntity::init(GameWorld&) {
    // 2.33: a zone may start off (switched on by an event: the plasma of Singularis's arena).
    active = obj_.prop<bool>("active", true);
    shown_ = active ? 1.0f : 0.0f;
    // 2.72: the raids' rising magma (docs/RAIDS.md §15).
    magma_ = obj_.propStr("style", "toxic") == "magma";
    level_ = magma_ && active ? 1.0f : 0.0f;
}

Rect DamageZoneEntity::magmaBody() const {
    const float h = box.h * std::clamp(level_, 0.0f, 1.0f);
    // A pool rises from the bottom of its box; a fall (taller than wide) pours from its top.
    if (box.w >= box.h) return {box.x, box.bottom() - h, box.w, h};
    return {box.x, box.y, box.w, h};
}

Rect DamageZoneEntity::overlapBox() const {
    if (!magma_) return box;
    const Rect b = magmaBody();
    return b.h > 0.5f ? b : Rect{box.x, box.bottom() + 1.0f, box.w, 0.0f};  // drained: touches nothing
}

void DamageZoneEntity::onPlayerStay(GameWorld& w, float dt) {
    if (!active) return;
    if (magma_ && level_ <= 0.02f) return;  // 2.72: nothing has risen yet
    Player& pl = w.player();
    if (obj_.prop<bool>("bounce", false) && pl.alive() && pl.vel.y > -200.0f) {
        // 2.33: it throws Nova back up (plasma: hurt, never stuck in it).
        pl.launch({pl.vel.x * 0.5f, -390.0f}, false);
    }
    tick_ -= dt;
    if (tick_ > 0.0f) return;
    tick_ = obj_.prop<float>("interval", 0.8f);
    if (obj_.prop<bool>("shieldProtects", true) && w.player().shieldActive()) return;
    DamageInfo d;
    d.amount = static_cast<float>(obj_.prop<int>("damage", 1));
    d.type = DamageType::Hazard;
    d.source = box.center();
    d.knockback = 60.0f;
    d.ignoreShield = !obj_.prop<bool>("shieldProtects", true);
    w.damagePlayer(d);
}

void DamageZoneEntity::fixedUpdate(GameWorld& w, float dt) {
    if (!playerInside) tick_ = 0.0f;
    if (magma_) {
        // 2.72: it rises while on, drains after « disable » (still hurting until it is gone).
        const float rise = std::max(0.1f, obj_.prop<float>("rise", 1.4f)), fall = std::max(0.1f, obj_.prop<float>("fall", 1.2f));
        level_ = active && !draining_ ? std::min(1.0f, level_ + dt / rise) : std::max(0.0f, level_ - dt / fall);
        if (draining_ && level_ <= 0.0f) {
            draining_ = false;
            active = false;
        }
        warnLeft_ = std::max(0.0f, warnLeft_ - dt);
        hidden_.clear();
        const Rect body = magmaBody();
        if (active && body.h > 2.0f) {
            // Every enemy caught in it burns like the players (not a boss, nor one still asleep).
            enemyTick_ -= dt;
            const bool burn = enemyTick_ <= 0.0f;
            if (burn) enemyTick_ = obj_.prop<float>("interval", 0.8f);
            for (auto& e : w.enemies()) {
                if (!e->alive() || !body.intersects(e->box)) continue;
                if (body.y < e->box.y) hidden_.push_back(e->center().x);  // display: under the surface
                if (!burn || e->dormant() || e->def().isBoss) continue;
                if (e->summonedBy != 0)  // a boss's creatures (Pyroclast's weak points) are its fight's
                    if (const Enemy* owner = w.findEnemy(e->summonedBy); owner && owner->def().isBoss) continue;
                DamageInfo d;
                d.amount = static_cast<float>(obj_.prop<int>("damage", 1));
                d.type = DamageType::Hazard;
                d.source = {e->center().x, body.bottom()};
                d.knockback = 40.0f;
                e->takeDamage(w, d);
            }
            for (int i = 0; i < w.teamSize(); ++i) {
                const Player& p = w.playerAt(i);
                if (p.alive() && body.intersects(p.box) && body.y < p.box.y) hidden_.push_back(p.center().x);
            }
        } else {
            enemyTick_ = 0.0f;
        }
    }
    shown_ = active ? std::min(1.0f, shown_ + dt / 0.6f) : std::max(0.0f, shown_ - dt / 0.4f);
    if (!active || !obj_.prop<bool>("visible", true)) return;
    fxTimer_ -= dt;
    if (fxTimer_ <= 0.0f) {
        const std::string style = obj_.propStr("style", "toxic");
        bool plasma = style == "plasma";
        bool abyss = style == "abyss";  // 2.53: the dark water of the Kraken's arena
        bool embers = style == "embers";  // 2.56: the embers under the ash of the Phénix's nest
        bool souls = style == "souls";    // 2.57: the souls of the Liche's ossuary
        fxTimer_ = std::max(0.05f, (plasma || abyss || embers || souls ? 1.2f : 2.0f) / std::max(1.0f, box.w * box.h / 2000.0f));
        static const Color kSoul{150, 240, 220, 255};
        w.particles().emit(plasma ? "lava_bubbles" : (abyss ? "bubbles" : (embers || souls ? "embers_rising" : "toxic_mist")),
                           {w.fxRng().range(box.x, box.right()), w.fxRng().range(box.y, box.bottom())}, 1.0f, {}, souls ? &kSoul : nullptr);
    }
}

bool DamageZoneEntity::handleAction(GameWorld& w, const std::string& action, const Json& params) {
    const std::string style = obj_.propStr("style", "toxic");
    if (magma_) {
        // 2.72: the raids' rising magma: announced, rising, draining.
        if (action == "warn") {
            warnLeft_ = std::max(0.5f, jget<float>(params, "seconds", 3.0f));
            w.playSound("sounds/lava_rise.wav", box.center(), 0.45f, 0.7f);
            return true;
        }
        if (action == "enable" || action == "activate" || (action == "toggle" && (!active || draining_))) {
            if (!active || draining_) {
                w.playSound("sounds/lava_rise.wav", box.center(), 0.9f);
                w.shake(0.3f);
            }
            active = true;
            draining_ = false;
            warnLeft_ = 0.0f;
            return true;
        }
        if (action == "disable" || action == "toggle") {
            if (active) draining_ = true;
            return true;
        }
        return false;
    }
    if ((action == "enable" || action == "activate") && !active &&
        (style == "plasma" || style == "abyss" || style == "embers" || style == "souls")) {
        w.playSound("sounds/lava_rise.wav", box.center(), 0.9f);
        w.shake(0.3f);
    }
    if (action == "enable" || action == "activate") active = true;
    else if (action == "disable") active = false;
    else if (action == "toggle") active = !active;
    else return false;
    return true;
}

void DamageZoneEntity::render(RenderContext& ctx) {
    if (ctx.editor) {
        ctx.r.fillWorldRect(box, {255, 60, 60, 40});
        ctx.r.drawWorldRect(box, {255, 60, 60, 160});
    }
    if (magma_) {
        renderMagma(ctx);
        return;
    }
    if (!obj_.prop<bool>("visible", true) || shown_ <= 0.0f) return;
    if (obj_.propStr("style", "toxic") == "plasma") {
        renderPlasma(ctx);
        return;
    }
    if (obj_.propStr("style", "toxic") == "abyss") {
        renderAbyss(ctx);
        return;
    }
    if (obj_.propStr("style", "toxic") == "embers") {
        renderEmbers(ctx);
        return;
    }
    if (obj_.propStr("style", "toxic") == "souls") {
        renderSouls(ctx);
        return;
    }
    if (active) {
        ctx.r.fillWorldRect(box, {157, 255, 58, 18}, BlendMode::Additive);
        ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.6f, {120, 200, 60, 255});
    }
}

void DamageZoneEntity::renderMagma(RenderContext& ctx) const {
    // 2.72 (raids): molten rock drawn from the generated sheet (tools/assetgen/environment/
    // raid_visuals.py): a body that flows, dark crust drifting on it, bright waves at its surface,
    // bubbles popping, embers, its heat; the telegraph of a rise (the level it will reach, the
    // floor glowing). Drawn over the actors (GameRenderer): what it covers is hidden, a glow at
    // its surface above each body under it. Display only.
    const float t = ctx.time;
    auto fx = ctx.res.animation("animations/raid_magma.json");
    const bool sheet = fx && fx->texture;
    const AnimationClip* body = sheet ? fx->clip("body") : nullptr;
    const AnimationClip* crust = sheet ? fx->clip("crust") : nullptr;
    const AnimationClip* surf = sheet ? fx->clip("surface") : nullptr;
    const AnimationClip* bubble = sheet ? fx->clip("bubble") : nullptr;
    const AnimationClip* ember = sheet ? fx->clip("ember") : nullptr;
    const AnimationClip* warn = sheet ? fx->clip("warn") : nullptr;
    const AnimationClip* glow = sheet ? fx->clip("glow") : nullptr;
    const float fw = sheet ? static_cast<float>(fx->frameWidth) : 32.0f, fh = sheet ? static_cast<float>(fx->frameHeight) : 32.0f;
    const bool pool = box.w >= box.h;
    const Rect vis = ctx.r.view().visibleWorld().expanded(fw);
    auto frameOf = [](const AnimationClip* c, float at, float fps) {
        return c ? static_cast<int>(at * fps) % std::max(1, c->frameCount()) : 0;
    };
    // The telegraph: the area that will flood breathes, its future surface glows, the floor cracks.
    if (warnLeft_ > 0.0f && level_ < 0.99f) {
        const float k = 0.5f + 0.5f * dmath::sin(t * 9.0f);
        const float near = 1.0f - std::clamp(warnLeft_ / 3.0f, 0.0f, 1.0f);  // brighter as it comes
        ctx.r.fillWorldRect(box, Color{255, 80, 20, static_cast<uint8_t>(18 + 30 * k + 30 * near)}, BlendMode::Additive);
        const float lineY = pool ? box.y : box.y;
        for (float x = box.x; x < box.right(); x += 12.0f)
            ctx.r.fillWorldRect({x, lineY - 1.0f, std::min(7.0f, box.right() - x), 3.0f}, Color{255, 190, 90, static_cast<uint8_t>(150 + 90 * k)},
                                BlendMode::Additive);
        if (warn && pool)
            for (float x = std::max(box.x, std::floor((vis.x - box.x) / fw) * fw + box.x); x < std::min(box.right(), vis.right()); x += fw) {
                const float wd = std::min(fw, box.right() - x);
                Rect src = fx->frameRect(*warn, (frameOf(warn, t, 10.0f) + static_cast<int>(x / fw)) % std::max(1, warn->frameCount()));
                src.w *= wd / fw;
                ctx.r.drawWorldTexture(*fx->texture, &src, {x, box.bottom() - fh, wd, fh}, Color{255, 255, 255, static_cast<uint8_t>(160 + 95 * near)},
                                       BlendMode::Additive);
            }
        for (float x = box.x + 24.0f; x < box.right(); x += 96.0f) ctx.lights.add({x, box.bottom() - 8.0f}, 70.0f + 50.0f * near, Color{255, 110, 40, 255});
    }
    const Rect b = magmaBody();
    if (b.h <= 0.5f) return;
    const Rect drawn = b.intersection(vis);
    if (drawn.w <= 0.0f || drawn.h <= 0.0f) return;
    const float surfaceY = pool ? b.y : b.bottom();
    // The body: opaque molten rock (never a glimpse of what it covers), flowing.
    ctx.r.fillWorldRect(b, Color{120, 28, 8, 255});
    if (body && crust) {
        const float flow = t * (pool ? 6.0f : 40.0f);  // a fall pours down, a pool drifts
        const float x0 = b.x + std::max(0.0f, std::floor((drawn.x - b.x) / fw) * fw);
        for (float x = x0; x < drawn.right(); x += fw) {
            const float wd = std::min(fw, b.right() - x);
            const int col = std::abs(static_cast<int>(std::floor((x - box.x) / fw)));
            const float y0 = pool ? b.y + 6.0f : b.y - std::fmod(flow, fh);
            for (float y = y0; y < b.bottom(); y += fh) {
                const float top = std::max(y, b.y), bot = std::min(y + fh, b.bottom());
                if (bot <= top || bot < drawn.y || top > drawn.bottom()) continue;
                const int row = std::abs(static_cast<int>(std::floor((y - box.y) / fh)));
                Rect src = fx->frameRect(*body, (frameOf(body, t, 6.0f) + col * 3 + row * 5) % std::max(1, body->frameCount()));
                src.y += top - y;
                src.h = bot - top;
                src.w *= wd / fw;
                ctx.r.drawWorldTexture(*fx->texture, &src, {x, top, wd, bot - top});
                Rect cs = fx->frameRect(*crust, (frameOf(crust, t, 2.0f) + col * 5 + row * 3) % std::max(1, crust->frameCount()));
                cs.y += top - y;
                cs.h = bot - top;
                cs.w *= wd / fw;
                ctx.r.drawWorldTexture(*fx->texture, &cs, {x, top, wd, bot - top});
            }
            if (pool && surf) {
                // The bright waves at its surface (their crest on the surface line).
                Rect src = fx->frameRect(*surf, (frameOf(surf, t, 9.0f) + col * 2) % std::max(1, surf->frameCount()));
                src.w *= wd / fw;
                ctx.r.drawWorldTexture(*fx->texture, &src, {x, surfaceY - fh * 0.375f, wd, fh});
            }
        }
    }
    // Heat over the surface, and the glow of what it hides just under it.
    if (pool) {
        ctx.r.fillWorldRect({b.x, surfaceY - 18.0f, b.w, 18.0f}, Color{255, 120, 40, 34}, BlendMode::Additive);
        ctx.r.fillWorldRect({b.x, surfaceY - 8.0f, b.w, 8.0f}, Color{255, 150, 60, 40}, BlendMode::Additive);
        for (float x : hidden_) {
            if (glow) {
                const Rect src = fx->frameRect(*glow, 0);
                ctx.r.drawWorldTexture(*fx->texture, &src, {x - fw * 0.75f, surfaceY - fh * 0.75f, fw * 1.5f, fh * 1.5f},
                                       Color{255, 220, 160, static_cast<uint8_t>(150 + 60 * dmath::sin(t * 6.0f + x))}, BlendMode::Additive);
            } else {
                ctx.r.fillWorldRect({x - 10.0f, surfaceY - 3.0f, 20.0f, 6.0f}, Color{255, 220, 150, 160}, BlendMode::Additive);
            }
        }
        // Bubbles popping and embers flying, at places that change every second (display time).
        const int slots = std::max(1, static_cast<int>(b.w / 56.0f));
        for (int k = 0; k < slots; ++k) {
            const float cycle = t * 0.9f + static_cast<float>(k) * 0.37f;
            const float ph = cycle - std::floor(cycle);
            const float seed = std::floor(cycle) * 12.9898f + static_cast<float>(k) * 78.233f;
            const float rnd = std::fabs(std::fmod(dmath::sin(seed) * 43758.547f, 1.0f));
            const float x = b.x + 8.0f + rnd * std::max(0.0f, b.w - 16.0f);
            if (x < vis.x || x > vis.right()) continue;
            if (bubble) {
                const int n = bubble->frameCount();
                const Rect src = fx->frameRect(*bubble, std::min(n - 1, static_cast<int>(ph * static_cast<float>(n))));
                ctx.r.drawWorldTexture(*fx->texture, &src, {x - fw * 0.5f, surfaceY - fh * 0.6f, fw, fh});
            }
            if (ember && (k % 2) == 0) {
                const Rect src = fx->frameRect(*ember, frameOf(ember, t + static_cast<float>(k), 12.0f));
                const float ey = surfaceY - 10.0f - ph * 70.0f;
                ctx.r.drawWorldTexture(*fx->texture, &src, {x + 6.0f * dmath::sin(t * 3.0f + static_cast<float>(k)) - fw * 0.25f, ey - fh * 0.25f, fw * 0.5f, fh * 0.5f},
                                       Color{255, 255, 255, static_cast<uint8_t>(255 * (1.0f - ph))}, BlendMode::Additive);
            }
        }
    } else {
        for (float y = b.y; y < b.bottom(); y += 6.0f) {
            const float wv = 2.0f * dmath::sin(t * 6.0f + y * 0.09f);
            ctx.r.fillWorldRect({b.x - 1.0f + wv, y, 3.0f, 6.0f}, Color{255, 210, 120, 200}, BlendMode::Additive);
            ctx.r.fillWorldRect({b.right() - 2.0f - wv, y, 3.0f, 6.0f}, Color{255, 210, 120, 200}, BlendMode::Additive);
        }
    }
    const Color light{255, 120, 50, 255};
    if (pool)
        for (float x = b.x + 40.0f; x < b.right(); x += 120.0f) ctx.lights.add({x, surfaceY}, 130.0f, light);
    else
        for (float y = b.y + 60.0f; y < b.bottom(); y += 140.0f) ctx.lights.add({b.center().x, y}, 140.0f, light);
}

void DamageZoneEntity::renderPlasma(RenderContext& ctx) const {
    // Star plasma (2.33): a pool (wider than tall: a boiling surface) or a curtain (taller
    // than wide: shimmering edges), fading in when switched on.
    const float a = shown_;
    auto fx = ctx.res.animation("animations/plasma_flood.json");
    const AnimationClip* surf = fx && fx->texture ? fx->clip("surface") : nullptr;
    const AnimationClip* body = fx && fx->texture ? fx->clip("body") : nullptr;
    const bool pool = box.w >= box.h;
    if (pool) {
        ctx.r.fillWorldRect(box, Color{120, 20, 92, static_cast<uint8_t>(225 * a)});
        if (surf && body) {
            int frame = static_cast<int>(ctx.time * 10.0f);
            float fw = static_cast<float>(fx->frameWidth), fh = static_cast<float>(fx->frameHeight);
            for (float x = box.x; x < box.right(); x += fw) {
                float wdt = std::min(fw, box.right() - x);
                Rect s = fx->frameRect(*surf, frame % std::max(1, surf->frameCount()));
                s.w = s.w * (wdt / fw);
                ctx.r.drawWorldTexture(*fx->texture, &s, {x, box.y - fh * 0.5f, wdt, fh}, Color{255, 255, 255, static_cast<uint8_t>(255 * a)});
                for (float y = box.y + fh * 0.5f; y < box.bottom(); y += fh) {
                    Rect b = fx->frameRect(*body, (frame + static_cast<int>(x / fw)) % std::max(1, body->frameCount()));
                    float hgt = std::min(fh, box.bottom() - y);
                    b.w = b.w * (wdt / fw);
                    b.h = b.h * (hgt / fh);
                    ctx.r.drawWorldTexture(*fx->texture, &b, {x, y, wdt, hgt}, Color{255, 255, 255, static_cast<uint8_t>(255 * a)});
                }
            }
        }
    } else {
        ctx.r.fillWorldRect(box, Color{255, 90, 200, static_cast<uint8_t>(70 * a)}, BlendMode::Additive);
        ctx.r.fillWorldRect(box, Color{90, 10, 70, static_cast<uint8_t>(90 * a)});
        // Bright, wavering inner edges and drifting bands of light.
        for (float y = box.y; y < box.bottom(); y += 6.0f) {
            float wv = 2.0f * dmath::sin(ctx.time * 5.0f + y * 0.07f);
            ctx.r.fillWorldRect({box.x - 1.0f + wv, y, 3.0f, 6.0f}, Color{255, 200, 240, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
            ctx.r.fillWorldRect({box.right() - 2.0f - wv, y, 3.0f, 6.0f}, Color{255, 200, 240, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
        }
        for (int k = 0; k < 6; ++k) {
            float ph = std::fmod(ctx.time * 0.25f + static_cast<float>(k) / 6.0f, 1.0f);
            float y = box.bottom() - ph * box.h;
            ctx.r.fillWorldRect({box.x, y, box.w, 3.0f}, Color{255, 150, 230, static_cast<uint8_t>(60 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    }
    for (float x = box.x + 40.0f; x < box.right(); x += 120.0f)
        ctx.lights.add({x, pool ? box.y : box.center().y}, 120.0f, Color{255, 110, 220, static_cast<uint8_t>(255 * a)});
    if (!pool)
        for (float y = box.y + 60.0f; y < box.bottom(); y += 140.0f) ctx.lights.add({box.center().x, y}, 140.0f, Color{255, 110, 220, static_cast<uint8_t>(255 * a)});
}

void DamageZoneEntity::renderEmbers(RenderContext& ctx) const {
    // The embers under the ash of the Phénix's nest (2.56): a pool (grey ash crusted over a bed
    // of embers that breathe, their glow showing through, sparks rising) or a curtain (a wall of
    // fire, its heat wavering, ash swept up in it), fading in when switched on. Display only.
    const float a = shown_;
    const float t = ctx.time;
    const bool pool = box.w >= box.h;
    if (pool) {
        ctx.r.fillWorldRect(box, Color{46, 26, 22, static_cast<uint8_t>(235 * a)});
        ctx.r.fillWorldRect({box.x, box.y + 6.0f, box.w, std::max(0.0f, box.h - 6.0f)}, Color{255, 90, 30, static_cast<uint8_t>(40 * a)},
                            BlendMode::Additive);
        for (float x = box.x; x < box.right(); x += 8.0f) {
            const float wdt = std::min(8.0f, box.right() - x);
            const float breath = 0.5f + 0.5f * dmath::sin(t * 1.6f + x * 0.031f) * dmath::sin(t * 0.7f + x * 0.013f);
            ctx.r.fillWorldRect({x, box.y - 1.0f, wdt, 4.0f}, Color{120, 108, 100, static_cast<uint8_t>(230 * a)});
            ctx.r.fillWorldRect({x, box.y + 3.0f, wdt, 3.0f},
                                Color{255, static_cast<uint8_t>(110 + 90 * breath), 40, static_cast<uint8_t>(200 * a * breath)}, BlendMode::Additive);
        }
        const int sparks = static_cast<int>(box.w / 40.0f);
        for (int k = 0; k < sparks; ++k) {
            const float ph = std::fmod(t * 0.6f + static_cast<float>(k) * 0.618f, 1.0f);
            const float x = box.x + std::fmod(static_cast<float>(k) * 97.3f, box.w) + 5.0f * dmath::sin(t * 2.0f + static_cast<float>(k));
            const float y = box.y - ph * 60.0f;
            ctx.r.fillWorldRect({x, y, 2.0f, 2.0f}, Color{255, 190, 90, static_cast<uint8_t>(220 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    } else {
        ctx.r.fillWorldRect(box, Color{255, 100, 40, static_cast<uint8_t>(70 * a)}, BlendMode::Additive);
        ctx.r.fillWorldRect(box, Color{60, 20, 14, static_cast<uint8_t>(100 * a)});
        for (float y = box.y; y < box.bottom(); y += 6.0f) {
            const float wv = 2.0f * dmath::sin(t * 6.0f + y * 0.09f);
            ctx.r.fillWorldRect({box.x - 1.0f + wv, y, 3.0f, 6.0f}, Color{255, 200, 120, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
            ctx.r.fillWorldRect({box.right() - 2.0f - wv, y, 3.0f, 6.0f}, Color{255, 200, 120, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
        }
        for (int k = 0; k < 8; ++k) {
            const float ph = std::fmod(t * 0.5f + static_cast<float>(k) / 8.0f, 1.0f);
            const float x = box.x + box.w * (0.1f + 0.8f * std::fmod(static_cast<float>(k) * 0.381f, 1.0f));
            const float y = box.bottom() - ph * box.h;
            ctx.r.fillWorldRect({x, y, 3.0f, 18.0f}, Color{255, 170, 80, static_cast<uint8_t>(80 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    }
    const Color glow{255, 120, 50, static_cast<uint8_t>(255 * a)};
    for (float x = box.x + 40.0f; x < box.right(); x += 120.0f) ctx.lights.add({x, pool ? box.y : box.center().y}, 120.0f, glow);
    if (!pool)
        for (float y = box.y + 60.0f; y < box.bottom(); y += 140.0f) ctx.lights.add({box.center().x, y}, 140.0f, glow);
}

void DamageZoneEntity::renderSouls(RenderContext& ctx) const {
    // The ossuary of the Liche's crypt (2.57): a pool (a heap of bones and skulls under a mist of
    // soul fire, wisps rising out of it) or a curtain (a wall of wailing souls, pale faces drifting
    // up through it), fading in when switched on. Display only.
    const float a = shown_;
    const float t = ctx.time;
    const bool pool = box.w >= box.h;
    const Color soul{150, 240, 220, 255};
    if (pool) {
        ctx.r.fillWorldRect(box, Color{18, 22, 30, static_cast<uint8_t>(235 * a)});
        // Bones heaped in it: pale shafts and skulls catching the light of the mist.
        for (float x = box.x + 3.0f; x < box.right() - 3.0f; x += 9.0f) {
            const float k = std::fmod(x * 0.618f, 1.0f);
            const float y = box.y + 3.0f + 5.0f * std::fmod(x * 0.377f, 1.0f);
            const float ang = (k - 0.5f) * 1.6f;
            const Vec2 d{dmath::cos(ang) * 4.0f, dmath::sin(ang) * 4.0f};
            ctx.r.drawWorldLine({x - d.x, y - d.y}, {x + d.x, y + d.y}, Color{196, 188, 166, static_cast<uint8_t>(210 * a)},
                                std::max(1.0f, ctx.r.view().scale * 0.7f));
            if (static_cast<int>(x) % 5 == 0) ctx.r.fillWorldCircle({x, y + 5.0f}, 2.6f, Color{214, 206, 184, static_cast<uint8_t>(220 * a)});
        }
        for (float x = box.x; x < box.right(); x += 8.0f) {
            const float wdt = std::min(8.0f, box.right() - x);
            const float breath = 0.5f + 0.5f * dmath::sin(t * 1.3f + x * 0.027f) * dmath::sin(t * 0.6f + x * 0.011f);
            ctx.r.fillWorldRect({x, box.y - 3.0f, wdt, 6.0f}, soul.withAlpha(static_cast<uint8_t>(60 * a * breath)), BlendMode::Additive);
        }
        const int wisps = static_cast<int>(box.w / 36.0f);
        for (int k = 0; k < wisps; ++k) {
            const float ph = std::fmod(t * 0.45f + static_cast<float>(k) * 0.618f, 1.0f);
            const float x = box.x + std::fmod(static_cast<float>(k) * 89.3f, box.w) + 6.0f * dmath::sin(t * 1.6f + static_cast<float>(k));
            const float y = box.y - ph * 70.0f;
            ctx.r.drawWorldGlow({x, y}, 4.0f + 3.0f * (1.0f - ph), soul.withAlpha(static_cast<uint8_t>(170 * a * (1.0f - ph))));
        }
    } else {
        ctx.r.fillWorldRect(box, Color{20, 30, 40, static_cast<uint8_t>(110 * a)});
        ctx.r.fillWorldRect(box, soul.withAlpha(static_cast<uint8_t>(45 * a)), BlendMode::Additive);
        for (float y = box.y; y < box.bottom(); y += 6.0f) {
            const float wv = 2.0f * dmath::sin(t * 4.0f + y * 0.07f);
            ctx.r.fillWorldRect({box.x - 1.0f + wv, y, 3.0f, 6.0f}, soul.withAlpha(static_cast<uint8_t>(190 * a)), BlendMode::Additive);
            ctx.r.fillWorldRect({box.right() - 2.0f - wv, y, 3.0f, 6.0f}, soul.withAlpha(static_cast<uint8_t>(190 * a)), BlendMode::Additive);
        }
        for (int k = 0; k < 7; ++k) {
            // A wailing face drifting up: a pale glow, two dark eyes, an open mouth.
            const float ph = std::fmod(t * 0.22f + static_cast<float>(k) / 7.0f, 1.0f);
            const float x = box.x + box.w * (0.2f + 0.6f * std::fmod(static_cast<float>(k) * 0.381f, 1.0f)) + 4.0f * dmath::sin(t + static_cast<float>(k));
            const float y = box.bottom() - ph * box.h;
            const uint8_t fa = static_cast<uint8_t>(150 * a * dmath::sin(ph * 3.1415927f));
            ctx.r.drawWorldGlow({x, y}, 9.0f, soul.withAlpha(fa));
            ctx.r.fillWorldCircle({x - 2.5f, y - 1.5f}, 1.4f, Color{10, 16, 24, fa});
            ctx.r.fillWorldCircle({x + 2.5f, y - 1.5f}, 1.4f, Color{10, 16, 24, fa});
            ctx.r.fillWorldRect({x - 1.2f, y + 1.5f, 2.4f, 3.0f}, Color{10, 16, 24, fa});
        }
    }
    const Color glow = soul.withAlpha(static_cast<uint8_t>(255 * a));
    for (float x = box.x + 40.0f; x < box.right(); x += 120.0f) ctx.lights.add({x, pool ? box.y : box.center().y}, 110.0f, glow);
    if (!pool)
        for (float y = box.y + 60.0f; y < box.bottom(); y += 140.0f) ctx.lights.add({box.center().x, y}, 130.0f, glow);
}

void DamageZoneEntity::renderAbyss(RenderContext& ctx) const {
    // The dark water of the abyss (2.53, the Kraken's arena): a pool (a swell lit in cyan, foam
    // on its crests, bubbles rising) or a curtain (a wall of falling water, wavering edges),
    // fading in when switched on. Display only (time-driven).
    const float a = shown_;
    const float t = ctx.time;
    const bool pool = box.w >= box.h;
    const Color deep{10, 22, 52, static_cast<uint8_t>(228 * a)};
    const Color crest{122, 230, 255, static_cast<uint8_t>(210 * a)};
    if (pool) {
        ctx.r.fillWorldRect(box, deep);
        ctx.r.fillWorldRect({box.x, box.y + 8.0f, box.w, std::max(0.0f, box.h - 8.0f)}, Color{60, 150, 230, static_cast<uint8_t>(30 * a)},
                            BlendMode::Additive);
        for (float x = box.x; x < box.right(); x += 8.0f) {
            const float wdt = std::min(8.0f, box.right() - x);
            const float swell = 2.5f * dmath::sin(t * 1.8f + x * 0.025f) + 1.2f * dmath::sin(t * 4.3f + x * 0.11f);
            ctx.r.fillWorldRect({x, box.y - 2.0f + swell, wdt, 4.0f}, Color{30, 70, 120, static_cast<uint8_t>(240 * a)});
            ctx.r.fillWorldRect({x, box.y - 2.0f + swell, wdt, 2.0f}, crest, BlendMode::Additive);
            if (swell < -2.2f) ctx.r.fillWorldRect({x, box.y - 4.0f + swell, wdt, 2.0f}, Color{235, 250, 255, static_cast<uint8_t>(180 * a)}, BlendMode::Additive);
        }
        const int bubbles = static_cast<int>(box.w / 48.0f);
        for (int k = 0; k < bubbles; ++k) {
            const float ph = std::fmod(t * 0.5f + static_cast<float>(k) * 0.618f, 1.0f);
            const float x = box.x + std::fmod(static_cast<float>(k) * 97.3f, box.w) + 3.0f * dmath::sin(t * 3.0f + static_cast<float>(k));
            const float y = box.bottom() - ph * box.h;
            ctx.r.drawWorldCircle({x, y}, 1.2f + 1.2f * ph, Color{200, 240, 255, static_cast<uint8_t>(140 * a * ph)}, 1.0f);
        }
    } else {
        ctx.r.fillWorldRect(box, deep);
        ctx.r.fillWorldRect(box, Color{60, 150, 230, static_cast<uint8_t>(40 * a)}, BlendMode::Additive);
        for (float y = box.y; y < box.bottom(); y += 6.0f) {
            const float wv = 2.0f * dmath::sin(t * 5.0f + y * 0.07f);
            ctx.r.fillWorldRect({box.x - 1.0f + wv, y, 3.0f, 6.0f}, crest, BlendMode::Additive);
            ctx.r.fillWorldRect({box.right() - 2.0f - wv, y, 3.0f, 6.0f}, crest, BlendMode::Additive);
        }
        // Streaks of water falling down the curtain.
        for (int k = 0; k < 8; ++k) {
            const float ph = std::fmod(t * 0.6f + static_cast<float>(k) / 8.0f, 1.0f);
            const float x = box.x + box.w * (0.1f + 0.8f * std::fmod(static_cast<float>(k) * 0.381f, 1.0f));
            const float y = box.y + ph * box.h;
            ctx.r.fillWorldRect({x, y, 2.0f, 26.0f}, Color{170, 230, 255, static_cast<uint8_t>(70 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    }
    const Color glow{90, 200, 255, static_cast<uint8_t>(255 * a)};
    for (float x = box.x + 40.0f; x < box.right(); x += 120.0f) ctx.lights.add({x, pool ? box.y : box.center().y}, 120.0f, glow);
    if (!pool)
        for (float y = box.y + 60.0f; y < box.bottom(); y += 140.0f) ctx.lights.add({box.center().x, y}, 140.0f, glow);
}

// ---------------------------------------------------------------- slow zone

void SlowZoneEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    w.player().speedMultiplier = std::min(w.player().speedMultiplier, obj_.prop<float>("factor", 0.5f));
}

namespace {
// Cloud look of the slow zones: tint + particle presets per style.
struct CloudStyle {
    Color tint;
    const char* motes;
    const char* puffs;
};
CloudStyle cloudStyle(const std::string& s) {
    if (s == "frost") return {{127, 246, 255, 255}, "frosted", "frost_mist"};
    if (s == "ash") return {{170, 150, 140, 255}, "ash_fall", "ash_cloud"};
    return {{224, 138, 255, 255}, "spores", "spore_cloud"};
}
}  // namespace

void SlowZoneEntity::fixedUpdate(GameWorld& w, float dt) {
    if (!obj_.prop<bool>("visible", true)) return;
    CloudStyle st = cloudStyle(obj_.propStr("style", "spores"));
    float area = std::max(1.0f, box.w * box.h / 3000.0f);
    fxTimer_ -= dt;
    if (fxTimer_ <= 0.0f) {
        fxTimer_ = std::max(0.04f, 0.9f / area);
        w.particles().emit(st.motes, {w.fxRng().range(box.x, box.right()), w.fxRng().range(box.y, box.bottom())}, 1.0f);
        if (w.fxRng().chance(0.85f))
            w.particles().emit(st.puffs, {w.fxRng().range(box.x + 8, box.right() - 8), w.fxRng().range(box.y + 8, box.bottom() - 4)},
                               1.0f);
    }
}

void SlowZoneEntity::render(RenderContext& ctx) {
    if (ctx.editor) ctx.r.drawWorldRect(box, {120, 120, 255, 160});
    if (!obj_.prop<bool>("visible", true)) return;
    CloudStyle st = cloudStyle(obj_.propStr("style", "spores"));
    // Soft, slowly drifting glowing blobs (no hard edge) give the cloud its volume.
    int n = std::clamp(static_cast<int>(box.w * box.h / 1100.0f), 4, 40);
    for (int i = 0; i < n; ++i) {
        float hx = std::fmod(dmath::sin(static_cast<float>(i) * 12.9898f + box.x) * 43758.5453f, 1.0f);
        float hy = std::fmod(dmath::sin(static_cast<float>(i) * 78.233f + box.y) * 12345.678f, 1.0f);
        hx = hx < 0 ? hx + 1.0f : hx;
        hy = hy < 0 ? hy + 1.0f : hy;
        float drift = std::fmod(hx + ctx.time * (0.008f + 0.004f * static_cast<float>(i % 3)), 1.0f);
        Vec2 c{box.x + 10.0f + drift * std::max(0.0f, box.w - 20.0f),
               box.y + 10.0f + hy * std::max(0.0f, box.h - 20.0f) + dmath::sin(ctx.time * 0.7f + static_cast<float>(i)) * 3.0f};
        float r = 18.0f + 16.0f * std::fmod(hx * 7.0f, 1.0f);
        ctx.r.drawWorldGlow(c, r, st.tint.withAlpha(52));
    }
    ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.6f, st.tint.withAlpha(120));
}

// -------------------------------------------------------------------- laser

void LaserEntity::init(GameWorld& w) {
    enabled_ = obj_.prop<bool>("active", true);
    cycle_ = obj_.prop<float>("phase", 0.0f);
    emitter_.load(w.resources(), "animations/laser_emitter.json", "off");
    beam_ = w.resources().texture("sprites/objects/laser_beam.png");
    end_ = origin();
}

Vec2 LaserEntity::origin() const {
    float a = degToRad(obj_.rotation);
    Vec2 dir{dmath::cos(a), dmath::sin(a)};
    return box.center() - dir * (box.w * 0.5f - 8.0f);
}

Rect LaserEntity::reach() const {
    const float a = degToRad(obj_.rotation);
    const Vec2 o = origin();
    const Vec2 far = o + Vec2{dmath::cos(a), dmath::sin(a)} * (box.w - 8.0f);
    return Rect{std::min(o.x, far.x), std::min(o.y, far.y), std::abs(far.x - o.x), std::abs(far.y - o.y)}.expanded(4.0f);
}

void LaserEntity::fixedUpdate(GameWorld& w, float dt) {
    if (closing_.waiting && closing_.ready(w)) enabled_ = true;  // 2.80: the whole team is through
    emitter_.anim.update(dt);
    hitTimer_ = std::max(0.0f, hitTimer_ - dt);
    float onT = obj_.prop<float>("onTime", 1.5f), offT = obj_.prop<float>("offTime", 1.5f);
    float warnT = std::min(obj_.prop<float>("warnTime", 0.6f), offT);
    bool wasOn = on_;
    if (!enabled_) {
        on_ = warning_ = false;
    } else if (offT <= 0.0f) {
        on_ = true;
        warning_ = false;
    } else {
        cycle_ += dt;
        float period = onT + offT;
        float t = std::fmod(cycle_, period);
        on_ = t < onT;
        warning_ = !on_ && t > period - warnT;
    }
    if (on_ != wasOn) {
        emitter_.anim.play(on_ ? "on" : "off", true);
        if (on_) w.playSound("sounds/laser_fire.wav", box.center(), 0.5f);
    }
    if (warning_ && emitter_.anim.current() != "charging") {
        emitter_.anim.play("charging", true);
        w.playSound("sounds/laser_charge.wav", box.center(), 0.35f);
    }
    float a = degToRad(obj_.rotation);
    Vec2 o = origin();
    Vec2 far = o + Vec2{dmath::cos(a), dmath::sin(a)} * (box.w - 8.0f);
    end_ = far;
    if (on_) {
        if (auto t = w.physics().raycast(o + Vec2{dmath::cos(a), dmath::sin(a)} * 9.0f, far)) end_ = o + (far - o) * *t;
        if (hitTimer_ <= 0.0f) {
            bool hurt = false;
            w.forEachPlayer([&](Player& p) {  // 2.62: every player in the beam (alone: the only one)
                if (!p.alive() || !Collision::segmentIntersectsRect(o, end_, p.box.expanded(-1))) return;
                DamageInfo d;
                d.amount = static_cast<float>(obj_.prop<int>("damage", 1));
                d.type = DamageType::Hazard;
                d.source = {p.center().x, p.center().y + 10};
                if (w.damagePlayer(d)) hurt = true;
            });
            if (hurt) hitTimer_ = 0.5f;
        }
        if (static_cast<int>(cycle_ * 30) % 3 == 0) w.particles().emit("laser_sparks", end_, 0.5f);
    }
}

bool LaserEntity::handleAction(GameWorld& w, const std::string& action, const Json& params) {
    if (action == "close") {
        // 2.80 (rules of cooperation 4): a beam that closes a way behind a team waits for the whole team.
        if (enabled_ || closing_.ask(w, reach(), params)) {
            closing_.waiting = false;
            enabled_ = true;
        }
        return true;
    }
    if (action == "enable" || action == "activate") enabled_ = true;
    else if (action == "disable" || action == "open") enabled_ = false;
    else if (action == "toggle") enabled_ = !enabled_;
    else return false;
    closing_.waiting = false;
    return true;
}

bool LaserEntity::firesIn(float seconds) const {
    if (!enabled_) return false;
    const float onT = obj_.prop<float>("onTime", 1.5f), offT = obj_.prop<float>("offTime", 1.5f);
    if (offT <= 0.0f) return true;
    return std::fmod(cycle_ + seconds, onT + offT) < onT;
}

Vec2 LaserEntity::beamTo() const { return end_; }

std::string LaserEntity::debugInfo() const { return enabled_ ? (on_ ? "actif" : "en attente") : "désactivé"; }

void LaserEntity::render(RenderContext& ctx) {
    Vec2 o = origin();
    float ang = obj_.rotation;
    // The emitter sprite rotates around its bottom-centre pivot (8 px below its centre):
    // place that pivot so the sprite centre stays on the beam origin for any rotation.
    float ar = degToRad(ang);
    emitter_.draw(ctx, o + Vec2{-8.0f * dmath::sin(ar), 8.0f * dmath::cos(ar)}, false, Color::white(), ang);
    if (ctx.editor) {
        float a = degToRad(ang);
        ctx.r.drawWorldLine(o, o + Vec2{dmath::cos(a), dmath::sin(a)} * (box.w - 8), {255, 77, 109, 120}, 1.0f);
        return;
    }
    if (warning_) {
        float flick = 0.5f + 0.5f * dmath::sin(ctx.time * 40.0f);
        ctx.r.drawWorldLine(o, end_, Color{255, 77, 109, static_cast<uint8_t>(60 + 80 * flick)}, 1.0f);
    }
    if (on_ && beam_) {
        Vec2 d = end_ - o;
        float len = d.length();
        int frame = static_cast<int>(ctx.time * 16.0f) % 4;
        for (float s = 0; s < len; s += 16.0f) {
            float segLen = std::min(16.0f, len - s);
            Vec2 c = o + d.normalized() * (s + segLen * 0.5f);
            Rect src{frame * 16.0f, 0, segLen, 8};
            ctx.r.drawWorldTexture(*beam_, &src, Rect::fromCenter(c, {segLen, 8}), Color::white(), BlendMode::Additive,
                                   false, ang);
        }
        ctx.lights.add((o + end_) * 0.5f, len * 0.5f + 20.0f, {255, 77, 109, 255});
        ctx.r.drawWorldGlow(end_, 10.0f, {255, 140, 160, 200});
    }
}

}  // namespace cosmic
