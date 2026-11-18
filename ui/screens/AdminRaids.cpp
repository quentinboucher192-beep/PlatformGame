#include "ui/screens/AdminRaids.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/raid/RaidBranch.h"
#include "game/online/Clans.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

using namespace raidui;

namespace {

const char* const kFormats[] = {"court", "standard", "legendaire"};
const int kLocks[] = {10, 15, 30, 45, 60};

const Json& at(const Json& j, const char* key) {
    static const Json null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}

std::string plural(int n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

}  // namespace

AdminRaids::AdminRaids(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)) {
    clearForm();
}

void AdminRaids::enter(bool force) {
    if (!loaded_ || force) load();
}

void AdminRaids::load() {
    const int gen = ++gen_;
    g_.online.get("/api/v1/admin/raids", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != gen_) return;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      events_ = r.data.value("events", Json::array());
                      if (!r.fromCache) skew_ = skewOf(r.data);
                      loaded_ = true;
                      if (!selected_.empty()) loadDetail();
                  }),
                  false);
}

void AdminRaids::loadDetail() {
    if (selected_.empty()) {
        detail_ = Json();
        return;
    }
    const int gen = ++detailGen_;
    g_.online.get("/api/v1/admin/raids/" + selected_, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != detailGen_) return;
                      if (lostRole_(r)) return;
                      if (r.ok) detail_ = r.data;
                  }),
                  false);
}

void AdminRaids::clearForm() {
    selected_.clear();
    detail_ = Json();
    formatIdx_ = 1;
    lockIdx_ = 2;
    difficultyIdx_ = 2;
    open_ = true;
    capacity_ = 0.0f;
    clanCapacity_ = 50.0f;
    rewards_.clear();
    // A week from now at 21:00 of this computer.
    const raids::Civil d = raids::toSystemLocal(nowUtc(skew_) + 7 * 86400);
    date_ = raids::dateField(d);
    clock_ = "21:00";
    saved_ = Json();
}

void AdminRaids::select(const Json& j) {
    const raids::Event e = raids::parseEvent(j);
    if (!e.valid) return;
    selected_ = e.id;
    formatIdx_ = e.format == "court" ? 0 : e.format == "legendaire" ? 2 : 1;
    lockIdx_ = 2;
    for (int i = 0; i < 5; ++i)
        if (kLocks[i] == e.lockMinutes) lockIdx_ = i;
    difficultyIdx_ = std::clamp(e.difficulty - 1, 0, 4);
    open_ = e.registrationOpen;
    capacity_ = static_cast<float>(e.capacity);
    clanCapacity_ = static_cast<float>(e.clanCapacity);
    rewards_ = jstr(j, "rewards_text");
    const raids::Civil loc = raids::toSystemLocal(e.startsAt);
    date_ = raids::dateField(loc);
    clock_ = raids::hhmm(loc);
    saved_ = formJson(e.startsAt);
    // The stored lock time when the list does not offer it: kept as it is.
    if (kLocks[lockIdx_] != e.lockMinutes) saved_["lock_minutes"] = kLocks[lockIdx_];
    loadDetail();
}

Json AdminRaids::formJson(int64_t utc) const {
    Json b = {{"format", kFormats[formatIdx_]},
              {"lock_minutes", kLocks[lockIdx_]},
              {"registration_open", open_},
              {"capacity", static_cast<int>(std::lround(capacity_))},
              {"clan_capacity", static_cast<int>(std::lround(clanCapacity_))},
              {"difficulty", difficultyIdx_ + 1},
              {"rewards_text", clans::cleanName(rewards_)}};
    if (utc > 0) b["starts_at"] = raids::isoUtc(utc);
    return b;
}

void AdminRaids::save(const Json& body) {
    busy_ = true;
    const bool creating = selected_.empty();
    const std::string path = creating ? "/api/v1/admin/raids" : "/api/v1/admin/raids/" + selected_;
    g_.online.send(creating ? "POST" : "PATCH", path, body, OnlineService::guard(life_, [this, creating](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) return say_(r.message, true);
                       say_(creating ? "Brouillon créé : publiez-le pour prévenir tous les clans (inscrit au Journal)." : "Événement enregistré (inscrit au Journal).",
                            false);
                       select(at(r.data, "event"));
                       load();
                   }));
}

void AdminRaids::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    ui.pushId("raids");
    const float formW = std::min(ui.s(600), in.w * 0.44f);
    drawForm({in.x, in.y, formW, in.h});
    Rect right{in.x + formW + ui.s(18), in.y, in.w - formW - ui.s(18), in.h};
    // 2.67: « Tester Asterion » at the bottom of the right column (administrators only).
    if (g_.online.isAdmin() && !raid::Branch::all().empty()) {
        const float testH = ui.s(226);
        drawTest({right.x, right.bottom() - testH, right.w, testH});
        right.h -= testH + ui.s(14);
    }
    const float listH = std::max(ui.s(160), right.h * 0.46f);
    drawList({right.x, right.y, right.w, listH});
    drawDetail({right.x, right.y + listH + ui.s(14), right.w, right.h - listH - ui.s(14)});
    ui.popId();
}

void AdminRaids::drawTest(const Rect& r) {
    // 2.67: a branch of the catalog (raid::Branch::all: a branch added later appears by itself), one
    // of its zones, then « Lancer le test »: solo, local, never reported.
    UIContext& ui = g_.ui;
    ui.pushId("test");
    const float top = section(ui, r, "TESTER ASTERION · ADMINISTRATEURS", kGold);
    const auto& branches = raid::Branch::all();
    testBranch_ = std::clamp(testBranch_, 0, static_cast<int>(branches.size()) - 1);
    const raid::Branch& b = *branches[static_cast<size_t>(testBranch_)];
    std::vector<std::string> branchNames;
    for (const raid::Branch* x : branches) branchNames.push_back(x->name);
    std::vector<std::string> zoneNames;
    int bossZone = 0;
    for (const raid::ZoneInfo& z : b.zones) {
        zoneNames.push_back(std::to_string(z.id) + " · " + z.name + (z.boss ? " (" + b.gigaboss + ")" : ""));
        if (z.boss && bossZone == 0) bossZone = static_cast<int>(zoneNames.size()) - 1;
    }
    if (testZone_ < 0 || testZone_ >= static_cast<int>(zoneNames.size())) testZone_ = bossZone;  // the gigaboss's fight by default
    const raid::ZoneRules* rules = b.zoneRules(b.zones[static_cast<size_t>(testZone_)].id);
    const bool giga = rules && rules->gigaboss.present;
    const float x = r.x + ui.s(14), w = r.w - ui.s(28), gap = ui.s(10), rowH = ui.s(44);
    const float half = (w - gap) * 0.5f;
    float y = top;
    const int prevBranch = testBranch_;
    ui.cycle("branch", {x, y, half, rowH}, "Branche", testBranch_, branchNames);
    if (testBranch_ != prevBranch) testZone_ = -1;
    ui.cycle("zone", {x + half + gap, y, half, rowH}, "Zone", testZone_, zoneNames);
    y += rowH + gap;
    static const std::vector<std::string> kPlayers = {"2", "3", "4", "5", "6", "7", "8"};
    static const std::vector<std::string> kPhases = {"1", "2", "3"};
    ui.cycle("players", {x, y, half, rowH}, "Joueurs simulés", testPlayers_, kPlayers);
    const bool ro = ui.readOnly;
    ui.readOnly = !giga;
    ui.cycle("phase", {x + half + gap, y, half, rowH}, giga ? "Phase de départ" : "Phase (sans gigaboss)", testPhase_, kPhases);
    ui.readOnly = ro;
    y += rowH + gap;
    bool inv = g_.settings.gameplay.adminInvincible;
    if (ui.toggle("invincible", {x, y, half, rowH}, "Invincible (admin) · non classé", inv)) {
        g_.settings.gameplay.adminInvincible = inv;
        g_.saveSettings();
    }
    UIContext::ButtonStyle bs;
    bs.primary = true;
    if (ui.button("launch", {x + half + gap, y, half, rowH}, "Lancer le test", bs)) {
        RaidTestSetup t;
        t.branch = b.id;
        t.zone = b.zones[static_cast<size_t>(testZone_)].id;
        t.players = testPlayers_ + 2;
        t.startPhase = giga ? testPhase_ + 1 : 1;
        t.invincible = g_.settings.gameplay.adminInvincible;
        g_.app.pushScene(makeRaidTest(g_, t));
    }
    y += rowH + ui.s(6);
    drawLines(ui, wrap(ui, "En local et seul : aucun rapport, ni consensus, ni état de branche, ni classement, ni contribution, ni récompense.", FontFace::Body, 13, w,
                       std::max(1, static_cast<int>((r.bottom() - ui.s(8) - y) / ui.s(18)))),
              {x, y}, 13, ui.s(18), kDim);
    ui.popId();
}

void AdminRaids::drawForm(const Rect& r) {
    UIContext& ui = g_.ui;
    raids::Event cur;
    for (const Json& e : events_)
        if (jstr(e, "id") == selected_) cur = raids::parseEvent(e);
    const bool editing = !selected_.empty() && cur.valid;
    const int64_t now = nowUtc(skew_);
    const raids::State st = editing ? raids::stateAt(cur, now) : raids::State::Draft;
    const std::string title = !editing ? "PROGRAMMER UN ÉVÉNEMENT MONDIAL" : !cur.published ? "BROUILLON · ÉVÉNEMENT MONDIAL" : "ÉVÉNEMENT MONDIAL · " + raids::stateLabel(st);
    const float top = section(ui, r, title, kPeach);
    const float x = r.x + ui.s(16), w = r.w - ui.s(32), right = x + w;
    const float labelW = std::min(ui.s(200), w * 0.38f), fx = x + labelW + ui.s(10), fw = right - fx;
    const float rowH = ui.s(56);
    const float buttonsH = ui.s(116), previewH = ui.s(104);
    const Rect fields{x, top, w + ui.s(10), r.bottom() - ui.s(14) - buttonsH - previewH - top};
    const float contentH = 9 * rowH;
    const bool locked = editing && (st == raids::State::Running || st == raids::State::Finished || st == raids::State::Cancelled);
    ui.beginScroll("fields", fields, contentH);
    float y = fields.y - ui.scrollOffset();
    auto label = [&](const std::string& t) { fit(ui, t, {x, y + rowH * 0.5f}, 15, labelW, kDim); };
    const bool ro = ui.readOnly;
    ui.readOnly = locked;
    label("Raid");
    fit(ui, "Asterion, le Dévoreur de mondes", {fx, y + rowH * 0.5f}, 16, fw, kText, FontFace::Bold);
    y += rowH;
    label("Format");
    {
        static const char* kNames[] = {"Court", "Standard", "Légendaire"};
        const float bw = (fw - 2 * ui.s(8)) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            UIContext::ButtonStyle st2;
            st2.small = true;
            st2.selected = formatIdx_ == i;
            if (ui.button(std::string("format/") + kFormats[i], {fx + static_cast<float>(i) * (bw + ui.s(8)), y + ui.s(6), bw, ui.s(44)}, kNames[i], st2) && !locked)
                formatIdx_ = i;
        }
    }
    y += rowH;
    label("Début (votre heure)");
    {
        const float dw = fw * 0.58f;
        ui.textField("date", {fx, y + ui.s(6), dw, ui.s(44)}, date_, "21/11/2026", 10);
        ui.textField("clock", {fx + dw + ui.s(8), y + ui.s(6), fw - dw - ui.s(8), ui.s(44)}, clock_, "21:00", 5);
    }
    y += rowH;
    std::vector<std::string> locks;
    for (int m : kLocks) locks.push_back(std::to_string(m) + " min avant le début");
    ui.cycle("lock", {x, y + ui.s(4), w, ui.s(48)}, "Verrouillage des inscriptions", lockIdx_, locks);
    lockIdx_ = std::clamp(lockIdx_, 0, 4);
    y += rowH;
    ui.toggle("open", {x, y + ui.s(4), w, ui.s(48)}, "Inscriptions ouvertes", open_);
    y += rowH;
    label("Joueurs maximum (0 : illimité)");
    ui.numberField("capacity", {fx, y + ui.s(6), std::min(fw, ui.s(240)), ui.s(44)}, capacity_, 10.0f, 0.0f, 100000.0f, "%.0f");
    y += rowH;
    label("Joueurs par clan (1 à 50)");
    ui.numberField("clancap", {fx, y + ui.s(6), std::min(fw, ui.s(240)), ui.s(44)}, clanCapacity_, 1.0f, 1.0f, 50.0f, "%.0f");
    y += rowH;
    ui.cycle("difficulty", {x, y + ui.s(4), w, ui.s(48)}, "Difficulté", difficultyIdx_, {"1 / 5", "2 / 5", "3 / 5", "4 / 5", "5 / 5"});
    difficultyIdx_ = std::clamp(difficultyIdx_, 0, 4);
    y += rowH;
    label("Récompenses (texte)");
    ui.textField("rewards", {fx, y + ui.s(6), fw, ui.s(44)}, rewards_, "Vide : le texte d'Asterion", raids::kMaxRewardsText);
    ui.readOnly = ro;
    ui.endScroll();

    // The start: read in this computer's zone, sent in UTC, previewed in several zones.
    std::string problem;
    int64_t utc = 0;
    int yy = 0, mo = 0, dd = 0, hh = 0, mi = 0;
    if (!raids::parseDate(date_, yy, mo, dd)) problem = "Date attendue : JJ/MM/AAAA.";
    else if (!raids::parseClock(clock_, hh, mi)) problem = "Heure attendue : HH:MM.";
    else {
        raids::Civil wall;
        wall.year = yy;
        wall.month = mo;
        wall.day = dd;
        wall.hour = hh;
        wall.minute = mi;
        utc = raids::fromSystemLocal(wall);
    }
    const Json body = formJson(utc);
    Json changes = Json::object();
    if (editing && saved_.is_object())
        for (auto it = body.begin(); it != body.end(); ++it)
            if (!saved_.contains(it.key()) || saved_[it.key()] != it.value()) changes[it.key()] = it.value();
    const bool startChanged = !editing || changes.contains("starts_at");
    if (problem.empty() && startChanged) {
        const int lead = std::max(raids::kMinAheadMinutes, kLocks[lockIdx_] + 5);
        if (utc < now + 60LL * lead) problem = "Au moins " + std::to_string(lead) + " minutes à l'avance.";
        else if (utc > now + 365LL * 86400) problem = "Au plus un an à l'avance.";
    }
    const float py = fields.bottom() + ui.s(8);
    const Rect prev{x, py, w, previewH - ui.s(14)};
    ui.r().fillRoundedRect(prev, ui.s(10), Color{13, 18, 38, 255});
    ui.r().drawRoundedRect(prev, ui.s(10), kBorder, ui.s(1));
    std::string text;
    if (!problem.empty()) text = problem;
    else {
        std::string zones;
        for (const raids::Zone& z : raids::previewZones()) {
            const raids::Civil c = raids::toLocal(utc, z);
            zones += (zones.empty() ? "" : " · ") + z.name + " " + raids::weekdayShort(c.weekday) + " " + raids::hhmm(c);
        }
        text = "Enregistré en UTC : " + raids::isoUtc(utc) + ". Aperçu des heures locales : " + zones +
               ". Le changement d'heure est calculé par le jeu de chaque joueur.";
    }
    drawLines(ui, wrap(ui, text, FontFace::Body, 13, prev.w - ui.s(20), static_cast<int>((prev.h - ui.s(12)) / ui.s(18))), {prev.x + ui.s(10), prev.y + ui.s(6)}, 13,
              ui.s(18), problem.empty() ? kDim : ui.theme().danger);

    // The buttons: two rows, each measured to share the width.
    const float by1 = r.bottom() - ui.s(14) - ui.s(44) - ui.s(10) - ui.s(44), by2 = r.bottom() - ui.s(14) - ui.s(44);
    const float half = (w - ui.s(10)) * 0.5f;
    UIContext::ButtonStyle plain;
    plain.small = true;
    plain.enabled = !busy_;
    if (ui.button("new", {x, by1, half, ui.s(44)}, "Nouvel événement", plain)) clearForm();
    UIContext::ButtonStyle saveSt = plain;
    saveSt.primary = !editing;
    saveSt.enabled = !busy_ && problem.empty() && !locked && (!editing || !changes.empty());
    if (ui.button("save", {x + half + ui.s(10), by1, half, ui.s(44)}, editing ? "Enregistrer les changements" : "Créer le brouillon", saveSt))
        save(editing ? changes : body);
    if (editing && !cur.published) {
        UIContext::ButtonStyle pub = plain;
        pub.primary = true;
        pub.enabled = !busy_ && changes.empty() && st == raids::State::Draft && now < cur.lockAt;
        if (ui.button("publish", {x, by2, w, ui.s(44)}, changes.empty() ? "Publier et notifier tous les clans" : "Enregistrez avant de publier", pub)) {
            busy_ = true;
            g_.online.send("POST", "/api/v1/admin/raids/" + selected_ + "/publish", Json::object(), OnlineService::guard(life_, [this](const ApiResult& res) {
                               busy_ = false;
                               if (lostRole_(res)) return;
                               if (!res.ok) return say_(res.message, true);
                               const int n = jget<int>(res.data, "notified", 0);
                               say_("Événement publié : " + plural(n, "joueur prévenu", "joueurs prévenus") + " (inscrit au Journal).", false);
                               load();
                           }));
        }
    } else if (editing && !locked) {
        UIContext::ButtonStyle tog = plain;
        tog.enabled = !busy_ && changes.empty();
        if (ui.button("toggle", {x, by2, half, ui.s(44)}, cur.registrationOpen ? "Fermer les inscriptions" : "Ouvrir les inscriptions", tog))
            save({{"registration_open", !cur.registrationOpen}});
        UIContext::ButtonStyle cancel = plain;
        cancel.danger = true;
        if (ui.button("cancel", {x + half + ui.s(10), by2, half, ui.s(44)}, "Annuler l'événement…", cancel)) {
            reason_.clear();
            confirmError_.clear();
            confirm_ = true;
        }
    } else if (editing && !cur.cancelled && st != raids::State::Finished) {
        UIContext::ButtonStyle cancel = plain;
        cancel.danger = true;
        if (ui.button("cancel", {x, by2, w, ui.s(44)}, "Annuler l'événement…", cancel)) {
            reason_.clear();
            confirmError_.clear();
            confirm_ = true;
        }
    }
}

void AdminRaids::drawList(const Rect& r) {
    UIContext& ui = g_.ui;
    const float top = section(ui, r, "ÉVÉNEMENTS MONDIAUX", kSky);
    const Rect list{r.x + ui.s(14), top, r.w - ui.s(20), r.bottom() - ui.s(12) - top};
    if (events_.empty()) {
        fit(ui, loaded_ ? "Aucun événement mondial pour l'instant : programmez-en un à gauche." : "Chargement…", {list.x, list.y + ui.s(14)}, 15, list.w - ui.s(10), kDim);
        return;
    }
    const int64_t now = nowUtc(skew_);
    const float rowH = ui.s(52), gap = ui.s(6);
    ui.beginScroll("events", list, static_cast<float>(events_.size()) * (rowH + gap));
    float ry = list.y - ui.scrollOffset();
    for (const Json& j : events_) {
        const raids::Event e = raids::parseEvent(j);
        const Rect row{list.x, ry, list.w - ui.s(14), rowH};
        ry += rowH + gap;
        if (!e.valid || row.bottom() < list.y || row.y > list.bottom()) continue;
        bool hover = false;
        if (ui.hotspot("ev/" + e.id, row, &hover)) select(j);
        const bool sel = e.id == selected_;
        ui.r().fillRoundedRect(row, ui.s(9), sel ? Color{58, 28, 34, 245} : hover ? Color{34, 44, 92, 245} : kCard);
        if (sel) ui.r().drawRoundedRect(row, ui.s(9), kOrange, ui.s(1.5f));
        const raids::State st = raids::stateAt(e, now);
        const std::string sl = raids::stateLabel(st);
        const float pw = textW(ui, sl, 13, FontFace::Bold) + ui.s(22);
        pill(ui, row.right() - ui.s(10) - pw, row.center().y, sl, stateFill(st), stateText(st), row.right(), 13);
        const std::string count = plural(e.clans, "clan", "clans") + " · " + plural(e.players, "joueur", "joueurs");
        const float cw = std::min(textW(ui, count, 13), (row.w - pw) * 0.35f);
        fit(ui, count, {row.right() - ui.s(22) - pw, row.center().y}, 13, cw, kDim, FontFace::Body, Align::Right);
        fit(ui, whenShort(e.startsAt) + " · " + raids::formatLabel(e.format), {row.x + ui.s(12), row.center().y}, 15,
            row.w - ui.s(46) - pw - cw, kText, FontFace::Bold);
    }
    ui.endScroll();
}

void AdminRaids::drawDetail(const Rect& r) {
    UIContext& ui = g_.ui;
    raids::Event e = raids::parseEvent(at(detail_, "event"));
    const std::string title = e.valid && e.id == selected_ ? "INSCRIPTIONS · " + whenShort(e.startsAt) : "INSCRIPTIONS";
    const float top = section(ui, r, title, kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    const auto note = wrap(ui, "Seules les inscriptions existent dans cette version : les instances, les boss et les rapports des joueurs arriveront avec les combats.",
                           FontFace::Body, 13, w, 3);
    const float noteH = static_cast<float>(note.size()) * ui.s(18);
    drawLines(ui, note, {x, r.bottom() - ui.s(12) - noteH}, 13, ui.s(18), kDim);
    if (!e.valid || e.id != selected_) {
        fit(ui, "Choisissez un événement pour voir ses inscriptions.", {x, top + ui.s(14)}, 15, w, kDim);
        return;
    }
    // Three figures, then the clans.
    const float tw = (w - 2 * ui.s(10)) / 3.0f, th = ui.s(70);
    const std::string vals[3] = {std::to_string(e.clans), std::to_string(e.players), e.capacity > 0 ? std::to_string(e.capacity) : std::string("illimitées")};
    const char* caps[3] = {"Clans inscrits", "Joueurs inscrits", "Places"};
    for (int i = 0; i < 3; ++i) {
        const Rect t{x + static_cast<float>(i) * (tw + ui.s(10)), top, tw, th};
        ui.r().fillRoundedRect(t, ui.s(10), Color{13, 18, 38, 255});
        fit(ui, caps[i], {t.x + ui.s(10), t.y + ui.s(18)}, 12, t.w - ui.s(20), kDim);
        fit(ui, vals[i], {t.x + ui.s(10), t.y + ui.s(46)}, 20, t.w - ui.s(20), kText, FontFace::Title);
    }
    const Rect list{x, top + th + ui.s(12), w + ui.s(6), r.bottom() - ui.s(18) - noteH - (top + th + ui.s(12))};
    const Json& cl = at(detail_, "clans");
    if (!cl.is_array() || cl.empty()) {
        fit(ui, "Aucun clan inscrit pour l'instant.", {x, list.y + ui.s(14)}, 15, w, kDim);
        return;
    }
    const float rowH = ui.s(30);
    ui.beginScroll("clans", list, static_cast<float>(cl.size()) * rowH);
    float ry = list.y - ui.scrollOffset();
    for (const Json& c : cl) {
        const float cy = ry + rowH * 0.5f;
        ry += rowH;
        if (cy < list.y || cy > list.bottom()) continue;
        const std::string n = plural(jget<int>(c, "players", 0), "inscrit", "inscrits");
        const float nw = fit(ui, n, {x + w - ui.s(8), cy}, 14, w * 0.25f, kGold, FontFace::Bold, Align::Right).x;
        const std::string leader = jstr(c, "leader");
        const float lw = leader.empty() ? 0.0f : std::min(textW(ui, "chef : " + leader, 13), w * 0.3f);
        if (!leader.empty()) fit(ui, "chef : " + leader, {x + w - ui.s(20) - nw, cy}, 13, lw, kDim, FontFace::Body, Align::Right);
        fit(ui, clans::tagged(jstr(c, "name"), jstr(c, "tag")), {x, cy}, 14, w - nw - lw - ui.s(36), kText, FontFace::Body, Align::Left, true);
    }
    ui.endScroll();
}

void AdminRaids::drawModal() {
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 175});
    const float w = std::min(ui.screenW() - ui.s(40), ui.s(680)), tw = w - ui.s(60);
    const auto intro = wrap(ui, "Annuler cet événement mondial ? Les clans et les joueurs inscrits sont prévenus ; le Journal garde la trace.", FontFace::Body, 16, tw, 4);
    const float introH = static_cast<float>(intro.size()) * ui.s(22);
    const float h = ui.s(80) + introH + ui.s(16) + ui.s(76) + ui.s(34) + ui.s(76);
    const Rect box{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), ui.theme().danger, ui.s(1.5f));
    ui.pushId("admin/raids/modal");
    const float x = box.x + ui.s(30);
    fit(ui, "Annuler l'événement", {x, box.y + ui.s(40)}, 26, tw, kText, FontFace::Title);
    drawLines(ui, intro, {x, box.y + ui.s(70)}, 16, ui.s(22), ui.theme().textDim);
    float y = box.y + ui.s(80) + introH + ui.s(16);
    fit(ui, "Motif (facultatif, gardé au Journal)", {x, y + ui.s(10)}, 15, tw, ui.theme().textDim);
    ui.textField("reason", {x, y + ui.s(24), tw, ui.s(44)}, reason_, "Mise à jour du jeu, erreur d'horaire…", 200);
    y += ui.s(76);
    fit(ui, confirmError_, {x, y + ui.s(12)}, 15, tw, ui.theme().danger);
    const float by = box.bottom() - ui.s(66);
    UIContext::ButtonStyle ok;
    ok.danger = true;
    ok.enabled = !busy_;
    const std::string okLabel = "Annuler l'événement";
    const float okW = textW(ui, okLabel, 17, FontFace::Bold) + ui.s(44);
    const float cW = textW(ui, "Garder", 17, FontFace::Bold) + ui.s(44);
    const bool go = ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, okLabel, ok);
    const bool keep = ui.button("keep", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, "Garder") || (ui.backPressed() && !ui.editingText());
    ui.popId();
    if (keep) {
        confirm_ = false;
        return;
    }
    if (!go || busy_) return;
    busy_ = true;
    Json body = Json::object();
    if (!clans::cleanName(reason_).empty()) body["reason"] = clans::cleanName(reason_);
    g_.online.send("DELETE", "/api/v1/admin/raids/" + selected_, body, OnlineService::guard(life_, [this](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           confirmError_ = r.message;
                           return;
                       }
                       confirm_ = false;
                       say_("Événement annulé : les inscrits sont prévenus (inscrit au Journal).", false);
                       load();
                   }));
}

}  // namespace cosmic
