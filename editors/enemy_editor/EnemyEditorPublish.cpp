// Enemy editor, ADMINISTRATION › Ennemis (2.45, docs/online/ENEMY_REVISIONS.md): the definition of
// a native entity published online as a new revision, together with a new revision of every
// campaign level that uses it (same content, bound to the new definition, rated again), each
// with its own choice about the records. The server computes the numbers, checks everything
// again and publishes it all in one batch (server/src/enemyRevisions.ts); its answer is shown.

#include <algorithm>
#include <chrono>

#include "editors/enemy_editor/EnemyEditorScene.h"
#include "game/GameServices.h"
#include "game/online/OnlineSession.h"

namespace cosmic {

namespace {
/// « 3 révisions de niveaux », « 1 révision de niveau ».
std::string levelRevisions(size_t n) { return std::to_string(n) + (n > 1 ? " révisions de niveaux" : " révision de niveau"); }

/// A check box with its caption, reached like a button (keyboard, gamepad, mouse).
bool checkBox(UIContext& ui, const std::string& name, const Rect& r, const std::string& caption, bool on) {
    bool hovered = false;
    bool clicked = ui.hotspot(name, r, &hovered);
    const bool focus = ui.focused(name);
    if (hovered || focus) ui.r().fillRoundedRect(r, ui.s(6), Color{40, 60, 100, 160});
    if (focus) ui.r().drawRoundedRect(r, ui.s(6), ui.theme().accent, ui.s(1.5f));
    const float bs = ui.s(24);
    Rect box{r.x + ui.s(8), r.center().y - bs * 0.5f, bs, bs};
    ui.r().fillRoundedRect(box, ui.s(5), on ? ui.theme().accent.withAlpha(220) : Color{20, 28, 50, 255});
    ui.r().drawRoundedRect(box, ui.s(5), on ? ui.theme().accent : Color{120, 140, 170, 255}, ui.s(1.5f));
    if (on) {
        const Color ink{8, 20, 30, 255};
        ui.r().drawLine({box.x + bs * 0.22f, box.y + bs * 0.52f}, {box.x + bs * 0.42f, box.y + bs * 0.72f}, ink, ui.s(3));
        ui.r().drawLine({box.x + bs * 0.42f, box.y + bs * 0.72f}, {box.x + bs * 0.78f, box.y + bs * 0.30f}, ink, ui.s(3));
    }
    const float tx = box.right() + ui.s(10), room = r.right() - tx - ui.s(6);
    const float fs = ui.fitFont(caption, FontFace::Body, 16, room, 0.7f);
    ui.label(ui.elide(caption, FontFace::Body, fs, room), {tx, r.center().y}, fs, on ? ui.theme().text : ui.theme().textDim, Align::Left, FontFace::Body,
             VAlign::Middle);
    return clicked;
}
}  // namespace

// ------------------------------------------------------------------ the left panel

void EnemyEditorScene::drawAdminPanel(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {12, 16, 32, 245});
    ui.pushId("adminpanel");
    const float x = r.x + ui.s(16), w = r.w - ui.s(32);
    float y = r.y + ui.s(18);
    const float ns = ui.fitFont(work_.name, FontFace::Bold, 23, w);
    ui.label(ui.elide(work_.name, FontFace::Bold, ns, w), {x, y}, ns, ui.theme().text, Align::Left, FontFace::Bold);
    y += ui.s(36);
    auto line = [&](const std::string& t, Color c) {
        const float fs = ui.fitFont(t, FontFace::Body, 16, w, 0.75f);
        ui.label(ui.elide(t, FontFace::Body, fs, w), {x, y}, fs, c, Align::Left, FontFace::Body);
        y += ui.s(26);
    };
    line("Identifiant : " + selectedId_, ui.theme().textDim);
    line(adminBase_ > 0 ? "En ligne : révision " + std::to_string(adminBase_) : std::string("En ligne : la définition du jeu"), ui.theme().textDim);
    if (dirty_) line("Modifications non publiées", ui.theme().warm);
    y += ui.s(8);
    // The buttons at the bottom; the explanation in the room above them only.
    const float bh = ui.s(52), gap = ui.s(10);
    const float by = r.bottom() - ui.s(16) - bh * 2 - gap;
    const std::string info =
        "Entité native : sa définition est celle de tous les niveaux de la campagne qui l'utilisent. « Publier… » met en ligne une nouvelle "
        "révision et une nouvelle révision de chacun de ces niveaux, avec le choix des records pour chacun. Ses planches d'animation et "
        "leurs durées restent celles du jeu. Les niveaux de la communauté et les vôtres gardent la définition du jeu.";
    const float ih = ui.text().measureWrapped(info, FontFace::Body, ui.s(15), w).y;
    if (y + ih <= by - ui.s(10)) ui.labelWrapped(info, {x, y}, w, 15, ui.theme().textDim);
    UIContext::ButtonStyle ps;
    ps.small = true;
    ps.primary = dirty_;
    ps.enabled = dirty_ && access().publish;
    if (ui.button("publish", {x, by, w, bh}, "Publier…", ps)) openPublish();
    UIContext::ButtonStyle rs;
    rs.small = true;
    rs.enabled = dirty_;
    if (ui.button("revert", {x, by + bh + gap, w, bh}, "Annuler les modifications", rs)) {
        select(selectedId_);
        flash("Modifications annulées");
    }
    ui.popId();
}

// ------------------------------------------------------------------ the publication

void EnemyEditorScene::openPublish() {
    if (!admin_ || publish_ || !access().publish) return;
    publish_ = std::make_unique<Publish>();
    // The definition exactly as it is sent and stored (its identifier: the native's).
    EnemyDefinition d = work_;
    d.id = selectedId_;
    publish_->definition = d.toJson().dump(1) + "\n";
    const std::string id = selectedId_;
    // The revision online now, the next number, the campaign levels with their revision and bests.
    g_.online.get("/api/v1/admin/enemies/" + id + "/revisions", OnlineService::guard(life_, [this, id](const ApiResult& r) {
                      if (!publish_ || publish_->step != 0) return;
                      Publish& p = *publish_;
                      if (!r.ok) {
                          p.step = 6;
                          p.error = r.status == 404 && r.code == "NOT_FOUND" ? std::string("Ce compte n'a plus le rôle admin.") : r.message;
                          return;
                      }
                      const int cur = jget<int>(r.data, "current_revision", 0);
                      if (cur != adminBase_) {
                          p.step = 6;
                          p.error = "La définition en ligne a changé depuis l'ouverture (révision " + std::to_string(cur) +
                                    ") : rouvrez l'entité depuis Administration › Ennemis.";
                          return;
                      }
                      p.revision = jget<int>(r.data, "next_revision", cur + 1);
                      std::map<std::string, ServerCampaignLevel> server;
                      for (const Json& l : jfield(r.data, "levels", Json::array())) {
                          ServerCampaignLevel s;
                          s.version = jget<int>(l, "version", 0);
                          s.nextVersion = jget<int>(l, "next_version", s.version + 1);
                          s.contentHash = jstr(l, "content_hash");
                          s.bests = jget<int>(l, "bests", 0);
                          server[jstr(l, "level_id")] = s;
                      }
                      std::map<std::string, int> current;
                      const Json& cr = jfield(r.data, "current_revisions", Json::object());
                      if (cr.is_object())
                          for (const auto& [eid, rev] : cr.items())
                              if (rev.is_number_integer()) current[eid] = rev.get<int>();
                      // The new definition as the games will read it, in the content while the
                      // levels are prepared with it (removed again if nothing is published).
                      Json parsed = Json::parse(p.definition, nullptr, false);
                      if (parsed.is_discarded() || !g_.content.setEnemyRevision(p.revision, EnemyDefinition::fromJson(parsed))) {
                          p.step = 6;
                          p.error = "Définition illisible.";
                          return;
                      }
                      p.pending = true;
                      p.step = 1;
                      const ContentDatabase* content = &g_.content;
                      const int rev = p.revision;
                      // A world runs each level's intro, the rating analyses its route: never on the main thread.
                      p.preparing = std::async(std::launch::async, [content, id, rev, server, current]() {
                          return planEnemyPublication(*content, id, rev, server, current);
                      });
                  }),
                  false);
}

void EnemyEditorScene::updatePublish() {
    if (!publish_ || publish_->step != 1 || !publish_->preparing.valid()) return;
    if (publish_->preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    EnemyPublicationPlan plan = publish_->preparing.get();
    if (!plan.error.empty()) {
        publish_->step = 6;
        publish_->error = plan.error;
        return;
    }
    publish_->levels = std::move(plan.levels);
    publish_->reset.assign(publish_->levels.size(), 1);  // new boards by default: the enemy changed
    publish_->step = 2;
    g_.ui.setFocus("publish/all");
}

void EnemyEditorScene::sendPublish() {
    if (!publish_ || publish_->step != 3) return;
    Publish& p = *publish_;
    Json levels = Json::array();
    for (size_t i = 0; i < p.levels.size(); ++i) {
        const EnemyPublicationLevel& l = p.levels[i];
        levels.push_back({{"level_id", l.levelId},
                          {"base_version", l.baseVersion},
                          {"records", p.reset[i] ? "reset" : "keep"},
                          {"level_file", l.text},
                          {"manifest", l.manifest},
                          {"rating", l.rating}});
    }
    Json body = {{"definition", p.definition},
                 {"base_revision", adminBase_},
                 {"game_version", std::string(COSMIC_VERSION_STRING)},
                 {"levels", levels}};
    p.step = 4;
    const std::string id = selectedId_;
    g_.online.send(
        "POST", "/api/v1/admin/enemies/" + id + "/revisions", body,
        OnlineService::guard(life_,
                             [this, id](const ApiResult& r) {
                                 if (!publish_ || publish_->step != 4) return;
                                 Publish& p = *publish_;
                                 if (!r.ok) {
                                     p.step = 6;
                                     std::string why = r.status == 404 && r.code == "NOT_FOUND" ? std::string("Ce compte n'a plus le rôle admin.") : r.message;
                                     if (r.details.is_array())
                                         for (const Json& d : r.details) {
                                             if (d.is_string()) why += " · " + d.get<std::string>();
                                             else if (d.is_object()) why += " · " + jstr(d, "field") + " : " + jstr(d, "message");
                                         }
                                     p.error = why;
                                     return;
                                 }
                                 p.result = r.data;
                                 const int revision = jget<int>(r.data, "revision", p.revision);
                                 // This game plays it at once: the definition and the level revisions, as
                                 // the server stored them (checked like a download).
                                 EnemyRevisionFile f;
                                 f.enemyId = id;
                                 f.revision = revision;
                                 f.sha256 = jstr(r.data, "sha256");
                                 f.size = static_cast<int64_t>(p.definition.size());
                                 f.minGameVersion = COSMIC_VERSION_STRING;
                                 f.path = jstr(r.data, "path");
                                 std::string err;
                                 if (g_.enemyRevisions.install(g_.content, f, p.definition, &err)) {
                                     g_.enemyRevisions.save();
                                     p.pending = false;  // kept for good
                                 }
                                 for (const Json& l : jfield(r.data, "levels", Json::array())) {
                                     auto it = std::find_if(p.levels.begin(), p.levels.end(),
                                                            [&](const EnemyPublicationLevel& x) { return x.levelId == jstr(l, "level_id"); });
                                     if (it == p.levels.end()) continue;
                                     const Json& file = jfield(l, "file", Json());
                                     CampaignRevisionFile cf;
                                     cf.levelId = it->levelId;
                                     cf.version = jget<int>(l, "version", it->version);
                                     cf.contentHash = jstr(l, "content_hash", it->hash);
                                     cf.sha256 = jstr(file, "sha256");
                                     cf.size = static_cast<int64_t>(it->text.size());
                                     cf.minGameVersion = COSMIC_VERSION_STRING;
                                     cf.path = jstr(file, "path");
                                     if (g_.campaignRevisions.install(g_.content, cf, it->text, &err)) g_.campaignRevisions.save();
                                 }
                                 g_.refreshCampaignRevisions();
                                 g_.ratings.contentChanged();
                                 // The editor goes on from the published revision.
                                 adminBase_ = revision;
                                 adminMissing_ = false;
                                 dirty_ = false;
                                 confirmQuit_ = false;
                                 history_.reset(work_.toJson());
                                 p.step = 5;
                                 g_.ui.setFocus("publish/close");
                                 flash("Révision " + std::to_string(revision) + " publiée");
                             }),
        120000);
}

void EnemyEditorScene::closePublish() {
    if (!publish_) return;
    if (publish_->preparing.valid()) publish_->preparing.wait();  // it reads the content
    if (publish_->pending) {
        // Not published: the prepared definition goes (a revision of that number downloaded
        // meanwhile comes back from its file).
        g_.content.removeEnemyRevision(selectedId_, publish_->revision);
        g_.enemyRevisions.applyTo(g_.content, g_.online.config().serverUrl, COSMIC_VERSION_STRING);
    }
    publish_.reset();
}

void EnemyEditorScene::drawPublish() {
    UIContext& ui = g_.ui;
    Publish& p = *publish_;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 190});
    const float bw = std::min(ui.s(1000), ui.screenW() - ui.s(48));
    const float bhBox = std::min(ui.s(780), ui.screenH() - ui.s(48));
    Rect box{(ui.screenW() - bw) * 0.5f, (ui.screenH() - bhBox) * 0.5f, bw, bhBox};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), ui.theme().warm.withAlphaF(0.8f), ui.s(1.5f));
    ui.pushId("publish");
    const float pad = ui.s(30);
    const float x = box.x + pad, w = box.w - 2 * pad;
    float y = box.y + pad;
    const std::string rev = p.revision > 0 ? " " + std::to_string(p.revision) : std::string();
    const std::string title = "Publier la révision" + rev + " de « " + work_.name + " »";
    const float ts = ui.fitFont(title, FontFace::Title, 26, w);
    ui.label(ui.elide(title, FontFace::Title, ts, w), {x, y + ui.s(16)}, ts, ui.theme().warm, Align::Left, FontFace::Title, VAlign::Middle);
    y += ui.s(46);
    const float bh = ui.s(54);
    const float footY = box.bottom() - pad - bh;
    auto para = [&](const std::string& t, Color c, float size = 17) {
        y += ui.labelWrapped(t, {x, y}, w, size, c).y + ui.s(12);
    };
    auto footer = [&](const std::string& cancel, const std::string& go, bool goEnabled, bool cancelEnabled) {
        // Right-aligned: [cancel] [go], each as wide as its label.
        int pressed = 0;
        float gw = go.empty() ? 0.0f : std::min(w * 0.6f, ui.text().measure(go, FontFace::Bold, ui.s(20)).x + ui.s(56));
        float cw = std::min(w * 0.35f, std::max(ui.s(150), ui.text().measure(cancel, FontFace::Bold, ui.s(20)).x + ui.s(48)));
        float bx = box.right() - pad;
        if (!go.empty()) {
            bx -= gw;
            UIContext::ButtonStyle gs;
            gs.primary = true;
            gs.enabled = goEnabled;
            if (ui.button("go", {bx, footY, gw, bh}, go, gs)) pressed = 2;
            bx -= ui.s(14);
        }
        bx -= cw;
        UIContext::ButtonStyle cs;
        cs.enabled = cancelEnabled;
        if (ui.button("close", {bx, footY, cw, bh}, cancel, cs) || (cancelEnabled && ui.backPressed())) pressed = 1;
        return pressed;
    };

    if (p.step == 0 || p.step == 1) {
        // No button meanwhile: the preparation reads the game's content, it ends first.
        para(p.step == 0 ? std::string("Lecture de la définition en ligne et des niveaux de la campagne…")
                         : std::string("Préparation des niveaux qui utilisent cette entité : empreintes, manifestes, notes de difficulté…"),
             ui.theme().text, 19);
    } else if (p.step == 6) {
        para(p.error.empty() ? std::string("Publication impossible.") : p.error, ui.theme().danger, 18);
        int b = footer("Fermer", "Réessayer", true, true);
        if (b == 1) closePublish();
        else if (b == 2) {
            closePublish();
            openPublish();
        }
    } else if (p.step == 4) {
        para("Envoi au serveur : la définition et " + levelRevisions(p.levels.size()) + ", publiées ensemble…", ui.theme().text, 19);
    } else if (p.step == 5) {
        // The server's answer, as it gave it.
        const int revision = jget<int>(p.result, "revision", p.revision);
        const Json& levels = jfield(p.result, "levels", Json::array());
        para("Le serveur a publié la révision " + std::to_string(revision) + " de « " + work_.name + " » et " + levelRevisions(levels.size()) +
                 ". Les jeux les téléchargent au démarrage ; un défi déjà commencé garde sa révision.",
             ui.theme().success, 17);
        const float rowH = ui.s(30);
        Rect list{x, y, w, footY - ui.s(14) - y};
        ui.beginScroll("done", list, static_cast<float>(levels.size()) * rowH);
        float ry = list.y - ui.scrollOffset();
        for (const Json& l : levels) {
            const std::string lid = jstr(l, "level_id");
            std::string name = lid;
            if (const LevelEntry* e = g_.content.level(lid)) name = e->name;
            const int carried = jget<int>(l, "carried_bests", 0);
            const std::string t = "« " + name + " » : révision " + std::to_string(jget<int>(l, "version", 0)) + " · " +
                                  (jstr(l, "records") == "keep"
                                       ? "records gardés (" + std::to_string(carried) + (carried > 1 ? " meilleurs temps reportés)" : " meilleur temps reporté)")
                                       : std::string("records réinitialisés"));
            const float room = list.w - ui.s(16);
            const float fs = ui.fitFont(t, FontFace::Body, 16, room, 0.8f);
            ui.label(ui.elide(t, FontFace::Body, fs, room), {list.x, ry + rowH * 0.5f}, fs, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
            ry += rowH;
        }
        ui.endScroll();
        if (footer("Fermer", {}, false, true) == 1) closePublish();
    } else if (p.step == 3) {
        int resets = 0;
        for (char c : p.reset) resets += c ? 1 : 0;
        const int keeps = static_cast<int>(p.reset.size()) - resets;
        para("Confirmer la publication ?", ui.theme().text, 21);
        std::string text = "La révision " + std::to_string(p.revision) + " de « " + work_.name + " »";
        if (p.levels.empty()) text += " est publiée seule : aucun niveau de la campagne ne l'utilise.";
        else
            text += " et " + levelRevisions(p.levels.size()) + " sont publiées ensemble. Records réinitialisés : " + std::to_string(resets) +
                    (resets > 1 ? " niveaux" : " niveau") + " ; records gardés : " + std::to_string(keeps) + (keeps > 1 ? " niveaux." : " niveau.");
        para(text, ui.theme().text);
        para("Les jeux les téléchargent au démarrage ; les révisions d'avant restent jouables avec leur définition (records archivés, replays, "
             "défis déjà commencés). La publication est inscrite au Journal.",
             ui.theme().textDim, 16);
        int b = footer("Retour", "Confirmer la publication", true, true);
        if (b == 1) p.step = 2;
        else if (b == 2) sendPublish();
    } else {  // step 2: the levels and their choice
        if (p.levels.empty()) {
            para("Aucun niveau de la campagne n'utilise « " + work_.name + " » : seule sa définition est publiée.", ui.theme().text);
        } else {
            const size_t n = p.levels.size();
            para(std::to_string(n) + (n > 1 ? " niveaux de la campagne utilisent « " : " niveau de la campagne utilise « ") + work_.name +
                     " » : chacun reçoit une nouvelle révision (même contenu, avec cette définition, note recalculée). Cochée, « Réinitialiser "
                     "les records » lui donne de nouveaux classements ; décochée, ses meilleurs temps y sont reportés.",
                 ui.theme().text, 16);
            // « Tout cocher », « Tout décocher ».
            const float ab = ui.s(44);
            float ax = x;
            for (int k = 0; k < 2; ++k) {
                const std::string t = k == 0 ? "Tout cocher" : "Tout décocher";
                const float aw = ui.text().measure(t, FontFace::Bold, ui.s(18)).x + ui.s(40);
                UIContext::ButtonStyle s;
                s.small = true;
                if (ui.button(k == 0 ? "all" : "none", {ax, y, aw, ab}, t, s)) std::fill(p.reset.begin(), p.reset.end(), static_cast<char>(k == 0));
                ax += aw + ui.s(12);
            }
            y += ab + ui.s(12);
            // The list: name, planet, revisions, bests; the box on the right.
            const std::string boxCaption = "Réinitialiser les records";
            const float boxW = ui.s(24) + ui.s(10) + ui.text().measure(boxCaption, FontFace::Body, ui.s(16)).x + ui.s(28);
            const float rowH = ui.s(72), gap = ui.s(6);
            Rect list{x, y, w, footY - ui.s(14) - y};
            ui.beginScroll("levels", list, static_cast<float>(p.levels.size()) * (rowH + gap));
            float ry = list.y - ui.scrollOffset();
            const float rw = list.w - ui.s(16);
            for (size_t i = 0; i < p.levels.size(); ++i) {
                const EnemyPublicationLevel& l = p.levels[i];
                Rect row{list.x, ry, rw, rowH};
                ry += rowH + gap;
                ui.r().fillRoundedRect(row, ui.s(8), p.reset[i] ? Color{34, 24, 30, 235} : Color{18, 26, 50, 235});
                Rect cb{row.right() - ui.s(8) - boxW, row.center().y - ui.s(22), boxW, ui.s(44)};
                if (checkBox(ui, "reset" + std::to_string(i), cb, boxCaption, p.reset[i] != 0)) p.reset[i] = p.reset[i] ? 0 : 1;
                const float tx = row.x + ui.s(14), room = cb.x - ui.s(14) - tx;
                std::string name = l.levelId, planet = l.planetId;
                if (const LevelEntry* e = g_.content.level(l.levelId)) name = e->name;
                if (const PlanetData* pd = g_.content.planet(l.planetId)) planet = pd->name;
                const float ns = ui.fitFont(name, FontFace::Bold, 18, room, 0.8f);
                ui.label(ui.elide(name, FontFace::Bold, ns, room), {tx, row.y + ui.s(22)}, ns, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                std::string facts = planet + " · révision " + std::to_string(l.baseVersion) + " → " + std::to_string(l.version) + " · " +
                                    (l.bests == 0 ? std::string("aucun meilleur temps")
                                                  : std::to_string(l.bests) + (l.bests > 1 ? " meilleurs temps" : " meilleur temps")) +
                                    " · " +
                                    (l.instances > 0 ? std::to_string(l.instances) + (l.instances > 1 ? " exemplaires placés" : " exemplaire placé")
                                                     : std::string("amenée par une autre entité"));
                const float fs = ui.fitFont(facts, FontFace::Body, 15, room, 0.8f);
                ui.label(ui.elide(facts, FontFace::Body, fs, room), {tx, row.y + ui.s(50)}, fs, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
            }
            ui.endScroll();
        }
        int b = footer("Annuler", "Publier…", true, true);
        if (b == 1) closePublish();
        else if (b == 2) {
            p.step = 3;
            g_.ui.setFocus("publish/close");
        }
    }
    ui.popId();
}

}  // namespace cosmic
