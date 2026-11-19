// 2.75.4: no text left in the other language (docs/I18N.md, « Vérifier qu'il ne reste rien »).
// The audit of the text renderer (i18n::startAudit, --i18n-audit <file>): what it notes in English
// (a text absent from the dictionary, left French although some of its words are not English, a
// French part put in a model's slot, a translation handed back to the renderer) and in French (an
// English text); the key screens drawn in English with nothing noted (main menu, settings, raid
// sheets, wardrobe, notifications, statistics, a raid's HUD with « Ce qu'il reste à faire »); the
// texts the Cosmic Online server writes (notifications, the titan's alerts, errors) translated by
// their models; the installer's language and the setting.

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/online/HttpTransport.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

std::string describe(const std::vector<i18n::AuditFinding>& found, size_t max = 25) {
    std::string out;
    for (size_t i = 0; i < found.size() && i < max; ++i) {
        out += "  [" + found[i].reason + "] " + found[i].text;
        if (!found[i].part.empty()) out += "  (partie : " + found[i].part + ")";
        out += "\n      → " + found[i].shown;
        out += "\n";
    }
    return out;
}

/// A Cosmic Online of its own (never the real server): a session, and the notifications and the
/// titan's alerts written as server/src/notifications.ts and raidMap.ts write them.
class AuditServer final : public IHttpTransport {
public:
    HttpResponse perform(const HttpRequest& rq) override {
        const std::string path = rq.url.substr(rq.url.find("/api/"));
        if (path == "/api/v1/auth/login" || path == "/api/v1/auth/refresh") {
            Json session = {{"access_token", "access-audit-0000000000000000"}, {"access_expires_at", "2099-01-01T00:00:00.000Z"},
                            {"refresh_token", "refresh-audit-000000000000000"}, {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}};
            return ok({{"player", player()}, {"session", session}});
        }
        if (path == "/api/v1/auth/me") return ok({{"player", player()}});
        if (path.rfind("/api/v1/notifications?", 0) == 0) {
            Json list = Json::array();
            const char* texts[] = {
                "Nova vient de publier un nouveau niveau : Sky",
                "Rappel : Asterion (raid court) commence dans quelques instants",
                "[STAR] participe au raid commun Asterion (raid standard) : inscrivez-vous",
                "Bienvenue dans le clan [STAR] Star : votre candidature est acceptée",
                "Défi du jour du 20/11 : vous avez gagné « Aurore »",
            };
            int i = 0;
            for (const char* t : texts) {
                Json n = {{"id", "n" + std::to_string(i)}, {"kind", i % 2 ? "raid" : "clan"}, {"text", t}, {"count", 1},
                          {"created_at", "2026-11-19T10:00:00.000Z"}, {"read", false}, {"data", Json::object()}};
                list.push_back(n);
                ++i;
            }
            return ok({{"notifications", list}, {"unread", static_cast<int>(list.size())}});
        }
        if (path.find("/titan") != std::string::npos) {
            Json feed = Json::array();
            feed.push_back({{"at", "2026-11-19T10:00:00.000Z"}, {"kind", "zone"}, {"branch", "bras_droit"}, {"branch_name", "BRAS DROIT"},
                            {"by", "[STAR] 1"}, {"text", "Zone terminée : Biceps de basalte"}});
            feed.push_back({{"at", "2026-11-19T09:58:00.000Z"}, {"kind", "wipe"}, {"branch", "bras_droit"}, {"branch_name", "BRAS DROIT"},
                            {"by", "[STAR] 2"}, {"text", "Groupe éliminé : Veine de magma (zone à reprendre)"}});
            Json event = {{"id", "evt1"}, {"state", "en_cours"}, {"kind", "monde"}};
            Json branches = Json::array();
            branches.push_back({{"part", "bras_droit"}, {"name", "BRAS DROIT"}, {"state", "attaque"}, {"zones", 8}, {"zones_controlled", 3},
                                {"groups", 2}, {"gigaboss", "Pyroclast"}, {"sealed", false}, {"gigaboss_down", false}});
            branches.push_back({{"part", "crane"}, {"name", "CRÂNE"}, {"state", "inconnu"}, {"zones", 8}, {"zones_controlled", 0},
                                {"groups", 0}, {"gigaboss", "Skullmaw"}, {"sealed", true}, {"gigaboss_down", false}});
            Json needs = Json::array();
            needs.push_back({{"branch", "bras_droit"}, {"name", "Veine de magma"}, {"groups", 1}, {"required", 2}});
            Json totals = {{"zones_controlled", 3}, {"zones", 56}, {"objectives_done", 4}, {"objectives", 60}, {"groups", 2},
                           {"players", 7}, {"clans", 1}, {"gigaboss_defeated", 0}, {"gigaboss", 7}, {"links_on", 1}, {"links", 12}};
            return ok({{"event", event}, {"feed", feed}, {"branches", branches}, {"needs", needs}, {"clans", Json::array()},
                       {"links", Json::array()}, {"totals", totals}, {"my", {{"registered", true}, {"clan_tag", "STAR"}}}});
        }
        return ok(Json::object());
    }
    const char* name() const override { return "audit"; }

private:
    static Json player() { return {{"id", "11111111-1111-4111-8111-111111111111"}, {"username", "Nova"}, {"country_code", "FR"}}; }
    static HttpResponse ok(const Json& data) {
        HttpResponse r;
        r.status = 200;
        r.body = Json{{"ok", true}, {"data", data}}.dump();
        r.headers["content-type"] = "application/json";
        return r;
    }
};

/// A headless game in English (no window shown), the audit on.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    explicit Rig(Language l = Language::English) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = 1280;
        cfg.height = 720;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        app.onUpdate = [this](float dt) { g->online.update(dt); };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = true;
        g->settings.language = i18n::code(l);
        g->applySettings();
        i18n::startAudit();
        app.pushScene(makeMainMenu(*g));
        app.runFrames(4);
    }
    ~Rig() {
        i18n::stopAudit();
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
        app.onUpdate = nullptr;
        i18n::setLanguage(Language::French);
    }
    /// `scene` over the menu for a few frames, then closed.
    void show(std::unique_ptr<Scene> scene, int frames = 6) {
        if (!scene) return;
        const size_t before = app.sceneCount();
        app.pushScene(std::move(scene));
        app.runFrames(frames);
        close(before);
    }
    /// Back to `count` scenes (a pop is applied at the next frame: one per scene above).
    void close(size_t count) {
        for (size_t n = app.sceneCount(); n > count; --n) app.popScene();
        app.runFrames(1);
    }
};

}  // namespace

TEST_CASE("i18n audit: what it notes, in English and in French; off, nothing") {
    i18n::setLanguage(Language::English);
    CHECK(!i18n::auditing());
    i18n::startAudit();
    CHECK(i18n::auditing());
    CHECK(i18n::tr("Jouer") == "Play");
    CHECK(i18n::tr("Paramètres") == "Settings");
    CHECK(i18n::tr("12 / 72") == "12 / 72");
    CHECK(i18n::auditFindings().empty());
    // A text unknown to the dictionary: left in French.
    (void)i18n::tr("Le zorglub mange des fléchettes bleues");
    // A known model, a French part in its slot (« Succès débloqué : {0} »).
    (void)i18n::tr("Succès débloqué : Forêt zorglubienne");
    // Already English (a pseudonym such as « Nova », a translation shown again): nothing.
    (void)i18n::tr("Level 3");
    (void)i18n::tr("Options");
    {
        i18n::Verbatim theirs;  // a player's own words are never judged
        (void)i18n::tr("Bonjour tout le monde, je suis le zorglub");
    }
    const auto found = i18n::auditFindings();
    std::fprintf(stderr, "%s", describe(found).c_str());
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[0].reason == "absent");
        CHECK(found[0].text == "Le zorglub mange des fléchettes bleues");
        CHECK(found[1].reason == "inséré");
        CHECK(found[1].part == "Forêt zorglubienne");
    }
    // Each text judged once (the cache).
    (void)i18n::tr("Le zorglub mange des fléchettes bleues");
    CHECK(i18n::auditFindings().size() == 2);
    i18n::stopAudit();
    CHECK(!i18n::auditing());

    // French: an English text of the dictionary shown as it is.
    i18n::setLanguage(Language::French);
    i18n::startAudit();
    (void)i18n::tr("Paramètres");
    (void)i18n::tr("Asterion");
    (void)i18n::tr("Settings");
    const auto fr = i18n::auditFindings();
    CHECK(fr.size() == 1);
    if (!fr.empty()) {
        CHECK(fr[0].reason == "anglais");
        CHECK(fr[0].text == "Settings");
    }
    i18n::stopAudit();
}

TEST_CASE("i18n audit: the texts the server writes, translated by their models") {
    i18n::setLanguage(Language::English);
    i18n::startAudit();
    // Notifications (server/src/notifications.ts: sentence, raidSentence, clanSentence, securitySentence,
    // rewardSentence), with what their helpers insert (« Asterion (raid court) », « votre clan »…).
    const char* notifications[] = {
        "Nova vient de publier un nouveau niveau : Sky",
        "3 réponses à votre commentaire sur Sky",
        "Nova vous invite à jouer en équipe : Sky",
        "Inscriptions ouvertes : Asterion (raid court), événement mondial",
        "Événement mondial annoncé : Asterion (raid légendaire)",
        "Rappel : Asterion (raid standard) commence dans 10 min",
        "Rappel : Asterion (raid standard) commence dans quelques instants",
        "Nova a inscrit [STAR] à Asterion (raid court) : inscrivez-vous",
        "Nova a inscrit votre clan à Asterion (raid court) : inscrivez-vous",
        "[STAR] participe au raid commun Asterion (raid court) : inscrivez-vous",
        "C'est l'heure : Asterion (raid court) commence, rejoignez votre groupe",
        "3 membres se sont inscrits à Asterion (raid court)",
        "Raid annulé : Asterion (raid court)",
        "Le Cœur d'Asterion s'ouvre : les sept gigaboss sont tombés, rejoignez le combat final de Asterion (raid court)",
        "Résultats officiels de Asterion (raid court) : Asterion est tombé (progression mondiale 100 %)",
        "Session de rattrapage ouverte : Asterion (raid court) pendant 7 jour(s), récompenses argentées",
        "Bienvenue dans le clan [STAR] Star : votre candidature est acceptée",
        "Du nouveau dans votre clan [STAR] Star",
        "Connexion refusée depuis un autre ordinateur (France) : quelqu'un a saisi votre mot de passe.",
        "Votre compte est maintenant lié à cet ordinateur (« Mon ordinateur ») : il ne s'utilise plus ailleurs.",
        "Un joueur vous suit",
    };
    // The titan's alerts (raidMap.ts: feed; RaidTitanScene translates the branch and the text, the group's
    // tag as it is), a branch's « Ce qu'il reste à faire », a gigaboss's words, errors.
    const char* others[] = {
        "BRAS DROIT",
        "Zone terminée : Biceps de basalte",
        "Groupe éliminé : Veine de magma (zone à reprendre)",
        "Zone reprise par un groupe de renfort : Épaule",
        "Coude-forge : Détruire le soufflet ouest (un groupe) (0 / 2) · Détruire le soufflet est (un autre groupe) (0 / 2)",
        "« Skullmaw ouvre les yeux : son regard traque chacun de vous. »",
        "Record mondial battu de plus de 20 % : vérification manuelle.",
        "Pseudonyme ou mot de passe incorrect.",
        "Mot de passe incorrect.",
        "Aucun replay pour ce record.",
        "Inscrivez-vous au raid pour y combattre.",
        "Version 2.75.4 :\n• Raids : le titan tombe.",
    };
    for (const char* t : notifications) (void)i18n::tr(t);
    for (const char* t : others) (void)i18n::tr(t);
    std::vector<i18n::AuditFinding> wrong;
    for (const auto& f : i18n::auditFindings()) {
        // The players' and clans' own names put in a slot stay as they are.
        if (f.reason == "inséré" && (f.part.find("Sky") != std::string::npos || f.part.find("STAR") != std::string::npos ||
                                     f.part.find("Nova") != std::string::npos || f.part.find("Star") != std::string::npos))
            continue;
        // A changelog line of a version this test does not know.
        if (f.text.find("le titan tombe") != std::string::npos) continue;
        wrong.push_back(f);
    }
    std::fprintf(stderr, "%s", describe(wrong).c_str());
    CHECK(wrong.empty());
    i18n::stopAudit();
    i18n::setLanguage(Language::French);
}

TEST_CASE("i18n audit: the key screens in English, nothing left in French") {
    Rig rig(Language::English);
    GameServices& g = *rig.g;
    for (int tab = 0; tab < 5; ++tab) rig.show(makeSettingsScene(g, false, tab));
    rig.show(makeRaidSheets(g, "asterion", 0));
    rig.show(makeRaidSheets(g, "asterion", 1));
    rig.show(makeRaidSheets(g, "skullmaw", 0));
    rig.show(makeRaidSheets(g, "skullmaw", 2));
    rig.show(makeCollection(g, 0));
    rig.show(makeNotifications(g, 0));
    rig.show(makeStatistics(g, 0));
    rig.show(makeAchievements(g));
    rig.show(makePlanetMap(g));
    RaidTestSetup t;
    t.branch = "bras_droit";
    t.zone = 1;
    t.players = 2;
    t.capture = true;
    rig.show(makeRaidTest(g, t), 60);
    const auto found = i18n::auditFindings();
    std::fprintf(stderr, "%s", describe(found, 60).c_str());
    CHECK(found.empty());
}

TEST_CASE("i18n audit: the online screens in English, with what the server writes") {
    Rig rig(Language::English);
    GameServices& g = *rig.g;
    const fs::path dir = fs::temp_directory_path() / "cosmic_i18n_audit_online";
    std::error_code ec;
    fs::remove_all(dir, ec);
    OnlineSettings os = g.settings.online;
    os.enabled = true;
    g.online.init(os, "http://127.0.0.1:9", std::make_unique<AuditServer>(), dir);
    bool done = false;
    g.online.login("Nova", "audit password", false, [&](const ApiResult&) { done = true; });
    for (int i = 0; i < 300 && !done; ++i) {
        g.online.update(0.016f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(g.online.loggedIn());
    auto settle = [&](std::unique_ptr<Scene> scene) {
        const size_t before = rig.app.sceneCount();
        rig.app.pushScene(std::move(scene));
        for (int i = 0; i < 40; ++i) {
            rig.app.runFrames(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        rig.close(before);
    };
    settle(makeNotifications(g, 1));
    settle(makeRaidTitan(g, "evt1"));
    std::vector<i18n::AuditFinding> wrong;
    for (const auto& f : i18n::auditFindings()) {
        const bool theirs = f.part.find("Sky") != std::string::npos || f.part.find("STAR") != std::string::npos ||
                            f.part.find("Nova") != std::string::npos || f.part.find("Star") != std::string::npos ||
                            f.part.find("Aurore") != std::string::npos;
        if (!(f.reason == "inséré" && theirs)) wrong.push_back(f);
    }
    std::fprintf(stderr, "%s", describe(wrong, 60).c_str());
    CHECK(wrong.empty());
    g.online.shutdown();
    fs::remove_all(dir, ec);
}

TEST_CASE("i18n audit: the installer's « en » gives a game in English from its first start; the setting changes it at once") {
    // As main.cpp does at start: the settings of a first start, then language.ini (installer).
    fs::path dir = fs::temp_directory_path() / "cosmic_i18n_audit_ini";
    fs::create_directories(dir);
    FileUtil::writeTextAtomic(dir / "language.ini", "[Language]\r\nLanguage=english\r\n");
    Settings first;
    CHECK(applyInstallerLanguage(first, dir / "language.ini"));
    i18n::setLanguage(settingsLanguage(first));
    CHECK(i18n::language() == Language::English);
    CHECK(i18n::tr("Jouer") == "Play");
    fs::remove_all(dir);
    {
        Rig rig(Language::English);
        CHECK(rig.app.sceneCount() == 1);
        CHECK(i18n::auditFindings().empty());  // the main menu: all English
        // Paramètres › Jeu › « Langue / Language »: applied at once, nothing to restart.
        rig.g->settings.language = "fr";
        rig.g->applySettings();
        CHECK(i18n::language() == Language::French);
        CHECK(i18n::tr("Jouer") == "Jouer");
        rig.g->settings.language = "en";
        rig.g->applySettings();
        CHECK(i18n::tr("Jouer") == "Play");
    }
    i18n::setLanguage(Language::French);
}
