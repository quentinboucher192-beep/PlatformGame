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
// 2.71 (docs/RAIDS.md §15): the HUD placed by the player (Paramètres › HUD › « Raids »:
// RaidHudLayout, drawn by ui/online/RaidHud), the minimap in textures and its large map
// (Action::RaidMap: M, View / Back; display only, the group keeps playing), the off-screen arrows
// of the solo levels (enemies met, the ambush's), and the test played as a real instance
// (raid::TestSession: the branch's state kept from zone to zone as RAID_STATE gives it, the
// simulated players « Joueur simulé N » on their seats, seen, named, in their colours).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

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
#include "game/raid/RaidTest.h"
#include "game/raid/ArenaCamera.h"
#include "game/gameplay/EnemyArrows.h"
#include "game/gameplay/EnemyArrowsDraw.h"
#include "game/gameplay/entities/Entities.h"
#include "engine/resources/ResourceManager.h"
#include "saves/Settings.h"
#include "ui/online/RaidHud.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/Glyphs.h"
#include "ui/screens/Navigation.h"

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

class RaidGameScene : public Scene {
public:
    RaidGameScene(GameServices& g, RaidEntry entry) : g_(g), entry_(std::move(entry)) {}
    /// 2.68: an administrator's test (solo, local, never reported).
    RaidGameScene(GameServices& g, RaidTestSetup t) : g_(g), test_(true), testSetup_(std::move(t)) {
        entry_.clanTag = "TEST";
        entry_.group = 1;
    }

    ~RaidGameScene() override {
        stopWorker_ = true;
        if (worker_.joinable()) worker_.join();
        if (link_) link_->close();
    }

    bool holdsNotifications() const override { return true; }
    // 2.68 (phase 3): the pointer comes back with the card of the gigaboss's fall (« Résultats du raid »).
    bool hidesCursor() const override { return run_ && !catching_ && !connecting_ && !paused_ && !ended_ && !fallCard_ && !bigMap_; }

    void onEnter() override {
        branch_ = raid::Branch::builtin(entry_.branch);
        if (test_) {
            beginTest();
            return;
        }
        if (!branch_) {
            fail_ = "Les zones du raid sont introuvables dans les données du jeu : réinstallez le jeu.";
            return;
        }
        g_.ui.setFocus("raidgame/start");
        connecting_ = true;
        g_.online.freshAccess([this, life = std::weak_ptr<int>(life_)](const std::string& token, const std::string& url) {
            if (life.expired()) return;
            connecting_ = false;
            if (token.empty() || url.empty()) {
                fail_ = "Connectez-vous à Cosmic Online pour combattre dans le raid.";
                return;
            }
            link_ = std::make_unique<NetCoopLink>(makeWebSocketTransport(url + "/api/v1/raids/instances/" + entry_.instanceId + "/socket", token),
                                                  g_.online.username(), 255, g_.online.playerId());
            link_->start();
        });
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
        local_ = link_->seat();
        ResourceManager& res = g_.app.resources();
        WorldServices ws{res, g_.app.audio(), g_.content, &g_.app.input(), g_.settings.graphics.screenShake,
                         g_.settings.graphics.particleQuality == 0 ? 0.4f : g_.settings.graphics.particleQuality == 1 ? 0.7f : 1.0f};
        ownRun_ = std::make_unique<raid::RaidRun>(ws, *branch_, go.players);
        run_ = ownRun_.get();
        run_->setCameraFollow(local_);
        if (!run_->begin(link_->raidStateAt(0))) {
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

    /// 2.68: an administrator's test: the branch and zone chosen, alone on this computer; 2.71: as a
    /// real instance (raid::TestSession): the simulated players on seats 1 .. players − 1 (the others
    /// vacant), the branch's state kept from zone to zone as RAID_STATE gives it, never reported.
    void beginTest() {
        branch_ = nullptr;
        for (const raid::Branch* b : raid::Branch::all())
            if (b->id == testSetup_.branch) branch_ = b;
        if (!g_.online.isAdmin()) {
            fail_ = "Réservé aux administrateurs connectés à Cosmic Online.";
            return;
        }
        if (!branch_ || !branch_->zone(testSetup_.zone)) {
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
        run_ = &session_->run();
        run_->setCameraFollow(0);
        if (!session_->begin(testSetup_.zone)) {
            fail_ = "Impossible de charger la zone du test.";
            run_ = nullptr;
            session_.reset();
            return;
        }
        hud_.setRaidView(true);
        zoneReady();
    }

    /// 2.68: one step of a test: this computer's input on seat 0 (neutral while paused: the pause
    /// never stops the world); 2.71: the simulated players on theirs (TestSession, playing on during
    /// the pause and the large map), the free seats vacant.
    void stepTest() {
        InputFrame f = InputFrame::fromPlayerInput(g_.app.input().consumePlayerInput());
        if (paused_) f = InputFrame{};
        if (const auto& offer = run_->world().powerOffer(); offer && offer->player == 0) f.choice = InputFrame::kChoiceLeave;
        session_->step(f);  // never reports anything
        ++tick_;
        if (run_->zoneSerial() != serial_) zoneReady();
    }

    void catchUp() {
        std::vector<InputFrame> team;
        while (!stopWorker_) {
            if (link_->frameFor(tick_, team)) {
                run_->step(team.data(), static_cast<int>(team.size()), link_->raidStateAt(tick_));
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
        minimap_.invalidate();  // 2.71: its terrain baked again
        metEnemies_.reset();    // 2.71: nobody met yet in this world (the off-screen arrows)
        mapPan_ = 0.0f;
        renderer_.build(g_.app.renderer(), g_.app.resources(), w);
        std::vector<Color> colors;
        for (int i = 0; i < w.teamSize(); ++i) colors.push_back(seatColor(i, local_));
        renderer_.setTeamColors(colors);
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
            if (run_) stepTest();
            return;
        }
        if (!run_ || catching_ || ended_) {
            if (catching_ && workerDone_) finishCatchUp();
            return;
        }
        int steps = 1;
        if (link_->available() > tick_ + static_cast<uint32_t>(link_->inputDelay() + 6)) steps = 4;
        for (int k = 0; k < steps && !ended_; ++k) {
            sendLocal();
            if (!link_->frameFor(tick_, frame_)) {
                waiting_ += dt;
                break;
            }
            waiting_ = 0.0f;
            run_->step(frame_.data(), static_cast<int>(frame_.size()), link_->raidStateAt(tick_));
            ++tick_;
            if (run_->gigaboss().fellNow() && !link_->joinedLate()) fallCard_ = true;  // « Résultats du raid »
            if (run_->gigaboss().fellNow() && link_->joinedLate() && tick_ >= link_->rejoinAt()) fallCard_ = true;
            if (run_->zoneSerial() != serial_) zoneReady();
            sendReports();
        }
    }

    void sendLocal() {
        const uint32_t target = tick_ + static_cast<uint32_t>(link_->inputDelay());
        if (sentUpTo_ != UINT32_MAX && target <= sentUpTo_) return;
        sentUpTo_ = target;
        InputFrame f = InputFrame::fromPlayerInput(g_.app.input().consumePlayerInput());
        if (paused_) f = InputFrame{};
        // A power offered to this player (none in the branch's zones): left on its tablet, through
        // the frame like every choice (the replays stay exact).
        if (const auto& offer = run_->world().powerOffer(); offer && offer->player == local_) f.choice = InputFrame::kChoiceLeave;
        frames_.assign(1, {local_, f});
        link_->pushLocal(target, frames_);
    }

    void sendReports() {
        if (test_) return;  // 2.68: an administrator's test never reports anything
        for (const raid::Report& r : run_->takeReports()) {
            if (link_->joinedLate() && tick_ < link_->rejoinAt()) continue;  // not in play yet
            const char* kind = r.kind == raid::Report::Kind::Objective ? "objective"
                               : r.kind == raid::Report::Kind::Boss    ? "boss"
                               : r.kind == raid::Report::Kind::Result  ? "result"
                                                                       : "zone";
            Json body{{"kind", kind}, {"zone", r.zone}, {"tick", r.tick}, {"hash", hex16(r.hash)}};
            if (r.kind == raid::Report::Kind::Objective) body["objective"] = r.objective;
            if (r.kind == raid::Report::Kind::Boss) body["phase"] = r.phase;
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
        if (test_) {
            updateGame(dt);
            return;
        }
        if (link_) link_->update(dt);
        if (link_ && !run_ && fail_.empty() && link_->go()) begin();
        if (link_ && !link_->error().empty() && fail_.empty() && !run_) fail_ = link_->error();
        if (link_ && !link_->endReason().empty() && !ended_ && run_) {
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
        if (!ended_ && g_.app.input().menuPressed(Action::Pause)) {
            paused_ = !paused_;
            pauseOpenedNow_ = paused_;  // the same press (Échap = pause and back) must not close it
            g_.ui.setFocus("raidgame/pause/resume");
        }
        // 2.71: the large map (display only: the group, and this player, keep playing).
        if (!ended_ && !paused_ && g_.app.input().menuPressed(Action::RaidMap)) {
            bigMap_ = !bigMap_;
            mapPan_ = 0.0f;
        }
        if (paused_ || ended_) bigMap_ = false;
    }

    std::string seatName(int seat) const {
        if (seat == local_) return g_.online.username().empty() ? "Vous" : g_.online.username();
        // 2.71: a test's simulated players (the seats beyond them are free).
        if (session_) return seat < session_->players() ? raid::TestSession::botName(seat) : "Place libre";
        if (link_ && link_->lobby().is_object())
            for (const auto& s : link_->lobby().value("seats", Json::array()))
                if (jget<int>(s, "seat", -1) == seat && !jstr(s, "name").empty()) return jstr(s, "name");
        return "Joueur " + std::to_string(seat + 1);
    }

    // ------------------------------------------------------------------ drawing

    void render(float alpha) override {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.pushId("raidgame");
        if (!run_ || catching_) {
            r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{11, 10, 28, 255});
            drawWaiting();
        } else {
            GameWorld& w = run_->world();
            // 2.68: in a gigaboss's arena, its own camera (display only): the whole arena first, then a
            // view sized for this player; elsewhere the world's camera.
            WorldView view = arenaCam_.active() ? arenaCam_.view(r.width(), r.height()) : w.camera().view(r.width(), r.height(), alpha);
            view_ = view;
            RenderOptions opt;
            opt.lighting = g_.settings.graphics.dynamicLighting;
            opt.debug = g_.app.debugOverlay;
            renderer_.render(r, g_.app.resources(), w, view, alpha, opt);
            drawGigabossWorld(w, view);  // 2.68: its vents, its carapace (over the arena, under the HUD)
            drawSwitches(w, view);      // 2.68: the group's plates
            {
                // The vitals, powers and gigaboss bar of this computer's player, at the raid layout's
                // places (2.71, Paramètres › HUD › « Raids »; display only).
                GameWorld::PlayerScope me(w, std::clamp(local_, 0, w.teamSize() - 1));
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
            drawArrows(w, view);
            if (bigMap_ && !paused_ && !ended_) drawBigMap(w);
            drawArenaIntro();
            if (fallCard_ && !paused_ && !ended_) drawFall();
            if (paused_ && !ended_) drawPause();
            if (ended_) drawEnd();
        }
        ui.popId();
        ui.end();
    }

    /// Before the game: connecting, the instance's lobby (« Lancer »), the catch-up.
    void drawWaiting() {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        const Rect box{W * 0.5f - std::min(ui.s(330), W * 0.45f), H * 0.5f - ui.s(170), std::min(ui.s(660), W * 0.9f), ui.s(340)};
        ui.panel(box, true, 16);
        const float x = box.x + ui.s(28), w = box.w - ui.s(56);
        fit(ui, "ASTERION · " + std::string(branch_ ? branch_->name : "BRAS DROIT"), {x, box.y + ui.s(36)}, 26, w, kText, FontFace::Title);
        const std::string group = "[" + entry_.clanTag + "]·" + std::to_string(entry_.group);
        std::string line, sub;
        bool lobby = false;
        if (!fail_.empty()) {
            line = fail_;
        } else if (catching_) {
            const uint32_t total = std::max<uint32_t>(1, link_->available());
            line = "Le groupe joue déjà : rattrapage de la partie (" + std::to_string(std::min<uint32_t>(100, progress_ * 100 / total)) + " %)";
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

    /// 2.71: the raid's HUD at the places of the player's raid layout (RaidHudLayout), each element
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
            Color c = kText;
            const std::string t = statusText(c);
            raidhud::drawLineBox(ui, r, k(E::Status), t, c, test_);
            hudRects_.push_back(r);
        }
        if (shown(E::Timer)) {
            // 2.71: the raid's chrono from the simulation's steps (the same on every computer; it
            // runs on during a pause: the world never stops).
            const Rect r = at(E::Timer);
            raidhud::drawTimer(ui, r, k(E::Timer), static_cast<float>(run_->tick()) / static_cast<float>(kSimHz),
                               static_cast<float>(run_->zoneTicks()) / static_cast<float>(kSimHz));
            hudRects_.push_back(r);
        }
        if (shown(E::Contributions))
            if (const std::string t = contributionText(); !t.empty()) {
                const Rect r = at(E::Contributions);
                raidhud::drawLineBox(ui, r, k(E::Contributions), t, kText, false);
                hudRects_.push_back(r);
            }
    }

    /// The name of the key (keyboard / mouse, else gamepad) of an action, for the hints.
    static std::string actionKey(InputManager& in, Action a) {
        const auto& keys = in.bindings().keys[static_cast<int>(a)];
        const SDL_Scancode k = keys[0] != SDL_SCANCODE_UNKNOWN ? keys[0] : keys[1];
        return k != SDL_SCANCODE_UNKNOWN ? keyName(k) : std::string();
    }

    /// 2.71: the group panel's data (8 rows).
    raidhud::Group groupData(GameWorld& w) {
        using P = GameWorld::Presence;
        raidhud::Group g;
        int held = 0;
        for (int i = 0; i < w.teamSize(); ++i) held += w.member(i).presence != P::Vacant;
        g.title = "[" + entry_.clanTag + "]·" + std::to_string(entry_.group) + " · " + std::to_string(held) + " / 8";
        for (int i = 0; i < w.teamSize(); ++i) {
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
            g.rows.push_back(std::move(row));
        }
        g.footer = "Réanimations de la zone : " + std::to_string(w.revivesUsed()) + " / " + std::to_string(GameWorld::kTeamRevives);
        return g;
    }

    /// 2.71: what the maps show: the zone's objects, the enemies, the group, this player.
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

    /// 2.71: the areas the magma floods (and whether it is coming, or there).
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

    /// 2.71: the large map over most of the screen (Action::RaidMap; the group keeps playing): the
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
        std::vector<raidhud::ZoneChip> zones;
        const uint32_t mask = run_->mask();
        const std::vector<bool> reach = branch_->reachable(mask);
        for (const raid::ZoneInfo& z : branch_->zones) {
            raidhud::ZoneChip c;
            const bool reachable = z.id >= 0 && static_cast<size_t>(z.id) < reach.size() && reach[static_cast<size_t>(z.id)];
            c.here = z.id == run_->zone();
            c.name = z.secret && !reachable && !c.here ? "Zone secrète" : z.name;
            c.state = c.here ? "vous êtes ici" : branch_->controlled(z.id, mask) ? "contrôlée" : reachable ? "ouverte" : "verrouillée";
            c.color = c.here ? kGold : branch_->controlled(z.id, mask) ? kGreen : reachable ? kSky : kDim;
            zones.push_back(std::move(c));
        }
        const raid::ZoneInfo* z = branch_->zone(run_->zone());
        const std::string key = actionKey(in, Action::RaidMap);
        const std::string hint = (key.empty() ? std::string("La touche de la carte") : key) +
                                 " (ou Vue à la manette) : fermer la carte · le groupe continue à jouer · glisser à la souris : déplacer";
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
                const Rect world{f.at.x - 24.0f, f.at.y - 144.0f - 220.0f * fall, 48.0f, 144.0f};
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
        auto clipAt = [&](const std::string& name, const Rect& worldRect, float fps) {
            if (name.empty() || !vents_ || !vents_->texture) return;
            const AnimationClip* c = vents_->clip(name);
            if (!c) return;
            const Rect src = vents_->frameRect(*c, static_cast<int>(clock_ * fps) % std::max(1, c->frameCount()));
            r.drawTexture(*vents_->texture, &src, view.toScreen(worldRect));
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
                    clipAt(m.clip, {m.area.center().x - 24.0f, m.area.bottom() - 144.0f, 48.0f, 144.0f}, 12.0f);
                    break;
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
            const Player& me = w.playerAt(local_);
            f.arena = arena.rect;
            f.level = w.level().bounds;
            f.me = me.center();
            f.meInArena = w.member(local_).presence != GameWorld::Presence::Vacant && f.arena.contains(f.me) &&
                          !(gb.present() && gb.stage() == raid::Gigaboss::Stage::Absent);
            if (const Enemy* b = gb.boss(w); b && b->alive()) f.boss = b->box;
            for (const auto& fist : gb.fists()) f.threats.push_back(fist.at);
            if (gb.routine()) gb.routine()->threats(gb, f.threats);  // 2.69 (phase 4B)
            for (int k = 0; k < static_cast<int>(gb.vents().size()); ++k)
                if (gb.jetWarning(k) || gb.jetting(k)) f.threats.push_back(gb.vents()[static_cast<size_t>(k)].area.center());
            for (int i = 0; i < w.teamSize(); ++i)
                if (i != local_ && w.member(i).presence == GameWorld::Presence::Playing) f.allies.push_back(w.playerAt(i).center());
        }
        arenaCam_.update(f, dt, g_.ui.r().width(), g_.ui.r().height());
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

    /// 2.68: arrows on the border toward what the view does not show: the boss (its weak points when
    /// bare), the allies (their colours); 2.71: the enemies already met and the ambush's, exactly as
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
        if (gb.stage() == raid::Gigaboss::Stage::Exposed) {
            for (const auto& e : w.enemies())
                if (e->alive() && e->summonedBy == gb.bossUid() && e->def().id == gb.rules()->weakEnemy && off(e->box)) bossT.push_back(view.toScreen(e->box));
        } else if (const Enemy* b = gb.boss(w); b && b->alive() && off(b->box)) {
            bossT.push_back(view.toScreen(b->box));
        }
        draw(bossT, *tex, kGold);
        // The allies, each in their colour.
        for (int i = 0; i < w.teamSize(); ++i) {
            if (i == local_ || w.member(i).presence != GameWorld::Presence::Playing || !off(w.playerAt(i).box)) continue;
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
            case S::Absent: add("✓", name + " est déjà tombé : " + gb.rules()->goneText + ".", kGreen); break;
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
            case S::Fallen: add("✓", name + " est vaincu !", kGreen); break;
        }
        for (int i = 0; i < w.teamSize(); ++i)
            if (w.member(i).presence == GameWorld::Presence::Down) {
                add("✚", "Relever " + seatName(i) + " (" + std::to_string(static_cast<int>(GameWorld::kReviveSeconds)) + " s près de son âme) · réanimations " +
                             std::to_string(w.revivesUsed()) + " / " + std::to_string(GameWorld::kTeamRevives),
                    Color{255, 138, 138, 255});
                break;
            }
    }

    /// The card of the gigaboss's fall: its results (validated by the server) or back to the fight.
    void drawFall() {
        UIContext& ui = g_.ui;
        Rect box = centred(ui.s(600), ui.s(250));
        box.y = ui.screenH() * 0.18f;
        ui.panel(box, true, 16);
        const std::string name = run_->gigaboss().rules() ? run_->gigaboss().rules()->name : "Le gigaboss";
        fit(ui, name + " EST VAINCU", {box.center().x, box.y + ui.s(40)}, 30, box.w - ui.s(48), kGold, FontFace::Title, Align::Center);
        const std::string fell = run_->gigaboss().rules() ? run_->gigaboss().rules()->fallText : std::string();
        const auto lines = wrap(ui, fell + " Les résultats comptent dès que la majorité de votre instance les a confirmés au serveur.", FontFace::Body, 16,
                                box.w - ui.s(56), 3);
        drawLines(ui, lines, {box.x + ui.s(28), box.y + ui.s(70)}, 16, ui.s(22), kDim);
        ui.pushId("fall");
        const float gap = ui.s(12), bw = (box.w - ui.s(56) - gap) * 0.5f, by = box.bottom() - ui.s(24) - ui.s(52);
        UIContext::ButtonStyle bs;
        bs.primary = true;
        if (ui.button("results", {box.x + ui.s(28), by, bw, ui.s(52)}, "Résultats du raid", bs)) {
            fallCard_ = false;
            g_.app.pushScene(makeRaidResults(g_, entry_.eventId));
        }
        if (ui.button("continue", {box.x + ui.s(28) + bw + gap, by, bw, ui.s(52)}, "Continuer")) fallCard_ = false;
        ui.popId();
    }

    /// 2.71: the objectives' lines (the zone's, its locked passages, its mechanisms, the links of the
    /// titan, the ambush, the gigaboss).
    std::vector<raidhud::Line> objectiveLines(GameWorld& w) {
        std::vector<raidhud::Line> lines;
        const uint32_t mask = run_->mask();
        for (const raid::ZoneObjective& zo : run_->rules().objectives) {
            const raid::ObjectiveInfo& o = branch_->objectives[static_cast<size_t>(zo.index)];
            const bool done = raid::Branch::done(mask, o.index);
            std::string mark = done ? "✓" : o.optional ? "◆" : "→";
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
                lines.push_back({"⇄", name + " : " + why, kSky});
            else if (p.lock.rfind("link:", 0) == 0)
                lines.push_back({"✦", name + " : " + why, kViolet});
            else
                lines.push_back({"▮", name + " : passage verrouillé", kDim});
        }
        // 2.68: the zone's co-op mechanisms (locks, goods lift): the players on their plate.
        for (size_t k = 0; k < run_->rules().coops.size(); ++k) {
            const raid::RaidRun::CoopView v = run_->coop(k);
            const std::string& label = run_->rules().coops[k].label;
            if (v.done) lines.push_back({"✓", label + " : en marche", kGreen});
            else if (v.need > 0) lines.push_back({std::to_string(v.on) + " / " + std::to_string(v.need), label + " : joueurs ensemble sur la plaque", kGold});
        }
        // 2.68: what the other branches of the titan did here (a link on in this zone).
        for (const raid::EffectInfo& e : branch_->effects)
            if (e.kind != "open" && e.inZone(run_->zone()) && raid::Branch::on(mask, e)) lines.push_back({"✦", e.label, kGreen});
        if (!run_->rules().sealedLabel.empty()) lines.push_back({"▮", run_->rules().sealedLabel, kGold});
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

    /// 2.71: the simulation's alerts of the last 8 s (group, branch, danger), then the server's answers.
    std::vector<raidhud::Line> alertLines() {
        std::vector<raidhud::Line> show;
        for (const raid::Alert& a : run_->alerts())
            if (run_->tick() - a.tick < 8 * 120)
                show.push_back({"", a.text, a.kind == raid::Alert::Kind::Branch ? kGreen : a.kind == raid::Alert::Kind::Danger ? kDanger : kGold});
        for (const auto& n : notes_)
            if (clock_ - n.at < 8.0f) show.push_back({"", n.text, n.c});
        if (show.size() > 4) show.erase(show.begin(), show.end() - 4);
        return show;
    }

    /// 2.71: the danger band's text now (the danger announced, then on), empty when none.
    std::string dangerText() {
        const float in = run_->magmaIn();
        if (in < 0.0f) return {};
        const raid::Magma& m = run_->rules().magma;  // 2.68: each branch's own danger
        if (run_->magmaActive()) return m.message;
        if (in <= m.warn + 0.01f) return m.name + " dans " + std::to_string(static_cast<int>(std::ceil(in))) + " s · " + m.hint + " !";
        return {};
    }

    /// 2.71: the network line, or a test's badge (never counted: said where the network's line is).
    std::string statusText(Color& c) {
        if (test_) {
            c = kGold;
            return "TEST ADMIN · non classé · " + std::to_string(testSetup_.players) + " joueurs (" +
                   std::to_string(std::max(0, testSetup_.players - 1)) + " simulés)" + (testSetup_.invincible ? " · invincible" : "");
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
        Rect box = centred(ui.s(600), ui.s(380));
        ui.panel(box, true, 16);
        fit(ui, "PAUSE", {box.center().x, box.y + ui.s(44)}, 40, box.w - ui.s(48), kText, FontFace::Title, Align::Center);
        fit(ui, test_ ? "Le test continue pendant la pause : vous pouvez encore être touché." : "Le groupe continue à jouer : vous pouvez encore être touché.", {box.center().x, box.y + ui.s(88)}, 18, box.w - ui.s(48), kDim,
            FontFace::Body, Align::Center);
        ui.pushId("pause");
        const float bw = box.w - ui.s(80), bh = ui.s(58);
        float y = box.y + ui.s(124);
        UIContext::ButtonStyle bs;
        bs.primary = true;
        if (ui.button("resume", {box.x + ui.s(40), y, bw, bh}, "Reprendre", bs)) paused_ = false;
        y += bh + ui.s(14);
        UIContext::ButtonStyle danger;
        danger.danger = true;
        if (ui.button("quit", {box.x + ui.s(40), y, bw, bh}, test_ ? "Quitter le test (retour à l'administration)" : "Quitter le raid (retour à la carte)", danger))
            leave();
        y += bh + ui.s(18);
        // 2.71: the raid's keys (the large map), measured into the panel.
        const std::string mapKey = actionKey(g_.app.input(), Action::RaidMap);
        const std::string keys = "Commandes du raid · carte agrandie : " + (mapKey.empty() ? std::string("aucune touche") : mapKey) +
                                 " (Vue / Retour à la manette) · à changer dans Paramètres › Commandes";
        const auto lines = wrap(ui, keys, FontFace::Body, 15, box.w - ui.s(80), 2);
        drawLines(ui, lines, {box.x + ui.s(40), y}, 15, ui.s(20), kDim);
        ui.popId();
        if (!pauseOpenedNow_ && ui.backPressed()) paused_ = false;
        pauseOpenedNow_ = false;
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
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    const raid::Branch* branch_ = nullptr;
    std::unique_ptr<NetCoopLink> link_;
    std::unique_ptr<raid::RaidRun> ownRun_;        // a real instance's run
    std::unique_ptr<raid::TestSession> session_;   // 2.71: an administrator's test (its run, its simulated players)
    raid::RaidRun* run_ = nullptr;                 // the one played
    GameRenderer renderer_;
    Hud hud_;
    TexturePtr suit_;
    raidhud::HudAreas hudAreas_;    // 2.71: the game's HUD at the raid layout's places (this frame)
    GameplaySettings hudSettings_;  // 2.71: what the game's HUD is drawn with in the raid
    raidhud::Minimap minimap_;      // 2.71: the zone's terrain baked, the maps
    EnemyArrows::Tracker metEnemies_;  // 2.71: the enemies met in this world (the arrows of the solo levels)
    bool bigMap_ = false;           // 2.71: the large map (Action::RaidMap)
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
    bool pauseOpenedNow_ = false;  // the pause opened this frame: its own key press must not close it
    bool test_ = false;         // 2.68: an administrator's test (solo, local, never reported)
    RaidTestSetup testSetup_;
    bool fallCard_ = false;     // 2.68: the gigaboss fell: « Résultats du raid »
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

}  // namespace cosmic
