// 2.62: a team plays an EXTRÊME level (docs/COOP.md): 2 or 3 players in one world, on one PC
// (keyboard / first gamepad for player 1, a gamepad each for the others), on the local
// network or online (a relay orders the inputs: CoopLink). The simulation only ever sees the
// team frames (one InputFrame per player), the same on every computer.

#include "ui/screens/CoopGameScene.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <map>
#include <set>
#include <thread>

#include "engine/core/Log.h"
#include "engine/input/InputScript.h"
#include "engine/input/LocalPad.h"
#include "game/GameServices.h"
#include "game/cosmetics/SuitRecolor.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/powers/PowerKeys.h"
#include "game/powers/Powers.h"
#include "game/race/Race.h"
#include "ui/screens/DeviceLostUi.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

/// « 1:18 » / « 12:05 ».
std::string clock(float seconds) {
    const int s = static_cast<int>(std::max(0.0f, seconds));
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

/// The suits of the players 2 and 3 (as the lobby shows them), recoloured from Nova's sheet.
SuitColors seatSuit(int seat) {
    SuitColors c;
    if (seat == 1) c = {{255, 231, 240}, {255, 122, 184}, {255, 210, 122}};
    else if (seat == 2) c = {{255, 243, 214}, {255, 210, 122}, {62, 230, 214}};
    else c = {{234, 242, 255}, {62, 230, 214}, {255, 93, 143}};
    return c;
}

class CoopGameScene : public Scene {
public:
    CoopGameScene(GameServices& g, CoopSetup setup, std::unique_ptr<CoopLink> link)
        : g_(g), setup_(std::move(setup)), link_(std::move(link)) {}

    /// 2.70, a capture for the documentation (--coop-test, headless only): each seat's script
    /// and where it appears.
    void setCapture(const CoopCapture& c) {
        scripts_.assign(setup_.seats.size(), InputScript{});
        for (size_t i = 0; i < c.scripts.size() && i < scripts_.size(); ++i) scripts_[i].parse(c.scripts[i]);
        spawns_ = c.spawns;
    }
    bool scripted(int seat) const { return seat >= 0 && seat < static_cast<int>(scripts_.size()) && scripts_[static_cast<size_t>(seat)].active(); }

    ~CoopGameScene() override {
        stopWorker_ = true;
        if (worker_.joinable()) worker_.join();
        releaseInput();
    }

    /// Player 1's controls back on the input, every pad free again (leaving the game).
    void releaseInput() {
        InputManager& in = g_.app.input();
        in.setReservedPads({});
        in.setGamepadGameplay(true);
        in.setBindings(g_.settings.input);
    }

    bool holdsNotifications() const override { return true; }
    bool hidesCursor() const override {
        return mounted_ && !loadFailed_ && world_ && !catching_ && !paused_ && !choosing_ && !ended_ && !unplugShown_;
    }

    void onEnter() override {
        ResourceManager& res = g_.app.resources();
        auto lvl = g_.content.loadLevel(setup_.levelId, &loadError_);
        if (!lvl || setup_.seats.size() < 2) {
            loadFailed_ = true;
            if (loadError_.empty()) loadError_ = "Équipe incomplète";
            return;
        }
        level_ = *lvl;
        if (const LevelEntry* e = g_.content.level(setup_.levelId); e && !e->assetRoot.empty()) {
            res.pushMount(e->assetRoot);
            mounted_ = true;
        }
        const int n = static_cast<int>(setup_.seats.size());
        WorldServices ws{res, g_.app.audio(), g_.content, &g_.app.input(), g_.settings.graphics.screenShake,
                         g_.settings.graphics.particleQuality == 0 ? 0.4f : g_.settings.graphics.particleQuality == 1 ? 0.7f : 1.0f};
        world_ = std::make_unique<GameWorld>(ws, WorldMode::Play);
        world_->setTeamSize(n);
        world_->setKeysPerSlot(true);
        world_->load(level_);
        world_->seedGameplay(setup_.seed != 0 ? setup_.seed : levelSeed(setup_.levelId));
        // Looks (display only): player 1 on this computer wears its suit, the others theirs.
        for (int i = 0; i < n; ++i) {
            TexturePtr t;
            if (i == 0 && setup_.local(0)) t = g_.suitSheet(g_.cosmeticLook("suit"));
            else if (i > 0) t = g_.suitTextures.get(res, g_.app.renderer(), seatSuit(i));
            suits_.push_back(t);
            world_->playerAt(i).skin = t ? t.get() : nullptr;
        }
        std::vector<Color> colors;
        Hud::TeamInfo info;
        for (int i = 0; i < n; ++i) {
            colors.push_back(setup_.seats[static_cast<size_t>(i)].color);
            info.names.push_back(setup_.seats[static_cast<size_t>(i)].name);
        }
        info.colors = colors;
        info.promptPlayer = setup_.localSeat >= 0 ? setup_.localSeat : 0;
        hud_.setTeam(info);
        // 2.70, a capture: where each seat appears (a free spot near the place given).
        for (size_t i = 0; i < spawns_.size() && static_cast<int>(i) < n; ++i)
            if (spawns_[i]) world_->restoreMember(static_cast<int>(i), world_->carryOf(static_cast<int>(i)), *spawns_[i]);
        renderer_.build(g_.app.renderer(), res, *world_);
        renderer_.setTeamColors(colors);
        if (g_.settings.gameplay.progressBar > 0)
            trackFuture_ = std::async(std::launch::async, [level = level_, &content = g_.content] { return buildProgressTrack(level, content); });
        // The progress of the attempt (the course à la vérification counts the most advanced player).
        RaceLayout layout = buildRaceLayout(level_, g_.content, false);
        trackers_.resize(static_cast<size_t>(n));
        for (auto& t : trackers_) t.begin(layout);
        routeFuture_ = std::async(std::launch::async, [level = level_, &content = g_.content] { return raceRoute(intendedRoute(level, content)); });
        // The other local players' gamepads: theirs only.
        std::vector<SDL_JoystickID> reserved;
        pads_.resize(static_cast<size_t>(n));
        devices_.assign(static_cast<size_t>(n), LocalDevice::none());
        dropped_.assign(static_cast<size_t>(n), 0);
        unplug_.reset(n);
        for (int i = 0; i < n; ++i) {
            CoopSeat& s = setup_.seats[static_cast<size_t>(i)];
            if (!setup_.local(i)) continue;
            if (s.device == CoopSeat::Device::Keyboard) devices_[static_cast<size_t>(i)] = LocalDevice::keyboard();
            if (s.device != CoopSeat::Device::Pad) continue;
            if (s.padNumber == 0) s.padNumber = g_.app.input().devices().numberOf(static_cast<SDL_JoystickID>(s.pad));
            devices_[static_cast<size_t>(i)] = LocalDevice::gamepad(s.padNumber);
            pads_[static_cast<size_t>(i)] = std::make_unique<LocalPad>();
            if (pads_[static_cast<size_t>(i)]->open(static_cast<SDL_JoystickID>(s.pad))) reserved.push_back(static_cast<SDL_JoystickID>(s.pad));
        }
        g_.app.input().setReservedPads(reserved);
        reserved_ = reserved;
        // 2.65, one PC: each player's own controls (profiles J1, J2, J3); the keyboard player plays
        // with the keyboard and the mouse only, every pad being read by its own LocalPad.
        if (setup_.mode == CoopSetup::Mode::SamePc) {
            g_.app.input().setGamepadGameplay(false);
            if (const int kb = keyboardSeat(); kb >= 0) g_.app.input().setBindings(g_.settings.playerInput(kb));
        }
        powerKeys_ = levelPowerKeys(level_, g_.content, g_.settings.input, &g_.powerKeyOverrides);
        seatKeys_.clear();
        for (int i = 0; i < n; ++i)
            seatKeys_.push_back(setup_.mode == CoopSetup::Mode::SamePc
                                    ? levelPowerKeys(level_, g_.content, g_.settings.playerInput(i), &g_.powerKeysOf(i))
                                    : powerKeys_);
        syncPowerKeys();
        g_.app.input().consumePlayerInput();  // the lobby's presses
        // The team replay (format 5): every player's inputs and trace, the team's digests.
        // 2.63.5: a community level is identified by what it plays like (its published fingerprint).
        std::string hash = std::string(64, '0');
        if (!setup_.communityId.empty()) hash = fingerprintGameplay(g_.content, level_);
        else if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, setup_.levelId)) hash = fp->hash;
        levelHash_ = hash;
        recorder_.begin(setup_.levelId, hash, setup_.attemptId.empty() ? makeUuidV4() : setup_.attemptId, world_->gameplaySeed());
        recorder_.beginTeam(n);
        world_->eventObserver = [this](const std::string& src, const std::string& trig) { recorder_.onWorldEvent(src, trig, world_->currentPlayer()); };
        frame_.reserve(static_cast<size_t>(n));
        localFrames_.reserve(static_cast<size_t>(n));
        // Back in a game already started: its steps are played again off the main thread, fast
        // and quiet (no sound, no particle), while the screen shows how far it went.
        if (link_->joinedLate()) {
            catching_ = true;
            world_->setQuiet(true);
            worker_ = std::thread([this] { catchUp(); });
        }
    }

    void catchUp() {
        std::vector<InputFrame> team;
        team.reserve(4);
        while (!stopWorker_) {
            if (link_->frameFor(tick_, team)) {
                recorder_.recordTeamInputs(team.data(), static_cast<int>(team.size()));
                stepTeam(*world_, team.data(), static_cast<int>(team.size()), kSimDt);
                recorder_.tick(*world_);
                ++tick_;
                progress_ = tick_;
                if (world_->state() == WorldState::Completed || world_->state() == WorldState::Failed) break;
                continue;
            }
            // Within two steps of what the relay gave: caught up.
            if (tick_ + 2 >= link_->available()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        workerDone_ = true;
    }

    void finishCatchUp() {
        if (worker_.joinable()) worker_.join();
        catching_ = false;
        world_->setQuiet(false);
        g_.app.resources().finishDeferred();  // the textures asked for off the main thread
        link_->catchupDone(tick_);
        if (world_->state() == WorldState::Completed) finish(true);
        else if (world_->state() == WorldState::Failed) finish(false);
    }

    void onExit() override {
        releaseInput();
        g_.app.input().clearPowerSlotKeys();
        if (mounted_) g_.app.resources().popMount();
        mounted_ = false;
    }

    // ------------------------------------------------------------------ simulation

    void fixedUpdate(float dt) override {
        if (!world_ || ended_) return;
        if (catching_) {
            if (workerDone_) finishCatchUp();
            return;
        }
        const bool network = setup_.mode != CoopSetup::Mode::SamePc;
        if (paused_ && !network) return;  // one PC: the pause stops the team
        if (!network && unplug_.paused()) return;  // 2.65: a device missing, then 3-2-1: no step, the chrono waits
        // How many steps this frame: one, more when the relay is ahead (after a wait).
        int steps = 1;
        if (network && link_->available() > tick_ + static_cast<uint32_t>(link_->inputDelay() + 6)) steps = 4;
        for (int k = 0; k < steps && !ended_; ++k) {
            sendLocal(dt);
            if (!link_->frameFor(tick_, frame_)) {
                waiting_ += dt;
                break;
            }
            waiting_ = 0.0f;
            step(frame_);
        }
    }

    void sendLocal(float dt) {
        const uint32_t target = tick_ + static_cast<uint32_t>(link_->inputDelay());
        if (sentUpTo_ != UINT32_MAX && target <= sentUpTo_) return;  // already given for that step
        sentUpTo_ = target;
        auto& frames = localFrames_;  // kept: no allocation per step
        frames.clear();
        const int n = static_cast<int>(setup_.seats.size());
        // Hold Confirm / Jump to skip a skippable cinematic (any local player).
        bool skip = false;
        if (world_->cinematic().active() && world_->cinematic().skippable()) {
            bool held = g_.app.input().held(Action::MenuConfirm) || g_.app.input().held(Action::Jump);
            for (const auto& p : pads_)
                if (p && p->connected() && p->heldJump()) held = true;
            skipHold_ = held ? skipHold_ + dt / 0.8f : std::max(0.0f, skipHold_ - dt * 2.0f);
            if (skipHold_ >= 1.0f) {
                skipHold_ = 0.0f;
                skip = true;
            }
        } else {
            skipHold_ = 0.0f;
        }
        hud_.setSkipHold(skipHold_);
        for (int i = 0; i < n; ++i) {
            if (!setup_.local(i)) continue;
            const CoopSeat& s = setup_.seats[static_cast<size_t>(i)];
            PlayerInput in;
            if (scripted(i)) in = scripts_[static_cast<size_t>(i)].next();  // 2.70: a capture's seat
            else if (s.device == CoopSeat::Device::Pad && pads_[static_cast<size_t>(i)]) in = pads_[static_cast<size_t>(i)]->consume();
            else if (s.device == CoopSeat::Device::Keyboard) in = g_.app.input().consumePlayerInput();
            InputFrame f = InputFrame::fromPlayerInput(in);
            // 2.65: « Continuer sans » : this PC decides the player's absence as the relay does
            // online (Standby: 60 s to come back, then gone; the team is ranked incomplete).
            if (i < static_cast<int>(dropped_.size()) && dropped_[static_cast<size_t>(i)]) {
                f = InputFrame{};
                f.flags = InputFrame::Standby;
                frames.emplace_back(i, f);
                continue;
            }
            if (paused_ || choosing_) f = InputFrame{};  // nothing moves for a player in a menu
            if (skip) {
                f = InputFrame{};
                f.flags = InputFrame::SkipCinematic;
            }
            if (pendingChoice_ != 0 && world_->powerOffer() && world_->powerOffer()->player == i) {
                f.choice = pendingChoice_;
                pendingChoice_ = 0;
                choosing_ = false;
            }
            frames.emplace_back(i, f);
        }
        link_->pushLocal(target, frames);
    }

    void step(const std::vector<InputFrame>& team) {
        recorder_.recordTeamInputs(team.data(), static_cast<int>(team.size()));
        stepTeam(*world_, team.data(), static_cast<int>(team.size()), kSimDt);
        ++tick_;
        for (int i = 0; i < world_->teamSize(); ++i) {
            GameWorld::PlayerScope who(*world_, i);
            trackers_[static_cast<size_t>(i)].update(*world_);
        }
        const RaceProgress p = teamProgress();
        recorder_.raceProgress(p.bossRemoved, p.wavesDone);
        recorder_.tick(*world_);
        if (world_->state() == WorldState::Completed) finish(true);
        else if (world_->state() == WorldState::Failed) finish(false);
    }

    /// The progress of the team: the most advanced player's route, the bosses and waves.
    RaceProgress teamProgress() const {
        RaceProgress p;
        if (trackers_.empty()) return p;
        p = trackers_.front().progress();
        for (const auto& t : trackers_) {
            p.route = std::max(p.route, t.progress().route);
            p.finished = p.finished || t.progress().finished;
        }
        return p;
    }

    void finish(bool completed) {
        if (ended_) return;
        ended_ = true;
        completed_ = completed;
        endTime_ = world_->timer().elapsed();
        RaceProgress p = teamProgress();
        if (completed) p.finished = true;
        endScore_ = trackers_.empty() ? 0.0f : raceScore(trackers_.front().layout(), p);
        if (completed) recorder_.finish(*world_, endTime_);
        else recorder_.finishAttempt(*world_, endTime_);
        replay_ = std::make_shared<ReplayData>(recorder_.take());
        link_->result(tick_);  // online: the relay notes the inputs of the attempt (ranking)
        submitRanking(completed, p);
        loadBoard();
        g_.ui.setFocus("coopend/again");
    }

    /// The Duo / Trio boards (docs/COOP.md § 6): this computer's player confirms the attempt with
    /// its own account; the host (player 1 on one PC) sends it with the team replay. Ranked only
    /// when every player has an account and confirmed it.
    void submitRanking(bool completed, const RaceProgress& p) {
        if (!replay_ || replay_->inputs.empty() || endScore_ < 0.1f) return;
        if (setup_.mode == CoopSetup::Mode::SamePc) {
            // An account opens only on its own computer (2.47): players 2 and 3 have none here.
            rankNote_ = "Hors classement : une partie sur un même PC n'est pas classée (chacun joue avec son compte sur son PC).";
            return;
        }
        bool accounts = true;
        std::vector<std::string> ids;
        for (const auto& s : setup_.seats) {
            if (s.accountId.empty()) accounts = false;
            ids.push_back(s.accountId);
        }
        if (g_.online.loggedIn()) g_.online.confirmCoopAttempt(replay_->attemptId);
        if (!accounts) {
            rankNote_ = "Hors classement : chaque joueur doit être connecté à son compte Cosmic Online.";
            return;
        }
        const bool submitter = setup_.localSeat == 0;  // the host sends it
        rankNote_ = "Tentative envoyée pour le classement " + std::string(ids.size() >= 3 ? "Trio" : "Duo") +
                    " : classée dès que chaque joueur l'a confirmée depuis son compte.";
        if (!submitter) return;
        OnlineService::RaceAttempt a;
        if (!setup_.communityId.empty()) {
            // 2.63.5: a community level's Duo / Trio boards, per version (no clan points: those
            // count the official levels only).
            a.kind = "community";
            a.levelId = setup_.communityId;
            a.version = setup_.communityVersion;
        } else {
            const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, setup_.levelId);
            if (!fp) return;
            a.kind = "official";
            a.levelId = setup_.levelId;
            a.levelHash = fp->hash;
        }
        a.title = level_.name;
        a.attemptId = replay_->attemptId;
        a.timeMs = static_cast<int64_t>(std::llround(static_cast<double>(endTime_) * 1000.0));
        a.finished = completed;
        for (float b : p.bossRemoved) a.bosses.push_back(static_cast<int>(std::clamp(std::lround(b * 1000.0f), 0L, 1000L)));
        a.waves = p.wavesDone;
        a.estimate = endScore_;
        const char* mode = setup_.mode == CoopSetup::Mode::Online ? "online" : setup_.mode == CoopSetup::Mode::Lan ? "lan" : "same_pc";
        g_.online.submitCoopAttempt(std::move(a), mode, ids, replay_->encode());
    }

    // ---------------------------------------------------------------- frame update

    void syncPowerKeys() {
        if (!world_) return;
        // The keyboard / first gamepad player: the keys of the powers it carries.
        const int kb = keyboardSeat();
        if (kb >= 0) applyPowerSlotKeys(g_.app.input(), world_->playerAt(kb), keysOf(kb));
        // Each other gamepad: the buttons of the powers its player carries.
        for (size_t i = 0; i < pads_.size(); ++i) {
            if (!pads_[i]) continue;
            std::array<PadBinding, kPowerSlotKeys> slots{};
            const auto& powers = world_->playerAt(static_cast<int>(i)).powers();
            for (int s = 0; s < kPowerSlotKeys && static_cast<size_t>(s) < powers.size(); ++s) {
                const Power& pw = *powers[static_cast<size_t>(s)];
                if (pw.trigger() != PowerTrigger::SlotKey || pw.key() != powerSlotAction(s)) continue;
                if (const ResolvedPowerKey* r = keysOf(static_cast<int>(i)).find(pw.id())) slots[static_cast<size_t>(s)] = r->pad;
            }
            pads_[i]->setSlotPads(slots);
        }
    }

    /// The power keys of a player (its own profile on one PC).
    const PowerKeyMap& keysOf(int seat) const {
        return seat >= 0 && seat < static_cast<int>(seatKeys_.size()) ? seatKeys_[static_cast<size_t>(seat)] : powerKeys_;
    }

    int keyboardSeat() const {
        for (int i = 0; i < static_cast<int>(setup_.seats.size()); ++i)
            if (setup_.local(i) && setup_.seats[static_cast<size_t>(i)].device == CoopSeat::Device::Keyboard) return i;
        return -1;
    }

    void update(float dt) override {
        if (!world_) return;
        link_->update(dt);
        if (catching_) return;  // the catch-up thread has the world
        for (size_t i = 0; i < pads_.size(); ++i)
            if (pads_[i]) pads_[i]->update(g_.settings.playerInput(static_cast<int>(i)), dt);
        if (setup_.mode == CoopSetup::Mode::SamePc && !ended_) watchDevices(dt);
        else if (setup_.mode == CoopSetup::Mode::SamePc && !reserved_.empty()) {
            reserved_.clear();  // the end screen: every pad moves in it
            g_.app.input().setReservedPads({});
        }
        world_->update(dt);
        hud_.update(*world_, dt);
        if (trackFuture_.valid() && trackFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) hud_.setProgressTrack(trackFuture_.get());
        if (routeFuture_.valid() && routeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto route = routeFuture_.get();
            for (auto& t : trackers_) t.setRoute(route);
        }
        syncPowerKeys();
        // The team panel's network lines.
        Hud::TeamInfo info = hud_.team();
        info.pingMs.clear();
        for (int i = 0; i < static_cast<int>(setup_.seats.size()); ++i) info.pingMs.push_back(link_->pingMs(i));
        info.waitingFor.clear();
        if (waiting_ > 0.4f) {
            std::string who = link_->waitingFor();
            info.waitingFor = who.empty() ? "En attente du réseau…" : "En attente de " + who + "…";
        } else if (setup_.mode != CoopSetup::Mode::SamePc) {
            const int ms = (link_->inputDelay() * 1000 + 60) / 120;
            info.waitingFor = "Délai d'entrée : " + std::to_string(link_->inputDelay()) + " images (" + std::to_string(ms) + " ms)";
        }
        info.deviceNotes = deviceNotes();
        hud_.setTeam(info);
        if (!link_->endReason().empty() && !ended_) {
            ended_ = true;
            aborted_ = link_->endReason();
            g_.ui.setFocus("coopend/lobby");
        }
        // Pause: Échap, or Start on any local gamepad (not while a device is missing).
        if (!ended_ && !choosing_ && !unplug_.paused()) {
            bool toggle = g_.app.input().menuPressed(Action::Pause);
            for (auto& p : pads_)
                if (p && p->startPressed()) toggle = true;
            if (toggle) {
                paused_ = !paused_;
                pauseOpenedNow_ = paused_;  // the same press (Échap = pause and back) must not close it
                g_.ui.setFocus("coop/pause/resume");
            }
        }
        // 2.70, a capture: a scripted seat leaves the power offered (no menu).
        if (const auto& offer = world_->powerOffer(); offer && scripted(offer->player) && pendingChoice_ == 0) pendingChoice_ = InputFrame::kChoiceLeave;
        // A power offered to a player of this computer: its choice (stops the world, as alone).
        if (const auto& offer = world_->powerOffer(); offer && setup_.local(offer->player) && pendingChoice_ == 0) {
            if (!choosing_) {
                choosing_ = true;
                choiceIndex_ = 0;
                g_.ui.setFocus("coop/choice/0");
            }
            if (LocalPad* p = offer->player < static_cast<int>(pads_.size()) ? pads_[static_cast<size_t>(offer->player)].get() : nullptr) {
                // The gamepad of the player it is offered to moves the choice too.
                const int count = static_cast<int>(world_->playerAt(offer->player).powers().size()) + 1;
                if (p->navDown()) choiceIndex_ = (choiceIndex_ + 1) % count;
                if (p->navUp()) choiceIndex_ = (choiceIndex_ + count - 1) % count;
                if (p->confirmPressed()) pendingChoice_ = choiceIndex_ + 1 >= count ? InputFrame::kChoiceLeave : static_cast<uint8_t>(choiceIndex_ + 1);
                if (p->backPressed()) pendingChoice_ = InputFrame::kChoiceLeave;
            }
        } else if (!world_->powerOffer()) {
            choosing_ = false;
        }
    }

    // ------------------------------------------------------------ 2.65: devices on one PC

    /// The player who has pad `number`, -1 if it is free.
    int padOwner(int number) const {
        for (size_t i = 0; i < devices_.size(); ++i)
            if (devices_[i].isPad() && devices_[i].pad == number) return static_cast<int>(i);
        return -1;
    }

    /// Gives a player a pad (its own plugged back, or a free one taking its place): a LocalPad
    /// of its own, its buttons for the powers it carries; it vibrates once when it takes a place.
    void givePad(int i, int number, bool rumble) {
        const PadEntry* p = g_.app.input().devices().byNumber(number);
        if (!p || !p->connected || i < 0 || i >= static_cast<int>(setup_.seats.size())) return;
        CoopSeat& s = setup_.seats[static_cast<size_t>(i)];
        s.device = CoopSeat::Device::Pad;
        s.pad = p->id;
        s.padNumber = number;
        devices_[static_cast<size_t>(i)] = LocalDevice::gamepad(number);
        spare_.erase(p->id);
        pads_[static_cast<size_t>(i)] = std::make_unique<LocalPad>();
        pads_[static_cast<size_t>(i)]->open(p->id);
        if (rumble && g_.settings.playerInput(i).gamepad.rumble) pads_[static_cast<size_t>(i)]->rumble(0.55f, 0.55f, 220);
        syncPowerKeys();
    }

    /// Gives a player the keyboard and the mouse, with its own keys.
    void giveKeyboard(int i) {
        CoopSeat& s = setup_.seats[static_cast<size_t>(i)];
        s.device = CoopSeat::Device::Keyboard;
        s.pad = 0;
        s.padNumber = 0;
        devices_[static_cast<size_t>(i)] = LocalDevice::keyboard();
        pads_[static_cast<size_t>(i)].reset();
        g_.app.input().setBindings(g_.settings.playerInput(i));
        g_.app.input().consumePlayerInput();
        syncPowerKeys();
    }

    /// A player has a device again: the run waits no more for it; one left out comes back.
    void backIn(int i) {
        unplug_.restored(i);
        dropped_[static_cast<size_t>(i)] = 0;
        unplugMsg_.clear();
    }

    /// What was pressed during the pause does nothing once the run goes on.
    void flushInputs() {
        g_.app.input().consumePlayerInput();
        for (auto& p : pads_)
            if (p) p->consume();
    }

    /// Every frame on one PC: a player's pad gone (the run waits: no step, the chrono stops),
    /// plugged back (3, 2, 1), a free pad's A or the keyboard's Entrée taking the place, the
    /// menus' pads, the batteries.
    void watchDevices(float dt) {
        InputManager& in = g_.app.input();
        const DeviceRegistry& reg = in.devices();
        const int n = static_cast<int>(setup_.seats.size());
        for (int i = 0; i < n; ++i) {
            if (!devices_[static_cast<size_t>(i)].isPad()) continue;
            const int num = devices_[static_cast<size_t>(i)].pad;
            const PadEntry* p = reg.byNumber(num);
            if (!p || !p->connected) {
                if (!dropped_[static_cast<size_t>(i)] && !unplug_.waiting(i)) unplug_.lost(i);
                continue;
            }
            // Plugged back (SDL gives it another id): its LocalPad again, the run goes on.
            if (!pads_[static_cast<size_t>(i)] || pads_[static_cast<size_t>(i)]->id() != p->id || !pads_[static_cast<size_t>(i)]->connected())
                givePad(i, num, false);
            if (unplug_.waiting(i) || dropped_[static_cast<size_t>(i)]) backIn(i);
        }
        // Whose place a free pad (A, Start) or the free keyboard (Entrée) takes: the player the run
        // waits for, else a player the team went on without.
        int target = unplug_.firstWaiting();
        for (int i = 0; i < n && target < 0; ++i)
            if (dropped_[static_cast<size_t>(i)]) target = i;
        std::set<SDL_JoystickID> freePads;
        if (target >= 0)
            for (int num : reg.connectedNumbers())
                if (padOwner(num) < 0) freePads.insert(reg.byNumber(num)->id);
        for (auto it = spare_.begin(); it != spare_.end();) it = freePads.count(it->first) ? std::next(it) : spare_.erase(it);
        SDL_JoystickID taker = 0;
        for (SDL_JoystickID id : freePads) {
            auto& w = spare_[id];
            if (!w) {
                w = std::make_unique<LocalPad>();
                w->open(id);  // a button already held counts once released
            }
            w->update(g_.settings.playerInput(std::max(0, target)), dt);
            if (!taker && (w->confirmPressed() || w->startPressed())) taker = id;
        }
        if (taker && target >= 0) {
            givePad(target, reg.numberOf(taker), true);
            backIn(target);
        } else if (target >= 0 && keyboardSeat() < 0 && (in.keyPressed(SDL_SCANCODE_RETURN) || in.keyPressed(SDL_SCANCODE_KP_ENTER))) {
            giveKeyboard(target);
            backIn(target);
        }
        // 3, 2, 1: the run goes on.
        const bool was = unplug_.paused();
        unplug_.update(dt);
        if (was && !unplug_.paused()) flushInputs();
        if (unplug_.paused() && !unplugShown_) {
            unplugShown_ = true;
            unplugConfirmQuit_ = false;
            g_.ui.setFocus("unplug/resume");
        } else if (!unplug_.paused()) {
            unplugShown_ = false;
        }
        // The menus: the pads of the players still there move in « MANETTE DÉBRANCHÉE » (the
        // free ones take a place), every pad in the pause menu; none during the run (each pad
        // plays for its own player).
        std::vector<SDL_JoystickID> reserved;
        if (unplug_.paused()) reserved.assign(freePads.begin(), freePads.end());
        else if (!paused_)
            for (int num : reg.connectedNumbers()) reserved.push_back(reg.byNumber(num)->id);
        if (reserved != reserved_) {
            reserved_ = reserved;
            in.setReservedPads(reserved_);
        }
        // A low battery: a line in the team panel, no pause (only when SDL knows the charge).
        if ((batteryTimer_ -= dt) <= 0.0f) {
            batteryTimer_ = 2.0f;
            batteryNotes_.clear();
            for (int i = 0; i < n; ++i) {
                const LocalPad* p = pads_[static_cast<size_t>(i)].get();
                const int pct = p ? p->batteryPercent() : -1;
                if (pct >= 0 && pct <= 15)
                    batteryNotes_.push_back("Pile faible : manette de " + setup_.seats[static_cast<size_t>(i)].name + " (" + std::to_string(pct) + " %)");
            }
        }
    }

    /// The team panel's device lines.
    std::vector<std::string> deviceNotes() const {
        std::vector<std::string> out = batteryNotes_;
        if (setup_.mode != CoopSetup::Mode::SamePc && g_.app.input().lostPad() != 0)
            out.push_back("Votre manette est débranchée : rebranchez-la ou jouez au clavier. En ligne, la partie continue pour l'équipe.");
        return out;
    }

    /// « MANETTE DÉBRANCHÉE · J2 · Lyra · Manette Xbox (n° 1) », then 3, 2, 1.
    void drawUnplug() {
        const int p = unplug_.firstWaiting();
        DeviceLostView v;
        v.coop = true;
        if (p >= 0) {
            const CoopSeat& s = setup_.seats[static_cast<size_t>(p)];
            v.seat = "J" + std::to_string(p + 1);
            v.name = s.name;
            v.color = s.color;
            const PadEntry* e = g_.app.input().devices().byNumber(devices_[static_cast<size_t>(p)].pad);
            v.device = e ? padLabel(*e) : std::string("Manette");
        }
        v.keyboardFree = keyboardSeat() < 0;
        v.countdown = unplug_.countdownNumber();
        v.message = unplugMsg_;
        v.confirmQuit = unplugConfirmQuit_;
        switch (drawDeviceLost(g_.ui, v)) {
            case DeviceLostChoice::Resume:
                if (p >= 0 && g_.app.input().devices().connected(devices_[static_cast<size_t>(p)].pad)) backIn(p);
                else if (p >= 0)
                    unplugMsg_ = "La manette de " + v.name + " est toujours débranchée : rebranchez-la, ou A sur une manette libre, ou Entrée au clavier.";
                break;
            case DeviceLostChoice::ContinueWithout:
                if (p >= 0) {
                    dropped_[static_cast<size_t>(p)] = 1;
                    unplug_.dropped(p);
                    unplugMsg_.clear();
                }
                break;
            case DeviceLostChoice::Quit:
                if (unplugConfirmQuit_) backToLobby();
                unplugConfirmQuit_ = true;
                break;
            case DeviceLostChoice::None: break;
        }
    }


    // --------------------------------------------------------------------- drawing

    void render(float alpha) override {
        Renderer& r = g_.app.renderer();
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        if (loadFailed_ || !world_) {
            r.fillRect({0, 0, static_cast<float>(r.width()), static_cast<float>(r.height())}, {8, 10, 20, 255});
            ui.label("Impossible de charger le niveau", {ui.screenW() * 0.5f, ui.screenH() * 0.42f}, 40, ui.theme().danger, Align::Center,
                     FontFace::Title, VAlign::Middle);
            ui.label(loadError_, {ui.screenW() * 0.5f, ui.screenH() * 0.5f}, 24, ui.theme().text, Align::Center, FontFace::Body, VAlign::Middle);
            UIContext::ButtonStyle bs;
            if (ui.button("coop/fail/back", {ui.screenW() * 0.5f - ui.s(160), ui.screenH() * 0.6f, ui.s(320), ui.s(56)}, "Retour au salon", bs))
                backToLobby();
            return;
        }
        if (catching_) {
            drawCatchUp();
            return;
        }
        WorldView view = world_->camera().view(r.width(), r.height(), alpha);
        RenderOptions opt;
        opt.lighting = g_.settings.graphics.dynamicLighting;
        opt.debug = g_.app.debugOverlay;
        renderer_.render(r, g_.app.resources(), *world_, view, alpha, opt);
        hud_.render(ui, *world_, view, nullptr, g_.settings.gameplay);
        if (unplug_.paused() && !ended_) {
            drawUnplug();
            return;
        }
        if (choosing_ && !ended_) drawChoice();
        if (paused_ && !ended_) drawPause();
        if (ended_) drawEnd();
    }

    /// A centred panel of `w` × `h`.
    Rect centred(float w, float h) {
        UIContext& ui = g_.ui;
        w = std::min(w, ui.screenW() - ui.s(40));
        h = std::min(h, ui.screenH() - ui.s(40));
        return {(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
    }

    void drawCatchUp() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{7, 11, 26, 255});
        Rect box = centred(ui.s(720), ui.s(240));
        ui.panel(box, true, 16);
        const std::string title = "Retour dans la partie";
        ui.label(title, {box.center().x, box.y + ui.s(46)}, ui.fitFont(title, FontFace::Title, 32, box.w - ui.s(48)), ui.theme().text, Align::Center,
                 FontFace::Title, VAlign::Middle);
        const uint32_t total = std::max<uint32_t>(1, link_->available());
        const float k = std::clamp(static_cast<float>(progress_.load()) / static_cast<float>(total), 0.0f, 1.0f);
        const std::string t = "Rattrapage de la partie en accéléré : " + std::to_string(static_cast<int>(k * 100.0f)) + " %";
        ui.label(t, {box.center().x, box.y + ui.s(100)}, ui.fitFont(t, FontFace::Body, 20, box.w - ui.s(48)), ui.theme().textDim, Align::Center,
                 FontFace::Body, VAlign::Middle);
        const Rect bar{box.x + ui.s(40), box.y + ui.s(140), box.w - ui.s(80), ui.s(16)};
        r.fillRoundedRect(bar, ui.s(8), Color{4, 7, 18, 255});
        r.fillRoundedRect({bar.x, bar.y, bar.w * k, bar.h}, ui.s(8), ui.theme().accent);
        const std::string note = "Vous réapparaîtrez près de l'équipe dès que votre jeu sera à jour.";
        ui.label(note, {box.center().x, box.y + ui.s(196)}, ui.fitFont(note, FontFace::Body, 16, box.w - ui.s(48)), ui.theme().textDim, Align::Center,
                 FontFace::Body, VAlign::Middle);
    }

    void drawPause() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 170});
        const bool network = setup_.mode != CoopSetup::Mode::SamePc;
        Rect box = centred(ui.s(560), ui.s(network ? 330 : 400));
        ui.panel(box, true, 16);
        ui.label("PAUSE", {box.center().x, box.y + ui.s(44)}, 40, ui.theme().text, Align::Center, FontFace::Title, VAlign::Middle);
        const std::string note = network ? "La partie continue pendant la pause : vous pouvez encore être touché." : "Toute l'équipe est en pause.";
        ui.label(note, {box.center().x, box.y + ui.s(88)}, ui.fitFont(note, FontFace::Body, 18, box.w - ui.s(48)), ui.theme().textDim,
                 Align::Center, FontFace::Body, VAlign::Middle);
        ui.pushId("coop/pause");
        float y = box.y + ui.s(124);
        const float bw = box.w - ui.s(80), bh = ui.s(58);
        UIContext::ButtonStyle bs;
        bs.primary = true;
        if (ui.button("resume", {box.x + ui.s(40), y, bw, bh}, "Reprendre", bs)) paused_ = false;
        y += bh + ui.s(14);
        if (!network) {
            if (ui.button("restart", {box.x + ui.s(40), y, bw, bh}, "Recommencer la tentative")) restart();
            y += bh + ui.s(14);
        }
        UIContext::ButtonStyle danger;
        danger.danger = true;
        if (ui.button("quit", {box.x + ui.s(40), y, bw, bh}, network ? "Quitter la partie" : "Retour au salon", danger)) backToLobby();
        ui.popId();
        if (!pauseOpenedNow_ && ui.backPressed()) paused_ = false;
        pauseOpenedNow_ = false;
    }

    void drawChoice() {
        UIContext& ui = g_.ui;
        const auto& offer = world_->powerOffer();
        if (!offer) return;
        const Player& p = world_->playerAt(offer->player);
        const PowerDefinition* def = g_.content.power(offer->powerId);
        const int count = static_cast<int>(p.powers().size());
        const float rowH = ui.s(54), gap = ui.s(10);
        Rect box = centred(ui.s(720), ui.s(190) + (count + 1) * (rowH + gap));
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 150});
        ui.panel(box, true, 16);
        const std::string who = setup_.seats[static_cast<size_t>(offer->player)].name;
        const std::string title = who + " : " + (def ? def->name : offer->powerId);
        ui.label(title, {box.center().x, box.y + ui.s(40)}, ui.fitFont(title, FontFace::Title, 30, box.w - ui.s(48)), ui.theme().accent, Align::Center,
                 FontFace::Title, VAlign::Middle);
        const std::string q = offer->conflictSlot >= 0 ? "Ce pouvoir prend la touche d'un pouvoir porté : l'échanger ?"
                                                       : "Tous les emplacements sont pris : quel pouvoir laisser ici ?";
        ui.label(q, {box.center().x, box.y + ui.s(84)}, ui.fitFont(q, FontFace::Body, 18, box.w - ui.s(48)), ui.theme().textDim, Align::Center,
                 FontFace::Body, VAlign::Middle);
        ui.pushId("coop/choice");
        float y = box.y + ui.s(116);
        for (int i = 0; i <= count; ++i) {
            const bool leave = i == count;
            std::string label;
            bool enabled = true;
            if (leave) {
                label = "Laisser " + (def ? def->name : offer->powerId) + " ici";
            } else {
                const Power& pw = *p.powers()[static_cast<size_t>(i)];
                label = "Remplacer " + pw.def().name;
                enabled = offer->conflictSlot < 0 || offer->conflictSlot == i;
            }
            UIContext::ButtonStyle bs;
            bs.enabled = enabled;
            bs.selected = i == choiceIndex_;
            if (ui.button(std::to_string(i), {box.x + ui.s(40), y, box.w - ui.s(80), rowH}, label, bs))
                pendingChoice_ = leave ? InputFrame::kChoiceLeave : static_cast<uint8_t>(i + 1);
            y += rowH + gap;
        }
        ui.popId();
    }

    /// The board of this level for this team's size (Duo or Trio): kind, id and version or hash.
    std::string boardKind() const { return setup_.communityId.empty() ? "official" : "community"; }
    std::string boardLevel() const { return setup_.communityId.empty() ? setup_.levelId : setup_.communityId; }

    /// 2.63.5: the best team of the level's Duo / Trio board, under the result.
    void loadBoard() {
        const int n = world_ ? world_->teamSize() : 2;
        const std::string label = n >= 3 ? "Classement Trio" : "Classement Duo";
        if (!g_.online.usable() || (setup_.communityId.empty() && !g_.online.fingerprints().get(g_.content, setup_.levelId))) {
            boardLine_ = label + " : indisponible hors ligne.";
            return;
        }
        boardLine_ = label + " : chargement…";
        std::string q = "/api/v1/coop/board?size=" + std::to_string(n >= 3 ? 3 : 2) + "&kind=" + boardKind() + "&level_id=" + boardLevel();
        if (!setup_.communityId.empty()) q += "&version=" + std::to_string(setup_.communityVersion);
        else q += "&level_hash=" + levelHash_;
        g_.online.get(q, OnlineService::guard(life_, [this, label](const ApiResult& r) {
                          if (!r.ok) {
                              boardLine_ = label + " : " + (r.message.empty() ? std::string("indisponible.") : r.message);
                              return;
                          }
                          const Json ranking = r.data.value("ranking", Json::array());
                          if (!ranking.is_array() || ranking.empty()) {
                              boardLine_ = label + " : aucune équipe classée pour l'instant.";
                              return;
                          }
                          std::string names;
                          for (const auto& p : ranking[0].value("players", Json::array())) names += (names.empty() ? "" : " + ") + jstr(p, "username");
                          char sc[32];
                          std::snprintf(sc, sizeof sc, "%.1f", jget<double>(ranking[0], "score", 0.0));
                          std::string s = sc;
                          for (char& c : s)
                              if (c == '.') c = ',';
                          boardLine_ = label + " : 1re équipe " + names + " · " + s + " %";
                      }),
                      false);
    }

    void drawEnd() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 190});
        const int n = world_->teamSize();
        Rect box = centred(ui.s(760), ui.s(364) + n * ui.s(46));
        ui.panel(box, true, 16);
        std::string title = !aborted_.empty() ? "Partie interrompue" : completed_ ? "Niveau terminé !" : "Toute l'équipe est à terre";
        ui.label(title, {box.center().x, box.y + ui.s(44)}, ui.fitFont(title, FontFace::Title, 38, box.w - ui.s(48)),
                 completed_ ? ui.theme().success : ui.theme().warm, Align::Center, FontFace::Title, VAlign::Middle);
        float y = box.y + ui.s(90);
        auto line = [&](const std::string& t, Color c) {
            const float fs = ui.fitFont(t, FontFace::Body, 20, box.w - ui.s(64));
            ui.label(t, {box.center().x, y}, fs, c, Align::Center, FontFace::Body, VAlign::Middle);
            y += ui.s(34);
        };
        if (!aborted_.empty()) {
            line(aborted_, ui.theme().text);
        } else {
            char pct[32];
            std::snprintf(pct, sizeof pct, "%.1f", static_cast<double>(endScore_));
            std::string score = pct;
            for (char& c : score)
                if (c == '.') c = ',';
            const int used = world_->revivesUsed();
            line("Progression de l'équipe : " + score + " % · temps " + clock(endTime_), ui.theme().text);
            line(std::to_string(used) + (used > 1 ? " réanimations utilisées" : " réanimation utilisée") + " sur " +
                     std::to_string(GameWorld::kTeamRevives) + (world_->teamIncomplete() ? " · équipe incomplète" : ""),
                 ui.theme().textDim);
        }
        // Each player: its colour, its name, how far it went.
        for (int i = 0; i < n; ++i) {
            const float route = trackers_.size() > static_cast<size_t>(i) ? trackers_[static_cast<size_t>(i)].progress().route : 0.0f;
            const std::string t = setup_.seats[static_cast<size_t>(i)].name + " · itinéraire " + std::to_string(static_cast<int>(std::floor(route * 100.0f))) + " %";
            ui.r().fillCircle({box.x + ui.s(60), y}, ui.s(7), setup_.seats[static_cast<size_t>(i)].color);
            const float fs = ui.fitFont(t, FontFace::Bold, 19, box.w - ui.s(140));
            ui.label(t, {box.x + ui.s(80), y}, fs, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            y += ui.s(46);
        }
        const std::string note = !rankNote_.empty() ? rankNote_ : "Course à la vérification : le serveur vérifie la tentative d'équipe avec le replay.";
        ui.label(note, {box.center().x, y}, ui.fitFont(note, FontFace::Body, 16, box.w - ui.s(48)), ui.theme().textDim, Align::Center, FontFace::Body,
                 VAlign::Middle);
        y += ui.s(34);
        if (!boardLine_.empty()) {
            // 2.63.5: the Duo / Trio board of this level (a community level: of its version).
            const std::string t = ui.elide(boardLine_, FontFace::Body, ui.fitFont(boardLine_, FontFace::Body, 17, box.w - ui.s(48)), box.w - ui.s(48));
            ui.label(t, {box.center().x, y}, ui.fitFont(t, FontFace::Body, 17, box.w - ui.s(48)), Color{201, 179, 255, 255}, Align::Center, FontFace::Body,
                     VAlign::Middle);
        }
        ui.pushId("coopend");
        const float gap = ui.s(16), bh = ui.s(58);
        const float bw = (box.w - ui.s(80) - gap * 2.0f) / 3.0f;
        const float by = box.bottom() - bh - ui.s(28);
        const std::string boardLabel = n >= 3 ? "Classement Trio" : "Classement Duo";
        float fs = 20.0f;
        for (const char* t : {"Rejouer en équipe", "Retour au salon"}) fs = std::min(fs, ui.fitFont(t, FontFace::Bold, 20.0f, bw - ui.s(24), 0.55f));
        fs = std::min(fs, ui.fitFont(boardLabel, FontFace::Bold, 20.0f, bw - ui.s(24), 0.55f));
        UIContext::ButtonStyle bs;
        bs.primary = true;
        bs.fontSize = fs;
        bs.enabled = setup_.mode == CoopSetup::Mode::SamePc && aborted_.empty();
        if (ui.button("again", {box.x + ui.s(40), by, bw, bh}, "Rejouer en équipe", bs)) restart();
        UIContext::ButtonStyle rs;
        rs.fontSize = fs;
        rs.enabled = g_.online.usable() && (!setup_.communityId.empty() || g_.online.fingerprints().get(g_.content, setup_.levelId));
        if (ui.button("board", {box.x + ui.s(40) + bw + gap, by, bw, bh}, boardLabel, rs))
            g_.app.pushScene(makeRaceBoard(g_, boardKind(), boardLevel(), setup_.communityVersion, setup_.communityId.empty() ? levelHash_ : std::string(),
                                           n >= 3 ? 3 : 2));
        UIContext::ButtonStyle ls;
        ls.fontSize = fs;
        if (ui.button("lobby", {box.x + ui.s(40) + (bw + gap) * 2.0f, by, bw, bh}, "Retour au salon", ls)) backToLobby();
        ui.popId();
    }

    void restart() {
        CoopSetup again = setup_;
        again.attemptId = makeUuidV4();  // a new attempt (its own place in the race)
        g_.app.replaceTop(makeCoopGame(g_, again, std::make_unique<LocalCoopLink>(static_cast<int>(again.seats.size()))), 0.3f);
    }
    void backToLobby() { g_.app.switchScene(makeCoopLobby(g_, setup_), 0.3f); }

    GameServices& g_;
    CoopSetup setup_;
    std::unique_ptr<CoopLink> link_;
    LevelData level_;
    std::unique_ptr<GameWorld> world_;
    GameRenderer renderer_;
    Hud hud_;
    std::vector<TexturePtr> suits_;
    std::vector<std::unique_ptr<LocalPad>> pads_;
    std::vector<InputScript> scripts_;  // 2.70: a capture's seats (docs/BUILDING.md)
    std::vector<std::optional<Vec2>> spawns_;
    // 2.65, one PC: each player's device, whether the team goes on without it, the pause of a
    // device missing, the free pads (A takes a place), each player's power keys.
    std::vector<LocalDevice> devices_;
    std::vector<uint8_t> dropped_;
    UnplugPause unplug_;
    bool unplugShown_ = false;
    bool unplugConfirmQuit_ = false;
    std::string unplugMsg_;
    std::map<SDL_JoystickID, std::unique_ptr<LocalPad>> spare_;
    std::vector<SDL_JoystickID> reserved_;
    std::vector<PowerKeyMap> seatKeys_;
    std::vector<std::string> batteryNotes_;
    float batteryTimer_ = 0.0f;
    std::vector<RaceTracker> trackers_;
    std::future<LevelProgressTrack> trackFuture_;
    std::future<std::vector<Vec2>> routeFuture_;
    PowerKeyMap powerKeys_;
    ReplayRecorder recorder_;  // the team replay (format 5)
    std::shared_ptr<ReplayData> replay_;
    std::vector<InputFrame> frame_;  // the team frame of the step (kept: no allocation per step)
    std::vector<std::pair<int, InputFrame>> localFrames_;
    std::thread worker_;  // the catch-up after a reconnection
    std::atomic<bool> stopWorker_{false}, workerDone_{false};
    std::atomic<uint32_t> progress_{0};
    bool catching_ = false;
    uint32_t tick_ = 0;
    uint32_t sentUpTo_ = UINT32_MAX;
    float waiting_ = 0.0f;
    float skipHold_ = 0.0f;
    bool mounted_ = false, loadFailed_ = false;
    std::string loadError_;
    bool paused_ = false;
    bool pauseOpenedNow_ = false;  // the pause opened this frame: its own key press must not close it
    bool choosing_ = false;
    int choiceIndex_ = 0;
    uint8_t pendingChoice_ = 0;
    bool ended_ = false, completed_ = false;
    float endTime_ = 0.0f, endScore_ = 0.0f;
    std::string aborted_;
    std::string rankNote_;
    std::string levelHash_;  // the fingerprint the team replay carries
    std::string boardLine_;  // 2.63.5: the best team of the level's Duo / Trio board
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace

std::unique_ptr<Scene> makeCoopGame(GameServices& g, CoopSetup setup, std::unique_ptr<CoopLink> link) {
    return std::make_unique<CoopGameScene>(g, std::move(setup), std::move(link));
}

std::unique_ptr<Scene> makeCoopCapture(GameServices& g, CoopCapture capture) {
    CoopSetup setup;
    setup.mode = CoopSetup::Mode::SamePc;
    setup.levelId = capture.levelId;
    const int n = std::clamp(capture.players, 2, 3);
    for (int i = 0; i < n; ++i) {
        CoopSeat seat;
        seat.name = "Joueur " + std::to_string(i + 1);
        // Player 1 keeps the keyboard when it has no script; the others play their scripts only.
        seat.device = i == 0 ? CoopSeat::Device::Keyboard : CoopSeat::Device::Remote;
        seat.color = coopColor(i);
        setup.seats.push_back(seat);
    }
    auto scene = std::make_unique<CoopGameScene>(g, std::move(setup), std::make_unique<LocalCoopLink>(n));
    scene->setCapture(capture);
    return scene;
}

}  // namespace cosmic
