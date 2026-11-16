#include "ui/screens/AdminCampaign.h"

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

std::string day(const std::string& iso) {
    std::string s = stamp(iso);
    size_t dot = s.find(" · ");
    return dot == std::string::npos ? s : s.substr(0, dot);
}

std::string who(const Json& by) { return by.is_object() ? jstr(by, "username", "un compte supprimé") : std::string("un admin"); }

/// « records réinitialisés », « records gardés (12 meilleurs temps reportés) ».
std::string recordsText(const Json& change) {
    if (jstr(change, "records") == "keep") {
        int n = jget<int>(change, "carried_bests", 0);
        return "records gardés (" + std::to_string(n) + (n > 1 ? " meilleurs temps reportés)" : " meilleur temps reporté)");
    }
    return "records réinitialisés";
}

}  // namespace

AdminCampaign::AdminCampaign(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)) {}

void AdminCampaign::enter(bool force, const std::string& select) {
    if (!select.empty()) selected_ = select;
    if (!loaded_ || force) load();
    if (!selected_.empty() && (force || !history_.is_object() || jstr(jfield(history_, "level", Json()), "id") != selected_)) loadHistory(selected_);
}

void AdminCampaign::load() {
    busy_ = true;
    g_.online.get("/api/v1/admin/campaign", OnlineService::guard(life_, [this](const ApiResult& r) {
                      busy_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      planets_ = r.data.value("planets", Json::array());
                      loaded_ = true;
                  }),
                  false);
}

void AdminCampaign::loadHistory(const std::string& levelId) {
    selected_ = levelId;
    int gen = ++historyGen_;
    historyBusy_ = true;
    g_.online.get("/api/v1/admin/campaign/" + levelId + "/revisions", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != historyGen_) return;
                      historyBusy_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      history_ = r.data;
                  }),
                  false);
}

const Json* AdminCampaign::levelById(const std::string& id) const {
    // The rows themselves (never a copy: the pointer is used for the rest of the frame).
    if (!planets_.is_array()) return nullptr;
    for (const Json& p : planets_) {
        auto it = p.find("levels");
        if (it == p.end() || !it->is_array()) continue;
        for (const Json& l : *it)
            if (jstr(l, "level_id") == id) return &l;
    }
    return nullptr;
}

std::string AdminCampaign::sourceLine(const Json& rev, const Json& change) const {
    if (change.is_object()) {
        std::string verb = jstr(change, "action") == "revert" ? "Remise en ligne par " : "Publiée par ";
        return verb + who(jfield(change, "by", Json())) + " le " + stamp(jstr(change, "at")) + " · " + recordsText(change);
    }
    if (jstr(rev, "source") == "admin") return "Publiée par " + who(jfield(rev, "published_by", Json())) + " le " + stamp(jstr(rev, "published_at"));
    return "Version du jeu " + jstr(rev, "min_game_version", "?") + " · " + day(jstr(rev, "created_at"));
}

void AdminCampaign::edit(const Json& l) {
    const std::string id = jstr(l, "level_id");
    const Json& cur = jfield(l, "current", Json());
    const int version = jget<int>(cur, "version", 0);
    const std::string hash = jstr(cur, "content_hash");
    const LevelEntry* e = g_.content.level(id);
    if (!e || e->source != ContentSource::Builtin) return say_("Ce niveau n'existe pas dans cette version du jeu : mettez le jeu à jour.", true);
    // The editor opens the revision that is online: the one this game plays, or downloaded first.
    if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, id); fp && fp->hash == hash) return openEditor(id, version, hash);
    if (!jget<bool>(cur, "has_file", false))
        return say_("La révision en ligne de ce niveau vient de la version " + jstr(cur, "min_game_version", "?") +
                        " du jeu : mettez le jeu à jour pour la modifier.",
                    true);
    opening_ = true;
    say_("Téléchargement de la révision en ligne…", false);
    std::weak_ptr<int> life = life_;
    g_.refreshCampaignRevisions([this, life, id, version, hash](const std::string& err) {
        if (!life.lock()) return;
        opening_ = false;
        if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, id); fp && fp->hash == hash) return openEditor(id, version, hash);
        say_(err.empty() ? std::string("Cette révision ne se joue pas avec cette version du jeu : mettez le jeu à jour.") : err, true);
    });
}

void AdminCampaign::openEditor(const std::string& levelId, int version, const std::string& hash) {
    g_.app.pushScene(makeLevelEditorCampaign(g_, levelId, version, hash));
}

void AdminCampaign::revert() {
    const Json* l = levelById(selected_);
    if (!l || revertVersion_ <= 0 || revertChoice_ < 0) return;
    reverting_ = true;
    const int base = jget<int>(jfield(*l, "current", Json()), "version", 0);
    const int target = revertVersion_;
    const std::string id = selected_, name = jstr(*l, "name");
    const bool keep = revertChoice_ == 1;
    g_.online.send("POST", "/api/v1/admin/campaign/" + id + "/revert", Json{{"version", target}, {"base_version", base}, {"records", keep ? "keep" : "reset"}},
                   OnlineService::guard(life_, [this, id, name, target, keep](const ApiResult& r) {
                       reverting_ = false;
                       revertVersion_ = 0;
                       revertChoice_ = -1;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           say_(r.message, true);
                       } else {
                           say_("« " + name + " » : la révision " + std::to_string(target) + " est de nouveau en ligne (" +
                                    (keep ? std::string("records gardés") : std::string("records réinitialisés")) + ").",
                                false);
                           // This game plays it at once (downloaded if needed).
                           g_.refreshCampaignRevisions();
                       }
                       load();
                       loadHistory(id);
                   }));
}

// ------------------------------------------------------------------ drawing

void AdminCampaign::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    ui.pushId("campaign");
    float y = in.y, bh = ui.s(46);
    UIContext::ButtonStyle rs;
    rs.small = true;
    rs.enabled = !busy_;
    float rw = ui.text().measure("Actualiser", FontFace::Bold, ui.s(17)).x + ui.s(44);
    if (ui.button("refresh", {in.right() - rw, y, rw, bh}, "Actualiser", rs)) {
        load();
        if (!selected_.empty()) loadHistory(selected_);
    }
    {
        const std::string info = "Une révision publiée ici est téléchargée par les jeux, sans mise à jour. Chaque publication demande de choisir : "
                                 "réinitialiser les records, ou les garder.";
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
    drawLevels({in.x, y, lw, in.bottom() - y});
    drawHistory({in.x + lw + gap, y, in.w - lw - gap, in.bottom() - y});
    ui.popId();
}

void AdminCampaign::drawLevels(const Rect& r) {
    UIContext& ui = g_.ui;
    float headH = ui.s(36), rowH = ui.s(76), gap = ui.s(6);
    float content = 0.0f;
    for (const Json& p : planets_) content += headH + gap + static_cast<float>(jfield(p, "levels", Json::array()).size()) * (rowH + gap);
    if (planets_.empty()) {
        ui.label(loaded_ ? "Aucun niveau de campagne sur le serveur." : "Chargement…", {r.center().x, r.y + ui.s(40)}, 18,
                 ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        return;
    }
    ui.beginScroll("levels", r, content);
    float yy = r.y - ui.scrollOffset();
    const float w = r.w - ui.s(16);
    const float editW = ui.text().measure("Modifier dans l'éditeur", FontFace::Bold, ui.s(16)).x + ui.s(36);
    const float histW = ui.text().measure("Historique", FontFace::Bold, ui.s(16)).x + ui.s(36);
    for (const Json& p : planets_) {
        Rect hr{r.x, yy, w, headH};
        std::string pname = jstr(p, "planet_name");
        if (const PlanetData* pd = g_.content.planet(jstr(p, "planet_id"))) pname = pd->name;
        if (pname.empty()) pname = jstr(p, "planet_id");
        ui.label(ui.elide(pname, FontFace::Bold, 19, hr.w - ui.s(12)), {hr.x + ui.s(4), hr.center().y}, 19, ui.theme().accent, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        yy += headH + gap;
        for (const Json& l : jfield(p, "levels", Json::array())) {
            Rect row{r.x, yy, w, rowH};
            yy += rowH + gap;
            if (row.bottom() < r.y || row.y > r.bottom()) continue;
            const std::string id = jstr(l, "level_id");
            const Json& cur = jfield(l, "current", Json());
            const bool sel = id == selected_;
            ui.r().fillRoundedRect(row, ui.s(8), sel ? Color{26, 38, 72, 240} : Color{18, 26, 50, 235});
            if (sel) ui.r().drawRoundedRect(row, ui.s(8), ui.theme().accent.withAlphaF(0.7f), ui.s(1));
            // The buttons on the right, then the texts in the room left.
            float bh = ui.s(40), by = row.center().y - bh * 0.5f;
            Rect hb{row.right() - ui.s(10) - histW, by, histW, bh};
            Rect eb{hb.x - ui.s(8) - editW, by, editW, bh};
            UIContext::ButtonStyle es;
            es.small = true;
            es.primary = true;
            es.fontSize = 16;
            es.enabled = !opening_;
            if (ui.button("edit" + id, eb, "Modifier dans l'éditeur", es)) edit(l);
            UIContext::ButtonStyle hs;
            hs.small = true;
            hs.fontSize = 16;
            hs.selected = sel;
            if (ui.button("hist" + id, hb, "Historique", hs)) loadHistory(id);
            float tx = row.x + ui.s(14), room = eb.x - ui.s(14) - tx;
            // Name (the game's, translated) and the revision pill.
            std::string name = jstr(l, "name");
            if (const LevelEntry* e = g_.content.level(id)) name = e->name;
            name = std::to_string(jget<int>(l, "order", 0)) + ". " + name;
            std::string pill = "Révision " + std::to_string(jget<int>(cur, "version", 0));
            float pw = ui.text().measure(pill, FontFace::Bold, ui.s(13)).x + ui.s(18);
            float nameRoom = std::max(ui.s(40), room - pw - ui.s(10));
            std::string shown = ui.elide(name, FontFace::Bold, 18, nameRoom);
            float nw = ui.text().measure(shown, FontFace::Bold, ui.s(18)).x;
            float ly = row.y + ui.s(24);
            ui.label(shown, {tx, ly}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            if (nw + ui.s(10) + pw <= room) {
                Rect pr{tx + nw + ui.s(10), ly - ui.s(11), pw, ui.s(22)};
                Color pc = jstr(cur, "source") == "admin" ? ui.theme().warm : Color{150, 170, 200, 255};
                ui.r().fillRoundedRect(pr, ui.s(11), pc.withAlphaF(0.15f));
                ui.r().drawRoundedRect(pr, ui.s(11), pc, ui.s(1));
                ui.label(pill, pr.center(), 13, pc, Align::Center, FontFace::Bold, VAlign::Middle);
            }
            std::string src = sourceLine(cur, jfield(cur, "change", Json()));
            float fs = ui.fitFont(src, FontFace::Body, 15, room, 0.8f);
            ui.label(ui.elide(src, FontFace::Body, fs, room), {tx, row.y + ui.s(54)}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
    }
    ui.endScroll();
}

void AdminCampaign::drawHistory(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(r, ui.s(12), Color{14, 20, 42, 235});
    ui.r().drawRoundedRect(r, ui.s(12), ui.theme().border, ui.s(1));
    Rect in{r.x + ui.s(16), r.y + ui.s(14), r.w - ui.s(32), r.h - ui.s(28)};
    const Json* l = selected_.empty() ? nullptr : levelById(selected_);
    if (!l) {
        ui.labelWrapped("Choisissez « Historique » sur un niveau pour voir ses révisions et revenir à l'une d'elles.", {in.x, in.y + ui.s(8)}, in.w, 17,
                        ui.theme().textDim);
        return;
    }
    std::string name = jstr(*l, "name");
    if (const LevelEntry* e = g_.content.level(selected_)) name = e->name;
    std::string title = "Historique · " + name;
    float ts = ui.fitFont(title, FontFace::Bold, 21, in.w);
    ui.label(ui.elide(title, FontFace::Bold, ts, in.w), {in.x, in.y + ui.s(16)}, ts, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
    float y = in.y + ui.s(40);
    const bool fresh = history_.is_object() && jstr(jfield(history_, "level", Json()), "id") == selected_;
    if (!fresh) {
        ui.label(historyBusy_ ? "Chargement…" : "Historique indisponible.", {in.x, y + ui.s(16)}, 17, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        return;
    }
    const Json& revs = jfield(history_, "revisions", Json::array());
    const float revertW = ui.text().measure("Revenir à cette révision", FontFace::Bold, ui.s(15)).x + ui.s(32);
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
        const int v = jget<int>(rev, "version", 0);
        ui.r().fillRoundedRect(row, ui.s(8), current ? Color{30, 26, 22, 235} : Color{18, 26, 50, 235});
        float tx = row.x + ui.s(12);
        float room = row.w - ui.s(24);
        if (!current) {
            UIContext::ButtonStyle bs;
            bs.small = true;
            bs.fontSize = 15;
            bs.enabled = !reverting_ && jstr(rev, "compatibility_status") != "disabled";
            Rect b{row.right() - ui.s(10) - revertW, row.center().y - ui.s(20), revertW, ui.s(40)};
            if (ui.button("revert" + std::to_string(v), b, "Revenir à cette révision", bs)) {
                revertVersion_ = v;
                revertChoice_ = -1;
                ui.setFocus("revert/cancel");
            }
            room = b.x - ui.s(12) - tx;
        }
        std::string head = "Révision " + std::to_string(v);
        float hw = ui.text().measure(head, FontFace::Bold, ui.s(18)).x;
        ui.label(ui.elide(head, FontFace::Bold, 18, room), {tx, row.y + ui.s(20)}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string pill = current ? "en ligne" : jstr(rev, "compatibility_status") == "disabled" ? "désactivée" : "archivée";
        float pw = ui.text().measure(pill, FontFace::Bold, ui.s(13)).x + ui.s(18);
        if (hw + ui.s(10) + pw <= room) {
            Color pc = current ? ui.theme().success : Color{150, 170, 200, 255};
            Rect pr{tx + hw + ui.s(10), row.y + ui.s(9), pw, ui.s(22)};
            ui.r().fillRoundedRect(pr, ui.s(11), pc.withAlphaF(0.15f));
            ui.r().drawRoundedRect(pr, ui.s(11), pc, ui.s(1));
            ui.label(pill, pr.center(), 13, pc, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        std::string src = sourceLine(rev, Json());
        float fs = ui.fitFont(src, FontFace::Body, 15, room, 0.8f);
        ui.label(ui.elide(src, FontFace::Body, fs, room), {tx, row.y + ui.s(46)}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        int bests = jget<int>(rev, "bests", 0);
        std::string facts = (bests == 0 ? std::string("aucun meilleur temps") : std::to_string(bests) + (bests > 1 ? " meilleurs temps" : " meilleur temps")) +
                            " · empreinte " + jstr(rev, "content_hash").substr(0, 12);
        float fs2 = ui.fitFont(facts, FontFace::Body, 14, room, 0.8f);
        ui.label(ui.elide(facts, FontFace::Body, fs2, room), {tx, row.y + ui.s(70)}, fs2, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    ui.endScroll();
}

void AdminCampaign::drawModal() {
    UIContext& ui = g_.ui;
    const Json* l = levelById(selected_);
    if (!l) {
        revertVersion_ = 0;
        return;
    }
    std::string name = jstr(*l, "name");
    if (const LevelEntry* e = g_.content.level(selected_)) name = e->name;
    const int cur = jget<int>(jfield(*l, "current", Json()), "version", 0);
    const std::string v = std::to_string(revertVersion_);
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 185});
    const float bw = std::min(ui.s(760), ui.screenW() - ui.s(48));
    const float pad = ui.s(32), w = bw - 2 * pad;
    const std::string title = "Revenir à la révision " + v + " ?";
    const std::string text = "« " + name + " » : la révision " + v + " remplace la révision " + std::to_string(cur) +
                             " en ligne. Les jeux la téléchargent ; un défi déjà commencé garde sa révision.";
    const std::string opt[2] = {"Réinitialiser les records", "Garder les records"};
    const std::string desc[2] = {
        "La révision " + v + " revient avec ses propres classements, tels qu'ils étaient. Ceux de la révision " + std::to_string(cur) +
            " restent archivés avec elle, jamais mélangés.",
        "Seulement si les deux révisions se jouent exactement de la même façon (décor seul). Les meilleurs temps de la révision " + std::to_string(cur) +
            " sont reportés sur la révision " + v + " : le meilleur temps de chaque joueur est gardé.",
    };
    // Heights measured first: nothing overlaps whatever the texts.
    const float optH = ui.s(46);
    float textH = ui.text().measureWrapped(text, FontFace::Body, ui.s(17), w).y;
    float descH[2];
    for (int i = 0; i < 2; ++i) descH[i] = ui.text().measureWrapped(desc[i], FontFace::Body, ui.s(15), w - ui.s(20)).y;
    const float buttonsH = ui.s(52);
    float h = pad + ui.s(36) + ui.s(12) + textH + ui.s(18) + ui.s(24) + ui.s(10);
    for (int i = 0; i < 2; ++i) h += optH + ui.s(6) + descH[i] + ui.s(14);
    h += ui.s(10) + buttonsH + ui.s(26);
    h = std::min(h, ui.screenH() - ui.s(32));
    Rect box{(ui.screenW() - bw) * 0.5f, (ui.screenH() - h) * 0.5f, bw, h};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), ui.theme().warm.withAlphaF(0.8f), ui.s(1.5f));
    ui.pushId("revert");
    float x = box.x + pad, y = box.y + pad;
    float ts = ui.fitFont(title, FontFace::Title, 26, w);
    ui.label(title, {x, y + ui.s(18)}, ts, ui.theme().warm, Align::Left, FontFace::Title, VAlign::Middle);
    y += ui.s(36) + ui.s(12);
    ui.labelWrapped(text, {x, y}, w, 17, ui.theme().text);
    y += textH + ui.s(18);
    ui.label("Les records (choix obligatoire) :", {x, y + ui.s(12)}, 16, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
    y += ui.s(24) + ui.s(10);
    for (int i = 0; i < 2; ++i) {
        UIContext::ButtonStyle os;
        os.selected = revertChoice_ == i;
        os.align = Align::Left;
        os.danger = i == 0 && revertChoice_ == 0;
        std::string label = (revertChoice_ == i ? "● " : "○ ") + opt[i];
        if (ui.button(i == 0 ? "reset" : "keep", {x, y, w, optH}, label, os)) revertChoice_ = i;
        y += optH + ui.s(6);
        ui.labelWrapped(desc[i], {x + ui.s(20), y}, w - ui.s(20), 15, ui.theme().textDim);
        y += descH[i] + ui.s(14);
    }
    float by = box.bottom() - buttonsH - ui.s(26);
    float cw = ui.s(170), ow = std::min(w - cw - ui.s(14), ui.text().measure("Revenir à la révision " + v, FontFace::Bold, ui.s(20)).x + ui.s(48));
    UIContext::ButtonStyle ok;
    ok.primary = true;
    ok.enabled = revertChoice_ >= 0 && !reverting_;
    if (ui.button("cancel", {box.right() - pad - ow - ui.s(14) - cw, by, cw, buttonsH}, "Annuler") || ui.backPressed()) {
        revertVersion_ = 0;
        revertChoice_ = -1;
    } else if (ui.button("ok", {box.right() - pad - ow, by, ow, buttonsH}, reverting_ ? "Envoi…" : "Revenir à la révision " + v, ok)) {
        revert();
    }
    ui.popId();
}

}  // namespace cosmic
