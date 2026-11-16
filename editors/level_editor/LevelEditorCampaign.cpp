// Level editor, campaign revisions (2.45, ADMINISTRATION › Campagne): a campaign level edited by
// an administrator is published online as a new revision. Before publishing, the steps of the
// campaign pipeline of tools/levelgen run on it (seals, power stations, rating:
// game/levels/CampaignPipeline.h), off the main thread; then the REQUIRED choice about the
// records, then the upload. The server checks everything again (campaignRevisions.ts).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "editors/level_editor/LevelEditorScene.h"
#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/online/EnemyPublication.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/Sha256.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

std::unique_ptr<Scene> makeLevelEditorCampaign(GameServices& g, const std::string& levelId, int baseVersion, const std::string& baseHash) {
    auto s = std::make_unique<LevelEditorScene>(g, levelId);
    s->goCampaign(baseVersion, baseHash);
    return s;
}

void LevelEditorScene::goCampaign(int baseVersion, const std::string& baseHash) {
    campaignMode_ = true;
    campaign_ = std::make_unique<CampaignEdit>();
    campaign_->baseVersion = baseVersion;
    campaign_->baseHash = baseHash;
}

void LevelEditorScene::openPublish() {
    if (!campaignMode_ || !campaign_) return;
    if (campaign_->step == 1 || campaign_->step == 3) {  // already preparing / sending: the dialog again
        modal_ = Modal::Publish;
        return;
    }
    menu_ = Menu::None;
    modal_ = Modal::Publish;
    campaign_->step = 0;
    campaign_->records = -1;
    campaign_->error.clear();
    campaign_->prepared.reset();
    // The next revision number and the revision online now (another admin may have published).
    g_.online.get("/api/v1/admin/campaign/" + level_.id + "/revisions", OnlineService::guard(life_, [this](const ApiResult& r) {
                      if (!campaign_ || campaign_->step != 0) return;
                      if (!r.ok) {
                          campaign_->step = 5;
                          campaign_->error = r.status == 404 && r.code == "NOT_FOUND" ? std::string("Ce compte n'a plus le rôle admin.") : r.message;
                          return;
                      }
                      if (jget<int>(r.data, "current_version", 0) != campaign_->baseVersion || jstr(r.data, "content_hash") != campaign_->baseHash) {
                          campaign_->step = 5;
                          campaign_->error = "La révision en ligne a changé depuis l'ouverture de ce niveau (révision " +
                                             std::to_string(jget<int>(r.data, "current_version", 0)) +
                                             ") : votre brouillon est gardé ; rouvrez le niveau depuis Administration › Campagne.";
                          autosave();
                          return;
                      }
                      campaign_->nextVersion = jget<int>(r.data, "next_version", campaign_->baseVersion + 1);
                      campaign_->step = 1;
                      LevelData copy = level_;
                      copy.metadata["editedWith"] = std::string("Cosmic Platformer ") + COSMIC_VERSION_STRING;
                      const int rev = campaign_->nextVersion;
                      const ContentDatabase* content = &g_.content;
                      // 2.45: the native entities it uses at their revision online (ADMINISTRATION ›
                      // Ennemis), named in the revision (docs/online/ENEMY_REVISIONS.md).
                      auto current = g_.enemyRevisions.currentRevisions(g_.online.config().serverUrl);
                      // The analyser and the rating take a moment: never on the main thread.
                      campaign_->preparing = std::async(std::launch::async, [copy, rev, content, current]() -> std::optional<CampaignEdit::Prepared> {
                          CampaignEdit::Prepared p;
                          LevelData bound = copy;
                          setEnemyBinding(bound, enemyBindingFor(*content, copy, {}, 0, current));
                          p.level = prepareCampaignRevision(bound, *content, rev, &p.report);
                          return p;
                      });
                  }),
                  false);
}

void LevelEditorScene::updatePublish() {
    if (!campaign_ || campaign_->step != 1 || !campaign_->preparing.valid()) return;
    if (campaign_->preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto p = campaign_->preparing.get();
    if (!p) {
        campaign_->step = 5;
        campaign_->error = "La préparation du niveau a échoué.";
        return;
    }
    // The file exactly as it will be stored, and its fingerprint computed from those bytes (the
    // games download them and compute the same).
    p->text = p->level.toJson().dump(1) + "\n";
    Json parsed = Json::parse(p->text, nullptr, false);
    auto fp = parsed.is_discarded() ? std::nullopt : fingerprintRankedLevel(g_.content, parsed);
    auto lvl = parsed.is_discarded() ? std::nullopt : LevelData::fromJson(parsed);
    if (!fp || !lvl) {
        campaign_->step = 5;
        campaign_->error = "Empreinte du niveau impossible à calculer.";
        return;
    }
    p->hash = fp->hash;
    p->manifest = onlineManifestEntry(g_.content, *lvl, *fp);
    campaign_->prepared = std::move(p);
    campaign_->step = 2;
}

void LevelEditorScene::sendPublish() {
    if (!campaign_ || campaign_->step != 2 || !campaign_->prepared || campaign_->records < 0) return;
    const CampaignEdit::Prepared& p = *campaign_->prepared;
    const bool keep = campaign_->records == 1;
    Json body = {{"level_file", p.text},
                 {"base_version", campaign_->baseVersion},
                 {"records", keep ? "keep" : "reset"},
                 {"game_version", std::string(COSMIC_VERSION_STRING)},
                 {"manifest", p.manifest},
                 {"rating", p.report.rating.toJson()}};
    campaign_->step = 3;
    g_.online.send("POST", "/api/v1/admin/campaign/" + level_.id + "/revisions", body, OnlineService::guard(life_, [this, keep](const ApiResult& r) {
                       if (!campaign_ || !campaign_->prepared) return;
                       if (!r.ok) {
                           campaign_->step = 5;
                           std::string why = r.status == 404 && r.code == "NOT_FOUND" ? std::string("Ce compte n'a plus le rôle admin.") : r.message;
                           if (r.details.is_array()) {
                               for (const Json& d : r.details) {
                                   if (d.is_string()) why += " · " + d.get<std::string>();
                                   else if (d.is_object()) why += " · " + jstr(d, "field") + " : " + jstr(d, "message");
                               }
                           }
                           campaign_->error = why;
                           return;
                       }
                       CampaignEdit::Prepared done = std::move(*campaign_->prepared);
                       campaign_->prepared.reset();
                       const int version = jget<int>(r.data, "version", campaign_->nextVersion);
                       const Json& file = jfield(r.data, "file", Json());
                       // This game plays the published revision at once (no download).
                       CampaignRevisionFile f;
                       f.levelId = level_.id;
                       f.version = version;
                       f.contentHash = done.hash;
                       f.sha256 = jstr(file, "sha256");
                       f.size = static_cast<int64_t>(done.text.size());
                       f.minGameVersion = COSMIC_VERSION_STRING;
                       f.path = jstr(file, "path");
                       std::string err;
                       if (g_.campaignRevisions.install(g_.content, f, done.text, &err)) g_.campaignRevisions.save();
                       g_.refreshCampaignRevisions();
                       // The document is now the published revision (seals and stations included).
                       campaign_->baseVersion = version;
                       campaign_->baseHash = done.hash;
                       level_ = done.level;
                       history_.reset(level_.toJson(), "Révision " + std::to_string(version) + " publiée");
                       dirty_ = false;
                       previewDirty_ = true;
                       checksDirty_ = true;
                       std::error_code ec;
                       fs::remove(Paths::autosaveDir() / (FileUtil::slugify(level_.id) + ".json"), ec);
                       autosaveAvailable_ = false;
                       int carried = jget<int>(r.data, "carried_bests", 0);
                       campaign_->publishedText =
                           "La révision " + std::to_string(version) + " de « " + level_.name + " » est en ligne : les jeux la téléchargent au démarrage. " +
                           (keep ? "Records gardés : " + std::to_string(carried) + (carried > 1 ? " meilleurs temps reportés." : " meilleur temps reporté.")
                                 : std::string("Records réinitialisés : les anciens restent archivés avec la révision précédente."));
                       campaign_->step = 4;
                       flash("Révision " + std::to_string(version) + " publiée");
                   }));
}

void LevelEditorScene::modalPublish(const Rect& c) {
    UIContext& ui = g_.ui;
    if (!campaign_) {
        modal_ = Modal::None;
        return;
    }
    CampaignEdit& ce = *campaign_;
    float y = c.y;
    const std::string title = "Publier une révision de la campagne";
    ui.label(title, {c.x, y}, ui.fitFont(title, FontFace::Bold, 30, c.w), ui.theme().text, Align::Left, FontFace::Bold);
    y += ui.s(50);
    std::string sub = "« " + level_.name + " » · révision en ligne " + std::to_string(ce.baseVersion) +
                      (ce.nextVersion > 0 ? " → nouvelle révision " + std::to_string(ce.nextVersion) : std::string());
    float ss = ui.fitFont(sub, FontFace::Body, 19, c.w);
    ui.label(ui.elide(sub, FontFace::Body, ss, c.w), {c.x, y}, ss, ui.theme().textDim, Align::Left, FontFace::Body);
    y += ui.s(40);
    const float bh = ui.s(56);
    const float footY = c.bottom() - bh;
    auto line = [&](const std::string& t, Color col, float size = 18) {
        float fs = ui.fitFont(t, FontFace::Body, size, c.w - ui.s(20), 0.8f);
        ui.label(ui.elide(t, FontFace::Body, fs, c.w - ui.s(20)), {c.x + ui.s(20), y}, fs, col, Align::Left, FontFace::Body);
        y += ui.s(size + 12);
    };

    if (ce.step == 0 || ce.step == 1) {
        line(ce.step == 0 ? "Vérification de la révision en ligne…" : "Préparation : sceaux, stations de pouvoirs, note de difficulté…", ui.theme().text, 20);
    } else if (ce.step == 5) {
        float th = ui.labelWrapped(ce.error.empty() ? std::string("Publication impossible.") : ce.error, {c.x, y}, c.w, 20, ui.theme().danger).y;
        y += th + ui.s(16);
    } else if (ce.step == 4) {
        float th = ui.labelWrapped(ce.publishedText, {c.x, y}, c.w, 20, ui.theme().success).y;
        y += th + ui.s(16);
    } else if (ce.prepared) {
        const CampaignPipelineReport& rep = ce.prepared->report;
        ui.label("Étapes de la campagne (comme tools/levelgen) :", {c.x, y}, 19, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(34);
        line(rep.gatesAdded > 0 ? "Sceaux : " + std::to_string(rep.gatesAdded) + (rep.gatesAdded > 1 ? " portes ajoutées" : " porte ajoutée") +
                                      " contre les raccourcis qui évitent un checkpoint"
                                : std::string("Sceaux : aucun raccourci ne contourne un checkpoint"),
             ui.theme().text);
        if (level_.powerSlots() > 0)
            line("Stations de pouvoirs : " + std::to_string(rep.stations) + (rep.stations > 1 ? " stations, " : " station, ") + std::to_string(rep.tablets) +
                     (rep.tablets > 1 ? " tablettes" : " tablette") + " (replacées sur le parcours modifié)",
                 ui.theme().text);
        else
            line("Stations de pouvoirs : ce niveau n'a pas d'emplacements de pouvoirs", ui.theme().textDim);
        char note[64];
        std::snprintf(note, sizeof note, "%.1f", static_cast<double>(rep.rating.score));
        std::string n = note;
        std::replace(n.begin(), n.end(), '.', ',');
        line("Note de difficulté : " + n + " / 5", ui.theme().text);
        for (size_t i = 0; i < rep.notes.size() && i < 2; ++i) line("À vérifier : " + rep.notes[i], ui.theme().warm, 16);
        y += ui.s(6);
    }

    // The required choice about the records (also while preparing: it can be made first).
    if (ce.step <= 3) {
        ui.label("Les records (choix obligatoire) :", {c.x, y}, 19, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(34);
        const std::string base = std::to_string(ce.baseVersion);
        const std::string opt[2] = {"Réinitialiser les records", "Garder les records"};
        const std::string desc[2] = {
            "Nouvelle révision avec ses propres classements. Les records actuels restent archivés avec la révision " + base + ", jamais mélangés.",
            "Seulement pour un changement sans effet sur le jeu (décor…) : la nouvelle révision est déclarée compatible, les meilleurs temps, "
            "classements, records actuels et progressions de la course à la vérification (EXTRÊME) y sont reportés.",
        };
        const float optH = ui.s(46);
        for (int i = 0; i < 2; ++i) {
            float dh = ui.text().measureWrapped(desc[i], FontFace::Body, ui.s(16), c.w - ui.s(24)).y;
            if (y + optH + ui.s(4) + dh > footY - ui.s(40)) break;  // never under the buttons
            UIContext::ButtonStyle os;
            os.selected = ce.records == i;
            os.align = Align::Left;
            os.enabled = ce.step != 3;
            if (ui.button(i == 0 ? "precords_reset" : "precords_keep", {c.x, y, c.w, optH}, (ce.records == i ? "● " : "○ ") + opt[i], os)) ce.records = i;
            y += optH + ui.s(4);
            ui.labelWrapped(desc[i], {c.x + ui.s(24), y}, c.w - ui.s(24), 16, ui.theme().textDim);
            y += dh + ui.s(12);
        }
        const std::string journal = "La publication est inscrite au journal : votre pseudo, le niveau, la révision et ce choix.";
        if (y + ui.s(24) < footY - ui.s(8)) {
            float js = ui.fitFont(journal, FontFace::Body, 15, c.w, 0.8f);
            ui.label(ui.elide(journal, FontFace::Body, js, c.w), {c.x, y}, js, ui.theme().textDim, Align::Left, FontFace::Body);
        }
    }

    // Buttons.
    const float cw = ui.s(170);
    if (ce.step == 4 || ce.step == 5) {
        if (ui.button("pclose", {c.right() - cw, footY, cw, bh}, "Fermer")) modal_ = Modal::None;
        if (ce.step == 5) {
            UIContext::ButtonStyle rs;
            rs.primary = true;
            const std::string again = "Réessayer";
            float rw = ui.text().measure(again, FontFace::Bold, ui.s(20)).x + ui.s(48);
            if (ui.button("pretry", {c.right() - cw - ui.s(14) - rw, footY, rw, bh}, again, rs)) openPublish();
        }
        return;
    }
    const std::string go = ce.step == 3 ? std::string("Envoi…")
                                        : "Publier la révision" + (ce.nextVersion > 0 ? " " + std::to_string(ce.nextVersion) : std::string());
    UIContext::ButtonStyle ps;
    ps.primary = true;
    ps.enabled = ce.step == 2 && ce.records >= 0;
    float gw = std::min(c.w - cw - ui.s(14), ui.text().measure(go, FontFace::Bold, ui.s(20)).x + ui.s(56));
    if (ui.button("pgo", {c.right() - gw, footY, gw, bh}, go, ps)) sendPublish();
    UIContext::ButtonStyle cs;
    cs.enabled = ce.step != 3;
    if (ui.button("pcancel", {c.right() - gw - ui.s(14) - cw, footY, cw, bh}, "Annuler", cs)) modal_ = Modal::None;
    if (ce.step == 2 && ce.records < 0) {
        const std::string hint = "Choisissez d'abord ce que deviennent les records.";
        float room = c.w - gw - cw - ui.s(40);
        if (room > ui.s(120)) {
            float hs = ui.fitFont(hint, FontFace::Body, 16, room, 0.7f);
            ui.label(ui.elide(hint, FontFace::Body, hs, room), {c.x, footY + bh * 0.5f}, hs, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        }
    }
}

}  // namespace cosmic
