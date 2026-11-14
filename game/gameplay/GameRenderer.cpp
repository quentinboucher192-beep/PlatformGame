#include "game/gameplay/GameRenderer.h"

#include <cmath>

#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

void GameRenderer::build(Renderer& r, ResourceManager& res, const GameWorld& world) {
    const LevelData& lvl = world.level();
    parallax_.load(res, lvl.background, lvl.bounds.center().y);
    terrain_.build(r, res, lvl);
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
            if (e->box.expanded(64).intersects(vis)) e->render(ctx);
        world.player().render(ctx);
        drawProjectiles(r, world, alpha, lights_);
        world.particles().render(r, true, view.visibleWorld());
    };

    for (const auto& layer : lvl.layers) {
        if (layer.foreground) drawActors();
        if (layer.visible) terrain_.render(r, layer.id);
        drawLayerEntities(layer);
    }
    drawActors();

    if (!opt.mapView) {
        // Telegraphs read on top of everything in the world.
        for (const auto& e : world.enemies())
            if (e->box.expanded(200).intersects(vis)) e->renderTelegraph(ctx);
        parallax_.render(r, view, true);
    }

    // Dynamic lighting (multiplied over the world)
    lastLights_ = lights_.count();
    if (opt.lighting) {
        Color ambient = lvl.ambientLight;
        if (opt.editor) ambient = Color::lerp(ambient, Color::white(), 0.45f);
        lights_.apply(r, view, ambient);
    }

    // Screen flash
    Color f = opt.mapView ? Color{0, 0, 0, 0} : world.flashColor();
    if (f.a > 0) r.fillRect({0, 0, static_cast<float>(r.width()), static_cast<float>(r.height())}, f, BlendMode::Additive);

    if (opt.debug) drawDebug(r, world);
}

void GameRenderer::drawProjectiles(Renderer& r, GameWorld& world, float alpha, LightMap& lights) {
    for (const auto& p : world.projectiles()) {
        Vec2 pos = lerpv(p.prevPos, p.pos, alpha);
        lights.add(pos, p.lightRadius, p.light);
        if (p.clip == "shockwave") {
            float t = std::fmod(p.age * 3.0f, 1.0f);
            if (ringTex_) {
                Rect dst = Rect::fromCenter(pos + Vec2{0, -2}, {18.0f + t * 4, 14.0f});
                r.drawWorldTexture(*ringTex_, nullptr, dst, Color{255, 179, 71, 230}, BlendMode::Additive);
            }
            r.drawWorldGlow(pos, 12.0f, {255, 200, 120, 160});
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
