#include "ui/widgets/CosmicFrame.h"
#include "ui/screens/AdminClans.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <vector>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/online/Clans.h"
#include "game/online/OnlineSession.h"
#include "ui/online/ClanWidgets.h"

namespace cosmic {

namespace {

/// "16/11/2026" (local date of this computer).
std::string day(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return "—";
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[48];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d", m.tm_mday, m.tm_mon + 1, m.tm_year + 1900);
    return b;
}

/// `t` fitted into `room` (smaller down to 75 %, then shortened); a player's words are never translated.
void cell(UIContext& ui, const std::string& t, Vec2 pos, float size, float room, Color c, FontFace face = FontFace::Body, bool verbatim = false,
          Align a = Align::Left) {
    if (t.empty() || room <= ui.s(10)) return;
    i18n::Verbatim v(verbatim);
    const float f = ui.fitFont(t, face, size, room, 0.75f);
    ui.label(ui.elide(t, face, f, room), pos, f, c, a, face, VAlign::Middle);
}

}  // namespace

AdminClans::AdminClans(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)) {}

void AdminClans::enter(bool force) {
    if (!loaded_ || force) load(false);
}

void AdminClans::load(bool more) {
    const int offset = more ? static_cast<int>(clans_.size()) : 0;
    std::string path = "/api/v1/admin/clans?limit=50&offset=" + std::to_string(offset);
    const std::string q = clans::cleanName(query_);
    if (!q.empty()) path += "&q=" + online::encodeQueryValue(q);
    int gen = ++gen_;
    busy_ = true;
    g_.online.get(path, OnlineService::guard(life_, [this, gen, more](const ApiResult& r) {
                      if (gen != gen_) return;
                      busy_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      if (!more) clans_ = Json::array();
                      for (const auto& c : r.data.value("clans", Json::array())) clans_.push_back(c);
                      total_ = jget<int>(r.data, "total", 0);
                      more_ = jget<bool>(r.data, "has_more", false);
                      loaded_ = true;
                  }),
                  false);
}

void AdminClans::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    ui.pushId("clans");
    const float fh = ui.s(46);
    const std::string before = query_;
    const float fieldW = std::min(in.w * 0.45f, ui.s(520));
    ui.textField("q", {in.x, in.y, fieldW, fh}, query_, "Tag ou nom d'un clan", 64);
    if (query_ != before) {
        queryDirty_ = true;
        queryT_ = ui.time();
    }
    if (queryDirty_ && ui.time() - queryT_ > 0.35f) {
        queryDirty_ = false;
        load(false);
    }
    const std::string count = std::to_string(total_) + (total_ > 1 ? " clans sur le serveur" : " clan sur le serveur");
    cell(ui, count, {in.right(), in.y + fh * 0.5f}, 16, in.right() - (in.x + fieldW + ui.s(24)), ui.theme().textDim, FontFace::Body, false, Align::Right);
    float y = in.y + fh + ui.s(16);
    // Column titles (the same shares as the rows).
    const float x0 = in.x + ui.s(12);
    const float w = in.w - ui.s(40);
    const float c1 = x0, c2 = x0 + w * 0.36f, c3 = x0 + w * 0.53f, c4 = x0 + w * 0.62f, c5 = x0 + w * 0.72f;
    cell(ui, "Clan", {c1, y + ui.s(12)}, 14, c2 - c1 - ui.s(8), ui.theme().textDim, FontFace::Bold);
    cell(ui, "Chef", {c2, y + ui.s(12)}, 14, c3 - c2 - ui.s(8), ui.theme().textDim, FontFace::Bold);
    cell(ui, "Membres", {c3, y + ui.s(12)}, 14, c4 - c3 - ui.s(8), ui.theme().textDim, FontFace::Bold);
    cell(ui, "Points", {c4, y + ui.s(12)}, 14, c5 - c4 - ui.s(8), ui.theme().textDim, FontFace::Bold);
    cell(ui, "Fondé le", {c5, y + ui.s(12)}, 14, ui.s(130), ui.theme().textDim, FontFace::Bold);
    y += ui.s(30);
    const float moreH = more_ ? ui.s(56) : 0.0f;
    Rect list{in.x, y, in.w, in.bottom() - y - moreH};
    if (clans_.empty()) {
        ui.label(loaded_ ? (query_.empty() ? "Aucun clan pour l'instant." : "Aucun clan ne correspond.") : "Chargement…", {list.center().x, list.y + ui.s(40)}, 18,
                 ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
    } else {
        const float rowH = ui.s(58), gap = ui.s(6);
        ui.beginScroll("list", list, static_cast<float>(clans_.size()) * (rowH + gap));
        float ry = list.y - ui.scrollOffset();
        for (const auto& c : clans_) {
            Rect row{list.x, ry, list.w - ui.s(16), rowH};
            ry += rowH + gap;
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            drawRow(c, row);
        }
        ui.endScroll();
    }
    if (more_) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = !busy_;
        if (cframe::button(ui, "more", {in.right() - ui.s(220), in.bottom() - ui.s(46), ui.s(220), ui.s(44)}, "Plus de clans", st)) load(true);
    }
    ui.popId();
}

void AdminClans::drawRow(const Json& c, const Rect& row) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(row, ui.s(8), Color{18, 26, 50, 235});
    const std::string id = jstr(c, "id");
    // The actions at the right first; the columns stop before them.
    UIContext::ButtonStyle st;
    st.small = true;
    float bx = row.right() - ui.s(10);
    auto btn = [&](const std::string& key, const std::string& label, bool danger) {
        UIContext::ButtonStyle s2 = st;
        s2.danger = danger;
        const float bw = ui.text().measure(label, FontFace::Bold, ui.s(15)).x + ui.s(30);
        bx -= bw;
        bool clicked = cframe::button(ui, key + id, {bx, row.y + ui.s(11), bw, ui.s(36)}, label, s2);
        bx -= ui.s(8);
        return clicked;
    };
    if (btn("dissolve", "Dissoudre…", true)) {
        target_ = c;
        reason_.clear();
        error_.clear();
        mode_ = Mode::Dissolve;
    }
    if (btn("rename", "Renommer…", false)) {
        target_ = c;
        name_ = jstr(c, "name");
        tag_ = jstr(c, "tag");
        reason_.clear();
        error_.clear();
        mode_ = Mode::Rename;
    }
    const float x0 = row.x + ui.s(12);
    const float w = row.w + ui.s(16) - ui.s(40);  // the same shares as the column titles
    const float c2 = x0 + w * 0.36f, c3 = x0 + w * 0.53f, c4 = x0 + w * 0.62f, c5 = x0 + w * 0.72f;
    const float limit = bx - ui.s(8);
    const float cy = row.center().y;
    drawClanEmblem(ui, {x0, row.y + ui.s(9), ui.s(40), ui.s(40)}, clans::emblemFromJson(c.value("emblem", Json())));
    const float tx = x0 + ui.s(52);
    cell(ui, clans::tagged(jstr(c, "name"), jstr(c, "tag")), {tx, cy}, 16, std::min(c2, limit) - tx - ui.s(8), ui.theme().text, FontFace::Bold, true);
    const Json& leader = c.contains("leader") ? c["leader"] : Json();
    cell(ui, leader.is_object() ? jstr(leader, "username") : std::string("—"), {c2, cy}, 15, std::min(c3, limit) - c2 - ui.s(8), ui.theme().text,
         FontFace::Body, leader.is_object());
    cell(ui, std::to_string(jget<int>(c, "members", 0)) + " / " + std::to_string(jget<int>(c, "max_members", clans::kMaxMembers)), {c3, cy}, 15,
         std::min(c4, limit) - c3 - ui.s(8), ui.theme().text);
    cell(ui, clans::pointsLabel(jget<double>(c, "points", 0.0)), {c4, cy}, 15, std::min(c5, limit) - c4 - ui.s(8), ui.theme().text);
    cell(ui, day(jstr(c, "created_at")), {c5, cy}, 15, limit - c5, ui.theme().textDim);
}

void AdminClans::drawModal() {
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 175});
    const bool rename = mode_ == Mode::Rename;
    const std::string who = clans::tagged(jstr(target_, "name"), jstr(target_, "tag"));
    const float w = std::min(ui.screenW() - ui.s(40), ui.s(680)), tw = w - ui.s(60);
    const std::string intro = rename ? "Clan : " + who + ". Le nouveau nom et le nouveau tag remplacent les anciens partout (page du clan, classements, SOCIAL). Le Journal garde les deux."
                                     : "Dissoudre le clan " + who + " ? Ses membres le quittent, sa discussion et ses points disparaissent ; ils sont prévenus. "
                                       "Le Journal garde la trace.";
    const float th = ui.text().measureWrapped(intro, FontFace::Body, ui.s(16), tw).y;
    const float fieldsH = rename ? 3 * ui.s(76) : ui.s(76);
    const float h = ui.s(80) + th + ui.s(16) + fieldsH + ui.s(34) + ui.s(76);
    Rect box{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
    cframe::panel(ui, box, true, 18);
    ui.r().drawRoundedRect(box, ui.s(18), rename ? ui.theme().accent.withAlphaF(0.55f) : ui.theme().danger, ui.s(1.5f));
    ui.pushId("admin/clans/modal");
    const float x = box.x + ui.s(30);
    cell(ui, rename ? "Renommer un clan" : "Dissoudre un clan", {x, box.y + ui.s(40)}, 26, tw, ui.theme().text, FontFace::Title);
    ui.labelWrapped(intro, {x, box.y + ui.s(70)}, tw, 16, ui.theme().textDim);
    float y = box.y + ui.s(80) + th + ui.s(16);
    auto caption = [&](const std::string& t) { cell(ui, t, {x, y + ui.s(10)}, 15, tw, ui.theme().textDim); };
    std::string problem;
    if (rename) {
        caption("Nom du clan (3 à 32 caractères)");
        ui.textField("name", {x, y + ui.s(24), tw, ui.s(44)}, name_, "Nom du clan", 96);
        y += ui.s(76);
        caption("Tag (2 à 5 lettres ou chiffres)");
        ui.textField("tag", {x, y + ui.s(24), std::min(tw, ui.s(260)), ui.s(44)}, tag_, "TAG", 5);
        y += ui.s(76);
        problem = clans::nameError(clans::cleanName(name_));
        if (problem.empty()) problem = clans::tagError(tag_);
        if (problem.empty() && clans::cleanName(name_) == jstr(target_, "name") && tag_ == jstr(target_, "tag")) problem = "Rien n'a changé.";
    }
    caption("Motif (facultatif, gardé au Journal)");
    ui.textField("reason", {x, y + ui.s(24), tw, ui.s(44)}, reason_, "Nom injurieux, triche…", 200);
    y += ui.s(76);
    const std::string shown = !error_.empty() ? error_ : problem;
    cell(ui, shown, {x, y + ui.s(12)}, 15, tw, ui.theme().danger);
    const float by = box.bottom() - ui.s(66);
    UIContext::ButtonStyle ok;
    ok.primary = rename;
    ok.danger = !rename;
    ok.enabled = !busy_ && problem.empty();
    const std::string okLabel = rename ? "Renommer" : "Dissoudre";
    const float okW = ui.text().measure(okLabel, FontFace::Bold, ui.s(17)).x + ui.s(44);
    const float cW = ui.text().measure("Annuler", FontFace::Bold, ui.s(17)).x + ui.s(44);
    const bool go = cframe::button(ui, "ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, okLabel, ok);
    const bool cancel = cframe::button(ui, "cancel", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, "Annuler") ||
                        (ui.backPressed() && !ui.editingText());
    ui.popId();
    if (cancel) {
        mode_ = Mode::None;
        return;
    }
    if (!go || !ok.enabled) return;
    const std::string id = jstr(target_, "id");
    Json body = Json::object();
    if (!clans::cleanName(reason_).empty()) body["reason"] = clans::cleanName(reason_);
    if (rename) {
        const std::string nm = clans::cleanName(name_);
        if (nm != jstr(target_, "name")) body["name"] = nm;
        if (tag_ != jstr(target_, "tag")) body["tag"] = tag_;
    }
    busy_ = true;
    error_.clear();
    g_.online.send(rename ? "PATCH" : "DELETE", "/api/v1/admin/clans/" + id, body, OnlineService::guard(life_, [this, rename, who](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           error_ = r.message;
                           return;
                       }
                       mode_ = Mode::None;
                       say_(rename ? "Clan renommé (inscrit au Journal)." : "Clan " + who + " dissous (inscrit au Journal).", false);
                       load(false);
                   }));
}

}  // namespace cosmic
