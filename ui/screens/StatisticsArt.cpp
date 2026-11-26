#include "ui/screens/StatisticsArt.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"
#include "game/planets/PlanetData.h"

namespace cosmic::statsart {

namespace {

/// The point of the route at `f` (0..1).
Vec2 pointAt(const LevelProgressTrack& t, float f) {
    if (!t.valid()) return {};
    const float at = std::clamp(f, 0.0f, 1.0f) * t.length;
    auto it = std::lower_bound(t.cumulative.begin(), t.cumulative.end(), at);
    if (it == t.cumulative.begin()) return t.route.front();
    if (it == t.cumulative.end()) return t.route.back();
    const size_t j = static_cast<size_t>(it - t.cumulative.begin());
    const float seg = t.cumulative[j] - t.cumulative[j - 1];
    const float u = seg > 0.0f ? (at - t.cumulative[j - 1]) / seg : 0.0f;
    return t.route[j - 1] + (t.route[j] - t.route[j - 1]) * u;
}

/// The box of the route between `a` and `b`.
Rect routeBox(const LevelProgressTrack& t, float a, float b) {
    if (!t.valid()) return {};
    Vec2 p = pointAt(t, a), q = pointAt(t, b);
    float x0 = std::min(p.x, q.x), x1 = std::max(p.x, q.x), y0 = std::min(p.y, q.y), y1 = std::max(p.y, q.y);
    for (size_t i = 0; i < t.route.size(); ++i) {
        const float f = t.cumulative[i] / t.length;
        if (f < a || f > b) continue;
        x0 = std::min(x0, t.route[i].x);
        x1 = std::max(x1, t.route[i].x);
        y0 = std::min(y0, t.route[i].y);
        y1 = std::max(y1, t.route[i].y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

/// `win` at the proportions `aspect` (grown, never shrunk), then inside `bounds` (all of it when smaller).
Rect fitWindow(Rect win, float aspect, const Rect& bounds) {
    if (win.w / std::max(1.0f, win.h) < aspect) {
        const float nw = win.h * aspect;
        win.x -= (nw - win.w) * 0.5f;
        win.w = nw;
    } else {
        const float nh = win.w / aspect;
        win.y -= (nh - win.h) * 0.5f;
        win.h = nh;
    }
    if (bounds.w > 0.0f && bounds.h > 0.0f) {
        if (win.w >= bounds.w) win.x = bounds.x - (win.w - bounds.w) * 0.5f;
        else win.x = std::clamp(win.x, bounds.x, bounds.right() - win.w);
        if (win.h >= bounds.h) win.y = bounds.y - (win.h - bounds.h) * 0.5f;
        else win.y = std::clamp(win.y, bounds.y, bounds.bottom() - win.h);
    }
    return win;
}

}  // namespace

struct ReadLevel {
    LevelInfo info;
    std::shared_ptr<LevelData> level;
};

namespace {

std::shared_ptr<const ReadLevel> readLevel(const ContentDatabase& content, const std::string& id) {
    auto out = std::make_shared<ReadLevel>();
    LevelInfo& info = out->info;
    auto level = content.loadLevel(id);
    if (!level) return out;
    info.ok = true;
    info.name = level->name;
    info.extreme = level->extreme;
    info.bounds = level->bounds;
    LevelProgressTrack t = buildProgressTrack(*level, content);
    if (t.valid()) {
        // The height of the route, sampled along its length (higher ground: taller).
        const int n = 120;
        std::vector<float> ys;
        for (int k = 0; k < n; ++k) ys.push_back(pointAt(t, static_cast<float>(k) / static_cast<float>(n - 1)).y);
        auto [lo, hi] = std::minmax_element(ys.begin(), ys.end());
        const float span = *hi - *lo;
        for (float y : ys) info.relief.push_back(span > 1.0f ? 0.15f + 0.85f * (*hi - y) / span : 0.5f);
    }
    for (const LevelObject& o : level->objects) {
        const float at = t.valid() ? t.fractionAt(o.rect.bottomCenter()) : 0.0f;
        if (o.type == "checkpoint") {
            info.checkpoints.push_back({at, o.id});
            info.labels[o.id] = o.propStr("label");
        } else if (o.type == "enemy") {
            const EnemyDefinition* d = content.enemyFor(o.propStr("enemy"), *level);
            if (!d) continue;
            info.things.push_back({d->isBoss ? LevelInfo::Thing::Kind::Boss : LevelInfo::Thing::Kind::Enemy, d->id, at});
        } else if (o.type == "power_unlock") {
            const std::string p = o.propStr("power");
            if (p.empty()) continue;
            info.things.push_back({o.prop<bool>("station", false) ? LevelInfo::Thing::Kind::Station : LevelInfo::Thing::Kind::Tablet, p, at});
        } else if (o.type == "crumble_platform") {
            info.things.push_back({LevelInfo::Thing::Kind::Crumble, "crumble", at});
        }
    }
    std::sort(info.checkpoints.begin(), info.checkpoints.end());
    std::stable_sort(info.things.begin(), info.things.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
    info.track = std::move(t);
    out->level = std::make_shared<LevelData>(std::move(*level));
    return out;
}

}  // namespace

// ------------------------------------------------------------------ LevelInfo

std::vector<LevelInfo::Group> LevelInfo::between(float a, float b) const {
    std::vector<Group> out;
    for (const Thing& t : things) {
        if (t.at < a - 1e-4f || t.at > b + 1e-4f) continue;
        if (b < 0.9999f && t.at >= b - 1e-4f && t.at > a) continue;  // the next segment's
        auto it = std::find_if(out.begin(), out.end(), [&](const Group& g) { return g.kind == t.kind && g.id == t.id; });
        if (it == out.end()) out.push_back({t.kind, t.id, 1});
        else ++it->count;
    }
    return out;
}

std::string LevelInfo::checkpointName(const std::string& id, bool end) const {
    if (id.empty()) return end ? "Arrivée" : "Départ";
    auto it = labels.find(id);
    if (it != labels.end() && !it->second.empty()) return it->second;
    for (size_t i = 0; i < checkpoints.size(); ++i)
        if (checkpoints[i].second == id) return "Point de passage " + std::to_string(i + 1);
    return "Point de passage";
}

float LevelInfo::at(const std::string& id, bool end) const {
    if (id.empty()) return end ? 1.0f : 0.0f;
    for (const auto& [f, cp] : checkpoints)
        if (cp == id) return f;
    return end ? 1.0f : 0.0f;
}

Rect stretchWindow(const LevelInfo& info, float a, float b, float w, float h) {
    const float aspect = std::max(0.2f, w / std::max(1.0f, h));
    Rect box = routeBox(info.track, std::min(a, b), std::max(a, b));
    if (box.w <= 0.0f && box.h <= 0.0f && !info.track.valid()) box = Rect::fromCenter(info.bounds.center(), {640.0f, 360.0f});
    // Some room around the route (more above it: the jumps), a screen's height at least.
    const float mx = std::max(64.0f, box.w * 0.05f), my = std::max(72.0f, box.h * 0.2f);
    Rect win{box.x - mx, box.y - my * 1.6f, box.w + 2 * mx, box.h + my * 2.6f};
    if (win.h < 240.0f) {
        win.y -= (240.0f - win.h) * 0.6f;
        win.h = 240.0f;
    }
    return fitWindow(win, aspect, info.bounds);
}

// ------------------------------------------------------------------ LevelArt

struct LevelArt::Loaded {
    std::string level, mount;
    std::unique_ptr<GameWorld> world;
    std::unique_ptr<GameRenderer> renderer;
    size_t terrainLeft = 1;
    std::shared_ptr<const ReadLevel> read;
};

LevelArt::LevelArt(GameServices& g) : g_(g) {}

LevelArt::~LevelArt() {
    pictures_.clear();
    loaded_.reset();
    reads_.clear();  // waits for a level still being read (a few milliseconds)
}

const ReadLevel* LevelArt::readyRead(const std::string& id) const {
    auto it = reads_.find(id);
    if (it == reads_.end() || it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return it->second.get().get();
}

const LevelInfo* LevelArt::info(const std::string& levelId) {
    if (levelId.empty()) return nullptr;
    if (!reads_.count(levelId))
        reads_.emplace(levelId, std::async(std::launch::async, [&content = g_.content, levelId] { return readLevel(content, levelId); }).share());
    const ReadLevel* r = readyRead(levelId);
    return r ? &r->info : nullptr;
}

bool LevelArt::failed(const std::string& levelId) const {
    auto it = failed_.find(levelId);
    if (it != failed_.end() && it->second) return true;
    const ReadLevel* r = readyRead(levelId);
    return r && !r->info.ok;
}

LevelArt::Picture* LevelArt::ask(const std::string& key, const std::string& level, bool strip, float a, float b, int w, int h) {
    for (auto it = pictures_.begin(); it != pictures_.end(); ++it)
        if (it->key == key) {
            it->used = frame_;
            pictures_.splice(pictures_.begin(), pictures_, it);  // the most recent first
            return &pictures_.front();
        }
    Picture p;
    p.key = key;
    p.level = level;
    p.strip = strip;
    p.a = a;
    p.b = b;
    p.w = std::clamp(w, 8, 2048);
    p.h = std::clamp(h, 8, 1024);
    p.used = frame_;
    pictures_.push_front(std::move(p));
    // At most 60 pictures kept: the least recently shown go.
    while (pictures_.size() > 60 && pictures_.back().used + 2 < frame_) pictures_.pop_back();
    return &pictures_.front();
}

const Texture* LevelArt::stretch(const std::string& levelId, float a, float b, int w, int h) {
    if (!info(levelId)) return nullptr;
    char k[96];
    std::snprintf(k, sizeof k, "|s|%.4f|%.4f|%d|%d", a, b, w, h);
    Picture* p = ask(levelId + k, levelId, false, a, b, w, h);
    return p->ready ? p->tex.get() : nullptr;
}

const Texture* LevelArt::sliceStrip(const std::string& levelId, int cellW, int h) {
    if (!info(levelId)) return nullptr;
    char k[64];
    std::snprintf(k, sizeof k, "|strip|%d|%d", cellW, h);
    Picture* p = ask(levelId + k, levelId, true, 0.0f, 1.0f, cellW * playstats::kSlices, h);
    return p->ready ? p->tex.get() : nullptr;
}

void LevelArt::update() {
    ++frame_;
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsedMs = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    // The pictures still to draw, of the level loaded first.
    Picture* next = nullptr;
    for (auto& p : pictures_) {
        if (p.ready || failed(p.level) || p.used + 2 < frame_) continue;  // a picture no longer shown waits
        if (!readyRead(p.level)) continue;
        if (loaded_ && p.level == loaded_->level) {
            next = &p;
            break;
        }
        if (!next) next = &p;
    }
    if (!next) {
        // Nothing to draw: the level loaded goes after a while (its terrain's textures with it).
        if (loaded_ && frame_ > idleSince_ + 90) loaded_.reset();
        return;
    }
    idleSince_ = frame_;
    if (!loaded_ || loaded_->level != next->level) {
        // One level loaded per frame at most (main thread: its textures).
        loaded_.reset();
        const ReadLevel* r = readyRead(next->level);
        auto keep = reads_.at(next->level).get();
        if (!r || !r->level || !r->info.ok) {
            failed_[next->level] = true;
            return;
        }
        auto l = std::make_unique<Loaded>();
        l->level = next->level;
        l->read = keep;
        ResourceManager& res = g_.app.resources();
        if (const LevelEntry* e = g_.content.level(next->level)) l->mount = Paths::toUtf8(e->assetRoot);
        if (!l->mount.empty()) res.pushMount(Paths::fromUtf8(l->mount));
        WorldServices ws{res, g_.app.audio(), g_.content, nullptr, 0.0f, 1.0f};
        l->world = std::make_unique<GameWorld>(ws, WorldMode::Editor);
        l->world->load(*r->level);
        l->world->player().setHidden(true);
        l->renderer = std::make_unique<GameRenderer>();
        l->renderer->buildProgressive(res, *l->world);
        if (!l->mount.empty()) res.popMount();
        loaded_ = std::move(l);
        Log::info("Statistiques : rendu de {} préparé en {:.0f} ms", next->level, elapsedMs());
        return;
    }
    ResourceManager& res = g_.app.resources();
    if (!loaded_->mount.empty()) res.pushMount(Paths::fromUtf8(loaded_->mount));
    if (loaded_->terrainLeft > 0) loaded_->terrainLeft = loaded_->renderer->bakeTerrain(g_.app.renderer(), 5.0);
    // The pictures of this level, a few milliseconds' worth.
    if (loaded_->terrainLeft == 0)
        for (auto& p : pictures_) {
            if (elapsedMs() > 6.0) break;
            if (p.ready || p.level != loaded_->level || p.used + 2 < frame_) continue;
            drawSome(p);
        }
    if (!loaded_->mount.empty()) res.popMount();
}

void LevelArt::drawView(Picture& p, const Rect& window, const Rect& dst) {
    (void)p;
    Renderer& r = g_.app.renderer();
    const float k = std::min(dst.w / std::max(1.0f, window.w), dst.h / std::max(1.0f, window.h));
    WorldView v;
    v.scale = k;
    v.screenW = static_cast<int>(dst.w);
    v.screenH = static_cast<int>(dst.h);
    v.topLeft = {window.x - dst.x / k, window.y - dst.y / k};
    v.origin = dst.pos();
    RenderOptions opt;
    opt.lighting = false;
    opt.mapView = true;
    r.pushClip(dst);
    loaded_->renderer->render(r, g_.app.resources(), *loaded_->world, v, 1.0f, opt);
    r.setView(WorldView{});
    r.popClip();
}

void LevelArt::drawSome(Picture& p) {
    Renderer& r = g_.app.renderer();
    const LevelInfo& info = loaded_->read->info;
    if (!p.tex) {
        p.tex = r.createTarget(p.w, p.h, true);
        if (!p.tex) {
            failed_[p.level] = true;
            return;
        }
        r.setTarget(p.tex);
        SDL_SetRenderDrawColor(r.sdl(), 8, 11, 24, 255);
        SDL_RenderClear(r.sdl());
        r.setTarget(nullptr);
    }
    r.setTarget(p.tex);
    if (!p.strip) {
        drawView(p, stretchWindow(info, p.a, p.b, static_cast<float>(p.w), static_cast<float>(p.h)), {0.0f, 0.0f, static_cast<float>(p.w), static_cast<float>(p.h)});
        p.ready = true;
    } else {
        // The slices side by side: each its stretch of route, at one scale (the usual width of a
        // slice), its height centred on the route there.
        const int n = playstats::kSlices;
        const float cw = static_cast<float>(p.w) / static_cast<float>(n), ch = static_cast<float>(p.h);
        std::vector<float> widths;
        for (int i = 0; i < n; ++i) widths.push_back(routeBox(info.track, static_cast<float>(i) / n, static_cast<float>(i + 1) / n).w);
        std::vector<float> sorted = widths;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const float ww = std::max(96.0f, sorted[static_cast<size_t>(n / 2)]);
        const float wh = ww * ch / cw;
        for (int k = 0; k < 10 && p.done < n; ++k, ++p.done) {
            const int i = p.done;
            Rect box = routeBox(info.track, static_cast<float>(i) / n, static_cast<float>(i + 1) / n);
            Rect win = fitWindow(Rect::fromCenter(box.center(), {ww, wh}), cw / ch, info.bounds);
            drawView(p, win, {std::floor(cw * i), 0.0f, std::floor(cw * (i + 1)) - std::floor(cw * i), ch});
        }
        p.ready = p.done >= n;
    }
    r.setTarget(nullptr);
}

// ------------------------------------------------------------------ sprites

namespace {

Rect fitInto(Rect src, const Rect& box, bool stand) {
    float k = std::min(box.w / std::max(1.0f, src.w), box.h / std::max(1.0f, src.h));
    if (k >= 1.0f) k = std::floor(k);
    const Vec2 size{src.w * k, src.h * k};
    if (stand) return {std::floor(box.center().x - size.x * 0.5f), box.bottom() - size.y, size.x, size.y};
    return {std::floor(box.center().x - size.x * 0.5f), std::floor(box.center().y - size.y * 0.5f), size.x, size.y};
}

const AnimationClip* clipOf(const AnimationSet& set, const std::string& clip) {
    const AnimationClip* c = set.clip(clip);
    if (!c) c = set.clip("idle");
    if (!c && !set.clips.empty()) c = &set.clips.begin()->second;
    return c && c->frameCount() > 0 ? c : nullptr;
}

Rect drawFrame(GameServices& g, const AnimationSet& set, const AnimationClip& c, int frame, const Rect& box, Color tint, bool flipX, bool stand) {
    Rect src = set.frameRect(c, std::clamp(frame, 0, c.frameCount() - 1));
    const Rect vis = g.app.resources().visibleBox(set, src);
    if (vis.w > 0.0f && vis.h > 0.0f) src = {src.x + vis.x, src.y + vis.y, vis.w, vis.h};
    const Rect dst = fitInto(src, box, stand);
    g.app.renderer().drawTexture(*set.texture, &src, dst, tint, BlendMode::Alpha, flipX);
    return dst;
}

}  // namespace

Rect drawSprite(GameServices& g, const std::string& anim, const std::string& clip, int frame, const Rect& box, Color tint, bool flipX, bool stand) {
    AnimationSetPtr set = g.app.resources().animation(anim);
    if (!set || !set->texture) return {};
    const AnimationClip* c = clipOf(*set, clip);
    return c ? drawFrame(g, *set, *c, frame, box, tint, flipX, stand) : Rect{};
}

Rect drawSpriteAt(GameServices& g, const std::string& anim, const std::string& clip, float t, const Rect& box, Color tint, bool flipX, bool stand) {
    AnimationSetPtr set = g.app.resources().animation(anim);
    if (!set || !set->texture) return {};
    const AnimationClip* c = clipOf(*set, clip);
    if (!c) return {};
    const float total = std::max(0.01f, c->totalDuration());
    float local = std::fmod(std::max(0.0f, t), total), acc = 0.0f;
    int frame = 0;
    for (int i = 0; i < c->frameCount(); ++i) {
        acc += c->durations[static_cast<size_t>(i)];
        if (local < acc) {
            frame = i;
            break;
        }
    }
    return drawFrame(g, *set, *c, frame, box, tint, flipX, stand);
}

Rect drawEnemy(GameServices& g, const std::string& enemyId, const Rect& box, float t, const std::string& clip, bool stand, Color tintMul) {
    const EnemyDefinition* d = g.content.enemy(enemyId);
    if (!d) return {};
    const Color tint{static_cast<uint8_t>(d->tint.r * tintMul.r / 255), static_cast<uint8_t>(d->tint.g * tintMul.g / 255),
                     static_cast<uint8_t>(d->tint.b * tintMul.b / 255), tintMul.a};
    return drawSpriteAt(g, d->animation, clip, t, box, tint, false, stand);
}

void drawCover(GameServices& g, const Texture& tex, const Rect& box, Color tint, float focusX) {
    if (box.w <= 0.0f || box.h <= 0.0f || tex.width() <= 0 || tex.height() <= 0) return;
    const float tw = static_cast<float>(tex.width()), th = static_cast<float>(tex.height());
    const float k = std::max(box.w / tw, box.h / th);
    // The part of the picture that covers the box, cut from the source (no clip needed).
    const float sw = box.w / k, sh = box.h / k;
    Rect src{(tw - sw) * std::clamp(focusX, 0.0f, 1.0f), (th - sh) * 0.5f, sw, sh};
    g.app.renderer().drawTexture(tex, &src, box, tint);
}

bool drawPlanetBanner(GameServices& g, const std::string& planetId, const Rect& box, Color tint) {
    const PlanetData* p = g.content.planet(planetId);
    if (!p || p->banner.empty() || !g.app.resources().exists(p->banner)) return false;
    TexturePtr t = g.app.resources().texture(p->banner, false);
    if (!t) return false;
    drawCover(g, *t, box, tint);
    return true;
}

std::pair<std::string, int> levelPlace(const GameServices& g, const std::string& levelId) {
    const std::string planet = g.content.campaignPlanetOf(levelId);
    if (planet.empty()) return {"", 0};
    int n = 0;
    for (const LevelEntry* l : g.content.levelsForPlanet(planet)) {
        ++n;
        if (l->id == levelId) return {planet, n};
    }
    return {planet, 0};
}

BossPlace bossPlace(const GameServices& g, const CreatureAtlas* atlas, const std::string& bossId) {
    BossPlace out;
    if (!atlas) return out;
    for (const CreaturePlace& p : atlas->fightsOf(g.content, bossId)) {
        if (p.planetId.empty()) continue;
        out.planetId = p.planetId;
        out.levelId = p.levelId;
        out.levelNumber = p.levelNumber;
        break;
    }
    return out;
}

}  // namespace cosmic::statsart
