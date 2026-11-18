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
// 2.69 (phase 4B): Skullmaw, Dreadfang and Heartbreaker: their own rules drawn over their arenas
// (raidui::BossOverlay), their lines of the objectives, their threats for the arena camera, the
// arrows toward their weak points, their places on the minimap.

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
#include "game/raid/ArenaCamera.h"
#include "game/gameplay/EnemyArrows.h"
#include "game/gameplay/entities/Entities.h"
#include "engine/resources/ResourceManager.h"
#include "saves/Settings.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/RaidGigabossOverlay.h"

namespace cosmic {

namespace {

using namespace raidui;

const Color kMe{255, 210, 122, 255};
const Color kMate{124, 240, 240, 255};
const Color kEnemy{255, 106, 106, 255};
const Color kViolet{201, 168, 255, 255};
const Color kDanger{255, 74, 74, 255};
const Color kHud{11, 10, 20, 214};

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
    bool hidesCursor() const override { return run_ && !catching_ && !connecting_ && !paused_ && !ended_ && !fallCard_; }

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
        run_ = std::make_unique<raid::RaidRun>(ws, *branch_, go.players);
        run_->setCameraFollow(local_);
        if (!run_->begin(link_->raidStateAt(0))) {
            fail_ = "Impossible de charger la première zone du raid.";
            run_.reset();
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

    /// 2.68: an administrator's test: the branch and zone chosen, solo (the 7 other seats vacant).
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
        run_ = std::make_unique<raid::RaidRun>(ws, *branch_, raid::kMaxInstancePlayers);
        raid::RaidRun::Test t;
        t.on = true;
        t.invincible = testSetup_.invincible;
        t.players = testSetup_.players;
        t.startPhase = testSetup_.startPhase;
        run_->setTest(t);
        run_->setCameraFollow(0);
        if (!run_->begin(0, testSetup_.zone)) {
            fail_ = "Impossible de charger la zone du test.";
            run_.reset();
            return;
        }
        hud_.setRaidView(true);
        zoneReady();
    }

    /// 2.68: one step of a test: this computer's input on seat 0, the others vacant.
    void stepTest() {
        InputFrame f = InputFrame::fromPlayerInput(g_.app.input().consumePlayerInput());
        if (paused_) f = InputFrame{};
        if (const auto& offer = run_->world().powerOffer(); offer && offer->player == 0) f.choice = InputFrame::kChoiceLeave;
        frame_.assign(raid::kMaxInstancePlayers, InputFrame{});
        frame_[0] = f;
        for (size_t k = 1; k < frame_.size(); ++k) frame_[k].flags = InputFrame::Vacant;
        run_->step(frame_.data(), static_cast<int>(frame_.size()), 0);
        ++tick_;
        run_->takeReports();  // empty: a test never reports anything
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
            g_.ui.setFocus("raidgame/pause/resume");
        }
    }

    std::string seatName(int seat) const {
        if (seat == local_) return g_.online.username().empty() ? "Vous" : g_.online.username();
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
                // The vitals and powers of this computer's player (display only).
                GameWorld::PlayerScope me(w, std::clamp(local_, 0, w.teamSize() - 1));
                hud_.render(ui, w, view, nullptr, g_.settings.gameplay);
                // Where the HUD put this player's vitals and powers: the raid's panels go around them.
                hudArea_ = {};
                for (HudElement e : {HudElement::Vitals, HudElement::Powers}) {
                    if (!hudVisible(g_.settings.gameplay, e)) continue;
                    const Rect a = hud_.area(ui, w, e, hud_.place(ui, w, g_.settings.gameplay, e));
                    if (a.w > 0.0f) hudArea_ = hudArea_.w > 0.0f ? hudArea_.united(a) : a;
                }
            }
            drawRaidHud(w);
            drawArrows(w, view);
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

    /// A rounded panel of the HUD.
    void hudBox(const Rect& r) {
        g_.ui.r().fillRoundedRect(r, g_.ui.s(12), kHud);
        g_.ui.r().drawRoundedRect(r, g_.ui.s(12), Color{255, 255, 255, 50}, g_.ui.s(1));
    }

    void drawRaidHud(GameWorld& w) {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), m = ui.s(16);
        // Widths first: the side panels, then the minimap in the room left between them.
        const float leftW = std::min(ui.s(270), W * 0.2f), rightW = std::min(ui.s(320), W * 0.24f);
        // The group panel under this player's vitals and powers (the HUD's own places).
        const float groupY = hudArea_.w > 0.0f && hudArea_.x < m + leftW ? hudArea_.bottom() + ui.s(12) : ui.s(14);
        hudRects_.clear();
        if (hudArea_.w > 0.0f) hudRects_.push_back(hudArea_);
        const Rect group = drawGroup(w, {m, groupY, leftW, 0});
        hudRects_.push_back(group);
        // The minimap between what is at the left (the group, the vitals) and the objectives.
        float left = groupY <= ui.s(110) ? group.right() : m;
        if (hudArea_.w > 0.0f && hudArea_.y < ui.s(110)) left = std::max(left, hudArea_.right());
        const float mapX = left + ui.s(14), mapR = W - m - rightW - ui.s(14);
        if (mapR - mapX > ui.s(200)) {
            drawMinimap(w, {mapX, ui.s(14), mapR - mapX, ui.s(96)});
            hudRects_.push_back({mapX, ui.s(14), mapR - mapX, ui.s(96)});
        }
        const Rect obj = drawObjectives(w, {W - m - rightW, ui.s(14), rightW, 0});
        hudRects_.push_back(obj);
        drawAlerts({obj.x, obj.bottom() + ui.s(10), rightW, 0});
        drawDanger();
        drawNetwork();
        drawContribution();
    }

    // ------------------------------------------------------------- 2.68: the gigaboss

    /// The vents over the arena (the generated sheet: closed, open, announced, throwing magma),
    /// the hold of each (a ring), the carapace around the gigaboss while it protects it.
    void drawGigabossWorld(GameWorld& w, const WorldView& view) {
        const raid::Gigaboss& gb = run_->gigaboss();
        if (!gb.present()) return;
        Renderer& r = g_.ui.r();
        bossOverlay_.draw(r, g_.app.resources(), w, gb, view, clock_);  // 2.69: Skullmaw, Dreadfang, Heartbreaker
        if (!vents_) vents_ = g_.app.resources().animation("animations/pyroclast_vents.json");
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
            if (const raid::BossMechanic* m = gb.mechanic()) m->threats(w, f.threats);  // 2.69
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
    /// bare), the allies (their colours), the enemies of the ambush (violet). Kept off the HUD.
    void drawArrows(GameWorld& w, const WorldView& view) {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        const Rect screen{0.0f, 0.0f, W, H};
        const EnemyArrows::Look usual = EnemyArrows::look(false), fight = EnemyArrows::look(true);
        auto tex = g_.app.resources().texture(usual.texture);
        auto ambushTex = g_.app.resources().texture(fight.texture);
        if (!tex || !ambushTex) return;
        EnemyArrows::Layout l;
        l.screen = screen;
        l.arrowSize = ui.s(44);
        l.gap = ui.s(6);
        l.inset = l.arrowSize * 0.5f + ui.s(12);
        l.packGap = ui.s(90);
        l.packSpan = ui.s(260);
        l.badgeSize = {ui.s(44), ui.s(26)};
        l.keepOut = hudRects_;
        if (const Enemy* b = w.activeBoss(); b && b->alive() && hudVisible(g_.settings.gameplay, HudElement::Boss))
            l.keepOut.push_back(hud_.area(ui, w, HudElement::Boss, hud_.place(ui, w, g_.settings.gameplay, HudElement::Boss)));
        auto draw = [&](const std::vector<Rect>& targets, const Texture& arrow, Color tint) {
            if (targets.empty()) return;
            for (const EnemyArrows::Arrow& a : EnemyArrows::layout(l, targets)) {
                ui.r().drawTexture(arrow, nullptr, a.box, tint, BlendMode::Alpha, false, false, a.angle * 57.29578f);
                if (a.count > 1) {
                    const std::string n = "×" + std::to_string(a.count);
                    ui.r().fillRoundedRect(a.badge, a.badge.h * 0.5f, Color{8, 12, 26, 220});
                    fit(ui, n, a.badge.center(), 13, a.badge.w - ui.s(8), tint, FontFace::Bold, Align::Center, true);
                }
                l.keepOut.push_back(a.area);
            }
        };
        const auto away = [&](const Rect& r) { return !view.toScreen(r).intersects(screen); };
        // The boss, or its weak points while they are bare.
        const raid::Gigaboss& gb = run_->gigaboss();
        std::vector<Rect> bossT;
        if (const raid::BossMechanic* m = gb.mechanic()) {
            // 2.69: its weak points (eyes, knuckles, plates, heart) and nailing claws while they stand.
            std::vector<uint32_t> uids;
            m->targets(uids);
            for (uint32_t u : uids)
                if (const Enemy* e = w.findEnemy(u); e && e->alive() && away(e->box)) bossT.push_back(view.toScreen(e->box));
            if (uids.empty())
                if (const Enemy* b = gb.boss(w); b && b->alive() && away(b->box)) bossT.push_back(view.toScreen(b->box));
        } else if (gb.stage() == raid::Gigaboss::Stage::Exposed) {
            for (const auto& e : w.enemies())
                if (e->alive() && e->summonedBy == gb.bossUid() && e->def().id == gb.rules()->weakEnemy && away(e->box)) bossT.push_back(view.toScreen(e->box));
        } else if (const Enemy* b = gb.boss(w); b && b->alive() && away(b->box)) {
            bossT.push_back(view.toScreen(b->box));
        }
        draw(bossT, *tex, kGold);
        // The allies, each in their colour.
        for (int i = 0; i < w.teamSize(); ++i) {
            if (i == local_ || w.member(i).presence != GameWorld::Presence::Playing || !away(w.playerAt(i).box)) continue;
            draw({view.toScreen(w.playerAt(i).box)}, *ambushTex, seatColor(i, local_));
        }
        // The enemies of the ambush under way.
        if (const CombatZoneEntity* amb = w.activeAmbush()) {
            std::vector<Rect> t;
            for (const Enemy* e : amb->fightingEnemies(w))
                if (away(e->box)) t.push_back(view.toScreen(e->box));
            draw(t, *ambushTex, Color::white());
        }
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

    /// This player's contributions at the bottom right (the same numbers every computer counts).
    void drawContribution() {
        const raid::Contribution* me = nullptr;
        for (const raid::Contribution& c : run_->contributions().records())
            if (c.seat == local_) me = &c;  // the latest record of this seat
        if (!me) return;
        UIContext& ui = g_.ui;
        std::string t = "Dégâts " + std::to_string(me->damage / 10) + " · Mécanismes " + std::to_string(me->vents) + " · Sauvetages " +
                        std::to_string(me->rescues) + " · Dégâts évités " + std::to_string(me->avoided / 10);
        if (me->weakPoints > 0) t += " · Points faibles " + std::to_string(me->weakPoints);
        const float w = std::min(textW(ui, t, 13) + ui.s(24), ui.screenW() * 0.4f);
        const Rect r{ui.screenW() - ui.s(16) - w, ui.screenH() - ui.s(16) - ui.s(34), w, ui.s(34)};
        ui.r().fillRoundedRect(r, ui.s(10), kHud);
        hudRects_.push_back(r);
        fit(ui, t, {r.x + ui.s(12), r.center().y}, 13, r.w - ui.s(24), kText);
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
        if (const raid::BossMechanic* m = gb.mechanic(); m && gb.stage() == S::Fight) {
            // 2.69 (phase 4B): its own rules say what to do (mirrors, chains, plates, ribs…).
            std::vector<raid::BossLine> lines;
            m->lines(w, gb, lines);
            for (auto& l : lines) add(std::move(l.mark), std::move(l.text), l.color);
        } else if (m && gb.stage() == S::Absent) {
            add("✓", m->goneLine(name), kGreen);
        } else switch (gb.stage()) {
            case S::Absent: add("✓", name + " est déjà tombé : la Main est éteinte.", kGreen); break;
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
        const raid::BossMechanic* mech = run_->gigaboss().mechanic();
        const auto lines = wrap(ui, mech ? mech->fallCard() : "Le bras droit d'Asterion s'éteint. Les résultats comptent dès que la majorité de votre instance les a confirmés au serveur.",
                                FontFace::Body, 16, box.w - ui.s(56), 3);
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

    Rect drawGroup(GameWorld& w, Rect r) {
        UIContext& ui = g_.ui;
        Renderer& rr = ui.r();
        using P = GameWorld::Presence;
        const float pad = ui.s(10), rowH = ui.s(22);
        int held = 0;
        for (int i = 0; i < w.teamSize(); ++i) held += w.member(i).presence != P::Vacant;
        r.h = pad * 2 + ui.s(24) + rowH * static_cast<float>(w.teamSize()) + ui.s(22);
        hudBox(r);
        const float x = r.x + pad, iw = r.w - pad * 2;
        fit(ui, "[" + entry_.clanTag + "]·" + std::to_string(entry_.group) + " · " + std::to_string(held) + " / 8", {x, r.y + pad + ui.s(9)}, 14, iw, kText,
            FontFace::Title, Align::Left, true);
        float y = r.y + pad + ui.s(24);
        const float nameW = iw * 0.36f, stateW = iw * 0.3f, barX = x + nameW + ui.s(6), barW = iw - nameW - stateW - ui.s(12);
        for (int i = 0; i < w.teamSize(); ++i) {
            const GameWorld::TeamMember& mb = w.member(i);
            const Player& p = w.playerAt(i);
            const float cy = y + rowH * 0.5f;
            const bool vacant = mb.presence == P::Vacant;
            fit(ui, vacant ? "—" : seatName(i), {x, cy}, 13, nameW, vacant ? kDim : seatColor(i, local_), FontFace::Bold, Align::Left, true);
            const float hp = vacant ? 0.0f : static_cast<float>(std::max(0, p.health)) / static_cast<float>(std::max(1, p.maxHealth()));
            const Color hc = hp > 0.6f ? kGreen : hp > 0.3f ? kGold : kOrange;
            rr.fillRoundedRect({barX, cy - ui.s(4), barW, ui.s(8)}, ui.s(4), Color{58, 42, 58, 255});
            if (mb.presence == P::Playing && hp > 0.0f) rr.fillRoundedRect({barX, cy - ui.s(4), barW * hp, ui.s(8)}, ui.s(4), hc);
            std::string st;
            Color sc = kDim;
            switch (mb.presence) {
                case P::Playing: st = p.state() == PlayerState::Dead ? "tombe" : "en jeu"; sc = kGreen; break;
                case P::Down: st = "à terre"; sc = kDanger; break;
                case P::Standby: st = "absent " + std::to_string((GameWorld::kStandbyLimitTicks - mb.standbyTicks + 119) / 120) + " s"; sc = kViolet; break;
                case P::Gone: st = "parti"; break;
                case P::Vacant: st = "place libre"; break;
            }
            fit(ui, st, {r.right() - pad, cy}, 12, stateW, sc, FontFace::Body, Align::Right);
            y += rowH;
        }
        fit(ui, "Réanimations de la zone : " + std::to_string(w.revivesUsed()) + " / " + std::to_string(GameWorld::kTeamRevives), {x, y + ui.s(10)}, 12, iw,
            kGold);
        return r;
    }

    void drawMinimap(GameWorld& w, const Rect& r) {
        UIContext& ui = g_.ui;
        Renderer& rr = ui.r();
        hudBox(r);
        const raid::ZoneInfo* z = branch_->zone(run_->zone());
        // The legend at the right first, the title in the room left.
        const std::string legend = "● vous  ● groupe  ● ennemis  ◆ objectif  ▮ passage";
        float lw = textW(ui, legend, 12);
        const float titleRoom = r.w - ui.s(20);
        bool legendShown = lw <= titleRoom * 0.6f;
        if (legendShown) fit(ui, legend, {r.right() - ui.s(10), r.y + ui.s(13)}, 12, lw, kGold, FontFace::Body, Align::Right);
        fit(ui, "MINIMAP · " + std::string(z ? z->name : "?"), {r.x + ui.s(10), r.y + ui.s(13)}, 12, titleRoom - (legendShown ? lw + ui.s(12) : 0.0f),
            Color{200, 192, 208, 255});
        const Rect strip{r.x + ui.s(10), r.y + ui.s(30), r.w - ui.s(20), r.h - ui.s(40)};
        rr.fillRoundedRect(strip, ui.s(8), Color{42, 20, 20, 255});
        const Rect b = w.level().bounds;
        const float k = std::min(strip.w / std::max(1.0f, b.w), strip.h / std::max(1.0f, b.h));
        const Vec2 o{strip.x + (strip.w - b.w * k) * 0.5f, strip.y + (strip.h - b.h * k) * 0.5f};
        auto at = [&](Vec2 p) { return Vec2{o.x + (p.x - b.x) * k, o.y + (p.y - b.y) * k}; };
        // The magma flow's zone, then the passages, objectives, enemies, the group, me.
        if (run_->rules().magma.present)
            if (const LevelObject* mz = w.level().findObject(run_->rules().magma.zone)) {
                const Vec2 a = at({mz->rect.x, mz->rect.y});
                rr.fillRect({a.x, a.y, mz->rect.w * k, std::max(ui.s(2), mz->rect.h * k)}, run_->magmaActive() ? Color{255, 74, 42, 220} : Color{255, 74, 42, 70});
            }
        for (const raid::Passage& p : run_->rules().passages) {
            const Vec2 a = at({p.rect.x, p.rect.y});
            rr.fillRect({a.x, a.y, std::max(ui.s(3), p.rect.w * k), std::max(ui.s(6), p.rect.h * k)}, run_->passageOpen(p) ? kSky : Color{120, 106, 120, 255});
        }
        if (run_->gigaboss().present()) {
            // 2.68: the vents of the gigaboss (gold while open, green once closed).
            for (const auto& v : run_->gigaboss().vents()) {
                const Vec2 c = at(v.area.center());
                const float s = ui.s(4);
                rr.fillRect({c.x - s, c.y - s, s * 2, s * 2}, v.closed ? kGreen : kGold);
            }
            std::vector<std::pair<Rect, Color>> places;  // 2.69: mirrors, anchors, ribs
            raidui::BossOverlay::places(run_->gigaboss(), places);
            for (const auto& [area, col] : places) {
                const Vec2 c = at(area.center());
                const float s = ui.s(4);
                rr.fillRect({c.x - s, c.y - s, s * 2, s * 2}, col);
            }
        }
        for (size_t ci = 0; ci < run_->rules().coops.size(); ++ci) {  // 2.68: the co-op plates
            const Rect& pr = run_->rules().coops[ci].rect;
            const Vec2 a = at({pr.x, pr.y});
            rr.fillRect({a.x, a.y, std::max(ui.s(4), pr.w * k), std::max(ui.s(3), pr.h * k)}, run_->coop(ci).done ? kGreen : kGold);
        }
        if (!run_->rules().sealedDoor.empty())
            if (const LevelObject* d = w.level().findObject(run_->rules().sealedDoor)) {
                const Vec2 a = at({d->rect.x, d->rect.y});
                rr.fillRect({a.x, a.y, std::max(ui.s(3), d->rect.w * k), std::max(ui.s(6), d->rect.h * k)}, kGold);
            }
        for (const raid::ZoneObjective& zo : run_->rules().objectives) {
            const LevelObject* src = w.level().findObject(zo.source);
            if (!src) continue;
            const Vec2 c = at(src->rect.center());
            const bool done = raid::Branch::done(run_->mask(), zo.index);
            const float s = ui.s(5);
            const Color col = done ? kGreen : branch_->objectives[static_cast<size_t>(zo.index)].optional ? kViolet : Color{255, 176, 74, 255};
            rr.fillTriangle({c.x, c.y - s}, {c.x + s, c.y}, {c.x - s, c.y}, col);
            rr.fillTriangle({c.x, c.y + s}, {c.x + s, c.y}, {c.x - s, c.y}, col);
        }
        for (const auto& e : w.enemies())
            if (e->alive()) rr.fillCircle(at(e->center()), ui.s(2.5f), kEnemy);
        for (int i = 0; i < w.teamSize(); ++i) {
            if (i == local_ || w.member(i).presence != GameWorld::Presence::Playing) continue;
            rr.fillCircle(at(w.playerAt(i).center()), ui.s(3.5f), kMate);
        }
        if (local_ >= 0 && local_ < w.teamSize()) rr.fillCircle(at(w.playerAt(local_).center()), ui.s(4.5f), kMe);
    }

    Rect drawObjectives(GameWorld& w, Rect r) {
        UIContext& ui = g_.ui;
        const float pad = ui.s(10), iw = r.w - pad * 2, markW = ui.s(44);
        struct Line {
            std::string mark, text;
            Color c;
            std::vector<std::string> lines;
        };
        std::vector<Line> lines;
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
            lines.push_back({mark, o.label, c, {}});
        }
        // The locked passages of the zone: what opens them, with its progress.
        for (const raid::Passage& p : run_->rules().passages) {
            if (run_->passageOpen(p) || p.lock.empty()) continue;
            const raid::ZoneInfo* to = branch_->zone(p.to);
            const std::string name = to ? to->name : "?";
            const std::string why = branch_->lockText(p.lock).objective;  // 2.68: each branch's own words
            if (p.lock == "generators")
                lines.push_back({std::to_string(branch_->generators(mask)) + " / " + std::to_string(branch_->generatorsNeeded), name + " : " + why, kGold, {}});
            else if (p.lock == "shortcut")
                lines.push_back({"⇄", name + " : " + why, kSky, {}});
            else if (p.lock.rfind("link:", 0) == 0)
                lines.push_back({"✦", name + " : " + why, kViolet, {}});
            else
                lines.push_back({"▮", name + " : passage verrouillé", kDim, {}});
        }
        // 2.68: the zone's co-op mechanisms (locks, goods lift): the players on their plate.
        for (size_t k = 0; k < run_->rules().coops.size(); ++k) {
            const raid::RaidRun::CoopView v = run_->coop(k);
            const std::string& label = run_->rules().coops[k].label;
            if (v.done) lines.push_back({"✓", label + " : en marche", kGreen, {}});
            else if (v.need > 0) lines.push_back({std::to_string(v.on) + " / " + std::to_string(v.need), label + " : joueurs ensemble sur la plaque", kGold, {}});
        }
        // 2.68: what the other branches of the titan did here (a link on in this zone).
        for (const raid::EffectInfo& e : branch_->effects)
            if (e.kind != "open" && e.inZone(run_->zone()) && raid::Branch::on(mask, e)) lines.push_back({"✦", e.label, kGreen, {}});
        if (!run_->rules().sealedLabel.empty()) lines.push_back({"▮", run_->rules().sealedLabel, kGold, {}});
        if (const CombatZoneEntity* amb = w.activeAmbush()) {
            // 2.68: the ambush under way (its storeys, its waves, the enemies left: violet arrows too).
            lines.push_back({std::to_string(amb->currentWave() + 1 > amb->waveCount() ? amb->waveCount() : amb->currentWave() + 1) + " / " +
                                 std::to_string(amb->waveCount()),
                             "Embuscade : vague en cours, " + std::to_string(amb->enemiesLeft(w)) + " ennemis (flèches violettes)", kViolet, {}});
        }
        {
            // 2.68: the gigaboss of the zone (its stage, the vents, the weak points, the allies down).
            std::vector<std::string> marks, texts;
            std::vector<Color> colors;
            gigabossLines(w, marks, texts, colors);
            for (size_t i = 0; i < marks.size(); ++i) lines.push_back({marks[i], texts[i], colors[i], {}});
        }
        if (lines.empty()) lines.push_back({"·", "Aucun objectif dans cette zone : avancez vers les passages.", kDim, {}});
        float h = pad * 2 + ui.s(22);
        for (auto& l : lines) {
            l.lines = wrap(ui, l.text, FontFace::Body, 13, iw - markW, 3);
            h += static_cast<float>(std::max<size_t>(1, l.lines.size())) * ui.s(18) + ui.s(4);
        }
        r.h = h;
        hudBox(r);
        fit(ui, "OBJECTIFS · " + std::string(branch_->zone(run_->zone()) ? branch_->zone(run_->zone())->name : ""), {r.x + pad, r.y + pad + ui.s(8)}, 13, iw, kText,
            FontFace::Title);
        float y = r.y + pad + ui.s(22);
        for (const auto& l : lines) {
            fit(ui, l.mark, {r.x + pad, y + ui.s(9)}, 13, markW - ui.s(6), l.c, FontFace::Bold);
            y += drawLines(ui, l.lines, {r.x + pad + markW, y}, 13, ui.s(18), kText) + ui.s(4);
        }
        return r;
    }

    void drawAlerts(Rect r) {
        UIContext& ui = g_.ui;
        struct A {
            std::string text;
            Color c;
        };
        std::vector<A> show;
        // The simulation's alerts of the last 8 s (group, branch, danger), then the server's answers.
        for (const raid::Alert& a : run_->alerts())
            if (run_->tick() - a.tick < 8 * 120)
                show.push_back({a.text, a.kind == raid::Alert::Kind::Branch ? kGreen : a.kind == raid::Alert::Kind::Danger ? kDanger : kGold});
        for (const auto& n : notes_)
            if (clock_ - n.at < 8.0f) show.push_back({n.text, n.c});
        if (show.size() > 4) show.erase(show.begin(), show.end() - 4);
        float y = r.y;
        const float pad = ui.s(10);
        for (const auto& a : show) {
            const auto lines = wrap(ui, a.text, FontFace::Body, 13, r.w - pad * 2 - ui.s(4), 2);
            const float h = static_cast<float>(lines.size()) * ui.s(18) + pad;
            if (y + h > ui.screenH() * 0.58f) break;  // never down to the danger band (62 %)
            ui.r().fillRoundedRect({r.x, y, r.w, h}, ui.s(8), Color{20, 14, 18, 220});
            hudRects_.push_back({r.x, y, r.w, h});
            ui.r().fillRect({r.x, y, ui.s(4), h}, a.c);
            drawLines(ui, lines, {r.x + pad + ui.s(4), y + pad * 0.5f}, 13, ui.s(18), kText);
            y += h + ui.s(6);
        }
    }

    void drawDanger() {
        UIContext& ui = g_.ui;
        const float in = run_->magmaIn();
        if (in < 0.0f) return;
        std::string t;
        const raid::Magma& m = run_->rules().magma;  // 2.68: each branch's own danger
        if (run_->magmaActive()) t = m.message;
        else if (in <= m.warn + 0.01f) t = m.name + " dans " + std::to_string(static_cast<int>(std::ceil(in))) + " s · " + m.hint + " !";
        else return;
        const float W = ui.screenW();
        const float tw = std::min(textW(ui, t, 20, FontFace::Bold) + ui.s(40), W * 0.6f);
        const Rect band{(W - tw) * 0.5f, ui.screenH() * 0.62f, tw, ui.s(44)};
        ui.r().fillRoundedRect(band, ui.s(8), Color{120, 20, 20, 210});
        hudRects_.push_back(band);
        ui.r().drawRoundedRect(band, ui.s(8), kDanger, ui.s(2));
        fit(ui, t, {band.center().x, band.center().y}, 20, band.w - ui.s(24), Color{255, 200, 200, 255}, FontFace::Bold, Align::Center);
    }

    void drawNetwork() {
        UIContext& ui = g_.ui;
        std::string t;
        if (test_) {
            // 2.68: an administrator's test: never counted, said where the network's line is.
            t = "TEST ADMIN · non classé · " + std::to_string(testSetup_.players) + " joueurs simulés" + (testSetup_.invincible ? " · invincible" : "");
            const float w = std::min(textW(ui, t, 13, FontFace::Bold) + ui.s(24), ui.screenW() * 0.4f);
            const Rect r{ui.s(16), ui.screenH() - ui.s(16) - ui.s(34), w, ui.s(34)};
            ui.r().fillRoundedRect(r, ui.s(10), Color{120, 20, 40, 230});
            hudRects_.push_back(r);
            ui.r().drawRoundedRect(r, ui.s(10), kGold, ui.s(1.5f));
            fit(ui, t, {r.x + ui.s(12), r.center().y}, 13, r.w - ui.s(24), kGold, FontFace::Bold);
            return;
        }
        if (waiting_ > 0.4f) {
            const std::string who = link_->waitingFor();
            t = who.empty() ? "En attente du réseau…" : "En attente de " + who + "…";
        } else {
            t = "Zone " + std::to_string(run_->zone()) + " sur 8 · délai d'entrée " + std::to_string(link_->inputDelay()) + " images";
        }
        const float w = std::min(textW(ui, t, 13) + ui.s(24), ui.screenW() * 0.4f);
        const Rect r{ui.s(16), ui.screenH() - ui.s(16) - ui.s(34), w, ui.s(34)};
        ui.r().fillRoundedRect(r, ui.s(10), kHud);
        hudRects_.push_back(r);
        fit(ui, t, {r.x + ui.s(12), r.center().y}, 13, r.w - ui.s(24), waiting_ > 0.4f ? kGold : kText);
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
        Rect box = centred(ui.s(560), ui.s(320));
        ui.panel(box, true, 16);
        fit(ui, "PAUSE", {box.center().x, box.y + ui.s(44)}, 40, box.w - ui.s(48), kText, FontFace::Title, Align::Center);
        fit(ui, "Le groupe continue à jouer pendant la pause.", {box.center().x, box.y + ui.s(88)}, 18, box.w - ui.s(48), kDim, FontFace::Body, Align::Center);
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
        ui.popId();
        if (ui.backPressed()) paused_ = false;
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
    std::unique_ptr<raid::RaidRun> run_;
    GameRenderer renderer_;
    Hud hud_;
    TexturePtr suit_;
    Rect hudArea_;  // the HUD's vitals and powers of this player (last frame)
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
    bool test_ = false;         // 2.68: an administrator's test (solo, local, never reported)
    RaidTestSetup testSetup_;
    bool fallCard_ = false;     // 2.68: the gigaboss fell: « Résultats du raid »
    raid::ArenaCamera arenaCam_;   // 2.68: the camera of a gigaboss's arena (display only)
    std::vector<Rect> hudRects_;   // 2.68: what the HUD covers this frame (kept clear of the arrows)
    WorldView view_;
    AnimationSetPtr vents_;     // 2.68: the sheet of its vents
    raidui::BossOverlay bossOverlay_;  // 2.69 (phase 4B): the own rules of Skullmaw, Dreadfang, Heartbreaker
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
