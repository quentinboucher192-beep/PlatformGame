// Before a run (Ghost Racing): which ghosts to race against, and the training mode.
//
// Ghosts: mine (best time, previous record, last run of this exact level version), the
// replays downloaded or imported, and online the world record and friends' records (shown
// with their category and version; downloaded when chosen, kept in the local cache). Every
// replay is checked again when the run loads it. Training: start at a checkpoint, race the
// segment to the next one against the ghosts, again and again; never a record.
//
// Section training (2.30.2): the segment chosen is shown on the real level drawn small (its
// start, its end, the path of my record through it), with its references (best segment of
// my full runs, its time in my record, my best training time), the Sum of Best of the level,
// and the powers carried (the route's ones, or chosen, remembered per segment).
//
// 2.60.3: the Sum of Best counts the section trainings too (each segment's best, full runs and
// trainings together), and « Regarder » plays it: the best segments one after the other, each
// from the exact replay of the run or the attempt that did it (SobMontageScene).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/SobMontage.h"
#include "game/ghost/TrainingSegments.h"
#include "game/ghost/WorldRecordSplits.h"
#include "engine/core/I18n.h"
#include "game/online/LevelFingerprint.h"
#include "game/player/Player.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"
#include "ui/screens/TasUi.h"
#include "ui/widgets/FileDialogs.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// Section training: the powers carried on a segment, chosen among the level's ones.
struct PowerPick {
    std::vector<std::string> available;  // the level's powers (start powers, tablets)
    std::vector<std::string> defaults;   // the route's ones (start powers and tablets before the start)
    std::vector<std::string> chosen;
    int slots = 2;
    std::string segment;  // its name (« Seuil I → Galerie II »)
    PowerKeyMap keys;     // 2.31: the key of each power in this level (the player's, else the level's)
};
std::unique_ptr<Scene> makeTrainingPowers(GameServices& g, PowerPick pick, std::function<void(std::vector<std::string>)> done);

class GhostSetupScene : public Scene {
public:
    GhostSetupScene(GameServices& g, std::string levelId, GhostSetupRequest req) : g_(g), levelId_(std::move(levelId)), req_(std::move(req)) {}
    bool isOverlay() const override { return true; }

    void onEnter() override {
        auto lvl = g_.content.loadLevel(levelId_, &error_);
        if (!lvl) return;
        level_ = *lvl;
        if (const Json* c = g_.online.communityInstall(levelId_)) {
            community_ = true;
            communityId_ = jstr(*c, "id");
            version_ = jget<int>(*c, "version", 0);
            hash_ = fingerprintGameplay(g_.content, level_);
        } else if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, levelId_)) {
            official_ = true;
            hash_ = fp->hash;
            version_ = fp->version;
        } else {
            hash_ = fingerprintGameplay(g_.content, level_);
        }
        // The route gives the checkpoints' order (training) — off the main thread.
        route_ = std::async(std::launch::async, [l = level_, &content = g_.content] { return buildProgressTrack(l, content); });
        // The machine's run first (« TAS », its category chosen in « TAS de ce niveau »), then mine
        // (best, previous, last), then what was downloaded or imported.
        initTas();
        for (GhostEntry& e : g_.ghosts.localGhosts(levelId_)) addReady(std::move(e));
        for (GhostEntry& e : g_.ghosts.cachedFor(levelId_)) addReady(std::move(e));
        for (GhostEntry& e : g_.ghosts.files())
            if (e.meta.levelId == levelId_ || e.meta.levelHash == hash_) addReady(std::move(e));
        // By default: my best time on this version (and the TAS when asked: « Course contre le fantôme TAS »).
        if (tasRow_ >= 0 && !req_.tasCategory.empty()) select(static_cast<size_t>(tasRow_), true);
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].entry.meta.source == "pb" && rows_[i].sameVersion) {
                select(i, true);
                break;
            }
        // Section training: my record of this version (its segments, its path) and my last run.
        for (const Row& r : rows_) {
            if (!r.sameVersion || r.state != Row::State::Ready) continue;
            if (r.entry.meta.source == "pb" && !pbEntry_) pbEntry_ = r.entry;
            if (r.entry.meta.source == "last" && !lastEntry_) lastEntry_ = r.entry;
        }
        levelKey_ = GhostStore::levelKey(levelId_, hash_);
        if (!req_.recordId.empty()) preselectOnline();
        if (req_.training) training_ = true;  // back from a training: the same segment (set once the route is known)
        fetchOnline();
        g_.ui.setFocus("gsetup/launch");
    }

    void update(float dt) override {
        t_ += dt;
        if (route_.valid() && route_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            routeTrack_ = route_.get();
            const LevelProgressTrack& t = routeTrack_;
            // Checkpoints in the order of the route.
            std::vector<std::pair<float, const LevelObject*>> cps;
            for (const LevelObject& o : level_.objects)
                if (o.type == "checkpoint") cps.push_back({t.valid() ? t.fractionAt(o.rect.bottomCenter()) : o.rect.x, &o});
            std::sort(cps.begin(), cps.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [at, o] : cps) checkpoints_.push_back({o->id, o->propStr("label"), o->rect.bottomCenter(), at});
            // Power tablets in the order of the route (training: what was picked up before).
            for (const LevelObject& o : level_.objects)
                if (o.type == "power_unlock") tablets_.push_back({t.valid() ? t.fractionAt(o.rect.center()) : o.rect.x, o.propStr("power", "dash")});
            std::sort(tablets_.begin(), tablets_.end());
            // The powers of this level (its start powers, then its tablets in the route's order).
            for (const std::string& id : level_.startPowers)
                if (g_.content.power(id) && std::find(levelPowers_.begin(), levelPowers_.end(), id) == levelPowers_.end()) levelPowers_.push_back(id);
            for (const auto& [at, id] : tablets_)
                if (g_.content.power(id) && std::find(levelPowers_.begin(), levelPowers_.end(), id) == levelPowers_.end()) levelPowers_.push_back(id);
            routeReady_ = true;
            refsDirty_ = true;
            if (req_.training) {
                // The segment the training was on: from its start checkpoint (0 = the level's start).
                startIdx_ = 0;
                for (size_t i = 0; i < checkpoints_.size(); ++i)
                    if (checkpoints_[i].id == req_.trainingCheckpoint) startIdx_ = static_cast<int>(i) + 1;
                req_.training = false;
            }
            if (req_.chains) {
                // 2.91: back from an attempt launched by « Segments et enchaînements »: that screen again.
                req_.chains = false;
                ChainTrainingRequest r = chainRequest();
                r.from = req_.chainFrom;
                r.to = req_.chainTo;
                r.pickFirst = false;
                g_.app.pushScene(makeChainTraining(g_, std::move(r)));
            }
        }
        if (routeReady_) updateTraining();
        if (routeReady_ && !myOnline_.empty()) importMyOnlineRuns();
        if (importDialog_ >= 0)
            if (auto res = FileDialogs::result(importDialog_)) {
                importDialog_ = -1;
                if (!res->empty()) {
                    std::string err;
                    if (auto e = g_.ghosts.importFile(Paths::fromUtf8(*res), &err)) {
                        addReady(std::move(*e));
                        Row& r = rows_.back();
                        if (!r.sameVersion && r.entry.meta.levelId != levelId_) {
                            say("Ce replay est celui d'un autre niveau (" + r.entry.meta.levelId + ") : il ne peut pas courir ici.", true);
                            rows_.pop_back();
                        } else {
                            select(rows_.size() - 1, true);
                            say("Replay importé.", false);
                        }
                    } else {
                        say("Replay refusé : " + err, true);
                    }
                }
            }
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 252});
        const float m = ui.s(48);
        Rect panel{m, ui.s(36), ui.screenW() - 2 * m, ui.screenH() - ui.s(72)};
        ui.panel(panel, true, 18);
        tasui::pieces("setup").clear(panel);
        ui.pushId("gsetup");
        float x = panel.x + ui.s(30), y = panel.y + ui.s(28), w = panel.w - ui.s(60);
        ui.label("COURSE CONTRE DES FANTÔMES", {x, y + ui.s(20)}, 34, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        std::string sub = level_.name.empty() ? levelId_ : level_.name;
        if (official_) sub += "  ·  version " + std::to_string(version_) + " (classée)";
        else if (community_) sub += "  ·  niveau de joueur, version " + std::to_string(version_);
        else sub += "  ·  niveau local";
        ui.label(ui.elide(sub, FontFace::Body, 18, w), {x, y + ui.s(60)}, 18, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        float top = y + ui.s(92), bottom = panel.bottom() - ui.s(86);
        if (!error_.empty()) {
            ui.labelWrapped("Niveau introuvable : " + error_, {x, top}, w, 18, ui.theme().danger);
        } else {
            float listW = w * 0.52f;
            drawList({x, top, listW, bottom - top});
            drawSide({x + listW + ui.s(24), top, w - listW - ui.s(24), bottom - top});
        }
        // Bottom bar.
        float by = panel.bottom() - ui.s(70);
        UIContext::ButtonStyle primary;
        primary.primary = true;
        primary.enabled = error_.empty() && !(training_ && !routeReady_);
        bool launch = ui.button("launch", {panel.right() - ui.s(30) - ui.s(300), by, ui.s(300), ui.s(54)},
                                training_ ? "Lancer l'entraînement" : "Lancer la course", primary);
        bool cancel = ui.button("cancel", {panel.right() - ui.s(30) - ui.s(300) - ui.s(16) - ui.s(200), by, ui.s(200), ui.s(54)}, "Annuler");
        if (msgTimer_ > 0.0f) {
            float room = panel.w - ui.s(60) - ui.s(540);
            ui.labelWrapped(msg_, {x, by + ui.s(6)}, room, fz(ui, 16), msgError_ ? ui.theme().danger : ui.theme().success);
        } else {
            float hx = x;
            const float hh = ui.s(30), room = panel.w - ui.s(60) - ui.s(540);
            const float backW = Glyphs::measureAction(ui, Action::MenuBack, hh) + hh * 0.3f + ui.text().measure("Annuler", FontFace::Body, hh * 0.62f).x;
            if (tasCats_.size() > 1) {
                // Q / E, LB / RB: the category of the TAS (the list and the block follow it).
                const std::string t = "Catégorie du TAS";
                const float tw = Glyphs::measureAction(ui, Action::MenuTabPrev, hh) + ui.s(4) + Glyphs::measureAction(ui, Action::MenuTabNext, hh) + hh * 0.3f +
                                 ui.text().measure(t, FontFace::Body, hh * 0.62f).x;
                if (tw + ui.s(28) + backW <= room) {
                    const float x0 = hx;
                    hx += Glyphs::drawAction(ui, Action::MenuTabPrev, {hx, by + ui.s(27)}, hh) + ui.s(4);
                    hx += Glyphs::drawHint(ui, Action::MenuTabNext, t, {hx, by + ui.s(27)}, hh) + ui.s(28);
                    tasui::pieces("setup").add("hint/tas", {x0, by + ui.s(12), hx - ui.s(28) - x0, hh});
                }
            }
            Glyphs::drawHint(ui, Action::MenuBack, "Annuler", {hx, by + ui.s(27)}, hh);
            tasui::pieces("setup").add("hint/back", {hx, by + ui.s(12), backW, hh});
        }
        if (tasCats_.size() > 1 && ui.acceptsInput() && !ui.editingText()) {
            const int n = static_cast<int>(tasCats_.size());
            const int k = static_cast<int>(std::find(tasCats_.begin(), tasCats_.end(), tasCat_) - tasCats_.begin());
            if (g_.app.input().menuPressed(Action::MenuTabPrev)) setTasCategory(tasCats_[static_cast<size_t>((k + n - 1) % n)]);
            else if (g_.app.input().menuPressed(Action::MenuTabNext)) setTasCategory(tasCats_[static_cast<size_t>((k + 1) % n)]);
        }
        tasui::pieces("setup").add("button/launch", {panel.right() - ui.s(30) - ui.s(300), by, ui.s(300), ui.s(54)});
        tasui::pieces("setup").add("button/cancel", {panel.right() - ui.s(30) - ui.s(300) - ui.s(16) - ui.s(200), by, ui.s(200), ui.s(54)});
        ui.popId();
        if (launch && primary.enabled) start();
        else if (cancel || ui.backPressed()) g_.app.popScene();
        ui.end();
    }

private:
    struct Row {
        GhostEntry entry;
        std::string recordId, downloadPath;
        enum class State { Ready, Remote, Downloading, Failed } state = State::Ready;
        bool selected = false;
        bool sameVersion = true;
        bool tas = false;  // the machine's run (« TAS »): never a player's, never ranked
        std::string error;
    };
    struct Checkpoint {
        std::string id, label;
        Vec2 feet;
        float at = 0.0f;
    };

    void say(const std::string& m, bool error) {
        msg_ = m;
        msgError_ = error;
        msgTimer_ = 5.0f;
    }

    void addReady(GhostEntry e) {
        for (const Row& r : rows_)
            if (r.entry.file == e.file && !e.file.empty()) return;
        Row r;
        r.sameVersion = e.meta.levelHash == hash_;
        r.recordId = e.meta.recordId;
        r.entry = std::move(e);
        rows_.push_back(std::move(r));
    }

    Row* byRecord(const std::string& id) {
        for (Row& r : rows_)
            if (!id.empty() && r.recordId == id) return &r;
        return nullptr;
    }

    int selectedCount() const {
        int n = 0;
        for (const Row& r : rows_) n += r.selected ? 1 : 0;
        return n;
    }

    void select(size_t i, bool on) {
        Row& r = rows_[i];
        if (on && !r.selected && selectedCount() >= g_.settings.ghosts.maxGhosts) {
            say(std::to_string(g_.settings.ghosts.maxGhosts) + " fantômes au plus en même temps (réglable dans « Options d'affichage »).", true);
            return;
        }
        r.selected = on;
        if (on) order_.push_back(i);
        else order_.erase(std::remove(order_.begin(), order_.end(), i), order_.end());
        if (on && (r.state == Row::State::Remote || r.state == Row::State::Failed)) download(i);
    }

    // -------------------------------------------------------------- the TAS

    /// The level's TAS (its categories shown for this version): the row « TAS » at the head of the list.
    void initTas() {
        for (RunCategory c : kAllCategories)
            if (tas::catalog().tasFor(g_, levelId_, c)) tasCats_.push_back(c);
        if (tasCats_.empty()) return;  // no TAS for this version: nothing of it on this screen
        tasCat_ = tasCats_.front();
        RunCategory asked = RunCategory::AnyPercent;
        if (!req_.tasCategory.empty() && categoryFromId(req_.tasCategory, asked) && hasTasCategory(asked)) tasCat_ = asked;
        else if (hasTasCategory(RunCategory::AnyPercent)) tasCat_ = RunCategory::AnyPercent;
        Row r;
        r.tas = true;
        tasRow_ = static_cast<int>(rows_.size());
        rows_.push_back(std::move(r));
        refreshTasRow();
    }
    bool hasTasCategory(RunCategory c) const { return std::find(tasCats_.begin(), tasCats_.end(), c) != tasCats_.end(); }
    const tas::TasRun* tasRun() { return tasCats_.empty() ? nullptr : tas::catalog().tasFor(g_, levelId_, tasCat_); }
    void refreshTasRow() {
        const tas::TasRun* t = tasRun();
        if (tasRow_ < 0 || !t) return;
        Row& r = rows_[static_cast<size_t>(tasRow_)];
        r.entry = t->ghost();
        r.sameVersion = t->levelHash == hash_;
    }
    void setTasCategory(RunCategory c) {
        if (!hasTasCategory(c) || c == tasCat_) return;
        tasCat_ = c;
        refreshTasRow();
    }
    /// « Fiche du TAS… »: its « Course contre le TAS » comes back here with the TAS checked.
    void openTasSheet() {
        auto alive = std::weak_ptr<int>(life_);
        tasui::openSheet(g_, levelId_, tasCat_, [this, alive](RunCategory c) {
            if (!alive.lock()) return;
            setTasCategory(c);
            if (tasRow_ >= 0 && !rows_[static_cast<size_t>(tasRow_)].selected) select(static_cast<size_t>(tasRow_), true);
        });
    }

    // -------------------------------------------------------------- online

    void addRemote(const Json& entry, const std::string& source, bool officialBoard) {
        std::string rid = jstr(entry, "record_id");
        if (rid.empty() || byRecord(rid)) return;
        const Json& p = entry.value("player", Json::object());
        GhostMeta m;
        m.source = source;
        m.label = jstr(p, "username");
        m.playerId = jstr(p, "id");
        m.levelId = levelId_;
        m.levelHash = hash_;
        m.recordId = rid;
        m.category = officialBoard ? "ANY_PERCENT" : "";
        m.levelVersion = version_;
        m.timeMs = jget<int64_t>(entry, "time_ms", 0);
        m.date = jstr(entry, "achieved_at");
        m.validated = true;
        m.community = !officialBoard;
        m.suit = jstr(p, "suit");  // the suit of its player (2.23), checked when drawn
        m.trail = jstr(p, "trail");  // 2.59: their trail, drawn behind the ghost (checked the same way)
        Row r;
        r.recordId = rid;
        r.sameVersion = true;
        if (auto cached = g_.ghosts.cached(rid)) {
            r.entry = std::move(*cached);
            r.entry.meta.source = source;  // what it is to this player now (world / friend)
            r.entry.meta.suit = m.suit;      // and what they wear now
            r.entry.meta.trail = m.trail;
            r.sameVersion = r.entry.meta.levelHash == hash_;
        } else {
            r.entry.meta = m;
            r.state = Row::State::Remote;
            r.downloadPath = officialBoard ? "/api/v1/records/" + rid + "/replay" : "/api/v1/community/records/" + rid + "/replay";
        }
        rows_.push_back(std::move(r));
        if (!req_.recordId.empty() && rid == req_.recordId) select(rows_.size() - 1, true);
    }

    void preselectOnline() {
        if (Row* r = byRecord(req_.recordId); r && !r->selected) select(static_cast<size_t>(r - rows_.data()), true);
    }

    void fetchOnline() {
        if (!g_.online.usable() || !g_.settings.online.enabled) {
            onlineNote_ = "Hors ligne : seuls les fantômes enregistrés sur cet ordinateur sont proposés.";
            return;
        }
        auto alive = std::weak_ptr<int>(life_);
        if (official_) {
            onlineBusy_ += 1;
            g_.online.get("/api/v1/leaderboards/level/" + levelId_ + "?category=ANY_PERCENT&limit=10&offset=0&period=all",
                          [this, alive](const ApiResult& r) {
                              if (!alive.lock()) return;
                              --onlineBusy_;
                              if (!r.ok) return note(r.message);
                              for (const Json& e : r.data.value("entries", Json::array())) {
                                  // 2.91: my own verified run joins my Sum of Best (« En ligne »).
                                  if (jget<bool>(e, "has_replay", false) && jget<bool>(e, "is_me", false))
                                      myOnline_.push_back({e, "/api/v1/records/" + jstr(e, "record_id") + "/replay"});
                                  if (!jget<bool>(e, "has_replay", false) || jget<bool>(e, "is_me", false)) continue;
                                  addRemote(e, jget<int>(e, "rank", 0) == 1 ? "world" : "record", true);
                              }
                          });
            if (g_.online.loggedIn()) {
                onlineBusy_ += 1;
                g_.online.get("/api/v1/leaderboards/friends?category=ANY_PERCENT&limit=10&offset=0&period=all&level_id=" + levelId_,
                              [this, alive](const ApiResult& r) {
                                  if (!alive.lock()) return;
                                  --onlineBusy_;
                                  if (!r.ok) return;
                                  for (const Json& e : r.data.value("entries", Json::array()))
                                      if (jget<bool>(e, "has_replay", false) && !jget<bool>(e, "is_me", false)) addRemote(e, "friend", true);
                              });
            }
        } else if (community_) {
            onlineBusy_ += 1;
            g_.online.get("/api/v1/community/levels/" + communityId_ + "/leaderboard?version=" + std::to_string(version_),
                          [this, alive](const ApiResult& r) {
                              if (!alive.lock()) return;
                              --onlineBusy_;
                              if (!r.ok) return note(r.message);
                              const std::set<std::string>& friends = g_.online.friendIds();
                              for (const Json& e : r.data.value("entries", Json::array())) {
                                  if (jget<bool>(e, "me", false)) {
                                      if (jget<bool>(e, "has_replay", false))  // 2.91: my verified run (« En ligne »)
                                          myOnline_.push_back({e, "/api/v1/community/records/" + jstr(e, "record_id") + "/replay"});
                                      continue;
                                  }
                                  std::string pid = jstr(e.value("player", Json::object()), "id");
                                  if (jget<int>(e, "rank", 0) == 1) addRemote(e, "world", false);
                                  else if (friends.count(pid)) addRemote(e, "friend", false);
                              }
                          });
        }
    }

    void note(const std::string& m) { onlineNote_ = "En ligne : " + m; }

    void download(size_t i) {
        Row& r = rows_[i];
        if (r.downloadPath.empty()) return;
        r.state = Row::State::Downloading;
        r.error.clear();
        GhostMeta meta = r.entry.meta;
        auto alive = std::weak_ptr<int>(life_);
        g_.online.getBinary(r.downloadPath, [this, alive, i, meta](const HttpResponse& resp) {
            if (!alive.lock() || i >= rows_.size()) return;
            Row& row = rows_[i];
            std::string err;
            if (resp.networkFailure()) err = "Téléchargement interrompu (" + resp.errorMessage + ").";
            else if (resp.status != 200) err = parseApiResponse(resp).message;
            else {
                std::vector<uint8_t> bytes(resp.body.begin(), resp.body.end());
                // Kept even when made on another version of the level: raced, never as an
                // official comparison (the row says so).
                if (g_.ghosts.storeDownload(bytes, meta, &err, false)) {
                    if (auto e = g_.ghosts.cached(meta.recordId)) {
                        row.entry = std::move(*e);
                        row.entry.meta.source = meta.source;
                        row.sameVersion = row.entry.meta.levelHash == hash_;
                        row.state = Row::State::Ready;
                        g_.ghosts.trimCache(static_cast<uint64_t>(g_.settings.ghosts.cacheMB) * 1024 * 1024);
                        return;
                    }
                    err = "Écriture impossible.";
                }
            }
            row.state = Row::State::Failed;
            row.error = err.empty() ? "Téléchargement impossible." : err;
            row.selected = false;
            order_.erase(std::remove(order_.begin(), order_.end(), i), order_.end());
        }, GhostStore::kMaxReplayBytes);
    }

    // -------------------------------------------------------------- drawing

    Color kindColor(const GhostMeta& m) const {
        const GhostSettings& s = g_.settings.ghosts;
        if (m.source == "tas") return tasui::kTas;  // the machine's colour, outside the ghosts' palette
        bool mine = m.source == "pb" || m.source == "prev" || m.source == "last";
        return ghostPaletteColor(mine ? s.colorMine : m.source == "world" ? s.colorWorld : m.source == "friend" ? s.colorFriend : s.colorOther);
    }

    void drawList(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.label("Fantômes disponibles", {area.x, area.y}, 20, th.text, Align::Left, FontFace::Bold);
        std::string count = std::to_string(selectedCount()) + " / " + std::to_string(g_.settings.ghosts.maxGhosts) + " choisis";
        ui.label(count, {area.right(), area.y + ui.s(2)}, fz(ui, 16), th.textDim, Align::Right);
        Rect list{area.x, area.y + ui.s(36), area.w, area.h - ui.s(36) - ui.s(64)};
        float rh = ui.s(70), gap = ui.s(8);
        ui.beginScroll("list", list, std::max(1.0f, static_cast<float>(rows_.size())) * (rh + gap));
        float y = list.y - ui.scrollOffset();
        if (rows_.empty()) {
            ui.labelWrapped(onlineBusy_ > 0 ? "Recherche des fantômes en ligne…"
                                            : "Aucun fantôme pour ce niveau : terminez-le une première fois (votre course devient votre fantôme), "
                                              "ou importez un replay.",
                            {list.x + ui.s(8), y + ui.s(8)}, list.w - ui.s(30), fz(ui, 17), th.textDim);
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            Row& r = rows_[i];
            Rect row{list.x, y, list.w - ui.s(14), rh};
            y += rh + gap;
            UIContext::ButtonStyle st;
            st.selected = r.selected;
            if (ui.button("g" + std::to_string(i), row, "", st)) select(i, !r.selected);
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            if (r.tas) {
                drawTasRow(r, row, list);
                continue;
            }
            // Check box, colour, then the text.
            Rect box{row.x + ui.s(14), row.center().y - ui.s(12), ui.s(24), ui.s(24)};
            ui.r().drawRoundedRect(box, ui.s(5), r.selected ? th.accent : th.textDim, ui.s(2));
            if (r.selected) ui.r().fillRoundedRect(box.expanded(-ui.s(5)), ui.s(3), th.accent);
            ui.r().fillCircle({box.right() + ui.s(18), row.center().y}, ui.s(7), kindColor(r.entry.meta));
            float tx = box.right() + ui.s(36);
            std::string time = r.entry.meta.timeMs > 0 ? formatGhostTime(static_cast<float>(r.entry.meta.timeMs) / 1000.0f) : "";
            float timeW = ui.text().measure(time, FontFace::Bold, ui.s(20)).x;
            float room = row.right() - ui.s(16) - timeW - ui.s(16) - tx;
            std::string title = r.entry.meta.sourceLabel();
            if (!r.entry.meta.label.empty() && r.entry.meta.label != "Moi") title += " · " + r.entry.meta.label;
            ui.label(ui.elide(title, FontFace::Bold, 18, room), {tx, row.y + ui.s(24)}, 18, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
            if (!time.empty()) ui.label(time, {row.right() - ui.s(16), row.y + ui.s(24)}, 20, th.text, Align::Right, FontFace::Bold, VAlign::Middle);
            std::string info;
            Color ic = th.textDim;
            switch (r.state) {
                case Row::State::Remote: info = "en ligne · téléchargé si choisi"; break;
                case Row::State::Downloading: info = "Téléchargement…"; ic = th.warm; break;
                case Row::State::Failed: info = r.error; ic = th.danger; break;
                default: break;
            }
            if (r.state == Row::State::Ready || r.state == Row::State::Remote) {
                std::string tags;
                if (!r.entry.meta.category.empty()) tags += "Any% · ";
                if (r.entry.meta.levelVersion > 0) tags += "v" + std::to_string(r.entry.meta.levelVersion) + " · ";
                tags += r.sameVersion ? "cette version" : "autre version : comparaison non officielle";
                tags += r.entry.meta.validated ? " · vérifié en ligne" : " · local";
                if (!r.entry.meta.date.empty()) tags += " · " + r.entry.meta.date.substr(0, 10);
                info = info.empty() ? tags : info + " · " + tags;
                if (!r.sameVersion) ic = th.warm;
            }
            float f = ui.fitFont(info, FontFace::Body, fz(ui, 15), row.right() - ui.s(16) - tx, 0.8f);
            ui.label(ui.elide(info, FontFace::Body, f, row.right() - ui.s(16) - tx), {tx, row.y + ui.s(50)}, f, ic, Align::Left, FontFace::Body,
                     VAlign::Middle);
        }
        ui.endScroll();
        // Under the list: import, and what is known about the online ghosts.
        float by = list.bottom() + ui.s(12);
        UIContext::ButtonStyle s;
        s.small = true;
        if (ui.button("import", {area.x, by, ui.s(260), ui.s(46)}, "Importer un replay…", s) && importDialog_ < 0)
            importDialog_ = FileDialogs::openFile(g_.app.window(), "Replay COSMIC PLATFORMER", "cprz", Paths::toUtf8(Paths::userDir()));
        std::string n = onlineBusy_ > 0 ? "Recherche des fantômes en ligne…" : onlineNote_;
        if (!n.empty())
            ui.label(ui.elide(n, FontFace::Body, fz(ui, 15), area.w - ui.s(280)), {area.x + ui.s(276), by + ui.s(23)}, 15, th.textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
    }

    /// The TAS's row of the list: its own look (lime, the machine's diamond: never a ghost's colour), and
    /// what it is said plainly.
    void drawTasRow(const Row& r, const Rect& row, const Rect& list) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        tasui::Pieces& P = tasui::pieces("setup");
        ui.r().fillGradientH({row.x + ui.s(2), row.y + ui.s(2), row.w * 0.5f, row.h - ui.s(4)}, tasui::kTasDeep.withAlpha(150), tasui::kTasDeep.withAlpha(0));
        ui.r().drawRoundedRect(row, ui.s(8), tasui::kTasLine, std::max(1.0f, ui.s(1.5f)));
        ui.r().fillRoundedRect({row.x, row.y + ui.s(8), ui.s(4), row.h - ui.s(16)}, ui.s(2), tasui::kTas);
        const Rect box{row.x + ui.s(14), row.center().y - ui.s(12), ui.s(24), ui.s(24)};
        ui.r().drawRoundedRect(box, ui.s(5), r.selected ? th.accent : th.textDim, ui.s(2));
        if (r.selected) ui.r().fillRoundedRect(box.expanded(-ui.s(5)), ui.s(3), th.accent);
        tasui::diamond(ui.r(), {box.right() + ui.s(18), row.center().y}, ui.s(17), std::max(1.5f, ui.s(2.5f)));
        const float tx = box.right() + ui.s(36);
        const std::string cat = tasui::categoryName(tasCat_);
        const std::string time = formatGhostTime(static_cast<float>(r.entry.meta.timeMs) / 1000.0f);
        const float timeW = ui.text().measure(time, FontFace::Bold, ui.s(20)).x;
        const float room = row.right() - ui.s(16) - timeW - ui.s(16) - tx;
        const std::string title = "TAS · " + cat + " · meilleur temps trouvé par la machine";
        const float tf = ui.fitFont(title, FontFace::Bold, 18, room, 0.75f);
        const std::string ts = ui.elide(title, FontFace::Bold, tf, room);
        ui.label(ts, {tx, row.y + ui.s(24)}, tf, tasui::kTas, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(time, {row.right() - ui.s(16), row.y + ui.s(24)}, 20, tasui::kTas, Align::Right, FontFace::Bold, VAlign::Middle);
        std::string info = cat;
        if (r.entry.meta.levelVersion > 0) info += " · v" + std::to_string(r.entry.meta.levelVersion);
        info += r.sameVersion ? " · cette version" : " · autre version";
        info += " · calculé hors ligne, relu à l'identique par le moteur · hors classement";
        const float iroom = row.right() - ui.s(16) - tx;
        const float f = ui.fitFont(info, FontFace::Body, fz(ui, 15), iroom, 0.8f);
        const std::string is = ui.elide(info, FontFace::Body, f, iroom);
        ui.label(is, {tx, row.y + ui.s(50)}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        if (row.y >= list.y && row.bottom() <= list.bottom()) {
            P.add("row/title", {tx, row.y + ui.s(24) - ui.s(tf) * 0.6f, ui.text().measure(ts, FontFace::Bold, ui.s(tf)).x, ui.s(tf) * 1.2f});
            P.add("row/time", {row.right() - ui.s(16) - timeW, row.y + ui.s(24) - ui.s(12), timeW, ui.s(24)});
            P.add("row/info", {tx, row.y + ui.s(50) - ui.s(f) * 0.6f, ui.text().measure(is, FontFace::Body, ui.s(f)).x, ui.s(f) * 1.2f});
        }
    }

    /// « TAS de ce niveau »: a row per category (its TAS, my record, the gap), what it is, and the ways
    /// to it. `compact`: the chosen category only (the training needs the room). Returns its height
    /// (`draw` false: only measures).
    float drawTasBlock(float x, float y0, float w, bool compact, bool draw) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        tasui::Pieces& P = tasui::pieces("setup");
        float y = y0;
        // The title and the machine's pill.
        const std::string title = "TAS de ce niveau", pillText = "MACHINE · HORS CLASSEMENT";
        const float pf = fz(ui, 12);
        const float pillW = tasui::pill(ui, pillText, {0.0f, 0.0f}, pf, false);
        const float titleF = ui.fitFont(title, FontFace::Bold, 20, std::max(ui.s(60), w - pillW - ui.s(12)), 0.7f);
        const float titleW = ui.text().measure(title, FontFace::Bold, ui.s(titleF)).x;
        const float titleH = ui.s(36);
        if (draw) {
            ui.label(title, {x, y + titleH * 0.4f}, titleF, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
            P.add("block/title", {x, y + titleH * 0.4f - ui.s(titleF) * 0.6f, titleW, ui.s(titleF) * 1.2f});
            if (titleW + ui.s(12) + pillW <= w) {
                tasui::pill(ui, pillText, {x + titleW + ui.s(12), y + titleH * 0.4f}, pf);
                P.add("block/pill", {x + titleW + ui.s(12), y + titleH * 0.4f - ui.s(pf), pillW, ui.s(pf) * 2.0f});
            }
        }
        y += titleH;
        // The columns, measured: category | TAS (« = ANY% ») | my record | gap; one font for all.
        float rf = fz(ui, 17);
        const float hf = fz(ui, 12);
        auto widths = [&](float f) {
            float cat = ui.text().measure("CATÉGORIE", FontFace::Bold, ui.s(hf)).x;
            for (RunCategory c : tasCats_) cat = std::max(cat, ui.text().measure(tasui::categoryName(c), FontFace::Bold, ui.s(f)).x);
            const float num = std::max(ui.text().measure("10:00,000", FontFace::Bold, ui.s(f)).x, ui.text().measure("MON RECORD", FontFace::Bold, ui.s(hf)).x);
            const float gap = std::max(ui.text().measure("+1:15,854", FontFace::Bold, ui.s(f)).x, ui.text().measure("ÉCART", FontFace::Bold, ui.s(hf)).x);
            const float same = ui.text().measure("= ANY%", FontFace::Body, ui.s(fz(ui, 14))).x;
            return std::array<float, 4>{cat, num, gap, same};
        };
        auto need = [&](const std::array<float, 4>& c) { return ui.s(16) + c[0] + ui.s(20) + c[1] + ui.s(12) + c[3] + ui.s(12) + c[1] + ui.s(20) + c[2] + ui.s(16); };
        auto cw = widths(rf);
        if (need(cw) > w) {
            rf = std::max(fz(ui, 11.5f), rf * w / need(cw));
            cw = widths(rf);
        }
        const bool sameCol = need(cw) <= w;  // « = ANY% » beside the time when it fits
        const float rowW = w;
        const float cGap = x + rowW - ui.s(16), cRec = cGap - cw[2] - ui.s(20), cTas = sameCol ? cRec - cw[1] - ui.s(12) - cw[3] - ui.s(12) : cRec - cw[1] - ui.s(20);
        const float cCat = x + ui.s(16);
        const float headH = ui.s(hf) * 1.3f + ui.s(6);
        if (!compact) {
            if (draw) {
                auto head = [&](const std::string& s, float hx, Align al, const std::string& id) {
                    const float hw = ui.text().measure(s, FontFace::Bold, ui.s(hf)).x;
                    ui.label(s, {hx, y}, hf, th.textDim, al, FontFace::Bold);
                    P.add("block/head/" + id, {al == Align::Right ? hx - hw : hx, y, hw, ui.s(hf) * 1.3f});
                };
                head("CATÉGORIE", cCat, Align::Left, "cat");
                head("TAS", cTas, Align::Right, "tas");
                head("MON RECORD", cRec, Align::Right, "rec");
                head("ÉCART", cGap, Align::Right, "gap");
            }
            y += headH;
        }
        const float rh = std::max(ui.s(38), ui.s(rf) * 1.6f), rg = ui.s(4);
        for (size_t i = 0; i < tasCats_.size(); ++i) {
            const RunCategory c = tasCats_[i];
            if (compact && c != tasCat_) continue;
            const tas::TasRun* t = tas::catalog().tasFor(g_, levelId_, c);
            if (!t) continue;
            const Rect row{x, y, rowW, rh};
            y += rh + rg;
            if (!draw) continue;
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = c == tasCat_;
            if (ui.button("tascat" + std::to_string(i), row, "", st)) setTasCategory(c);
            if (c == tasCat_) {
                ui.r().fillRoundedRect(row, ui.s(8), tasui::kTasDeep.withAlpha(140));
                ui.r().drawRoundedRect(row, ui.s(8), tasui::kTas, std::max(1.5f, ui.s(2.5f)));
            }
            const float cy = row.center().y;
            const std::string id = "block/row" + std::to_string(i) + "/";
            auto cell = [&](const std::string& s, float cx, Align al, FontFace f, float size, Color col, const std::string& name) {
                ui.label(s, {cx, cy}, size, col, al, f, VAlign::Middle);
                const float sw = ui.text().measure(s, f, ui.s(size)).x;
                P.add(id + name, {al == Align::Right ? cx - sw : cx, cy - ui.s(size) * 0.6f, sw, ui.s(size) * 1.2f});
            };
            cell(tasui::categoryName(c), cCat, Align::Left, FontFace::Bold, rf, th.text, "cat");
            cell(tas::timeText(t->timeMs), cTas, Align::Right, FontFace::Bold, rf, tasui::kTas, "tas");
            if (t->sameAsAny && sameCol) cell("= ANY%", cTas + ui.s(12), Align::Left, FontFace::Body, fz(ui, 14), th.textDim, "same");
            const float mine = tas::myRecord(g_, *t);
            cell(mine > 0.0f ? formatGhostSeconds(mine) : std::string("—"), cRec, Align::Right, FontFace::Body, rf, mine > 0.0f ? Color{200, 211, 226, 255} : th.textDim,
                 "rec");
            if (mine > 0.0f) {
                const int64_t d = static_cast<int64_t>(std::llround(static_cast<double>(mine) * 1000.0)) - t->timeMs;
                cell(tas::gapText(d), cGap, Align::Right, FontFace::Bold, rf, d > 0 ? tasui::kBehind : th.success, "gap");
            } else {
                cell("—", cGap, Align::Right, FontFace::Body, rf, th.textDim, "gap");
            }
        }
        // What it is, honestly (one line, when not compact).
        if (!compact) {
            const std::string note = "Le meilleur temps trouvé par notre recherche hors ligne, pas un optimum prouvé : il peut être battu.";
            const float nf = ui.fitFont(note, FontFace::Body, fz(ui, 14), w, 0.8f);
            if (draw) {
                const std::string ns = ui.elide(note, FontFace::Body, nf, w);
                ui.label(ns, {x, y + ui.s(2)}, nf, th.textDim, Align::Left, FontFace::Body);
                P.add("block/note", {x, y + ui.s(2), ui.text().measure(ns, FontFace::Body, ui.s(nf)).x, ui.s(nf) * 1.3f});
            }
            y += ui.s(nf) * 1.3f + ui.s(10);
        }
        // The ways to it: watch it, race it (the row « TAS » checked), its sheet.
        if (compact) return y - rg - y0;
        const float bh = ui.s(46);
        if (draw) {
            const bool racing = tasRow_ >= 0 && rows_[static_cast<size_t>(tasRow_)].selected;
            const std::string l0 = "▶ Regarder le TAS", l1 = "Course contre le fantôme TAS", l2 = "Fiche du TAS…";
            const float g = ui.s(12);
            const float w0 = ui.text().measure(l0, FontFace::Bold, ui.s(18)).x + ui.s(30), w1 = ui.text().measure(l1, FontFace::Bold, ui.s(18)).x + ui.s(30),
                        w2 = ui.text().measure(l2, FontFace::Bold, ui.s(18)).x + ui.s(30);
            const float k = (w - 2 * g) / (w0 + w1 + w2);
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = std::min({18.0f, ui.fitFont(l0, FontFace::Bold, 18, w0 * k - ui.s(20), 0.55f), ui.fitFont(l1, FontFace::Bold, 18, w1 * k - ui.s(20), 0.55f),
                                    ui.fitFont(l2, FontFace::Bold, 18, w2 * k - ui.s(20), 0.55f)});
            float bx = x;
            const Rect b0{bx, y, w0 * k, bh};
            bx += b0.w + g;
            const Rect b1{bx, y, w1 * k, bh};
            bx += b1.w + g;
            const Rect b2{bx, y, x + w - bx, bh};
            st.primary = true;
            const bool watch = ui.button("taswatch", b0, l0, st);
            ui.r().drawRoundedRect(b0, ui.s(8), tasui::kTas, std::max(1.0f, ui.s(1.5f)));
            st.primary = false;
            st.selected = racing;
            const bool race = ui.button("tasrace", b1, l1, st);
            if (racing) ui.r().drawRoundedRect(b1, ui.s(8), tasui::kTas, std::max(1.0f, ui.s(1.5f)));
            st.selected = false;
            const bool sheet = ui.button("tassheet", b2, l2, st);
            P.add("block/watch", b0);
            P.add("block/race", b1);
            P.add("block/sheet", b2);
            if (watch)
                if (const tas::TasRun* t = tasRun()) tasui::watch(g_, *t);
            if (race && tasRow_ >= 0) select(static_cast<size_t>(tasRow_), !racing);
            if (sheet) openTasSheet();
        }
        y += bh;
        return y - y0;
    }

    void drawSide(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        float y = area.y;
        ui.label("Affichage", {area.x, y}, 20, th.text, Align::Left, FontFace::Bold);
        y += ui.s(34);
        const GhostSettings& s = g_.settings.ghosts;
        static const char* kOpacity[] = {"discrète", "normale", "marquée"};
        std::string look = std::string("Opacité ") + kOpacity[std::clamp(s.opacity, 0, 2)] + (s.showNames ? " · noms affichés" : "") +
                           (s.showPanel ? " · panneau des segments" : "");
        // One line, the two buttons side by side: the training below needs the room.
        float lf = ui.fitFont(look, FontFace::Body, fz(ui, 15), area.w, 0.85f);
        ui.label(ui.elide(look, FontFace::Body, lf, area.w), {area.x, y + ui.s(10)}, lf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(30);
        UIContext::ButtonStyle st;
        st.small = true;
        const float half = (area.w - ui.s(12)) * 0.5f;
        if (ui.button("options", {area.x, y, half, ui.s(46)}, "Options d'affichage…", st)) g_.app.pushScene(makeGhostOptions(g_, nullptr));
        // Two of these replays side by side, B shifted in time (read only).
        if (ui.button("compare", {area.x + half + ui.s(12), y, half, ui.s(46)}, "Comparer deux replays…", st)) openCompare();
        y += ui.s(60);
        ui.separator({area.x, y}, area.w);
        y += ui.s(14);
        if (!tasCats_.empty()) {
            // « TAS de ce niveau »: whole when the training keeps its room, else the chosen category only
            // (its row, without the buttons: the list's « TAS » row races it), else nothing (a small window).
            const float trainingNeed = training_ ? ui.s(530) : ui.s(100);
            const float left = area.bottom() - y - ui.s(28);
            const float full = drawTasBlock(area.x, y, area.w, false, false);
            const float compactH = drawTasBlock(area.x, y, area.w, true, false);
            if (left - full >= trainingNeed || left - compactH >= trainingNeed) {
                y += drawTasBlock(area.x, y, area.w, left - full < trainingNeed, true) + ui.s(14);
                ui.separator({area.x, y}, area.w);
                y += ui.s(14);
            }
        }
        ui.label("Entraînement", {area.x, y}, 20, th.text, Align::Left, FontFace::Bold);
        y += ui.s(32);
        bool training = training_;
        // 2.91: the training screen (segments, enchaînements, startpos and measure points) beside it.
        const float chainsW = std::min(ui.s(330), area.w * 0.46f);
        const Rect toggleRect{area.x, y, area.w - chainsW - ui.s(12), ui.s(50)};
        if (ui.toggle("training", toggleRect, "Répéter un segment", training)) training_ = training;
        {
            UIContext::ButtonStyle cs;
            cs.small = true;
            cs.enabled = routeReady_ && error_.empty();
            const std::string label = "Segments et enchaînements…";
            cs.fontSize = ui.fitFont(label, FontFace::Bold, 16, chainsW - ui.s(20), 0.6f);
            const Rect cr{area.right() - chainsW, y + ui.s(2), chainsW, ui.s(46)};
            if (ui.button("chains", cr, label, cs) && cs.enabled) g_.app.pushScene(makeChainTraining(g_, chainRequest()));
        }
        // « NOUVEAU » (2.30.2): the preview, the references and the powers are under it.
        ui.newBadge("training", toggleRect, "ghosts.training");
        y += ui.s(60);
        if (!training_) return;
        // 2.60.3: an administrator makes the segment invincible right here (not ranked, as in the pause).
        if (g_.online.isAdmin()) {
            bool inv = g_.settings.gameplay.adminInvincible;
            const Rect invRect{area.x, y, area.w, ui.s(50)};
            if (ui.toggle("invincible", invRect, "Invincible (admin) · non classé", inv)) {
                g_.settings.gameplay.adminInvincible = inv;
                g_.saveSettings();
            }
            ui.newBadge("invincible", invRect, "ghosts.training.invincible");
            y += ui.s(60);
        }
        if (!routeReady_) {
            ui.label("Calcul du parcours…", {area.x, y}, fz(ui, 16), th.textDim);
            return;
        }
        const int n = segmentCount();
        startIdx_ = std::clamp(startIdx_, 0, n - 1);
        // The segment: its start → its end, the arrows when there are several.
        float bw = n > 1 ? ui.s(52) : 0.0f;
        if (n > 1) {
            if (ui.button("prev", {area.x, y, bw, ui.s(46)}, "<", st)) startIdx_ = (startIdx_ + n - 1) % n;
            if (ui.button("next", {area.right() - bw, y, bw, ui.s(46)}, ">", st)) startIdx_ = (startIdx_ + 1) % n;
        }
        // What this frame shows is up to date (the toggle or the arrows were just used).
        if (refsDirty_) computeRefs();
        if (powersSegment_ != startIdx_) loadPowers();
        {
            std::string segText = segmentName(startIdx_);
            std::string count = std::to_string(startIdx_ + 1) + " / " + std::to_string(n);
            const float cf = fz(ui, 15);
            const float cw = ui.text().measure(count, FontFace::Body, ui.s(cf)).x;
            const float room = area.w - 2 * bw - ui.s(24) - (n > 1 ? cw + ui.s(16) : 0.0f);
            float f = ui.fitFont(segText, FontFace::Bold, 18, room, 0.6f);
            std::string shown = ui.elide(segText, FontFace::Bold, f, room);
            float sw = ui.text().measure(shown, FontFace::Bold, ui.s(f)).x;
            float total = sw + (n > 1 ? ui.s(16) + cw : 0.0f);
            float sx = area.center().x - total * 0.5f;
            ui.label(shown, {sx, y + ui.s(23)}, f, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
            if (n > 1) ui.label(count, {sx + sw + ui.s(16), y + ui.s(23)}, cf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        y += ui.s(46) + ui.s(18);
        // Bottom up: the note, the references, the powers; the preview takes the room left.
        const float gap = ui.s(12);
        const float nf = fz(ui, 14);
        std::string note = trainingNote(n);
        float noteH = ui.text().measureWrapped(note, FontFace::Body, ui.s(nf), area.w).y;
        const float statsH = drawStats({area.x, 0.0f, area.w, 0.0f}, false);
        const float powersH = ui.s(44);
        const float pillRoom = ui.s(18);  // the « NOUVEAU » pill of « Choisir… » stays off the preview
        // 2.60.3: the Sum of Best and « Regarder » (two lines of text beside the button).
        const float watchH = std::max(ui.s(46), ui.s(fz(ui, 16)) * 1.35f + ui.s(fz(ui, 14)) * 1.35f + ui.s(4));
        float previewH = area.bottom() - y - (powersH + pillRoom) - (statsH + gap) - (pillRoom + watchH + gap) - (noteH + gap);
        if (previewH < ui.s(130)) {
            previewH += noteH + gap;  // too small a window: the note goes
            noteH = 0.0f;
        }
        previewH = std::max(previewH, ui.s(80));
        Rect pv{area.x, y, area.w, previewH};
        drawPreview(pv);
        float yy = pv.bottom() + pillRoom;
        drawPowersRow({area.x, yy, area.w, powersH});
        yy += powersH + gap;
        drawStats({area.x, yy, area.w, statsH}, true);
        yy += statsH + gap + pillRoom;  // the « NOUVEAU » pill of « Regarder » stays off the references
        drawWatchRow({area.x, yy, area.w, watchH});
        yy += watchH + gap;
        if (noteH > 0.0f) ui.labelWrapped(note, {area.x, yy}, area.w, nf, th.textDim);
    }

    // -------------------------------------------------------------- my verified runs (2.91)

    /// My own runs verified online (their replays as the server keeps them, this version only):
    /// each joins my ONE Sum of Best as « En ligne », once per record (never their passages twice).
    void importMyOnlineRuns() {
        std::vector<std::pair<Json, std::string>> list = std::move(myOnline_);
        myOnline_.clear();
        std::vector<std::pair<std::string, Vec2>> cps;
        for (const Checkpoint& c : checkpoints_) cps.push_back({c.id, c.feet});
        for (auto& [entry, path] : list) {
            const std::string rid = jstr(entry, "record_id");
            const int64_t timeMs = jget<int64_t>(entry, "time_ms", 0);
            if (rid.empty() || timeMs <= 0 || g_.bests.knowsOnline(levelKey_, rid)) continue;
            std::string date = jstr(entry, "achieved_at");
            if (date.size() >= 16) date = date.substr(0, 10) + " " + date.substr(11, 5);
            auto alive = std::weak_ptr<int>(life_);
            g_.online.getBinary(path, [this, alive, rid, timeMs, date, cps](const HttpResponse& resp) {
                if (!alive.lock() || resp.networkFailure() || resp.status != 200) return;
                std::vector<uint8_t> bytes(resp.body.begin(), resp.body.end());
                auto rep = ReplayData::decode(bytes);
                if (!rep) return;
                auto splits = worldRecordSplits(*rep, hash_);  // this exact version only
                if (!splits) return;
                GhostTrack track;
                track.load(*rep);
                std::vector<std::string> ids = checkpointsOfTrack(track, cps);
                BestAttempt a;
                a.passage = BestPassage::ofSplits(*splits, ids, static_cast<float>(timeMs) / 1000.0f);
                a.source = BestSource::Online;
                a.date = date;
                a.onlineId = rid;
                if (rep->exact()) a.replay = &bytes;
                g_.bests.add(levelKey_, a);
                g_.bests.save();
                refsDirty_ = true;
            }, GhostStore::kMaxReplayBytes);
        }
    }

    // -------------------------------------------------------------- section training

    /// 2.91: what « Segments et enchaînements » needs of the level (its route known).
    ChainTrainingRequest chainRequest() const {
        ChainTrainingRequest r;
        r.levelId = levelId_;
        r.levelName = level_.name.empty() ? levelId_ : level_.name;
        r.levelHash = hash_;
        r.levelBounds = level_.bounds;
        for (const Checkpoint& c : checkpoints_) r.checkpoints.push_back({c.id, c.label, c.feet, c.at});
        r.tablets = tablets_;
        r.startPowers = level_.startPowers;
        r.powerSlots = level_.powerSlots();
        for (size_t i : order_)
            if (i < rows_.size() && rows_[i].selected && rows_[i].state == Row::State::Ready) r.ghosts.push_back(rows_[i].entry);
        r.fromOnline = req_.fromOnline;
        return r;
    }

    int segmentCount() const { return static_cast<int>(checkpoints_.size()) + 1; }
    /// The ends of segment `k` (checkpoint ids; "" = the start of the level, its exit).
    std::string segFrom(int k) const { return k <= 0 || k > static_cast<int>(checkpoints_.size()) ? std::string{} : checkpoints_[static_cast<size_t>(k - 1)].id; }
    std::string segTo(int k) const { return k < 0 || k >= static_cast<int>(checkpoints_.size()) ? std::string{} : checkpoints_[static_cast<size_t>(k)].id; }
    /// A boundary's name: 0 the start, 1..n the checkpoints (their label), n + 1 the exit.
    std::string pointName(int k) const {
        if (k <= 0) return "Départ du niveau";
        if (k > static_cast<int>(checkpoints_.size())) return "Arrivée";
        const Checkpoint& c = checkpoints_[static_cast<size_t>(k - 1)];
        return c.label.empty() ? "Point " + std::to_string(k) : c.label;
    }
    std::string segmentName(int k) const { return pointName(k) + "  →  " + pointName(k + 1); }

    Vec2 segmentStart(int k) const {
        if (k > 0 && k <= static_cast<int>(checkpoints_.size())) return checkpoints_[static_cast<size_t>(k - 1)].feet;
        for (const LevelObject& o : level_.objects)
            if (o.type == "player_start") return o.rect.bottomCenter();
        if (routeTrack_.valid()) return routeTrack_.route.front();
        return {level_.bounds.x + 64.0f, level_.bounds.y + 64.0f};
    }
    Vec2 segmentEnd(int k) const {
        if (k >= 0 && k < static_cast<int>(checkpoints_.size())) return checkpoints_[static_cast<size_t>(k)].feet;
        // The exit the route ends at (the nearest one to its last point).
        const LevelObject* exit = nullptr;
        float bd = 1e30f;
        Vec2 to = routeTrack_.valid() ? routeTrack_.route.back() : Vec2{level_.bounds.right(), level_.bounds.bottom()};
        for (const LevelObject& o : level_.objects)
            if (o.type == "exit") {
                float d = (o.rect.bottomCenter() - to).lengthSq();
                if (d < bd) {
                    bd = d;
                    exit = &o;
                }
            }
        return exit ? exit->rect.bottomCenter() : to;
    }

    /// The tablets placed on the route before the start of segment `k`, in the route's order.
    std::vector<std::string> tabletsBefore(int k) const {
        std::vector<std::string> out;
        if (k <= 0 || k > static_cast<int>(checkpoints_.size())) return out;
        const Checkpoint& c = checkpoints_[static_cast<size_t>(k - 1)];
        for (const auto& [at, power] : tablets_)
            if (at <= c.at) out.push_back(power);
        return out;
    }
    int powerSlots() const {
        int sl = level_.powerSlots();
        return sl > 0 ? std::min(sl, LevelData::kMaxPowerSlots) : LevelData::kMaxPowerSlots;
    }
    /// The powers the route gives on segment `k`, as the run sets them up: the level's start
    /// powers, then the tablets before the start (the last ones when the slots are fewer).
    std::vector<std::string> defaultPowers(int k) const {
        Player probe;
        probe.setKeysPerSlot(true);
        for (const std::string& id : level_.startPowers)
            if (const PowerDefinition* d = g_.content.power(id)) probe.grantPower(*d);
        std::vector<std::string> tabs = tabletsBefore(k);
        int slots = level_.powerSlots();
        if (slots > 0 && static_cast<int>(tabs.size()) > slots) tabs.erase(tabs.begin(), tabs.end() - slots);
        for (const std::string& id : tabs)
            if (const PowerDefinition* d = g_.content.power(id); d && !probe.hasPower(id)) probe.grantPower(*d);
        std::vector<std::string> out;
        for (const auto& p : probe.powers()) out.push_back(p->id());
        return out;
    }
    /// The powers of the segment shown: the ones chosen last time (still in this level), else the route's.
    void loadPowers() {
        powersSegment_ = startIdx_;
        std::vector<std::string> def = defaultPowers(startIdx_);
        chosenPowers_ = def;
        powersCustom_ = false;
        if (const std::vector<std::string>* saved = g_.bests.powers(levelKey_, {segFrom(startIdx_), segTo(startIdx_)})) {
            std::vector<std::string> keep;
            for (const std::string& id : *saved)
                if (std::find(levelPowers_.begin(), levelPowers_.end(), id) != levelPowers_.end()) keep.push_back(id);
            keep = fitTrainingPowers(g_.content, keep, powerSlots());
            if (keep != def) {
                chosenPowers_ = keep;
                powersCustom_ = true;
            }
        }
    }
    void rememberPowers() {
        g_.bests.setPowers(levelKey_, {segFrom(startIdx_), segTo(startIdx_)}, powersCustom_ ? &chosenPowers_ : nullptr, FileUtil::nowString());
        g_.bests.save();
    }
    void openPowers() {
        PowerPick p;
        p.available = levelPowers_;
        p.defaults = defaultPowers(startIdx_);
        p.chosen = chosenPowers_;
        p.slots = powerSlots();
        p.segment = segmentName(startIdx_);
        p.keys = playerPowerKeys(g_, level_, p.available);
        auto alive = std::weak_ptr<int>(life_);
        const int k = startIdx_;
        g_.app.pushScene(makeTrainingPowers(g_, std::move(p), [this, alive, k](std::vector<std::string> chosen) {
            if (!alive.lock() || k != startIdx_) return;
            powersCustom_ = chosen != defaultPowers(k);
            chosenPowers_ = std::move(chosen);
            rememberPowers();
        }));
    }

    /// A ghost's track (read off the main thread); nullptr while it loads or when it cannot be read.
    const GhostTrack* track(const std::string& file) {
        TrackSlot& t = tracks_[file];
        if (t.track) return &*t.track;
        if (t.failed) return nullptr;
        if (!t.job.valid()) {
            fs::path f = Paths::fromUtf8(file);
            t.job = std::async(std::launch::async, [f]() -> std::optional<GhostTrack> {
                auto rep = GhostStore::loadReplay(f);
                if (!rep) return std::nullopt;
                GhostTrack g;
                g.load(*rep);
                return g;
            });
            return nullptr;
        }
        if (t.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            t.track = t.job.get();
            t.failed = !t.track;
        }
        return t.track ? &*t.track : nullptr;
    }
    bool trackFailed(const std::string& file) const {
        auto it = tracks_.find(file);
        return it != tracks_.end() && it->second.failed;
    }

    /// Whose path the preview shows: my record, else my last run, else the first ghost
    /// chosen (this version). File and caption; empty when there is none.
    std::pair<std::string, std::string> pathCandidate() const {
        if (pbEntry_ && !trackFailed(Paths::toUtf8(pbEntry_->file))) return {Paths::toUtf8(pbEntry_->file), "Trajet de mon record"};
        if (lastEntry_ && !trackFailed(Paths::toUtf8(lastEntry_->file))) return {Paths::toUtf8(lastEntry_->file), "Trajet de ma dernière course"};
        for (size_t i : order_) {
            if (i >= rows_.size()) continue;
            const Row& r = rows_[i];
            if (!r.selected || r.state != Row::State::Ready || !r.sameVersion || r.entry.file.empty()) continue;
            std::string f = Paths::toUtf8(r.entry.file);
            if (trackFailed(f)) continue;
            std::string who = r.entry.meta.sourceLabel();
            if (!r.entry.meta.label.empty() && r.entry.meta.label != "Moi") who += " · " + r.entry.meta.label;
            return {f, "Trajet : " + who};
        }
        return {};
    }

    void computeRefs() {
        refsDirty_ = false;
        std::vector<std::string> route;
        for (const Checkpoint& c : checkpoints_) route.push_back(c.id);
        // 2.91: the level's own route is the route of its ONE personal Sum of Best (every source:
        // runs, trainings, enchaînements, StartPos, verified online runs).
        if (auto known = g_.bests.route(levelKey_); !known || *known != route) {
            g_.bests.setRoute(levelKey_, route);
            g_.bests.save();
        }
        refs_ = Refs{};
        refs_.sob = g_.bests.sob(levelKey_);
        for (const PersonalBests::Segment& sg : g_.bests.segments(levelKey_))
            if (sg.best() > 0.0f) {
                refs_.best[{sg.from, sg.to}] = sg.best();
                refs_.anyRun = true;
            }
        // My record: the store's PB (its checkpoints), else my best ghost's (its replay's checkpoints).
        std::vector<float> splits;
        std::vector<std::string> ids;
        if (const PersonalBests::Run* pb = g_.bests.pb(levelKey_)) {
            refs_.pbTime = pb->time;
            splits = pb->splits;
            ids = pb->checkpoints;
        } else if (pbEntry_ && pbEntry_->meta.timeMs > 0) {
            refs_.pbTime = static_cast<float>(pbEntry_->meta.timeMs) / 1000.0f;
            splits = pbEntry_->meta.splits;
            auto it = tracks_.find(Paths::toUtf8(pbEntry_->file));
            if (splits.empty() && it != tracks_.end() && it->second.track) splits = it->second.track->splits();
            if (pbCheckpoints_.size() == splits.size()) ids = pbCheckpoints_;
        }
        if (refs_.pbTime > 0.0f) {
            refs_.anyRun = true;
            if (ids.size() != splits.size() && splits.size() == route.size()) ids = route;
            refs_.pbOnRoute = ids == route;
            if (ids.size() == splits.size())
                for (size_t i = 0; i <= splits.size(); ++i) {
                    const float a0 = i == 0 ? 0.0f : splits[i - 1], b0 = i < splits.size() ? splits[i] : refs_.pbTime;
                    refs_.pb[{i == 0 ? std::string{} : ids[i - 1], i < ids.size() ? ids[i] : std::string{}}] = b0 - a0;
                }
        }
        buildPlan();
    }

    // -------------------------------------------------------------- « Regarder » (2.60.3)

    /// The montage of the Sum of Best: which replay shows each segment (the same sum as refs_).
    void buildPlan() {
        // My runs of this version kept here (record, previous record, last run): an old best may be one of theirs.
        std::vector<MontageRun> kept;
        for (const Row& r : rows_) {
            const GhostMeta& m = r.entry.meta;
            if (!r.sameVersion || r.state != Row::State::Ready || (m.source != "pb" && m.source != "prev" && m.source != "last") || m.timeMs <= 0) continue;
            std::vector<std::string> ids = m.source == "pb" && pbCheckpoints_.size() == m.splits.size() ? pbCheckpoints_ : std::vector<std::string>{};
            kept.push_back({Paths::toUtf8(r.entry.file), m.date, m.splits, static_cast<float>(m.timeMs) / 1000.0f, ids});
        }
        plan_ = buildMontagePlan(g_.bests, levelKey_, kept);
    }

    /// A checkpoint's name by its id ("" = the level's start, or its exit).
    std::string boundaryName(const std::string& id, bool start) const {
        if (id.empty()) return start ? "Départ du niveau" : "Arrivée";
        for (size_t i = 0; i < checkpoints_.size(); ++i)
            if (checkpoints_[i].id == id) return checkpoints_[i].label.empty() ? "Point " + std::to_string(i + 1) : checkpoints_[i].label;
        return "Point de passage";
    }

    /// Under « Regarder »: what it will show, or why it cannot yet.
    std::string watchNote() const {
        const int n = static_cast<int>(plan_.segments.size());
        if (n == 0) return "Terminez le niveau, ou entraînez chacun de ses segments, pour voir vos meilleurs segments à la suite.";
        if (plan_.withReplay == 0) return "Aucun meilleur segment n'a encore de replay : battez-en un (course complète ou entraînement) pour le voir ici.";
        if (plan_.withReplay == n) return "Vos meilleurs segments à la suite, chacun rejoué à l'identique (course ou entraînement).";
        return std::to_string(plan_.withReplay) + " segments sur " + std::to_string(n) + " ont leur replay : le montage annonce les autres.";
    }

    void drawWatchRow(const Rect& row) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = plan_.withReplay > 0;
        const float bw = std::min(ui.s(190), row.w * 0.3f);
        const Rect br{row.right() - bw, row.y, bw, row.h};
        const bool watch = ui.button("watch", br, "Regarder", st);
        ui.newBadge("watch", br, "ghosts.training.montage");  // « NOUVEAU » (2.60.3)
        const float room = br.x - ui.s(14) - row.x;
        std::string line1 = "Somme des meilleurs : " + (refs_.sob > 0.0f ? formatGhostTime(refs_.sob) : std::string("incomplète"));
        if (refs_.pbTime > 0.0f) line1 += " (record : " + formatGhostTime(refs_.pbTime) + ")";
        const std::string line2 = watchNote();
        const float f1 = ui.fitFont(line1, FontFace::Bold, fz(ui, 16), room, 0.75f);
        const float f2 = ui.fitFont(line2, FontFace::Body, fz(ui, 14), room, 0.8f);
        ui.label(ui.elide(line1, FontFace::Bold, f1, room), {row.x, row.y + ui.s(f1) * 0.675f}, f1, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(ui.elide(line2, FontFace::Body, f2, room), {row.x, row.bottom() - ui.s(f2) * 0.675f}, f2, plan_.withReplay > 0 ? th.textDim : th.warm,
                 Align::Left, FontFace::Body, VAlign::Middle);
        if (watch && st.enabled) openMontage();
    }

    void openMontage() {
        // Golds of before 2.60.3 found in my kept runs (exactly their time): a copy of that run's
        // replay is kept with them from now on (the record or the last run may change later).
        std::map<std::string, std::vector<PersonalBests::Adoption>> byFile;
        std::map<std::string, std::string> dateOf;
        for (const MontageSegment& s : plan_.segments)
            if (s.adopt && s.hasReplay()) {
                byFile[s.file].push_back({{s.from, s.to}, s.start, s.end});
                dateOf[s.file] = s.date;
            }
        bool adopted = false;
        for (const auto& [file, list] : byFile) adopted = g_.bests.adoptReplay(levelKey_, Paths::fromUtf8(file), dateOf[file], list) || adopted;
        if (adopted) {
            g_.bests.save();
            computeRefs();
        }
        SobMontageRequest req;
        req.levelId = levelId_;
        req.levelHash = hash_;
        req.levelName = level_.name;
        req.plan = plan_;
        const size_t n = plan_.segments.size();
        for (const MontageSegment& s : plan_.segments)
            req.names.push_back(s.from.empty() && s.to.empty() && n > 1 ? "Segment " + std::to_string(s.index + 1)
                                                                         : boundaryName(s.from, true) + "  →  " + boundaryName(s.to, false));
        g_.app.pushScene(makeSobMontage(g_, std::move(req)));
    }

    /// Every frame once the route is known.
    void updateTraining() {
        if (!training_) return;  // nothing read nor drawn until the training is chosen
        // My record's checkpoints, read from its replay: which of its segments is which.
        if (pbEntry_ && !pbCheckpointsRead_) {
            std::string f = Paths::toUtf8(pbEntry_->file);
            if (const GhostTrack* t = track(f)) {
                std::vector<std::pair<std::string, Vec2>> cps;
                for (const Checkpoint& c : checkpoints_) cps.push_back({c.id, c.feet});
                pbCheckpoints_ = checkpointsOfTrack(*t, cps);
                pbCheckpointsRead_ = true;
                refsDirty_ = true;
            } else if (trackFailed(f)) {
                pbCheckpointsRead_ = true;
            }
        }
        if (refsDirty_) computeRefs();
        startIdx_ = std::clamp(startIdx_, 0, segmentCount() - 1);
        if (powersSegment_ != startIdx_) loadPowers();
        if (previewState_ == 1) loadPreview();
        if (previewState_ == 2 && previewTerrainLeft_ > 0) {
            // A few milliseconds per frame, the part shown first: the screen stays responsive.
            if (!previewMount_.empty()) g_.app.resources().pushMount(Paths::fromUtf8(previewMount_));
            previewTerrainLeft_ = previewRenderer_.bakeTerrain(g_.app.renderer(), 6.0, lastWindow_.w > 0.0f ? &lastWindow_ : nullptr);
            if (!previewMount_.empty()) g_.app.resources().popMount();
        }
        auto [file, label] = pathCandidate();
        bool pathReady = !file.empty() && track(file) != nullptr;
        if (seg_.segment != startIdx_ || seg_.pathKey != file || (seg_.pathLoading && (pathReady || trackFailed(file)))) rebuildSeg();
    }

    /// The level drawn small (main thread: its textures), a frame after its « Chargement » line.
    void loadPreview() {
        previewState_ = 3;
        const auto t0 = std::chrono::steady_clock::now();
        ResourceManager& res = g_.app.resources();
        if (const LevelEntry* e = g_.content.level(levelId_)) previewMount_ = Paths::toUtf8(e->assetRoot);
        if (!previewMount_.empty()) res.pushMount(Paths::fromUtf8(previewMount_));
        WorldServices ws{res, g_.app.audio(), g_.content, nullptr, 0.0f, 1.0f};
        previewWorld_ = std::make_unique<GameWorld>(ws, WorldMode::Editor);
        previewWorld_->load(level_);
        previewWorld_->player().setHidden(true);
        // The terrain is baked over the next frames (the part shown first).
        previewRenderer_.buildProgressive(res, *previewWorld_);
        if (!previewMount_.empty()) res.popMount();
        previewState_ = 2;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        Log::info("Entraînement : aperçu de {} chargé en {} ms", levelId_, ms);
    }

    /// The first split of `t` taken near `p` after `after` (-1: none).
    static float splitNear(const GhostTrack& t, Vec2 p, float after) {
        float best = -1.0f, bd = 320.0f * 320.0f;
        for (float s : t.splits()) {
            if (s < after) continue;
            float d = (t.poseAt(s).feet - p).lengthSq();
            if (d < bd) {
                bd = d;
                best = s;
            }
        }
        return best;
    }

    /// What the preview frames for the segment shown: its ends, the route between them, the path.
    void rebuildSeg() {
        const int k = startIdx_, n = segmentCount();
        seg_ = PreviewSeg{};
        seg_.segment = k;
        seg_.startFeet = segmentStart(k);
        seg_.endFeet = segmentEnd(k);
        float x0 = std::min(seg_.startFeet.x, seg_.endFeet.x), x1 = std::max(seg_.startFeet.x, seg_.endFeet.x);
        float y0 = std::min(seg_.startFeet.y, seg_.endFeet.y), y1 = std::max(seg_.startFeet.y, seg_.endFeet.y);
        auto grow = [&](Vec2 p) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        };
        if (routeTrack_.valid()) {
            float fa = k <= 0 ? 0.0f : checkpoints_[static_cast<size_t>(k - 1)].at;
            float fb = k >= n - 1 ? 1.0f : checkpoints_[static_cast<size_t>(k)].at;
            for (size_t i = 0; i < routeTrack_.route.size() && i < routeTrack_.cumulative.size(); ++i) {
                float f = routeTrack_.cumulative[i] / routeTrack_.length;
                if (f >= fa - 0.002f && f <= fb + 0.002f) grow(routeTrack_.route[i]);
            }
        }
        auto [file, label] = pathCandidate();
        seg_.pathKey = file;
        if (!file.empty()) {
            if (const GhostTrack* t = track(file)) {
                float a = k <= 0 ? 0.0f : splitNear(*t, seg_.startFeet, 0.0f);
                if (a < 0.0f) a = t->nearestTime(seg_.startFeet);
                float b = k >= n - 1 ? t->duration() : splitNear(*t, seg_.endFeet, a);
                if (b < 0.0f) b = t->nearestTime(seg_.endFeet);
                if (b > a) {
                    seg_.pathLabel = label;
                    seg_.pathTime = b - a;
                    std::vector<Vec2> line;
                    Vec2 prev;
                    for (float tt = a;; tt += 1.0f / 20.0f) {
                        float at = std::min(tt, b);
                        GhostTrack::Pose p = t->poseAt(at);
                        bool cut = !p.visible || (!line.empty() && (p.feet - prev).lengthSq() > 120.0f * 120.0f);
                        if (cut && line.size() > 1) seg_.path.push_back(std::move(line));
                        if (cut) line.clear();
                        if (p.visible) {
                            line.push_back(p.feet + Vec2{0.0f, -14.0f});  // the body, not the feet
                            grow(p.feet);
                        }
                        prev = p.feet;
                        if (at >= b) break;
                    }
                    if (line.size() > 1) seg_.path.push_back(std::move(line));
                }
            } else {
                seg_.pathLoading = !trackFailed(file);
            }
        }
        seg_.bbox = {x0, y0, std::max(1.0f, x1 - x0), std::max(1.0f, y1 - y0)};
    }

    /// The part of the level shown in `frame`: the segment with some room around (more above,
    /// for the names), a screen's worth at least, at the frame's proportions, inside the level.
    Rect previewWindow(const Rect& frame) const {
        const Rect& b = seg_.bbox;
        float mx = std::max(160.0f, b.w * 0.08f), my = std::max(150.0f, b.h * 0.12f);
        Rect win{b.x - mx, b.y - my * 1.4f, b.w + 2 * mx, b.h + my * 2.4f};
        if (win.w < 720.0f) {
            win.x -= (720.0f - win.w) * 0.5f;
            win.w = 720.0f;
        }
        if (win.h < 400.0f) {
            win.y -= (400.0f - win.h) * 0.5f;
            win.h = 400.0f;
        }
        const float aspect = frame.w / std::max(1.0f, frame.h);
        if (win.w / win.h < aspect) {
            float nw = win.h * aspect;
            win.x -= (nw - win.w) * 0.5f;
            win.w = nw;
        } else {
            float nh = win.w / aspect;
            win.y -= (nh - win.h) * 0.5f;
            win.h = nh;
        }
        // Inside the level (a level smaller than the window: all of it, centred in the frame).
        const Rect& lb = level_.bounds;
        if (lb.w > 0.0f && lb.h > 0.0f) {
            if (win.w >= lb.w) {
                win.x = lb.x;
                win.w = lb.w;
            } else {
                win.x = std::clamp(win.x, lb.x, lb.right() - win.w);
            }
            if (win.h >= lb.h) {
                win.y = lb.y;
                win.h = lb.h;
            } else {
                win.y = std::clamp(win.y, lb.y, lb.bottom() - win.h);
            }
        }
        return win;
    }

    void drawPreview(const Rect& box) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const UITheme& th = ui.theme();
        r.fillRoundedRect(box, ui.s(8), Color{5, 8, 18, 255});
        const float cf = fz(ui, 14);
        const float capH = ui.s(cf + 16);
        if (previewState_ == 0) previewState_ = 1;  // loaded by the next update, once this line is seen
        if (previewState_ < 2 || !previewWorld_) {
            ui.label(previewState_ == 3 ? "Aperçu indisponible" : "Chargement de l'aperçu…", box.center(), fz(ui, 16), th.textDim, Align::Center,
                     FontFace::Body, VAlign::Middle);
        } else {
            Rect frame{box.x + ui.s(2), box.y + ui.s(2), box.w - ui.s(4), box.h - ui.s(4) - capH};
            Rect win = previewWindow(frame);
            lastWindow_ = win;
            const float k = std::min(frame.w / win.w, frame.h / win.h);
            Rect dst{std::floor(frame.center().x - win.w * k * 0.5f), std::floor(frame.center().y - win.h * k * 0.5f), std::floor(win.w * k),
                     std::floor(win.h * k)};
            WorldView v;
            v.scale = k;
            v.screenW = static_cast<int>(dst.w);
            v.screenH = static_cast<int>(dst.h);
            v.topLeft = {win.x - dst.x / k, win.y - dst.y / k};
            v.origin = dst.pos();
            RenderOptions opt;
            opt.lighting = false;
            opt.mapView = true;
            ResourceManager& res = g_.app.resources();
            if (!previewMount_.empty()) res.pushMount(Paths::fromUtf8(previewMount_));
            r.pushClip(dst);
            previewRenderer_.render(r, res, *previewWorld_, v, 1.0f, opt);
            r.setView(WorldView{});
            r.popClip();
            if (!previewMount_.empty()) res.popMount();
            r.pushClip(frame);
            auto toMap = [&](Vec2 p) { return Vec2{dst.x + (p.x - win.x) * k, dst.y + (p.y - win.y) * k}; };
            // The path through the segment, outlined to read on any art.
            const Color ink{6, 8, 16, 230};
            const Color pathColor = ghostPaletteColor(g_.settings.ghosts.colorMine);
            const float lw = std::max(2.0f, ui.s(3.0f));
            for (const auto& line : seg_.path)
                for (size_t i = 1; i < line.size(); ++i) r.drawLine(toMap(line[i - 1]), toMap(line[i]), ink, lw + 3.0f);
            for (const auto& line : seg_.path)
                for (size_t i = 1; i < line.size(); ++i) r.drawLine(toMap(line[i - 1]), toMap(line[i]), pathColor, lw);
            // The two ends, then their names (inside the frame, never over each other).
            const float u = std::max(ui.s(7), 5.0f);
            const Color startColor = th.success, endColor{255, 206, 84, 255};
            Vec2 a = toMap(seg_.startFeet + Vec2{0.0f, -20.0f}), b = toMap(seg_.endFeet + Vec2{0.0f, -20.0f});
            r.fillCircle(a, u + 2.5f, ink);
            r.fillCircle(a, u, startColor);
            r.fillRect({b.x - u - 2.5f, b.y - u * 1.3f - 2.5f, u * 2 + 5.0f, u * 2.6f + 5.0f}, ink);
            r.fillRect({b.x - u, b.y - u * 1.3f, u * 2, u * 2.6f}, endColor);
            const float lf = fz(ui, 14);
            auto pill = [&](const std::string& text, Vec2 at, bool above) {
                std::string t = ui.elide(text, FontFace::Bold, lf, frame.w * 0.45f);
                Vec2 ts = ui.text().measure(t, FontFace::Bold, ui.s(lf));
                Rect p{at.x - ts.x * 0.5f - ui.s(8), 0.0f, ts.x + ui.s(16), ts.y + ui.s(6)};
                p.y = above ? at.y - u * 2.0f - ui.s(6) - p.h : at.y + u * 2.0f + ui.s(6);
                p.x = std::clamp(p.x, frame.x + ui.s(4), frame.right() - ui.s(4) - p.w);
                p.y = std::clamp(p.y, frame.y + ui.s(4), frame.bottom() - ui.s(4) - p.h);
                return std::make_pair(t, p);
            };
            auto [st, sp] = pill(pointName(seg_.segment), a, true);
            auto [et, ep] = pill(pointName(seg_.segment + 1), b, true);
            if (sp.intersects(ep)) ep = pill(pointName(seg_.segment + 1), b, false).second;
            if (sp.intersects(ep)) ep.y = sp.bottom() + ui.s(4);  // the same place: one under the other
            for (auto [t, p, c] : {std::make_tuple(st, sp, startColor), std::make_tuple(et, ep, endColor)}) {
                r.fillRoundedRect(p, p.h * 0.5f, Color{6, 10, 22, 225});
                r.drawRoundedRect(p, p.h * 0.5f, c, std::max(1.0f, ui.s(1.5f)));
                ui.label(t, p.center(), lf, c, Align::Center, FontFace::Bold, VAlign::Middle);
            }
            r.popClip();
            // Under the drawing: whose path it is.
            Rect cap{box.x + ui.s(1), box.bottom() - capH, box.w - ui.s(2), capH - ui.s(1)};
            r.fillRect(cap, Color{6, 10, 22, 235});
            std::string caption;
            if (!seg_.pathLabel.empty()) caption = seg_.pathLabel + " · " + formatGhostTime(seg_.pathTime);
            else if (seg_.pathLoading) caption = "Lecture du trajet…";
            else caption = "Aucune course de cette version : pas de trajet à montrer";
            float cx = cap.x + ui.s(12);
            if (!seg_.pathLabel.empty()) {
                r.drawLine({cx, cap.center().y}, {cx + ui.s(26), cap.center().y}, ink, lw + 3.0f);
                r.drawLine({cx, cap.center().y}, {cx + ui.s(26), cap.center().y}, pathColor, lw);
                cx += ui.s(36);
            }
            float room = cap.right() - ui.s(12) - cx;
            float f = ui.fitFont(caption, FontFace::Body, cf, room, 0.8f);
            ui.label(ui.elide(caption, FontFace::Body, f, room), {cx, cap.center().y}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        r.drawRoundedRect(box, ui.s(8), th.border, std::max(1.0f, ui.s(1.0f)));
        // « NOUVEAU » (2.30.2) on the preview: seen with the segment's arrows (focus) or a click.
        if (ui.newBadge("prev", box, "ghosts.training.section", false)) ui.newBadge("next", box, "ghosts.training.section");
    }

    void drawPowersRow(const Rect& row) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const float f = fz(ui, 16);
        Vec2 ls = ui.label("Pouvoirs", {row.x, row.center().y}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        float right = row.right();
        if (!levelPowers_.empty()) {
            UIContext::ButtonStyle st;
            st.small = true;
            float bw = std::min(ui.s(190), row.w * 0.3f);
            Rect br{row.right() - bw, row.y, bw, row.h};
            bool open = ui.button("powers", br, "Choisir…", st);
            ui.newBadge("powers", br, "ghosts.training.powers");  // « NOUVEAU » (2.30.2)
            if (open) openPowers();
            right = br.x - ui.s(14);
        }
        float x = row.x + ls.x + ui.s(14);
        const float tf = fz(ui, 14);
        std::string tag = levelPowers_.empty() ? "ce niveau n'en donne aucun" : powersCustom_ ? "choisis" : "par défaut";
        float tagW = ui.text().measure(tag, FontFace::Body, ui.s(tf)).x;
        float room = right - x - tagW - ui.s(12);
        const float ic = std::min(row.h - ui.s(8), ui.s(30));
        // Icons, names and their keys in this level (2.31); only the icons and keys when the
        // names do not fit.
        const PowerKeyMap keys = playerPowerKeys(g_, level_, chosenPowers_);
        const float kh = ic * 0.8f;
        auto keyW = [&](const PowerDefinition& d) { return Glyphs::drawPowerUse(ui, d, keys, {0.0f, 0.0f}, kh, 1.0f, false) + ui.s(6); };
        float full = 0.0f;
        for (const std::string& id : chosenPowers_)
            if (const PowerDefinition* d = g_.content.power(id))
                full += ic + ui.s(6) + ui.text().measure(d->name, FontFace::Body, ui.s(f)).x + keyW(*d) + ui.s(14);
        const bool names = full <= room;
        if (chosenPowers_.empty() && !levelPowers_.empty()) {
            Vec2 ns = ui.label("aucun", {x, row.center().y}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            x += ns.x + ui.s(12);
        }
        for (size_t i = 0; i < chosenPowers_.size(); ++i) {
            const PowerDefinition* d = g_.content.power(chosenPowers_[i]);
            if (!d) continue;
            float need = ic + (names ? ui.s(6) + ui.text().measure(d->name, FontFace::Body, ui.s(f)).x : 0.0f) + keyW(*d);
            if (x + need > row.x + ls.x + ui.s(14) + room) {
                // No room for the others: how many more.
                std::string more = "+" + std::to_string(chosenPowers_.size() - i);
                ui.label(more, {x, row.center().y}, f, th.textDim, Align::Left, FontFace::Bold, VAlign::Middle);
                x += ui.text().measure(more, FontFace::Bold, ui.s(f)).x + ui.s(12);
                break;
            }
            ui.icon("ui/icons_powers.png", d->icon, 32, {x, row.center().y - ic * 0.5f, ic, ic});
            x += ic;
            if (names) {
                Vec2 ns = ui.label(d->name, {x + ui.s(6), row.center().y}, f, th.text, Align::Left, FontFace::Body, VAlign::Middle);
                x += ui.s(6) + ns.x;
            }
            x += ui.s(6) + Glyphs::drawPowerUse(ui, *d, keys, {x + ui.s(6), row.center().y}, kh);
            x += ui.s(14);
        }
        ui.label(tag, {x, row.center().y}, tf, powersCustom_ ? th.warm : th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }

    /// The references of the segment and of the level (`draw` false: only its height).
    float drawStats(const Rect& area, bool draw) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const float f = fz(ui, 16), hf = fz(ui, 14);
        const float lineH = ui.s(f + 10.0f);
        const std::string from = segFrom(startIdx_), to = segTo(startIdx_);
        // 2.91: where the segment's best was done (Partie, Entraînement, Enchaînement, StartPos, En
        // ligne) and how often it was crossed, every source together.
        const PersonalBests::Record* tr = g_.bests.record(levelKey_, from, to);
        std::string trainValue = "pas encore";
        if (tr && tr->best > 0.0f) {
            trainValue = i18n::tr(bestSourceLabel(tr->source));
            if (tr->date.size() >= 10) trainValue += " · " + tr->date.substr(8, 2) + "/" + tr->date.substr(5, 2);
            trainValue += "  (" + std::to_string(tr->passages) + (tr->passages > 1 ? " passages)" : " passage)");
        }
        auto cell = [&](float x, float y, float w, const std::string& label, const std::string& value, bool known) {
            float vw = ui.text().measure(value, FontFace::Bold, ui.s(f)).x;
            float room = w - vw - ui.s(12);
            float lf = ui.fitFont(label, FontFace::Body, f, room, 0.8f);
            ui.label(ui.elide(label, FontFace::Body, lf, room), {x, y}, lf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(value, {x + w, y}, f, known ? th.text : th.textDim, Align::Right, FontFace::Bold, VAlign::Middle);
        };
        if (!refs_.anyRun) {
            // Nothing to compare with yet: said plainly, and the training time anyway.
            std::string none = "Aucun temps encore sur cette version du niveau : une course ou un entraînement donne vos meilleurs segments, "
                               "et la somme des meilleurs (SoB) dès que chacun est connu.";
            float h = ui.text().measureWrapped(none, FontFace::Body, ui.s(hf), area.w).y;
            if (draw) {
                ui.labelWrapped(none, {area.x, area.y}, area.w, hf, th.textDim);
                cell(area.x, area.y + h + ui.s(8) + lineH * 0.5f, area.w, "Meilleur fait en", trainValue, tr != nullptr);
            }
            return h + ui.s(8) + lineH;
        }
        // The TAS ANY% of this version (« meilleur temps trouvé par la machine »): one more line.
        const tas::TasRun* machine = tas::catalog().tasFor(g_, levelId_, RunCategory::AnyPercent);
        const float rows = machine ? 5.0f : 4.0f;
        if (!draw) return lineH * rows;
        const float colGap = ui.s(28);
        const float colW = (area.w - colGap) * 0.5f;
        const float x0 = area.x, x1 = area.x + colW + colGap;
        float y = area.y + lineH * 0.5f;
        ui.label("CE SEGMENT", {x0, y}, hf, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label("NIVEAU", {x1, y}, hf, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        const float best = refs_.bestOf(from, to), pb = refs_.pbOf(from, to);
        y += lineH;
        cell(x0, y, colW, "Meilleur segment", best >= 0.0f ? formatGhostTime(best) : "non comparable", best >= 0.0f);
        cell(x1, y, colW, "Somme des meilleurs (SoB)", refs_.sob > 0.0f ? formatGhostTime(refs_.sob) : "incomplète", refs_.sob > 0.0f);
        y += lineH;
        std::string pbValue = pb >= 0.0f ? formatGhostTime(pb) : refs_.pbTime > 0.0f ? "non comparable" : "aucun record";
        cell(x0, y, colW, "Dans mon record", pbValue, pb >= 0.0f);
        cell(x1, y, colW, "Mon record", refs_.pbTime > 0.0f ? formatGhostTime(refs_.pbTime) : "aucun", refs_.pbTime > 0.0f);
        y += lineH;
        cell(x0, y, colW, "Meilleur fait en", trainValue, tr != nullptr);
        float gain = refs_.possibleGain();
        std::string gainValue = gain >= 0.0f ? formatGhostDelta(gain).substr(formatGhostDelta(gain)[0] == '+' ? 1 : 0) : refs_.pbTime > 0.0f && refs_.sob > 0.0f ? "autre parcours" : "—";
        cell(x1, y, colW, "Gain possible", gainValue, gain >= 0.0f);
        if (machine) {
            y += lineH;
            const int64_t inTas = machine->segment(from, to);
            cell(x0, y, colW, "Dans le TAS", inTas > 0 ? formatGhostTime(static_cast<float>(inTas) / 1000.0f) : std::string("autre parcours"), inTas > 0);
            cell(x1, y, colW, "TAS (machine)", formatGhostTime(machine->seconds()), true);
        }
        return lineH * rows;
    }

    /// Under the references: what the training is, or why this segment has no reference.
    std::string trainingNote(int n) const {
        if (n <= 1) return "Ce niveau n'a pas de point de sauvegarde : le segment va du départ à l'arrivée.";
        const std::string from = segFrom(startIdx_), to = segTo(startIdx_);
        // 2.95: from the segment before (its run-up), so that this one is entered as in a run.
        const bool runUp = startIdx_ > 0;
        if (refs_.anyRun && refs_.bestOf(from, to) < 0.0f)
            return runUp ? "Départ au début du segment précédent (élan non compté), le temps part à l'entrée dans ce segment. Il n'a encore jamais été "
                           "parcouru en entier : son premier essai fera sa référence."
                         : "Ce segment n'a encore jamais été parcouru en entier : son premier essai fera sa référence.";
        return runUp ? "Départ au début du segment précédent (élan non compté), le temps part à l'entrée dans ce segment. Il recommence dès son "
                       "arrivée. Aucun record, aucune progression, rien n'est envoyé en ligne."
                     : "Le segment recommence dès son arrivée. Aucun record, aucune progression, rien n'est envoyé en ligne.";
    }

    /// « Comparer deux replays »: the ghosts chosen here are offered first (A, then B).
    void openCompare() {
        ReplayCompareRequest req;
        req.levelId = levelId_;
        for (size_t i : order_) {
            if (i >= rows_.size() || !rows_[i].selected || rows_[i].tas) continue;  // the TAS: « Comparer avec le TAS » of the viewer
            const GhostMeta& m = rows_[i].entry.meta;
            bool mine =m.source == "pb" || m.source == "prev" || m.source == "last";
            req.preselect.push_back(mine ? m.source : !rows_[i].recordId.empty() ? rows_[i].recordId : Paths::toUtf8(rows_[i].entry.file));
        }
        g_.app.pushScene(makeReplayCompareSetup(g_, std::move(req)));
    }

    // -------------------------------------------------------------- start

    void start() {
        PlayOptions o;
        o.ghostsChosen = true;
        o.fromOnline = req_.fromOnline;
        // The first one chosen is the reference of the gaps.
        for (size_t i : order_) {
            if (i >= rows_.size() || !rows_[i].selected) continue;
            if (rows_[i].state != Row::State::Ready) {
                say("Attendez la fin du téléchargement (ou décochez ce fantôme).", true);
                return;
            }
            o.ghosts.push_back(rows_[i].entry);
        }
        // The TAS, when chosen, is the reference of the gaps (« Course contre le fantôme TAS »).
        auto tasIt = std::find_if(o.ghosts.begin(), o.ghosts.end(), [](const GhostEntry& e) { return e.meta.source == "tas"; });
        if (tasIt != o.ghosts.end()) std::rotate(o.ghosts.begin(), tasIt, tasIt + 1);
        if (training_ && startIdx_ > 0 && startIdx_ <= static_cast<int>(checkpoints_.size())) {
            const Checkpoint& c = checkpoints_[static_cast<size_t>(startIdx_ - 1)];
            o.training = true;
            o.trainingCheckpoint = c.id;
            // 2.95: the attempt starts at the start of the segment before (its run-up, « élan »,
            // never counted): this one is entered as in a run, its time from that entry.
            if (startIdx_ >= 2) {
                const Checkpoint& r = checkpoints_[static_cast<size_t>(startIdx_ - 2)];
                o.trainingRunUp = r.id;
                o.spawn = r.feet;
            } else {
                o.trainingRunUp = std::string{};  // the first segment is the run-up: from the level's start
            }
            if (startIdx_ < static_cast<int>(checkpoints_.size())) o.trainingEnd = checkpoints_[static_cast<size_t>(startIdx_)].id;
            o.trainingPowers = tabletsBefore(startIdx_ - 1);
        } else if (training_) {
            o.training = true;  // from the start of the level, to the first checkpoint
            if (!checkpoints_.empty()) o.trainingEnd = checkpoints_.front().id;
        }
        if (o.training) {
            // What the setup showed (display only), and the powers chosen (remembered).
            o.trainingBest = refs_.bestOf(o.trainingCheckpoint, o.trainingEnd);
            o.trainingPbSegment = refs_.pbOf(o.trainingCheckpoint, o.trainingEnd);
            if (powersCustom_) {
                o.trainingPowers = chosenPowers_;
                o.trainingPowersChosen = true;
            }
            rememberPowers();
        }
        g_.app.switchScene(makeGameScene(g_, levelId_, std::move(o)), 0.4f);
    }

    GameServices& g_;
    std::string levelId_;
    GhostSetupRequest req_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    LevelData level_;
    std::string error_, hash_, communityId_;
    bool official_ = false, community_ = false;
    int version_ = 0;
    std::vector<Row> rows_;
    std::vector<size_t> order_;
    // The TAS (« meilleur temps trouvé par la machine »): its categories for this version, the one chosen,
    // its row in the list (-1: the level has none).
    std::vector<RunCategory> tasCats_;
    RunCategory tasCat_ = RunCategory::AnyPercent;
    int tasRow_ = -1;
    int onlineBusy_ = 0;
    std::string onlineNote_;
    int importDialog_ = -1;
    std::future<LevelProgressTrack> route_;
    bool routeReady_ = false;
    std::vector<Checkpoint> checkpoints_;
    std::vector<std::pair<float, std::string>> tablets_;
    // Section training (2.30.2).
    LevelProgressTrack routeTrack_;
    std::string levelKey_;
    std::optional<GhostEntry> pbEntry_, lastEntry_;  // my record and my last run of this version
    /// Ghost tracks read off the main thread (by replay file): the paths shown on the preview.
    struct TrackSlot {
        std::future<std::optional<GhostTrack>> job;
        std::optional<GhostTrack> track;
        bool failed = false;
    };
    std::map<std::string, TrackSlot> tracks_;
    std::vector<std::string> pbCheckpoints_;  // the checkpoints of my record's splits (its replay)
    bool pbCheckpointsRead_ = false;
    /// The references of the segments, from the ONE personal Sum of Best (2.91).
    struct Refs {
        std::map<std::pair<std::string, std::string>, float> best, pb;  // (from, to) → seconds
        float sob = -1.0f, pbTime = -1.0f;
        bool anyRun = false, pbOnRoute = false;
        float bestOf(const std::string& f, const std::string& t) const {
            auto it = best.find({f, t});
            return it == best.end() ? -1.0f : it->second;
        }
        float pbOf(const std::string& f, const std::string& t) const {
            auto it = pb.find({f, t});
            return it == pb.end() ? -1.0f : it->second;
        }
        float possibleGain() const { return pbOnRoute && sob > 0.0f && pbTime > 0.0f ? std::max(0.0f, pbTime - sob) : -1.0f; }
    };
    Refs refs_;
    std::vector<std::pair<Json, std::string>> myOnline_;  // 2.91: my verified runs to read (entry, replay path)
    MontagePlan plan_;  // 2.60.3: « Regarder »
    bool refsDirty_ = true;
    /// The level drawn small: loaded on the main thread (textures) once the training is shown,
    /// a frame after the « Chargement » line.
    std::unique_ptr<GameWorld> previewWorld_;
    GameRenderer previewRenderer_;
    int previewState_ = 0;  // 0 not asked, 1 asked (drawn next frame), 2 ready, 3 failed
    size_t previewTerrainLeft_ = 1;  // terrain chunks still to bake (a few per frame)
    Rect lastWindow_;                // the part of the level shown last (baked first)
    std::string previewMount_;
    /// What the preview frames for the segment shown (recomputed when the segment or the path changes).
    struct PreviewSeg {
        int segment = -1;
        std::string pathKey;
        bool pathLoading = false;
        Vec2 startFeet, endFeet;
        Rect bbox;
        std::vector<std::vector<Vec2>> path;  // polylines (cut at deaths, respawns)
        std::string pathLabel;
        float pathTime = -1.0f;  // the path's own time on the segment
    } seg_;
    std::vector<std::string> levelPowers_;
    std::vector<std::string> chosenPowers_;
    bool powersCustom_ = false;
    int powersSegment_ = -1;  // the segment `chosenPowers_` was read for
    bool training_ = false;
    int startIdx_ = 1;
    std::string msg_;
    bool msgError_ = false;
    float msgTimer_ = 0.0f;
    float t_ = 0.0f;
};

/// Section training → « Choisir… »: the powers carried on the segment, among the level's ones,
/// at most its power slots (a power on the same input as one already chosen replaces it, as
/// in a level). « Par défaut »: the route's ones again. Keyboard, gamepad and mouse.
class TrainingPowersScene : public Scene {
public:
    TrainingPowersScene(GameServices& g, PowerPick pick, std::function<void(std::vector<std::string>)> done)
        : g_(g), pick_(std::move(pick)), done_(std::move(done)) {}
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("tpow/ok"); }
    void update(float dt) override {
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 200});
        const int cols = pick_.available.size() > 4 ? 3 : 2;
        const int rows = static_cast<int>((pick_.available.size() + cols - 1) / cols);
        const float cellH = ui.s(58), cgap = ui.s(10);
        const float w = std::min(ui.s(900), ui.screenW() - ui.s(80));
        const float pad = ui.s(30);
        const float h = std::min(ui.screenH() - ui.s(60), pad + ui.s(46) + ui.s(34) + ui.s(40) + rows * (cellH + cgap) + ui.s(40) + ui.s(54) + pad);
        Rect panel{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
        ui.panel(panel, true, 18);
        ui.pushId("tpow");
        const float x = panel.x + pad, iw = panel.w - 2 * pad;
        float y = panel.y + pad;
        std::string title = "POUVOIRS DU SEGMENT";
        ui.label(title, {x, y + ui.s(18)}, ui.fitFont(title, FontFace::Title, 30, iw), th.text, Align::Left, FontFace::Title, VAlign::Middle);
        y += ui.s(46);
        ui.label(ui.elide(pick_.segment, FontFace::Bold, fz(ui, 17), iw), {x, y + ui.s(12)}, fz(ui, 17), th.accent, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        y += ui.s(34);
        // How many, and the rule of the inputs.
        std::string count = std::to_string(chosen().size()) + " / " + std::to_string(pick_.slots) + " emplacements";
        Vec2 cs = ui.label(count, {x, y + ui.s(14)}, fz(ui, 17), chosen().size() >= static_cast<size_t>(pick_.slots) ? th.warm : th.text, Align::Left,
                           FontFace::Bold, VAlign::Middle);
        std::string hint = "Un pouvoir utilisé de la même façon qu'un autre le remplace.";
        float hx = x + cs.x + ui.s(24), hroom = x + iw - hx;
        if (hroom > ui.s(80)) {
            float f = ui.fitFont(hint, FontFace::Body, fz(ui, 15), hroom, 0.75f);
            ui.label(ui.elide(hint, FontFace::Body, f, hroom), {x + iw, y + ui.s(14)}, f, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
        }
        y += ui.s(40);
        // The level's powers: the chosen ones numbered by slot.
        const float cw = (iw - cgap * (cols - 1)) / cols;
        for (size_t i = 0; i < pick_.available.size(); ++i) {
            const PowerDefinition* p = g_.content.power(pick_.available[i]);
            if (!p) continue;
            Rect c{x + (cw + cgap) * static_cast<float>(i % static_cast<size_t>(cols)), y + (cellH + cgap) * static_cast<float>(i / static_cast<size_t>(cols)), cw,
                   cellH};
            auto it = std::find(chosen_.begin(), chosen_.end(), p->id);
            bool on = it != chosen_.end();
            UIContext::ButtonStyle st;
            st.selected = on;
            st.small = true;
            if (ui.button("pw" + std::to_string(i), c, "", st)) toggle(p->id);
            Rect icon{c.x + ui.s(10), c.center().y - (cellH - ui.s(18)) * 0.5f, cellH - ui.s(18), cellH - ui.s(18)};
            ui.icon("ui/icons_powers.png", p->icon, 32, icon, on ? Color::white() : Color{150, 160, 180, 255});
            float tx = icon.right() + ui.s(10);
            // 2.31: the power's own key in this level (or how it is used: the dashed gold frame).
            const float kh = std::min(cellH - ui.s(24), ui.s(30));
            float badge = Glyphs::drawPowerUse(ui, *p, pick_.keys, {0.0f, 0.0f}, kh, 1.0f, false) + ui.s(10);
            float room = c.right() - ui.s(12) - badge - tx;
            float f = ui.fitFont(p->name, FontFace::Bold, fz(ui, 17), room, 0.7f);
            ui.label(ui.elide(p->name, FontFace::Bold, f, room), {tx, c.center().y}, f, on ? th.text : th.textDim, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            Glyphs::drawPowerUse(ui, *p, pick_.keys, {c.right() - ui.s(12) - (badge - ui.s(10)), c.center().y}, kh, on ? 1.0f : 0.55f);
        }
        y += rows * (cellH + cgap);
        // What just happened (a power replaced, the slots full), else what « Par défaut » gives.
        std::string line = msgTimer_ > 0.0f ? msg_ : chosen_ == pick_.defaults ? "Les pouvoirs du parcours : ceux du départ et des tablettes placées avant."
                                                                               : "Votre choix, retenu pour ce segment.";
        float lf = ui.fitFont(line, FontFace::Body, fz(ui, 15), iw, 0.8f);
        ui.label(ui.elide(line, FontFace::Body, lf, iw), {x, y + ui.s(16)}, lf, msgTimer_ > 0.0f ? th.warm : th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        // Buttons, from the right: Valider, Annuler, Par défaut.
        const float by = panel.bottom() - pad - ui.s(54);
        UIContext::ButtonStyle primary;
        primary.primary = true;
        const float bw = std::min(ui.s(220), (iw - ui.s(32)) / 3.0f);
        bool ok = ui.button("ok", {x + iw - bw, by, bw, ui.s(54)}, "Valider", primary);
        bool cancel = ui.button("cancel", {x + iw - 2 * bw - ui.s(16), by, bw, ui.s(54)}, "Annuler");
        UIContext::ButtonStyle def;
        def.enabled = chosen_ != pick_.defaults;
        if (ui.button("default", {x, by, bw, ui.s(54)}, "Par défaut", def) && def.enabled) chosen_ = pick_.defaults;
        ui.popId();
        if (ok) {
            if (done_) done_(chosen_);
            g_.uiSound("ui_confirm", 0.5f);
            g_.app.popScene();
        } else if (cancel || ui.backPressed()) {
            g_.app.popScene();
        }
        ui.end();
    }

private:
    const std::vector<std::string>& chosen() const { return chosen_; }

    void toggle(const std::string& id) {
        auto it = std::find(chosen_.begin(), chosen_.end(), id);
        if (it != chosen_.end()) {
            chosen_.erase(it);
            return;
        }
        if (static_cast<int>(chosen_.size()) >= pick_.slots) {
            g_.uiSound("ui_error");
            say("Tous les emplacements sont pris : retirez d'abord un pouvoir.");
            return;
        }
        std::vector<std::string> wanted = chosen_;
        wanted.push_back(id);
        std::vector<std::string> fitted = fitTrainingPowers(g_.content, wanted, pick_.slots);
        // A power on the same input as one already chosen replaces it (as in a level).
        const PowerDefinition* added = g_.content.power(id);
        for (const std::string& old : chosen_)
            if (std::find(fitted.begin(), fitted.end(), old) == fitted.end())
                if (const PowerDefinition* o = g_.content.power(old); o && added) say("« " + added->name + " » remplace « " + o->name + " » (même façon de l'utiliser).");
        chosen_ = fitted;
    }

    void say(const std::string& m) {
        msg_ = m;
        msgTimer_ = 4.0f;
    }

    GameServices& g_;
    PowerPick pick_;
    std::vector<std::string> chosen_ = pick_.chosen;
    std::function<void(std::vector<std::string>)> done_;
    std::string msg_;
    float msgTimer_ = 0.0f;
};

std::unique_ptr<Scene> makeTrainingPowers(GameServices& g, PowerPick pick, std::function<void(std::vector<std::string>)> done) {
    return std::make_unique<TrainingPowersScene>(g, std::move(pick), std::move(done));
}

}  // namespace

std::unique_ptr<Scene> makeGhostSetup(GameServices& g, const std::string& levelId, GhostSetupRequest req) {
    return std::make_unique<GhostSetupScene>(g, levelId, std::move(req));
}

}  // namespace cosmic
