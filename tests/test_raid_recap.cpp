// 2.78.1: the « fiche récapitulative » of a finished raid (game/online/RaidRecap, ui/screens/RaidRecapScene):
// the server's answers read as they are (no figure computed by the game, « — » for what the server does not
// follow yet), the texts of the header, the figures and the timeline, and the screen's layout: at 1280 × 720
// and 1920 × 1080, in French and in English, every text holds in its rect (never under 11.5 pixels), nothing
// overlaps (texts, portraits, relics, avatars, previews), every element stays in its panel; the screen itself
// draws the gigaboss's sprites, the titan, the backdrop of Asterion's fall and the rewards' textures.
//
// The answers below are TEST FIXTURES (names « Essai… », clan [ESSAI]): nothing here is shipped with the game.

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/online/RaidRecap.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/RaidRecapScene.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}
bool inside(const Rect& in, const Rect& out) {
    return in.x >= out.x - 0.5f && in.y >= out.y - 0.5f && in.right() <= out.right() + 0.5f && in.bottom() <= out.bottom() + 0.5f;
}
bool empty(const Rect& r) { return r.w <= 0.0f || r.h <= 0.0f; }

/// TEST FIXTURE: GET /clans/{id}/raids.
Json fixtureList() {
    return Json::parse(R"({"raids": [
        {"id": "e0000000-0000-4000-8000-000000000001", "kind": "world", "format": "standard", "starts_at": "2026-11-20T20:00:00.000Z",
         "ends_at": "2026-11-20T23:00:00.000Z", "end_at": "2026-11-20T22:47:00.000Z", "duration_s": 10020, "heart_fallen": true, "final": true,
         "parts_down": 7, "parts_total": 7},
        {"id": "e0000000-0000-4000-8000-000000000002", "kind": "clan", "format": "legendaire", "starts_at": "2026-11-13T20:00:00.000Z",
         "ends_at": "2026-11-13T23:00:00.000Z", "end_at": "2026-11-13T23:00:00.000Z", "duration_s": 10800, "heart_fallen": false, "final": true,
         "parts_down": 3, "parts_total": 7},
        {"id": "e0000000-0000-4000-8000-000000000003", "kind": "clan", "format": "court", "starts_at": "2026-11-06T20:00:00.000Z",
         "ends_at": "2026-11-06T23:30:00.000Z", "end_at": "2026-11-06T23:12:00.000Z", "duration_s": 11520, "heart_fallen": true, "final": true,
         "parts_down": 7, "parts_total": 7}]})");
}

/// TEST FIXTURE: GET /clans/{id}/raids/{event}/recap of a won raid (shaped as server/src/raidRecap.ts answers).
Json fixtureWon() {
    Json j = Json::parse(R"({
      "clan": {"id": "c0000000-0000-4000-8000-0000000000aa", "tag": "ESSAI", "name": "Clan de test", "emblem": {"shape": "star", "primary": "#ffd27a", "secondary": "#a66bff"}},
      "event": {"id": "e0000000-0000-4000-8000-000000000001", "kind": "world", "raid": "asterion", "format": "standard", "catchup": false,
                "starts_at": "2026-11-20T20:00:00.000Z", "ends_at": "2026-11-20T23:00:00.000Z", "end_at": "2026-11-20T22:47:00.000Z",
                "duration_s": 10020, "heart_fallen": true, "fallen_at": "2026-11-20T22:47:00.000Z", "final": false},
      "pending": {"tables": 2, "objectives": 0},
      "reviews": {"average": 4.3, "count": 17},
      "world": {"my_clan_rank": 2, "clans": 23},
      "people": {"registered": 26, "players": 23, "groups": 3, "reinforcements": 2},
      "figures": {"parts_down": 7, "parts_total": 7, "zones_done": 56, "zones_total": 56, "zones_by_clan": 31, "relics": 4, "relics_total": 7,
                  "revives": 38, "deaths": 41, "wiped": 1, "retaken": 1},
      "parts": [
        {"part": "crane", "name": "CRÂNE", "boss": "Skullmaw", "fallen": true, "at": "2026-11-20T20:58:00.000Z", "relic": true, "phase": 4,
         "by": {"clan_tag": "ESSAI", "group": 1, "reinforcement": false, "mine": true, "me": true}},
        {"part": "bras_gauche", "name": "BRAS GAUCHE", "boss": "Dreadfang", "fallen": true, "at": "2026-11-20T21:10:00.000Z", "relic": false, "phase": 4,
         "by": {"clan_tag": "ESSAI", "group": 2, "reinforcement": false, "mine": true, "me": false}},
        {"part": "bras_droit", "name": "BRAS DROIT", "boss": "Pyroclast", "fallen": true, "at": "2026-11-20T21:24:00.000Z", "relic": true, "phase": 4,
         "by": {"clan_tag": "AUTRE", "group": 3, "reinforcement": false, "mine": false, "me": false}},
        {"part": "torse", "name": "TORSE", "boss": "Heartbreaker", "fallen": true, "at": "2026-11-20T21:41:00.000Z", "relic": true, "phase": 4,
         "by": {"clan_tag": "ESSAI", "group": 1, "reinforcement": false, "mine": true, "me": true}},
        {"part": "abdomen", "name": "ABDOMEN", "boss": "Voidworm", "fallen": true, "at": "2026-11-20T21:52:00.000Z", "relic": false, "phase": 4,
         "by": {"clan_tag": "ESSAI", "group": 4, "reinforcement": true, "mine": true, "me": false}},
        {"part": "jambe_gauche", "name": "JAMBE GAUCHE", "boss": "Graviton", "fallen": true, "at": "2026-11-20T22:05:00.000Z", "relic": true, "phase": 4,
         "by": {"clan_tag": "ESSAI", "group": 3, "reinforcement": false, "mine": true, "me": false}},
        {"part": "jambe_droite", "name": "JAMBE DROITE", "boss": "Titanus", "fallen": true, "at": "2026-11-20T22:19:00.000Z", "relic": false, "phase": 4,
         "by": {"clan_tag": "ESSAI", "group": 1, "reinforcement": false, "mine": true, "me": true}}],
      "heart": {"part": "coeur", "name": "CŒUR", "boss": "Cœur d'Asterion", "fallen": true, "open": true, "at": "2026-11-20T22:47:00.000Z", "phase": 4,
                "taken": 10000, "groups": 12, "clan_groups": 3, "clan_share": 0.34, "relic": false,
                "by": {"clan_tag": "ESSAI", "group": 1, "reinforcement": false, "mine": true, "me": true}},
      "best": {
        "gigaboss_damage": {"player": {"id": "p1", "username": "EssaiKaëlis", "avatar": "avatar_03", "me": false}, "value": 4200, "share": 0.18},
        "heart_damage": {"player": {"id": "p0", "username": "EssaiMoi", "avatar": "avatar_05", "me": true}, "value": 900, "share": 0.09},
        "support": {"player": {"id": "p2", "username": "EssaiOrion", "avatar": "avatar_07", "me": false}, "value": 2340, "share": 0},
        "revives": {"player": {"id": "p3", "username": "EssaiLyra", "avatar": "avatar_09", "me": false}, "value": 11, "share": 0},
        "mechanisms": {"player": {"id": "p4", "username": "EssaiVegaAuNomTrèsLongDeTest", "avatar": "avatar_11", "me": false}, "value": 27, "share": 0},
        "objectives": {"player": {"id": "p1", "username": "EssaiKaëlis", "avatar": "avatar_03", "me": false}, "value": 19, "share": 0},
        "fewest_deaths": {"player": {"id": "p2", "username": "EssaiOrion", "avatar": "avatar_07", "me": false}, "value": 0, "share": 0}},
      "rewards": [
        {"reward": "coeur_badge", "name": "Badge « Cœur d'Asterion »", "kind": "badge", "ref": "communaute_coeur", "players": 23, "mine": true, "gigaboss": false},
        {"reward": "coeur_clan", "name": "Titre de clan « Briseurs d'étoile »", "kind": "item", "ref": "title_briseurs_etoile", "players": 23, "mine": true, "gigaboss": false},
        {"reward": "asterion_ecorce", "name": "Combinaison « Écorce d'Asterion »", "kind": "item", "ref": "suit_ecorce_asterion", "players": 21, "mine": true, "gigaboss": false},
        {"reward": "coeur_supernova", "name": "Effet de victoire « Supernova d'Asterion »", "kind": "item", "ref": "victory_supernova_asterion", "players": 17, "mine": true, "gigaboss": false},
        {"reward": "coeur_decor", "name": "Cadre de profil « Cœur d'Asterion »", "kind": "item", "ref": "frame_coeur_asterion", "players": 4, "mine": false, "gigaboss": false},
        {"reward": "skullmaw_badge", "name": "Badge « Regard de Skullmaw »", "kind": "badge", "ref": "communaute_skullmaw", "players": 8, "mine": true, "gigaboss": true},
        {"reward": "dreadfang_badge", "name": "Badge « Griffes de Dreadfang »", "kind": "badge", "ref": "communaute_dreadfang", "players": 8, "mine": false, "gigaboss": true},
        {"reward": "pyroclast_badge", "name": "Badge « Main de Pyroclast »", "kind": "badge", "ref": "communaute_pyroclast", "players": 6, "mine": false, "gigaboss": true},
        {"reward": "heartbreaker_badge", "name": "Badge « Cage de Heartbreaker »", "kind": "badge", "ref": "communaute_heartbreaker", "players": 8, "mine": true, "gigaboss": true},
        {"reward": "voidworm_badge", "name": "Badge « Ventre de Voidworm »", "kind": "badge", "ref": "communaute_voidworm", "players": 7, "mine": false, "gigaboss": true},
        {"reward": "graviton_badge", "name": "Badge « Pied de Graviton »", "kind": "badge", "ref": "communaute_graviton", "players": 8, "mine": false, "gigaboss": true},
        {"reward": "titanus_badge", "name": "Badge « Talon de Titanus »", "kind": "badge", "ref": "communaute_titanus", "players": 8, "mine": true, "gigaboss": true}],
      "titles": [{"title": "Briseurs d'étoile", "rank": 1}, {"title": "Dévoreurs dévorés", "rank": 2}],
      "me": {"played": true, "groups": [{"clan_tag": "ESSAI", "group": 1, "part": "crane", "reinforcement": false, "wiped": false, "mine": true},
                                         {"clan_tag": "ESSAI", "group": 1, "part": "coeur", "reinforcement": false, "wiped": false, "mine": true}],
             "parts": ["crane", "torse", "jambe_droite"], "zones": 24, "revives": 5, "deaths": 3, "heart_share": 0.09, "heart_rank": 1,
             "rewards": ["coeur_badge"]},
      "contributions": {
        "zone_tables": [{"branch": "asterion_crane", "zone": 8, "group": "[ESSAI]·1", "tick": 120000, "accepted_at": "2026-11-20T20:58:00.000Z",
                         "share_of": "boss", "rows": [
          {"username": "EssaiMoi", "seat": 0, "me": true, "share": 0.6, "boss_damage": 900, "damage": 1500, "support": 4, "mechanisms": 3,
           "objectives": 2, "mini_damage": 100, "deaths": 1, "revives": 2, "revived": 1},
          {"username": "EssaiKaëlis", "seat": 1, "me": false, "share": 0.4, "boss_damage": 600, "damage": 1100, "support": 1, "mechanisms": 5,
           "objectives": 1, "mini_damage": 50, "deaths": 0, "revives": 0, "revived": 1}]}],
        "raid_totals": [
          {"username": "EssaiMoi", "me": true, "zones": 24, "boss_damage": 5200, "damage": 9100, "support": 40, "mechanisms": 30, "objectives": 12,
           "mini_damage": 800, "deaths": 3, "revives": 5, "revived": 2},
          {"username": "EssaiKaëlis", "me": false, "zones": 22, "boss_damage": 7400, "damage": 11000, "support": 12, "mechanisms": 18, "objectives": 19,
           "mini_damage": 900, "deaths": 5, "revives": 1, "revived": 4}]}
    })");
    return j;
}

/// TEST FIXTURE: a raid that did not win (three parts, the heart sealed), that I did not play.
Json fixtureLost() {
    Json j = fixtureWon();
    j["event"]["id"] = "e0000000-0000-4000-8000-000000000002";
    j["event"]["kind"] = "clan";
    j["event"]["format"] = "legendaire";
    j["event"]["heart_fallen"] = false;
    j["event"]["fallen_at"] = nullptr;
    j["event"]["final"] = true;
    j["event"]["end_at"] = "2026-11-20T23:00:00.000Z";
    j["event"]["duration_s"] = 10800;
    j["pending"] = {{"tables", 0}, {"objectives", 0}};
    j["reviews"] = {{"average", nullptr}, {"count", 0}};
    j["world"] = nullptr;
    j["figures"]["parts_down"] = 3;
    j["figures"]["zones_done"] = 29;
    j["figures"]["relics"] = 1;
    j["figures"]["wiped"] = 0;
    j["figures"]["retaken"] = 0;
    for (size_t i = 3; i < 7; ++i) {
        j["parts"][i]["fallen"] = false;
        j["parts"][i]["at"] = nullptr;
        j["parts"][i]["by"] = nullptr;
        j["parts"][i]["relic"] = false;
        j["parts"][i]["phase"] = i == 3 ? 2 : 0;
    }
    j["heart"] = {{"part", "coeur"}, {"fallen", false}, {"open", false}, {"phase", 0}, {"taken", 0}, {"groups", 0}, {"clan_groups", 0}, {"clan_share", 0}};
    j["rewards"] = Json::array();
    j["titles"] = Json::array();
    j["best"]["heart_damage"] = {{"player", nullptr}, {"value", 0}, {"share", 0}};
    j["me"] = {{"played", false}, {"groups", Json::array()}, {"parts", Json::array()}, {"zones", 0}, {"revives", 0}, {"deaths", nullptr}, {"heart_share", 0}, {"heart_rank", 0}};
    // An event of before 2.78: no table counts the deaths (« — », the category not shown), no table of contributions.
    j["figures"]["deaths"] = nullptr;
    j["best"]["fewest_deaths"] = nullptr;
    j["contributions"] = {{"zone_tables", Json::array()}, {"raid_totals", Json::array()}};
    return j;
}

/// A headless game (no window shown) over its main menu.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig(int w = 1280, int h = 720) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = w;
        cfg.height = h;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = true;  // its first viewing is its own test (test_asterion_film)
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~Rig() {
        app.shutdown();
        app.onSceneRender = nullptr;
    }
    void resize(int w, int h) {
        app.setWindowSize(w, h);
        app.renderer().refreshOutputSize();
        app.runFrames(1);
    }
};

/// Every text in its rect, never under 11.5 pixels; nothing over anything else; each element in its panel.
void checkLayout(UIContext& ui, const RaidRecapLayout& L, const char* what) {
    const auto texts = L.texts();
    for (const RecapText* t : texts) {
        i18n::Verbatim v(t->verbatim);
        const float w = ui.text().measure(t->text, t->face, t->px).x;
        if (w > t->r.w + 0.5f || t->px < 11.4f || t->px > t->r.h + 0.5f)
            std::printf("  %s : « %s » %.1f px dans %.1f × %.1f\n", what, t->text.c_str(), t->px, t->r.w, t->r.h);
        CHECK(w <= t->r.w + 0.5f);
        CHECK(t->px >= 11.4f);
        CHECK(t->px <= t->r.h + 0.5f);
        CHECK(inside(t->r, L.screen));
    }
    for (size_t i = 0; i < texts.size(); ++i)
        for (size_t j = i + 1; j < texts.size(); ++j) {
            if (overlaps(texts[i]->r, texts[j]->r))
                std::printf("  %s : « %s » et « %s » se chevauchent\n", what, texts[i]->text.c_str(), texts[j]->text.c_str());
            CHECK(!overlaps(texts[i]->r, texts[j]->r));
        }
    // The panels: in the screen, none over another.
    std::vector<Rect> panels = {L.back, L.list, L.titanCard, L.header, L.timeline, L.best, L.rewards, L.me};
    for (const auto& c : L.cards) panels.push_back(c.r);
    for (const auto& b : L.buttons) panels.push_back(b.r);
    if (!L.hint.text.empty()) panels.push_back(L.hint.r);
    for (size_t i = 0; i < panels.size(); ++i) {
        CHECK(inside(panels[i], L.screen));
        for (size_t j = i + 1; j < panels.size(); ++j) {
            if (overlaps(panels[i], panels[j])) std::printf("  %s : panneaux %zu et %zu se chevauchent\n", what, i, j);
            CHECK(!overlaps(panels[i], panels[j]));
        }
    }
    CHECK(!overlaps(L.crumb.r, L.back));
    // Each element in its panel, the art never over a text.
    std::vector<Rect> art = {L.emblem, L.titanBox};
    auto in = [&](const RecapText& t, const Rect& p) {
        if (!t.text.empty()) CHECK(inside(t.r, p));
    };
    in(L.title, L.header);
    in(L.eventLine, L.header);
    in(L.clanLine, L.header);
    in(L.pending, L.header);
    if (!empty(L.ratingStars)) {
        CHECK(inside(L.ratingStars, L.aside) && inside(L.ratingStars, L.header));
        art.push_back(L.ratingStars);
    }
    in(L.ratingCaption, L.aside);
    in(L.ratingText, L.aside);
    CHECK(inside(L.emblem, L.header));
    for (const RecapText& t : L.titanCaption) in(t, L.titanCard);
    CHECK(inside(L.titanBox, L.titanCard));
    for (const auto& r : L.rows) {
        if (r.r.bottom() > L.list.bottom() + 0.5f) continue;
        in(r.title, r.r);
        in(r.line, r.r);
        CHECK(inside(r.mark, r.r));
        art.push_back(r.mark);
    }
    for (const auto& c : L.cards) {
        in(c.value, c.r);
        in(c.caption, c.r);
        if (!empty(c.icon)) {
            CHECK(inside(c.icon, c.r));
            art.push_back(c.icon);
        }
    }
    in(L.timelineTitle, L.timeline);
    in(L.timelineSub, L.timeline);
    for (const auto& r : L.trows) {
        CHECK(inside(r.r, L.timeline));
        in(r.name, r.r);
        in(r.boss, r.r);
        in(r.label, r.r);
        in(r.label2, r.r);
        CHECK(inside(r.portrait, r.r));
        CHECK(inside(r.track, r.r));
        CHECK(r.track.w > 0.0f);
        art.push_back(r.portrait);
        art.push_back(r.track);
        if (!empty(r.relic)) {
            CHECK(inside(r.relic, r.r));
            art.push_back(r.relic);
        }
    }
    for (const RecapText& t : L.ticks) in(t, L.timeline);
    in(L.bestTitle, L.best);
    in(L.bestPending, L.best);
    for (const auto& r : L.brows) {
        CHECK(inside(r.r, L.best));
        in(r.label, r.r);
        in(r.name, r.r);
        in(r.value, r.r);
        if (!empty(r.avatar)) {
            CHECK(inside(r.avatar, r.r));
            art.push_back(r.avatar);
        }
    }
    in(L.rewardsTitle, L.rewards);
    in(L.rewardsPage, L.rewards);
    in(L.rewardsNote, L.rewards);
    in(L.rewardsEmpty, L.rewards);
    for (const auto& t : L.tiles) {
        CHECK(inside(t.r, L.rewards));
        CHECK(inside(t.preview, t.r));
        in(t.kind, t.r);
        in(t.name, t.r);
        in(t.count, t.r);
        art.push_back(t.preview);
    }
    if (!empty(L.prev)) {
        CHECK(inside(L.prev, L.rewards) && inside(L.next, L.rewards));
        art.push_back(L.prev);
        art.push_back(L.next);
    }
    in(L.meTitle, L.me);
    in(L.mePending, L.me);
    for (const auto& l : L.mlines) {
        in(l.label, L.me);
        in(l.value, L.me);
    }
    for (const RecapText& t : L.meNote) in(t, L.me);
    for (const auto& b : L.buttons) in(b.text, b.r);
    for (const Rect& a : art)
        for (const RecapText* t : texts) {
            if (overlaps(a, t->r)) std::printf("  %s : « %s » sous une image\n", what, t->text.c_str());
            CHECK(!overlaps(a, t->r));
        }
}

}  // namespace

TEST_CASE("raid recap: the server's answers read as they are, nothing computed by the game") {
    const raids::Recap r = raids::parseRecap(fixtureWon());
    CHECK(r.valid);
    CHECK(r.clanTag == "ESSAI");
    CHECK(r.heartFallen && !r.final);
    CHECK(raids::recapPending(r));
    CHECK(r.parts.size() == 7);
    CHECK(r.heart.fallen && r.heart.groups == 12);
    CHECK(r.deaths == 41 && r.me.deaths == 3);  // 2.78: the deaths the zones' tables count
    CHECK(r.contributions.zoneTables.size() == 1 && r.contributions.raidTotals.size() == 2 && r.contributions.raidTotals[0].v2);
    CHECK(r.rating.known && r.rating.count == 17 && raidreviews::summaryText(r.rating) == "4,3 · 17 avis");  // 2.77.4: the players' reviews
    CHECK(raids::recapTitle(r) == "ASTERION EST TOMBÉ");
    CHECK(raids::recapClanLine(r) == "Clan [ESSAI] · 23 joueurs · 3 groupes · 2 renforts · 2e sur 23 clans");
    CHECK(raids::recapPendingText(r) == "2 résultats en attente de validation");
    CHECK(raids::recapEventLine(r).find(" · durée 2 h 47 min") != std::string::npos);
    const auto figs = raids::recapFigures(r);
    CHECK(figs.size() == 7);
    CHECK(figs[0].value == "7 / 7" && figs[1].value == "Vaincu" && figs[2].value == "56 / 56" && figs[3].value == "4 / 7");
    CHECK(figs[4].value == "41" && figs[5].value == "38" && figs[6].value == "1 · 1 repris");
    // The timeline's labels: my clan's group, another clan's, a group of reinforcement, the heart.
    CHECK(raids::recapPartLabel(r.parts[0], false, 0).second == "Groupe 1");
    CHECK(raids::recapPartLabel(r.parts[0], false, 0).first.size() == 5);  // « 21:58 », in the computer's zone
    CHECK(raids::recapPartLabel(r.parts[2], false, 0).second == "[AUTRE] groupe 3");
    CHECK(raids::recapPartLabel(r.parts[4], false, 0).second == "Groupe 4 (renfort)");
    CHECK(raids::recapPartLabel(r.heart, true, r.heart.groups).second == "12 groupes");
    // The bests: a share of the clan's damage, a count; the category the server does not follow is not shown.
    CHECK(r.best.size() == raids::recapCategories().size());
    CHECK(raids::recapBestValue(r.best[0]) == "18 %");
    CHECK(raids::recapBestValue(r.best[2]) == "2 340");
    CHECK(r.best[6].followed && r.best[6].has && raids::recapBestValue(r.best[6]) == "0");
    CHECK(r.titles.size() == 2 && r.titles[1] == "Dévoreurs dévorés");
    const auto mine = raids::recapMyLines(r, {{"crane", "Crâne"}, {"torse", "Torse"}, {"jambe_droite", "Jambe droite"}});
    CHECK(mine.size() == 5);
    CHECK(mine[0].second == "1" && mine[1].second == "Crâne, Torse, Jambe droite");
    CHECK(mine[3].second == "3 / 5" && mine[4].second == "9 % (1er du clan)");
    // A raid that did not win: how far it got; a raid I did not play: no line of mine.
    const raids::Recap lost = raids::parseRecap(fixtureLost());
    CHECK(raids::recapTitle(lost) == "RAID TERMINÉ");
    CHECK(raids::recapPendingText(lost).empty());
    CHECK(raids::recapEventLine(lost).find(" · temps écoulé (3 h 0 min)") != std::string::npos);
    CHECK(raids::recapPartLabel(lost.parts[3], false, 0) == std::make_pair(std::string("debout"), std::string("phase 2 / 3")));
    CHECK(raids::recapPartLabel(lost.parts[5], false, 0).first == "jamais affronté");
    CHECK(raids::recapPartLabel(lost.heart, true, 0).first == "scellé");
    CHECK(raids::recapFigures(lost)[1].value == "Scellé");
    CHECK(raids::recapMyLines(lost, {}).empty());
    CHECK(lost.deaths == -1 && raids::recapFigures(lost)[4].value == "—" && !lost.best[6].followed);
    // The list.
    const auto list = raids::parseRecapList(fixtureList());
    CHECK(list.size() == 3);
    CHECK(raids::recapEntryLine(list[0]) == "Asterion vaincu · 2 h 47 min");
    CHECK(raids::recapEntryLine(list[1]) == "3 / 7 parties · temps écoulé");
    CHECK(raids::durationText(59) == "moins d'une minute");
    CHECK(raids::durationText(47 * 60) == "47 min");
    // A broken answer is not a recap.
    CHECK(!raids::parseRecap(Json::parse(R"({"clan": {"id": "x"}})")).valid);
    CHECK(raids::parseRecapList(Json::parse(R"({"raids": [{"id": ""}]})")).empty());
}

TEST_CASE("raid recap: the layout holds its texts at 1280 x 720 and 1920 x 1080, in French and in English, nothing overlapping") {
    Rig rig;
    GameServices& g = *rig.g;
    const auto list = raids::parseRecapList(fixtureList());
    const raids::Recap won = raids::parseRecap(fixtureWon()), lost = raids::parseRecap(fixtureLost());
    for (auto size : {std::pair<int, int>{1280, 720}, std::pair<int, int>{1920, 1080}, std::pair<int, int>{1600, 900}})
        for (Language lang : {Language::French, Language::English}) {
            rig.resize(size.first, size.second);
            i18n::setLanguage(lang);
            g.ui.begin(rig.app, 1.0f / 60.0f);
            char what[96];
            for (int page = 0; page < 2; ++page) {
                RaidRecapOptions opt;
                opt.rewardPage = page;
                opt.contributions = true;
                opt.rateLabel = page == 0 ? "Noter ce raid" : "Modifier mon avis";  // 2.77.4: the server lets this player review it
                std::snprintf(what, sizeof what, "%dx%d %s gagné p%d", size.first, size.second, lang == Language::French ? "fr" : "en", page);
                const RaidRecapLayout L = layoutRaidRecap(g.ui, won, list, opt);
                checkLayout(g.ui, L, what);
                CHECK(L.cards.size() == 7);
                CHECK(L.trows.size() == 8);
                CHECK(L.brows.size() == 7);  // « Le moins de morts » too (2.78: the zones' tables count the deaths)
                CHECK(L.rewardPages >= 1 && !L.tiles.empty());
                CHECK(L.buttons.size() == 5);  // the contributions, the titan's map, the victory film, the event's sheet, « Noter ce raid »
                CHECK(!L.ratingText.text.empty() && !empty(L.ratingStars));
                CHECK(L.ticks.size() >= 2);
                CHECK(L.mlines.size() == 5);
                // A clan's tag is the players' word: never translated (« ESSAI » is a French word too).
                CHECK(L.clanLine.text.find("[ESSAI]") != std::string::npos);
                CHECK(L.trows[2].label2.text.find("[AUTRE]") != std::string::npos);
                // Each fall on its track, in the order of the time.
                for (size_t i = 0; i + 1 < 7; ++i) CHECK(L.trows[i].mark >= 0.0f && L.trows[i].mark <= L.trows[i + 1].mark + 0.5f);
            }
            std::snprintf(what, sizeof what, "%dx%d %s perdu", size.first, size.second, lang == Language::French ? "fr" : "en");
            RaidRecapOptions opt;
            const RaidRecapLayout L = layoutRaidRecap(g.ui, lost, list, opt);
            checkLayout(g.ui, L, what);
            CHECK(L.buttons.size() == 2);  // no victory film for a raid whose heart stood
            CHECK(L.tiles.empty() && !L.rewardsEmpty.text.empty());
            CHECK(L.brows.size() == 6);  // an event of before 2.78: no « Le moins de morts »
            CHECK(L.mlines.empty() && !L.meNote.empty());
            CHECK(L.trows[3].mark < 0.0f);
        }
    i18n::setLanguage(Language::French);
    rig.resize(1280, 720);
}

TEST_CASE("raid recap: the screen draws the gigaboss's sprites, the titan, the fall's backdrop and the rewards' textures") {
    Rig rig;
    GameServices& g = *rig.g;
    for (auto size : {std::pair<int, int>{1280, 720}, std::pair<int, int>{1920, 1080}}) {
        rig.resize(size.first, size.second);
        rig.app.pushScene(makeRaidRecapFrom(g, fixtureList(), fixtureWon()));
        rig.app.runFrames(3);
        const RaidRecapView v = raidRecapView(rig.app.topScene());
        CHECK(v.valid && v.loaded);
        CHECK(v.rows == 3 && v.cards == 7 && v.timelineRows == 8 && v.bestRows == 7);
        CHECK(v.portraits == 8);  // the seven gigaboss and the heart, each with its own sprite
        CHECK(v.titan && v.backdrop);
        CHECK(v.tiles >= 4 && v.previews == v.tiles);  // every reward drawn with its texture
        CHECK(v.buttons == 2);  // a fixture: the contributions and the film (no titan's map nor sheet: no event on a server)
        CHECK(!v.contributions);
        rig.app.popScene();
        rig.app.runFrames(1);
        rig.app.pushScene(makeRaidRecapFrom(g, fixtureList(), fixtureLost()));
        rig.app.runFrames(2);
        const RaidRecapView w = raidRecapView(rig.app.topScene());
        CHECK(w.loaded && w.tiles == 0 && w.buttons == 0 && w.portraits == 8);
        rig.app.popScene();
        rig.app.runFrames(1);
        // 2.78: its « Tableau des contributions » over it (the clan's tables, « Raid entier » first).
        rig.app.pushScene(makeRaidRecapFrom(g, fixtureList(), fixtureWon(), true));
        rig.app.runFrames(3);
        CHECK(raidRecapView(rig.app.topScene()).contributions);
        rig.app.popScene();
        rig.app.runFrames(1);
    }
    rig.resize(1280, 720);
}

TEST_CASE("raid recap: in English, nothing left in French (the audit of the texts drawn)") {
    Rig rig;
    GameServices& g = *rig.g;
    i18n::setLanguage(Language::English);
    i18n::startAudit();
    for (const Json& recap : {fixtureWon(), fixtureLost()}) {
        rig.app.pushScene(makeRaidRecapFrom(g, fixtureList(), recap));
        rig.app.runFrames(4);
        rig.app.popScene();
        rig.app.runFrames(1);
    }
    const auto found = i18n::auditFindings();
    i18n::stopAudit();
    i18n::setLanguage(Language::French);
    for (const auto& f : found) std::printf("  [%s] %s → %s (partie : %s)\n", f.reason.c_str(), f.text.c_str(), f.shown.c_str(), f.part.c_str());
    CHECK(found.empty());
}

TEST_CASE("raid recap: a capture at 1920 x 1080, on demand (COSMIC_RECAP_SHOT=<file.png>[,<file in English>])") {
    const char* shot = std::getenv("COSMIC_RECAP_SHOT");
    if (!shot) return;  // nothing to capture: the other cases check the screen
    std::string fr = shot, en;
    if (const size_t comma = fr.find(','); comma != std::string::npos) {
        en = fr.substr(comma + 1);
        fr = fr.substr(0, comma);
    }
    Rig rig(1920, 1080);
    GameServices& g = *rig.g;
    for (int k = 0; k < 2; ++k) {
        const std::string& file = k == 0 ? fr : en;
        if (file.empty()) continue;
        i18n::setLanguage(k == 1 ? Language::English : Language::French);
        rig.app.pushScene(makeRaidRecapFrom(g, fixtureList(), fixtureWon()));
        rig.app.runFrames(20);
        rig.app.requestScreenshot(file);
        rig.app.runFrames(2);
        CHECK(raidRecapView(rig.app.topScene()).loaded);
        rig.app.popScene();
        rig.app.runFrames(1);
    }
    i18n::setLanguage(Language::French);
}
