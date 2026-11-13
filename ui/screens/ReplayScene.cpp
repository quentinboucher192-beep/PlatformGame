// VIEW REPLAY: downloads the replay of a verified record and plays it back over the real
// level: the player's ghost (position, state, facing), its trail, deaths, pickups, powers
// and checkpoints, with a timeline, pause, speed and seeking.

#include <cmath>

#include "game/GameServices.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
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
    ReplayScene(GameServices& g, std::string recordId) : g_(g), recordId_(std::move(recordId)) {}

    void onEnter() override {
        status_ = "Chargement du record…";
        g_.online.get("/api/v1/records/" + recordId_, OnlineService::guard(life_, [this](const ApiResult& r) {
            if (!r.ok) return fail("Record introuvable : " + r.message);
            record_ = r.data;
            downloadReplay();
        }), false);
    }

    void onExit() override {
        if (mounted_) g_.app.resources().popMount();
    }

    void update(float dt) override {
        if (!world_) return;
        if (playing_) {
            t_ += dt * speeds_[static_cast<size_t>(speedIdx_)];
            if (t_ >= duration()) {
                t_ = duration();
                playing_ = false;
            }
            // The level around keeps living (enemies, decor); the player of the world is hidden.
            fixedAcc_ += dt;
            while (fixedAcc_ >= Application::kFixedDt) {
                world_->fixedUpdate(PlayerInput{}, Application::kFixedDt);
                fixedAcc_ -= Application::kFixedDt;
            }
        }
        world_->update(dt);
        ReplayFrame f = replay_.sampleAt(t_);
        ReplayFrame prev = replay_.sampleAt(std::max(0.0f, t_ - 0.05f));
        Vec2 vel{(f.x - prev.x) / 0.05f, (f.y - prev.y) / 0.05f};
        ghost_.play(clipFor(f, vel));
        ghost_.update(dt * (playing_ ? speeds_[static_cast<size_t>(speedIdx_)] : 0.0f));
        Vec2 target{f.x, f.y - 12.0f};
        camPos_ = snapCam_ ? target : camPos_ + (target - camPos_) * std::min(1.0f, dt * 6.0f);
        snapCam_ = false;
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
        WorldView view = makeView(r);
        RenderOptions opt;
        opt.lighting = g_.settings.graphics.dynamicLighting;
        renderer_.render(r, g_.app.resources(), *world_, view, 1.0f, opt);
        r.setView(view);
        drawTrailAndEvents(r);
        drawGhost(r);
        drawHud();
        ui.end();
    }

private:
    float duration() const { return replay_.durationMs / 1000.0f; }

    void fail(const std::string& m) {
        status_ = m;
        failed_ = true;
    }

    void downloadReplay() {
        status_ = "Téléchargement du replay…";
        g_.online.getBinary("/api/v1/records/" + recordId_ + "/replay", [this, alive = std::weak_ptr<int>(life_)](const HttpResponse& resp) {
            if (!alive.lock()) return;
            if (resp.networkFailure()) return fail(std::string("Réseau : ") + netErrorLabel(resp.error));
            if (resp.status != 200) return fail(parseApiResponse(resp).message);
            std::vector<uint8_t> bytes(resp.body.begin(), resp.body.end());
            std::string err;
            auto rep = ReplayData::decode(bytes, &err);
            if (!rep) return fail("Replay illisible : " + err);
            replay_ = std::move(*rep);
            loadLevel();
        });
    }

    void loadLevel() {
        const Json& rec = record_.value("record", Json::object());
        std::string levelId = jstr(rec, "level_id");
        // Any installed copy of the level works, as long as it is the exact same revision.
        const LevelEntry* entry = g_.content.level(levelId);
        if (!entry) return fail("Ce niveau n'est pas installé dans votre jeu.");
        auto fp = fingerprintLevelFile(entry->path);
        if (!fp || fp->hash != replay_.levelHash) return fail("Ce replay a été enregistré sur une autre version du niveau.");
        std::string err;
        auto level = g_.content.loadLevel(levelId, &err);
        if (!level) return fail("Chargement du niveau impossible : " + err);
        level_ = *level;
        ResourceManager& res = g_.app.resources();
        if (const LevelEntry* e = g_.content.level(levelId); e && !e->assetRoot.empty()) {
            res.pushMount(e->assetRoot);
            mounted_ = true;
        }
        WorldServices ws{res, g_.app.audio(), g_.content, nullptr, 0.0f, 0.7f};
        world_ = std::make_unique<GameWorld>(ws, WorldMode::Play);
        world_->load(level_);
        if (world_->cinematic().active() && world_->cinematic().skippable()) world_->cinematic().skip(*world_);
        world_->player().setHidden(true);
        renderer_.build(g_.app.renderer(), res, *world_);
        ghost_.setSet(world_->player().anim.set());
        const Json& pl = record_.value("player", Json::object());
        playerName_ = jstr(pl, "username");
        timeMs_ = jget<int64_t>(rec, "time_ms", replay_.durationMs);
        RunCategory c;
        categoryText_ = categoryFromId(jstr(rec, "category"), c) ? categoryLabel(c) : "";
        rank_ = record_.contains("rank") && record_["rank"].is_number() ? record_["rank"].get<int>() : 0;
        snapCam_ = true;
        playing_ = true;
    }

    WorldView makeView(const Renderer& r) const {
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
        // Trail of the last two seconds.
        int hz = replay_.sampleHz;
        int cur = std::min(static_cast<int>(t_ * hz), static_cast<int>(replay_.frames.size()) - 1);
        for (int i = std::max(1, cur - hz * 2); i <= cur; ++i) {
            const ReplayFrame& a = replay_.frames[static_cast<size_t>(i - 1)];
            const ReplayFrame& b = replay_.frames[static_cast<size_t>(i)];
            if (std::hypot(b.x - a.x, b.y - a.y) > 120.0f) continue;
            float k = 1.0f - static_cast<float>(cur - i) / (hz * 2.0f);
            r.drawWorldLine({a.x, a.y - 11}, {b.x, b.y - 11}, Color{57, 240, 216, static_cast<uint8_t>(170 * k)}, 2.5f, BlendMode::Additive);
        }
        // Events that already happened.
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

    void drawHud() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillGradientV({0, 0, ui.screenW(), ui.s(90)}, Color{4, 8, 18, 220}, Color{4, 8, 18, 0});
        ui.label("REPLAY - " + playerName_, {ui.s(30), ui.s(32)}, 26, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        std::string sub = level_.name + " · " + categoryText_ + " · " + formatRunTime(timeMs_) + (rank_ > 0 ? " · n°" + std::to_string(rank_) : "");
        ui.label(sub, {ui.s(30), ui.s(62)}, 17, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
        // Counters up to the current time.
        int cur = static_cast<int>(t_ * replay_.sampleHz);
        int deaths = 0, pickups = 0, jumps = 0, powers = 0;
        for (const auto& e : replay_.events) {
            if (static_cast<int>(e.frame) > cur) break;
            deaths += e.type == ReplayEventType::Death;
            pickups += e.type == ReplayEventType::Pickup;
            jumps += e.type == ReplayEventType::Jump || e.type == ReplayEventType::AirJump;
            powers += e.type == ReplayEventType::Power;
        }
        std::string counters = "Morts " + std::to_string(deaths) + "   Objets " + std::to_string(pickups) + "   Sauts " + std::to_string(jumps) +
                               "   Pouvoirs " + std::to_string(powers);
        ui.label(counters, {ui.screenW() - ui.s(30), ui.s(32)}, 18, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
        ui.label(formatRunTime(static_cast<int64_t>(t_ * 1000.0f)) + " / " + formatRunTime(replay_.durationMs),
                 {ui.screenW() - ui.s(30), ui.s(62)}, 20, ui.theme().accent, Align::Right, FontFace::Bold, VAlign::Middle);
        // Timeline with event ticks.
        Rect bar{ui.s(200), ui.screenH() - ui.s(70), ui.screenW() - ui.s(560), ui.s(14)};
        r.fillRoundedRect(bar, ui.s(7), Color{20, 30, 56, 230});
        float k = duration() > 0 ? t_ / duration() : 0.0f;
        r.fillRoundedRect({bar.x, bar.y, bar.w * k, bar.h}, ui.s(7), ui.theme().accent.withAlphaF(0.8f));
        size_t n = std::max<size_t>(1, replay_.frames.size() - 1);
        for (const auto& e : replay_.events) {
            float x = bar.x + bar.w * static_cast<float>(e.frame) / static_cast<float>(n);
            if (e.type == ReplayEventType::Death) r.fillRect({x - 1, bar.y - ui.s(8), ui.s(3), bar.h + ui.s(16)}, Color{255, 77, 109, 230});
            else if (e.type == ReplayEventType::Checkpoint) r.fillRect({x - 1, bar.y - ui.s(5), ui.s(2), bar.h + ui.s(10)}, Color{57, 240, 216, 200});
        }
        InputManager& in = g_.app.input();
        if (in.mouseDown(SDL_BUTTON_LEFT) && bar.expanded(ui.s(8)).contains(in.mousePos())) {
            t_ = std::clamp((in.mousePos().x - bar.x) / bar.w, 0.0f, 1.0f) * duration();
            snapCam_ = true;
        }
        ui.pushId("replay");
        UIContext::ButtonStyle st;
        st.small = true;
        float by = ui.screenH() - ui.s(90);
        if (ui.button("back", {ui.s(24), by, ui.s(150), ui.s(42)}, "Retour", st) || ui.backPressed()) g_.app.popScene();
        if (ui.button("play", {ui.screenW() - ui.s(340), by, ui.s(140), ui.s(42)}, playing_ ? "Pause" : "Lecture", st)) {
            if (!playing_ && t_ >= duration()) t_ = 0.0f;
            playing_ = !playing_;
        }
        if (ui.button("speed", {ui.screenW() - ui.s(190), by, ui.s(80), ui.s(42)}, speedLabels_[static_cast<size_t>(speedIdx_)], st))
            speedIdx_ = (speedIdx_ + 1) % 4;
        if (ui.button("restart", {ui.screenW() - ui.s(100), by, ui.s(76), ui.s(42)}, "Début", st)) {
            t_ = 0.0f;
            snapCam_ = true;
            playing_ = true;
        }
        ui.popId();
        // Gamepad / keyboard: left / right seek by 5 s.
        if (in.menuPressed(Action::MenuLeft)) {
            t_ = std::max(0.0f, t_ - 5.0f);
            snapCam_ = true;
        }
        if (in.menuPressed(Action::MenuRight)) {
            t_ = std::min(duration(), t_ + 5.0f);
            snapCam_ = true;
        }
    }

    GameServices& g_;
    std::string recordId_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string status_;
    bool failed_ = false;
    Json record_;
    ReplayData replay_;
    LevelData level_;
    std::unique_ptr<GameWorld> world_;
    GameRenderer renderer_;
    bool mounted_ = false;
    Animator ghost_;
    std::string playerName_, categoryText_;
    int64_t timeMs_ = 0;
    int rank_ = 0;
    float t_ = 0.0f;
    float fixedAcc_ = 0.0f;
    bool playing_ = false;
    int speedIdx_ = 1;
    std::array<float, 4> speeds_{0.5f, 1.0f, 2.0f, 4.0f};
    std::array<const char*, 4> speedLabels_{"×0.5", "×1", "×2", "×4"};
    Vec2 camPos_;
    bool snapCam_ = true;
};

}  // namespace

std::unique_ptr<Scene> makeReplayViewer(GameServices& g, const std::string& recordId) { return std::make_unique<ReplayScene>(g, recordId); }

}  // namespace cosmic
