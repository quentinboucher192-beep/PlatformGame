// ADMINISTRATION › Calendrier des défis (2.41), see AdminCalendar.h. At the left the month:
// the month's challenge on top, Monday to Sunday with the week's challenge in a last column;
// each cell shows its level (its preview behind), and a pill: « tirage » (the server's draw,
// replaceable), « choisi » (by an administrator), or a padlock « en cours » / « passé ». At
// the right the editor of the period selected, in two tabs: « Niveau et règles » (campaign or
// community level, hearts, slots, imposed powers, stations, checkpoints) and « Objectif et
// paliers » (« Vitesse »: Argent and Or times, Or without damage; « Finir le niveau »: Bronze
// and Argent shares of the route, Or the finish; only for a level with a route), then
// « Revenir au tirage » and « Enregistrer ». Keyboard, gamepad and mouse throughout.

#include "ui/screens/AdminCalendar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"

namespace cosmic {

using namespace challenge_ui;

namespace {

constexpr float kPi = 3.14159265f;
constexpr int64_t kMaxTierMs = 2LL * 3600 * 1000;

const char* const kDayShort[7] = {"Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim"};
const char* const kDayNames[7] = {"Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"};

/// "20/11" of "2026-11-20".
std::string ddmm(const std::string& date) {
    if (date.size() < 10) return date;
    return date.substr(8, 2) + "/" + date.substr(5, 2);
}

/// "1:45" (whole seconds), else "1:45.250".
std::string tierTime(int64_t ms) {
    if (ms <= 0) return "—";
    if (ms % 1000 != 0) return formatRunTime(ms);
    int64_t s = ms / 1000;
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", static_cast<int>(s / 3600), static_cast<int>((s / 60) % 60), static_cast<int>(s % 60));
    else std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
    return b;
}

std::string kindPill(const std::string& kind) {
    return kind == "weekly" ? "Défi de la semaine" : kind == "monthly" ? "Défi du mois" : "Défi du jour";
}

/// Lower case, ASCII accents folded (the campaign's search).
std::string folded(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = static_cast<unsigned char>(s[i + 1]);
            ++i;
            if (d >= 0xA0 && d <= 0xA5) out += 'a';
            else if (d == 0xA7) out += 'c';
            else if (d >= 0xA8 && d <= 0xAB) out += 'e';
            else if (d >= 0xAC && d <= 0xAF) out += 'i';
            else if (d >= 0xB2 && d <= 0xB6) out += 'o';
            else if (d >= 0xB9 && d <= 0xBC) out += 'u';
            else if (d >= 0x80 && d <= 0x85) out += 'a';
            else if (d >= 0x88 && d <= 0x8B) out += 'e';
            else if (d >= 0x8C && d <= 0x8F) out += 'i';
            else if (d >= 0x92 && d <= 0x96) out += 'o';
            else if (d >= 0x99 && d <= 0x9C) out += 'u';
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

/// A small padlock centred on `c`, `h` high.
void padlock(UIContext& ui, Vec2 c, float h, Color col) {
    float bw = h * 0.7f, bh = h * 0.52f;
    Rect body{c.x - bw * 0.5f, c.y - h * 0.5f + h * 0.46f, bw, bh};
    ui.r().drawArc({c.x, body.y}, bw * 0.3f, std::max(1.2f, h * 0.13f), kPi, 2.0f * kPi, col);
    ui.r().fillRoundedRect(body, std::max(1.0f, h * 0.08f), col);
}

/// The French label of a field of the server's validation (its errors name them).
std::string fieldLabel(const std::string& f) {
    if (f == "hearts") return "Cœurs";
    if (f == "slots") return "Emplacements";
    if (f == "powers") return "Pouvoirs";
    if (f == "silver_ms") return "Argent";
    if (f == "gold_ms") return "Or";
    if (f == "bronze_progress") return "Bronze";
    if (f == "silver_progress") return "Argent";
    if (f == "objective") return "Objectif";
    if (f.rfind("level", 0) == 0) return "Niveau";
    return "Données";
}

/// What the server said, in a sentence (French).
std::string errorText(const ApiResult& r) {
    if (r.networkFailure()) return "Connexion impossible : rien n'a changé.";
    if (r.code == "VALIDATION_ERROR" && r.details.is_array() && !r.details.empty() && r.details[0].is_object())
        return fieldLabel(jstr(r.details[0], "field")) + " : " + jstr(r.details[0], "message");
    return r.message.empty() ? std::string("Refusé par le serveur.") : r.message;
}

}  // namespace

AdminCalendar::AdminCalendar(GameServices& g, OnlineUi& ou, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), ou_(ou), say_(std::move(say)), lostRole_(std::move(lostRole)), art_(g) {}

void AdminCalendar::open(const std::string& arg) {
    // "YYYY-MM[:<challenge id>[:objectif]]"
    size_t a = arg.find(':');
    std::string month = arg.substr(0, a);
    if (challenge_period::valid(ChallengeKind::Monthly, month)) month_ = month;
    if (a != std::string::npos) {
        std::string rest = arg.substr(a + 1);
        size_t b = rest.find(':');
        wantSelect_ = rest.substr(0, b);
        if (b != std::string::npos && rest.substr(b + 1).rfind("obj", 0) == 0) subTab_ = 1;
    }
}

void AdminCalendar::enter(bool force) {
    if (month_.empty()) month_ = ChallengeService::utcMonth(g_.challenges.nowMs());
    if (!entered_ || force) {
        entered_ = true;
        loadMonth(month_);
    }
    if (!campaignLoaded_) loadCampaign();
}

// ------------------------------------------------------------------ server

void AdminCalendar::loadMonth(const std::string& month) {
    month_ = month;
    loading_ = true;
    loadError_.clear();
    int gen = ++monthGen_;
    g_.online.get("/api/v1/admin/challenges?month=" + month, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != monthGen_) return;
                      loading_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) {
                          loadError_ = errorText(r);
                          return;
                      }
                      periods_.clear();
                      for (const Json& x : jfield(r.data, "periods", Json::array())) {
                          Period p;
                          p.id = jstr(x, "id");
                          p.kind = jstr(x, "kind");
                          p.period = jstr(x, "period");
                          p.date = jstr(x, "date");
                          p.endDate = jstr(x, "end_date");
                          p.state = jstr(x, "state");
                          p.drawsOn = jstr(x, "draws_on");
                          p.editable = jget<bool>(x, "editable", false);
                          p.challenge = jfield(x, "challenge", Json());
                          if (p.challenge.is_object()) p.parsed = DailyChallenge::fromJson(p.challenge);
                          periods_.push_back(std::move(p));
                      }
                      firstMonth_ = jstr(r.data, "first_month");
                      lastMonth_ = jstr(r.data, "last_month");
                      today_ = jstr(r.data, "today");
                      loaded_ = true;
                      std::string want = !wantSelect_.empty() ? wantSelect_ : selected_;
                      wantSelect_.clear();
                      if (const Period* p = find(want)) {
                          if (want != selected_ || !dirty_) {
                              selected_ = want;
                              draftFrom(*p);
                          }
                          g_.ui.setFocus("admin/cal/c" + want);
                      } else {
                          selected_.clear();
                      }
                  }),
                  false);
}

void AdminCalendar::loadCampaign() {
    campaignLoaded_ = true;  // asked once (again with « Réessayer »)
    g_.online.get("/api/v1/admin/challenges/levels?source=campaign", OnlineService::guard(life_, [this](const ApiResult& r) {
                      if (lostRole_(r)) return;
                      if (!r.ok) {
                          campaignLoaded_ = false;
                          return say_(errorText(r), true);
                      }
                      campaign_ = jfield(r.data, "levels", Json::array());
                      playerMaxHearts_ = std::clamp(jget<int>(r.data, "player_max_hearts", 10), 1, 20);
                      // A draft read before the list: its times when the challenge kept none.
                      if (const Period* p = find(selected_); p && !dirty_) draftFrom(*p);
                  }),
                  false);
}

void AdminCalendar::loadCommunity(int offset) {
    communityOffset_ = std::max(0, offset);
    searched_ = search_;
    int gen = ++communityGen_;
    std::string path = "/api/v1/admin/challenges/levels?source=community&limit=20&offset=" + std::to_string(communityOffset_);
    if (search_.size() >= 2) path += "&q=" + online::encodeQueryValue(search_);
    g_.online.get(path, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != communityGen_) return;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(errorText(r), true);
                      community_ = jfield(r.data, "levels", Json::array());
                      communityMore_ = jget<bool>(r.data, "has_more", false);
                      communityLoaded_ = true;
                  }),
                  false);
}

const AdminCalendar::Period* AdminCalendar::find(const std::string& id) const {
    if (id.empty()) return nullptr;
    for (const Period& p : periods_)
        if (p.id == id) return &p;
    return nullptr;
}

void AdminCalendar::select(const std::string& periodId) {
    const Period* p = find(periodId);
    if (!p) return;
    selected_ = periodId;
    draftFrom(*p);
}

void AdminCalendar::draftFrom(const Period& p) {
    Draft d;
    d.periodId = p.id;
    d.hearts = playerMaxHearts_;
    const Json& c = p.challenge;
    if (c.is_object()) {
        const Json& lv = jfield(c, "level", Json());
        d.level = true;
        d.levelSource = jstr(lv, "source");
        d.levelId = jstr(lv, "level_id");
        d.levelVersion = jget<int>(lv, "level_version", 0);
        d.levelTitle = p.parsed ? levelTitle(g_, *p.parsed) : jstr(lv, "title");
        const Json& author = jfield(lv, "author", Json());
        d.authorName = author.is_object() ? jstr(author, "username") : std::string();
        d.hasRace = jget<bool>(c, "progress_available", false);
        const Json& rules = jfield(c, "rules", Json());
        d.hearts = jget<int>(rules, "hearts", d.hearts);
        d.slots = jget<int>(rules, "slots", 4);
        for (const Json& pw : jfield(rules, "powers", Json::array()))
            if (pw.is_string()) d.powers.push_back(pw.get<std::string>());
        d.stations = jget<bool>(rules, "stations", true);
        d.checkpoints = jget<bool>(rules, "checkpoints", true);
        d.finish = jstr(c, "objective", "speed") == "finish";
        const Json& tiers = jfield(c, "tiers", Json());
        d.goldNoDamage = jget<bool>(tiers, "gold_no_damage", true);
        if (d.finish) {
            d.bronzePct = jget<int>(tiers, "bronze_progress", 50);
            d.silverPct = jget<int>(tiers, "silver_progress", 80);
            // Its times are those kept for the older games: « Vitesse » proposes the level's medals.
            for (const Json& l : campaign_)
                if (d.levelSource == "campaign" && jstr(l, "level_id") == d.levelId) {
                    d.silverMs = jget<int64_t>(l, "silver_ms", 0);
                    d.goldMs = jget<int64_t>(l, "gold_ms", 0);
                }
        } else {
            d.silverMs = jget<int64_t>(tiers, "silver_ms", 0);
            d.goldMs = jget<int64_t>(tiers, "gold_ms", 0);
        }
    }
    draft_ = d;
    dirty_ = false;
    scrollToChosen_ = true;
    // The list of the chosen level's kind (a community level: read once).
    if (d.level) {
        levelSource_ = d.levelSource == "community" ? 1 : 0;
        if (levelSource_ == 1 && !communityLoaded_) loadCommunity(0);
    }
    editorMsg_.clear();
    editorError_ = false;
    picker_ = false;
}

void AdminCalendar::pickCampaign(const Json& l) {
    draft_.level = true;
    draft_.levelSource = "campaign";
    draft_.levelId = jstr(l, "level_id");
    draft_.levelVersion = jget<int>(l, "version", 0);
    const LevelEntry* e = g_.content.level(draft_.levelId);
    draft_.levelTitle = e ? e->name : jstr(l, "title");
    draft_.authorName.clear();
    draft_.hasRace = jget<bool>(l, "has_race", false);
    // The level's own rules and medals, as the server's defaults.
    draft_.slots = std::clamp(jget<int>(l, "slots", 4), 1, 8);
    draft_.powers.clear();
    for (const Json& pw : jfield(l, "start_powers", Json::array()))
        if (pw.is_string()) draft_.powers.push_back(pw.get<std::string>());
    std::sort(draft_.powers.begin(), draft_.powers.end());
    draft_.silverMs = jget<int64_t>(l, "silver_ms", 0);
    draft_.goldMs = jget<int64_t>(l, "gold_ms", 0);
    if (draft_.finish && !draft_.hasRace) draft_.finish = false;
    dirty_ = true;
    editorMsg_.clear();
}

void AdminCalendar::pickCommunity(const Json& l) {
    draft_.level = true;
    draft_.levelSource = "community";
    draft_.levelId = jstr(l, "level_id");
    draft_.levelVersion = jget<int>(l, "version", 1);
    draft_.levelTitle = jstr(l, "title");
    const Json& author = jfield(l, "author", Json());
    draft_.authorName = author.is_object() ? jstr(author, "username") : std::string();
    draft_.hasRace = jget<bool>(l, "has_race", false);
    draft_.slots = 4;
    draft_.powers.clear();
    const Json& sug = jfield(l, "suggested", Json());
    if (sug.is_object()) {
        draft_.silverMs = jget<int64_t>(sug, "silver_ms", 0);
        draft_.goldMs = jget<int64_t>(sug, "gold_ms", 0);
    }
    if (draft_.finish && !draft_.hasRace) draft_.finish = false;
    dirty_ = true;
    editorMsg_.clear();
}

std::string AdminCalendar::draftProblem() const {
    const Draft& d = draft_;
    if (!d.level) return "Choisissez un niveau.";
    if (static_cast<int>(d.powers.size()) > d.slots) return "Plus de pouvoirs imposés que d'emplacements.";
    if (d.finish && !d.hasRace) return "« Finir le niveau » demande un niveau dont le parcours est tracé.";
    if (d.finish && d.silverPct <= d.bronzePct) return "L'Argent doit demander une plus grande part du parcours que le Bronze.";
    if (!d.finish && (d.silverMs <= 0 || d.goldMs <= 0)) return "Donnez les temps de l'Argent et de l'Or.";
    if (!d.finish && d.goldMs > d.silverMs) return "Le temps de l'Or doit être inférieur ou égal à celui de l'Argent.";
    return {};
}

Json AdminCalendar::body() const {
    const Draft& d = draft_;
    Json level = {{"source", d.levelSource}, {"id", d.levelId}};
    if (d.levelSource == "community") level["version"] = d.levelVersion;
    Json b = {{"level", level},
              {"hearts", d.hearts},
              {"slots", d.slots},
              {"powers", d.powers},
              {"stations", d.stations},
              {"checkpoints", d.checkpoints},
              {"objective", d.finish ? "finish" : "speed"},
              {"gold_no_damage", d.goldNoDamage}};
    if (d.finish) {
        b["bronze_progress"] = d.bronzePct;
        b["silver_progress"] = d.silverPct;
    } else {
        b["silver_ms"] = d.silverMs;
        b["gold_ms"] = d.goldMs;
    }
    return b;
}

void AdminCalendar::save() {
    const Period* p = find(selected_);
    if (!p || busy_) return;
    busy_ = true;
    std::string id = p->id;
    g_.online.send("PUT", "/api/v1/admin/challenges/" + id, body(), OnlineService::guard(life_, [this, id](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           editorMsg_ = errorText(r);
                           editorError_ = true;
                           say_(editorMsg_, true);
                           return;
                       }
                       for (Period& p : periods_)
                           if (p.id == id) {
                               p.challenge = jfield(r.data, "challenge", Json());
                               p.parsed = p.challenge.is_object() ? DailyChallenge::fromJson(p.challenge) : std::nullopt;
                               p.drawsOn.clear();
                               if (selected_ == id) draftFrom(p);
                           }
                       std::string msg = "Défi enregistré : il sera proposé aux joueurs à son début.";
                       const Json& warnings = jfield(r.data, "warnings", Json::array());
                       if (warnings.is_array() && !warnings.empty() && warnings[0].is_string()) {
                           editorMsg_ = "Enregistré. Attention : " + warnings[0].get<std::string>() + ".";
                           editorError_ = false;
                       } else {
                           editorMsg_ = msg;
                           editorError_ = false;
                       }
                       say_(msg, false);
                   }));
}

void AdminCalendar::resetToDraw() {
    const Period* p = find(selected_);
    if (!p || busy_) return;
    busy_ = true;
    std::string id = p->id;
    g_.online.send("DELETE", "/api/v1/admin/challenges/" + id, Json(), OnlineService::guard(life_, [this, id](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           editorMsg_ = errorText(r);
                           editorError_ = true;
                           say_(editorMsg_, true);
                           return;
                       }
                       std::string msg;
                       for (Period& p : periods_)
                           if (p.id == id) {
                               p.challenge = jfield(r.data, "challenge", Json());
                               p.parsed = p.challenge.is_object() ? DailyChallenge::fromJson(p.challenge) : std::nullopt;
                               p.drawsOn = jstr(r.data, "draws_on");
                               if (selected_ == id) draftFrom(p);
                               if (p.parsed) {
                                   i18n::Verbatim v(p.parsed->community());
                                   msg = "Revenu au tirage du serveur : « " + levelTitle(g_, *p.parsed) + " ».";
                               } else {
                                   msg = p.drawsOn.empty() ? std::string("Choix retiré : le serveur tirera ce défi.")
                                                           : "Choix retiré : le serveur tirera ce défi le " + ddmm(p.drawsOn) + ".";
                               }
                           }
                       editorMsg_ = msg;
                       editorError_ = false;
                       say_(msg, false);
                   }));
}

// ------------------------------------------------------------------ the calendar

void AdminCalendar::draw(const Rect& body) {
    UIContext& ui = g_.ui;
    art_.beginFrame();
    ui.pushId("cal");
    // The community search, once the typing stops.
    if (searchDirty_ && ui.time() - searchT_ > 0.35f) {
        searchDirty_ = false;
        if (levelSource_ == 1) loadCommunity(0);
    }
    const float gap = ui.s(18);
    Rect cal{body.x, body.y, (body.w - gap) * 0.615f, body.h};
    Rect ed{cal.right() + gap, body.y, body.right() - cal.right() - gap, body.h};
    drawCalendar(cal);
    drawEditor(ed);
    ui.popId();
}

void AdminCalendar::drawCalendar(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(r, ui.s(14), Color{10, 15, 32, 225});
    ui.r().drawRoundedRect(r, ui.s(14), ui.theme().border, ui.s(1));
    const float x = r.x + ui.s(18), w = r.w - ui.s(36);
    float y = r.y + ui.s(16);
    // Mois précédent, the month, Mois suivant.
    const float hh = ui.s(46);
    {
        const std::string prevL = "Mois précédent", nextL = "Mois suivant";
        float bw = std::max(ui.text().measure(prevL, FontFace::Bold, ui.s(17)).x, ui.text().measure(nextL, FontFace::Bold, ui.s(17)).x) + ui.s(36);
        UIContext::ButtonStyle nb;
        nb.small = true;
        nb.fontSize = 17;
        nb.enabled = !loading_ && (firstMonth_.empty() || month_ > firstMonth_);
        if (ui.button("prev", {x, y, bw, hh}, prevL, nb)) loadMonth(challenge_period::addMonths(month_, -1));
        nb.enabled = !loading_ && (lastMonth_.empty() || month_ < lastMonth_);
        if (ui.button("next", {x + w - bw, y, bw, hh}, nextL, nb)) loadMonth(challenge_period::addMonths(month_, 1));
        int yy = 0, mm = 0;
        std::sscanf(month_.c_str(), "%4d-%2d", &yy, &mm);
        fitLabel(ui, std::string(monthTitle(mm)) + " " + std::to_string(yy), {x + w * 0.5f, y + hh * 0.5f}, 30, w - 2 * bw - ui.s(40), ui.theme().text,
                 Align::Center, FontFace::Title);
    }
    y += hh + ui.s(12);
    const float footH = ui.s(30);
    const float bottom = r.bottom() - ui.s(14) - footH - ui.s(8);
    if (!loaded_) {
        std::string t = loadError_.empty() ? std::string("Chargement du calendrier…") : "Calendrier indisponible : " + loadError_;
        Vec2 s = ui.labelWrapped(t, {x, y + ui.s(20)}, w, fz(ui, 18), loadError_.empty() ? ui.theme().textDim : ui.theme().warm);
        if (!loadError_.empty() && ui.button("retry", {x, y + ui.s(40) + s.y, ui.s(220), ui.s(46)}, "Réessayer")) loadMonth(month_);
        return;
    }
    // The month's challenge on top.
    const float bandH = ui.s(50);
    drawMonthBand({x, y, w, bandH});
    y += bandH + ui.s(10);
    // The days, Monday to Sunday, and the week's challenge in a last column.
    const float gap = ui.s(6);
    const float weekW = std::max(ui.s(132), w * 0.15f);
    const float dayW = (w - weekW - 7 * gap) / 7.0f;
    const float headH = ui.s(24);
    for (int d = 0; d < 7; ++d)
        fitLabel(ui, kDayShort[d], {x + (dayW + gap) * static_cast<float>(d) + ui.s(8), y + headH * 0.5f}, 15, dayW - ui.s(10), ui.theme().textDim, Align::Left,
                 FontFace::Bold);
    fitLabel(ui, "Semaine", {x + 7 * (dayW + gap) + ui.s(8), y + headH * 0.5f}, 15, weekW - ui.s(10), ui.theme().violet, Align::Left, FontFace::Bold);
    y += headH + ui.s(4);
    const int64_t todayN = challenge_period::dayNumber(today_).value_or(g_.challenges.nowMs() / 86400000LL);
    const int64_t firstDay = challenge_period::firstDay(ChallengeKind::Monthly, month_).value_or(todayN);
    const int days = std::max(28, challenge_period::daysInMonth(month_));
    const int64_t gridStart = firstDay - challenge_period::weekday(firstDay);
    const int rows = static_cast<int>((firstDay + days - 1 - gridStart) / 7) + 1;
    const float rowH = std::min(ui.s(124), (bottom - y - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows));
    for (int row = 0; row < rows; ++row) {
        const int64_t weekStart = gridStart + 7 * row;
        const float ry = y + (rowH + gap) * static_cast<float>(row);
        for (int d = 0; d < 7; ++d) {
            const int64_t day = weekStart + d;
            Rect cell{x + (dayW + gap) * static_cast<float>(d), ry, dayW, rowH};
            if (day < firstDay || day >= firstDay + days) {
                ui.r().fillRoundedRect(cell, ui.s(8), Color{10, 14, 26, 90});
                continue;
            }
            const std::string date = challenge_period::dateOfDay(day);
            drawCell("c" + std::string("daily-") + date, cell, find("daily-" + date), std::to_string(day - firstDay + 1), day == todayN);
        }
        const std::string week = challenge_period::ofDay(ChallengeKind::Weekly, weekStart);
        Rect wcell{x + 7 * (dayW + gap), ry, weekW, rowH};
        drawCell("cweekly-" + week, wcell, find("weekly-" + week), "Sem. " + std::to_string(std::atoi(week.substr(6).c_str())), false);
        ui.r().drawRoundedRect(wcell, ui.s(8), ui.theme().violet.withAlpha(70), std::max(1.0f, ui.s(1.0f)));
    }
    drawLegend(x, r.bottom() - ui.s(14) - footH * 0.5f, w);
}

void AdminCalendar::drawMonthBand(const Rect& r) {
    UIContext& ui = g_.ui;
    const Period* p = find("monthly-" + month_);
    const DailyChallenge* c = p && p->parsed ? &*p->parsed : nullptr;
    bool hovered = false;
    const std::string name = "cmonthly-" + month_;
    if (p && ui.hotspot(name, r, &hovered)) select(p->id);
    const bool focused = ui.focused(name), sel = p && p->id == selected_;
    ui.r().fillRoundedRect(r, ui.s(10), hovered || focused ? Color{26, 44, 76, 240} : Color{14, 22, 46, 225});
    if (c) art_.drawCellImage(r, *c, hovered || focused || sel);
    ui.r().drawRoundedRect(r, ui.s(10), sel ? ui.theme().accent : ui.theme().violet.withAlpha(focused ? 230 : 130),
                           std::max(1.0f, ui.s(sel ? 2.5f : focused ? 2.0f : 1.2f)));
    float x = r.x + ui.s(14), right = r.right() - ui.s(14), cy = r.center().y, h = std::min(ui.s(28), r.h * 0.56f);
    x += chip(ui, "Défi du mois", x, cy, h, ui.theme().violet, r.w * 0.3f, true) + ui.s(14);
    // The pill at the right (measured first), the title in what is left.
    if (p) {
        float pw = 0.0f;
        std::string text = p->state == "past" ? "passé" : p->state == "running" ? "en cours · verrouillé" : !c ? "+ choisir" : c->source == "admin" ? "choisi" : "tirage";
        // As drawPill measures it (padding, padlock, text).
        float fs = std::max(h / ui.s(1.0f) * 0.5f, 11.5f / ui.s(1.0f));
        pw = ui.text().measure(text, FontFace::Bold, ui.s(fs)).x + h * 0.8f + (p->state != "coming" ? h * 0.62f : 0.0f);
        drawPill(p, right - pw, cy, h, pw);
        right -= pw + ui.s(14);
    }
    if (c) {
        std::optional<i18n::Verbatim> v;
        if (c->community()) v.emplace();
        float used = fitLabel(ui, levelTitle(g_, *c), {x, cy}, 19, right - x, ui.theme().text, Align::Left, FontFace::Bold).x;
        v.reset();
        art_.drawTags(*c, x + used + ui.s(14), cy, right, std::min(ui.s(22), r.h * 0.44f));
    } else if (p) {
        std::string t = !p->drawsOn.empty() ? "Pas encore tiré : tirage le " + ddmm(p->drawsOn) + ", sauf choix d'un admin." : std::string("Pas de défi du mois.");
        fitLabel(ui, t, {x, cy}, 17, right - x, ui.theme().textDim);
    }
}

void AdminCalendar::drawPill(const Period* p, float x, float cy, float h, float room) {
    UIContext& ui = g_.ui;
    if (!p) return;
    const DailyChallenge* c = p->parsed ? &*p->parsed : nullptr;
    const bool locked = p->state != "coming";
    std::string text;
    Color col = ui.theme().textDim;
    bool filled = false;
    if (p->state == "past") text = "passé";
    else if (p->state == "running") text = p->kind == "monthly" ? "en cours · verrouillé" : "en cours";
    else if (!c) text = "+ choisir";
    else if (c->source == "admin") text = "choisi", col = ui.theme().accent;
    else text = "tirage", col = Color{185, 200, 218, 255};
    if (p->state == "running") col = ui.theme().warm;
    // Tight: padding, the padlock (when locked), the text, padding.
    const float pad = h * 0.4f, lockW = locked ? h * 0.62f : 0.0f;
    float fs = std::max(h / ui.s(1.0f) * 0.5f, 11.5f / ui.s(1.0f));
    float tw = ui.text().measure(text, FontFace::Bold, ui.s(fs)).x;
    float w = std::min(room, tw + 2 * pad + lockW);
    if (w < h * 1.4f) return;
    Rect pr{x, cy - h * 0.5f, w, h};
    ui.r().fillRoundedRect(pr, h * 0.5f, filled ? col : col.withAlphaF(text == "+ choisir" ? 0.06f : 0.16f));
    ui.r().drawRoundedRect(pr, h * 0.5f, col.withAlphaF(0.85f), std::max(1.0f, ui.s(1.1f)));
    float tx = pr.x + pad;
    if (locked) {
        padlock(ui, {tx + h * 0.22f, cy}, h * 0.5f, col);
        tx += lockW;
    }
    ui.label(ui.elide(text, FontFace::Bold, fs, pr.right() - pad * 0.6f - tx), {tx, cy}, fs, col, Align::Left, FontFace::Bold, VAlign::Middle);
}

void AdminCalendar::drawCell(const std::string& name, const Rect& r, const Period* p, const std::string& head, bool today) {
    UIContext& ui = g_.ui;
    const DailyChallenge* c = p && p->parsed ? &*p->parsed : nullptr;
    bool hovered = false;
    if (p && ui.hotspot(name, r, &hovered)) select(p->id);
    const bool focused = ui.focused(name), sel = p && p->id == selected_;
    Color bg = c ? Color{12, 18, 38, 215} : Color{10, 14, 26, 160};
    if (hovered || focused) bg = Color{26, 44, 76, 240};
    ui.r().fillRoundedRect(r, ui.s(8), bg);
    if (c) {
        art_.drawCellImage(r, *c, hovered || focused || sel);
        Color kc = c->kind == ChallengeKind::Weekly ? ui.theme().violet : c->kind == ChallengeKind::Monthly ? ui.theme().warm : ui.theme().accent;
        ui.r().fillRoundedRect({r.x + ui.s(8), r.y, r.w - ui.s(16), std::max(2.0f, ui.s(3))}, ui.s(1.5f), kc.withAlpha(220));
    }
    if (sel) ui.r().drawRoundedRect(r, ui.s(8), ui.theme().accent, std::max(1.5f, ui.s(2.5f)));
    else if (focused) ui.r().drawRoundedRect(r, ui.s(8), ui.theme().accent.withAlpha(190), std::max(1.0f, ui.s(1.5f)));
    else if (today) ui.r().drawRoundedRect(r, ui.s(8), ui.theme().accent.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
    const float px = r.x + ui.s(8), room = r.w - ui.s(14);
    const float l1 = r.y + r.h * 0.2f, l2 = r.y + r.h * 0.46f, l3 = r.y + r.h * 0.76f;
    fitLabel(ui, head, {px, l1}, 17, room, today ? ui.theme().accent : ui.theme().text, Align::Left, FontFace::Bold);
    if (!p) return;
    if (c) {
        std::optional<i18n::Verbatim> v;
        if (c->community()) v.emplace();  // a player's level: as its author named it
        fitLabel(ui, levelTitle(g_, *c), {px, l2}, 14, room, Color{185, 200, 218, 255});
    } else {
        std::string t = !p->drawsOn.empty() ? "tirage le " + std::to_string(std::atoi(p->drawsOn.substr(8).c_str())) : std::string("—");
        fitLabel(ui, t, {px, l2}, 14, room, ui.theme().textDim);
    }
    drawPill(p, px, l3, std::min(ui.s(24), r.h * 0.24f), room);
}

void AdminCalendar::drawLegend(float x, float cy, float w) {
    UIContext& ui = g_.ui;
    struct Item {
        std::string pill, text;
        Color c;
        bool lock;
    };
    const Item items[3] = {{"tirage", "tiré au sort par le serveur, remplaçable", Color{185, 200, 218, 255}, false},
                           {"choisi", "par un admin", ui.theme().accent, false},
                           {"", "commencé : verrouillé", ui.theme().textDim, true}};
    const float h = ui.s(24);
    float size = 15.0f;
    // One size for the whole line, smaller until it fits (never cut).
    for (; size >= 11.5f; size -= 0.5f) {
        float total = 0.0f;
        for (const Item& it : items)
            total += (it.lock ? h * 0.8f : chipWidth(ui, it.pill, h) + ui.s(8)) + ui.text().measure(it.text, FontFace::Body, ui.s(fz(ui, size))).x + ui.s(26);
        if (total <= w) break;
    }
    float cx = x;
    for (const Item& it : items) {
        if (it.lock) {
            padlock(ui, {cx + h * 0.3f, cy}, h * 0.6f, it.c);
            cx += h * 0.8f;
        } else {
            cx += chip(ui, it.pill, cx, cy, h, it.c) + ui.s(8);
        }
        float tw = ui.text().measure(it.text, FontFace::Body, ui.s(fz(ui, size))).x;
        if (cx + tw > x + w) break;
        ui.label(it.text, {cx, cy}, fz(ui, size), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        cx += tw + ui.s(26);
    }
}

// ------------------------------------------------------------------ the editor

void AdminCalendar::drawEditor(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(r, ui.s(14), Color{10, 15, 32, 225});
    ui.r().drawRoundedRect(r, ui.s(14), ui.theme().accent.withAlpha(150), ui.s(1.2f));
    ui.pushId("ed");
    const float x = r.x + ui.s(22), w = r.w - ui.s(44);
    float y = r.y + ui.s(18);
    const Period* p = find(selected_);
    if (!p) {
        fitLabel(ui, "Éditeur du défi", {x, y + ui.s(16)}, 24, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(50);
        ui.labelWrapped("Choisissez un jour, une semaine ou le mois dans le calendrier (clic, Entrée ou bouton A). Un défi qui n'a pas commencé se choisit "
                        "ici : niveau, règles, objectif et paliers. Un défi commencé ou passé se consulte seulement.",
                        {x, y}, w, fz(ui, 17), ui.theme().textDim);
        ui.popId();
        return;
    }
    y = drawEditorHead(*p, x, y, w);
    // The editor's two tabs.
    {
        static const char* const kSub[2] = {"Niveau et règles", "Objectif et paliers"};
        float sx = x;
        for (int i = 0; i < 2; ++i) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = subTab_ == i;
            st.fontSize = 16;
            float bw = ui.text().measure(kSub[i], FontFace::Bold, ui.s(16)).x + ui.s(32);
            if (ui.button("sub" + std::to_string(i), {sx, y, bw, ui.s(38)}, kSub[i], st)) subTab_ = i;
            sx += bw + ui.s(8);
        }
        y += ui.s(38) + ui.s(14);
    }
    // At the bottom: the buttons, and above them what the server said or what is missing.
    const bool editable = p->editable;
    const float bh = ui.s(50), by = r.bottom() - ui.s(18) - bh;
    float msgTop = by - ui.s(10);
    std::string msg = editorMsg_;
    Color msgC = editorError_ ? ui.theme().danger : ui.theme().success;
    if (msg.empty() && editable && dirty_) {
        msg = draftProblem();
        msgC = ui.theme().warm;
    }
    if (!editable) {
        msg = p->state == "past" ? "Défi passé : lecture seule." : "Défi commencé : lecture seule. Un défi commencé ne change plus.";
        msgC = ui.theme().textDim;
    }
    if (!msg.empty()) {
        float mh = ui.text().measureWrapped(msg, FontFace::Body, ui.s(fz(ui, 16)), w).y;
        // Read-only: no button, the note at the very bottom.
        msgTop = editable ? by - ui.s(10) - mh : r.bottom() - ui.s(18) - mh;
        ui.labelWrapped(msg, {x, msgTop}, w, fz(ui, 16), msgC);
        msgTop -= ui.s(8);
    }
    if (editable) {
        const std::string resetL = "Revenir au tirage", saveL = busy_ ? "Envoi…" : "Enregistrer";
        float sw = std::max(ui.text().measure("Enregistrer", FontFace::Bold, ui.s(20)).x, ui.text().measure("Envoi…", FontFace::Bold, ui.s(20)).x) + ui.s(44);
        float rw = ui.text().measure(resetL, FontFace::Bold, ui.s(20)).x + ui.s(44);
        UIContext::ButtonStyle rs;
        rs.fontSize = std::min(20.0f, ui.fitFont(resetL, FontFace::Bold, 20, std::min(rw, w * 0.55f) - ui.s(24), 0.7f));
        rs.enabled = !busy_ && p->challenge.is_object() && jstr(p->challenge, "source") == "admin" && !dirty_;
        rw = std::min(rw, w * 0.55f);
        if (ui.button("reset", {x + w - sw - ui.s(12) - rw, by, rw, bh}, resetL, rs)) resetToDraw();
        UIContext::ButtonStyle ss;
        ss.primary = true;
        ss.fontSize = 20;
        ss.enabled = !busy_ && dirty_ && draftProblem().empty();
        if (ui.button("save", {x + w - sw, by, sw, bh}, saveL, ss)) save();
    }
    Rect content{x, y, w, msgTop - ui.s(6) - y};
    if (subTab_ == 0) drawLevelTab(*p, content, editable && !busy_);
    else drawObjectiveTab(*p, content, editable && !busy_);
    ui.popId();
}

float AdminCalendar::drawEditorHead(const Period& p, float x, float y, float w) {
    UIContext& ui = g_.ui;
    const float ph = ui.s(28);
    float pw = chip(ui, kindPill(p.kind), x, y + ui.s(16), ph, p.kind == "weekly" ? ui.theme().violet : p.kind == "monthly" ? ui.theme().warm : ui.theme().accent);
    std::string title;
    if (p.kind == "daily") {
        auto n = challenge_period::dayNumber(p.date);
        title = std::string(kDayNames[n ? challenge_period::weekday(*n) : 0]) + " " + frenchDate(p.date);
    } else if (p.kind == "weekly") {
        std::string last = challenge_period::dateOfDay(challenge_period::dayNumber(p.endDate).value_or(0) - 1);
        title = "Semaine " + std::to_string(std::atoi(p.period.substr(6).c_str())) + " · du " + frenchDate(p.date, false) + " au " + frenchDate(last, false);
    } else {
        int yy = 0, mm = 0;
        std::sscanf(p.period.c_str(), "%4d-%2d", &yy, &mm);
        title = std::string(monthTitle(mm)) + " " + std::to_string(yy);
    }
    fitLabel(ui, title, {x + pw + ui.s(14), y + ui.s(16)}, 24, w - pw - ui.s(14), ui.theme().text, Align::Left, FontFace::Bold);
    y += ui.s(40);
    // When it opens (and whether it may still change), then where it comes from: two lines.
    std::string when;
    if (p.state == "coming") when = "Ouvert aux joueurs le " + ddmm(p.date) + " à 00:00 UTC · modifiable jusque-là";
    else if (p.state == "running") when = "Ouvert depuis le " + ddmm(p.date) + " à 00:00 UTC, jusqu'au " + ddmm(p.endDate) + " à 00:00 UTC · verrouillé";
    else when = "Terminé le " + ddmm(p.endDate) + " à 00:00 UTC · verrouillé";
    std::string from;
    const Json& c = p.challenge;
    if (c.is_object()) {
        if (jstr(c, "source") == "admin") {
            std::string replaced = jstr(c, "replaced_title");
            from = replaced.empty() ? std::string("Choisi par un admin.") : "Choisi par un admin : remplace le tirage « " + replaced + " ».";
        } else {
            from = "Tiré au sort par le serveur : un admin peut le remplacer tant qu'il n'a pas commencé.";
            if (p.state != "coming") from = "Tiré au sort par le serveur.";
        }
    } else if (!p.drawsOn.empty()) {
        from = "Pas encore tiré : le serveur le tirera le " + ddmm(p.drawsOn) + ", sauf si un admin le choisit avant.";
    } else {
        from = p.state == "coming" ? std::string("Pas de tirage prévu : choisissez-le ici.") : std::string("Aucun défi pour cette période.");
    }
    Vec2 s = ui.labelWrapped(when, {x, y}, w, fz(ui, 15), ui.theme().textDim);
    y += s.y + ui.s(2);
    s = ui.labelWrapped(from, {x, y}, w, fz(ui, 15), ui.theme().textDim);
    return y + s.y + ui.s(12);
}

void AdminCalendar::rowLabel(const std::string& t, float x, float cy, float w) { fitLabel(g_.ui, t, {x, cy}, 17, w, g_.ui.theme().text); }

bool AdminCalendar::stepper(const std::string& id, float right, float cy, float h, const std::string& value, int& v, int step, int lo, int hi, bool enabled) {
    UIContext& ui = g_.ui;
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 18;
    const float bw = h;
    const float vw = std::max(ui.s(56), ui.text().measure(value, FontFace::Bold, ui.s(18)).x + ui.s(16));
    bool changed = false;
    st.enabled = enabled && v + step <= hi;
    if (ui.button(id + "+", {right - bw, cy - h * 0.5f, bw, h}, "+", st)) {
        v = std::min(hi, v + step);
        changed = true;
    }
    ui.label(value, {right - bw - vw * 0.5f, cy}, 18, enabled ? ui.theme().text : ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
    st.enabled = enabled && v - step >= lo;
    if (ui.button(id + "-", {right - 2 * bw - vw, cy - h * 0.5f, bw, h}, "-", st)) {
        v = std::max(lo, v - step);
        changed = true;
    }
    if (changed) {
        dirty_ = true;
        editorMsg_.clear();
    }
    return changed;
}

bool AdminCalendar::timeStepper(const std::string& id, float right, float cy, float h, int64_t& ms, bool enabled) {
    // Seconds by 5 (from 5 s to 2 h); « — » until a first time is given (+ starts at 1:00).
    int sec = static_cast<int>(ms / 1000);
    int v = sec;
    std::string shown = tierTime(ms);
    if (!stepper(id, right, cy, h, shown, v, 5, sec > 0 ? 5 : 0, static_cast<int>(kMaxTierMs / 1000), enabled)) return false;
    if (sec <= 0 && v > 0) v = 60;
    v = std::max(5, v - v % 5);
    ms = static_cast<int64_t>(v) * 1000;
    return true;
}

void AdminCalendar::drawLevelTab(const Period& p, const Rect& r, bool editable) {
    UIContext& ui = g_.ui;
    (void)p;
    const float x = r.x, w = r.w;
    float y = r.y;
    // Rules first measured: the list of levels takes what is left.
    const float rowH = ui.s(40);
    float powersRows = 1.0f;
    {
        // The powers' chips wrap under each other in the room right of their label.
        float labelW = std::min(w * 0.42f, ui.text().measure("Pouvoirs imposés", FontFace::Body, ui.s(17)).x + ui.s(16));
        float room = w - labelW, cx = 0.0f;
        const float ch = ui.s(28), gap = ui.s(6);
        std::vector<std::string> chips;
        for (const std::string& id : draft_.powers) chips.push_back(editable ? powerList(g_, {id}) + "  ×" : powerList(g_, {id}));
        if (editable) chips.push_back("+ Ajouter");
        for (const std::string& t : chips) {
            float cw = std::min(room, chipWidth(ui, t, ch));
            if (cx > 0.0f && cx + cw > room) {
                cx = 0.0f;
                powersRows += 1.0f;
            }
            cx += cw + gap;
        }
    }
    const float rulesH = ui.s(32) + rowH * 4 + (ui.s(34) * powersRows + ui.s(6));
    const float levelBottom = r.bottom() - rulesH - ui.s(8);
    fitLabel(ui, "Niveau", {x, y + ui.s(13)}, 20, w, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(32);
    if (!editable) {
        // Read-only: the chosen level alone, the rules right under it.
        const float cardH = ui.s(62);
        drawLevelList({x, y, w, cardH + ui.s(6)}, false);
        drawRules(x, y + cardH + ui.s(18), w, false);
        return;
    }
    // Campagne / Niveaux en ligne, and the search.
    {
        static const char* const kSrc[2] = {"Campagne", "Niveaux en ligne"};
        float sx = x;
        const float h = ui.s(40);
        for (int i = 0; i < 2; ++i) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = levelSource_ == i;
            st.fontSize = 16;
            st.enabled = editable;
            float bw = ui.text().measure(kSrc[i], FontFace::Bold, ui.s(16)).x + ui.s(30);
            if (ui.button("src" + std::to_string(i), {sx, y, bw, h}, kSrc[i], st) && levelSource_ != i) {
                levelSource_ = i;
                if (i == 1 && (!communityLoaded_ || searched_ != search_)) loadCommunity(0);
            }
            sx += bw + ui.s(8);
        }
        float fw = x + w - sx;
        if (fw > ui.s(120) && editable) {
            std::string before = search_;
            ui.textField("search", {sx, y, fw, h}, search_, "Rechercher un niveau", 40);
            if (search_ != before) {
                searchDirty_ = true;
                searchT_ = ui.time();
            }
        }
        y += h + ui.s(8);
    }
    drawLevelList({x, y, w, std::max(ui.s(70), levelBottom - y)}, editable);
    drawRules(x, std::max(levelBottom, y + ui.s(70)) + ui.s(8), w, editable);
}

void AdminCalendar::drawLevelList(const Rect& r, bool editable) {
    UIContext& ui = g_.ui;
    const bool community = levelSource_ == 1;
    // Read-only, or a level of the other kind than the list: the chosen level on top.
    std::vector<const Json*> rows;
    const std::string wanted = folded(search_);
    const Json& list = community ? community_ : campaign_;
    if (list.is_array())
        for (const Json& l : list) {
            if (!editable && !(draft_.level && jstr(l, "level_id") == draft_.levelId)) continue;
            if (!community && !wanted.empty()) {
                const LevelEntry* e = g_.content.level(jstr(l, "level_id"));
                std::string name = e ? e->name : jstr(l, "title");
                if (folded(name).find(wanted) == std::string::npos && folded(jstr(l, "title")).find(wanted) == std::string::npos) continue;
            }
            rows.push_back(&l);
        }
    const float cardH = ui.s(62), gap = ui.s(6);
    const bool pager = community && editable && (communityMore_ || communityOffset_ > 0);
    Rect view{r.x, r.y, r.w, r.h - (pager ? ui.s(44) : 0.0f)};
    // A period just selected: its level in view.
    if (scrollToChosen_ && !rows.empty()) {
        scrollToChosen_ = false;
        for (size_t i = 0; i < rows.size(); ++i)
            if (draft_.level && jstr(*rows[i], "level_id") == draft_.levelId)
                ui.setScroll(community ? "lvlc" : "lvl", std::max(0.0f, static_cast<float>(i) * (cardH + gap) - (view.h - cardH) * 0.5f));
    }
    if (rows.empty()) {
        std::string t;
        if (!editable && draft_.level) {
            // The chosen level is not in the list read (a community level): said as it is.
            std::optional<i18n::Verbatim> v;
            if (draft_.levelSource == "community") v.emplace();
            t = draft_.levelTitle;
        } else {
            t = !editable ? std::string("Aucun niveau.")
                : community ? (communityLoaded_ ? std::string("Aucun niveau en ligne publié ne correspond.") : std::string("Chargement…"))
                            : (campaign_.empty() ? std::string("Chargement…") : std::string("Aucun niveau de la campagne ne correspond."));
        }
        ui.labelWrapped(t, {view.x + ui.s(4), view.y + ui.s(8)}, view.w - ui.s(8), fz(ui, 16), ui.theme().textDim);
    }
    ui.beginScroll(community ? "lvlc" : "lvl", view, static_cast<float>(rows.size()) * (cardH + gap));
    for (size_t i = 0; i < rows.size(); ++i) {
        const Json& l = *rows[i];
        Rect card{view.x, view.y + static_cast<float>(i) * (cardH + gap) - ui.scrollOffset(), view.w - ui.s(14), cardH};
        if (card.bottom() < view.y || card.y > view.bottom()) continue;
        const std::string id = jstr(l, "level_id");
        const bool chosen = draft_.level && draft_.levelId == id && draft_.levelSource == (community ? "community" : "campaign");
        const bool available = community || jget<bool>(l, "available", false);
        bool hovered = false;
        const std::string name = "l" + id;
        if (editable && available && ui.hotspot(name, card, &hovered)) {
            if (community) pickCommunity(l);
            else pickCampaign(l);
        }
        const bool focused = ui.focused(name);
        ui.r().fillRoundedRect(card, ui.s(8), chosen ? Color{20, 52, 66, 235} : hovered || focused ? Color{26, 44, 76, 240} : Color{18, 26, 50, 235});
        if (chosen || focused) ui.r().drawRoundedRect(card, ui.s(8), ui.theme().accent.withAlpha(chosen ? 220 : 160), std::max(1.0f, ui.s(chosen ? 1.8f : 1.2f)));
        // The level's preview (a campaign level: as installed).
        Rect img{card.x + ui.s(6), card.y + ui.s(6), (cardH - ui.s(12)) * 16.0f / 9.0f, cardH - ui.s(12)};
        ui.r().fillRoundedRect(img, ui.s(6), Color{11, 18, 38, 255});
        if (!community)
            if (TexturePtr t = art_.levelThumb(id, std::to_string(jget<int>(l, "version", 0)))) ui.r().drawTexture(*t, nullptr, img, Color{255, 255, 255, available ? uint8_t(255) : uint8_t(110)});
        float tx = img.right() + ui.s(12), right = card.right() - ui.s(10);
        // At the right: « choisi », or why it cannot be chosen; the route.
        const float ph = ui.s(24);
        if (chosen) right -= chip(ui, "choisi", right - chipWidth(ui, "choisi", ph), card.center().y, ph, ui.theme().accent) + ui.s(8);
        else if (!available) right -= chip(ui, "non classé", right - chipWidth(ui, "non classé", ph), card.center().y, ph, ui.theme().textDim) + ui.s(8);
        if (jget<bool>(l, "has_race", false) && right - tx > ui.s(200))
            right -= chip(ui, "parcours tracé", right - chipWidth(ui, "parcours tracé", ph), card.center().y, ph, ui.theme().success) + ui.s(8);
        std::string title, sub;
        if (community) {
            title = jstr(l, "title");
            const Json& a = jfield(l, "author", Json());
            sub = "version " + std::to_string(jget<int>(l, "version", 1));
            {
                i18n::Verbatim v;
                fitLabel(ui, title, {tx, card.y + cardH * 0.33f}, 17, right - tx, ui.theme().text, Align::Left, FontFace::Bold);
            }
            Vec2 by = ui.label("par ", {tx, card.y + cardH * 0.7f}, fz(ui, 14), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            float ax = tx + by.x;
            float aw = 0.0f;
            {
                i18n::Verbatim v;
                aw = fitLabel(ui, a.is_object() ? jstr(a, "username") : std::string("?"), {ax, card.y + cardH * 0.7f}, 14, (right - ax) * 0.5f,
                              ui.theme().text, Align::Left, FontFace::Bold)
                         .x;
            }
            fitLabel(ui, " · " + sub, {ax + aw, card.y + cardH * 0.7f}, 14, right - ax - aw, ui.theme().textDim);
        } else {
            const LevelEntry* e = g_.content.level(id);
            title = e ? e->name : jstr(l, "title");
            sub = planetLineOf(g_, id);
            std::vector<std::string> kinds;
            for (const Json& k : jfield(l, "draw_kinds", Json::array()))
                if (k.is_string()) kinds.push_back(k.get<std::string>());
            std::string draws = kinds.empty() ? std::string("hors tirage")
                                              : kinds.size() == 3 ? std::string("tirage : jour, semaine, mois")
                                              : kinds.size() == 2 ? std::string("tirage : jour, semaine")
                                                                  : std::string("tirage : jour");
            if (kinds.size() == 1 && kinds[0] == "weekly") draws = "tirage : semaine";
            sub += (sub.empty() ? "" : " · ") + draws;
            fitLabel(ui, title, {tx, card.y + cardH * 0.33f}, 17, right - tx, available ? ui.theme().text : ui.theme().textDim, Align::Left, FontFace::Bold);
            fitLabel(ui, sub, {tx, card.y + cardH * 0.7f}, 14, right - tx, ui.theme().textDim);
        }
    }
    ui.endScroll();
    if (pager) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 15;
        float bw = (r.w - ui.s(10)) * 0.5f, py = r.bottom() - ui.s(38);
        st.enabled = communityOffset_ > 0;
        if (ui.button("cprev", {r.x, py, bw, ui.s(38)}, "Précédents", st)) loadCommunity(communityOffset_ - 20);
        st.enabled = communityMore_;
        if (ui.button("cnext", {r.x + bw + ui.s(10), py, bw, ui.s(38)}, "Suivants", st)) loadCommunity(communityOffset_ + 20);
    }
}

void AdminCalendar::drawRules(float x, float y, float w, bool editable) {
    UIContext& ui = g_.ui;
    fitLabel(ui, "Règles", {x, y + ui.s(13)}, 20, w, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(32);
    const float rowH = ui.s(40), bh = ui.s(32);
    auto line = [&](float yy) { ui.r().fillRect({x, yy, w, std::max(1.0f, ui.s(1))}, Color{40, 52, 82, 160}); };
    // Cœurs, Emplacements de pouvoirs.
    {
        float cy = y + rowH * 0.5f;
        rowLabel("Cœurs", x, cy, w * 0.55f);
        stepper("hearts", x + w, cy, bh, std::to_string(draft_.hearts), draft_.hearts, 1, 1, 20, editable);
        y += rowH;
        line(y);
        cy = y + rowH * 0.5f;
        rowLabel("Emplacements de pouvoirs", x, cy, w * 0.55f);
        stepper("slots", x + w, cy, bh, std::to_string(draft_.slots), draft_.slots, 1, 1, 8, editable);
        y += rowH;
        line(y);
    }
    // Pouvoirs imposés: a chip each (× takes it off), « + Ajouter » (the picker).
    {
        float labelW = std::min(w * 0.42f, ui.text().measure("Pouvoirs imposés", FontFace::Body, ui.s(17)).x + ui.s(16));
        const float ch = ui.s(28), gap = ui.s(6), lineH = ui.s(34);
        float cx = x + labelW, cy = y + ui.s(3) + lineH * 0.5f;
        rowLabel("Pouvoirs imposés", x, y + ui.s(3) + lineH * 0.5f, labelW - ui.s(8));
        std::string remove;
        if (!editable && draft_.powers.empty()) fitLabel(ui, "aucun", {cx, cy}, 16, x + w - cx, ui.theme().textDim);
        for (const std::string& id : draft_.powers) {
            // Editable: × takes it off.
            const std::string t = editable ? powerList(g_, {id}) + "  ×" : powerList(g_, {id});
            float cw = std::min(w - labelW, chipWidth(ui, t, ch));
            if (cx > x + labelW && cx + cw > x + w) {
                cx = x + labelW;
                cy += lineH;
            }
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = true;
            st.fontSize = 14;
            st.enabled = editable;
            if (ui.button("pw" + id, {cx, cy - ch * 0.5f, cw, ch}, t, st)) remove = id;
            cx += cw + gap;
        }
        if (editable) {
            const std::string t = "+ Ajouter";
            float cw = chipWidth(ui, t, ch);
            if (cx > x + labelW && cx + cw > x + w) {
                cx = x + labelW;
                cy += lineH;
            }
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 14;
            st.enabled = editable;
            if (ui.button("pwadd", {cx, cy - ch * 0.5f, cw, ch}, t, st)) {
                picker_ = true;
                ui.setFocus("picker/close");
            }
        }
        if (!remove.empty()) {
            draft_.powers.erase(std::remove(draft_.powers.begin(), draft_.powers.end(), remove), draft_.powers.end());
            dirty_ = true;
            editorMsg_.clear();
        }
        y = cy + lineH * 0.5f + ui.s(3);
        line(y);
    }
    // Stations de pouvoirs, Checkpoints: switches.
    auto sw = [&](const std::string& id, const std::string& label, bool& v) {
        Rect row{x, y, w, rowH};
        bool hovered = false;
        if (editable && ui.hotspot(id, row, &hovered)) {
            v = !v;
            dirty_ = true;
            editorMsg_.clear();
        }
        const bool focused = ui.focused(id);
        if (focused || hovered) ui.r().fillRoundedRect(row, ui.s(6), Color{26, 44, 76, focused ? uint8_t(200) : uint8_t(120)});
        rowLabel(label, x + ui.s(4), row.center().y, w - ui.s(80));
        Rect track{row.right() - ui.s(52), row.center().y - ui.s(12), ui.s(46), ui.s(24)};
        ui.r().fillRoundedRect(track, ui.s(12), v ? ui.theme().accent.withAlpha(editable ? 220 : 120) : Color{40, 50, 70, 255});
        ui.r().fillCircle({v ? track.right() - ui.s(12) : track.x + ui.s(12), track.center().y}, ui.s(9), editable ? Color::white() : Color{170, 180, 196, 255});
        y += rowH;
    };
    sw("stations", "Stations de pouvoirs", draft_.stations);
    line(y);
    sw("checkpoints", "Checkpoints", draft_.checkpoints);
}

void AdminCalendar::drawObjectiveTab(const Period& p, const Rect& r, bool editable) {
    UIContext& ui = g_.ui;
    (void)p;
    const float x = r.x, w = r.w;
    float y = r.y;
    fitLabel(ui, "Objectif", {x, y + ui.s(13)}, 20, w, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(34);
    // Two choices, each a card with its explanation.
    struct Choice {
        bool finish;
        std::string title, text;
        bool enabled;
    };
    const bool route = draft_.level && draft_.hasRace;
    const Choice choices[2] = {
        {false, "Vitesse", "Classement aux meilleurs temps. Paliers : des temps.", true},
        {true, "Finir le niveau",
         route ? "Classement à la progression, comme la course à la vérification : le plus loin d'abord, puis le premier à finir. Paliers : une part du parcours."
               : (draft_.level ? "Impossible avec ce niveau : il n'a pas de parcours tracé (comme en ont les niveaux EXTRÊME). Choisissez-en un qui en a un."
                               : "Choisissez d'abord un niveau : il lui faut un parcours tracé (comme en ont les niveaux EXTRÊME)."),
         route},
    };
    for (const Choice& c : choices) {
        const float tw = w - ui.s(64);
        float th = ui.text().measureWrapped(c.text, FontFace::Body, ui.s(fz(ui, 15)), tw).y;
        Rect card{x, y, w, ui.s(40) + th + ui.s(12)};
        const bool on = draft_.finish == c.finish;
        const std::string id = c.finish ? "objfinish" : "objspeed";
        bool hovered = false;
        if (editable && c.enabled && ui.hotspot(id, card, &hovered) && !on) {
            draft_.finish = c.finish;
            dirty_ = true;
            editorMsg_.clear();
        }
        const bool focused = ui.focused(id);
        ui.r().fillRoundedRect(card, ui.s(10), on ? Color{20, 52, 66, 235} : hovered || focused ? Color{26, 44, 76, 240} : Color{18, 26, 50, 235});
        ui.r().drawRoundedRect(card, ui.s(10), on || focused ? ui.theme().accent : Color{57, 70, 100, 200}, std::max(1.0f, ui.s(on ? 1.8f : 1.1f)));
        // The radio mark.
        Vec2 rc{card.x + ui.s(24), card.y + ui.s(22)};
        Color mc = c.enabled ? ui.theme().text : ui.theme().textDim;
        ui.r().drawCircle(rc, ui.s(9), mc, std::max(1.0f, ui.s(1.6f)));
        if (on) ui.r().fillCircle(rc, ui.s(5), ui.theme().accent);
        fitLabel(ui, c.title, {card.x + ui.s(46), rc.y}, 18, card.w - ui.s(60), c.enabled ? ui.theme().text : ui.theme().textDim, Align::Left, FontFace::Bold);
        ui.labelWrapped(c.text, {card.x + ui.s(46), card.y + ui.s(38)}, tw, fz(ui, 15), c.enabled ? ui.theme().textDim : ui.theme().warm);
        y += card.h + ui.s(10);
    }
    y += ui.s(4);
    fitLabel(ui, "Paliers", {x, y + ui.s(13)}, 20, w, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(32);
    const float rowH = ui.s(42), bh = ui.s(32);
    auto line = [&](float yy) { ui.r().fillRect({x, yy, w, std::max(1.0f, ui.s(1))}, Color{40, 52, 82, 160}); };
    auto tierName = [&](ChallengeTier t, float cy) {
        fitLabel(ui, challengeTierLabel(t), {x, cy}, 18, w * 0.25f, tierColor(t), Align::Left, FontFace::Bold);
    };
    // The « sans dégât » switch of Or, at the right of its row.
    auto noDamage = [&](float cy) {
        const std::string id = "nodamage";
        Rect track{x + w - ui.s(46), cy - ui.s(12), ui.s(46), ui.s(24)};
        float lw = ui.text().measure("sans dégât", FontFace::Body, ui.s(fz(ui, 15))).x;
        Rect hit{track.x - lw - ui.s(14), cy - ui.s(16), track.right() - (track.x - lw - ui.s(14)), ui.s(32)};
        bool hovered = false;
        if (editable && ui.hotspot(id, hit, &hovered)) {
            draft_.goldNoDamage = !draft_.goldNoDamage;
            dirty_ = true;
            editorMsg_.clear();
        }
        if (ui.focused(id) || hovered) ui.r().fillRoundedRect(hit, ui.s(6), Color{26, 44, 76, 200});
        ui.label("sans dégât", {track.x - ui.s(10), cy}, fz(ui, 15), ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        bool v = draft_.goldNoDamage;
        ui.r().fillRoundedRect(track, ui.s(12), v ? ui.theme().accent.withAlpha(editable ? 220 : 120) : Color{40, 50, 70, 255});
        ui.r().fillCircle({v ? track.right() - ui.s(12) : track.x + ui.s(12), track.center().y}, ui.s(9), editable ? Color::white() : Color{170, 180, 196, 255});
        return hit.w;
    };
    if (draft_.finish) {
        // Bronze and Argent: a share of the route (5 by 5, 10 to 95 %); Or: the finish.
        const ChallengeTier ts[2] = {ChallengeTier::Bronze, ChallengeTier::Silver};
        int* vals[2] = {&draft_.bronzePct, &draft_.silverPct};
        for (int i = 0; i < 2; ++i) {
            float cy = y + rowH * 0.5f;
            tierName(ts[i], cy);
            const float suffixW = ui.text().measure("du parcours", FontFace::Body, ui.s(fz(ui, 15))).x;
            ui.label("du parcours", {x + w, cy}, fz(ui, 15), ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
            float right = x + w - suffixW - ui.s(12);
            stepper(i == 0 ? "bronze" : "silver", right, cy, bh, std::to_string(*vals[i]) + " %", *vals[i], 5, 10, 95, editable);
            float vw = std::max(ui.s(56), ui.text().measure(std::to_string(*vals[i]) + " %", FontFace::Bold, ui.s(18)).x + ui.s(16));
            float left = right - 2 * bh - vw - ui.s(10);
            if (left - x > ui.s(170)) ui.label("atteindre", {left, cy}, fz(ui, 15), ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
            y += rowH;
            line(y);
        }
        float cy = y + rowH * 0.5f;
        tierName(ChallengeTier::Gold, cy);
        float used = noDamage(cy);
        fitLabel(ui, "finir le niveau", {x + w - used - ui.s(14), cy}, 15, w * 0.4f, ui.theme().textDim, Align::Right);
        y += rowH;
    } else {
        // Bronze: the finish; Argent and Or: times (5 s steps).
        float cy = y + rowH * 0.5f;
        tierName(ChallengeTier::Bronze, cy);
        ui.label("arriver au bout", {x + w, cy}, fz(ui, 15), ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        y += rowH;
        line(y);
        cy = y + rowH * 0.5f;
        tierName(ChallengeTier::Silver, cy);
        timeStepper("silverms", x + w, cy, bh, draft_.silverMs, editable);
        y += rowH;
        line(y);
        cy = y + rowH * 0.5f;
        tierName(ChallengeTier::Gold, cy);
        float used = noDamage(cy);
        timeStepper("goldms", x + w - used - ui.s(14), cy, bh, draft_.goldMs, editable);
        y += rowH;
    }
    // What the players will see.
    y += ui.s(12);
    if (y + ui.s(90) > r.bottom()) return;
    fitLabel(ui, "Ce que verront les joueurs", {x, y + ui.s(13)}, 20, w, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(32);
    std::string pill = draft_.finish ? "Plus loin" : "Meilleurs temps";
    std::string text = draft_.finish
                           ? "classement du défi : la progression (le plus loin, puis le premier à finir) · paliers « " + std::to_string(draft_.bronzePct) +
                                 " % / " + std::to_string(draft_.silverPct) + " % / fini » sur sa fiche"
                           : "classement du défi : les meilleurs temps · paliers « fini / " + tierTime(draft_.silverMs) + " / " + tierTime(draft_.goldMs) +
                                 " » sur sa fiche";
    const float ph = ui.s(26);
    float pw = chipWidth(ui, pill, ph);
    float tw = w - pw - ui.s(40);
    float th = ui.text().measureWrapped(text, FontFace::Body, ui.s(fz(ui, 15)), tw).y;
    Rect box{x, y, w, std::max(ui.s(48), th + ui.s(20))};
    if (box.bottom() > r.bottom()) return;
    ui.r().fillRoundedRect(box, ui.s(10), Color{18, 26, 50, 235});
    chip(ui, pill, box.x + ui.s(12), box.center().y, ph, ui.theme().success);
    ui.labelWrapped(text, {box.x + pw + ui.s(26), box.center().y - th * 0.5f}, tw, fz(ui, 15), ui.theme().textDim);
}

// ------------------------------------------------------------------ the powers' picker

void AdminCalendar::drawModal() {
    if (!picker_) return;
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 185});
    std::vector<const PowerDefinition*> powers;
    for (const PowerDefinition* d : g_.content.powers())
        if (d && g_.content.isNativePower(d->id)) powers.push_back(d);
    std::sort(powers.begin(), powers.end(), [](const PowerDefinition* a, const PowerDefinition* b) { return a->name < b->name; });
    const int cols = 3;
    const int rows = (static_cast<int>(powers.size()) + cols - 1) / cols;
    const float bh = ui.s(44), gap = ui.s(10);
    const float bw = ui.s(230);
    float boxW = cols * bw + (cols - 1) * gap + ui.s(64);
    float boxH = ui.s(30 + 40 + 34 + 16) + rows * (bh + gap) + ui.s(16 + 50 + 26);
    Rect box{ui.screenW() * 0.5f - boxW * 0.5f, std::max(ui.s(20), ui.screenH() * 0.5f - boxH * 0.5f), boxW, std::min(boxH, ui.screenH() - ui.s(40))};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.8f), ui.s(1.5f));
    ui.pushId("admin");
    ui.pushId("cal");
    ui.pushId("ed");
    ui.pushId("picker");
    float x = box.x + ui.s(32), w = box.w - ui.s(64), y = box.y + ui.s(30);
    fitLabel(ui, "Pouvoirs imposés", {x, y + ui.s(16)}, 26, w, ui.theme().text, Align::Left, FontFace::Title);
    y += ui.s(40);
    const int n = static_cast<int>(draft_.powers.size());
    std::string hint = std::to_string(n) + " sur " + std::to_string(draft_.slots) + (draft_.slots > 1 ? " emplacements" : " emplacement") +
                       " · le joueur part avec ces pouvoirs, à la place de ceux du niveau";
    fitLabel(ui, hint, {x, y + ui.s(12)}, 16, w, ui.theme().textDim);
    y += ui.s(34 + 16);
    for (size_t i = 0; i < powers.size(); ++i) {
        const PowerDefinition* d = powers[i];
        const bool on = std::find(draft_.powers.begin(), draft_.powers.end(), d->id) != draft_.powers.end();
        UIContext::ButtonStyle st;
        st.small = true;
        st.selected = on;
        st.fontSize = 17;
        st.enabled = on || n < draft_.slots;
        Rect br{x + static_cast<float>(i % cols) * (bw + gap), y + static_cast<float>(i / cols) * (bh + gap), bw, bh};
        if (ui.button("p" + d->id, br, d->name, st)) {
            if (on) draft_.powers.erase(std::remove(draft_.powers.begin(), draft_.powers.end(), d->id), draft_.powers.end());
            else draft_.powers.push_back(d->id);
            std::sort(draft_.powers.begin(), draft_.powers.end());
            dirty_ = true;
            editorMsg_.clear();
        }
    }
    UIContext::ButtonStyle cs;
    cs.primary = true;
    if (ui.button("close", {box.right() - ui.s(32) - ui.s(200), box.bottom() - ui.s(26) - ui.s(50), ui.s(200), ui.s(50)}, "Fermer", cs) || ui.backPressed())
        picker_ = false;
    ui.popId();
    ui.popId();
    ui.popId();
    ui.popId();
}

}  // namespace cosmic
