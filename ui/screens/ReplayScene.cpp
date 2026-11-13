// VIEW REPLAY: plays a record again.
//
// Exact replays (format 2) are re-simulated: the level runs with the recorded seed and the
// inputs of every step, so the run looks exactly like it did for the player (animations,
// enemies, attacks, blocks breaking, HUD, camera). The state digests stored every 0.5 s
// are checked on the way; if the local simulation differs (other engine version), the
// viewer says so and switches to the recorded trajectory. Old replays (format 1) only
// contain that trajectory: a ghost runs over the level.

#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

const char* clipFor(const ReplayFrame& f, Vec2 vel) {
    switch (static_cast<PlayerState>(f.state)) {
        case PlayerState::Dead: return "death";
        case PlayerState::Respawn: return "respawn";
        case PlayerState::Victory: return "victory";
        case PlayerState::Hurt: return "hurt";
        case PlayerState::Dash: return "dash";
        case PlayerState::Slide: return "slide";
        case PlayerState::Climb: return "climb";
        case PlayerState::WallSlide: return "wall_slide";
        default: break;
    }
    if (f.flags & ReplayFrame::Grounded) {
        float s = std::abs(vel.x);
        return s > 93.0f ? "run" : s > 8.0f ? "walk" : "idle";
    }
    return vel.y < -30.0f ? "jump" : "fall";
}

class ReplayScene : public Scene {
public:
    /// Online record (downloaded) or a replay already in memory (file, local run).
    ReplayScene(GameServices& g, std::string recordId) : g_(g), recordId_(std::move(recordId)) {}
    ReplayScene(GameServices& g, ReplayData replay, std::string title) : g_(g), replay_(std::move(replay)), playerName_(std::move(title)) {
        local_ = true;
    }

    void onEnter() override {
        if (local_) {
            timeMs_ = replay_.durationMs;
            loadLevel(replay_.levelId);
            return;
        }
        status_ = "Chargement du record…";
        g_.online.get("/api/v1/records/" + recordId_, OnlineService::guard(life_, [this](const ApiResult& r) {
            if (!r.ok) return fail("Record introuvable : " + r.message);
            record_ = r.data;
            const Json& rec = record_.value("record", Json::object());
            const Json& pl = record_.value("player", Json::object());
            playerName_ = jstr(pl, "username");
            timeMs_ = jget<int64_t>(rec, "time_ms", 0);
            RunCategory c;
            categoryText_ = categoryFromId(jstr(rec, "category"), c) ? categoryLabel(c) : "";
            rank_ = record_.contains("rank") && record_["rank"].is_number() ? record_["rank"].get<int>() : 0;
            downloadReplay(jstr(rec, "level_id"));
        }), false);
    }

    void onExit() override {
        if (mounted_) g_.app.resources().popMount();
    }

    void update(float dt) override {
        if (!world_) return;
        if (exact()) updateExact(dt);
        else updateTrace(dt);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        Renderer& r = g_.app.renderer();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        if (!world_) {
            r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{6, 9, 20, 255});
            ui.label(status_, {ui.screenW() * 0.5f, ui.screenH() * 0.45f}, 24, failed_ ? ui.theme().danger : ui.theme().text, Align::Center,
                     FontFace::Body, VAlign::Middle);
            UIContext::ButtonStyle st;
            if (ui.button("replay/back", {ui.screenW() * 0.5f - ui.s(120), ui.screenH() * 0.55f, ui.s(240), ui.s(48)}, "Retour", st) || ui.backPressed())
                g_.app.popScene();
            ui.end();
            return;
        }
        RenderOptions opt;
        opt.lighting = g_.settings.graphics.dynamicLighting;
        if (exact()) {
            float alpha = std::clamp(acc_ / kSimDt, 0.0f, 1.0f);
            WorldView view = world_->camera().view(r.width(), r.height(), alpha);
            renderer_.render(r, g_.app.resources(), *world_, view, alpha, opt);
            hud_.render(ui, *world_, view, nullptr, g_.settings.gameplay);
        } else {
            WorldView view = makeTraceView(r);
            renderer_.render(r, g_.app.resources(), *world_, view, 1.0f, opt);
            r.setView(view);
            drawTrailAndEvents(r);
            drawGhost(r);
        }
        drawControls();
        ui.end();
    }

private:
    // ------------------------------------------------------------- loading
    bool exact() const { return sim_ != nullptr && !traceMode_; }
    float traceDuration() const { return replay_.durationMs / 1000.0f; }

    void fail(const std::string& m) {
        status_ = m;
        failed_ = true;
    }

    void downloadReplay(const std::string& levelId) {
        status_ = "Téléchargement du replay…";
        g_.online.getBinary("/api/v1/records/" + recordId_ + "/replay", [this, levelId, alive = std::weak_ptr<int>(life_)](const HttpResponse& resp) {
            if (!alive.lock()) return;
            if (resp.networkFailure()) return fail(std::string("Réseau : ") + netErrorLabel(resp.error));
            if (resp.status != 200) return fail(parseApiResponse(resp).message);
            std::vector<uint8_t> bytes(resp.body.begin(), resp.body.end());
            std::string err;
            auto rep = ReplayData::decode(bytes, &err);
            if (!rep) return fail("Replay illisible : " + err);
            replay_ = std::move(*rep);
            loadLevel(levelId.empty() ? replay_.levelId : levelId);
        });
    }

    void loadLevel(const std::string& levelId) {
        // Any installed copy of the level works, as long as it is exactly the same revision
        // (level, enemies, powers and simulation version).
        const LevelEntry* entry = g_.content.level(levelId);
        if (!entry) return fail("Ce niveau n'est pas installé dans votre jeu.");
        std::optional<LevelFingerprint> fp;
        if (auto text = FileUtil::readText(entry->path)) {
            Json j = Json::parse(*text, nullptr, false);
            if (!j.is_discarded()) fp = fingerprintRankedLevel(g_.content, j);
        }
        if (!fp || fp->hash != replay_.levelHash)
            return fail("Ce replay a été enregistré sur une autre version du niveau ou du jeu.");
        std::string err;
        auto level = g_.content.loadLevel(levelId, &err);
        if (!level) return fail("Chargement du niveau impossible : " + err);
        level_ = *level;
        if (!entry->assetRoot.empty()) {
            g_.app.resources().pushMount(entry->assetRoot);
            mounted_ = true;
        }
        traceMode_ = !replay_.exact();
        buildWorld();
        playing_ = true;
    }

    /// (Re)creates the level from its start. Exact mode: a new simulation with the replay's
    /// seed; trace mode: the level lives on its own and the recorded player is a ghost.
    void buildWorld() {
        ResourceManager& res = g_.app.resources();
        WorldServices ws{res, g_.app.audio(), g_.content, nullptr, g_.settings.graphics.screenShake, 0.7f};
        sim_.reset();
        world_ = std::make_unique<GameWorld>(ws, WorldMode::Play);
        world_->load(level_);
        world_->seedGameplay(levelSeed(level_.id));
        if (!traceMode_) {
            sim_ = std::make_unique<ReplaySimulator>(replay_, *world_);
        } else {
            if (world_->cinematic().active() && world_->cinematic().skippable()) world_->cinematic().skip(*world_);
            world_->player().setHidden(true);
            ghost_.setSet(world_->player().anim.set());
            snapCam_ = true;
        }
        renderer_.build(g_.app.renderer(), res, *world_);
        hud_ = Hud{};
        acc_ = 0.0f;
    }

    // ---------------------------------------------------------- exact mode
    void updateExact(float dt) {
        float speed = speeds_[static_cast<size_t>(speedIdx_)];
        if (seekTarget_ >= 0) {
            // Fast-forward without drawing intermediate frames (a few thousand steps / frame).
            int budget = 4000;
            while (static_cast<int64_t>(sim_->tick()) < seekTarget_ && budget-- > 0 && sim_->step()) {
            }
            if (static_cast<int64_t>(sim_->tick()) >= seekTarget_ || sim_->finished() || sim_->desynced()) seekTarget_ = -1;
            world_->update(0.0f);
        } else if (playing_) {
            acc_ += dt * speed;
            int guard = 0;
            while (acc_ >= kSimDt && guard++ < 64) {
                acc_ -= kSimDt;
                if (!sim_->step()) {
                    acc_ = 0.0f;
                    playing_ = false;
                    break;
                }
            }
        }
        world_->update(playing_ ? dt * speed : 0.0f);
        hud_.update(*world_, playing_ ? dt * speed : 0.0f);
        if (sim_->desynced() && !desyncNoticed_) {
            desyncNoticed_ = true;
            playing_ = false;
        }
    }

    void seekTick(int64_t target) {
        target = std::clamp<int64_t>(target, 0, sim_->totalTicks());
        if (target < static_cast<int64_t>(sim_->tick())) buildWorld();  // the past is simulated again
        seekTarget_ = target;
        desyncNoticed_ = false;
    }

    /// Switches to the recorded trajectory (old replays, or after a desynchronisation).
    void switchToTrace() {
        float at = world_ ? world_->timer().elapsed() : 0.0f;
        traceMode_ = true;
        buildWorld();
        t_ = std::clamp(at, 0.0f, traceDuration());
        playing_ = true;
    }

    // ---------------------------------------------------------- trace mode
    void updateTrace(float dt) {
        float speed = speeds_[static_cast<size_t>(speedIdx_)];
        if (playing_) {
            t_ += dt * speed;
            if (t_ >= traceDuration()) {
                t_ = traceDuration();
                playing_ = false;
            }
            // The level around keeps living (enemies, decor); the world's own player is hidden.
            acc_ += dt;
            while (acc_ >= kSimDt) {
                world_->fixedUpdate(PlayerInput{}, kSimDt);
                acc_ -= kSimDt;
            }
        }
        world_->update(dt);
        ReplayFrame f = replay_.sampleAt(t_);
        ReplayFrame prev = replay_.sampleAt(std::max(0.0f, t_ - 0.05f));
        Vec2 vel{(f.x - prev.x) / 0.05f, (f.y - prev.y) / 0.05f};
        ghost_.play(clipFor(f, vel));
        ghost_.update(dt * (playing_ ? speed : 0.0f));
        Vec2 target{f.x, f.y - 12.0f};
        camPos_ = snapCam_ ? target : camPos_ + (target - camPos_) * std::min(1.0f, dt * 6.0f);
        snapCam_ = false;
    }

    WorldView makeTraceView(const Renderer& r) const {
        WorldView v;
        v.screenW = r.width();
        v.screenH = r.height();
        v.scale = static_cast<float>(r.height()) / Camera::kViewHeight * std::max(0.5f, level_.cameraZoom);
        float vw = v.screenW / v.scale, vh = v.screenH / v.scale;
        Vec2 tl{camPos_.x - vw * 0.5f, camPos_.y - vh * 0.5f};
        const Rect& b = level_.bounds;
        tl.x = std::clamp(tl.x, b.x, std::max(b.x, b.right() - vw));
        tl.y = std::clamp(tl.y, b.y, std::max(b.y, b.bottom() - vh));
        v.topLeft = tl;
        return v;
    }

    void drawTrailAndEvents(Renderer& r) {
        if (replay_.frames.empty()) return;
        int hz = replay_.sampleHz;
        int cur = std::min(static_cast<int>(t_ * hz), static_cast<int>(replay_.frames.size()) - 1);
        for (int i = std::max(1, cur - hz * 2); i <= cur; ++i) {
            const ReplayFrame& a = replay_.frames[static_cast<size_t>(i - 1)];
            const ReplayFrame& b = replay_.frames[static_cast<size_t>(i)];
            if (std::hypot(b.x - a.x, b.y - a.y) > 120.0f) continue;
            float k = 1.0f - static_cast<float>(cur - i) / (hz * 2.0f);
            r.drawWorldLine({a.x, a.y - 11}, {b.x, b.y - 11}, Color{57, 240, 216, static_cast<uint8_t>(170 * k)}, 2.5f, BlendMode::Additive);
        }
        for (const auto& e : replay_.events) {
            if (e.frame > static_cast<uint32_t>(cur) || e.frame >= replay_.frames.size()) continue;
            const ReplayFrame& f = replay_.frames[e.frame];
            Vec2 p{f.x, f.y - 12.0f};
            switch (e.type) {
                case ReplayEventType::Death:
                    r.drawWorldLine(p + Vec2{-6, -6}, p + Vec2{6, 6}, Color{255, 77, 109, 230}, 2.5f);
                    r.drawWorldLine(p + Vec2{-6, 6}, p + Vec2{6, -6}, Color{255, 77, 109, 230}, 2.5f);
                    break;
                case ReplayEventType::Pickup: r.drawWorldGlow(p, 10.0f, Color{255, 206, 84, 150}); break;
                case ReplayEventType::Checkpoint: r.drawWorldCircle(p, 8.0f, Color{57, 240, 216, 220}, 2.0f); break;
                case ReplayEventType::Power:
                case ReplayEventType::Teleport: r.fillWorldCircle(p, 2.5f, Color{176, 76, 255, 200}); break;
                default: break;
            }
        }
    }

    void drawGhost(Renderer& r) {
        ReplayFrame f = replay_.sampleAt(t_);
        if (static_cast<PlayerState>(f.state) == PlayerState::Hidden || !ghost_.set() || !ghost_.set()->texture) return;
        bool flip = (f.flags & ReplayFrame::FacingLeft) != 0;
        Rect src = ghost_.sourceRect();
        Rect dst = ghost_.destRect({f.x, f.y}, flip);
        r.drawWorldGlow({f.x, f.y - 12.0f}, 26.0f, Color{57, 240, 216, 90});
        r.drawWorldTexture(*ghost_.set()->texture, &src, dst, Color{200, 255, 250, 235}, BlendMode::Alpha, flip);
    }

    // ------------------------------------------------------------ controls
    /// Inputs of the current step, like a speedrun input display.
    void drawInputs(const Rect& area) {
        const InputFrame* in = sim_ ? sim_->currentInput() : nullptr;
        if (!in) return;
        UIContext& ui = g_.ui;
        struct Key {
            Action a;
            const char* label;
        };
        static const Key keys[] = {{Action::MoveLeft, "G"},  {Action::MoveRight, "D"}, {Action::MoveUp, "H"},
                                   {Action::MoveDown, "B"},  {Action::Jump, "Saut"},   {Action::Attack, "Att."},
                                   {Action::Dash, "Dash"},   {Action::Wave, "Onde"},   {Action::Shield, "Boucl."},
                                   {Action::Impulse, "Imp."}, {Action::Teleport, "Télép."}};
        float x = area.x, h = area.h;
        for (const auto& k : keys) {
            bool on = (in->held >> static_cast<int>(k.a)) & 1u;
            float w = std::max(h, ui.text().measure(k.label, FontFace::Bold, ui.s(14)).x + ui.s(14));
            Rect b{x, area.y, w, h};
            ui.r().fillRoundedRect(b, ui.s(5), on ? ui.theme().accent.withAlpha(220) : Color{20, 30, 56, 200});
            ui.label(k.label, b.center(), 14, on ? Color{6, 12, 24, 255} : ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
            x += w + ui.s(5);
        }
    }

    void drawControls() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        InputManager& in = g_.app.input();
        float H = ui.screenH(), W = ui.screenW();
        Rect strip{0, H - ui.s(150), W, ui.s(150)};
        r.fillGradientV(strip, Color{4, 8, 18, 0}, Color{4, 8, 18, 235});
        // Title line.
        std::string title = "REPLAY - " + playerName_;
        ui.label(title, {ui.s(30), H - ui.s(118)}, 24, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        std::string sub = level_.name;
        if (!categoryText_.empty()) sub += " · " + categoryText_;
        if (timeMs_ > 0) sub += " · " + formatRunTime(timeMs_);
        if (rank_ > 0) sub += " · n°" + std::to_string(rank_);
        ui.label(sub, {ui.s(30), H - ui.s(90)}, 16, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
        // Mode / integrity.
        std::string mode;
        Color mc = ui.theme().textDim;
        if (exact()) {
            if (sim_->desynced()) {
                mode = "Désynchronisé à " + formatRunTime(static_cast<int64_t>(sim_->desyncTick()) * 1000 / kSimHz) +
                       " : ce replay vient d'une autre version du moteur";
                mc = ui.theme().danger;
            } else {
                mode = "Partie rejouée à l'identique · " + std::to_string(sim_->checksPassed()) + " contrôles vérifiés";
                mc = ui.theme().success;
            }
        } else {
            mode = replay_.exact() ? "Trajectoire enregistrée (simulation désynchronisée)" : "Trajectoire enregistrée (ancien format de replay)";
            mc = ui.theme().warm;
        }
        ui.label(mode, {W - ui.s(30), H - ui.s(118)}, 15, mc, Align::Right, FontFace::Bold, VAlign::Middle);

        // Timeline.
        Rect bar{ui.s(30), H - ui.s(62), W - ui.s(760), ui.s(12)};
        r.fillRoundedRect(bar, ui.s(6), Color{20, 30, 56, 230});
        float k = 0.0f;
        int64_t nowMs = 0, totalMs = 0;
        if (exact()) {
            k = sim_->totalTicks() ? static_cast<float>(sim_->tick()) / sim_->totalTicks() : 0.0f;
            nowMs = static_cast<int64_t>(sim_->tick()) * 1000 / kSimHz;
            totalMs = static_cast<int64_t>(sim_->totalTicks()) * 1000 / kSimHz;
        } else {
            k = traceDuration() > 0 ? t_ / traceDuration() : 0.0f;
            nowMs = static_cast<int64_t>(t_ * 1000.0f);
            totalMs = replay_.durationMs;
        }
        r.fillRoundedRect({bar.x, bar.y, bar.w * k, bar.h}, ui.s(6), ui.theme().accent.withAlphaF(0.85f));
        ui.label(formatRunTime(nowMs) + " / " + formatRunTime(totalMs), {bar.right() + ui.s(14), bar.center().y}, 17, ui.theme().accent,
                 Align::Left, FontFace::Bold, VAlign::Middle);
        if (seekTarget_ >= 0)
            ui.label("Recherche…", {bar.center().x, bar.y - ui.s(14)}, 15, ui.theme().text, Align::Center, FontFace::Body, VAlign::Middle);
        if (in.mousePressed(SDL_BUTTON_LEFT) && bar.expanded(ui.s(8)).contains(in.mousePos())) {
            float at = std::clamp((in.mousePos().x - bar.x) / bar.w, 0.0f, 1.0f);
            if (exact()) seekTick(static_cast<int64_t>(at * sim_->totalTicks()));
            else {
                t_ = at * traceDuration();
                snapCam_ = true;
            }
        }
        if (exact()) drawInputs({ui.s(30), H - ui.s(38), 0, ui.s(26)});

        ui.pushId("replay");
        UIContext::ButtonStyle st;
        st.small = true;
        float by = H - ui.s(80);
        float x = W - ui.s(500);
        if (exact() && sim_->desynced()) {
            if (ui.button("trace", {x - ui.s(250), by, ui.s(240), ui.s(40)}, "Voir la trajectoire", st)) switchToTrace();
        }
        if (ui.button("play", {x + ui.s(90), by, ui.s(120), ui.s(40)}, playing_ ? "Pause" : "Lecture", st)) {
            bool atEnd = exact() ? sim_->finished() : t_ >= traceDuration();
            if (!playing_ && atEnd) restartFromStart();
            playing_ = !playing_;
        }
        if (ui.button("speed", {x + ui.s(220), by, ui.s(80), ui.s(40)}, speedLabels_[static_cast<size_t>(speedIdx_)], st)) speedIdx_ = (speedIdx_ + 1) % 4;
        if (ui.button("restart", {x + ui.s(310), by, ui.s(80), ui.s(40)}, "Début", st)) {
            restartFromStart();
            playing_ = true;
        }
        if (ui.button("back", {x + ui.s(400), by, ui.s(90), ui.s(40)}, "Retour", st) || ui.backPressed()) {
            ui.popId();
            g_.app.popScene();
            return;
        }
        ui.popId();
        // Keyboard / gamepad: left / right seek by 5 s, confirm pauses.
        if (in.menuPressed(Action::MenuLeft)) seekRelative(-5.0f);
        if (in.menuPressed(Action::MenuRight)) seekRelative(5.0f);
    }

    void restartFromStart() {
        if (exact()) {
            buildWorld();
            desyncNoticed_ = false;
        } else {
            t_ = 0.0f;
            snapCam_ = true;
        }
    }

    void seekRelative(float seconds) {
        if (exact()) seekTick(static_cast<int64_t>(sim_->tick()) + static_cast<int64_t>(seconds * kSimHz));
        else {
            t_ = std::clamp(t_ + seconds, 0.0f, traceDuration());
            snapCam_ = true;
        }
    }

    GameServices& g_;
    std::string recordId_;
    bool local_ = false;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string status_;
    bool failed_ = false;
    Json record_;
    ReplayData replay_;
    LevelData level_;
    std::unique_ptr<GameWorld> world_;
    std::unique_ptr<ReplaySimulator> sim_;
    GameRenderer renderer_;
    Hud hud_;
    bool mounted_ = false;
    bool traceMode_ = false;
    bool desyncNoticed_ = false;
    int64_t seekTarget_ = -1;
    float acc_ = 0.0f;
    Animator ghost_;
    std::string playerName_, categoryText_;
    int64_t timeMs_ = 0;
    int rank_ = 0;
    float t_ = 0.0f;
    bool playing_ = false;
    int speedIdx_ = 1;
    std::array<float, 4> speeds_{0.5f, 1.0f, 2.0f, 4.0f};
    std::array<const char*, 4> speedLabels_{"×0.5", "×1", "×2", "×4"};
    Vec2 camPos_;
    bool snapCam_ = true;
};

}  // namespace

std::unique_ptr<Scene> makeReplayViewer(GameServices& g, const std::string& recordId) { return std::make_unique<ReplayScene>(g, recordId); }

std::unique_ptr<Scene> makeLocalReplayViewer(GameServices& g, ReplayData replay, const std::string& title) {
    return std::make_unique<ReplayScene>(g, std::move(replay), title);
}

}  // namespace cosmic
