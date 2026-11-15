#include "ui/online/RatingWidgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "game/GameServices.h"
#include "game/online/LevelRatings.h"
#include "ui/online/OnlineWidgets.h"

namespace cosmic {

namespace {
const Color kStarColor{255, 206, 84, 255};
const Color kStarOff{110, 124, 150, 255};
}  // namespace

void drawRatingStar(Renderer& r, Vec2 c, float radius, Color col, bool filled) {
    Vec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -kPi / 2.0f + static_cast<float>(i) * kPi / 5.0f;
        float rr = i % 2 == 0 ? radius : radius * 0.45f;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) {
        if (filled) r.fillTriangle(c, pts[i], pts[(i + 1) % 10], col);
        else r.drawLine(pts[i], pts[(i + 1) % 10], col, std::max(1.0f, radius * 0.16f));
    }
}

float measureRatingSummary(UIContext& ui, double average, int count, float size) {
    if (count <= 0) return ui.text().measure("aucune note pour l'instant", FontFace::Body, ui.s(size)).x;
    float px = ui.s(size);
    return ui.text().measure(LevelRatingCache::averageText(average), FontFace::Bold, px).x + px * 1.25f +
           ui.text().measure("· " + LevelRatingCache::countText(count), FontFace::Body, px).x;
}

float drawRatingSummary(UIContext& ui, Vec2 leftMid, double average, int count, float size, Color text, float maxWidth) {
    float natural = measureRatingSummary(ui, average, count, size);
    if (natural > maxWidth && natural > 0.0f) size = std::max(size * 0.6f, size * maxWidth / natural);
    if (count <= 0) {
        ui.label("aucune note pour l'instant", leftMid, size, text, Align::Left, FontFace::Body, VAlign::Middle);
        return measureRatingSummary(ui, average, count, size);
    }
    float px = ui.s(size);
    Vec2 a = ui.label(LevelRatingCache::averageText(average), leftMid, size, text, Align::Left, FontFace::Bold, VAlign::Middle);
    float x = leftMid.x + a.x + px * 0.18f;
    drawRatingStar(ui.r(), {x + px * 0.42f, leftMid.y}, px * 0.46f, kStarColor, true);
    x += px * 1.07f;
    Vec2 b = ui.label("· " + LevelRatingCache::countText(count), {x, leftMid.y}, size, text, Align::Left, FontFace::Body, VAlign::Middle);
    return x + b.x - leftMid.x;
}

float drawCampaignRating(GameServices& g, const std::string& levelId, Vec2 topLeft, float width, float size) {
    UIContext& ui = g.ui;
    const LevelRatingCache& cache = g.online.levelRatings();
    auto info = cache.get(levelId, g.online.playerId());
    const std::string head = "Note des joueurs : ";
    float px = ui.s(size);
    float lineH = px * 1.45f;
    Vec2 mid{topLeft.x, topLeft.y + lineH * 0.5f};
    if (!info) {
        // Nothing ever received: no invented value, only where to find it.
        std::string t = head + "en ligne (Cosmic Online)";
        ui.label(t, mid, ui.fitFont(t, FontFace::Body, size, width), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        return lineH;
    }
    float hw = ui.text().measure(head, FontFace::Body, px).x;
    float room = width - hw;
    float hs = size;
    if (room < measureRatingSummary(ui, info->average, info->count, size) * 0.6f) {
        hs = ui.fitFont(head, FontFace::Body, size, width * 0.45f);
        hw = ui.text().measure(head, FontFace::Body, ui.s(hs)).x;
        room = width - hw;
    }
    ui.label(head, mid, hs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    drawRatingSummary(ui, {topLeft.x + hw, mid.y}, info->average, info->count, size, ui.theme().text, room);
    float h = lineH;
    // A value received earlier (offline, not read yet in this session) says so.
    std::string note;
    if (!cache.fresh() && cache.receivedAt() > 0) {
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        note = "Dernière moyenne reçue " + OnlineUi::ageText(static_cast<double>(std::max<int64_t>(0, now - cache.receivedAt())));
        if (!g.online.loggedIn()) note += " (non connecté)";
        else if (g.online.state() == OnlineState::Offline) note += " (hors ligne)";
    }
    if (info->myStars > 0) note += (note.empty() ? "Votre note : " : " · votre note : ") + std::to_string(info->myStars) + "/5";
    if (!note.empty()) {
        float ns = size * 0.8f;
        ui.label(note, {topLeft.x, topLeft.y + h + ui.s(ns) * 0.7f}, ui.fitFont(note, FontFace::Body, ns, width), ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        h += ui.s(ns) * 1.45f;
    }
    return h;
}

// ------------------------------------------------------------ rating panel

LevelRatingPanel::LevelRatingPanel(GameServices& g) : g_(g) {}

void LevelRatingPanel::open(const std::string& levelId) {
    if (levelId != levelId_) {
        view_ = Json();
        error_.clear();
        errorCode_.clear();
        fromCache_ = false;
    }
    levelId_ = levelId;
    refresh();
}

void LevelRatingPanel::say(const std::string& m, bool error) {
    status_ = m;
    statusError_ = error;
    statusT_ = g_.ui.time();
}

int LevelRatingPanel::comments() const { return view_.is_object() ? jget<int>(view_, "comments", 0) : 0; }

void LevelRatingPanel::refresh() {
    if (levelId_.empty()) return;
    busy_ = true;
    int gen = ++generation_;
    g_.online.get("/api/v1/levels/" + levelId_ + "/rating", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != generation_) return;
                      busy_ = false;
                      if (!r.ok) {
                          error_ = r.message.empty() ? std::string("Note indisponible.") : r.message;
                          errorCode_ = r.code;
                          return;
                      }
                      error_.clear();
                      errorCode_.clear();
                      view_ = r.data;
                      fromCache_ = r.fromCache;
                      cacheAge_ = r.cacheAgeSeconds;
                      if (!r.fromCache) g_.online.noteLevelRating(r.data);
                  }));
}

void LevelRatingPanel::send(int stars) {
    if (sending_ || levelId_.empty()) return;
    sending_ = true;
    auto done = OnlineService::guard(life_, [this, stars](const ApiResult& r) {
        sending_ = false;
        if (!r.ok) return say(r.message, true);
        view_ = r.data;
        fromCache_ = false;
        g_.online.noteLevelRating(r.data);
        say(stars > 0 ? "Merci : votre note est enregistrée." : "Votre note est retirée.", false);
    });
    if (stars > 0) g_.online.send("PUT", "/api/v1/levels/" + levelId_ + "/rating", {{"stars", stars}}, done);
    else g_.online.send("DELETE", "/api/v1/levels/" + levelId_ + "/rating", Json(), done);
}

int LevelRatingPanel::starRow(const std::string& id, float x, float y, float size, int mine, bool enabled) {
    UIContext& ui = g_.ui;
    int clicked = 0, hover = 0;
    float gap = size * 0.14f;
    for (int s = 1; s <= 5; ++s) {
        Rect b{x + (s - 1) * (size + gap), y, size, size};
        if (enabled && ui.mouseOver(b)) hover = s;
    }
    for (int s = 1; s <= 5; ++s) {
        Rect b{x + (s - 1) * (size + gap), y, size, size};
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = enabled && !sending_;
        st.selected = s == mine;
        if (ui.button(id + std::to_string(s), b, "", st)) clicked = s;
        if (ui.focused(id + std::to_string(s)) && enabled) hover = std::max(hover, s);
        int lit = hover > 0 ? hover : mine;
        bool on = s <= lit;
        drawRatingStar(ui.r(), b.center(), size * 0.33f, on ? (enabled ? kStarColor : kStarColor.withAlphaF(0.5f)) : kStarOff, on);
    }
    return clicked;
}

void LevelRatingPanel::drawCompact(const Rect& area) {
    UIContext& ui = g_.ui;
    // A run of this level was just verified: the server now accepts the rating.
    if (!attemptId_.empty() && !attemptVerifiedSeen_ && loaded() && !jget<bool>(view_, "can_rate", false))
        if (const OnlineAttempt* a = g_.online.attempts().find(attemptId_))
            for (const auto& s : a->submissions)
                if (s.status == RecordStatus::Verified) {
                    attemptVerifiedSeen_ = true;
                    refresh();
                    break;
                }
    ui.pushId("rating/" + levelId_);
    bool can = loaded() && jget<bool>(view_, "can_rate", false) && !fromCache_;
    int mine = loaded() && view_.contains("my_stars") && view_["my_stars"].is_number_integer() ? view_["my_stars"].get<int>() : 0;
    std::string line;
    Color lc = ui.theme().textDim;
    if (!status_.empty() && ui.time() - statusT_ < 5.0f) {
        line = status_;
        lc = statusError_ ? ui.theme().danger : ui.theme().success;
    } else if (!loaded()) {
        line = busy_ ? "Note du niveau : chargement…" : (error_.empty() ? "Note du niveau" : "Note indisponible : " + error_);
    } else if (fromCache_) {
        line = "Hors ligne : notation impossible pour l'instant";
    } else if (!can) {
        line = jstr(view_, "reason") == "LEVEL_NOT_COMPLETED" ? "Notez ce niveau quand votre temps sera vérifié" : "Connectez-vous pour noter ce niveau";
    } else {
        line = mine > 0 ? "Votre note du niveau : " + std::to_string(mine) + "/5" : "Notez ce niveau (1 à 5 étoiles)";
        lc = ui.theme().text;
    }
    float lineY = area.y + ui.s(9);
    ui.label(line, {area.x, lineY}, ui.fitFont(line, FontFace::Body, 16, area.w), lc, Align::Left, FontFace::Body, VAlign::Middle);
    float size = std::min(ui.s(38), (area.w - 4 * ui.s(38) * 0.14f) / 5.0f);
    int s = starRow("star", area.x, area.y + ui.s(22), size, mine, can);
    if (s > 0 && s != mine) send(s);
    ui.popId();
}

float LevelRatingPanel::drawFull(const Rect& area) {
    UIContext& ui = g_.ui;
    ui.pushId("rating/" + levelId_);
    float x = area.x, y = area.y, w = area.w;
    ui.label("NOTE DES JOUEURS", {x, y + ui.s(10)}, 15, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
    y += ui.s(26);
    if (!loaded()) {
        std::string t = busy_ ? "Chargement…" : (error_.empty() ? "Note indisponible." : error_);
        Vec2 sz = ui.labelWrapped(t, {x, y}, w, 17, ui.theme().textDim);
        ui.popId();
        return y + sz.y + ui.s(8) - area.y;
    }
    const Json& rt = view_.value("rating", Json::object());
    int count = jget<int>(rt, "count", 0);
    double avg = rt.contains("average") && rt["average"].is_number() ? rt["average"].get<double>() : 0.0;
    drawRatingSummary(ui, {x, y + ui.s(18)}, avg, count, 26, ui.theme().text, w);
    y += ui.s(40);
    if (fromCache_) {
        std::string t = "Hors ligne : moyenne reçue " + OnlineUi::ageText(cacheAge_);
        ui.label(t, {x, y + ui.s(8)}, ui.fitFont(t, FontFace::Body, 15, w), ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(24);
    }
    Vec2 ex = ui.labelWrapped("Moyenne calculée par Cosmic Online à partir des notes des joueurs qui ont terminé ce niveau (temps vérifié). "
                              "Ce n'est pas la difficulté /5, calculée par le jeu à partir du niveau lui-même.",
                              {x, y}, w, 14, ui.theme().textDim);
    y += ex.y + ui.s(14);
    ui.label("VOTRE NOTE", {x, y + ui.s(10)}, 15, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
    y += ui.s(26);
    bool can = jget<bool>(view_, "can_rate", false) && !fromCache_;
    int mine = view_.contains("my_stars") && view_["my_stars"].is_number_integer() ? view_["my_stars"].get<int>() : 0;
    float size = std::min(ui.s(46), (w - 4 * ui.s(46) * 0.14f) / 5.0f);
    int s = starRow("star", x, y, size, mine, can);
    if (s > 0 && s != mine) send(s);
    y += size + ui.s(10);
    std::string hint;
    if (fromCache_) hint = "Hors ligne : la note se donne une fois reconnecté.";
    else if (!can && jstr(view_, "reason") == "LEVEL_NOT_COMPLETED")
        hint = "Terminez ce niveau connecté à Cosmic Online : une fois votre temps vérifié, vous pourrez le noter.";
    else if (!can) hint = "Connectez-vous pour noter ce niveau.";
    else if (mine > 0) hint = "Votre note : " + std::to_string(mine) + "/5. Choisissez une autre étoile pour la changer.";
    else hint = "Choisissez de 1 à 5 étoiles.";
    Vec2 hs = ui.labelWrapped(hint, {x, y}, w, 15, can ? ui.theme().text : ui.theme().textDim);
    y += hs.y + ui.s(10);
    if (mine > 0 && can) {
        UIContext::ButtonStyle d;
        d.small = true;
        d.danger = true;
        d.fontSize = 16;
        d.enabled = !sending_;
        float bw = std::min(w, ui.text().measure("Retirer ma note", FontFace::Bold, ui.s(16)).x + ui.s(36));
        if (ui.button("remove", {x, y, bw, ui.s(36)}, "Retirer ma note", d)) send(0);
        y += ui.s(46);
    }
    if (!status_.empty() && ui.time() - statusT_ < 6.0f) {
        ui.label(status_, {x, y + ui.s(10)}, ui.fitFont(status_, FontFace::Body, 15, w), statusError_ ? ui.theme().danger : ui.theme().success,
                 Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(26);
    }
    ui.popId();
    return y - area.y;
}

}  // namespace cosmic
