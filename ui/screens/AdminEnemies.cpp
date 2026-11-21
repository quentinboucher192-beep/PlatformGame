#include "ui/widgets/CosmicFrame.h"
#include "ui/screens/AdminEnemies.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <vector>

#include "game/GameServices.h"
#include "game/online/OnlineSession.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

/// "16/11/2026 · 02:14" (local time of this computer).
std::string stamp(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return "—";
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d · %02d:%02d", m.tm_mday, m.tm_mon + 1, m.tm_year + 1900, m.tm_hour, m.tm_min);
    return b;
}

std::string who(const Json& by) { return by.is_object() ? jstr(by, "username", "un compte supprimé") : std::string("un admin"); }

}  // namespace

AdminEnemies::AdminEnemies(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)) {}

void AdminEnemies::enter(bool force) {
    if (!loaded_ || force) load();
    if (!selected_.empty() && (force || !history_.is_object() || jstr(history_, "enemy_id") != selected_)) loadHistory(selected_);
}

void AdminEnemies::load() {
    busy_ = true;
    g_.online.get("/api/v1/admin/enemies", OnlineService::guard(life_, [this](const ApiResult& r) {
                      busy_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      enemies_ = r.data.value("enemies", Json::array());
                      loaded_ = true;
                  }),
                  false);
}

void AdminEnemies::loadHistory(const std::string& id) {
    selected_ = id;
    int gen = ++historyGen_;
    historyBusy_ = true;
    g_.online.get("/api/v1/admin/enemies/" + id + "/revisions", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != historyGen_) return;
                      historyBusy_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      history_ = r.data;
                  }),
                  false);
}

const Json* AdminEnemies::serverRow(const std::string& id) const {
    if (!enemies_.is_array()) return nullptr;
    for (const Json& e : enemies_)
        if (jstr(e, "enemy_id") == id) return &e;
    return nullptr;
}

void AdminEnemies::edit(const std::string& id) {
    if (!loaded_) return say_("La liste du serveur n'est pas encore lue : actualisez.", true);
    const Json* row = serverRow(id);
    const int cur = row ? jget<int>(jfield(*row, "current", Json()), "revision", 0) : 0;
    // The editor starts from the definition online: the game's own, or the revision downloaded.
    if (cur == 0 || g_.content.enemyRevision(id, cur)) return g_.app.pushScene(makeEnemyEditorAdmin(g_, id, cur));
    opening_ = true;
    say_("Téléchargement de la définition en ligne…", false);
    std::weak_ptr<int> life = life_;
    g_.refreshCampaignRevisions([this, life, id, cur](const std::string& err) {
        if (!life.lock()) return;
        opening_ = false;
        if (g_.content.enemyRevision(id, cur)) return g_.app.pushScene(makeEnemyEditorAdmin(g_, id, cur));
        say_(err.empty() ? std::string("La révision en ligne de cette entité ne se lit pas avec cette version du jeu : mettez le jeu à jour.") : err, true);
    });
}

// ------------------------------------------------------------------ drawing

void AdminEnemies::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    ui.pushId("enemies");
    float y = in.y, bh = ui.s(46);
    UIContext::ButtonStyle rs;
    rs.small = true;
    rs.enabled = !busy_;
    float rw = ui.text().measure("Actualiser", FontFace::Bold, ui.s(17)).x + ui.s(44);
    if (cframe::button(ui, "refresh", {in.right() - rw, y, rw, bh}, "Actualiser", rs)) {
        load();
        if (!selected_.empty()) loadHistory(selected_);
    }
    {
        const std::string info = "Les entités natives de la campagne. Une révision publiée ici est téléchargée par les jeux, avec une nouvelle révision "
                                 "de chaque niveau qui l'utilise : le choix des records se fait niveau par niveau.";
        float room = in.w - rw - ui.s(24);
        float th = ui.text().measureWrapped(info, FontFace::Body, ui.s(15), room).y;
        if (th <= bh + ui.s(4)) ui.labelWrapped(info, {in.x, y + (bh - th) * 0.5f}, room, 15, ui.theme().textDim);
        else {
            float fs = ui.fitFont(info, FontFace::Body, 15, room);
            ui.label(ui.elide(info, FontFace::Body, fs, room), {in.x, y + bh * 0.5f}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
    }
    y += bh + ui.s(14);
    float gap = ui.s(20);
    float lw = std::floor((in.w - gap) * 0.56f);
    drawList({in.x, y, lw, in.bottom() - y});
    drawHistory({in.x + lw + gap, y, in.w - lw - gap, in.bottom() - y});
    ui.popId();
}

void AdminEnemies::drawList(const Rect& r) {
    UIContext& ui = g_.ui;
    std::vector<const EnemyDefinition*> natives;
    for (const EnemyDefinition* d : g_.content.enemies())
        if (d->builtin && !d->tutorial && d->raid.empty()) natives.push_back(d);  // 2.64: the tutorial's creatures stay as they are; 2.68: a raid's too
    if (natives.empty()) {
        ui.label("Aucune entité native dans ce jeu.", {r.center().x, r.y + ui.s(40)}, 18, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        return;
    }
    const float rowH = ui.s(76), gap = ui.s(6);
    ui.beginScroll("list", r, static_cast<float>(natives.size()) * (rowH + gap));
    float yy = r.y - ui.scrollOffset();
    const float w = r.w - ui.s(16);
    const float editW = ui.text().measure("Modifier", FontFace::Bold, ui.s(16)).x + ui.s(36);
    const float histW = ui.text().measure("Historique", FontFace::Bold, ui.s(16)).x + ui.s(36);
    for (const EnemyDefinition* d : natives) {
        Rect row{r.x, yy, w, rowH};
        yy += rowH + gap;
        const std::string id = d->id;
        const bool sel = id == selected_;
        // Off screen: only its buttons are registered (keyboard / gamepad reach every row).
        float bh = ui.s(40), by = row.center().y - bh * 0.5f;
        Rect hb{row.right() - ui.s(10) - histW, by, histW, bh};
        Rect eb{hb.x - ui.s(8) - editW, by, editW, bh};
        const bool visible = !(row.bottom() < r.y || row.y > r.bottom());
        if (visible) {
            ui.r().fillRoundedRect(row, ui.s(8), sel ? Color{26, 38, 72, 240} : Color{18, 26, 50, 235});
            if (sel) ui.r().drawRoundedRect(row, ui.s(8), ui.theme().accent.withAlphaF(0.7f), ui.s(1));
        }
        UIContext::ButtonStyle es;
        es.small = true;
        es.primary = true;
        es.fontSize = 16;
        es.enabled = !opening_ && loaded_;
        if (cframe::button(ui, "edit" + id, eb, "Modifier", es)) edit(id);
        UIContext::ButtonStyle hs;
        hs.small = true;
        hs.fontSize = 16;
        hs.selected = sel;
        if (cframe::button(ui, "hist" + id, hb, "Historique", hs)) loadHistory(id);
        if (!visible) continue;
        const float tx = row.x + ui.s(14), room = eb.x - ui.s(14) - tx;
        const Json* srv = serverRow(id);
        const Json& cur = srv ? jfield(*srv, "current", Json()) : Json();
        const int rev = jget<int>(cur, "revision", 0);
        // Name and the revision pill.
        std::string pill = rev > 0 ? "Révision " + std::to_string(rev) : std::string("Définition du jeu");
        float pw = ui.text().measure(pill, FontFace::Bold, ui.s(13)).x + ui.s(18);
        float nameRoom = std::max(ui.s(40), room - pw - ui.s(10));
        std::string shown = ui.elide(d->name, FontFace::Bold, 18, nameRoom);
        float nw = ui.text().measure(shown, FontFace::Bold, ui.s(18)).x;
        float ly = row.y + ui.s(24);
        ui.label(shown, {tx, ly}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        if (nw + ui.s(10) + pw <= room) {
            Rect pr{tx + nw + ui.s(10), ly - ui.s(11), pw, ui.s(22)};
            Color pc = rev > 0 ? ui.theme().warm : Color{150, 170, 200, 255};
            ui.r().fillRoundedRect(pr, ui.s(11), pc.withAlphaF(0.15f));
            ui.r().drawRoundedRect(pr, ui.s(11), pc, ui.s(1));
            ui.label(pill, pr.center(), 13, pc, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        std::string src = id + " · " + (d->isBoss ? std::string("Boss") : std::string(familyLabel(d->family)));
        if (rev > 0) src += " · publiée par " + who(jfield(cur, "published_by", Json())) + " le " + stamp(jstr(cur, "published_at"));
        float fs = ui.fitFont(src, FontFace::Body, 15, room, 0.8f);
        ui.label(ui.elide(src, FontFace::Body, fs, room), {tx, row.y + ui.s(54)}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    ui.endScroll();
}

void AdminEnemies::drawHistory(const Rect& r) {
    UIContext& ui = g_.ui;
    cframe::panel(ui, r, false, 12);
    Rect in{r.x + ui.s(16), r.y + ui.s(14), r.w - ui.s(32), r.h - ui.s(28)};
    const EnemyDefinition* d = selected_.empty() ? nullptr : g_.content.enemy(selected_);
    if (!d) {
        ui.labelWrapped("Choisissez « Historique » sur une entité pour voir ses révisions publiées et les niveaux qui les ont reçues.",
                        {in.x, in.y + ui.s(8)}, in.w, 17, ui.theme().textDim);
        return;
    }
    std::string title = "Historique · " + d->name;
    float ts = ui.fitFont(title, FontFace::Bold, 21, in.w);
    ui.label(ui.elide(title, FontFace::Bold, ts, in.w), {in.x, in.y + ui.s(16)}, ts, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
    float y = in.y + ui.s(40);
    const bool fresh = history_.is_object() && jstr(history_, "enemy_id") == selected_;
    if (!fresh) {
        ui.label(historyBusy_ ? "Chargement…" : "Historique indisponible.", {in.x, y + ui.s(16)}, 17, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        return;
    }
    const Json& revs = jfield(history_, "revisions", Json::array());
    if (revs.empty()) {
        ui.labelWrapped("Aucune révision publiée : les jeux jouent la définition livrée avec eux.", {in.x, y + ui.s(4)}, in.w, 16, ui.theme().textDim);
        return;
    }
    const float rowH = ui.s(92), gap = ui.s(6);
    Rect list{in.x, y, in.w, in.bottom() - y};
    ui.beginScroll("history", list, static_cast<float>(revs.size()) * (rowH + gap));
    float yy = list.y - ui.scrollOffset();
    const float w = list.w - ui.s(16);
    for (const Json& rev : revs) {
        Rect row{list.x, yy, w, rowH};
        yy += rowH + gap;
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        const bool current = jget<bool>(rev, "is_current", false);
        ui.r().fillRoundedRect(row, ui.s(8), current ? Color{30, 26, 22, 235} : Color{18, 26, 50, 235});
        const float tx = row.x + ui.s(12), room = row.w - ui.s(24);
        std::string head = "Révision " + std::to_string(jget<int>(rev, "revision", 0));
        float hw = ui.text().measure(head, FontFace::Bold, ui.s(18)).x;
        ui.label(ui.elide(head, FontFace::Bold, 18, room), {tx, row.y + ui.s(20)}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string pill = current ? "en ligne" : "archivée";
        float pw = ui.text().measure(pill, FontFace::Bold, ui.s(13)).x + ui.s(18);
        if (hw + ui.s(10) + pw <= room) {
            Color pc = current ? ui.theme().success : Color{150, 170, 200, 255};
            Rect pr{tx + hw + ui.s(10), row.y + ui.s(9), pw, ui.s(22)};
            ui.r().fillRoundedRect(pr, ui.s(11), pc.withAlphaF(0.15f));
            ui.r().drawRoundedRect(pr, ui.s(11), pc, ui.s(1));
            ui.label(pill, pr.center(), 13, pc, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        std::string src = "Publiée par " + who(jfield(rev, "published_by", Json())) + " le " + stamp(jstr(rev, "published_at"));
        float fs = ui.fitFont(src, FontFace::Body, 15, room, 0.8f);
        ui.label(ui.elide(src, FontFace::Body, fs, room), {tx, row.y + ui.s(46)}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        const Json& levels = jfield(rev, "levels", Json::array());
        int resets = 0, keeps = 0;
        for (const Json& l : levels) (jstr(l, "records") == "keep" ? keeps : resets)++;
        const int n = static_cast<int>(levels.size());
        std::string facts = n == 0 ? std::string("aucun niveau de la campagne")
                                   : std::to_string(n) + (n > 1 ? " niveaux republiés" : " niveau republié") + " · records réinitialisés : " +
                                         std::to_string(resets) + " · gardés : " + std::to_string(keeps);
        float fs2 = ui.fitFont(facts, FontFace::Body, 14, room, 0.8f);
        ui.label(ui.elide(facts, FontFace::Body, fs2, room), {tx, row.y + ui.s(70)}, fs2, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    ui.endScroll();
}

}  // namespace cosmic
