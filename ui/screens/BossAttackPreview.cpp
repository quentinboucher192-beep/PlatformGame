#include "ui/screens/BossAttackPreview.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/content/CreaturePlaces.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

/// Each frame, at most this long spent preparing the example (the screen stays fluid).
constexpr double kBudgetMs = 5.0;

}  // namespace

BossAttackPreview::BossAttackPreview(GameServices& g) : g_(g) {}

BossAttackPreview::~BossAttackPreview() {
    if (reading_.valid()) reading_.wait();
    demo_.reset();
}

void BossAttackPreview::request(const EnemyDefinition* def, const std::string& attackId, const std::shared_ptr<const CreatureAtlas>& atlas) {
    if (atlas) atlas_ = atlas;
    if (!def || attackId.empty()) {
        stop();
        return;
    }
    if (def->id == bossId_ && attackId == attackId_ && status_ != Status::Idle) return;
    bossId_ = def->id;
    attackId_ = attackId;
    paused_ = false;
    begin();
}

void BossAttackPreview::stop() {
    status_ = Status::Idle;
    bossId_.clear();
    attackId_.clear();
    reason_.clear();
    demo_.reset();
    renderer_.reset();
    camValid_ = false;
}

void BossAttackPreview::unavailable(const std::string& why) {
    status_ = Status::Unavailable;
    reason_ = why;
    demo_.reset();
    renderer_.reset();
}

void BossAttackPreview::begin() {
    demo_.reset();
    renderer_.reset();
    camValid_ = false;
    reason_.clear();
    terrainLeft_ = 0;
    const EnemyDefinition* def = g_.content.enemy(bossId_);
    const AttackDef* a = def ? def->attack(attackId_) : nullptr;
    if (!a) {
        unavailable("Cette attaque est introuvable dans la définition du boss.");
        return;
    }
    const AttackPreviewPlan plan = attackPreviewPlan(*def, *a);
    if (!plan.showable) {
        unavailable(plan.reason);
        return;
    }
    // The same boss as the example before: its arena is known (and its level read).
    if (!levelId_.empty() && cachedLevelId_ == levelId_ && cachedLevel_ && atlas_) {
        if (auto place = bossTrainingPlace(*atlas_, g_.content, bossId_); place && place->levelId == levelId_) {
            bossObject_ = place->objectId;
            status_ = Status::Preparing;
            return;
        }
    }
    status_ = Status::Searching;
}

void BossAttackPreview::update(float dt) {
    spinner_ += dt;
    ++unseen_;  // draw() sets it back
    switch (status_) {
        case Status::Idle:
        case Status::Unavailable: return;
        case Status::Searching: {
            if (!atlas_) return;  // the levels are still being read (worker thread)
            levelId_.clear();
            bossObject_.clear();
            const auto place = bossTrainingPlace(*atlas_, g_.content, bossId_);
            if (!place) {  // in no level of the game: a neutral arena
                status_ = Status::Preparing;
                return;
            }
            levelId_ = place->levelId;
            bossObject_ = place->objectId;
            if (cachedLevelId_ == levelId_ && cachedLevel_) {
                status_ = Status::Preparing;
                return;
            }
            const LevelEntry* e = g_.content.level(levelId_);
            if (!e) {
                unavailable("Son niveau est introuvable.");
                return;
            }
            // Its level read off the main thread (a file and its JSON: nothing else is touched).
            const fs::path path = e->path;
            if (reading_.valid()) reading_.wait();
            reading_ = std::async(std::launch::async, [path]() -> std::optional<LevelData> {
                auto j = FileUtil::loadJson(path);
                if (!j) return std::nullopt;
                return LevelData::fromJson(*j);
            });
            status_ = Status::Loading;
            return;
        }
        case Status::Loading:
            if (!reading_.valid() || reading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            cachedLevel_ = reading_.get();
            cachedLevelId_ = cachedLevel_ ? levelId_ : std::string{};
            if (!cachedLevel_) {
                unavailable("Son niveau ne peut pas être lu.");
                return;
            }
            status_ = Status::Preparing;
            return;
        case Status::Preparing: prepare(); return;
        case Status::Running: {
            if (!demo_ || demo_->over()) {
                // The scene ended (a time limit of the level…): set up again.
                demo_.reset();
                renderer_.reset();
                status_ = Status::Preparing;
                return;
            }
            GameWorld* w = demo_->world();
            if (unseen_ > 2) return;  // its frame is out of sight: nothing to animate
            if (!paused_) w->update(dt);
            // The part of the world shown follows the boss and Nova smoothly (display only).
            const Enemy* b = demo_->boss();
            const Rect want = demoWindow(demo_->arena(), b ? b->box : demo_->arena(), w->player().box, aspect_);
            if (!camValid_) {
                cam_ = want;
                camValid_ = true;
            } else {
                const float k = 1.0f - std::exp(-std::max(0.0f, dt) * 3.5f);
                cam_ = {cam_.x + (want.x - cam_.x) * k, cam_.y + (want.y - cam_.y) * k, cam_.w + (want.w - cam_.w) * k, cam_.h + (want.h - cam_.h) * k};
            }
            return;
        }
    }
}

void BossAttackPreview::prepare() {
    const Clock::time_point t0 = Clock::now();
    ResourceManager& res = g_.app.resources();
    if (!content_) {
        // Once per visit: the example's boss lives in this copy (the game's content is never touched).
        content_ = std::make_shared<ContentDatabase>(g_.content);
        Log::info("Fiche des boss : copie du contenu pour les exemples en {:.1f} ms", msSince(t0));
        return;
    }
    if (!demo_) {
        const EnemyDefinition* def = g_.content.enemy(bossId_);
        if (!def) {
            unavailable("Ce boss est introuvable.");
            return;
        }
        std::optional<LevelData> level;
        if (!levelId_.empty() && cachedLevel_ && cachedLevelId_ == levelId_) level = *cachedLevel_;
        ownArena_ = level.has_value();
        demo_ = std::make_unique<BossAttackDemo>();
        std::string error;
        if (!demo_->setup(content_, res, *def, attackId_, std::move(level), bossObject_, &error)) {
            unavailable(error.empty() ? std::string("L'exemple ne peut pas être préparé.") : error);
            return;
        }
        Log::info("Fiche des boss : niveau de l'exemple {} / {} chargé en {:.1f} ms", bossId_, attackId_, msSince(t0));
        return;
    }
    if (!demo_->arranged()) {
        // Its own frame: the boss at its phase, Nova at her place, then the drawing prepared.
        if (!demo_->arrange()) {
            unavailable(demo_->failure().empty() ? std::string("L'exemple ne peut pas être préparé.") : demo_->failure());
            return;
        }
        renderer_ = std::make_unique<GameRenderer>();
        renderer_->buildProgressive(res, *demo_->world());
        terrainLeft_ = renderer_->terrain().pendingChunks();
        Log::info("Fiche des boss : exemple {} / {} mis en place en {:.1f} ms", bossId_, attackId_, msSince(t0));
        return;
    }
    // The terrain baked (the arena first) and the scene brought to the attack, a few
    // milliseconds per frame.
    if (terrainLeft_ > 0) {
        const Rect arena = demo_->arena();
        terrainLeft_ = renderer_->bakeTerrain(g_.app.renderer(), kBudgetMs * 0.5, &arena);
    }
    while (!demo_->ready() && !demo_->failed() && msSince(t0) < kBudgetMs) demo_->warmUp(12);
    if (demo_->failed()) {
        unavailable(demo_->failure());
        return;
    }
    if (demo_->ready() && terrainLeft_ == 0) {
        status_ = Status::Running;
        camValid_ = false;
        update(0.0f);  // the first framing
    }
}

void BossAttackPreview::fixedUpdate() {
    // Played only while its frame is on screen (scrolled out of the list: it waits).
    if (status_ != Status::Running || paused_ || !demo_ || unseen_ > 2) return;
    demo_->step();
}

std::string BossAttackPreview::caption() const {
    switch (status_) {
        case Status::Idle: return {};
        case Status::Searching: return "Recherche de son arène…";
        case Status::Loading: return "Chargement de son arène…";
        case Status::Preparing: return "Préparation de l'exemple…";
        case Status::Unavailable: return "Exemple indisponible";
        case Status::Running: break;
    }
    std::string where = demo_ && demo_->lastStand() ? std::string("Dernier souffle") : "Phase " + std::to_string((demo_ ? demo_->phase() : 0) + 1);
    if (!ownArena_) where += "  ·  arène neutre";
    if (paused_) where += "  ·  en pause";
    return where;
}

void BossAttackPreview::draw(UIContext& ui, const Rect& frame, float alpha) {
    Renderer& r = ui.r();
    const UITheme& th = ui.theme();
    r.fillRoundedRect(frame, ui.s(8), Color{5, 8, 18, 255});
    if (frame.w < 4.0f || frame.h < 4.0f) return;
    aspect_ = frame.w / frame.h;
    unseen_ = 0;  // drawn: on screen (the caller draws it only then)
    if (status_ == Status::Running && demo_ && demo_->world() && renderer_ && camValid_ && cam_.w > 1.0f) {
        WorldView v;
        v.scale = frame.w / cam_.w;
        v.screenW = static_cast<int>(frame.w);
        v.screenH = static_cast<int>(frame.h);
        v.topLeft = {cam_.x - frame.x / v.scale, cam_.y - frame.y / v.scale};
        v.origin = frame.pos();
        RenderOptions opt;
        opt.lighting = true;
        r.pushClip(frame);
        renderer_->render(r, g_.app.resources(), *demo_->world(), v, paused_ ? 1.0f : alpha, opt);
        r.setView(WorldView{});
        // Nova is small in a whole arena: a little mark over her head (display only).
        const Rect nb = demo_->world()->player().box;
        const Vec2 tip = v.toScreen({nb.center().x, nb.y - 5.0f});
        const float k = std::max(ui.s(5), 3.0f);
        r.fillTriangle({tip.x - k - 1.5f, tip.y - k * 1.6f - 1.5f}, {tip.x + k + 1.5f, tip.y - k * 1.6f - 1.5f}, {tip.x, tip.y + 1.5f}, Color{6, 8, 16, 220});
        r.fillTriangle({tip.x - k, tip.y - k * 1.6f}, {tip.x + k, tip.y - k * 1.6f}, {tip.x, tip.y}, th.accent);
        r.popClip();
        return;
    }
    if (status_ == Status::Idle) return;
    // What is happening, in the middle of the frame (measured: wrapped to its width, smaller
    // when it would not fit its height).
    const bool waiting = status_ != Status::Unavailable;
    const std::string text = waiting ? caption() : reason_;
    const float pad = ui.s(16);
    const float spin = waiting ? ui.s(34) : 0.0f;
    const float room = frame.w - 2.0f * pad;
    if (room < ui.s(40)) return;
    float size = 16.0f;
    Vec2 sz = ui.text().measureWrapped(text, FontFace::Body, ui.s(size), room);
    while (size > 11.5f / ui.scale() + 0.01f && sz.y > frame.h - 2.0f * pad - spin) {
        size = std::max(11.5f / ui.scale(), size - 1.0f);
        sz = ui.text().measureWrapped(text, FontFace::Body, ui.s(size), room);
    }
    const float top = frame.center().y - (sz.y + spin) * 0.5f;
    r.pushClip(frame);
    if (waiting) {
        const Vec2 c{frame.center().x, top + ui.s(12)};
        r.drawArc(c, ui.s(10), std::max(1.5f, ui.s(3)), spinner_ * 5.0f, spinner_ * 5.0f + 4.2f, th.accent);
    }
    // Centred line by line: each line measured on its own.
    ui.labelWrapped(text, {frame.center().x - sz.x * 0.5f, top + spin}, room, size, waiting ? th.textDim : th.warm);
    r.popClip();
}

}  // namespace cosmic
