#include "game/gameplay/GameRenderer.h"

#include <algorithm>
#include <cmath>

#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/PowerFx.h"

namespace cosmic {

void GameRenderer::build(Renderer& r, ResourceManager& res, const GameWorld& world) {
    const LevelData& lvl = world.level();
    parallax_.load(res, lvl.background, lvl.bounds.center().y);
    terrain_.build(r, res, lvl);
    projectiles_ = res.animation("animations/projectiles.json");
    ringTex_ = res.texture("textures/fx/ring.png", false);
}

void GameRenderer::buildProgressive(ResourceManager& res, const GameWorld& world) {
    const LevelData& lvl = world.level();
    parallax_.load(res, lvl.background, lvl.bounds.center().y);
    terrain_.prepare(res, lvl);
    projectiles_ = res.animation("animations/projectiles.json");
    ringTex_ = res.texture("textures/fx/ring.png", false);
}

void GameRenderer::rebuildTerrain(Renderer& r, ResourceManager& res, const GameWorld& world) {
    terrain_.build(r, res, world.level());
    const LevelData& lvl = world.level();
    parallax_.load(res, lvl.background, lvl.bounds.center().y);
}

void GameRenderer::render(Renderer& r, ResourceManager& res, GameWorld& world, const WorldView& view, float alpha,
                          const RenderOptions& opt) {
    const LevelData& lvl = world.level();
    r.setView(view);
    lights_.clear();
    RenderContext ctx{r, res, lights_, alpha, world.time(), opt.editor};

    // Background colour + parallax
    Color bg = lvl.backgroundColor;
    if (opt.mapView) r.fillRect({view.origin.x, view.origin.y, static_cast<float>(view.screenW), static_cast<float>(view.screenH)}, bg);
    else r.fillRect({0, 0, static_cast<float>(r.width()), static_cast<float>(r.height())}, bg);
    parallax_.update(0.0f);
    parallax_.render(r, view, false);

    // Secret areas fade the foreground decor covering them.
    std::vector<const SecretAreaEntity*> secrets;
    for (const auto& e : world.entities())
        if (auto* s = dynamic_cast<const SecretAreaEntity*>(e.get()); s && s->reveal() > 0.0f) secrets.push_back(s);

    const Rect vis = view.visibleWorld().expanded(96);
    auto drawLayerEntities = [&](const LevelLayer& layer) {
        if (!layer.visible) return;
        for (const auto& e : world.entities()) {
            if (e->removed || !e->visible) continue;
            if (e->object().layer != layer.id) continue;
            if (e->drawnOverActors() && !opt.editor) continue;  // 2.71: after the actors (drawActors)
            if (!e->box.expanded(64).intersects(vis) && e->type() != "moving_platform" && e->type() != "laser") continue;
            if (layer.foreground && !secrets.empty()) {
                if (auto* d = dynamic_cast<DecorEntity*>(e.get())) {
                    float fade = 1.0f;
                    for (const auto* s : secrets)
                        if (s->box.expanded(24).intersects(d->box)) fade = std::min(fade, 1.0f - 0.85f * s->reveal());
                    d->fadeOverride = fade;
                }
            }
            e->render(ctx);
        }
    };

    bool actorsDrawn = false;
    auto drawActors = [&]() {
        if (actorsDrawn) return;
        actorsDrawn = true;
        world.particles().render(r, false, view.visibleWorld());
        for (const auto& e : world.enemies())
            if (e->box.expanded(64).intersects(vis)) {
                e->render(ctx);
                renderFrozenEnemy(ctx, *e);  // Nova de givre (2.19)
            }
        renderDecoy(ctx, world);  // Leurre holographique (2.19)
        if (world.coop())
            drawTeam(ctx, world);  // 2.62: every player of the team, souls and ghosts
        else
            world.player().render(ctx);
        drawProjectiles(r, world, alpha, lights_, opt.threats);
        world.particles().render(r, true, view.visibleWorld());
        // 2.71: what covers the actors (the raids' rising magma): nothing shows through it.
        if (!opt.editor)
            for (const auto& e : world.entities())
                if (e->drawnOverActors() && !e->removed && e->visible && e->box.expanded(64).intersects(vis)) e->render(ctx);
    };

    for (const auto& layer : lvl.layers) {
        if (layer.foreground) drawActors();
        if (layer.visible) terrain_.render(r, layer.id);
        drawLayerEntities(layer);
    }
    drawActors();

    if (!opt.mapView) {
        // Lasting effects of the guardians' attacks (frost, lava, thorns, wells), then the
        // telegraphs, on top of everything in the world: the boss's threats (2.60: the HUD
        // fades over them, RenderOptions::threats).
        r.setListener(opt.threats);
        world.hazards().render(ctx);
        for (const auto& e : world.enemies())
            if (e->box.expanded(200).intersects(vis) && !e->frozen()) e->renderTelegraph(ctx);  // frozen: no threat
        r.setListener(nullptr);
        parallax_.render(r, view, true);
    }

    // Dynamic lighting (multiplied over the world)
    lastLights_ = lights_.count();
    if (opt.lighting) {
        Color ambient = lvl.ambientLight;
        if (opt.editor) ambient = Color::lerp(ambient, Color::white(), 0.45f);
        // A guardian that swallows the light (Nyxara, 2.19): the arena darkens from its phase.
        if (const Enemy* b = world.activeBoss(); b && b->alive() && b->def().darkPhase > 0 && b->phaseIndex() + 1 >= b->def().darkPhase)
            ambient = Color::lerp(ambient, Color{6, 4, 14, 255}, 0.6f);
        lights_.apply(r, view, ambient);
    }

    // The Phénix's ashes (2.56): over the lit world, under the flash and the HUD.
    if (!opt.mapView && !opt.editor) drawAshVeil(r, world, view, alpha);

    // Screen flash
    Color f = opt.mapView ? Color{0, 0, 0, 0} : world.flashColor();
    if (f.a > 0) r.fillRect({0, 0, static_cast<float>(r.width()), static_cast<float>(r.height())}, f, BlendMode::Additive);

    if (opt.debug) drawDebug(r, world);
}

Color GameRenderer::teamColor(int i) const {
    static const Color kDefault[3] = {{62, 230, 214, 255}, {255, 122, 184, 255}, {255, 210, 122, 255}};
    if (i >= 0 && i < static_cast<int>(teamColors_.size())) return teamColors_[static_cast<size_t>(i)];
    return kDefault[std::clamp(i, 0, 2)];
}

void GameRenderer::drawTeam(RenderContext& ctx, GameWorld& world) {
    using Presence = GameWorld::Presence;
    for (int i = 0; i < world.teamSize(); ++i) {
        const GameWorld::TeamMember& m = world.member(i);
        const Player& p = world.playerAt(i);
        const Color c = teamColor(i);
        if (m.presence == Presence::Playing) {
            p.render(ctx);
            continue;
        }
        if (m.presence == Presence::Down) {
            // Its soul: a light of its colour, bobbing; the revival fills a ring around it.
            const float bob = std::sin(ctx.time * 2.6f + static_cast<float>(i)) * 2.5f;
            const Vec2 at = m.soul + Vec2{0.0f, -16.0f + bob};
            const bool canRevive = world.revivesLeft() > 0;
            const uint8_t a = canRevive ? 220 : 90;
            ctx.r.drawWorldGlow(at, 16.0f, Color{c.r, c.g, c.b, static_cast<uint8_t>(a / 2)});
            ctx.r.drawWorldGlow(at, 6.0f, Color{255, 255, 255, a});
            ctx.lights.add(at, 60.0f, c);
            if (canRevive) {
                ctx.r.drawWorldCircle(at, 12.0f, Color{c.r, c.g, c.b, 90}, 1.0f);
                const float k = std::clamp(m.revive / GameWorld::kReviveSeconds, 0.0f, 1.0f);
                const int seg = static_cast<int>(24.0f * k);
                for (int s = 0; s < seg; ++s) {
                    const float a0 = -1.5707963f + 6.2831853f * static_cast<float>(s) / 24.0f;
                    const float a1 = -1.5707963f + 6.2831853f * static_cast<float>(s + 1) / 24.0f;
                    ctx.r.drawWorldLine(at + Vec2{std::cos(a0), std::sin(a0)} * 12.0f, at + Vec2{std::cos(a1), std::sin(a1)} * 12.0f,
                                        Color{255, 222, 140, 255}, 2.0f);
                }
            }
            continue;
        }
        if (m.presence == Presence::Standby && p.anim.set() && p.anim.set()->texture) {
            // In standby: a grey ghost where it stopped, untouchable.
            const Texture& tex = p.skin ? *p.skin : *p.anim.set()->texture;
            const Rect src = p.anim.sourceRect();
            const Rect dst = p.anim.destRect(p.box.bottomCenter(), p.facing < 0.0f);
            const uint8_t pulse = static_cast<uint8_t>(70 + 30 * std::sin(ctx.time * 3.0f));
            ctx.r.drawWorldTexture(tex, &src, dst, Color{150, 165, 200, pulse}, BlendMode::Alpha, p.facing < 0.0f);
        }
    }
}

void GameRenderer::drawAshVeil(Renderer& r, GameWorld& world, const WorldView& view, float alpha) {
    const float now = world.time();
    const float dt = ashClock_ < 0.0f ? 0.0f : std::clamp(now - ashClock_, 0.0f, 0.1f);
    ashClock_ = now;
    const Enemy* boss = world.activeBoss();
    const bool on = boss && boss->ashVeil();
    ashVeil_ = std::clamp(ashVeil_ + (on ? 0.6f : -0.9f) * dt, 0.0f, 1.0f);
    if (ashVeil_ <= 0.01f) return;
    const float k = ashVeil_;
    const float W = static_cast<float>(view.screenW), H = static_cast<float>(view.screenH);
    const Vec2 o = view.origin;
    const Player& p = world.player();
    const Vec2 nova = o + view.toScreen(lerpv(p.prevBox.center(), p.box.center(), alpha));
    // The veil: a grid whose corners are clear near Nova and grey-brown far from her.
    const float clear = 95.0f * view.scale, thick = 330.0f * view.scale;
    constexpr int kCols = 24, kRows = 14;
    std::vector<SDL_Vertex> verts;
    std::vector<int> idx;
    verts.reserve((kCols + 1) * (kRows + 1));
    for (int j = 0; j <= kRows; ++j)
        for (int i = 0; i <= kCols; ++i) {
            const float x = o.x + W * static_cast<float>(i) / kCols, y = o.y + H * static_cast<float>(j) / kRows;
            const float d = std::sqrt((x - nova.x) * (x - nova.x) + (y - nova.y) * (y - nova.y));
            float a = std::clamp((d - clear) / std::max(1.0f, thick - clear), 0.0f, 1.0f);
            a = a * a * (3.0f - 2.0f * a);
            SDL_Vertex v{};
            v.position = {x, y};
            v.color = {0.30f, 0.27f, 0.25f, 0.62f * a * k};
            verts.push_back(v);
        }
    for (int j = 0; j < kRows; ++j)
        for (int i = 0; i < kCols; ++i) {
            const int a = j * (kCols + 1) + i, b = a + 1, c = a + kCols + 1, d = c + 1;
            idx.insert(idx.end(), {a, b, c, b, d, c});
        }
    r.drawGeometry(nullptr, verts, idx, BlendMode::Alpha);
    // The ash falling slantwise (screen space, wrapping), a few embers still glowing in it.
    const float t = now;
    for (int n = 0; n < 140; ++n) {
        const float fx = std::fmod(static_cast<float>(n) * 0.6180339f, 1.0f), fy = std::fmod(static_cast<float>(n) * 0.4142135f, 1.0f);
        const float speed = 0.06f + 0.05f * std::fmod(static_cast<float>(n) * 0.7548777f, 1.0f);
        float x = std::fmod(fx * W + t * speed * W * 0.35f + 9.0f * std::sin(t * 1.3f + static_cast<float>(n)), W);
        float y = std::fmod(fy * H + t * speed * H, H);
        if (x < 0.0f) x += W;
        const float size = (1.2f + 1.6f * std::fmod(static_cast<float>(n) * 0.3819660f, 1.0f)) * std::max(1.0f, view.scale * 0.5f);
        const bool ember = n % 9 == 0;
        const Color c = ember ? Color{255, 150, 80, static_cast<uint8_t>(200 * k)} : Color{170, 160, 152, static_cast<uint8_t>(170 * k)};
        if (ember) r.drawGlow(o + Vec2{x, y}, size * 3.0f, c.withAlpha(static_cast<uint8_t>(90 * k)));
        r.fillRect({o.x + x, o.y + y, size, size}, c);
    }
}

void GameRenderer::drawProjectiles(Renderer& r, GameWorld& world, float alpha, LightMap& lights, CoverageProbe* threats) {
    struct Quiet {  // the listener is off again after the shots, whatever path the loop takes
        Renderer& r;
        ~Quiet() { r.setListener(nullptr); }
    } quiet{r};
    for (const auto& p : world.projectiles()) {
        // 2.60: the enemies' shots are threats (the HUD fades over them), Nova's are not.
        r.setListener(p.fromPlayer ? nullptr : threats);
        Vec2 pos = lerpv(p.prevPos, p.pos, alpha);
        lights.add(pos, p.lightRadius, p.light);
        if (p.clip == "feather") {
            // 2.56, the Phénix's feathers of light: a quill along its flight, two vanes of its
            // colour, a white-hot tip.
            const float sp = p.vel.length();
            const Vec2 d = sp > 1.0f ? p.vel / sp : Vec2{1.0f, 0.0f};
            const Vec2 n{-d.y, d.x};
            const float len = std::max(12.0f, p.radius * 3.2f), wid = std::max(3.0f, p.radius * 0.9f);
            const Vec2 tip = pos + d * (len * 0.5f), tail = pos - d * (len * 0.5f);
            const auto& v = r.view();
            const Vec2 st = v.toScreen(tip), sb = v.toScreen(tail), sl = v.toScreen(pos + n * wid - d * (len * 0.1f)),
                       sr = v.toScreen(pos - n * wid - d * (len * 0.1f));
            r.drawWorldGlow(pos, len * 0.9f, p.light.withAlpha(90));
            r.fillTriangle(st, sl, sb, p.light.withAlpha(235));
            r.fillTriangle(st, sr, sb, Color{p.light.r, p.light.g, p.light.b, 200});
            r.drawWorldLine(tail - d * 3.0f, tip, Color{255, 250, 236, 255}, std::max(1.0f, v.scale * 0.6f));
            r.drawWorldGlow(tip, wid * 1.6f, Color{255, 255, 255, 200});
            continue;
        }
        if (p.clip == "soul") {
            // 2.57, the Liche's souls in torment: a pale head of soul fire, two hollow eyes, a
            // wavering tail of its colour streaming behind it.
            const float sp = p.vel.length();
            const Vec2 d = sp > 1.0f ? p.vel / sp : Vec2{1.0f, 0.0f};
            const Vec2 n{-d.y, d.x};
            const float rad = std::max(4.0f, p.radius);
            const float wob = std::sin(p.age * 18.0f + p.pos.x * 0.05f);
            for (int i = 4; i >= 1; --i) {
                const float k = static_cast<float>(i) / 4.0f;
                const Vec2 q = pos - d * (rad * 1.3f * static_cast<float>(i)) + n * (wob * rad * 0.35f * k);
                r.drawWorldGlow(q, rad * (1.25f - 0.2f * k), p.light.withAlpha(static_cast<uint8_t>(150 * (1.0f - k * 0.7f))));
            }
            r.drawWorldGlow(pos, rad * 2.4f, p.light.withAlpha(110));
            r.fillWorldCircle(pos, rad, Color{p.light.r, p.light.g, p.light.b, 230});
            r.fillWorldCircle(pos + d * (rad * 0.15f), rad * 0.62f, Color{240, 255, 244, 240});
            r.fillWorldCircle(pos + d * (rad * 0.35f) + n * (rad * 0.3f), std::max(0.8f, rad * 0.18f), Color{20, 24, 40, 230});
            r.fillWorldCircle(pos + d * (rad * 0.35f) - n * (rad * 0.3f), std::max(0.8f, rad * 0.18f), Color{20, 24, 40, 230});
            continue;
        }
        if (p.clip == "bone") {
            // 2.57, the Liche's bones: an ivory shaft with its two knobs, spinning, a glow of soul fire.
            const float rad = std::max(4.0f, p.radius);
            const float a = p.age * 11.0f + p.pos.y * 0.01f;
            const Vec2 d{std::cos(a), std::sin(a)};
            const Vec2 n{-d.y, d.x};
            const Vec2 e0 = pos - d * (rad * 1.3f), e1 = pos + d * (rad * 1.3f);
            r.drawWorldGlow(pos, rad * 2.0f, p.light.withAlpha(90));
            r.drawWorldLine(e0, e1, Color{90, 80, 62, 255}, std::max(2.0f, rad * 0.9f * r.view().scale));
            r.drawWorldLine(e0, e1, Color{236, 226, 198, 255}, std::max(1.0f, rad * 0.55f * r.view().scale));
            for (const Vec2& e : {e0, e1}) {
                r.fillWorldCircle(e + n * (rad * 0.35f), rad * 0.42f, Color{226, 214, 184, 255});
                r.fillWorldCircle(e - n * (rad * 0.35f), rad * 0.42f, Color{200, 188, 158, 255});
            }
            continue;
        }
        if (p.clip == "shockwave") {
            // Its size follows its radius (2.52: the golem's waves are taller), its colour its light.
            float t = std::fmod(p.age * 3.0f, 1.0f);
            float k = std::max(1.0f, p.radius / 7.0f);
            if (ringTex_) {
                Rect dst = Rect::fromCenter(pos + Vec2{0, -2.0f * k}, {(18.0f + t * 4) * k, 14.0f * k});
                r.drawWorldTexture(*ringTex_, nullptr, dst, p.light.withAlpha(230), BlendMode::Additive);
            }
            r.drawWorldGlow(pos, 12.0f * k, k > 1.0f ? p.light.withAlpha(160) : Color{255, 200, 120, 160});
            continue;
        }
        if (projectiles_ && projectiles_->texture) {
            if (const AnimationClip* c = projectiles_->clip(p.clip)) {
                int frame = static_cast<int>(p.age * 14.0f) % std::max(1, c->frameCount());
                Rect src = projectiles_->frameRect(*c, frame);
                float ang = radToDeg(std::atan2(p.vel.y, p.vel.x));
                bool flip = false;
                if (p.vel.x < 0) {
                    flip = true;
                    ang = radToDeg(std::atan2(p.vel.y, -p.vel.x)) * -1.0f;
                }
                Rect dst = Rect::fromCenter(pos, {static_cast<float>(projectiles_->frameWidth),
                                                  static_cast<float>(projectiles_->frameHeight)});
                r.drawWorldTexture(*projectiles_->texture, &src, dst, Color::white(), BlendMode::Alpha, flip, ang);
                r.drawWorldGlow(pos, p.radius * 2.2f, p.light.withAlpha(110));
                continue;
            }
        }
        r.fillWorldCircle(pos, p.radius, p.light);
    }
}

void GameRenderer::drawDebug(Renderer& r, GameWorld& world) {
    const Rect vis = r.view().visibleWorld();
    for (const auto& c : world.physics().colliders()) {
        if (!c.box.intersects(vis)) continue;
        Color col = !c.enabled ? Color{120, 120, 120, 120}
                    : c.kind == ColliderKind::OneWay ? Color{255, 220, 0, 200}
                                                     : Color{0, 255, 120, 160};
        r.drawWorldRect(c.box, col, 1.0f);
    }
    const Player& p = world.player();
    r.drawWorldRect(p.box, {57, 240, 216, 255}, 2.0f);
    for (const auto& e : world.enemies()) {
        r.drawWorldRect(e->box, {255, 77, 109, 255}, 1.5f);
        r.drawWorldCircle(e->center(), e->def().detectionRange, {255, 200, 0, 60}, 1.0f);
        r.drawWorldCircle(e->center(), e->def().attackRange, {255, 60, 60, 90}, 1.0f);
    }
    for (const auto& e : world.entities()) {
        if (e->wantsPlayerOverlap() && e->overlapBox().intersects(vis)) r.drawWorldRect(e->overlapBox(), {200, 120, 255, 120});
    }
    for (const auto& pr : world.projectiles()) r.drawWorldCircle(pr.pos, pr.radius, {255, 255, 255, 200}, 1.0f);
}

}  // namespace cosmic
