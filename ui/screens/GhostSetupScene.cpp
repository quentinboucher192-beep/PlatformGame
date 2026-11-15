// Before a run (Ghost Racing): which ghosts to race against, and the training mode.
//
// Ghosts: mine (best time, previous record, last run of this exact level version), the
// replays downloaded or imported, and online the world record and friends' records (shown
// with their category and version; downloaded when chosen, kept in the local cache). Every
// replay is checked again when the run loads it. Training: start at a checkpoint, race the
// segment to the next one against the ghosts, again and again; never a record.

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/GhostRace.h"
#include "game/online/LevelFingerprint.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"
#include "ui/widgets/FileDialogs.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

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
        // Mine first (best, previous, last), then what was downloaded or imported.
        for (GhostEntry& e : g_.ghosts.localGhosts(levelId_)) addReady(std::move(e));
        for (GhostEntry& e : g_.ghosts.cachedFor(levelId_)) addReady(std::move(e));
        for (GhostEntry& e : g_.ghosts.files())
            if (e.meta.levelId == levelId_ || e.meta.levelHash == hash_) addReady(std::move(e));
        // By default: my best time on this version.
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].entry.meta.source == "pb" && rows_[i].sameVersion) {
                select(i, true);
                break;
            }
        if (!req_.recordId.empty()) preselectOnline();
        fetchOnline();
        g_.ui.setFocus("gsetup/launch");
    }

    void update(float dt) override {
        t_ += dt;
        if (route_.valid() && route_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            LevelProgressTrack t = route_.get();
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
            routeReady_ = true;
        }
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
            float listW = w * 0.58f;
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
            Glyphs::drawHint(ui, Action::MenuBack, "Annuler", {x, by + ui.s(27)}, ui.s(30));
        }
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
        Row r;
        r.recordId = rid;
        r.sameVersion = true;
        if (auto cached = g_.ghosts.cached(rid)) {
            r.entry = std::move(*cached);
            r.entry.meta.source = source;  // what it is to this player now (world / friend)
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
                                  if (jget<bool>(e, "me", false)) continue;
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
        y += ui.labelWrapped(look, {area.x, y}, area.w, fz(ui, 15), th.textDim).y + ui.s(10);
        UIContext::ButtonStyle st;
        st.small = true;
        if (ui.button("options", {area.x, y, std::min(area.w, ui.s(300)), ui.s(46)}, "Options d'affichage…", st)) g_.app.pushScene(makeGhostOptions(g_, nullptr));
        y += ui.s(56);
        // Two of these replays side by side, B shifted in time (read only).
        if (ui.button("compare", {area.x, y, std::min(area.w, ui.s(300)), ui.s(46)}, "Comparer deux replays…", st)) openCompare();
        y += ui.s(66);
        ui.separator({area.x, y}, area.w);
        y += ui.s(16);
        ui.label("Entraînement", {area.x, y}, 20, th.text, Align::Left, FontFace::Bold);
        y += ui.s(34);
        bool training = training_;
        if (ui.toggle("training", {area.x, y, area.w, ui.s(50)}, "Répéter un segment", training)) training_ = training;
        y += ui.s(62);
        if (training_) {
            if (!routeReady_) {
                ui.label("Calcul du parcours…", {area.x, y}, fz(ui, 16), th.textDim);
                return;
            }
            if (checkpoints_.empty()) {
                ui.labelWrapped("Ce niveau n'a pas de point de sauvegarde : l'entraînement part du début.", {area.x, y}, area.w, fz(ui, 16), th.textDim);
                return;
            }
            int n = static_cast<int>(checkpoints_.size()) + 1;  // 0 = the start of the level
            startIdx_ = std::clamp(startIdx_, 0, n - 1);
            auto name = [&](int k) -> std::string {
                if (k <= 0) return "Départ du niveau";
                if (k > static_cast<int>(checkpoints_.size())) return "Arrivée";
                const Checkpoint& c = checkpoints_[static_cast<size_t>(k - 1)];
                return c.label.empty() ? "Point " + std::to_string(k) : c.label;
            };
            float bw = ui.s(52);
            if (ui.button("prev", {area.x, y, bw, ui.s(46)}, "<", st)) startIdx_ = (startIdx_ + n - 1) % n;
            if (ui.button("next", {area.right() - bw, y, bw, ui.s(46)}, ">", st)) startIdx_ = (startIdx_ + 1) % n;
            std::string seg = name(startIdx_) + "  →  " + name(startIdx_ + 1);
            float f = ui.fitFont(seg, FontFace::Bold, 18, area.w - 2 * bw - ui.s(24), 0.6f);
            ui.label(ui.elide(seg, FontFace::Bold, f, area.w - 2 * bw - ui.s(24)), {area.center().x, y + ui.s(23)}, f, th.accent, Align::Center,
                     FontFace::Bold, VAlign::Middle);
            y += ui.s(62);
            y += ui.labelWrapped("Le segment recommence dès son arrivée. Les pouvoirs des tablettes placées avant le départ sont donnés. "
                                 "Aucun record, aucune progression, rien n'est envoyé en ligne.",
                                 {area.x, y}, area.w, fz(ui, 15), th.textDim).y;
        }
    }

    /// « Comparer deux replays »: the ghosts chosen here are offered first (A, then B).
    void openCompare() {
        ReplayCompareRequest req;
        req.levelId = levelId_;
        for (size_t i : order_) {
            if (i >= rows_.size() || !rows_[i].selected) continue;
            const GhostMeta& m = rows_[i].entry.meta;
            bool mine = m.source == "pb" || m.source == "prev" || m.source == "last";
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
        if (training_ && startIdx_ > 0 && startIdx_ <= static_cast<int>(checkpoints_.size())) {
            const Checkpoint& c = checkpoints_[static_cast<size_t>(startIdx_ - 1)];
            o.training = true;
            o.trainingCheckpoint = c.id;
            o.spawn = c.feet;
            if (startIdx_ < static_cast<int>(checkpoints_.size())) o.trainingEnd = checkpoints_[static_cast<size_t>(startIdx_)].id;
            for (const auto& [at, power] : tablets_)
                if (at <= c.at) o.trainingPowers.push_back(power);
        } else if (training_) {
            o.training = true;  // from the start of the level, to the first checkpoint
            if (!checkpoints_.empty()) o.trainingEnd = checkpoints_.front().id;
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
    int onlineBusy_ = 0;
    std::string onlineNote_;
    int importDialog_ = -1;
    std::future<LevelProgressTrack> route_;
    bool routeReady_ = false;
    std::vector<Checkpoint> checkpoints_;
    std::vector<std::pair<float, std::string>> tablets_;
    bool training_ = false;
    int startIdx_ = 1;
    std::string msg_;
    bool msgError_ = false;
    float msgTimer_ = 0.0f;
    float t_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeGhostSetup(GameServices& g, const std::string& levelId, GhostSetupRequest req) {
    return std::make_unique<GhostSetupScene>(g, levelId, std::move(req));
}

}  // namespace cosmic
