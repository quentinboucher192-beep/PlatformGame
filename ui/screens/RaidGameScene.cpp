// 2.65: the combat of a raid instance (docs/RAIDS.md, maquette « Combat »): up to 8 players of a
// group in lockstep through the instance's relay (the CoopRoom in raid mode), from zone to zone
// of the branch « BRAS DROIT ». The simulation is raid::RaidRun; this screen only connects, sends
// this computer's inputs, steps the frames the relay gives (with the branch's state of each step),
// sends the run's reports to the server (its consensus decides) and draws the raid's HUD:
//
// * the minimap at the top (this player, the group, the enemies, the objectives, the passages
//   and doors, the magma flow);
// * the group panel at the left (8 rows: name, hearts, state; the revivals of the zone);
// * the zone's objectives at the right, with their progress (generators, the pair of bellows),
//   the alerts of the group and of the branch under them;
// * the danger (the magma flow announced, then on) in the middle, the network at the bottom.
//
// Every text is measured into the room left by what is placed before it (raidui::fit / wrap):
// nothing overlaps. The pause never stops the group (lockstep): it only hides the game.
//
// 2.68 (phase 3): the gigaboss PYROCLAST in the Main: its vents drawn over the arena (closed,
// open, announced, throwing magma, the hold of each), its carapace, its objectives (vents k / N,
// the weak point bare, allies to revive), this player's contributions at the bottom right, the
// reports of its phases and of the contributions' tables, and the card of its fall (« Résultats
// du raid »). An administrator's test (ADMINISTRATION › Raids › « Tester Asterion »): the same
// screen, solo and local (no relay), never reported, badge « TEST ADMIN · non classé ».
//
// 2.74 (phase 7): a link to the relay that dies while the group plays (no END: a network cut, the
// relay restarted) is connected again by the screen itself during the 60 s the relay keeps the seat
// (raid::Reconnect: at once, then 1, 2, 4, 8 s apart); each attempt is a return (the history
// replayed off the main thread, then REJOIN). The world stays shown under a « Liaison perdue »
// card meanwhile; after 60 s the seat is free and the screen says so.
// 2.72 (docs/RAIDS.md §15): the HUD placed by the player (Paramètres › HUD › « Raids »:
// RaidHudLayout, drawn by ui/online/RaidHud), the minimap in textures and its large map
// (Action::RaidMap: M, View / Back; display only, the group keeps playing), the off-screen arrows
// of the solo levels (enemies met, the ambush's), and the test played as a real instance
// (raid::TestSession: the branch's state kept from zone to zone as RAID_STATE gives it, the
// simulated players « Joueur simulé N » on their seats, seen, named, in their colours).
// 2.75 (docs/RAIDS.md §19): « Ce qu'il reste à faire » (RaidHudElement::Remaining: the zone's
// requirements ticked from the validated state, the branch's zones still to finish, those to take
// back after a group fell), what each zone misses on the large map, and a zone finished: a card with
// its sound for every player of the instance at the same step, its « Tableau des contributions »
// (raid::RaidRun::finishes, counted in the simulation), reported to the server like the other tables.
// 2.75.5 (docs/RAIDS.md §21): no card opens by itself during the game any more. A zone finished, the
// group eliminated, a zone taken back, a gigaboss fallen, a link of the titan: a short notification
// under the menu's button (ui/online/RaidMenu, never a key nor the focus taken), and the raid's menu
// the player opens and closes (Action::RaidMenu) with what is left, the zones' tables, the journal,
// the group and the titan; this player's inputs neutral while it is open, as in the pause.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <memory>
#include <thread>

#include "engine/input/InputScript.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/coop/CoopTransport.h"
#include "game/coop/NetCoopLink.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/Hud.h"
#include "game/online/Clans.h"
#include "game/powers/PowerKeys.h"
#include "game/raid/RaidRun.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "ui/screens/AsterionFilm.h"
#include "game/raid/RaidTest.h"
#include "game/raid/RaidSimulation.h"
#include "game/online/AsterionBody.h"
#include "ui/widgets/AsterionFigure.h"
#include "game/raid/ZoneGoals.h"
#include "game/raid/ArenaCamera.h"
#include "game/raid/Reconnect.h"
#include "game/gameplay/EnemyArrows.h"
#include "game/gameplay/EnemyArrowsDraw.h"
#include "game/gameplay/entities/Entities.h"
#include "engine/resources/ResourceManager.h"
#include "saves/Settings.h"
#include "ui/online/RaidHud.h"
#include "ui/online/ContribTable.h"
#include "ui/online/RaidMenu.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/Glyphs.h"
#include "ui/screens/DownedView.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/PowerChoiceScene.h"
#include "ui/screens/RaidSpectator.h"

namespace cosmic {

namespace {

using namespace raidui;

const Color kMe{255, 210, 122, 255};
const Color kMate{124, 240, 240, 255};
const Color kEnemy{255, 106, 106, 255};
const Color kViolet{201, 168, 255, 255};
const Color kDanger{255, 74, 74, 255};

/// Eight colours of the group (names over the players): this computer's player wears gold.
Color seatColor(int seat, int local) {
    static const Color kSeats[8] = {{62, 230, 214, 255},  {255, 122, 184, 255}, {124, 240, 168, 255}, {159, 216, 255, 255},
                                    {255, 176, 74, 255},  {201, 168, 255, 255}, {255, 236, 140, 255}, {255, 138, 138, 255}};
    return seat == local ? kMe : kSeats[std::clamp(seat, 0, 7)];
}

std::string hex16(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

class RaidGameScene : public Scene, public PowerChoiceHost {
public:
    RaidGameScene(GameServices& g, RaidEntry entry) : g_(g), entry_(std::move(entry)) {}
    /// 2.68: an administrator's test (solo, local, never reported).
    RaidGameScene(GameServices& g, RaidTestSetup t) : g_(g), test_(true), testSetup_(std::move(t)) {
        entry_.clanTag = "TEST";
        entry_.group = 1;
    }
    /// 2.77: « Simuler un raid complet » (local, every branch and the heart, never anything sent).
    RaidGameScene(GameServices& g, RaidSimSetup s) : g_(g), test_(true) {
        simSetup_ = s;
        simMode_ = true;
        entry_.clanTag = "TEST";
        entry_.group = 1;
        testSetup_.capture = s.capture;
        testSetup_.invincible = s.invincible;
    }

    ~RaidGameScene() override {
        stopWorker_ = true;
        if (worker_.joinable()) worker_.join();
        if (link_) link_->close();
    }

    bool holdsNotifications() const override { return true; }
    // 2.75: the armory's power choice (PowerChoiceScene over the combat, which never stops).
    GameWorld* world() override { return run_ && !catching_ ? &run_->world() : nullptr; }
    const PowerKeyMap& powerKeys() const override { return powerKeys_; }
    void answerPowerOffer(uint8_t choice) override { pendingChoice_ = choice; }
    // 2.75.5: the pointer comes back with the raid's menu, and while the mouse moves (the HUD's button).
    bool hidesCursor() const override {
        return run_ && !catching_ && !connecting_ && !paused_ && !ended_ && !overlay_.wantsPointer() && clock_ - pointerAt_ > 2.5f && !bigMap_ &&
               !reconnect_.active() && !spec_.on && simPanel_ < 0;
    }

    void onEnter() override {
        branch_ = raid::Branch::builtin(entry_.branch);
        if (simMode_) {
            beginSim();
            return;
        }
        if (test_) {
            beginTest();
            return;
        }
        if (!branch_) {
            fail_ = "Les zones du raid sont introuvables dans les données du jeu : réinstallez le jeu.";
            return;
        }
        g_.ui.setFocus("raidgame/start");
        connect();
    }

    /// A link to the instance's relay (a fresh access token each time). 2.74: also each attempt of
    /// an automatic reconnection (the catch-up of the previous link stopped first: it reads that link).
    void connect() {
        connecting_ = true;
        g_.online.freshAccess([this, life = std::weak_ptr<int>(life_)](const std::string& token, const std::string& url) {
            if (life.expired()) return;
            connecting_ = false;
            if (token.empty() || url.empty()) {
                if (reconnect_.active()) reconnect_.failed(clock_);  // offline for now: the next attempt
                else fail_ = "Connectez-vous à Cosmic Online pour combattre dans le raid.";
                return;
            }
            stopCatchUp();
            if (link_) link_->close();
            // 2.75: a spectator's socket only watches (?watch=1: never a seat; the server decides anyway).
            const bool watch = entry_.spectator || watchSeat_ >= 0;
            link_ = std::make_unique<NetCoopLink>(makeWebSocketTransport(url + "/api/v1/raids/instances/" + entry_.instanceId + "/socket" + (watch ? "?watch=1" : ""), token),
                                                  g_.online.username(), 255, g_.online.playerId());
            link_->start();
            fresh_ = reconnect_.active();
        });
    }

    /// 2.74: stops a catch-up under way (its thread reads the link and the world).
    void stopCatchUp() {
        if (worker_.joinable()) {
            stopWorker_ = true;
            worker_.join();
        }
        stopWorker_ = false;
        workerDone_ = false;
        if (catching_) {
            // An unfinished catch-up: its world was never shown; the next GO makes a new one.
            catching_ = false;
            run_ = nullptr;
            ownRun_.reset();
        }
    }

    /// 2.74: the link died while the group plays (no END): connect again by itself while the relay
    /// keeps the seat (60 s), each attempt a return (catch-up, REJOIN); after that, say it is over.
    void updateReconnect() {
        if (reconnect_.expired(clock_)) {
            reconnect_.back();
            stopCatchUp();
            ended_ = true;
            endReason_ = raid::Reconnect::gaveUp();
            g_.ui.setFocus("raidgame/end/map");
            return;
        }
        if (fresh_ && link_) {
            if (link_->go()) {
                begin();  // back in the instance: the history, then its return
                return;
            }
            if (!link_->endReason().empty() && !link_->connectionLost()) {
                // The relay refused this return (its seat was freed, the instance is full or over).
                reconnect_.back();
                fresh_ = false;
                ended_ = true;
                endReason_ = link_->endReason();
                g_.ui.setFocus("raidgame/end/map");
                return;
            }
            if (!link_->error().empty() || link_->connectionLost()) {
                reconnect_.failed(clock_);
                fresh_ = false;
            }
        }
        if (!connecting_ && !fresh_ && reconnect_.due(clock_)) connect();
    }

    void onExit() override {
        g_.app.input().clearPowerSlotKeys();
        g_.app.input().setBindings(g_.settings.input);
    }

    // ------------------------------------------------------------------ the game

    void begin() {
        const auto& go = *link_->go();
        if (go.level != branch_->levelId() || go.hash != branch_->levelHash()) {
            fail_ = "Ce raid se joue avec une autre version de la branche : mettez le jeu à jour.";
            return;
        }
        if (reconnect_.active()) {
            // 2.74: back after a lost link: a new world, the whole history replayed (never the old one).
            reconnect_.back();
            fresh_ = false;
            rejoining_ = true;
            stopCatchUp();
            tick_ = 0;
            sentUpTo_ = UINT32_MAX;
            serial_ = -1;
            waiting_ = 0.0f;
            progress_ = 0;
            note("RÉSEAU · Liaison rétablie : retour dans l'instance", kGreen);
        }
        local_ = link_->seat();
        ResourceManager& res = g_.app.resources();
        WorldServices ws{res, g_.app.audio(), g_.content, &g_.app.input(), g_.settings.graphics.screenShake,
                         g_.settings.graphics.particleQuality == 0 ? 0.4f : g_.settings.graphics.particleQuality == 1 ? 0.7f : 1.0f};
        ownRun_ = std::make_unique<raid::RaidRun>(ws, *branch_, go.players);
        run_ = ownRun_.get();
        watchedRun_ = nullptr;  // 2.75.5: a new run: what it holds once caught up is no news
        run_->setCameraFollow(watchSeat_ >= 0 ? watchSeat_ : local_);  // 2.75: a spectator follows the player watched
        // 2.75: the armory first (each player picks their powers), else a group taking a wiped zone again starts there.
        if (!run_->begin(link_->raidStateAt(0), entry_.startZone > 0 ? entry_.startZone : raid::RaidRun::kArmory)) {
            fail_ = "Impossible de charger la première zone du raid.";
            run_ = nullptr;
            ownRun_.reset();
            return;
        }
        hud_.setRaidView(true);
        if (link_->joinedLate()) {
            // A late join or a return: the steps given so far, off the main thread, quiet.
            catching_ = true;
            run_->setQuiet(true);
            worker_ = std::thread([this] { catchUp(); });
        } else {
            zoneReady();
        }
    }

    /// 2.68: an administrator's test: the branch and zone chosen, alone on this computer; 2.72: as a
    /// real instance (raid::TestSession): the simulated players on seats 1 .. players − 1 (the others
    /// vacant), the branch's state kept from zone to zone as RAID_STATE gives it, never reported.
    void beginTest() {
        branch_ = nullptr;
        for (const raid::Branch* b : raid::Branch::all())
            if (b->id == testSetup_.branch || (testSetup_.capture && b->part == testSetup_.branch)) branch_ = b;  // 2.70: « bras_droit » too
        // 2.70: a capture for the documentation (--raid-test, headless only) needs no account.
        if (!g_.online.isAdmin() && !testSetup_.capture) {
            fail_ = "Réservé aux administrateurs connectés à Cosmic Online.";
            return;
        }
        if (!branch_ || !branch_->zone(testSetup_.zone == raid::RaidRun::kArmory ? 0 : testSetup_.zone)) {  // 2.75: the armory too
            fail_ = "Cette branche ou cette zone est introuvable dans les données du jeu.";
            return;
        }
        local_ = 0;
        ResourceManager& res = g_.app.resources();
        WorldServices ws{res, g_.app.audio(), g_.content, &g_.app.input(), g_.settings.graphics.screenShake,
                         g_.settings.graphics.particleQuality == 0 ? 0.4f : g_.settings.graphics.particleQuality == 1 ? 0.7f : 1.0f};
        raid::RaidRun::Test t;
        t.on = true;
        t.invincible = testSetup_.invincible;
        t.players = testSetup_.players;
        t.startPhase = testSetup_.startPhase;
        session_ = std::make_unique<raid::TestSession>(ws, *branch_, t);
        session_->setSkill(static_cast<raid::BotSkill>(std::clamp(testSetup_.skill, 0, 2)));  // 2.77
        run_ = &session_->run();
        run_->setCameraFollow(0);
        // 2.74 (phase 6): the heart's test: the parts chosen as fallen (their links on), the relics.
        uint32_t links = 0;
        for (const raid::EffectInfo& e : branch_->effects)
            for (int p = 0; p < raid::AsterionHeartRoutine::PartCount; ++p)
                if ((testSetup_.parts >> p) & 1u && e.id == std::string("coeur_") + raid::AsterionHeartRoutine::partId(p) && e.bit >= 0 && e.bit < 32)
                    links |= 1u << e.bit;
        raid::HeartShared heart;
        heart.relics = static_cast<uint8_t>(std::clamp(testSetup_.relics, 0, 7));
        session_->setHeart(links, heart);
        if (!session_->begin(testSetup_.zone)) {
            fail_ = "Impossible de charger la zone du test.";
            run_ = nullptr;
            session_.reset();
            return;
        }
        // 2.70, a capture: each seat's script, and where it appears.
        scripts_.assign(raid::kMaxInstancePlayers, InputScript{});
        for (size_t k = 0; k < testSetup_.scripts.size() && k < scripts_.size(); ++k) scripts_[k].parse(testSetup_.scripts[k]);
        GameWorld& w = run_->world();
        for (size_t k = 0; k < testSetup_.spawns.size() && static_cast<int>(k) < w.teamSize(); ++k)
            if (testSetup_.spawns[k]) w.restoreMember(static_cast<int>(k), w.carryOf(static_cast<int>(k)), *testSetup_.spawns[k]);
        hud_.setRaidView(true);
        zoneReady();
    }

    /// 2.68: one step of a test: this computer's input on seat 0 (neutral while paused: the pause
    /// never stops the world); 2.72: the simulated players on theirs (TestSession, playing on during
    /// the pause and the large map), the free seats vacant.
    void stepTest() {
        const PlayerInput in = g_.app.input().consumePlayerInput();
        // 2.72: a film over the combat holds this player only; 2.75.5: the raid's menu too (never a notification).
        InputFrame f = overlay_.frame(!scripts_.empty() && scripts_[0].active() ? scripts_[0].next() : in, paused_, g_.app.topScene() != this, false);
        const auto& offer = run_->world().powerOffer();
        if (offer && offer->player == 0 && (!scripts_.empty() && scripts_[0].active())) f.choice = InputFrame::kChoiceLeave;  // a capture
        else if (offer && offer->player == 0 && pendingChoice_ != 0) {  // 2.75: the armory's choice window
            f.choice = pendingChoice_;
            pendingChoice_ = 0;
        }
        // 2.70, a capture: a seat with a script plays it (in place of its simulated player, 2.72).
        std::vector<std::pair<int, InputFrame>> scripted;
        for (size_t k = 1; k < scripts_.size(); ++k)
            if (scripts_[k].active()) {
                InputFrame sf = InputFrame::fromPlayerInput(scripts_[k].next());
                if (offer && offer->player == static_cast<int>(k)) sf.choice = InputFrame::kChoiceLeave;
                scripted.push_back({static_cast<int>(k), sf});
            }
        session_->step(f, scripted);  // never reports anything
        ++tick_;
        if (const int z = run_->zoneFinishedNow()) zoneDone(z);  // 2.75
        if (testSetup_.openContrib && !contribShown_ && !run_->finishes().empty()) {
            // 2.78, a capture (--raid-contrib): the table over the menu, as the player would open it.
            contribShown_ = true;
            overlay_.menu.open(raidmenu::Section::Zones);
            contribOpen_ = true;
            contribView_ = contrib::View{};
        }
        if (run_->wipedNow()) groupWiped();  // 2.75.1
        if (run_->gigaboss().fellNow()) gigabossFell();  // 2.75.5: a notification (no results: a test is never reported)
        if (run_->heartWonNow()) playVictory();  // 2.74: a test's fall is its victory (never reported)
        watchBranch(true);  // 2.75.5
        if (run_->zoneSerial() != serial_) zoneReady();
    }

    /// 2.74 (phase 6): the film of Asterion's fall, over the combat (which keeps running: neutral inputs).
    void playVictory() {
        // 2.75.5: the world's event in the journal (the film, the end of the raid, stays as it was).
        notify("MONDE · Asterion est tombé", "Le Cœur a cédé sous les coups de tous les groupes.", kGold, raidmenu::Section::Titan, nullptr);
        // 2.81: then « Après Asterion », with the clans of the fall at the server's time (a test: the generic text)
        asterionfilm::playFall(g_, test_ ? std::string() : entry_.eventId, true);
    }

    void catchUp() {
        std::vector<InputFrame> team;
        while (!stopWorker_) {
            if (link_->frameFor(tick_, team)) {
                run_->step(team.data(), static_cast<int>(team.size()), link_->raidStateAt(tick_), raid::HeartShared::unpack(link_->raidHeartAt(tick_)));
                ++tick_;
                progress_ = tick_;
                continue;
            }
            if (tick_ + 2 >= link_->available()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        workerDone_ = true;
    }

    void finishCatchUp() {
        if (worker_.joinable()) worker_.join();
        catching_ = false;
        rejoining_ = false;
        run_->setQuiet(false);
        g_.app.resources().finishDeferred();
        run_->takeReports();  // what happened before this game came: the others reported it
        link_->catchupDone(tick_);
        zoneReady();
    }

    /// A world was loaded (the start, a passage, the zone again): its renderer and looks.
    void zoneReady() {
        GameWorld& w = run_->world();
        serial_ = run_->zoneSerial();
        arenaCam_.reset();  // 2.68: the arena's intro plays again in a new world
        blend_.stop();      // 2.77.2: a new world: no move from the last one
        minimap_.invalidate();  // 2.72: its terrain baked again
        metEnemies_.reset();    // 2.72: nobody met yet in this world (the off-screen arrows)
        mapPan_ = 0.0f;
        renderer_.build(g_.app.renderer(), g_.app.resources(), w);
        std::vector<Color> colors;
        for (int i = 0; i < w.teamSize(); ++i) colors.push_back(seatColor(i, local_));
        renderer_.setTeamColors(colors);
        // 2.75: this player opaque (a spectator: the player watched), the others semi-transparent.
        const int opaque = local_ >= 0 ? local_ : watchSeat_;
        renderer_.setOpaquePlayers(opaque >= 0 && opaque < 32 ? 1u << opaque : 0u);
        if (local_ >= 0 && local_ < w.teamSize()) {
            suit_ = g_.suitSheet(g_.cosmeticLook("suit"));
            w.playerAt(local_).skin = suit_ ? suit_.get() : nullptr;
        }
        powerKeys_ = levelPowerKeys(w.level(), g_.content, g_.settings.input, &g_.powerKeyOverrides);
        g_.app.input().consumePlayerInput();
    }

    void fixedUpdate(float dt) override {
        if (test_) {
            // The pause never stops the world, not even in a test: only this player's inputs
            // are held (neutral frame), the boss and the enemies keep going and can still hit.
            if (sim_) stepSim();
            else if (run_) stepTest();
            return;
        }
        if (!run_ || catching_ || ended_) {
            if (catching_ && workerDone_) finishCatchUp();
            return;
        }
        if (reconnect_.active()) return;  // 2.74: no relay for now: the world waits under its card
        if (entry_.spectator && !run_ && link_ && link_->watcher() && !link_->go()) {
            // 2.75: a spectator whose group's game is over (nobody left in it): the first player of the branch.
            link_->update(dt);
            spectatorWait_ += dt;
            if (spectatorWait_ > 4.0f) {
                if (ownInstance_.empty()) ownInstance_ = entry_.instanceId;
                if (clock_ - spec_.fetchedAt > 10.0f) fetchTargets();
                if (!spec_.targets.empty()) {
                    spectatorWait_ = 0.0f;
                    spec_.on = true;
                    spec_.index = -1;
                    watchStep(+1);
                }
            }
            return;
        }
        int steps = 1;
        if (link_->available() > tick_ + static_cast<uint32_t>(link_->inputDelay() + 6)) steps = 4;
        // 2.78: several zones may be simulated at once (one world per zone a player is in): a catch-up of several
        // steps stops at the image's budget (the next image goes on), so the main thread never waits on it.
        const auto frameStart = std::chrono::steady_clock::now();
        for (int k = 0; k < steps && !ended_; ++k) {
            if (k > 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count() > kOnlineBudgetMs) break;
            sendLocal();
            if (!link_->frameFor(tick_, frame_)) {
                waiting_ += dt;
                break;
            }
            waiting_ = 0.0f;
            run_->step(frame_.data(), static_cast<int>(frame_.size()), link_->raidStateAt(tick_), raid::HeartShared::unpack(link_->raidHeartAt(tick_)));
            ++tick_;
            const bool inPlay = !link_->joinedLate() || tick_ >= link_->rejoinAt();  // what happened before this game came: not told
            if (run_->heartWonNow() && inPlay) playVictory();  // 2.72: the validated victory
            if (run_->gigaboss().fellNow() && inPlay) gigabossFell();  // 2.75.5: its notification, « Résultats du raid » in the menu
            if (const int z = run_->zoneFinishedNow(); z && inPlay) zoneDone(z);  // 2.75
            if (run_->wipedNow() && inPlay) groupWiped();  // 2.75.1: every computer, this step
            watchBranch(inPlay);  // 2.75.5: another zone finished, a link of the titan on
            if (run_->zoneSerial() != serial_) zoneReady();
            sendReports();
        }
    }

    void sendLocal() {
        if (link_->watcher()) return;  // 2.75: a spectator never plays
        const uint32_t target = tick_ + static_cast<uint32_t>(link_->inputDelay());
        if (sentUpTo_ != UINT32_MAX && target <= sentUpTo_) return;
        sentUpTo_ = target;
        // 2.72: a film over the combat holds this player only; 2.75: the group wiped: neutral frames (the relay
        // goes on for the others); 2.75.5: the raid's menu the player opened, as the pause (never a notification).
        InputFrame f = overlay_.frame(g_.app.input().consumePlayerInput(), paused_, g_.app.topScene() != this, run_->wiped());
        // A power offered to this player (2.75: the armory's tablets): the answer of its choice window,
        // through the frame like every choice (the replays stay exact).
        if (const auto& offer = run_->world().powerOffer(); offer && offer->player == local_ && pendingChoice_ != 0) {
            f.choice = pendingChoice_;
            pendingChoice_ = 0;
        }
        frames_.assign(1, {local_, f});
        link_->pushLocal(target, frames_);
    }

    void sendReports() {
        if (test_) return;  // 2.68: an administrator's test never reports anything
        if (link_->watcher()) {  // 2.75: a spectator never reports
            run_->takeReports();
            return;
        }
        for (const raid::Report& r : run_->takeReports()) {
            if (link_->joinedLate() && tick_ < link_->rejoinAt()) continue;  // not in play yet
            const char* kind = r.kind == raid::Report::Kind::Objective ? "objective"
                               : r.kind == raid::Report::Kind::Boss    ? "boss"
                               : r.kind == raid::Report::Kind::Result  ? "result"
                               : r.kind == raid::Report::Kind::Heart   ? "heart"
                               : r.kind == raid::Report::Kind::ZoneTable ? "zone_table"
                               : r.kind == raid::Report::Kind::Wipe      ? "wipe"  // 2.75
                               : r.kind == raid::Report::Kind::Zones     ? "zones"  // 2.78
                                                                       : "zone";
            Json body{{"kind", kind}, {"zone", r.zone}, {"tick", r.tick}, {"hash", hex16(r.hash)}};
            if (r.kind == raid::Report::Kind::Objective) body["objective"] = r.objective;
            if (r.kind == raid::Report::Kind::Boss) body["phase"] = r.phase;
            if (r.kind == raid::Report::Kind::Heart) body["damage"] = r.damage;  // 2.72: this instance's part of the shared health
            // 2.78: where each player is (each one changes zone alone), with the wipe too.
            if ((r.kind == raid::Report::Kind::Zones || r.kind == raid::Report::Kind::Wipe) && !r.seats.empty()) body["seats"] = r.seats;
            if (r.kind == raid::Report::Kind::Result) {
                // 2.68: the players' contributions (the same table on every computer of the instance).
                Json rows = Json::array();
                for (const raid::Contribution& c : r.records)
                    rows.push_back(Json{{"seat", c.seat},
                                        {"from", c.from},
                                        {"damage", c.damage},
                                        {"boss_damage", c.bossDamage},
                                        {"avoided", c.avoided},
                                        {"blocks", c.blocks},
                                        {"rescues", c.rescues},
                                        {"vents", c.vents},
                                        {"weak_points", c.weakPoints},
                                        {"objectives", c.objectives},
                                        {"ticks", c.ticks},
                                        {"gigaboss", c.gigaboss ? 1 : 0}});
                body["records"] = rows;
            }
            if (r.kind == raid::Report::Kind::ZoneTable) {
                // 2.75: the zone's « Tableau des contributions » (the same on every computer of the instance).
                Json rows = Json::array();
                for (const raid::ZoneContribution& c : r.zoneRecords)
                    rows.push_back(Json{{"seat", c.seat},
                                        {"from", c.from},
                                        {"boss_damage", c.bossDamage},
                                        {"damage", c.damage},
                                        {"support", c.support},
                                        {"mechanisms", c.mechanisms},
                                        {"objectives", c.objectives},
                                        // 2.78: the mini-bosses, the deaths, the revivals done and received.
                                        {"mini_damage", c.miniDamage},
                                        {"deaths", c.deaths},
                                        {"revives", c.revives},
                                        {"revived", c.revived}});
                body["records"] = rows;
            }
            const bool objective = r.kind == raid::Report::Kind::Objective;
            const std::string label = objective && branch_->objective(r.objective) ? branch_->objective(r.objective)->label : "";
            g_.online.send("POST", "/api/v1/raids/instances/" + entry_.instanceId + "/reports", body,
                           OnlineService::guard(life_, [this, objective, label](const ApiResult& res) {
                               if (!objective) return;
                               const std::string st = res.ok ? jstr(res.data, "status") : "";
                               if (st == "accepted") note("SERVEUR · Validé pour la branche : " + label, kGreen);
                               else if (st == "rejected") note("SERVEUR · Refusé : " + jstr(res.data, "reason"), kDanger);
                               else if (!res.ok) note("SERVEUR · Rapport non envoyé : " + res.message, kDanger);
                           }));
        }
    }

    void note(std::string text, Color c) {
        notes_.push_back({std::move(text), c, clock_});
        if (notes_.size() > 6) notes_.erase(notes_.begin());
    }

    void update(float dt) override {
        clock_ += dt;
        overlay_.notices.update(clock_);  // 2.75.5: each notification goes by itself
        // 2.77.4: the pointer stays hidden while playing (the mouse can aim, click a power): it comes
        // back only when the mouse moves to the HUD's menu button (a mouse-only click zone), or while
        // watching a group of the full raid's simulation (its buttons).
        if (g_.app.input().mouseMovedThisFrame() && pointerWanted()) pointerAt_ = clock_;
        if (test_) {
            if (sim_) updateSim();
            updateGame(dt);
            return;
        }
        if (link_) link_->update(dt);
        if (!reconnect_.active() && link_ && !ended_ && link_->connectionLost() && run_) {
            reconnect_.lost(clock_);
            g_.ui.setFocus("raidgame/reconnect/map");
        }
        if (reconnect_.active()) updateReconnect();
        if (run_ && clock_ >= branchAt_) fetchBranch();  // 2.75: the zones to take back (a group fell there)
        if (link_ && !run_ && fail_.empty() && !reconnect_.active() && link_->go()) begin();
        if (link_ && !link_->error().empty() && fail_.empty() && !run_ && !reconnect_.active()) fail_ = link_->error();
        if (link_ && link_->connectionLost() && fail_.empty() && !run_ && !reconnect_.active()) fail_ = link_->endReason();  // lost in the lobby
        if (link_ && !link_->endReason().empty() && !ended_ && run_ && !reconnect_.active()) {
            ended_ = true;
            endReason_ = link_->endReason();
            g_.ui.setFocus("raidgame/end/map");
        }
        updateGame(dt);
    }

    void updateGame(float dt) {
        if (!run_ || catching_) return;
        GameWorld& w = run_->world();
        w.update(dt);
        hud_.update(w, dt);
        if (local_ >= 0 && local_ < w.teamSize()) applyPowerSlotKeys(g_.app.input(), w.playerAt(local_), powerKeys_);
        Hud::TeamInfo info;
        for (int i = 0; i < w.teamSize(); ++i) {
            info.names.push_back(seatName(i));
            info.colors.push_back(seatColor(i, local_));
        }
        info.promptPlayer = local_;
        hud_.setTeam(info);
        updateArenaCamera(w, dt);
        if (!sim_) updateSpectator(w, dt);  // 2.75 (the full raid's simulation has its own: updateSim)
        updateDowned(w);                    // 2.77.2: this player down: their soul, the others, the banner's keys
        // 2.75: a power tablet of the armory with every slot taken: which power to leave (the world goes on).
        if (const auto& offer = w.powerOffer(); offer && offer->player == local_ && pendingChoice_ == 0 && !paused_ && !ended_ &&
                                                g_.app.topScene() == this && !(test_ && !scripts_.empty() && scripts_[0].active()))
            g_.app.pushScene(std::make_unique<PowerChoiceScene>(g_, *this));
        // 2.75.5: the raid's menu, opened and closed by the player (its key, the HUD's button), as the pause:
        // the game goes on, this player's inputs are neutral while it is open; Échap or B close it.
        const bool menuWasOpen = overlay_.menu.isOpen();
        const bool menuAllowed = !ended_ && !paused_ && !reconnect_.active() && g_.app.topScene() == this && simPanel_ < 0;
        bool menuBack = false;
        if (contribOpen_ && overlay_.menu.isOpen() && menuAllowed) {
            // 2.78: the table's keys while it is open over the menu (Échap / B back to the menu, never the pause).
            if (contribView_.handleKeys(g_.app.input(), static_cast<int>(run_ ? run_->finishes().size() : 0))) contribOpen_ = false;
            menuBack = true;
        } else {
            contribOpen_ = false;
            menuBack = overlay_.menu.handleKeys(g_.app.input(), menuAllowed, menuSectionNow());
        }
        if (overlay_.menu.isOpen()) bigMap_ = false;
        if (!menuWasOpen && !menuBack && !ended_ && g_.app.topScene() == this && simPanel_ < 0 && !panelJustClosed_ &&
            g_.app.input().menuPressed(Action::Pause)) {  // 2.72: not under the victory film
            paused_ = !paused_;
            pauseOpenedNow_ = paused_;  // the same press (Échap = pause and back) must not close it
            g_.ui.setFocus("raidgame/pause/resume");
        }
        // 2.72: the large map (display only: the group, and this player, keep playing).
        if (!ended_ && !paused_ && simPanel_ < 0 && g_.app.input().menuPressed(Action::RaidMap)) {
            bigMap_ = !bigMap_;
            mapPan_ = 0.0f;
            if (bigMap_) overlay_.menu.close();
        }
        if (paused_ || ended_) bigMap_ = false;
    }

    /// 2.75: the branch's map as the server knows it, every 15 s: the zones a group fell in, to take back
    /// (`retake_by` of each zone: that group). Display only: the panel and the large map say it.
    void fetchBranch() {
        branchAt_ = clock_ + 15.0f;
        if (entry_.eventId.empty()) return;
        g_.online.get("/api/v1/raids/events/" + entry_.eventId + "/branches/" + entry_.branch, OnlineService::guard(life_, [this](const ApiResult& r) {
                          if (!r.ok) return;
                          std::map<int, std::string> out;
                          if (const Json zones = r.data.value("zones", Json::array()); zones.is_array())
                              for (const Json& z : zones)
                                  if (const std::string by = jstr(z, "retake_by"); !by.empty() && !jget<bool>(z, "finished", false))
                                      out[jget<int>(z, "id", 0)] = by;
                          // 2.75.1: a zone to take back now finished: a reinforcement group took it back (told once);
                          // 2.75.5: a notification, as a zone newly to take back (not those known at the first reading).
                          if (const Json zones = r.data.value("zones", Json::array()); zones.is_array())
                              for (const Json& z : zones) {
                                  const int id = jget<int>(z, "id", 0);
                                  if (retake_.count(id) && jget<bool>(z, "finished", false))
                                      notify("ZONE REPRISE · " + jstr(z, "name"), "Terminée par le groupe de renfort " + jstr(z, "finished_by", "?") + ".", kGreen,
                                             raidmenu::Section::Zones, "sounds/boss_phase_clear.wav");
                                  else if (branchRead_ && !retake_.count(id) && out.count(id) && !(run_ && run_->wipedZone() == id))  // own wipe: told already
                                      notify("ZONE À REPRENDRE · " + jstr(z, "name"), out[id] + " est tombé : un groupe qui a fini sa zone peut la reprendre.", kDanger,
                                             raidmenu::Section::Remaining, "sounds/enemy_alert.wav");
                              }
                          branchRead_ = true;
                          retake_ = std::move(out);
                      }),
                      false);
    }

    std::string seatName(int seat) const {
        if (seat == local_) return g_.online.username().empty() ? "Vous" : g_.online.username();
        if (sim_) {
            // 2.77: the free place of every group is the administrator's; the others its simulated players.
            if (seat == 0) return simSetup_.fullGroups ? raid::TestSession::botName(0) : std::string("Place libre (vous)");  // 2.77.3
            return seat <= simSetup_.bots ? raid::TestSession::botName(seat) : "Place libre";
        }
        // 2.72: a test's simulated players (the seats beyond them are free).
        if (session_) return seat < session_->players() ? raid::TestSession::botName(seat) : "Place libre";
        if (link_ && link_->lobby().is_object())
            for (const auto& s : link_->lobby().value("seats", Json::array()))
                if (jget<int>(s, "seat", -1) == seat && !jstr(s, "name").empty()) return jstr(s, "name");
        return "Joueur " + std::to_string(seat + 1);
    }

    // ------------------------------------------------------------------ drawing

    void render(float alpha) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);  // first: it gives ui.r() (2.70: also as the first screen)
        Renderer& r = ui.r();
        ui.pushId("raidgame");
        if (!run_ || catching_) {
            r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{11, 10, 28, 255});
            drawWaiting();
        } else {
            GameWorld& w = run_->world();
            // 2.68: in a gigaboss's arena, its own camera (display only): the whole arena first, then a
            // view sized for this player; elsewhere the world's camera.
            // 2.77.2: a world that no longer steps (the group wiped, everyone waiting for a power's choice) is
            // drawn as it is: with the app's alpha, which changes at every frame, its camera and every body
            // that moved at its last step swung between their last two states (« le jeu tremble »).
            const float shown = downed::drawAlpha(!run_->wiped() && !w.powerOffer(), alpha);
            WorldView view = spec_.on && spec_.free  ? spec_.view(r.width(), r.height())  // 2.75: the spectator's free camera
                             : arenaCam_.active() ? arenaCam_.view(r.width(), r.height())
                                                  : w.camera().view(r.width(), r.height(), shown);
            view = blend_.apply(view, g_.app.frameTime() / 1000.0f);  // 2.77.2: another player watched: eased, never a cut
            view_ = view;
            RenderOptions opt;
            opt.lighting = g_.settings.graphics.dynamicLighting;
            opt.debug = g_.app.debugOverlay;
            renderer_.render(r, g_.app.resources(), w, view, shown, opt);
            drawGigabossWorld(w, view);  // 2.68: its vents, its carapace (over the arena, under the HUD)
            drawSwitches(w, view);      // 2.68: the group's plates
            // 2.77.2: down or spectating: a light filter over the world (the HUD stays as readable).
            if (downed::stateOf(w, local_, spec_.on && !sim_) != downed::State::None) downed::drawFilter(r, ui.screenW(), ui.screenH());
            {
                // The vitals, powers and gigaboss bar of this computer's player, at the raid layout's
                // places (2.72, Paramètres › HUD › « Raids »; display only).
                GameWorld::PlayerScope me(w, std::clamp((spec_.on || (sim_ && local_ < 0)) && watchSeat_ >= 0 ? watchSeat_ : local_, 0, w.teamSize() - 1));
                hudSettings_ = raidhud::hudSettings(g_.settings.gameplay);
                hud_.render(ui, w, view, nullptr, hudSettings_);
                hudAreas_ = {};
                auto areaOf = [&](HudElement e) {
                    if (!hudVisible(hudSettings_, e)) return Rect{};
                    return hud_.area(ui, w, e, hud_.place(ui, w, hudSettings_, e));
                };
                hudAreas_.vitals = areaOf(HudElement::Vitals);
                hudAreas_.powers = areaOf(HudElement::Powers);
                hudAreas_.boss = areaOf(HudElement::Boss);
            }
            drawRaidHud(w);
            drawArmoryLabels(w, view);  // 2.77: the names of the armory's powers
            drawArrows(w, view);
            if (bigMap_ && !paused_ && !ended_) drawBigMap(w);
            drawArenaIntro();
            if (reconnect_.active() && !paused_ && !ended_) drawReconnect();
            if (bannerLayout_.panel.w > 0.0f) drawBanner();  // 2.75: the group wiped; 2.77.2: this player down (placed by drawRaidHud)
            if (overlay_.menu.isOpen() && !paused_ && !ended_) drawMenu(w);  // 2.75.5: opened by the player
            if (sim_ && local_ < 0 && !paused_ && !bigMap_ && simPanel_ < 0 && !overlay_.menu.isOpen()) drawSimWatch();  // 2.77
            if (sim_ && simPanel_ >= 0 && !paused_) drawSimPanel();                                                       // 2.77
            if (paused_ && !ended_) drawPause();
            if (ended_) drawEnd();
            if (sim_) drawSimBanner();  // 2.77: « TEST LOCAL — rien n'est envoyé », over everything
        }
        ui.popId();
        ui.end();
    }

    // ------------------------------------------------------------------ 2.77: the full raid's simulation

    /// « Simuler un raid complet »: every branch and the heart, played locally by simulated players
    /// (raid::RaidSimulation); this computer takes the free place of a group or watches one. Nothing is
    /// ever sent: no network call, no report, no reward.
    void beginSim() {
        if (!g_.online.isAdmin() && !simSetup_.capture) {
            fail_ = "Réservé aux administrateurs connectés à Cosmic Online.";
            return;
        }
        ResourceManager& res = g_.app.resources();
        WorldServices ws{res, g_.app.audio(), g_.content, &g_.app.input(), g_.settings.graphics.screenShake,
                         g_.settings.graphics.particleQuality == 0 ? 0.4f : g_.settings.graphics.particleQuality == 1 ? 0.7f : 1.0f};
        raid::SimSetup s;
        s.groups = simSetup_.groups;
        s.bots = simSetup_.bots;
        s.fullGroups = simSetup_.fullGroups;  // 2.77.3: « Joueurs par groupe »
        s.invincible = simSetup_.invincible;
        s.botsInvincible = simSetup_.botsInvincible;
        s.skill = static_cast<raid::BotSkill>(std::clamp(simSetup_.skill, 0, 2));
        s.background = simSetup_.background;
        sim_ = std::make_unique<raid::RaidSimulation>(ws, s);
        if (!sim_->begin()) {
            fail_ = "Les branches du raid sont introuvables dans les données du jeu : réinstallez le jeu.";
            sim_.reset();
            return;
        }
        hud_.setRaidView(true);
        bindView();
        if (simSetup_.panel >= 0) openSimPanel(simSetup_.panel);
    }

    /// The group shown: its run, its branch, this computer's seat (0 when it holds the free place, else
    /// -1 and the seat watched).
    void bindView() {
        const raid::RaidSimulation::Group& g = sim_->group(sim_->viewed());
        boundSession_ = g.session.get();
        run_ = &g.session->run();
        branch_ = &run_->branch();
        entry_.group = g.id;
        local_ = sim_->seated() ? 0 : -1;
        GameWorld& w = run_->world();
        if (local_ >= 0) watchSeat_ = -1;
        else if (watchSeat_ < 1 || watchSeat_ >= w.teamSize() || w.member(watchSeat_).presence == GameWorld::Presence::Vacant) watchSeat_ = 1;
        run_->setCameraFollow(local_ >= 0 ? 0 : watchSeat_);
        zoneReady();
    }

    /// One step of the raid, within the image's budget of time (the main thread never waits on it: past
    /// the budget the raid runs slower than real time, and the screen says so).
    void stepSim() {
        ++simWanted_;
        if (simSpent_ > kSimBudgetMs) return;
        const auto t0 = std::chrono::steady_clock::now();
        InputFrame f = InputFrame::fromPlayerInput(g_.app.input().consumePlayerInput());
        if (paused_ || simPanel_ >= 0 || g_.app.topScene() != this || !sim_->seated()) f = InputFrame{};
        if (const auto& offer = run_->world().powerOffer(); offer && offer->player == 0 && sim_->seated() && pendingChoice_ != 0) {
            f.choice = pendingChoice_;  // the armory's choice window, through the frame
            pendingChoice_ = 0;
        }
        sim_->step(f);
        ++simDone_;
        ++tick_;
        simSpent_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        // The group shown went to another branch or to the heart (a new instance), or this player lost
        // their place (the group wiped): shown again.
        if (sim_->group(sim_->viewed()).session.get() != boundSession_ || (local_ >= 0) != sim_->seated()) {
            if (run_->wipedNow()) groupWiped();
            bindView();
            return;
        }
        if (const int z = run_->zoneFinishedNow()) zoneDone(z);
        if (run_->wipedNow()) groupWiped();
        if (run_->heartWonNow() && !victoryPlayed_) {
            victoryPlayed_ = true;
            playVictory();
        }
        if (run_->zoneSerial() != serial_) zoneReady();
    }

    /// Once an image: its budget again, the speed measured, the world's events as notes, the zones to take
    /// back (the large map), the spectator's keys.
    void updateSim() {
        if (!sim_) return;
        simSpent_ = 0.0;
        panelJustClosed_ = false;
        if (clock_ - speedAt_ >= 1.0f) {
            speedPct_ = simWanted_ > 0 ? static_cast<int>(100 * simDone_ / simWanted_) : 100;
            simWanted_ = simDone_ = 0;
            speedAt_ = clock_;
        }
        const auto& feed = sim_->feed();
        for (const auto& e : feed)
            if (e.tick > feedSeen_ || (feedSeen_ == 0 && e.tick == 0 && !feedStarted_)) {
                using K = raid::RaidSimulation::WorldEvent::Kind;
                note(e.text, e.kind == K::Danger ? kDanger : e.kind == K::World ? kGold : e.kind == K::Branch ? kGreen : kSky);
            }
        if (!feed.empty()) feedSeen_ = feed.back().tick;
        feedStarted_ = true;
        retake_ = sim_->retake(sim_->group(sim_->viewed()).branch);
        InputManager& in = g_.app.input();
        if (simPanel_ >= 0) {
            if (g_.ui.backPressed() || in.menuPressed(Action::Pause)) {
                simPanel_ = -1;
                panelJustClosed_ = true;  // the same press must not open the pause
            }
            return;
        }
        // Watching a group: the previous / next of its players in play (Q / E, LB / RB).
        if (local_ < 0 && !paused_ && !bigMap_ && run_ && g_.app.topScene() == this) {
            if (in.menuPressed(Action::MenuTabNext)) watchNext(+1);
            if (in.menuPressed(Action::MenuTabPrev)) watchNext(-1);
        }
    }

    void watchNext(int dir) {
        const int n = run_->world().teamSize();
        for (int k = 1; k <= n; ++k) {
            const int s = ((watchSeat_ < 0 ? 0 : watchSeat_) + dir * k + n * 4) % n;
            // 2.78: every player of the group, whatever their zone (the camera goes to the zone of the one watched).
            const GameWorld* sw = run_->worldOf(run_->zoneOf(s));
            if (!sw || s >= sw->teamSize() || sw->member(s).presence == GameWorld::Presence::Vacant) continue;
            if (s != watchSeat_) blend_.start(view_);  // 2.77.2: eased
            watchSeat_ = s;
            run_->setCameraFollow(s);
            renderer_.setOpaquePlayers(1u << s);
            return;
        }
    }

    void openSimPanel(int tab) {
        simPanel_ = std::clamp(tab, 0, 3);
        bigMap_ = false;
        g_.ui.setFocus("raidgame/simpanel/close");
    }

    /// « TEST LOCAL — rien n'est envoyé » first, then where this player is (the line is fitted from its start).
    std::string simStatus() const {
        const raid::RaidSimulation::Group& g = sim_->group(sim_->viewed());
        std::string t = "TEST LOCAL — rien n'est envoyé · " + g.name + " · " + g.session->run().branch().name;
        if (g.wiped) t += " · groupe éliminé (spectateurs)";
        else if (sim_->seated()) t += g.session->autopilot() ? " · vous : pilote automatique (AFK)" : " · vous jouez";
        else t += " · vous regardez";
        if (simSetup_.background > 1) t += " · autres groupes ×" + std::to_string(simSetup_.background) + " (accéléré)";
        if (speedPct_ < 97) t += " · simulation à " + std::to_string(speedPct_) + " % du temps réel";
        return t;
    }

    /// The banner over everything that dims the screen (the pause, the raid's panel): always visible.
    void drawSimBanner() {
        if (!(paused_ || simPanel_ >= 0) || statusRect_.w <= 0.0f) return;
        raidhud::drawLineBox(g_.ui, statusRect_, statusK_, simStatus(), kGold, true);
    }

    /// Watching a group: whom, and how to change (keyboard, gamepad, mouse), in a free place over the status.
    void drawSimWatch() {
        UIContext& ui = g_.ui;
        const float w = std::min(ui.s(560), ui.screenW() - ui.s(24)), h = ui.s(70);
        Rect box{statusRect_.w > 0.0f ? statusRect_.x : ui.s(12), (statusRect_.w > 0.0f ? statusRect_.y : ui.screenH()) - h - ui.s(10), w, h};
        for (int guard = 0; guard < 12; ++guard) {
            bool hit = false;
            for (const Rect& o : hudRects_)
                if (o.intersects(box) && !(o.x == statusRect_.x && o.y == statusRect_.y)) {
                    box.y = o.y - h - ui.s(8);
                    hit = true;
                }
            if (!hit) break;
        }
        if (box.y < ui.s(4)) return;
        ui.panel(box, true, 10);
        const raid::RaidSimulation::Group& g = sim_->group(sim_->viewed());
        const std::string who = watchSeat_ >= 0 ? seatName(watchSeat_) : std::string("le groupe");
        const float pad = ui.s(12), bw = ui.s(44), gap = ui.s(6);
        fit(ui, "VOUS REGARDEZ · " + g.name + " · " + who, {box.x + pad, box.y + ui.s(20)}, 15, box.w - pad * 2 - bw * 2 - gap * 2, kGold, FontFace::Bold);
        fit(ui, "Q / E (LB / RB) : joueur précédent / suivant · Échap : rejoindre un groupe", {box.x + pad, box.y + ui.s(48)}, 13,
            box.w - pad * 2 - bw * 2 - gap * 2, kDim);
        UIContext::ButtonStyle bs;
        bs.small = true;
        ui.pushId("simwatch");
        if (ui.button("prev", {box.right() - pad - bw * 2 - gap, box.y + ui.s(13), bw, ui.s(44)}, "◀", bs)) watchNext(-1);
        if (ui.button("next", {box.right() - pad - bw, box.y + ui.s(13), bw, ui.s(44)}, "▶", bs)) watchNext(+1);
        ui.popId();
    }

    /// The raid's panel (from the pause): its groups (join, watch), the contributions of every player, the
    /// world's events, the titan's map with the state of the whole raid. Measured, scrolled, never over the
    /// status line.
    void drawSimPanel() {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{2, 4, 10, 170});
        const float bottom = statusRect_.w > 0.0f && statusRect_.y > H * 0.5f ? statusRect_.y - ui.s(10) : H - ui.s(24);
        const float pw = std::min(ui.s(1180), W - ui.s(32));
        const Rect box{(W - pw) * 0.5f, ui.s(24), pw, std::max(ui.s(200), bottom - ui.s(24))};
        ui.panel(box, true, 16);
        ui.pushId("simpanel");
        const float pad = ui.s(20), x = box.x + pad, w = box.w - pad * 2;
        const float closeW = ui.s(150), bh = ui.s(44);
        UIContext::ButtonStyle cs;
        cs.small = true;
        if (ui.button("close", {box.right() - pad - closeW, box.y + ui.s(14), closeW, bh}, "Fermer", cs)) {
            simPanel_ = -1;
            ui.popId();
            return;
        }
        fit(ui, "RAID COMPLET · TEST LOCAL — rien n'est envoyé", {x, box.y + ui.s(36)}, 20, w - closeW - ui.s(16), kGold, FontFace::Title);
        const std::vector<std::string> tabs = {"Groupes", "Contributions", "Événements du monde", "Carte du titan"};
        std::vector<bool> on(tabs.size(), false);
        on[static_cast<size_t>(simPanel_)] = true;
        float chipsBottom = box.y + ui.s(70);
        const int t = chips(ui, "tabs", x, box.y + ui.s(66), box.right() - pad, tabs, on, chipsBottom);
        if (t >= 0) simPanel_ = t;
        const Rect body{x, chipsBottom + ui.s(12), w, box.bottom() - pad - chipsBottom - ui.s(12)};
        if (simPanel_ == 0) drawSimGroups(body);
        else if (simPanel_ == 1) drawSimContributions(body);
        else if (simPanel_ == 2) drawSimFeed(body);
        else drawSimTitan(body);
        ui.popId();
    }

    std::string groupWhere(const raid::RaidSimulation::Group& g) const {
        const raid::RaidRun& r = g.session->run();
        const raid::ZoneInfo* z = r.branch().zone(r.zone());
        return r.branch().name + " · " + (r.zone() == 0 ? std::string("Vestiaire") : z ? z->name : std::string("?"));
    }

    void drawSimGroups(const Rect& body) {
        UIContext& ui = g_.ui;
        const std::string head = "Gigaboss tombés : " + std::to_string(sim_->gigabossesDown()) + " / 7 · Cœur : " +
                                 (sim_->heartFallen() ? std::string("tombé") : sim_->heartOpen() ? "ouvert, " + std::to_string(sim_->heartTaken() / 100) + " % ôtés" : std::string("fermé")) +
                                 " · Reliques : " + std::to_string(sim_->relics()) + " / 7 · coût d'un pas du raid : " +
                                 std::to_string(static_cast<int>(sim_->avgStepMs() * 100.0 + 0.5) / 100) + "," +
                                 std::to_string(static_cast<int>(sim_->avgStepMs() * 100.0 + 0.5) % 100 / 10) + " ms";
        fit(ui, head, {body.x, body.y + ui.s(10)}, 14, body.w, kText, FontFace::Bold);
        const Rect list{body.x, body.y + ui.s(30), body.w, body.h - ui.s(30)};
        const float rowH = ui.s(72), gap = ui.s(8);
        const auto& groups = sim_->groups();
        ui.beginScroll("groups", list, static_cast<float>(groups.size()) * (rowH + gap));
        float y = list.y - ui.scrollOffset();
        const float bw = std::min(ui.s(150), list.w * 0.18f), bgap = ui.s(8);
        for (size_t i = 0; i < groups.size(); ++i, y += rowH + gap) {
            if (y + rowH < list.y || y > list.bottom()) continue;
            const raid::RaidSimulation::Group& g = *groups[i];
            const bool here = static_cast<int>(i) == sim_->viewed();
            const Rect row{list.x, y, list.w - ui.s(14), rowH};
            ui.r().fillRoundedRect(row, ui.s(8), here ? Color{40, 34, 72, 255} : Color{22, 28, 60, 255});
            const float textW = row.w - bw * 2 - bgap * 3;
            int inPlay = 0, seats = 0;
            const raid::RaidRun& gr = g.session->run();
            // 2.77.3 (« Joueurs par groupe »): the administrator's seat, where they are not, is a simulated player's.
            // 2.78: each seat read in the world of its own zone (the group can be spread over several).
            for (int s = simSetup_.fullGroups && !(here && sim_->seated()) ? 0 : 1; s < raid::kMaxInstancePlayers; ++s) {
                const GameWorld* sw = gr.worldOf(gr.zoneOf(s));
                if (!sw || s >= sw->teamSize() || sw->member(s).presence == GameWorld::Presence::Vacant) continue;
                ++seats;
                inPlay += sw->member(s).presence == GameWorld::Presence::Playing;
            }
            std::string state = g.wiped ? "ÉLIMINÉ · ses joueurs regardent" : std::to_string(inPlay) + " / " + std::to_string(seats) + " joueurs simulés en jeu";
            if (g.relieves > 0) state += " · renfort du Groupe " + std::to_string(g.relieves);
            if (here) state += sim_->seated() ? " · VOUS ÊTES ICI" : " · vous regardez";
            fit(ui, g.name + " · " + groupWhere(g), {row.x + ui.s(12), row.y + ui.s(18)}, 16, textW, here ? kGold : kText, FontFace::Bold);
            fit(ui, state, {row.x + ui.s(12), row.y + ui.s(40)}, 13, textW, g.wiped ? kDanger : kGreen);
            const std::string plan = g.session->bots().plan();
            fit(ui, plan.empty() ? "—" : "Ce qu'ils font : " + plan, {row.x + ui.s(12), row.y + ui.s(60)}, 12, textW, kDim);
            UIContext::ButtonStyle bs;
            bs.small = true;
            bs.enabled = !g.wiped && !(here && sim_->seated());
            bs.primary = bs.enabled;
            const float by = row.y + (rowH - ui.s(44)) * 0.5f;
            if (ui.button("join" + std::to_string(g.id), {row.right() - bw * 2 - bgap * 2, by, bw, ui.s(44)}, "Rejoindre", bs) && sim_->join(static_cast<int>(i))) {
                simPanel_ = -1;
                bindView();
            }
            UIContext::ButtonStyle ws;
            ws.small = true;
            ws.enabled = !(here && !sim_->seated());
            if (ui.button("watch" + std::to_string(g.id), {row.right() - bw - bgap, by, bw, ui.s(44)}, "Regarder", ws)) {
                sim_->watch(static_cast<int>(i));
                simPanel_ = -1;
                bindView();
            }
        }
        ui.endScroll();
    }

    void drawSimContributions(const Rect& body) {
        UIContext& ui = g_.ui;
        // Every player of every group (the same counts as the tables of the zones), columns measured.
        static const char* kCols[] = {"Dégâts", "Gigaboss", "Relevés", "Mécanismes", "Points faibles", "Objectifs"};
        const float nameW = body.w * 0.34f, colW = (body.w - nameW - ui.s(14)) / 6.0f, lineH = ui.s(24);
        for (int c = 0; c < 6; ++c)
            fit(ui, kCols[c], {body.x + nameW + colW * static_cast<float>(c) + colW * 0.5f, body.y + ui.s(10)}, 13, colW - ui.s(6), kDim, FontFace::Bold, Align::Center);
        fit(ui, "Groupe · joueur", {body.x, body.y + ui.s(10)}, 13, nameW - ui.s(6), kDim, FontFace::Bold);
        struct Row {
            std::string name;
            std::vector<std::string> cells;
            bool group = false;
        };
        std::vector<Row> rows;
        for (const auto& gp : sim_->groups()) {
            const raid::RaidSimulation::Group& g = *gp;
            rows.push_back({g.name + " · " + groupWhere(g), {}, true});
            for (const raid::Contribution& c : g.session->run().contributions().records()) {
                if (c.seat < 0) continue;
                Row r;
                r.name = c.seat == 0 ? std::string("Place libre (vous)") : raid::TestSession::botName(c.seat);
                r.cells = {std::to_string(c.damage / 10), std::to_string(c.bossDamage / 10), std::to_string(c.rescues), std::to_string(c.vents),
                           std::to_string(c.weakPoints), std::to_string(c.objectives)};
                rows.push_back(std::move(r));
            }
        }
        const Rect list{body.x, body.y + ui.s(26), body.w, body.h - ui.s(26)};
        ui.beginScroll("contrib", list, static_cast<float>(rows.size()) * lineH);
        float y = list.y - ui.scrollOffset();
        for (const Row& r : rows) {
            if (y + lineH >= list.y && y <= list.bottom()) {
                if (r.group) {
                    fit(ui, r.name, {body.x, y + lineH * 0.5f}, 14, body.w - ui.s(14), kGold, FontFace::Bold);
                } else {
                    fit(ui, r.name, {body.x + ui.s(14), y + lineH * 0.5f}, 13, nameW - ui.s(20), kText);
                    for (size_t c = 0; c < r.cells.size(); ++c)
                        fit(ui, r.cells[c], {body.x + nameW + colW * static_cast<float>(c) + colW * 0.5f, y + lineH * 0.5f}, 13, colW - ui.s(6), kText,
                            FontFace::Body, Align::Center);
                }
            }
            y += lineH;
        }
        ui.endScroll();
    }

    void drawSimFeed(const Rect& body) {
        UIContext& ui = g_.ui;
        const auto& feed = sim_->feed();
        const float lineH = ui.s(19), timeW = ui.s(64);
        std::vector<std::vector<std::string>> lines;
        float total = 0.0f;
        for (size_t k = feed.size(); k-- > 0;) {
            lines.push_back(wrap(ui, feed[k].text, FontFace::Body, 14, body.w - timeW - ui.s(20), 3));
            total += static_cast<float>(lines.back().size()) * lineH + ui.s(6);
        }
        if (feed.empty()) {
            fit(ui, "Rien encore : le raid commence.", {body.x, body.y + ui.s(12)}, 14, body.w, kDim);
            return;
        }
        ui.beginScroll("feed", body, total);
        float y = body.y - ui.scrollOffset();
        size_t li = 0;
        for (size_t k = feed.size(); k-- > 0; ++li) {
            const auto& e = feed[k];
            const float h = static_cast<float>(lines[li].size()) * lineH;
            if (y + h >= body.y && y <= body.bottom()) {
                using K = raid::RaidSimulation::WorldEvent::Kind;
                const Color c = e.kind == K::Danger ? kDanger : e.kind == K::World ? kGold : e.kind == K::Branch ? kGreen : kSky;
                const int sec = static_cast<int>(e.tick / kSimHz);
                char clockText[24];
                std::snprintf(clockText, sizeof clockText, "%d:%02d", sec / 60, sec % 60);
                fit(ui, clockText, {body.x, y + lineH * 0.5f}, 13, timeW - ui.s(6), kDim);
                drawLines(ui, lines[li], {body.x + timeW, y}, 14, lineH, c);
            }
            y += h + ui.s(6);
        }
        ui.endScroll();
    }

    void drawSimTitan(const Rect& body) {
        UIContext& ui = g_.ui;
        // The titan (each part in the state of the raid), and at its right each branch's line.
        const float figW = std::min(body.w * 0.42f, body.h * 0.9f);
        std::map<std::string, asterion::State> states;
        using P = raid::RaidSimulation::PartState;
        auto word = [](P p) {
            switch (p) {
                case P::Unknown: return "pas encore découverte";
                case P::Attacked: return "combat en cours";
                case P::Weakened: return "affaiblie";
                case P::Destroyed: return "détruite";
                default: return "découverte";
            }
        };
        for (size_t b = 0; b < sim_->branches().size(); ++b) {
            const P p = sim_->partState(static_cast<int>(b));
            states[sim_->branches()[b]->part] = p == P::Destroyed ? asterion::State::Destroyed : p == P::Weakened ? asterion::State::Weakened
                                              : p == P::Attacked  ? asterion::State::Attacked  : p == P::Unknown ? asterion::State::Unknown
                                                                                                                 : asterion::State::Intact;
        }
        titanFigure_.draw(g_, "raidgame/simpanel/titan", {body.x, body.y, figW, body.h}, states);
        const float x = body.x + figW + ui.s(20), w = body.right() - x;
        const float lineH = ui.s(44);
        float y = body.y + ui.s(4);
        for (size_t b = 0; b < sim_->branches().size() && y + lineH <= body.bottom(); ++b) {
            const raid::Branch& br = *sim_->branches()[b];
            const P p = sim_->partState(static_cast<int>(b));
            int groups = 0;
            for (const auto& g : sim_->groups()) groups += g->branch == static_cast<int>(b) && !g->wiped;
            int finished = 0;
            for (const raid::ZoneInfo& z : br.zones) finished += raid::zoneFinished(br, z.id, sim_->mask(static_cast<int>(b)));
            const Color c = p == P::Destroyed ? kGreen : p == P::Attacked ? kOrange : p == P::Unknown ? kDim : kText;
            fit(ui, br.name + " · " + br.gigaboss + " · " + word(p), {x, y + ui.s(12)}, 15, w, c, FontFace::Bold);
            std::string sub = std::to_string(finished) + " / " + std::to_string(br.zones.size()) + " zones terminées · " + std::to_string(groups) +
                              (groups > 1 ? " groupes" : " groupe") + " sur place";
            if (static_cast<int>(b) == sim_->heartIndex()) sub = sim_->heartOpen() ? std::to_string(sim_->heartTaken() / 100) + " % de sa vie partagée ôtés · phase " +
                                                                                        std::to_string(std::min(4, sim_->heartPhase())) + " · " + std::to_string(groups) + " groupes"
                                                                                  : "fermé tant que les sept Gigaboss ne sont pas tombés (" +
                                                                                        std::to_string(sim_->gigabossesDown()) + " / 7)";
            const auto retake = sim_->retake(static_cast<int>(b));
            if (!retake.empty()) sub += " · " + std::to_string(retake.size()) + " zone(s) à reprendre";
            fit(ui, sub, {x, y + ui.s(32)}, 13, w, kDim);
            y += lineH;
        }
        titanFigure_.drawTooltip(g_);
    }

    /// 2.77: in an armory, the name of each power over its tablet (its icon and cost when there is room;
    /// raidhud::armoryLabels), never over the HUD (display only).
    void drawArmoryLabels(GameWorld& w, const WorldView& view) {
        if (!run_ || run_->zone() != 0) return;
        auto labels = raidhud::armoryLabels(g_.ui, w, view);
        labels.erase(std::remove_if(labels.begin(), labels.end(),
                                    [&](const raidhud::ArmoryLabel& l) {
                                        for (const Rect& o : hudRects_)
                                            if (o.intersects(l.box)) return true;
                                        return false;
                                    }),
                     labels.end());
        raidhud::drawArmoryLabels(g_.ui, labels);
    }

    // ------------------------------------------------------------------ 2.75: the spectator mode

    /// Whether this computer only watches: its group was wiped, or it watches another group.
    bool spectating() const { return run_ && !catching_ && (run_->wiped() || (link_ && link_->watcher())); }

    /// Enters the spectator mode (once), then moves its free camera and switches the player watched.
    void updateSpectator(GameWorld& w, float dt) {
        if (!spectating()) return;
        if (!spec_.on) {
            spec_.start(w.camera().center());
            spec_.free = watchSeat_ < 0;
            if (ownInstance_.empty()) ownInstance_ = entry_.instanceId;
            note("GROUPE ÉLIMINÉ — mode spectateur : caméra libre, ou joueur suivant / précédent de la branche", kDanger);
        }
        if (!test_ && clock_ - spec_.fetchedAt > 10.0f) fetchTargets();
        InputManager& in = g_.app.input();
        if (paused_ || ended_ || bigMap_ || overlay_.menu.isOpen() || g_.app.topScene() != this) return;  // 2.75.5: its keys are the menu's
        if (in.menuPressed(Action::MenuTabNext)) watchStep(+1);
        if (in.menuPressed(Action::MenuTabPrev)) watchStep(-1);
        if (in.menuPressed(Action::MenuConfirm) && !spec_.free) freeCamera();  // 2.77.2: Entrée, A (the banner says it)
        if (spec_.free) spec_.move(in, dt, w.level().bounds);
    }

    /// 2.77.2: the free camera again, from where the view is (eased).
    void freeCamera() {
        blend_.start(view_);
        spec_.index = -1;
        spec_.free = true;
        if (run_) spec_.center = run_->world().camera().center();
    }

    /// The players still playing in the branch (the server's list, refreshed every 10 s).
    void fetchTargets() {
        spec_.fetchedAt = clock_;
        g_.online.send("GET", "/api/v1/raids/instances/" + ownInstance_ + "/watch", Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                           if (!r.ok) return;
                           std::vector<raidspec::Target> list;
                           for (const auto& t : r.data.value("targets", Json::array())) {
                               raidspec::Target x;
                               x.instance = jstr(t, "instance_id");
                               x.name = jstr(t, "name");
                               x.clan = jstr(t, "clan_tag");
                               x.seat = std::clamp(jget<int>(t, "seat", 0), 0, raid::kMaxInstancePlayers - 1);
                               x.group = jget<int>(t, "group", 1);
                               x.zone = jget<int>(t, "zone", 0);
                               x.startZone = jget<int>(t, "start_zone", 0);
                               if (!x.instance.empty()) list.push_back(std::move(x));
                           }
                           if (list.size() > 200) list.resize(200);
                           spec_.targets = std::move(list);
                           if (spec_.index >= static_cast<int>(spec_.targets.size())) spec_.index = -1;
                       }));
    }

    /// The next (+1) or previous (−1) view: the free camera, or a player of the branch (another group:
    /// its game watched through a spectator's socket, its history replayed first).
    void watchStep(int dir) {
        blend_.start(view_);  // 2.77.2: eased (another group's world, new, cuts: zoneReady)
        const int k = spec_.step(dir);
        if (k < 0) return;  // the free camera over the world shown
        const raidspec::Target t = spec_.targets[static_cast<size_t>(k)];
        if (t.instance == entry_.instanceId && run_) {
            watchSeat_ = t.seat;
            run_->setCameraFollow(t.seat);
            renderer_.setOpaquePlayers(1u << t.seat);
            return;
        }
        stopCatchUp();
        if (link_) link_->close();
        link_.reset();
        run_ = nullptr;
        ownRun_.reset();
        entry_.instanceId = t.instance;
        entry_.startZone = t.startZone;
        watchSeat_ = t.seat;
        tick_ = 0;
        sentUpTo_ = UINT32_MAX;
        serial_ = -1;
        connect();
    }

    /// The spectator's line of the banner: the free camera, or the player of the branch watched.
    std::string spectatorLine() const {
        if (spec_.free || spec_.index < 0 || spec_.index >= static_cast<int>(spec_.targets.size()))
            return spec_.targets.empty() ? "Caméra libre (flèches, stick, molette) · plus personne ne joue dans la branche"
                                         : "Caméra libre (flèches, stick, molette) · " + std::to_string(spec_.targets.size()) + " joueurs à suivre";
        const raidspec::Target& t = spec_.targets[static_cast<size_t>(spec_.index)];
        return "Vous regardez " + t.name + " · [" + t.clan + "]·" + std::to_string(t.group) + " · zone " + std::to_string(t.zone);
    }

    // ------------------------------------------------------------------ 2.77.2: this player down

    /// This player down, waiting for a revival: the camera on their soul first; the previous / next player
    /// in play (Q / E, LB / RB), back to the soul (Entrée, A), or the banner's buttons. Display only: the
    /// camera is not part of the simulation, and nothing a player down sends plays.
    void updateDowned(GameWorld& w) {
        const bool down = downed::stateOf(w, local_, spec_.on && !sim_) == downed::State::Down;
        if (down != wasDown_) {
            wasDown_ = down;
            // Revived while watching another player: their own view again (the group wiped: the spectator's; the
            // full raid's simulation, the seat left: the group's watched seat, bindView).
            if (downWatch_ >= 0 && local_ >= 0 && !spec_.on) watchSeatDown(-1);
            downWatch_ = -1;
        }
        if (!down) return;
        // The player watched left the game (down too, away): back to the soul.
        if (downWatch_ >= 0 && (downWatch_ >= w.teamSize() || w.member(downWatch_).presence != GameWorld::Presence::Playing)) watchSeatDown(-1);
        if (paused_ || ended_ || bigMap_ || overlay_.menu.isOpen() || simPanel_ >= 0 || g_.app.topScene() != this) return;  // their keys
        InputManager& in = g_.app.input();
        if (in.menuPressed(Action::MenuTabNext)) watchDown(+1);
        if (in.menuPressed(Action::MenuTabPrev)) watchDown(-1);
        if (in.menuPressed(Action::MenuConfirm)) watchDown(0);
    }

    /// The next (+1) or previous (−1) player in play, in the seats' order, the soul between the last and
    /// the first (it wraps); 0: back to the soul.
    void watchDown(int dir) {
        GameWorld& w = run_->world();
        int next = -1;
        const int n = w.teamSize();
        if (dir != 0 && n > 0)
            for (int k = 1; k <= n; ++k) {
                const int s = (((downWatch_ >= 0 ? downWatch_ : local_) + dir * k) % n + n) % n;
                if (s == local_) break;  // round the group: the soul
                if (w.member(s).presence == GameWorld::Presence::Playing) {
                    next = s;
                    break;
                }
            }
        watchSeatDown(next);
    }

    void watchSeatDown(int seat) {
        if (seat == downWatch_) return;
        blend_.start(view_);
        downWatch_ = seat;
        const int follow = seat >= 0 ? seat : local_;
        run_->setCameraFollow(follow);
        renderer_.setOpaquePlayers(follow >= 0 && follow < 32 ? 1u << follow : 0u);
    }

    /// What this player down is told, from the simulation's state.
    downed::Banner downBanner(GameWorld& w) {
        const GameWorld::TeamMember& m = w.member(local_);
        downed::DownInfo d;
        d.revive = m.revive;
        if (m.revive > 0.0f && m.reviver >= 0 && m.reviver < w.teamSize()) d.reviver = seatName(m.reviver);
        d.others = false;
        for (int i = 0; i < w.teamSize(); ++i) d.others = d.others || (i != local_ && w.member(i).presence == GameWorld::Presence::Playing);
        d.canRevive = w.canRevive() && d.others;  // a raid: as many as wanted, while an ally is in play
        // 2.78: allies in play in the branch's other zones (only an ally of this zone revives: one who comes back).
        for (int z : run_->zonesLoaded())
            if (const GameWorld* o = run_->worldOf(z); o && o != &w)
                for (int i = 0; i < o->teamSize(); ++i)
                    if (run_->zoneOf(i) == z && o->member(i).presence == GameWorld::Presence::Playing) ++d.elsewhere;
        d.revivesLeft = w.revivesUnlimited() ? -1 : w.revivesLeft();
        d.revivesMax = GameWorld::kTeamRevives;
        d.soulInView = view_.toScreen(Rect{m.soul.x - 8.0f, m.soul.y - 28.0f, 16.0f, 28.0f}).intersects({0.0f, 0.0f, g_.ui.screenW(), g_.ui.screenH()});
        if (downWatch_ >= 0) d.watching = seatName(downWatch_);
        return downed::downBanner(d);
    }

    /// The banner of this player down, or of the group wiped: its text from the simulation's state, its
    /// place under the HUD at the top of the screen (the notifications go under it). Not over the pause,
    /// the menu, the large map, the arena's title nor a big message of the level (it waits).
    void placeBanner(GameWorld& w) {
        bannerLayout_ = {};
        banner_ = {};
        bool bigMessage = false;
        for (const WorldMessage& m : w.messages()) bigMessage = bigMessage || (m.style != WorldMessage::Style::Hint && m.age < m.duration);
        if (paused_ || ended_ || bigMap_ || overlay_.menu.isOpen() || reconnect_.active() || bigMessage || arenaCam_.intro() || simPanel_ >= 0) return;
        const downed::State st = downed::stateOf(w, local_, spec_.on && !sim_);
        if (st == downed::State::None) return;
        banner_ = st == downed::State::Wiped ? downed::wipedBanner(spectatorLine(), spec_.free, !spec_.targets.empty()) : downBanner(w);
        bannerLayout_ = downed::placed(g_.ui, banner_, hudTaken());
        hudRects_.push_back(bannerLayout_.panel);  // the arrows and the armory's names keep off it
    }

    /// The HUD drawn so far, the heart's bar and briefing (drawn after the notifications) and the danger
    /// band's place: what the banner and the notifications under it keep off.
    std::vector<Rect> hudTaken() {
        std::vector<Rect> taken = hudRects_;
        const RaidHudLayout& l = g_.settings.gameplay.raidHud;
        if (run_->heartRoutine())
            for (const RaidHudElement e : {RaidHudElement::Heart, RaidHudElement::HeartEffects})
                if (l[e].visible || !RaidHudLayout::canHide(e)) taken.push_back(raidhud::area(g_.ui, l, e, hudAreas_));
        taken.push_back(raidhud::area(g_.ui, l, RaidHudElement::Danger, hudAreas_));
        return taken;
    }

    /// Draws the banner placed this frame; its buttons answer the mouse (their keys: updateSpectator, updateDowned).
    void drawBanner() {
        const int k = downed::draw(g_.ui, banner_, bannerLayout_, "banner");
        if (k < 0) return;
        if (banner_.state == downed::State::Wiped) {
            if (k == 0) watchStep(-1);
            else if (k == 2) watchStep(+1);
            else freeCamera();
        } else {
            watchDown(k == 0 ? -1 : k == 2 ? +1 : 0);
        }
    }

    /// Before the game: connecting, the instance's lobby (« Lancer »), the catch-up.
    void drawWaiting() {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        const Rect box{W * 0.5f - std::min(ui.s(330), W * 0.45f), H * 0.5f - ui.s(170), std::min(ui.s(660), W * 0.9f), ui.s(340)};
        ui.panel(box, true, 16);
        const float x = box.x + ui.s(28), w = box.w - ui.s(56);
        fit(ui, "ASTERION — " + std::string(branch_ ? branch_->name : "BRAS DROIT"), {x, box.y + ui.s(36)}, 26, w, kText, FontFace::Title);
        const std::string group = "[" + entry_.clanTag + "]·" + std::to_string(entry_.group);
        std::string line, sub;
        bool lobby = false;
        if (!fail_.empty()) {
            line = fail_;
        } else if (entry_.spectator && !run_ && link_ && link_->watcher() && !link_->go()) {
            // 2.75: back after the group was wiped, its game over: a player of the branch is looked for.
            line = "GROUPE ÉLIMINÉ — mode spectateur";
            sub = spec_.targets.empty() && clock_ - spec_.fetchedAt < 10.0f ? "Plus personne ne joue dans la branche pour l'instant."
                                                                            : "Recherche d'un joueur de la branche à regarder…";
        } else if (reconnect_.active()) {
            line = reconnect_.message(clock_);  // 2.74: a catch-up interrupted by a lost link
            sub = "Le groupe continue sans vous ; vous le rejoignez dès que la liaison revient.";
        } else if (catching_) {
            const uint32_t total = std::max<uint32_t>(1, link_->available());
            const std::string pct = std::to_string(std::min<uint32_t>(100, progress_ * 100 / total));
            line = rejoining_ ? "Liaison rétablie : rattrapage de la partie (" + pct + " %)" : "Le groupe joue déjà : rattrapage de la partie (" + pct + " %)";
            sub = "Vous rejoignez " + group + " près de vos alliés dès que votre jeu est à jour.";
        } else if (connecting_ || !link_ || link_->connecting()) {
            line = "Connexion à l'instance de " + group + "…";
        } else if (link_->go()) {
            line = "Chargement de la zone…";
        } else {
            lobby = true;
            int n = 0;
            if (link_->lobby().is_object())
                for (const auto& s : link_->lobby().value("seats", Json::array())) n += jget<bool>(s, "connected", false) ? 1 : 0;
            line = "Instance de " + group + " : " + std::to_string(n) + " / 8 joueurs connectés";
            sub = "Lancez le raid quand votre groupe est là : ceux qui arrivent ensuite rejoignent la partie en cours.";
        }
        float y = box.y + ui.s(70);
        const auto l1 = wrap(ui, line, FontFace::Bold, 18, w, 3);
        y += drawLines(ui, l1, {x, y}, 18, ui.s(26), fail_.empty() ? kText : kDanger, FontFace::Bold) + ui.s(8);
        const auto l2 = wrap(ui, sub, FontFace::Body, 15, w, 3);
        y += drawLines(ui, l2, {x, y}, 15, ui.s(21), kDim);
        const float bh = ui.s(52), by = box.bottom() - ui.s(24) - bh;
        const float gap = ui.s(12), bw = lobby ? (w - gap) * 0.5f : w;
        UIContext::ButtonStyle bs;
        bs.primary = true;
        if (lobby && ui.button("start", {x, by, bw, bh}, "Lancer le raid", bs)) link_->startGame(raid::kMaxInstancePlayers);
        if (catching_ && link_) {
            const float p = static_cast<float>(progress_) / static_cast<float>(std::max<uint32_t>(1, link_->available()));
            ui.r().fillRoundedRect({x, by - ui.s(26), w, ui.s(10)}, ui.s(5), Color{40, 30, 50, 255});
            ui.r().fillRoundedRect({x, by - ui.s(26), w * std::clamp(p, 0.0f, 1.0f), ui.s(10)}, ui.s(5), kOrange);
        }
        if (!catching_ && (ui.button("leave", {lobby ? x + bw + gap : x, by, bw, bh}, "Retour à la carte") || ui.backPressed())) leave();
    }

    void leave() {
        if (test_) {
            g_.app.popScene();  // back to ADMINISTRATION › Raids
            return;
        }
        if (link_) link_->close();
        g_.app.switchScene(makeRaidBranch(g_, entry_.eventId, entry_.branch), 0.3f);
    }

    /// 2.72: the raid's HUD at the places of the player's raid layout (RaidHudLayout), each element
    /// drawn into its area (ui/online/RaidHud); what is drawn is kept clear of the arrows.
    void drawRaidHud(GameWorld& w) {
        UIContext& ui = g_.ui;
        const RaidHudLayout& l = g_.settings.gameplay.raidHud;
        using E = RaidHudElement;
        auto shown = [&](E e) { return l[e].visible || !RaidHudLayout::canHide(e); };
        auto at = [&](E e) { return raidhud::area(ui, l, e, hudAreas_); };
        auto k = [&](E e) { return raidhud::scaleOf(l, e); };
        hudRects_.clear();
        for (const Rect& a : {hudAreas_.vitals, hudAreas_.powers})
            if (a.w > 0.0f) hudRects_.push_back(a);
        if (const Enemy* b = w.activeBoss(); b && b->alive() && hudAreas_.boss.w > 0.0f) hudRects_.push_back(hudAreas_.boss);
        if (shown(E::Group)) {
            const Rect r = at(E::Group);
            raidhud::drawGroup(ui, r, k(E::Group), groupData(w));
            hudRects_.push_back(r);
        }
        if (shown(E::Minimap)) {
            const Rect r = at(E::Minimap);
            const raid::ZoneInfo* z = branch_->zone(run_->zone());
            const Vec2 focus = local_ >= 0 && local_ < w.teamSize() ? w.playerAt(local_).center() : w.level().bounds.center();
            const std::string key = actionKey(g_.app.input(), Action::RaidMap);
            const std::string title = "MINIMAP · " + std::string(z ? z->name : "?") + (key.empty() ? "" : " · " + key + " : agrandir");
            minimap_.drawSmall(ui, g_.app.resources(), w, r, k(E::Minimap), title, mapMarks(w), magmaAreas(w), focus);
            hudRects_.push_back(r);
        }
        if (shown(E::Objectives)) {
            const Rect r = at(E::Objectives);
            const raid::ZoneInfo* z = branch_->zone(run_->zone());
            raidhud::drawObjectives(ui, r, k(E::Objectives), "OBJECTIFS · " + std::string(z ? z->name : ""), objectiveLines(w));
            hudRects_.push_back(r);
        }
        if (shown(E::Alerts))
            for (const Rect& a : raidhud::drawAlerts(ui, at(E::Alerts), k(E::Alerts), alertLines())) hudRects_.push_back(a);
        if (const std::string d = dangerText(); !d.empty()) {
            const Rect r = at(E::Danger);
            raidhud::drawDanger(ui, r, k(E::Danger), d);
            hudRects_.push_back(r);
        }
        {
            const Rect r = at(E::Status);
            statusRect_ = r;  // 2.77: the full raid's banner is drawn there again over the pause
            statusK_ = k(E::Status);
            Color c = kText;
            const std::string t = statusText(c);
            raidhud::drawLineBox(ui, r, k(E::Status), t, c, test_);
            hudRects_.push_back(r);
        }
        if (shown(E::Timer)) {
            // 2.72: the raid's chrono from the simulation's steps (the same on every computer; it
            // runs on during a pause: the world never stops).
            const Rect r = at(E::Timer);
            raidhud::drawTimer(ui, r, k(E::Timer), static_cast<float>(run_->tick()) / static_cast<float>(kSimHz),
                               static_cast<float>(run_->zoneTicks()) / static_cast<float>(kSimHz));
            hudRects_.push_back(r);
        }
        if (shown(E::LairPassage))
            if (const auto lp = lairPassage(w)) {
                // 2.78: in the zone before the lair, the distance to its passage, its arrow and its state.
                const Rect r = at(E::LairPassage);
                raidhud::drawLairPassage(ui, r, k(E::LairPassage), *lp);
                hudRects_.push_back(r);
            }
        if (shown(E::Remaining)) {
            // 2.75: « Ce qu'il reste à faire »: the zone's requirements, then the branch's zones to finish.
            const Rect r = at(E::Remaining);
            const raid::ZoneInfo* z = branch_->zone(run_->zone());
            raidhud::drawObjectives(ui, r, k(E::Remaining), "CE QU'IL RESTE À FAIRE · " + std::string(z ? z->name : ""), remainingLines(w));
            hudRects_.push_back(r);
        }
        if (shown(E::Contributions))
            if (const std::string t = contributionText(); !t.empty()) {
                const Rect r = at(E::Contributions);
                raidhud::drawLineBox(ui, r, k(E::Contributions), t, kText, false);
                hudRects_.push_back(r);
            }
        // 2.74 (phase 6): the Cœur d'Asterion: its shared health in its arena, what the branches did to
        // it at its door (before it wakes, the first seconds of the zone).
        placeBanner(w);     // 2.77.2: this player down / the group wiped (the notifications under it)
        drawNoticesHud(w);  // 2.75.5
        if (const raid::AsterionHeartRoutine* hr = run_->heartRoutine()) {
            if (shown(E::Heart)) {
                const Rect r = at(E::Heart);
                raidhud::drawHeartBar(ui, r, k(E::Heart), heartBarData(*hr));
                hudRects_.push_back(r);
            }
            if (shown(E::HeartEffects) && !run_->gigaboss().engaged() && run_->zoneTicks() <= static_cast<uint32_t>(20 * kSimHz)) {
                const Rect r = at(E::HeartEffects);
                raidhud::drawObjectives(ui, r, k(E::HeartEffects), "EFFETS DES BRANCHES SUR LE CŒUR", heartEffectLines(*hr));
                hudRects_.push_back(r);
            }
        }
    }

    /// The name of the key (keyboard / mouse, else gamepad) of an action, for the hints.
    static std::string actionKey(InputManager& in, Action a) {
        const auto& keys = in.bindings().keys[static_cast<int>(a)];
        const SDL_Scancode k = keys[0] != SDL_SCANCODE_UNKNOWN ? keys[0] : keys[1];
        return k != SDL_SCANCODE_UNKNOWN ? keyName(k) : std::string();
    }

    /// 2.77.2: « Q (LB) »: the key of an action on the keyboard, then its button on the gamepad (the
    /// style of the pad last seen).
    std::string keyNames(Action a) const {
        const InputManager& in = g_.app.input();
        const int i = static_cast<int>(a);
        const auto& keys = in.bindings().keys[i];
        const SDL_Scancode k = keys[0] != SDL_SCANCODE_UNKNOWN ? keys[0] : keys[1];
        const auto& pads = in.bindings().pad[i];
        const PadBinding& pb = pads[0].kind != PadBinding::Kind::None ? pads[0] : pads[1];
        const std::string key = keyName(k);
        return pb.kind == PadBinding::Kind::None ? key : key + " (" + padBindingName(pb, in.gamepadStyle()) + ")";
    }

    /// 2.72: the group panel's data (8 rows).
    /// 2.78: the distance indicator of the zone shown (the one before a lair: its passage through the lair's
    /// armory), for the player the camera follows: along the zone's route, else horizontally. None elsewhere.
    std::optional<raidhud::LairPassage> lairPassage(const GameWorld& w) const {
        if (!run_ || !branch_) return std::nullopt;
        const raid::Passage* to = nullptr;
        for (const raid::Passage& p : run_->rules().passages)
            if (p.via == raid::kLairArmory) to = &p;
        if (!to) return std::nullopt;
        const int seat = run_->cameraFollow() >= 0 && run_->cameraFollow() < w.teamSize() ? run_->cameraFollow() : std::max(0, local_);
        const Vec2 feet = seat < w.teamSize() && w.member(seat).presence == GameWorld::Presence::Down ? w.member(seat).soul : w.playerAt(seat).feet();
        const raid::RouteLeft left = raid::routeLeft(run_->rules().route, feet, to->rect.center());
        raidhud::LairPassage lp;
        lp.title = i18n::keep(i18n::tr("Passage vers l'antre") + (i18n::language() == Language::English ? ": " : " : ") + std::to_string(left.metres()) + " m");
        lp.dir = left.dir;
        lp.open = run_->passageOpen(*to);
        if (lp.open) {
            lp.state = "ouvert";
        } else if (to->lock.rfind("ambushes:", 0) == 0) {
            const raid::Branch::AmbushCount c = branch_->ambushes(std::atoi(to->lock.c_str() + 9), run_->mask());
            lp.state = i18n::keep(i18n::tr("fermé") + " — " + i18n::tr(raid::Branch::ambushesLeftText(c)));
        } else {
            lp.state = "fermé";
        }
        return lp;
    }

    /// 2.78: the players of the instance in `zone` (names, « Vous » first), « » when none.
    std::string playersIn(int zone) const {
        if (!run_) return {};
        std::string out;
        for (int i = 0; i < raid::kMaxInstancePlayers; ++i) {
            if (run_->zoneOf(i) != zone || !((run_->seatsIn(zone) >> i) & 1u)) continue;
            const std::string n = i == local_ ? i18n::tr("Vous") : seatName(i);
            out = i == local_ ? n + (out.empty() ? "" : ", " + out) : out + (out.empty() ? "" : ", ") + n;
        }
        return out;
    }
    /// 2.78: « Groupe : Vous, Lyra ici · Orion : Veine de magma » (empty when everyone is here).
    std::string groupWhere() const {
        if (!run_ || !branch_) return {};
        const std::vector<int> zones = run_->zonesLoaded();
        if (zones.size() < 2) return {};
        const char* colon = i18n::language() == Language::English ? ": " : " : ";
        std::string out = i18n::tr("Groupe") + colon + playersIn(run_->zone()) + " " + i18n::tr("ici");
        for (int z : zones) {
            if (z == run_->zone() || playersIn(z).empty()) continue;
            const raid::ZoneInfo* info = branch_->zone(z);
            out += " · " + playersIn(z) + colon + (info ? i18n::tr(info->name) : std::to_string(z));
        }
        return i18n::keep(out);
    }

    /// 2.78: the world a seat is in (each player changes zone alone), else `w`.
    const GameWorld& seatWorld(const GameWorld& w, int seat) const {
        const GameWorld* sw = run_ ? run_->worldOf(run_->zoneOf(seat)) : nullptr;
        return sw && seat < sw->teamSize() ? *sw : w;
    }
    /// 2.78: the name of the zone a seat is in.
    std::string seatZoneName(int seat) const {
        const raid::ZoneInfo* z = run_ && branch_ ? branch_->zone(run_->zoneOf(seat)) : nullptr;
        return z ? z->name : std::string();
    }

    raidhud::Group groupData(GameWorld& fw) {
        using P = GameWorld::Presence;
        raidhud::Group g;
        int held = 0, revivals = 0;
        for (int i = 0; i < fw.teamSize(); ++i) held += seatWorld(fw, i).member(i).presence != P::Vacant;
        if (run_)
            for (int z : run_->zonesLoaded()) revivals += run_->worldOf(z)->revivalsDone();
        g.title = "[" + entry_.clanTag + "]·" + std::to_string(entry_.group) + " · " + std::to_string(held) + " / 8";
        if (sim_) g.title = sim_->group(sim_->viewed()).name + " · " + std::to_string(held) + " / 8";  // 2.77
        for (int i = 0; i < fw.teamSize(); ++i) {
            const GameWorld& w = seatWorld(fw, i);
            const GameWorld::TeamMember& mb = w.member(i);
            const Player& p = w.playerAt(i);
            raidhud::GroupRow row;
            row.vacant = mb.presence == P::Vacant;
            row.name = seatName(i);
            row.color = seatColor(i, local_);
            row.playing = mb.presence == P::Playing;
            row.health = row.vacant ? 0.0f : static_cast<float>(std::max(0, p.health)) / static_cast<float>(std::max(1, p.maxHealth()));
            row.stateColor = kDim;
            switch (mb.presence) {
                case P::Playing: row.state = p.state() == PlayerState::Dead ? "tombe" : "en jeu"; row.stateColor = kGreen; break;
                case P::Down: row.state = "à terre"; row.stateColor = kDanger; break;
                case P::Standby:
                    row.state = "absent " + std::to_string((GameWorld::kStandbyLimitTicks - mb.standbyTicks + 119) / 120) + " s";
                    row.stateColor = kViolet;
                    break;
                case P::Gone: row.state = "parti"; break;
                case P::Vacant: row.state = "place libre"; break;
            }
            // 2.78: a player in another zone: its zone with its state.
            if (!row.vacant && run_ && run_->zoneOf(i) != run_->zone() && !seatZoneName(i).empty())
                row.state = i18n::keep(i18n::tr(row.state) + " · " + i18n::tr(seatZoneName(i)));
            g.rows.push_back(std::move(row));
        }
        // 2.75: as many revivals as wanted; the whole group down: eliminated from the zone (spectators).
        g.footer = run_ && run_->wiped() ? "GROUPE ÉLIMINÉ — mode spectateur"
                                         : "Réanimations illimitées (" + std::to_string(revivals) + ") · tous à terre : groupe éliminé";
        return g;
    }

    /// 2.72: what the maps show: the zone's objects, the enemies, the group, this player.
    std::vector<raidhud::Mark> mapMarks(GameWorld& w) {
        using raidhud::Icon;
        std::vector<raidhud::Mark> out;
        auto add = [&](Icon i, Vec2 at, Color tint = Color::white()) { out.push_back({i, at, tint}); };
        for (const raid::Passage& p : run_->rules().passages) add(run_->passageOpen(p) ? Icon::Exit : Icon::ExitLocked, p.rect.center());
        for (const auto& e : w.entities())
            if (auto* d = dynamic_cast<const DoorEntity*>(e.get()); d && !d->isOpen() && !e->removed) add(Icon::Door, e->box.center());
        for (const std::string& id : run_->rules().checkpoints)
            if (LevelEntity* e = w.entity(id)) {
                auto* cp = dynamic_cast<CheckpointEntity*>(e);
                add(id == run_->lastCheckpoint() || (cp && cp->activated()) ? Icon::CheckpointOn : Icon::Checkpoint, e->box.center());
            }
        for (size_t ci = 0; ci < run_->rules().coops.size(); ++ci)
            add(run_->coop(ci).done ? Icon::PlateDone : Icon::Plate, run_->rules().coops[ci].rect.center());
        if (run_->gigaboss().present())
            for (const auto& v : run_->gigaboss().vents()) add(v.closed ? Icon::VentClosed : Icon::Vent, v.area.center());
        for (const raid::ZoneObjective& zo : run_->rules().objectives) {
            const LevelObject* src = w.level().findObject(zo.source);
            if (!src) continue;
            const bool done = raid::Branch::done(run_->mask(), zo.index);
            add(done ? Icon::ObjectiveDone : branch_->objectives[static_cast<size_t>(zo.index)].optional ? Icon::ObjectiveOptional : Icon::Objective,
                src->rect.center());
        }
        for (const auto& e : w.enemies())
            if (e->alive() && !e->dormant()) add(e->def().isBoss ? Icon::Boss : Icon::Enemy, e->center());
        for (int i = 0; i < w.teamSize(); ++i) {
            if (i == local_ || w.member(i).presence != GameWorld::Presence::Playing) continue;
            add(Icon::Ally, w.playerAt(i).center(), seatColor(i, local_));
        }
        if (local_ >= 0 && local_ < w.teamSize()) add(Icon::Me, w.playerAt(local_).center());
        return out;
    }

    /// 2.72: the areas the magma floods (and whether it is coming, or there).
    std::vector<raidhud::MagmaArea> magmaAreas(GameWorld& w) {
        std::vector<raidhud::MagmaArea> out;
        const raid::Magma& m = run_->rules().magma;
        if (!m.present) return out;
        if (const LevelObject* mz = w.level().findObject(m.zone)) {
            const float in = run_->magmaIn();
            out.push_back({mz->rect, run_->magmaActive(), !run_->magmaActive() && in >= 0.0f && in <= m.warn + 0.01f});
        }
        return out;
    }

    /// 2.72: the large map over most of the screen (Action::RaidMap; the group keeps playing): the
    /// zone, the branch's zones, the whole legend. Dragged with the mouse (or its wheel) when the
    /// zone is wider than the view.
    void drawBigMap(GameWorld& w) {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        const float m = ui.s(40);
        const Rect r{m, m, ui.screenW() - m * 2, ui.screenH() - m * 2};
        if (in.mouseDown(SDL_BUTTON_LEFT) && r.contains(in.mousePos())) mapPan_ -= in.mouseDelta().x * minimap_.largeScale();
        if (in.wheel() != 0.0f && r.contains(in.mousePos())) mapPan_ -= in.wheel() * 160.0f;
        const Rect b = w.level().bounds;
        mapPan_ = std::clamp(mapPan_, -b.w, b.w);
        // 2.75: each zone with its state and, while it is not finished, what it still misses.
        std::vector<raidhud::ZoneChip> zones;
        for (const raid::ZoneGoals& g : raid::branchGoals(*branch_, run_->validated(), run_->localDone())) {
            raidhud::ZoneChip c;
            c.here = g.zone == run_->zone();
            c.name = g.name;
            const auto fell = retake_.find(g.zone);
            const bool retake = !g.finished && fell != retake_.end();
            c.state = g.finished ? "terminée" : retake ? "zone à reprendre" : c.here ? "vous êtes ici" : g.reachable ? "à finir" : "verrouillée";
            if (c.here && g.finished) c.state = "terminée · vous êtes ici";
            if (g.passage) c.state = c.here ? "zone de passage · vous êtes ici" : g.reachable ? "zone de passage" : "verrouillée";  // 2.78
            if (const std::string who = playersIn(g.zone); !who.empty()) c.state = i18n::keep(i18n::tr(c.state) + " · " + who);  // 2.78
            c.color = c.here ? kGold : g.finished ? kGreen : retake ? kDanger : g.reachable ? kSky : kDim;
            if (!g.finished) c.missing = (retake ? fell->second + " est tombé · " : std::string()) + g.missing() + (g.optional && !g.passage ? " (facultatif)" : "");
            zones.push_back(std::move(c));
        }
        const raid::ZoneInfo* z = branch_->zone(run_->zone());
        const std::string key = actionKey(in, Action::RaidMap);
        std::string hint = (key.empty() ? std::string("La touche de la carte") : key) +
                           " (ou Vue à la manette) : fermer la carte · le groupe continue à jouer · glisser à la souris : déplacer";
        if (sim_) hint += " · tout le raid et la carte du titan : pause › Groupes";  // 2.77
        const Vec2 focus = local_ >= 0 && local_ < w.teamSize() ? w.playerAt(local_).center() : b.center();
        minimap_.drawLarge(ui, g_.app.resources(), w, r, "CARTE · " + std::string(branch_->name) + " · " + std::string(z ? z->name : "?"),
                           dangerText(), kDanger, mapMarks(w), magmaAreas(w), focus, mapPan_, zones, hint);
    }

    // ------------------------------------------------------------- 2.68: the gigaboss

    /// The vents over the arena (the generated sheet: closed, open, announced, throwing magma),
    /// the hold of each (a ring), the carapace around the gigaboss while it protects it.
    void drawGigabossWorld(GameWorld& w, const WorldView& view) {
        const raid::Gigaboss& gb = run_->gigaboss();
        if (!gb.present()) return;
        Renderer& r = g_.ui.r();
        if (!vents_ || ventsPath_ != gb.rules()->fx) {  // 2.69: each gigaboss its own sheet (rules().fx)
            ventsPath_ = gb.rules()->fx;
            vents_ = g_.app.resources().animation(ventsPath_);
        }
        const bool fighting = gb.stage() == raid::Gigaboss::Stage::Carapace || gb.stage() == raid::Gigaboss::Stage::Exposed;
        for (int k = 0; k < static_cast<int>(gb.vents().size()); ++k) {
            const raid::Gigaboss::Vent& v = gb.vents()[static_cast<size_t>(k)];
            const char* clip = !fighting || v.closed ? "closed" : gb.jetting(k) ? "jet" : gb.jetWarning(k) ? "warn" : "open";
            const Rect world{v.area.center().x - 24.0f, v.area.bottom() - 144.0f, 48.0f, 144.0f};
            const Rect dst = view.toScreen(world);
            if (vents_ && vents_->texture)
                if (const AnimationClip* c = vents_->clip(clip)) {
                    const int n = std::max(1, c->frameCount());
                    const int f = static_cast<int>(clock_ * (std::string(clip) == "jet" ? 12.0f : 7.0f)) % n;
                    const Rect src = vents_->frameRect(*c, f);
                    r.drawTexture(*vents_->texture, &src, dst);
                }
            if (fighting && !v.closed && gb.holdRatio(k) > 0.0f) {
                const Vec2 c = view.toScreen({v.area.center().x, v.area.y - 30.0f});
                const float rad = 14.0f * view.scale;
                r.drawRing(c, rad, 3.0f * view.scale, Color{40, 30, 30, 200});
                r.drawArc(c, rad, 3.0f * view.scale, -1.5708f, -1.5708f + 6.2832f * gb.holdRatio(k), kGold);
            }
        }
        // « Poings de magma »: a mark under each player marked, then the fist falling on it.
        const AnimationClip* fistClip = vents_ ? vents_->clip("fist") : nullptr;
        const float warnSteps = gb.fistWarnSeconds() * static_cast<float>(kSimHz);
        for (const auto& f : gb.fists()) {
            const Vec2 c = view.toScreen({f.at.x, f.at.y - 2.0f});
            const float k = std::clamp(1.0f - static_cast<float>(f.left) / std::max(1.0f, warnSteps), 0.0f, 1.0f);
            const float rad = gb.rules()->fistWidth * 0.5f * view.scale;
            r.drawRing(c, rad, 2.0f * view.scale, Color{255, 90, 40, static_cast<uint8_t>(120 + 120 * k)});
            r.drawArc(c, rad * 0.7f, 3.0f * view.scale, -1.5708f, -1.5708f + 6.2832f * k, Color{255, 200, 120, 230});
            if (fistClip && vents_->texture && f.left <= static_cast<int>(0.5f * static_cast<float>(kSimHz))) {
                // The last half second: the fist falls from the sky onto the mark.
                const float fall = std::clamp(static_cast<float>(f.left) / (0.5f * static_cast<float>(kSimHz)), 0.0f, 1.0f);
                // 2.69: a blow out of the ground (Dreadfang's claws, Heartbreaker's ribs) rises in place.
                const Rect world{f.at.x - 24.0f, f.at.y - 144.0f - (gb.rules()->fistRise ? 0.0f : 220.0f * fall), 48.0f, 144.0f};
                const Rect src = vents_->frameRect(*fistClip, static_cast<int>(clock_ * 10.0f) % std::max(1, fistClip->frameCount()));
                r.drawTexture(*vents_->texture, &src, view.toScreen(world));
            }
        }
        if (gb.stage() == raid::Gigaboss::Stage::Carapace)
            if (const Enemy* b = gb.boss(w); b && b->alive()) {
                const Rect sb = view.toScreen(b->box);
                const float pulse = 0.5f + 0.5f * std::sin(clock_ * 4.0f);
                r.drawRing(sb.center(), std::max(sb.w, sb.h) * 0.62f, 3.0f * view.scale, Color{255, 210, 122, static_cast<uint8_t>(110 + 80 * pulse)});
            }
        drawRoutineMarks(w, view);
    }

    /// 2.69 (phase 4B): what the routine of Voidworm, Graviton or Titanus shows over the arena: its
    /// zones (a sector turning over, the acid coming, the stomach), its plates (a clip of its sheet and
    /// the ring of their hold), the marks of its blows, its shockwaves; their short labels measured.
    void drawRoutineMarks(GameWorld& w, const WorldView& view) {
        const raid::Gigaboss& gb = run_->gigaboss();
        const raid::GigabossRoutine* rt = gb.routine();
        if (!rt) return;
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        std::vector<raid::BossMark> marks;
        rt->marks(w, gb, marks);
        const float pulse = 0.5f + 0.5f * std::sin(clock_ * 6.0f);
        auto clipAt = [&](const std::string& name, const Rect& worldRect, float fps, bool flip = false) {
            if (name.empty() || !vents_ || !vents_->texture) return;
            const AnimationClip* c = vents_->clip(name);
            if (!c) return;
            const Rect src = vents_->frameRect(*c, static_cast<int>(clock_ * fps) % std::max(1, c->frameCount()));
            r.drawTexture(*vents_->texture, &src, view.toScreen(worldRect), Color::white(), BlendMode::Alpha, flip);
        };
        for (const raid::BossMark& m : marks) {
            const Rect sr = view.toScreen(m.area);
            const Color c = m.color;
            using K = raid::BossMark::Kind;
            switch (m.kind) {
                case K::Zone: {
                    const uint8_t a = static_cast<uint8_t>(m.strong ? 60 + 40 * pulse : 25 + 50 * m.progress);
                    r.fillRect(sr, Color{c.r, c.g, c.b, a});
                    r.drawRect(sr, Color{c.r, c.g, c.b, static_cast<uint8_t>(m.strong ? 230 : 150)}, std::max(1.0f, 2.0f * view.scale));
                    if (!m.strong && m.progress > 0.0f)  // the warning fills up from the floor
                        r.fillRect({sr.x, sr.bottom() - sr.h * m.progress, sr.w, std::max(1.0f, ui.s(3))}, Color{c.r, c.g, c.b, 220});
                    break;
                }
                case K::Plate: {
                    clipAt(m.clip, {m.area.center().x - 24.0f, m.area.bottom() - 144.0f, 48.0f, 144.0f}, 8.0f);
                    if (m.progress > 0.0f) {
                        const Vec2 cc = view.toScreen({m.area.center().x, m.area.y - 34.0f});
                        const float rad = 14.0f * view.scale;
                        r.drawRing(cc, rad, 3.0f * view.scale, Color{40, 30, 30, 200});
                        r.drawArc(cc, rad, 3.0f * view.scale, -1.5708f, -1.5708f + 6.2832f * std::clamp(m.progress, 0.0f, 1.0f), c);
                    }
                    break;
                }
                case K::Target: {
                    const Vec2 cc = sr.center();
                    const float rad = std::max(sr.w, sr.h) * 0.5f;
                    r.drawRing(cc, rad, 2.0f * view.scale, Color{c.r, c.g, c.b, static_cast<uint8_t>(m.strong ? 160 + 90 * pulse : 140)});
                    r.drawArc(cc, rad * 0.72f, 3.0f * view.scale, -1.5708f, -1.5708f + 6.2832f * std::clamp(m.progress, 0.0f, 1.0f), c);
                    if (!m.clip.empty()) clipAt(m.clip, {m.area.center().x - 24.0f, m.area.bottom() - 144.0f, 48.0f, 144.0f}, 8.0f);
                    break;
                }
                case K::Wave:
                    clipAt(m.clip, {m.area.center().x - 24.0f, m.area.bottom() - 144.0f, 48.0f, 144.0f}, 12.0f, m.flip);
                    break;
                case K::Beam: {
                    // 2.69: a gaze (thin while it follows, bright once frozen, thick when it fires) or a chain.
                    const Vec2 a = view.toScreen(m.area.pos()), e = view.toScreen(m.to);
                    const float thick = std::max(1.0f, m.width * view.scale);
                    const uint8_t alpha = static_cast<uint8_t>(m.strong ? 230 : 90 + 120 * std::clamp(m.progress, 0.0f, 1.0f));
                    r.drawLine(a, e, Color{c.r, c.g, c.b, alpha}, thick, BlendMode::Additive);
                    if (m.strong) {
                        r.drawLine(a, e, Color{255, 246, 255, 230}, thick * 0.35f, BlendMode::Additive);
                        r.fillCircle(e, thick * 0.9f, Color{c.r, c.g, c.b, 200}, BlendMode::Additive);
                    } else if (m.progress > 0.0f) {
                        r.drawRing(e, (22.0f - 10.0f * std::clamp(m.progress, 0.0f, 1.0f)) * view.scale, 2.0f * view.scale, Color{c.r, c.g, c.b, 200});
                    }
                    break;
                }
                case K::Cone: {
                    // 2.69: Skullmaw's eye-light in its darkness: a cone, and the edges of the platforms it touches.
                    const Vec2 apex = m.area.pos();
                    const Vec2 d = m.to - apex;
                    const float len = std::max(1.0f, d.length()), ang = std::atan2(d.y, d.x), half = m.width;
                    const Vec2 l{apex.x + std::cos(ang - half) * len, apex.y + std::sin(ang - half) * len};
                    const Vec2 rr{apex.x + std::cos(ang + half) * len, apex.y + std::sin(ang + half) * len};
                    r.fillTriangle(view.toScreen(apex), view.toScreen(l), view.toScreen(rr), Color{c.r, c.g, c.b, 46}, BlendMode::Additive);
                    const Rect seen{std::min({apex.x, l.x, rr.x}) - 32.0f, std::min({apex.y, l.y, rr.y}) - 32.0f,
                                    std::max({apex.x, l.x, rr.x}) - std::min({apex.x, l.x, rr.x}) + 64.0f,
                                    std::max({apex.y, l.y, rr.y}) - std::min({apex.y, l.y, rr.y}) + 64.0f};
                    w.physics().forEachIn(seen, [&](const Collider& col) {
                        if (!col.enabled) return;
                        const float y = col.box.y;
                        for (float x = std::max(col.box.x, seen.x); x < std::min(col.box.right(), seen.right()); x += 16.0f) {
                            const float dx = x + 8.0f - apex.x, dy = y - apex.y, dist = std::sqrt(dx * dx + dy * dy);
                            if (dist > len || dist < 1.0f) continue;
                            float diff = std::atan2(dy, dx) - ang;
                            while (diff > 3.14159f) diff -= 6.28318f;
                            while (diff < -3.14159f) diff += 6.28318f;
                            if (std::abs(diff) > half) continue;
                            const float k = 1.0f - std::abs(diff) / std::max(0.01f, half);
                            r.drawLine(view.toScreen({x, y}), view.toScreen({std::min(x + 16.0f, col.box.right()), y}),
                                       Color{236, 220, 255, static_cast<uint8_t>(90 + 150 * k)}, 3.0f * view.scale, BlendMode::Additive);
                        }
                    });
                    break;
                }
                case K::Arrow: {
                    const float aw = sr.w, ah = sr.h;
                    const float lift = (m.strong ? std::fmod(clock_ * 1.6f, 1.0f) : pulse * 0.3f) * ah * 0.4f;
                    const Color ac{c.r, c.g, c.b, static_cast<uint8_t>(m.strong ? 230 : 150)};
                    r.fillTriangle({sr.x, sr.y + aw * 0.7f - lift}, {sr.right(), sr.y + aw * 0.7f - lift}, {sr.center().x, sr.y - lift}, ac);
                    r.fillRect({sr.center().x - aw * 0.18f, sr.y + aw * 0.7f - lift, aw * 0.36f, ah * 0.5f}, ac);
                    break;
                }
            }
            if (!m.label.empty()) {
                // Its label over it, measured (never wider than the mark, nor the screen).
                const float room = std::clamp(std::max(sr.w, ui.s(90)), ui.s(60), ui.screenW() * 0.4f);
                const float lw = std::min(textW(ui, m.label, 12, FontFace::Bold) + ui.s(14), room);
                const Rect pill{sr.center().x - lw * 0.5f, sr.y - ui.s(26), lw, ui.s(22)};
                r.fillRoundedRect(pill, ui.s(9), Color{16, 12, 30, 210});
                fit(ui, m.label, pill.center(), 12, pill.w - ui.s(10), c, FontFace::Bold, Align::Center);
            }
        }
    }

    /// 2.68 (phase 3): the camera of a gigaboss's arena (display only; every arena declared in
    /// metadata.raid.arena, the seven branches' lairs alike), fed with what threatens this player.
    void updateArenaCamera(GameWorld& w, float dt) {
        const raid::Gigaboss& gb = run_->gigaboss();
        const raid::ArenaInfo& arena = run_->rules().arena;
        raid::ArenaCamera::Frame f;
        if (arena.present && arena.rect.w > 0.0f && local_ >= 0 && local_ < w.teamSize()) {
            // 2.77.2: this player down: what they watch (their soul, or the player chosen), never their body.
            const bool down = w.member(local_).presence == GameWorld::Presence::Down;
            const int focus = down && downWatch_ >= 0 && downWatch_ < w.teamSize() ? downWatch_ : local_;
            const Player& me = w.playerAt(focus);
            f.arena = arena.rect;
            f.level = w.level().bounds;
            f.me = down && focus == local_ ? w.member(local_).soul + Vec2{0.0f, -14.0f} : me.center();
            f.meInArena = w.member(local_).presence != GameWorld::Presence::Vacant && f.arena.contains(f.me) &&
                          !(gb.present() && gb.stage() == raid::Gigaboss::Stage::Absent);
            if (const Enemy* b = gb.boss(w); b && b->alive()) f.boss = b->box;
            for (const auto& fist : gb.fists()) f.threats.push_back(fist.at);
            if (gb.routine()) gb.routine()->threats(gb, f.threats);  // 2.69 (phase 4B)
            for (int k = 0; k < static_cast<int>(gb.vents().size()); ++k)
                if (gb.jetWarning(k) || gb.jetting(k)) f.threats.push_back(gb.vents()[static_cast<size_t>(k)].area.center());
            for (int i = 0; i < w.teamSize(); ++i)
                if (i != focus && w.member(i).presence == GameWorld::Presence::Playing) f.allies.push_back(w.playerAt(i).center());
        }
        arenaCam_.update(f, dt, g_.app.renderer().width(), g_.app.renderer().height());  // 2.70: also before any UI frame
    }

    /// 2.68: the title of the arena while its whole is shown (the intro shot).
    void drawArenaIntro() {
        if (!arenaCam_.intro() || !run_->gigaboss().present()) return;
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        const float bw = std::min(ui.s(640), W * 0.5f), bh = ui.s(96);
        const Rect box{(W - bw) * 0.5f, H * 0.3f, bw, bh};
        const float t = arenaCam_.introProgress();
        const float alpha = std::clamp(std::min(t * 4.0f, (1.0f - t) * 4.0f), 0.0f, 1.0f);
        ui.r().fillRoundedRect(box, ui.s(14), Color{20, 8, 6, static_cast<uint8_t>(200 * alpha)});
        const std::string name = run_->gigaboss().rules()->name;
        fit(ui, name, {box.center().x, box.y + ui.s(36)}, 34, box.w - ui.s(40), kGold.withAlphaF(alpha), FontFace::Title, Align::Center, true);
        fit(ui, "Gigaboss · l'arène entière, puis votre vue de combat", {box.center().x, box.y + ui.s(74)}, 15, box.w - ui.s(40),
            kText.withAlphaF(alpha), FontFace::Body, Align::Center);
    }

    // ------------------------------------------------------------- 2.74 (phase 6): the Cœur d'Asterion

    /// « 63,2 % » from ten-thousandths.
    static std::string heartPct(int units) {
        units = std::clamp(units, 0, raid::kHeartUnits);
        return std::to_string(units / 100) + "," + std::to_string((units % 100) / 10) + " %";
    }

    /// Its shared health (RaidHudElement::Heart): what is left for every group, this group's part, the
    /// others', its phase, the parts of the titan fallen.
    raidhud::HeartBar heartBarData(const raid::AsterionHeartRoutine& hr) const {
        raidhud::HeartBar b;
        const bool won = run_->heartWon();
        const int left = won ? 0 : hr.remaining();
        const float units = static_cast<float>(raid::kHeartUnits);
        b.left = static_cast<float>(left) / units;
        b.own = static_cast<float>(hr.own()) / units;
        b.others = static_cast<float>(hr.others()) / units;
        b.fallen = won;
        b.phase = won ? "TOMBÉ" : "phase " + std::to_string(std::clamp(run_->gigaboss().phase(), 1, 4)) + " / 4";
        b.title = "CŒUR D'ASTERION · " + heartPct(left) + " · vie partagée par tous les groupes";
        int fallen = 0;
        for (int p = 0; p < raid::AsterionHeartRoutine::PartCount; ++p) fallen += hr.down(p) ? 1 : 0;
        b.sub = "votre groupe " + heartPct(hr.own()) + " · les autres groupes " + heartPct(hr.others()) + " · parties du titan tombées " +
                std::to_string(fallen) + " / 7" + (hr.relics() > 0 ? " · reliques " + std::to_string(hr.relics()) : std::string());
        return b;
    }

    /// Its briefing at its arena's door (RaidHudElement::HeartEffects, before it wakes): what each part
    /// of the titan, fallen or standing, does to it.
    static std::vector<raidhud::Line> heartEffectLines(const raid::AsterionHeartRoutine& hr) {
        static const char* kStanding[] = {"ses lances stellaires suivent chaque joueur", "ses balayages traversent les étages", "sa pluie de magma tombe",
                                          "une cage d'os le garde dès sa 1re phase", "ses engeances viennent en renfort", "ses pièges de gravité s'ouvrent",
                                          "ses charges stellaires sont rapides"};
        static const char* kNames[] = {"Crâne", "Bras gauche", "Bras droit", "Torse", "Abdomen", "Jambe gauche", "Jambe droite"};
        std::vector<raidhud::Line> lines;
        for (int p = 0; p < raid::AsterionHeartRoutine::PartCount; ++p) {
            if (hr.down(p)) lines.push_back({"◆", raid::AsterionHeartRoutine::partEffect(p), kGreen});
            else lines.push_back({"!", std::string(kNames[p]) + " debout : " + kStanding[p], kOrange});
        }
        return lines;
    }

    /// 2.68: arrows on the border toward what the view does not show: the boss (its weak points when
    /// bare), the allies (their colours); 2.72: the enemies already met and the ambush's, exactly as
    /// the solo levels draw them (EnemyArrowsDraw). Kept off the HUD; the raid layout's toggle.
    void drawArrows(GameWorld& w, const WorldView& view) {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        const Rect screen{0.0f, 0.0f, W, H};
        // Who is on screen is noted whatever the setting (turned on again, the arrows know them).
        const EnemyArrows::Targets away = EnemyArrows::targets(w, view, screen, metEnemies_);
        if (!g_.settings.gameplay.raidHud.arrows || bigMap_ || arenaCam_.intro()) return;
        const EnemyArrows::Look usual = EnemyArrows::look(false), fight = EnemyArrows::look(true);
        auto tex = g_.app.resources().texture(usual.texture);
        auto ambushTex = g_.app.resources().texture(fight.texture);
        if (!tex || !ambushTex) return;
        EnemyArrows::Layout l = EnemyArrows::standardLayout(ui, screen, std::max<size_t>({away.usual.size(), away.ambush.size(), 8}));
        l.keepOut = hudRects_;
        auto draw = [&](const std::vector<Rect>& targets, const Texture& arrow, Color tint) {
            if (targets.empty()) return;
            const float fs = std::max(22.0f, 13.0f / ui.s(1.0f));
            for (const EnemyArrows::Arrow& a : EnemyArrows::layout(l, targets)) {
                ui.r().drawTexture(arrow, nullptr, a.box, tint, BlendMode::Alpha, false, false, a.angle * 57.29578f);
                if (a.count > 1) {
                    const std::string n = "×" + std::to_string(a.count);
                    ui.r().fillRoundedRect(a.badge, a.badge.h * 0.5f, Color{8, 12, 26, 220});
                    ui.label(n, a.badge.center(), ui.fitFont(n, FontFace::Bold, fs, a.badge.w - ui.s(10), 0.6f), tint, Align::Center, FontFace::Bold,
                             VAlign::Middle);
                }
                l.keepOut.push_back(a.area);
            }
        };
        const auto off = [&](const Rect& r) { return !view.toScreen(r).intersects(screen); };
        // The boss, or its weak points while they are bare.
        const raid::Gigaboss& gb = run_->gigaboss();
        std::vector<Rect> bossT;
        // 2.69: its weak points by their uids (several kinds: Skullmaw's eyes and crystal), and what
        // its routine wants found (plates, claws, a stomach).
        std::vector<uint32_t> wanted;
        if (gb.stage() == raid::Gigaboss::Stage::Exposed) wanted = gb.weakUids();
        if (gb.routine()) gb.routine()->targets(wanted);
        bool any = false;
        for (uint32_t u : wanted)
            if (const Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive()) {
                any = true;
                if (off(e->box)) bossT.push_back(view.toScreen(e->box));
            }
        if (!any)
            if (const Enemy* b = gb.boss(w); b && b->alive() && off(b->box)) bossT.push_back(view.toScreen(b->box));
        draw(bossT, *tex, kGold);
        // The allies, each in their colour; 2.77: the soul of an ally down, where to revive them (pulsing).
        for (int i = 0; i < w.teamSize(); ++i) {
            const GameWorld::TeamMember& m = w.member(i);
            if (i == local_ && m.presence != GameWorld::Presence::Down) continue;  // 2.77.2: this player down: their own soul too
            if (m.presence == GameWorld::Presence::Down) {
                const Rect soul{m.soul.x - 8.0f, m.soul.y - 28.0f, 16.0f, 28.0f};
                if (!off(soul)) continue;
                const Color c = seatColor(i, local_);
                const float pulse = 0.5f + 0.5f * std::sin(w.time() * 5.0f);
                draw({view.toScreen(soul)}, *ambushTex, Color{c.r, c.g, c.b, static_cast<uint8_t>(150 + 105 * pulse)});
                continue;
            }
            if (m.presence != GameWorld::Presence::Playing || !off(w.playerAt(i).box)) continue;
            draw({view.toScreen(w.playerAt(i).box)}, *ambushTex, seatColor(i, local_));
        }
        // The enemies: the ambush's first (violet), then those already met (amber), as in solo.
        EnemyArrows::draw(ui, g_.app.resources(), l, away, nullptr);
    }

    /// 2.68 (phase 3): the zone's co-op plates over the world (gold until done) and the players each
    /// needs (metadata.raid.coop, the same plates as the objectives).
    void drawSwitches(GameWorld& w, const WorldView& view) {
        (void)w;
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        for (size_t k = 0; k < run_->rules().coops.size(); ++k) {
            const raid::RaidRun::CoopView v = run_->coop(k);
            const Rect sr = view.toScreen(run_->rules().coops[k].rect);
            const float ph = std::max(ui.s(4), std::min(sr.h * 0.25f, ui.s(10)));
            r.fillRect({sr.x, sr.bottom() - ph, sr.w, ph}, v.done ? Color{124, 240, 168, 160} : Color{255, 210, 122, 150});
            if (v.done || v.need <= 0) continue;
            const std::string t = std::to_string(std::min(v.on, v.need)) + " / " + std::to_string(v.need);
            const float pw = std::min(textW(ui, t, 13, FontFace::Bold) + ui.s(16), std::max(ui.s(40), sr.w));
            const Rect pill{sr.center().x - pw * 0.5f, sr.y - ui.s(30), pw, ui.s(24)};
            r.fillRoundedRect(pill, ui.s(10), Color{16, 22, 46, 220});
            fit(ui, t, pill.center(), 13, pill.w - ui.s(10), v.on >= v.need ? kGreen : kGold, FontFace::Bold, Align::Center, true);
        }
    }

    /// This player's contributions (the same numbers every computer counts); empty without any.
    std::string contributionText() {
        const raid::Contribution* me = nullptr;
        for (const raid::Contribution& c : run_->contributions().records())
            if (c.seat == local_) me = &c;  // the latest record of this seat
        if (!me) return {};
        std::string t = "Dégâts " + std::to_string(me->damage / 10) + " · Mécanismes " + std::to_string(me->vents) + " · Sauvetages " +
                        std::to_string(me->rescues) + " · Dégâts évités " + std::to_string(me->avoided / 10);
        if (me->weakPoints > 0) t += " · Points faibles " + std::to_string(me->weakPoints);
        return t;
    }

    /// The gigaboss's lines of the objectives (its stage, the vents, the weak points, the allies down).
    void gigabossLines(GameWorld& w, std::vector<std::string>& marks, std::vector<std::string>& texts, std::vector<Color>& colors) {
        const raid::Gigaboss& gb = run_->gigaboss();
        if (!gb.present()) return;
        using S = raid::Gigaboss::Stage;
        const std::string name = gb.rules()->name;
        const int total = static_cast<int>(gb.vents().size());
        auto add = [&](std::string m, std::string t, Color c) {
            marks.push_back(std::move(m));
            texts.push_back(std::move(t));
            colors.push_back(c);
        };
        if (const raid::GigabossRoutine* rt = gb.routine();
            rt && (gb.stage() == S::Fight || gb.stage() == S::Carapace || gb.stage() == S::Exposed)) {
            // 2.69 (phase 4B): Voidworm, Graviton, Titanus: their routine's own lines.
            std::vector<raid::BossLine> own;
            rt->lines(w, gb, own);
            if (own.size() > 4) own.resize(4);  // the most pressing first: the panel stays short (the alerts below it)
            for (raid::BossLine& l : own) add(std::move(l.mark), std::move(l.text), l.color);
        } else
        switch (gb.stage()) {
            case S::Absent: add("OK", name + " est déjà tombé : " + gb.rules()->goneText + ".", kGreen); break;
            case S::Waiting: add("→", "Entrez dans l'arène : " + name + " se réveille.", Color{255, 176, 74, 255}); break;
            case S::Fight:
                add("→", "Vaincre " + name + " · phase " + std::to_string(std::max(1, gb.phase())) + " / 3 : esquivez ses coups annoncés.", Color{255, 176, 74, 255});
                break;
            case S::Carapace:
                add(std::to_string(gb.ventsClosed()) + " / " + std::to_string(total),
                    "Fermer les évents de la main (tenir " + std::to_string(static_cast<int>(std::lround(gb.rules()->hold))) + " s chacun) : la carapace tombe quand les " +
                        std::to_string(total) + " sont fermés ensemble.",
                    kGold);
                break;
            case S::Exposed:
                add(std::to_string(static_cast<int>(std::ceil(gb.exposedLeft()))) + " s",
                    "Frapper le point faible : la carapace est brisée · points faibles " + std::to_string(gb.weakTotal() - gb.weakLeft()) + " / " +
                        std::to_string(gb.weakTotal()),
                    kGreen);
                break;
            case S::Fallen: add("OK", name + " est vaincu !", kGreen); break;
        }
        for (int i = 0; i < w.teamSize(); ++i)
            if (w.member(i).presence == GameWorld::Presence::Down) {
                add("+", "Relever " + seatName(i) + " (Interagir maintenu " + std::to_string(static_cast<int>(GameWorld::kReviveSeconds)) +
                             " s près de son âme) · réanimations illimitées · si tout le groupe tombe, il est éliminé",
                    Color{255, 138, 138, 255});
                break;
            }
    }

    /// 2.75.1: the whole group down at this step (every computer of the instance): eliminated from the zone.
    /// 2.75.5: a notification (the spectator's band says the rest), never a card.
    void groupWiped() {
        notify("GROUPE ÉLIMINÉ", "Tout le groupe est à terre : la zone attend un groupe de renfort. Vous devenez spectateurs.", kDanger,
               raidmenu::Section::Group, "sounds/player_death.wav");
        branchAt_ = clock_;  // the branch's map again now (« zone à reprendre »)
    }

    /// 2.75: a zone finished while the instance was in it (the same step on every computer), with its
    /// sound; 2.75.5: a short notification (its « Tableau des contributions » in the raid's menu), which
    /// takes no key: the players keep the controls, over the void too.
    void zoneDone(int zone) {
        if (run_->finishes().empty()) return;
        const raid::ZoneFinish& f = run_->finishes().back();
        const raid::ZoneInfo* z = branch_->zone(zone);
        notify("ZONE TERMINÉE · " + std::string(z ? z->name : "?"),
               f.byUs ? std::string("Par votre groupe : son tableau des contributions est dans le menu du raid.")
                      : std::string("Par un autre groupe (état validé par le serveur) : son tableau est dans le menu du raid."),
               kGreen, raidmenu::Section::Zones, "sounds/boss_phase_clear.wav", true);
    }

    /// 2.75.5: a notification (its sound, the journal with its time in the raid).
    void notify(std::string title, std::string text, Color c, raidmenu::Section section, const char* sound, bool table = false) {
        raidmenu::Notice n;
        n.title = std::move(title);
        n.text = std::move(text);
        n.color = c;
        n.section = section;
        n.table = table;
        n.raidSeconds = run_ ? static_cast<float>(run_->tick()) / static_cast<float>(kSimHz) : 0.0f;
        overlay_.notices.push(std::move(n));
        if (sound) g_.app.audio().playSound(sound, 0.85f);
    }

    /// 2.75.5: the gigaboss of the zone fell (a part of the titan): a notification; « Résultats du raid »
    /// waits in the menu's « Titan » (2.68: it was a card that took the keys).
    void gigabossFell() {
        const std::string name = run_->gigaboss().rules() ? run_->gigaboss().rules()->name : "Le gigaboss";
        const bool heart = run_->heartBranch();
        const std::string fell = run_->gigaboss().rules() ? run_->gigaboss().rules()->fallText : std::string();
        fallSeen_ = !test_;
        // 2.74 (phase 6): the Cœur d'Asterion falls here once its shared health is gone; the victory is the
        // server's (every group's damage summed): the menu's « Titan » says so (2.68: the card did).
        fallMore_ = test_ ? std::string()
                    : !heart ? std::string("Les résultats comptent dès que la majorité de votre instance les a confirmés au serveur.")
                    : run_->heartWon() ? std::string("Victoire validée par le serveur pour tous les clans : vos récompenses vous attendent dans CLANS › RAIDS.")
                                       : std::string("La victoire est validée quand le serveur a reçu de tous les groupes de quoi éteindre sa vie partagée : patientez.");
        notify(heart ? (run_->heartWon() ? std::string("ASTERION EST TOMBÉ") : std::string("LE CŒUR D'ASTERION CÈDE")) : name + " EST VAINCU",
               fell.empty() ? (test_ ? std::string("Test : rien n'est compté.") : std::string("Résultats du raid : menu du raid, section Titan."))
                            : fell + (test_ ? "" : " · Résultats du raid : menu du raid, section Titan."),
               kGold, raidmenu::Section::Titan, "sounds/medal.wav");
    }

    /// 2.75.5: what changed in the branch at this step, told once: another zone finished (validated), a
    /// link of the titan on here. What was so when this game came is not news (`tell`: in play).
    void watchBranch(bool tell) {
        uint32_t links = 0;
        for (const raid::EffectInfo& e : branch_->effects)
            if (raid::Branch::on(run_->mask(), e)) links |= 1u << e.bit;
        const uint32_t zones = run_->zonesFinished();
        if (watchedRun_ != run_) {
            watchedRun_ = run_;
            seenZones_ = zones;
            seenLinks_ = links;
            return;
        }
        if (tell) {
            for (const raid::ZoneInfo& z : branch_->zones)
                if (z.id > 0 && z.id < 32 && z.id != run_->zone() && ((zones & ~seenZones_) >> z.id) & 1u)
                    notify("BRANCHE · Zone terminée : " + z.name, "Validée par le serveur pour toute la branche.", kGreen, raidmenu::Section::Zones,
                           "sounds/checkpoint.wav");
            for (const raid::EffectInfo& e : branch_->effects)
                if (raid::Branch::on(links & ~seenLinks_, e)) notify("TITAN", e.label, kViolet, raidmenu::Section::Titan, "sounds/portal.wav");
        }
        seenZones_ = zones;
        seenLinks_ = links;
    }

    /// 2.75.5: where the menu opens: on the notification shown, else where the player left it.
    raidmenu::Section menuSectionNow() const { return overlay_.notices.latestSection(); }

    /// 2.75.5: the menu's key as the device in use names it (« Tab », « L3 »), for its hints.
    std::string menuKeyName() {
        InputManager& in = g_.app.input();
        const int a = static_cast<int>(Action::RaidMenu);
        if (in.lastDevice() == InputDevice::Gamepad)
            for (const PadBinding& p : in.bindings().pad[a])
                if (p.kind != PadBinding::Kind::None) return padBindingName(p, in.gamepadStyle());
        return actionKey(in, Action::RaidMenu);
    }

    /// 2.75.5: the menu's button and the notifications (RaidHudElement::Notices), never under a card, the
    /// large map, the menu or a big message of the level (they wait their turn: their time starts when
    /// they are drawn).
    void drawNoticesHud(GameWorld& w) {
        UIContext& ui = g_.ui;
        menuButtonRect_ = {};  // 2.77.4: drawn below, or not this frame
        bool bigMessage = false;
        for (const WorldMessage& m : w.messages()) bigMessage = bigMessage || (m.style != WorldMessage::Style::Hint && m.age < m.duration);
        if (paused_ || ended_ || bigMap_ || overlay_.menu.isOpen() || reconnect_.active() || bigMessage || g_.app.topScene() != this) return;
        const RaidHudLayout& l = g_.settings.gameplay.raidHud;
        Rect r = raidhud::area(ui, l, RaidHudElement::Notices, hudAreas_);
        // 2.77.2: under the banner of this player down / of the group wiped when they meet, never over it.
        if (!bannerLayout_.panel.empty()) r = downed::below(r, bannerLayout_.panel, ui.s(10), hudTaken());
        std::vector<const raidmenu::Notice*> list;
        std::vector<float> alpha;
        if (!arenaCam_.intro())  // the arena's title over the middle: they wait
            for (const raidmenu::Notice* n : overlay_.notices.front()) {
                list.push_back(n);
                alpha.push_back(raidmenu::Notices::alpha(*n, clock_));
            }
        size_t drawn = 0;
        if (raidmenu::drawNotices(ui, r, raidhud::scaleOf(l, RaidHudElement::Notices), list, alpha, menuKeyName(), true, &drawn, &menuButtonRect_))
            overlay_.menu.open(menuSectionNow());
        overlay_.notices.drawn(drawn, clock_);
        hudRects_.push_back(r);
    }

    /// 2.75: the table of a zone finished (raid::ZoneFinish) as the HUD draws it.
    raidhud::ZoneTable zoneTable(const raid::ZoneFinish& f) {
        raidhud::ZoneTable t;
        const raid::ZoneInfo* z = branch_->zone(f.zone);
        t.title = "TABLEAU DES CONTRIBUTIONS · " + std::string(z ? z->name : "?");
        const uint32_t hz = static_cast<uint32_t>(kSimHz), secs = (f.tick + hz / 2) / hz;
        t.sub = std::string(f.byUs ? "Zone terminée par votre groupe" : "Zone terminée par un autre groupe (état validé par le serveur)") + " · raid à " +
                std::to_string(secs / 60) + " min " + std::to_string(secs % 60) + " s · comptée par la simulation, la même sur tous les jeux du groupe";
        for (const raid::ZoneContribution& c : f.rows) {
            raidhud::ZoneRow row;
            bool later = false;  // a newer holder of the same seat: this row is a player who left
            for (const raid::ZoneContribution& o : f.rows) later = later || (o.seat == c.seat && o.from > c.from);
            row.name = later ? "Joueur parti (place " + std::to_string(c.seat + 1) + ")" : seatName(c.seat);
            row.color = seatColor(c.seat, local_);
            row.me = !later && c.seat == local_;
            row.bossDamage = static_cast<int>(c.bossDamage / 10);
            row.damage = static_cast<int>(c.damage / 10);
            row.support = c.support;
            row.mechanisms = c.mechanisms;
            row.objectives = c.objectives;
            t.rows.push_back(std::move(row));
        }
        return t;
    }

    /// 2.75.5: the raid's menu over the combat (opened by the player; the game goes on under it).
    void drawMenu(GameWorld& w) {
        // 2.78: the table open over the menu takes the whole screen (the menu's buttons under it never answer).
        const raidmenu::MenuAction act = contribOpen_ ? raidmenu::MenuAction{} : raidmenu::drawMenu(g_.ui, overlay_.menu, menuData(w));
        if (act.close) overlay_.menu.close();
        if (act.table && !contribOpen_) {
            contribOpen_ = true;
            contribView_ = contrib::View{};
        }
        if (contribOpen_ && overlay_.menu.isOpen()) {
            // 2.78: the full table over the menu (the game goes on: this player's inputs stay neutral).
            const float m = g_.ui.s(24);
            if (contrib::draw(g_.ui, {m, m, g_.ui.screenW() - m * 2, g_.ui.screenH() - m * 2}, contribModel(), contribView_)) contribOpen_ = false;
        }
        if (act.results && !test_ && !entry_.eventId.empty()) {
            overlay_.menu.close();
            g_.app.pushScene(makeRaidResults(g_, entry_.eventId));  // its own choice, as from the pause
        }
    }

    /// 2.78: a line of the « Tableau des contributions » from the simulation's numbers.
    contrib::Row contribRow(const raid::ZoneContribution& c, const std::vector<raid::ZoneContribution>& all) const {
        contrib::Row row;
        bool later = false;  // a newer holder of the same seat: this row is a player who left
        for (const raid::ZoneContribution& o : all) later = later || (o.seat == c.seat && o.from > c.from);
        row.name = later ? "Joueur parti (place " + std::to_string(c.seat + 1) + ")" : seatName(c.seat);
        row.color = seatColor(c.seat, local_);
        row.me = !later && c.seat == local_;
        row.bossTenths = c.bossDamage;
        row.miniTenths = c.miniDamage;
        row.damageTenths = c.damage;
        row.support = c.support;
        row.mechanisms = c.mechanisms;
        row.objectives = c.objectives;
        row.deaths = c.deaths;
        row.revives = c.revives;
        row.revived = c.revived;
        row.v2 = c.v2;
        return row;
    }

    /// 2.78: the « Tableau des contributions »: each zone finished while a player of the instance was in it
    /// (the newest first), and the whole raid (every zone of the instance added up).
    contrib::Model contribModel() const {
        contrib::Model m;
        m.tabKey = const_cast<RaidGameScene*>(this)->menuKeyName();
        m.legend = test_ ? "Test local : chiffres comptés par la simulation, rien n'est envoyé au serveur."
                         : "Chiffres comptés par la simulation (les mêmes sur tous les jeux du groupe), validés par la majorité.";
        m.tooltipNote = test_ ? "Compté par la simulation (test local)." : "Compté par la simulation, validé par la majorité du groupe.";
        if (!run_) return m;
        const auto& list = run_->finishes();
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            contrib::Table t;
            const raid::ZoneInfo* z = branch_->zone(it->zone);
            t.title = "Zone " + std::to_string(it->zone) + " · " + (z ? z->name : std::string("?"));
            t.sub = std::string(it->byUs ? "terminée par votre groupe" : "terminée par un autre groupe") + " · " +
                    raidmenu::raidTime(static_cast<float>(it->tick) / static_cast<float>(kSimHz));
            for (const raid::ZoneContribution& c : it->rows) t.rows.push_back(contribRow(c, it->rows));
            m.zones.push_back(std::move(t));
        }
        const std::vector<raid::ZoneContribution> all = run_->contributions().raidTable();
        m.raid.title = "Raid entier · " + std::string(branch_->name);
        m.raid.sub = "toutes les zones de votre instance, additionnées";
        for (const raid::ZoneContribution& c : all) m.raid.rows.push_back(contribRow(c, all));
        return m;
    }

    /// 2.75.5: what the menu shows, from this screen's data (the simulation's, the server's).
    raidmenu::MenuData menuData(GameWorld& w) {
        raidmenu::MenuData d;
        d.title = "MENU DU RAID · " + std::string(branch_->name);
        d.keyName = menuKeyName();
        const raid::ZoneInfo* z = branch_->zone(run_->zone());
        d.remainingTitle = "CE QU'IL RESTE À FAIRE · " + std::string(z ? z->name : "?");
        d.remaining = remainingLines(w);
        const auto& list = run_->finishes();
        // 2.78: a line per zone finished; the full table (tabs, sort, details) opens over the menu.
        const contrib::Model cm = contribModel();
        d.tableAvailable = !cm.zones.empty() || !cm.raid.rows.empty();
        for (const contrib::Table& t : cm.zones) {
            const contrib::Prepared pr = contrib::prepare(t, contrib::Col::Boss);
            std::string line = t.title + " · " + t.sub;
            for (size_t i = 0; i < pr.cells.size(); ++i)
                if (pr.cells[i][static_cast<size_t>(contrib::Col::Boss)].best) {
                    const contrib::Row& r = t.rows[i];
                    line += " · " + pr.headers[static_cast<size_t>(contrib::Col::Boss)] + " : " + (r.me ? std::string("vous") : r.name) + " (" +
                            pr.cells[i][static_cast<size_t>(contrib::Col::Boss)].text + ")";
                    break;
                }
            d.tableLines.push_back(i18n::keep(line));
        }
        for (const raid::ZoneGoals& g : raid::branchGoals(*branch_, run_->validated(), run_->localDone())) {
            bool here = false;
            for (const raid::ZoneFinish& f : list) here = here || f.zone == g.zone;
            if (g.finished && !here) d.otherZones.push_back(g.name);
        }
        d.journal = &overlay_.notices.journal();
        // The group: each seat's state, « spectateur » once the group is eliminated.
        const raidhud::Group g = groupData(w);
        d.groupTitle = "GROUPE · " + g.title;
        d.groupFooter = g.footer;
        {
            // 2.77.2: the two states of a player who no longer plays, and their keys (this player's own state lit).
            const downed::State st = downed::stateOf(w, local_, run_->wiped());
            const std::string look = keyNames(Action::MenuTabPrev) + " / " + keyNames(Action::MenuTabNext);
            const std::string back = keyNames(Action::MenuConfirm);
            d.groupStates.push_back({"À TERRE — en attente de réanimation",
                                     "Votre âme attend sur le dernier endroit sûr : un allié en jeu maintient Interagir 2 s près d'elle pour vous relever, "
                                     "autant de fois qu'il le faut. En attendant, " +
                                         look + " : regarder le joueur précédent / suivant ; " + back + " : revenir sur votre âme.",
                                     st == downed::State::Down ? kGold : kDim});
            d.groupStates.push_back({"GROUPE ÉLIMINÉ — mode spectateur jusqu'à la fin de l'événement",
                                     "Tout le groupe à terre en même temps : la zone n'est pas validée et attend un groupe de renfort, vous regardez "
                                     "jusqu'à la fin de l'événement. " +
                                         look + " : joueur précédent / suivant de la branche ; " + back + " : caméra libre (flèches, stick, molette).",
                                     st == downed::State::Wiped ? kDanger : kDim});
        }
        for (int i = 0; i < w.teamSize() && i < static_cast<int>(g.rows.size()); ++i) {
            const raidhud::GroupRow& row = g.rows[static_cast<size_t>(i)];
            raidmenu::Member m;
            m.name = row.name;
            m.color = row.color;
            m.vacant = row.vacant;
            m.me = i == local_;
            m.health = row.health;
            m.state = row.state;
            m.stateColor = row.stateColor;
            const Player& p = w.playerAt(i);
            if (!row.vacant) m.hearts = std::to_string(std::max(0, p.health)) + " / " + std::to_string(p.maxHealth());
            if (run_->wiped() && !row.vacant && w.member(i).presence != GameWorld::Presence::Gone) {
                m.state = "spectateur";
                m.stateColor = kViolet;
            }
            d.group.push_back(std::move(m));
        }
        d.titan = titanLines();
        d.results = fallSeen_ && !test_ && !entry_.eventId.empty();
        return d;
    }

    /// 2.75.5: the titan as this branch knows it: its gigaboss (its lair finished or not), its parts
    /// fallen (in the heart's arena), the links of the other branches that act here (on, or not yet).
    std::vector<raidhud::Line> titanLines() {
        std::vector<raidhud::Line> lines;
        const std::string boss = branch_->gigaboss.empty() ? std::string("Le gigaboss") : branch_->gigaboss;
        for (const raid::ZoneGoals& g : raid::branchGoals(*branch_, run_->validated(), run_->localDone())) {
            if (!g.lair) continue;
            if (g.finished) lines.push_back({"OK", boss + " est vaincu · " + g.name, kGreen});
            else lines.push_back({"→", boss + " est debout · " + g.name + (g.missing().empty() ? std::string() : " · " + g.missing()), kOrange});
        }
        if (const raid::AsterionHeartRoutine* hr = run_->heartRoutine()) {
            static const char* kParts[] = {"Crâne", "Bras gauche", "Bras droit", "Torse", "Abdomen", "Jambe gauche", "Jambe droite"};
            for (int p = 0; p < raid::AsterionHeartRoutine::PartCount && p < 7; ++p)
                lines.push_back(hr->down(p) ? raidhud::Line{"◆", std::string(kParts[p]) + " : tombé", kGreen}
                                            : raidhud::Line{"!", std::string(kParts[p]) + " : debout", kOrange});
        }
        for (const raid::EffectInfo& e : branch_->effects)
            lines.push_back(raid::Branch::on(run_->mask(), e) ? raidhud::Line{"◊", e.label, kGreen} : raidhud::Line{"·", "Pas encore : " + e.label, kDim});
        if (!fallMore_.empty()) lines.push_back({"i", fallMore_, kDim});
        if (lines.empty()) lines.push_back({"·", "Rien du titan n'agit sur cette branche pour l'instant.", kDim});
        return lines;
    }

    /// 2.75: « Ce qu'il reste à faire »: the zone's requirements ticked (from the state the server
    /// validated; « en attente de validation » when only this group did it), its ambushes and co-op
    /// plates, then each zone of the branch still to finish with what it misses (to take back first).
    std::vector<raidhud::Line> remainingLines(GameWorld& w) {
        std::vector<raidhud::Line> lines;
        const Color todo{255, 176, 74, 255};
        const raid::ZoneGoals here = raid::zoneGoals(*branch_, run_->zone(), run_->validated(), run_->localDone());
        // 2.78: where each player of the group is (each one changes zone alone).
        if (const std::string where = groupWhere(); !where.empty()) lines.push_back({"i", where, kSky});
        if (here.finished) lines.push_back({"OK", "Zone terminée", kGreen});
        if (const auto fell = retake_.find(run_->zone()); fell != retake_.end() && !here.finished)
            lines.push_back({"!", "Zone à reprendre : " + fell->second + " est tombé ici, finissez-la à sa place", kDanger});
        for (const raid::Requirement& r : here.items) {
            if (r.access) continue;  // the group is in it
            if (r.done) lines.push_back({r.pending ? "…" : "OK", r.text + (r.pending ? " · validation du serveur en attente" : ""), r.pending ? kGold : kGreen});
            else lines.push_back({r.progress.empty() ? (r.optional ? "◆" : "→") : r.progress, r.text, r.optional ? kViolet : todo});
        }
        // The zone's ambushes that are not an objective themselves (in the level's order), its plates.
        std::vector<const CombatZoneEntity*> fights;
        for (const auto& e : w.entities())
            if (auto* c = dynamic_cast<const CombatZoneEntity*>(e.get())) {
                bool objective = false;  // 2.78: the ambushes of the zone before a lair are objectives, and listed here too
                for (const raid::ZoneObjective& zo : run_->rules().objectives) {
                    const raid::ObjectiveInfo* o = branch_->objective(zo.id);
                    objective = objective || (zo.source == e->id() && !(o && o->role == "ambush"));
                }
                if (!objective) fights.push_back(c);
            }
        for (size_t i = 0; i < fights.size(); ++i) {
            const CombatZoneEntity* c = fights[i];
            const std::string name = fights.size() > 1 ? "Embuscade " + std::to_string(i + 1) + " sur " + std::to_string(fights.size()) : std::string("Embuscade");
            if (c->cleared()) lines.push_back({"OK", name + " : nettoyée", kGreen});
            else if (c->started()) lines.push_back({std::to_string(c->wavesDone()) + " / " + std::to_string(c->waveCount()), name + " : vague en cours", kViolet});
            else lines.push_back({"→", name + " : à venir", todo});
        }
        for (size_t k = 0; k < run_->rules().coops.size(); ++k) {
            const raid::RaidRun::CoopView v = run_->coop(k);
            const std::string& label = run_->rules().coops[k].label;
            if (v.done) lines.push_back({"OK", label, kGreen});
            else lines.push_back({std::to_string(std::min(v.on, std::max(0, v.need))) + " / " + std::to_string(std::max(1, v.need)), label + " : joueurs ensemble sur la plaque", todo});
        }
        // The branch: what is left in its other zones (the zones to take back first).
        std::vector<raidhud::Line> branch, retake;
        // 2.76.2: « zone : ce qui manque » joined in the language shown (the requirements hold « : » too,
        // the whole was cut at the wrong one): each part translated, the line kept as it is.
        const char* colon = i18n::language() == Language::English ? ": " : " : ";
        for (const raid::ZoneGoals& g : raid::branchGoals(*branch_, run_->validated(), run_->localDone())) {
            if (g.zone == run_->zone() || g.finished) continue;
            if (g.passage) {  // 2.78: the lair's armory, a zone of passage (once the way to the lair is open)
                if (g.reachable) branch.push_back({"i", i18n::keep(i18n::tr(g.name) + colon + i18n::tr(g.missing())), kSky});
                continue;
            }
            const std::string miss = g.missing();
            if (const auto fell = retake_.find(g.zone); fell != retake_.end()) {
                retake.push_back({"!", i18n::keep(i18n::tr(g.name) + colon + i18n::tr("zone à reprendre (" + fell->second + " est tombé)") +
                                                  (miss.empty() ? "" : " · " + i18n::tr(miss))),
                                  kDanger});
                continue;
            }
            branch.push_back({g.reachable ? "→" : "■",
                              i18n::keep(i18n::tr(g.name) + colon + i18n::tr(miss) + (g.optional ? " " + i18n::tr("(facultatif)") : "")),
                              g.optional ? kViolet : g.reachable ? kSky : kDim});
        }
        lines.push_back({"", retake.empty() && branch.empty() ? "BRANCHE · toutes les autres zones sont terminées" : "BRANCHE · ce qu'il manque", kSky});
        for (auto& l : retake) lines.push_back(std::move(l));
        for (auto& l : branch) lines.push_back(std::move(l));
        return lines;
    }

    /// 2.72: the objectives' lines (the zone's, its locked passages, its mechanisms, the links of the
    /// titan, the ambush, the gigaboss).
    std::vector<raidhud::Line> objectiveLines(GameWorld& w) {
        std::vector<raidhud::Line> lines;
        const uint32_t mask = run_->mask();
        for (const raid::ZoneObjective& zo : run_->rules().objectives) {
            const raid::ObjectiveInfo& o = branch_->objectives[static_cast<size_t>(zo.index)];
            const bool done = raid::Branch::done(mask, o.index);
            std::string mark = done ? "OK" : o.optional ? "◆" : "→";
            Color c = done ? kGreen : o.optional ? kViolet : Color{255, 176, 74, 255};
            if (!o.pair.empty()) {
                int n = 0, total = 0;
                for (const auto& x : branch_->objectives)
                    if (x.pair == o.pair) {
                        ++total;
                        n += raid::Branch::done(mask, x.index);
                    }
                if (!done) mark = std::to_string(n) + " / " + std::to_string(total);
            }
            lines.push_back({mark, o.label, c});
        }
        // The locked passages of the zone: what opens them, with its progress.
        for (const raid::Passage& p : run_->rules().passages) {
            if (run_->passageOpen(p) || p.lock.empty()) continue;
            const raid::ZoneInfo* to = branch_->zone(p.to);
            const std::string name = to ? to->name : "?";
            const std::string why = branch_->lockText(p.lock).objective;  // 2.68: each branch's own words
            if (p.lock == "generators")
                lines.push_back({std::to_string(branch_->generators(mask)) + " / " + std::to_string(branch_->generatorsNeeded), name + " : " + why, kGold});
            else if (p.lock == "shortcut")
                lines.push_back({"◀▶", name + " : " + why, kSky});
            else if (p.lock.rfind("link:", 0) == 0)
                lines.push_back({"◊", name + " : " + why, kViolet});
            else
                lines.push_back({"■", name + " : passage verrouillé", kDim});
        }
        // 2.68: the zone's co-op mechanisms (locks, goods lift): the players on their plate.
        for (size_t k = 0; k < run_->rules().coops.size(); ++k) {
            const raid::RaidRun::CoopView v = run_->coop(k);
            const std::string& label = run_->rules().coops[k].label;
            if (v.done) lines.push_back({"OK", label + " : en marche", kGreen});
            else if (v.need > 0) lines.push_back({std::to_string(v.on) + " / " + std::to_string(v.need), label + " : joueurs ensemble sur la plaque", kGold});
        }
        // 2.68: what the other branches of the titan did here (a link on in this zone).
        for (const raid::EffectInfo& e : branch_->effects)
            if (e.kind != "open" && e.inZone(run_->zone()) && raid::Branch::on(mask, e)) lines.push_back({"◊", e.label, kGreen});
        if (!run_->rules().sealedLabel.empty()) lines.push_back({"■", run_->rules().sealedLabel, kGold});
        if (const CombatZoneEntity* amb = w.activeAmbush()) {
            // 2.68: the ambush under way (its storeys, its waves, the enemies left: violet arrows too).
            lines.push_back({std::to_string(amb->currentWave() + 1 > amb->waveCount() ? amb->waveCount() : amb->currentWave() + 1) + " / " +
                                 std::to_string(amb->waveCount()),
                             "Embuscade : vague en cours, " + std::to_string(amb->enemiesLeft(w)) + " ennemis (flèches violettes)", kViolet});
        }
        {
            // 2.68: the gigaboss of the zone (its stage, the vents, the weak points, the allies down).
            std::vector<std::string> marks, texts;
            std::vector<Color> colors;
            gigabossLines(w, marks, texts, colors);
            for (size_t i = 0; i < marks.size(); ++i) lines.push_back({marks[i], texts[i], colors[i]});
        }
        if (lines.empty()) lines.push_back({"·", "Aucun objectif dans cette zone : avancez vers les passages.", kDim});
        return lines;
    }

    /// 2.75.5: the simulation's alerts now told as notifications (a zone finished, the titan, the world,
    /// the group eliminated): not twice on the screen.
    static bool toldAsNotice(const std::string& t) {
        return t.rfind("ZONE TERMINÉE · ", 0) == 0 || t.rfind("BRANCHE · Zone terminée : ", 0) == 0 || t.rfind("TITAN · ", 0) == 0 ||
               t.rfind("MONDE · ", 0) == 0 || t.rfind("GROUPE ÉLIMINÉ · ", 0) == 0;
    }

    /// 2.72: the simulation's alerts of the last 8 s (group, branch, danger), then the server's answers.
    std::vector<raidhud::Line> alertLines() {
        std::vector<raidhud::Line> show;
        for (const raid::Alert& a : run_->alerts()) {
            if (run_->tick() - a.tick >= 8 * 120 || toldAsNotice(a.text)) continue;
            // 2.78: what happens in another zone stays there; who changes zone is told to everyone.
            if (a.zone != 0 && a.zone != run_->zone() && a.seat < 0) continue;
            const std::string text = a.seat >= 0 ? i18n::keep(seatName(a.seat) + " · " + i18n::tr(a.text)) : a.text;
            show.push_back({"", text, a.kind == raid::Alert::Kind::Branch ? kGreen : a.kind == raid::Alert::Kind::Danger ? kDanger : kGold});
        }
        for (const auto& n : notes_)
            if (clock_ - n.at < 8.0f) show.push_back({"", n.text, n.c});
        if (show.size() > 4) show.erase(show.begin(), show.end() - 4);
        return show;
    }

    /// 2.72: the danger band's text now (the danger announced, then on), empty when none.
    std::string dangerText() {
        const float in = run_->magmaIn();
        if (in < 0.0f) return {};
        const raid::Magma& m = run_->rules().magma;  // 2.68: each branch's own danger
        if (run_->magmaActive()) return m.message;
        if (in <= m.warn + 0.01f) return m.name + " dans " + std::to_string(static_cast<int>(std::ceil(in))) + " s · " + m.hint + " !";
        return {};
    }

    /// 2.72: the network line, or a test's badge (never counted: said where the network's line is).
    std::string statusText(Color& c) {
        if (sim_) {
            // 2.77: always said first (the line is fitted from its start): nothing leaves this computer.
            c = kGold;
            return simStatus();
        }
        if (test_) {
            c = kGold;
            // 2.75: the simulated players counted without the administrator (0 to 7).
            const int bots = std::max(0, testSetup_.players - 1);
            return "TEST ADMIN · non classé · vous + " + std::to_string(bots) + (bots > 1 ? " joueurs simulés" : " joueur simulé") +
                   (run_ && run_->wiped() ? " · GROUPE ÉLIMINÉ — mode spectateur" : "") + (testSetup_.invincible ? " · invincible (vous)" : "");
        }
        if (waiting_ > 0.4f) {
            c = kGold;
            const std::string who = link_->waitingFor();
            return who.empty() ? "En attente du réseau…" : "En attente de " + who + "…";
        }
        c = kText;
        return "Zone " + std::to_string(run_->zone()) + " sur 8 · délai d'entrée " + std::to_string(link_->inputDelay()) + " images";
    }

    Rect centred(float w, float h) {
        UIContext& ui = g_.ui;
        w = std::min(w, ui.screenW() - ui.s(40));
        h = std::min(h, ui.screenH() - ui.s(40));
        return {(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
    }

    void drawPause() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 170});
        Rect box = centred(ui.s(600), ui.s(470) + (sim_ ? ui.s(72) : 0.0f));
        ui.panel(box, true, 16);
        fit(ui, "PAUSE", {box.center().x, box.y + ui.s(44)}, 40, box.w - ui.s(48), kText, FontFace::Title, Align::Center);
        fit(ui, sim_ ? "Le raid continue pendant la pause : tous les groupes jouent." : test_ ? "Le test continue pendant la pause : vous pouvez encore être touché." : "Le groupe continue à jouer : vous pouvez encore être touché.", {box.center().x, box.y + ui.s(88)}, 18, box.w - ui.s(48), kDim,
            FontFace::Body, Align::Center);
        ui.pushId("pause");
        const float bw = box.w - ui.s(80), bh = ui.s(58);
        float y = box.y + ui.s(124);
        UIContext::ButtonStyle bs;
        bs.primary = true;
        if (ui.button("resume", {box.x + ui.s(40), y, bw, bh}, "Reprendre", bs)) paused_ = false;
        y += bh + ui.s(14);
        if (sim_) {
            // 2.77: the groups (join, watch), the contributions, the world's events, the titan's map.
            if (ui.button("raid", {box.x + ui.s(40), y, bw, bh}, "Groupes, contributions et carte du titan")) {
                paused_ = false;
                openSimPanel(0);
            }
            y += bh + ui.s(14);
        }
        {
            // 2.75.5: the raid's menu (the zones' tables among its sections), the pause closed: its own choice.
            const std::string label = "Menu du raid · " + menuKeyName();
            UIContext::ButtonStyle ms;
            ms.fontSize = ui.fitFont(label, FontFace::Bold, 22, bw - ui.s(24));
            if (ui.button("menu", {box.x + ui.s(40), y, bw, bh}, label, ms)) {
                paused_ = false;
                overlay_.menu.open(run_ && !run_->finishes().empty() ? raidmenu::Section::Zones : raidmenu::Section::Remaining);
            }
            y += bh + ui.s(14);
        }
        UIContext::ButtonStyle danger;
        danger.danger = true;
        if (ui.button("quit", {box.x + ui.s(40), y, bw, bh}, test_ ? "Quitter le test (retour à l'administration)" : "Quitter le raid (retour à la carte)", danger))
            leave();
        y += bh + ui.s(18);
        // 2.72: the raid's keys (the large map; 2.75.5: the menu), measured into the panel.
        const std::string mapKey = actionKey(g_.app.input(), Action::RaidMap), menuKey = actionKey(g_.app.input(), Action::RaidMenu);
        const PadBinding& menuPad = g_.app.input().bindings().pad[static_cast<int>(Action::RaidMenu)][0];
        const std::string keys = "Commandes du raid · carte agrandie : " + (mapKey.empty() ? std::string("aucune touche") : mapKey) +
                                 " (Vue / Retour à la manette) · menu du raid : " + (menuKey.empty() ? std::string("aucune touche") : menuKey) + " (" +
                                 (menuPad.kind == PadBinding::Kind::None ? std::string("aucun bouton") : padBindingName(menuPad, g_.app.input().gamepadStyle())) +
                                 " à la manette) · à changer dans Paramètres › Commandes";
        const auto lines = wrap(ui, keys, FontFace::Body, 15, box.w - ui.s(80), 3);
        drawLines(ui, lines, {box.x + ui.s(40), y}, 15, ui.s(20), kDim);
        ui.popId();
        if (!pauseOpenedNow_ && ui.backPressed()) paused_ = false;
        pauseOpenedNow_ = false;
    }

    /// 2.74: the link to the relay is lost: the world stays shown (frozen) under this card while the
    /// screen connects again by itself; the player may go back to the map at any time.
    void drawReconnect() {
        UIContext& ui = g_.ui;
        Rect box = centred(ui.s(620), ui.s(250));
        ui.panel(box, true, 16);
        fit(ui, "LIAISON PERDUE", {box.center().x, box.y + ui.s(40)}, 30, box.w - ui.s(48), kGold, FontFace::Title, Align::Center);
        const auto lines = wrap(ui, reconnect_.message(clock_), FontFace::Body, 16, box.w - ui.s(56), 3);
        drawLines(ui, lines, {box.x + ui.s(28), box.y + ui.s(74)}, 16, ui.s(22), kText);
        const float p = reconnect_.remaining(clock_) / raid::Reconnect::kWindow;
        const Rect bar{box.x + ui.s(28), box.bottom() - ui.s(98), box.w - ui.s(56), ui.s(8)};
        ui.r().fillRoundedRect(bar, ui.s(4), Color{40, 30, 50, 255});
        ui.r().fillRoundedRect({bar.x, bar.y, bar.w * std::clamp(p, 0.0f, 1.0f), bar.h}, ui.s(4), kOrange);
        ui.pushId("reconnect");
        if (ui.button("map", {box.x + ui.s(40), box.bottom() - ui.s(74), box.w - ui.s(80), ui.s(54)}, "Retour à la carte de la branche") || ui.backPressed()) leave();
        ui.popId();
    }

    void drawEnd() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 190});
        Rect box = centred(ui.s(620), ui.s(260));
        ui.panel(box, true, 16);
        fit(ui, "FIN DE L'INSTANCE", {box.center().x, box.y + ui.s(44)}, 32, box.w - ui.s(48), kText, FontFace::Title, Align::Center);
        const auto lines = wrap(ui, endReason_, FontFace::Body, 17, box.w - ui.s(56), 3);
        drawLines(ui, lines, {box.x + ui.s(28), box.y + ui.s(80)}, 17, ui.s(24), kDim);
        ui.pushId("end");
        UIContext::ButtonStyle bs;
        bs.primary = true;
        if (ui.button("map", {box.x + ui.s(40), box.bottom() - ui.s(82), box.w - ui.s(80), ui.s(58)}, "Retour à la carte de la branche", bs)) leave();
        ui.popId();
    }

    GameServices& g_;
    RaidEntry entry_;
    // 2.77: « Simuler un raid complet » (raid::RaidSimulation): the raid, the instance shown, the budget
    // of time per image, the speed measured, the world's events already told, the raid's panel.
    static constexpr double kSimBudgetMs = 10.0;
    static constexpr double kOnlineBudgetMs = 6.0;  // 2.78: an online raid's steps in one image (at least one)
    RaidSimSetup simSetup_;
    bool simMode_ = false;
    std::unique_ptr<raid::RaidSimulation> sim_;
    const raid::TestSession* boundSession_ = nullptr;
    double simSpent_ = 0.0;
    uint64_t simWanted_ = 0, simDone_ = 0;
    int speedPct_ = 100;
    float speedAt_ = 0.0f;
    uint32_t feedSeen_ = 0;
    bool feedStarted_ = false;
    bool victoryPlayed_ = false;
    int simPanel_ = -1;  // the tab of the raid's panel open, -1 closed
    bool panelJustClosed_ = false;
    AsterionFigure titanFigure_;
    Rect statusRect_;
    float statusK_ = 1.0f;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    const raid::Branch* branch_ = nullptr;
    std::unique_ptr<NetCoopLink> link_;
    raidspec::Spectator spec_;   // 2.75: the spectator mode (a group wiped)
    downed::Banner banner_;          // 2.77.2: the banner of this player down / of the group wiped, this frame
    downed::Layout bannerLayout_;    // its places (an empty panel: not shown)
    downed::ViewBlend blend_;        // 2.77.2: a change of view (the player watched, the soul, the free camera), eased
    int downWatch_ = -1;             // 2.77.2: this player down: the seat watched (-1: their soul)
    bool wasDown_ = false;
    std::string ownInstance_;    // 2.75: the instance of this player's group (its spectators' list)
    int watchSeat_ = -1;         // 2.75: the seat watched in the instance shown (-1: none)
    float spectatorWait_ = 0.0f; // 2.75: a spectator waiting for a game to watch
    uint8_t pendingChoice_ = 0;  // 2.75: the armory's power choice, for this player's next frame
    std::unique_ptr<raid::RaidRun> ownRun_;        // a real instance's run
    std::unique_ptr<raid::TestSession> session_;   // 2.72: an administrator's test (its run, its simulated players)
    raid::RaidRun* run_ = nullptr;                 // the one played
    GameRenderer renderer_;
    Hud hud_;
    TexturePtr suit_;
    raidhud::HudAreas hudAreas_;    // 2.72: the game's HUD at the raid layout's places (this frame)
    GameplaySettings hudSettings_;  // 2.72: what the game's HUD is drawn with in the raid
    raidhud::Minimap minimap_;      // 2.72: the zone's terrain baked, the maps
    EnemyArrows::Tracker metEnemies_;  // 2.72: the enemies met in this world (the arrows of the solo levels)
    bool bigMap_ = false;           // 2.72: the large map (Action::RaidMap)
    float mapPan_ = 0.0f;           // world pixels the large map was dragged by
    PowerKeyMap powerKeys_;
    int local_ = 0;
    int serial_ = -1;
    uint32_t tick_ = 0;
    uint32_t sentUpTo_ = UINT32_MAX;
    std::vector<InputFrame> frame_;
    std::vector<std::pair<int, InputFrame>> frames_;
    std::thread worker_;
    std::atomic<bool> stopWorker_{false}, workerDone_{false};
    std::atomic<uint32_t> progress_{0};
    bool catching_ = false, connecting_ = false, paused_ = false, ended_ = false;
    raid::Reconnect reconnect_;  // 2.74: the automatic reconnection after a lost link
    bool fresh_ = false;         // 2.74: link_ is an attempt of the reconnection (its GO not come yet)
    bool rejoining_ = false;     // 2.74: the catch-up of a reconnection (its text)
    bool pauseOpenedNow_ = false;  // the pause opened this frame: its own key press must not close it
    bool test_ = false;         // 2.68: an administrator's test (solo, local, never reported)
    RaidTestSetup testSetup_;
    std::vector<InputScript> scripts_;  // 2.70: a capture's seats (docs/BUILDING.md)
    raidmenu::Overlay overlay_;  // 2.75.5: the notifications and the raid's menu (no card opens by itself)
    contrib::View contribView_;  // 2.78: the « Tableau des contributions » over the menu (tabs, sort, details)
    bool contribOpen_ = false;
    bool contribShown_ = false;  // 2.78: a capture opened it already
    bool fallSeen_ = false;      // 2.75.5: a gigaboss fell here: « Résultats du raid » in the menu
    std::string fallMore_;       // 2.75.5: when its results count (the menu's « Titan »)
    float pointerAt_ = -10.0f;   // 2.75.5: when the mouse last moved (the pointer shown, for the HUD's button)
    Rect menuButtonRect_{};      // 2.77.4: the HUD's menu button as last drawn (empty when not shown)

    /// 2.77.4: whether a mouse move shows the pointer: near the HUD's menu button (the only mouse-only
    /// zone while playing) or while watching a group of the full raid's simulation (its ◀ ▶ buttons).
    bool pointerWanted() const {
        if (sim_ && local_ < 0) return true;
        if (menuButtonRect_.w <= 0.0f) return false;
        const float m = g_.ui.s(40);
        const Rect near{menuButtonRect_.x - m, menuButtonRect_.y - m, menuButtonRect_.w + 2 * m, menuButtonRect_.h + 2 * m};
        return near.contains(g_.app.input().mousePos());
    }
    const raid::RaidRun* watchedRun_ = nullptr;  // 2.75.5: the run whose branch is watched (its zones, its links)
    uint32_t seenZones_ = 0, seenLinks_ = 0;
    bool branchRead_ = false;    // 2.75.5: the branch's map read once (what was so then is no news)
    std::map<int, std::string> retake_;  // 2.75: zones to take back (a group fell there): zone → that group
    float branchAt_ = 0.0f;              // 2.75: when the branch's map is read again
    raid::ArenaCamera arenaCam_;   // 2.68: the camera of a gigaboss's arena (display only)
    std::vector<Rect> hudRects_;   // 2.68: what the HUD covers this frame (kept clear of the arrows)
    WorldView view_;
    AnimationSetPtr vents_;     // 2.68: the sheet of its vents
    std::string ventsPath_;     // 2.69: whose (each gigaboss its own: rules().fx)
    float waiting_ = 0.0f, clock_ = 0.0f;
    std::string fail_, endReason_;
    struct Note {
        std::string text;
        Color c;
        float at;
    };
    std::vector<Note> notes_;
};

}  // namespace

std::unique_ptr<Scene> makeRaidGame(GameServices& g, RaidEntry entry) { return std::make_unique<RaidGameScene>(g, std::move(entry)); }

std::unique_ptr<Scene> makeRaidTest(GameServices& g, RaidTestSetup setup) { return std::make_unique<RaidGameScene>(g, std::move(setup)); }

std::unique_ptr<Scene> makeRaidSim(GameServices& g, RaidSimSetup setup) { return std::make_unique<RaidGameScene>(g, setup); }

}  // namespace cosmic
