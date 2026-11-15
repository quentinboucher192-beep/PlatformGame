// « Comparer deux replays »: the choice of A and B before the comparison.
//
// The same sources as the pre-race screen of Ghost Racing: my runs of this level (best
// time, previous record, last run), the replays downloaded (cache) or imported, the online
// records (world record, friends, the first places; downloaded when chosen, kept in the
// cache), plus the replay being watched. Every file is read and checked again before the
// comparison opens. Read only: no record, no progression, nothing sent online.

#include <algorithm>
#include <memory>
#include <set>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
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

const Color kColorA{57, 240, 216, 255};
const Color kColorB{255, 158, 64, 255};

class ReplayCompareSetupScene : public Scene {
public:
    ReplayCompareSetupScene(GameServices& g, ReplayCompareRequest req) : g_(g), req_(std::move(req)), levelId_(req_.levelId) {}
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
        // The replay being watched first, then mine, then downloads and imported files.
        if (req_.current) {
            Row r;
            r.key = "current";
            r.memory = req_.current;
            r.entry.meta = req_.currentMeta;
            if (r.entry.meta.label.empty()) r.entry.meta.label = "Replay en cours de lecture";
            if (r.entry.meta.timeMs <= 0) r.entry.meta.timeMs = req_.current->durationMs;
            r.entry.meta.levelHash = req_.current->levelHash;
            r.sameVersion = req_.current->levelHash == hash_;
            rows_.push_back(std::move(r));
        }
        for (GhostEntry& e : g_.ghosts.localGhosts(levelId_)) addReady(std::move(e));
        for (GhostEntry& e : g_.ghosts.cachedFor(levelId_)) addReady(std::move(e));
        for (GhostEntry& e : g_.ghosts.files())
            if (e.meta.levelId == levelId_ || e.meta.levelHash == hash_) addReady(std::move(e));
        for (const Json& e : req_.onlineEntries) addRemote(e, "record", official_ || !community_);
        if (req_.preselect.empty()) {
            // By default: the replay watched, else my best time on this version.
            for (size_t i = 0; i < rows_.size(); ++i)
                if (rows_[i].key == "current" || (rows_[i].key == "pb" && rows_[i].sameVersion)) {
                    assign(static_cast<int>(i), 0);
                    break;
                }
        }
        applyPreselect();
        fetchOnline();
        g_.ui.setFocus("rcmp/go");
    }

    void update(float dt) override {
        if (importDialog_ >= 0)
            if (auto res = FileDialogs::result(importDialog_)) {
                importDialog_ = -1;
                if (!res->empty()) {
                    std::string err;
                    if (auto e = g_.ghosts.importFile(Paths::fromUtf8(*res), &err)) {
                        size_t before = rows_.size();
                        addReady(std::move(*e));
                        if (rows_.size() > before) {
                            Row& r = rows_.back();
                            if (!r.sameVersion && r.entry.meta.levelId != levelId_) {
                                say("Ce replay est celui d'un autre niveau (" + r.entry.meta.levelId + ").", true);
                                rows_.pop_back();
                            } else {
                                assign(static_cast<int>(rows_.size()) - 1, slotA_ < 0 ? 0 : 1);
                                say("Replay importé.", false);
                            }
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
        ui.pushId("rcmp");
        float x = panel.x + ui.s(30), y = panel.y + ui.s(28), w = panel.w - ui.s(60);
        float tf = ui.fitFont("COMPARER DEUX REPLAYS", FontFace::Title, 34, w, 0.6f);
        ui.label("COMPARER DEUX REPLAYS", {x, y + ui.s(20)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        std::string sub = level_.name.empty() ? levelId_ : level_.name;
        if (official_) sub += "  ·  version " + std::to_string(version_) + " (classée)";
        else if (community_) sub += "  ·  niveau de joueur, version " + std::to_string(version_);
        else sub += "  ·  niveau local";
        ui.label(ui.elide(sub, FontFace::Body, fz(ui, 18), w), {x, y + ui.s(60)}, fz(ui, 18), ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        float top = y + ui.s(92), bottom = panel.bottom() - ui.s(86);
        if (!error_.empty()) {
            ui.labelWrapped("Niveau introuvable : " + error_, {x, top}, w, fz(ui, 18), ui.theme().danger);
        } else {
            float listW = w * 0.6f;
            drawList({x, top, listW, bottom - top});
            drawSide({x + listW + ui.s(24), top, w - listW - ui.s(24), bottom - top});
        }
        // Bottom bar.
        float by = panel.bottom() - ui.s(70);
        UIContext::ButtonStyle primary;
        primary.primary = true;
        primary.enabled = error_.empty() && slotA_ >= 0 && slotB_ >= 0;
        bool go = ui.button("go", {panel.right() - ui.s(30) - ui.s(300), by, ui.s(300), ui.s(54)}, "Comparer", primary);
        bool cancel = ui.button("cancel", {panel.right() - ui.s(30) - ui.s(300) - ui.s(16) - ui.s(200), by, ui.s(200), ui.s(54)}, "Annuler");
        float room = panel.w - ui.s(60) - ui.s(540);
        if (msgTimer_ > 0.0f) {
            float f = ui.fitFont(msg_, FontFace::Body, fz(ui, 16), room, 0.8f);
            ui.label(ui.elide(msg_, FontFace::Body, f, room), {x, by + ui.s(27)}, f, msgError_ ? ui.theme().danger : ui.theme().success, Align::Left,
                     FontFace::Body, VAlign::Middle);
        } else {
            Glyphs::drawHint(ui, Action::MenuBack, "Annuler", {x, by + ui.s(27)}, ui.s(30));
        }
        ui.popId();
        if (go && primary.enabled) start();
        else if (cancel || ui.backPressed()) g_.app.popScene();
        ui.end();
    }

private:
    struct Row {
        std::string key;  // "current", "pb", "prev", "last", a record id, a file
        GhostEntry entry;
        std::shared_ptr<const ReplayData> memory;  // the replay being watched
        std::string recordId, downloadPath;
        enum class State { Ready, Remote, Downloading, Failed } state = State::Ready;
        bool sameVersion = true;
        std::string error;
    };

    void say(const std::string& m, bool error) {
        msg_ = m;
        msgError_ = error;
        msgTimer_ = 5.0f;
    }

    void addReady(GhostEntry e) {
        for (const Row& r : rows_)
            if (!e.file.empty() && r.entry.file == e.file) return;
        Row r;
        r.sameVersion = e.meta.levelHash == hash_;
        r.recordId = e.meta.recordId;
        bool mine = e.meta.source == "pb" || e.meta.source == "prev" || e.meta.source == "last";
        r.key = mine ? e.meta.source : !r.recordId.empty() ? r.recordId : Paths::toUtf8(e.file);
        r.entry = std::move(e);
        rows_.push_back(std::move(r));
    }

    int byKey(const std::string& k) const {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (!k.empty() && (rows_[i].key == k || rows_[i].recordId == k)) return static_cast<int>(i);
        return -1;
    }

    /// Puts row `i` in slot 0 (A) or 1 (B); a row is never both.
    void assign(int i, int slot) {
        if (i < 0 || i >= static_cast<int>(rows_.size())) return;
        int& mineSlot = slot == 0 ? slotA_ : slotB_;
        int& other = slot == 0 ? slotB_ : slotA_;
        if (other == i) other = mineSlot;  // chosen for the other one: they change places
        mineSlot = i;
        Row& r = rows_[static_cast<size_t>(i)];
        if (r.state == Row::State::Remote || r.state == Row::State::Failed) download(static_cast<size_t>(i));
    }

    void applyPreselect() {
        while (preselectDone_ < req_.preselect.size()) {
            int i = byKey(req_.preselect[preselectDone_]);
            if (i < 0) return;  // maybe later (online)
            int slot = preselectDone_ == 0 ? 0 : 1;
            if ((slot == 0 ? slotA_ : slotB_) < 0) assign(i, slot);
            ++preselectDone_;
        }
    }

    // -------------------------------------------------------------- online

    void addRemote(const Json& entry, const std::string& source, bool officialBoard) {
        std::string rid = jstr(entry, "record_id");
        if (rid.empty() || byKey(rid) >= 0) return;
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
        r.key = rid;
        r.recordId = rid;
        if (auto cached = g_.ghosts.cached(rid)) {
            r.entry = std::move(*cached);
            r.entry.meta.source = source;
            r.sameVersion = r.entry.meta.levelHash == hash_;
        } else {
            r.entry.meta = m;
            r.state = Row::State::Remote;
            r.downloadPath = officialBoard ? "/api/v1/records/" + rid + "/replay" : "/api/v1/community/records/" + rid + "/replay";
        }
        rows_.push_back(std::move(r));
    }

    void fetchOnline() {
        if (!g_.online.usable() || !g_.settings.online.enabled) {
            onlineNote_ = "Hors ligne : seuls les replays enregistrés sur cet ordinateur sont proposés.";
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
                              applyPreselect();
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
                                  applyPreselect();
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
                              applyPreselect();
                          });
        }
    }

    void note(const std::string& m) { onlineNote_ = "En ligne : " + m; }

    void download(size_t i) {
        Row& r = rows_[i];
        if (r.downloadPath.empty() || !g_.online.usable()) {
            r.state = Row::State::Failed;
            r.error = "Hors ligne : téléchargement impossible.";
            return;
        }
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
                // Kept even when made on another version: compared, never officially.
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
        }, GhostStore::kMaxReplayBytes);
    }

    // -------------------------------------------------------------- drawing

    std::string title(const Row& r) const {
        if (r.key == "current") return "En lecture · " + r.entry.meta.label;
        std::string t = r.entry.meta.sourceLabel();
        if (!r.entry.meta.label.empty() && r.entry.meta.label != "Moi") t += " · " + r.entry.meta.label;
        return t;
    }

    std::string tags(const Row& r) const {
        std::string info;
        switch (r.state) {
            case Row::State::Remote: info = "en ligne · téléchargé si choisi"; break;
            case Row::State::Downloading: return "Téléchargement…";
            case Row::State::Failed: return r.error;
            default: break;
        }
        std::string t;
        if (!r.entry.meta.category.empty()) t += "Any% · ";
        if (r.entry.meta.levelVersion > 0) t += "v" + std::to_string(r.entry.meta.levelVersion) + " · ";
        t += r.sameVersion ? "cette version" : "autre version : comparaison non officielle";
        t += r.entry.meta.validated ? " · vérifié en ligne" : " · local";
        if (!r.entry.meta.date.empty()) t += " · " + r.entry.meta.date.substr(0, 10);
        return info.empty() ? t : info + " · " + t;
    }

    void drawList(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.label("Replays de ce niveau", {area.x, area.y}, fz(ui, 20), th.text, Align::Left, FontFace::Bold);
        std::string count = std::to_string(rows_.size()) + (rows_.size() > 1 ? " replays" : " replay");
        ui.label(count, {area.right(), area.y + ui.s(2)}, fz(ui, 16), th.textDim, Align::Right);
        Rect list{area.x, area.y + ui.s(36), area.w, area.h - ui.s(36) - ui.s(64)};
        float rh = std::max(ui.s(70), ui.s(fz(ui, 18)) * 1.45f + ui.s(fz(ui, 15)) * 1.45f + ui.s(16)), gap = ui.s(8);
        ui.beginScroll("list", list, std::max(1.0f, static_cast<float>(rows_.size())) * (rh + gap));
        float y = list.y - ui.scrollOffset();
        if (rows_.empty()) {
            ui.labelWrapped(onlineBusy_ > 0 ? "Recherche des replays en ligne…"
                                            : "Aucun replay pour ce niveau : terminez-le (votre course est gardée), ou importez un replay.",
                            {list.x + ui.s(8), y + ui.s(8)}, list.w - ui.s(30), fz(ui, 17), th.textDim);
        }
        UIContext::ButtonStyle sb;
        sb.small = true;
        sb.fontSize = fz(ui, 18);
        const float bw = std::max(ui.s(54), ui.text().measure("B", FontFace::Bold, ui.s(fz(ui, 18))).x + ui.s(30));
        for (size_t i = 0; i < rows_.size(); ++i) {
            Row& r = rows_[i];
            Rect row{list.x, y, list.w - ui.s(14), rh};
            y += rh + gap;
            bool isA = slotA_ == static_cast<int>(i), isB = slotB_ == static_cast<int>(i);
            // A and B buttons on the right of the row (focusable: keyboard and gamepad).
            Rect bB{row.right() - ui.s(12) - bw, row.center().y - ui.s(20), bw, ui.s(40)};
            Rect bA{bB.x - ui.s(8) - bw, bB.y, bw, ui.s(40)};
            if (row.bottom() >= list.y && row.y <= list.bottom()) {
                ui.r().fillRoundedRect(row, ui.s(8), isA || isB ? Color{24, 36, 66, 235} : Color{14, 20, 40, 200});
                ui.r().drawRoundedRect(row, ui.s(8), isA ? kColorA.withAlpha(180) : isB ? kColorB.withAlpha(180) : ui.theme().border,
                                       std::max(1.0f, ui.s(1.5f)));
            }
            UIContext::ButtonStyle sa = sb, sbb = sb;
            sa.selected = isA;
            sbb.selected = isB;
            if (ui.button("a" + std::to_string(i), bA, "A", sa)) assign(static_cast<int>(i), 0);
            if (ui.button("b" + std::to_string(i), bB, "B", sbb)) assign(static_cast<int>(i), 1);
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            float tx = row.x + ui.s(16);
            if (isA || isB) {
                Rect badge{tx, row.y + ui.s(12), ui.s(24), ui.s(24)};
                ui.r().fillRoundedRect(badge, ui.s(5), isA ? kColorA : kColorB);
                ui.label(isA ? "A" : "B", badge.center(), fz(ui, 15), Color{6, 12, 24, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
                tx = badge.right() + ui.s(10);
            }
            float right = bA.x - ui.s(14);
            std::string time = r.entry.meta.timeMs > 0 ? formatGhostTime(static_cast<float>(r.entry.meta.timeMs) / 1000.0f) : "";
            float timeW = ui.text().measure(time, FontFace::Bold, ui.s(fz(ui, 20))).x;
            float room = right - timeW - ui.s(14) - tx;
            float ty = row.y + ui.s(10) + ui.s(fz(ui, 18)) * 0.72f;
            ui.label(ui.elide(title(r), FontFace::Bold, fz(ui, 18), room), {tx, ty}, fz(ui, 18), th.text, Align::Left, FontFace::Bold, VAlign::Middle);
            if (!time.empty()) ui.label(time, {right, ty}, fz(ui, 20), th.text, Align::Right, FontFace::Bold, VAlign::Middle);
            std::string info = tags(r);
            Color ic = r.state == Row::State::Failed ? th.danger : r.state == Row::State::Downloading ? th.warm : !r.sameVersion ? th.warm : th.textDim;
            float ix = row.x + ui.s(16);
            float f = ui.fitFont(info, FontFace::Body, fz(ui, 15), right - ix, 0.85f);
            ui.label(ui.elide(info, FontFace::Body, f, right - ix), {ix, row.bottom() - ui.s(10) - ui.s(f) * 0.72f}, f, ic, Align::Left, FontFace::Body,
                     VAlign::Middle);
        }
        ui.endScroll();
        // Under the list: import, and what is known about the online replays.
        float by = list.bottom() + ui.s(12);
        UIContext::ButtonStyle s;
        s.small = true;
        if (ui.button("import", {area.x, by, ui.s(260), ui.s(46)}, "Importer un replay…", s) && importDialog_ < 0)
            importDialog_ = FileDialogs::openFile(g_.app.window(), "Replay COSMIC PLATFORMER", "cprz", Paths::toUtf8(Paths::userDir()));
        std::string n = onlineBusy_ > 0 ? "Recherche des replays en ligne…" : onlineNote_;
        if (!n.empty())
            ui.label(ui.elide(n, FontFace::Body, fz(ui, 15), area.w - ui.s(280)), {area.x + ui.s(276), by + ui.s(23)}, fz(ui, 15), th.textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
    }

    void drawSide(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        float y = area.y;
        ui.label("Replays comparés", {area.x, y}, fz(ui, 20), th.text, Align::Left, FontFace::Bold);
        y += ui.s(fz(ui, 20)) * 1.6f;
        auto card = [&](const char* letter, int slot, Color c, const char* role) {
            // As high as its text needs (measured, wrapped).
            float roleH = ui.text().measureWrapped(role, FontFace::Body, ui.s(fz(ui, 14)), area.w - ui.s(24)).y;
            float h = ui.s(10) + ui.s(28) + ui.s(8) + roleH + ui.s(12);
            Rect box{area.x, y, area.w, h};
            ui.r().fillRoundedRect(box, ui.s(8), Color{14, 20, 40, 220});
            ui.r().drawRoundedRect(box, ui.s(8), c.withAlpha(slot >= 0 ? 200 : 80), std::max(1.0f, ui.s(1.5f)));
            Rect badge{box.x + ui.s(12), box.y + ui.s(10), ui.s(28), ui.s(28)};
            ui.r().fillRoundedRect(badge, ui.s(6), c);
            ui.label(letter, badge.center(), fz(ui, 17), Color{6, 12, 24, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
            float tx = badge.right() + ui.s(12), room = box.right() - ui.s(12) - tx;
            float ly = badge.center().y;
            if (slot < 0) {
                ui.label(ui.elide("À choisir dans la liste", FontFace::Bold, fz(ui, 17), room), {tx, ly}, fz(ui, 17), th.textDim, Align::Left,
                         FontFace::Bold, VAlign::Middle);
            } else {
                const Row& r = rows_[static_cast<size_t>(slot)];
                std::string t = title(r);
                if (r.entry.meta.timeMs > 0) t += " · " + formatGhostTime(static_cast<float>(r.entry.meta.timeMs) / 1000.0f);
                ui.label(ui.elide(t, FontFace::Bold, fz(ui, 17), room), {tx, ly}, fz(ui, 17), th.text, Align::Left, FontFace::Bold, VAlign::Middle);
            }
            float iy = badge.bottom() + ui.s(8);
            ui.labelWrapped(role, {box.x + ui.s(12), iy}, box.w - ui.s(24), fz(ui, 14), th.textDim);
            y = box.bottom() + ui.s(12);
        };
        card("A", slotA_, kColorA, "Rejoué (à l'identique s'il est de cette version du niveau), avec le HUD et la caméra du jeu.");
        card("B", slotB_, kColorB, "Montré en fantôme sur le chronomètre de A, décalable dans le temps.");
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = slotA_ >= 0 || slotB_ >= 0;
        if (ui.button("swap", {area.x, y, std::min(area.w, ui.s(300)), ui.s(46)}, "Échanger A et B", st)) std::swap(slotA_, slotB_);
        y += ui.s(60);
        std::string note;
        Color nc = th.textDim;
        if (slotA_ >= 0 && slotB_ >= 0) {
            const Row& a = rows_[static_cast<size_t>(slotA_)];
            const Row& b = rows_[static_cast<size_t>(slotB_)];
            if (a.entry.meta.levelHash != b.entry.meta.levelHash && !a.entry.meta.levelHash.empty() && !b.entry.meta.levelHash.empty()) {
                note = "Autre version : comparaison non officielle (les deux replays n'ont pas été faits sur la même version du niveau).";
                nc = th.warm;
            }
        }
        if (note.empty())
            note = "Lecture seule : aucun record, aucune progression, rien n'est envoyé. Les replays en ligne sont téléchargés avant la lecture.";
        ui.labelWrapped(note, {area.x, y}, area.w, fz(ui, 15), nc);
    }

    // -------------------------------------------------------------- start

    bool load(int slot, ReplayChoice& out, std::string& err) {
        Row& r = rows_[static_cast<size_t>(slot)];
        out.meta = r.entry.meta;
        if (r.memory) {
            out.replay = r.memory;
            return true;
        }
        if (r.state != Row::State::Ready) {
            err = r.state == Row::State::Failed ? r.error : "attendez la fin du téléchargement.";
            return false;
        }
        auto rep = GhostStore::loadReplay(r.entry.file, &err);
        if (!rep) return false;
        if (GhostCheck c = checkGhostReplay(*rep, "", level_.bounds); !c.ok) {
            err = c.reason;
            return false;
        }
        if (rep->levelHash != hash_ && rep->levelId != levelId_ && r.entry.meta.levelId != levelId_) {
            err = "replay d'un autre niveau.";
            return false;
        }
        if (out.meta.timeMs <= 0) out.meta.timeMs = rep->durationMs;
        if (out.meta.levelHash.empty()) out.meta.levelHash = rep->levelHash;
        out.replay = std::make_shared<const ReplayData>(std::move(*rep));
        return true;
    }

    void start() {
        ReplayChoice a, b;
        std::string err;
        if (!load(slotA_, a, err)) return say("A : " + err, true);
        if (!load(slotB_, b, err)) return say("B : " + err, true);
        if (req_.onChosen) {
            auto cb = req_.onChosen;
            g_.app.popScene();
            cb(std::move(a), std::move(b));
        } else {
            g_.app.replaceTop(makeReplayCompare(g_, levelId_, std::move(a), std::move(b)), 0.3f);
        }
    }

    GameServices& g_;
    ReplayCompareRequest req_;
    std::string levelId_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    LevelData level_;
    std::string error_, hash_, communityId_;
    bool official_ = false, community_ = false;
    int version_ = 0;
    std::vector<Row> rows_;
    int slotA_ = -1, slotB_ = -1;
    size_t preselectDone_ = 0;
    int onlineBusy_ = 0;
    std::string onlineNote_;
    int importDialog_ = -1;
    std::string msg_;
    bool msgError_ = false;
    float msgTimer_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeReplayCompareSetup(GameServices& g, ReplayCompareRequest req) {
    return std::make_unique<ReplayCompareSetupScene>(g, std::move(req));
}

}  // namespace cosmic
