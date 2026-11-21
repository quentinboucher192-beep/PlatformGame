// COSMIC ONLINE (2.19) - « Avis et commentaires » of an official level of the campaign (the
// players' rating and its comments, shown in ONLINE only) and the comments of a run of the
// leaderboards, and (2.85) of an attempt of the « course à la vérification » of an EXTRÊME level
// (solo: "race:<attempt>", Duo / Trio: "coop:<attempt>"). Pushed over the ranking screen; every
// rule is checked by the server, the page only shows what it answers.

#include <algorithm>
#include <cstdio>
#include <map>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "ui/online/CommentsPanel.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/online/RatingWidgets.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayDownloadUi.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

namespace {

class OnlineThreadScene : public Scene {
public:
    OnlineThreadScene(GameServices& g, std::string thread, std::string levelId)
        : g_(g), ui_(g), comments_(g, ui_), rating_(g), thread_(std::move(thread)), levelId_(std::move(levelId)) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        g_.ui.setFocus("thread/close");
        run_ = thread_.rfind("run:", 0) == 0;
        team_ = thread_.rfind("coop:", 0) == 0;
        race_ = team_ || thread_.rfind("race:", 0) == 0;
        id_ = thread_.substr(thread_.find(':') + 1);
        if (!run_ && !race_) levelId_ = id_;  // a run's level is confirmed by its record, an attempt's by the server
        comments_.setBackButton(false);
        if (g_.onlineOpen == "comments-replies") {  // automated screenshots
            g_.onlineOpen.clear();
            comments_.expandAll();
        }
        if (run_) {
            comments_.openThread(CommentsPanel::Kind::Run, thread_, "/api/v1/records/" + id_, "");
            loadRun();
        } else if (race_) {
            comments_.openThread(CommentsPanel::Kind::Run, thread_, (team_ ? "/api/v1/coop/runs/" : "/api/v1/races/runs/") + id_, "");
        } else {
            comments_.openThread(CommentsPanel::Kind::Level, thread_, "/api/v1/levels/" + id_, levelName());
            rating_.open(id_);
        }
    }

    void onResume() override {
        if (!run_ && !race_) rating_.refresh();
    }

    void update(float) override {}

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 205});
        float W = std::min(ui.screenW() - ui.s(60), ui.s(1500)), H = std::min(ui.screenH() - ui.s(60), ui.s(980));
        box_ = {(ui.screenW() - W) * 0.5f, (ui.screenH() - H) * 0.5f, W, H};
        ui.r().fillRoundedRect(box_, ui.s(18), Color{9, 14, 32, 248});
        ui.r().drawRoundedRect(box_, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        const bool modal = comments_.modal();
        ui.inputEnabled = !modal;
        ui.pushId("thread");
        UIContext::ButtonStyle cs;
        cs.small = true;
        Rect close{box_.right() - ui.s(170), box_.y + ui.s(20), ui.s(150), ui.s(42)};
        bool leave = ui.button("close", close, "Fermer", cs);
        std::string title = run_ || race_ ? "Commentaires de la course" : "Avis et commentaires";
        float tx = box_.x + ui.s(30);
        float tf = ui.fitFont(title, FontFace::Title, 28, close.x - ui.s(20) - tx, 0.7f);
        ui.label(title, {tx, close.center().y}, tf, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        Rect content{box_.x + ui.s(30), close.bottom() + ui.s(18), box_.w - ui.s(60), box_.bottom() - ui.s(24) - close.bottom() - ui.s(18)};
        float colW = std::clamp(content.w * 0.3f, ui.s(300), ui.s(430));
        Rect left{content.x, content.y, colW, content.h};
        Rect right{left.right() + ui.s(24), content.y, content.right() - left.right() - ui.s(24), content.h};
        if (race_) drawAttempt(left);
        else if (run_) drawRun(left);
        else drawLevel(left);
        ui.r().fillRoundedRect(right, ui.s(14), Color{8, 13, 30, 220});
        ui.r().drawRoundedRect(right, ui.s(14), ui.theme().border, ui.s(1.2f));
        comments_.draw(right);
        ui.popId();
        ui.inputEnabled = true;
        if (modal) comments_.drawModal();
        else if (leave || (ui.backPressed() && !ui.editingText())) g_.app.popScene();
        ui.end();
    }

private:
    std::string levelName() const {
        if (const LevelEntry* e = g_.content.level(levelId_)) return e->name;
        return levelId_;
    }

    void loadRun() {
        g_.online.get("/api/v1/records/" + id_, OnlineService::guard(life_, [this](const ApiResult& r) {
                          if (!r.ok) {
                              runError_ = r.message;
                              return;
                          }
                          runError_.clear();
                          run_data_ = r.data;
                          const Json& rec = r.data.value("record", Json::object());
                          if (std::string lid = jstr(rec, "level_id"); !lid.empty()) levelId_ = lid;
                      }));
    }

    TexturePtr thumbnail() {
        if (thumbTried_) return thumb_;
        thumbTried_ = true;
        if (const LevelEntry* e = g_.content.level(levelId_))
            if (auto data = g_.content.loadLevel(levelId_)) {
                auto png = renderLevelThumbnail(g_, *data, Paths::toUtf8(e->assetRoot));
                if (!png.empty()) thumb_ = textureFromPng(g_, png);
            }
        return thumb_;
    }

    /// Planet and level names, and the difficulty computed by the game (never confused
    /// with the players' rating). Returns the bottom.
    float drawLevelHeader(float x, float y, float w) {
        UIContext& ui = g_.ui;
        const LevelEntry* e = g_.content.level(levelId_);
        const PlanetData* planet = e ? g_.content.planet(e->planet) : nullptr;
        if (planet) {
            ui.label(planet->name, {x, y + ui.s(11)}, ui.fitFont(planet->name, FontFace::Bold, 17, w), planet->color, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            y += ui.s(26);
        }
        std::string name = levelName();
        float nf = ui.fitFont(name, FontFace::Title, 26, w, 0.7f);
        ui.label(ui.elide(name, FontFace::Title, nf, w), {x, y + ui.s(16)}, nf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        y += ui.s(38);
        if (e)
            if (const DifficultyRating* d = g_.ratings.get(*e)) {
                std::string t = "Difficulté calculée par le jeu : " + d->label();
                ui.label(t, {x, y + ui.s(10)}, ui.fitFont(t, FontFace::Body, 16, w), ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
                y += ui.s(28);
            }
        return y;
    }

    void drawLevel(const Rect& col) {
        UIContext& ui = g_.ui;
        float x = col.x, y = col.y, w = col.w;
        Rect th{x, y, w, w * 9.0f / 16.0f};
        if (TexturePtr t = thumbnail()) {
            ui.r().drawTexture(*t, nullptr, th);
        } else {
            ui.r().fillRect(th, Color{20, 30, 56, 255});
            ui.label("Niveau non installé", th.center(), 16, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        }
        ui.r().drawRect(th, ui.theme().border, ui.s(1));
        y = th.bottom() + ui.s(12);
        y = drawLevelHeader(x, y, w);
        ui.separator({x, y + ui.s(4)}, w);
        y += ui.s(14);
        rating_.drawFull({x, y, w, col.bottom() - y});
    }

    void drawRun(const Rect& col) {
        UIContext& ui = g_.ui;
        float x = col.x, y = col.y, w = col.w;
        y = drawLevelHeader(x, y, w);
        ui.separator({x, y + ui.s(4)}, w);
        y += ui.s(16);
        if (!run_data_.is_object()) {
            std::string t = runError_.empty() ? "Chargement de la course…" : "Course indisponible : " + runError_;
            ui.labelWrapped(t, {x, y}, w, 17, runError_.empty() ? ui.theme().textDim : ui.theme().warm);
            return;
        }
        const Json& rec = run_data_.value("record", Json::object());
        const Json& p = run_data_.value("player", Json::object());
        std::string pid = jstr(p, "id");
        float av = ui.s(64);
        ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), pid, {x, y, av, av});
        float nx = x + av + ui.s(14), nw = w - av - ui.s(14);
        std::string name = jstr(p, "username");
        UIContext::ButtonStyle ns;
        ns.small = true;
        ns.fontSize = 18;
        ns.enabled = !pid.empty();
        float bw = std::min(nw, ui.text().measure(name, FontFace::Bold, ui.s(18)).x + ui.s(28));
        if (ui.button("player", {nx, y, bw, ui.s(34)}, name, ns)) g_.app.pushScene(makePlayerProfile(g_, pid));
        std::string cc = jstr(p, "country_code");
        if (!cc.empty()) {
            ui_.drawFlag(cc, {nx, y + ui.s(42), ui.s(24), ui.s(18)});
            std::string country = ui_.countryName(cc);
            ui.label(country, {nx + ui.s(32), y + ui.s(51)}, ui.fitFont(country, FontFace::Body, 15, nw - ui.s(32)), ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        }
        y += av + ui.s(14);
        RunCategory cat = RunCategory::AnyPercent;
        categoryFromId(jstr(rec, "category"), cat);
        auto row = [&](const std::string& k, const std::string& v, Color c, float size) {
            ui.label(k, {x, y + ui.s(10)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(v, {x, y + ui.s(32)}, ui.fitFont(v, FontFace::Bold, size, w), c, Align::Left, FontFace::Bold, VAlign::Middle);
            y += ui.s(54);
        };
        row("Temps", formatRunTime(jget<int64_t>(rec, "time_ms", 0)), ui.theme().accent, 26);
        std::string date = jstr(rec, "verified_at", jstr(rec, "created_at"));
        std::string when = date.size() >= 10 ? date.substr(8, 2) + "/" + date.substr(5, 2) + "/" + date.substr(0, 4) : std::string("—");
        int rank = run_data_.contains("rank") && run_data_["rank"].is_number_integer() ? run_data_["rank"].get<int>() : 0;
        row("Catégorie", std::string(categoryLabel(cat)) + (rank > 0 ? " · rang mondial n°" + std::to_string(rank) : ""), ui.theme().text, 19);
        row("Réalisé le", when, ui.theme().text, 19);
        y += ui.s(6);
        UIContext::ButtonStyle st;
        st.small = true;
        // « Voir le replay » and, beside it (2.45), « Télécharger » to keep it on this computer.
        const float gap = ui.s(10), half = (w - gap) * 0.5f;
        st.fontSize = std::min(ui.fitFont("Voir le replay", FontFace::Bold, 17, half - ui.s(24), 0.7f),
                               ui.fitFont(ReplayDownloadUi::longestLabel(), FontFace::Bold, 17, half - ui.s(24), 0.7f));
        const bool hasReplay = jget<bool>(rec, "has_replay", false);
        st.enabled = hasReplay && g_.content.level(levelId_) != nullptr;
        if (ui.button("replay", {x, y, half, ui.s(40)}, "Voir le replay", st)) g_.app.pushScene(makeReplayViewer(g_, id_));
        {
            UIContext::ButtonStyle ds = st;
            ds.enabled = hasReplay;
            const LevelEntry* lvl = g_.content.level(levelId_);
            ReplayDownloadUi::button(g_, "download", {x + half + gap, y, half, ui.s(40)},
                                     ReplayDownloadUi::request("record", id_, "/api/v1/records/" + id_ + "/replay", name, name, levelId_,
                                                               lvl ? lvl->name : std::string(), categoryLabel(cat), jget<int64_t>(rec, "time_ms", 0), date),
                                     ds);
        }
        y += ui.s(48);
        if (!st.enabled) {
            std::string why = !jget<bool>(rec, "has_replay", false) ? "Pas de replay vérifié pour cette course." : "Niveau non installé : replay illisible ici.";
            ui.labelWrapped(why, {x, y}, w, 14, ui.theme().textDim);
        }
    }

    /// 2.85: the attempt of the race (as the server describes it with its comments): its player or its
    /// team's players (each one opens their profile), its board, its progression, its time and day, and
    /// its replay. Every line fitted to the column.
    void drawAttempt(const Rect& col) {
        UIContext& ui = g_.ui;
        float x = col.x, y = col.y, w = col.w;
        const Json& a = comments_.subject();
        if (a.is_object())
            if (std::string lid = jstr(a, "level_id"); !lid.empty()) levelId_ = lid;
        y = drawLevelHeader(x, y, w);
        ui.separator({x, y + ui.s(4)}, w);
        y += ui.s(16);
        if (!a.is_object()) {
            const std::string& err = comments_.loadError();
            std::string t = err.empty() ? "Chargement de la course…" : "Course indisponible : " + err;
            ui.labelWrapped(t, {x, y}, w, 17, err.empty() ? ui.theme().textDim : ui.theme().warm);
            return;
        }
        const int size = std::clamp(jget<int>(a, "size", 1), 1, 3);
        Json players = Json::array();
        if (size > 1) players = jfield(a, "players", Json::array());
        else players.push_back(jfield(a, "player", Json::object()));
        const std::string myId = g_.online.playerId();
        UIContext::ButtonStyle ns;
        ns.small = true;
        int k = 0;
        for (const Json& p : players) {
            const std::string pid = jstr(p, "id");
            std::string name = jstr(p, "username");
            i18n::Verbatim words;  // a pseudonym, as its player wrote it
            ns.enabled = !pid.empty();
            ns.fontSize = ui.fitFont(name, FontFace::Bold, 18, w - ui.s(28), 0.7f);
            name = ui.elide(name, FontFace::Bold, ns.fontSize, w - ui.s(28));
            const float bw = std::min(w, ui.text().measure(name, FontFace::Bold, ui.s(ns.fontSize)).x + ui.s(28));
            if (ui.button("player" + std::to_string(k++), {x, y, bw, ui.s(34)}, name, ns)) g_.app.pushScene(makePlayerProfile(g_, pid));
            if (!myId.empty() && pid == myId) ui.r().fillRect({x + ui.s(6), y + ui.s(31), bw - ui.s(12), ui.s(2)}, ui.theme().accent);
            y += ui.s(40);
        }
        y += ui.s(4);
        auto row = [&](const std::string& key, const std::string& v, Color c, float sz) {
            ui.label(key, {x, y + ui.s(10)}, ui.fitFont(key, FontFace::Body, 15, w), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(v, {x, y + ui.s(32)}, ui.fitFont(v, FontFace::Bold, sz, w), c, Align::Left, FontFace::Bold, VAlign::Middle);
            y += ui.s(54);
        };
        row("Classement", std::string("Course à la vérification · ") + (size == 3 ? "Trio" : size == 2 ? "Duo" : "Solo"), ui.theme().text, 19);
        char pct[32];
        std::snprintf(pct, sizeof pct, "%.1f", jget<double>(a, "score", 0.0));
        std::string score = pct;
        if (auto d = score.find('.'); d != std::string::npos) score[d] = ',';
        const bool finished = jget<bool>(a, "finished", false);
        row("Progression", score + " %" + (finished ? " · Fini" : ""), finished ? ui.theme().success : ui.theme().accent, 26);
        if (size > 1) {
            const int revives = jget<int>(a, "revives", 0);
            std::string t = std::to_string(revives) + (revives > 1 ? " réanimations" : " réanimation");
            if (jget<bool>(a, "incomplete", false)) t += " · équipe incomplète";
            row("Équipe", t, ui.theme().text, 19);
        }
        std::string date = jstr(a, "at");
        std::string when = date.size() >= 10 ? date.substr(8, 2) + "/" + date.substr(5, 2) + "/" + date.substr(0, 4) : std::string("—");
        if (const int64_t ms = jget<int64_t>(a, "time_ms", 0); ms > 0) row("Temps", formatRunTime(ms), ui.theme().text, 19);
        row("Réalisé le", when, ui.theme().text, 19);
        y += ui.s(6);
        // « Voir le replay »: downloaded then played, as the ▶ of its row.
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = ui.fitFont("Voir le replay", FontFace::Bold, 17, w - ui.s(24), 0.7f);
        st.enabled = jget<bool>(a, "has_replay", false) && !fetching_;
        if (ui.button("replay", {x, y, w, ui.s(40)}, "Voir le replay", st)) {
            fetching_ = true;
            watchRaceAttempt(g_, a, size, life_, [this](const std::string& m) {
                replayMsg_ = m;
                if (m != "Téléchargement du replay…") fetching_ = false;
            });
        }
        y += ui.s(48);
        std::string why = !jget<bool>(a, "has_replay", false) ? "Pas de replay vérifié pour cette course." : replayMsg_;
        if (!why.empty()) ui.labelWrapped(why, {x, y}, w, 14, ui.theme().textDim);
    }

    GameServices& g_;
    OnlineUi ui_;
    CommentsPanel comments_;
    LevelRatingPanel rating_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string thread_, levelId_, id_;
    bool run_ = false;
    bool race_ = false, team_ = false, fetching_ = false;  // 2.85: an attempt of a race (a team's), its replay downloading
    std::string replayMsg_;
    Json run_data_;
    std::string runError_;
    Rect box_;
    TexturePtr thumb_;
    bool thumbTried_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeOnlineThread(GameServices& g, const std::string& thread, const std::string& levelId) {
    return std::make_unique<OnlineThreadScene>(g, thread, levelId);
}

}  // namespace cosmic
