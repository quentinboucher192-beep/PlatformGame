// 2.75.3: what an administrator sees through the privacy locks of a profile (game/online/AdminView):
// the server's words (`admin_view.locks`, the headers of a replay's download) turned into the marks
// « Vu en tant qu'admin », a record or replay not verified never counted as verified, the Journal's
// grouped lines, and a country the player hides staying hidden from the administrators too.

#include <string>

#include "engine/core/Json.h"
#include "game/online/AdminView.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("admin view: the locks the server lifted, in words, known ones only, without duplicates") {
    CHECK(adminview::lockLabel("profile_private") == "profil masqué");
    CHECK(adminview::lockLabel("account_inactive") == "compte inactif");
    CHECK(adminview::lockLabel("presence_hidden") == "présence masquée");
    CHECK(adminview::lockLabel("follows_private") == "abonnements privés");
    CHECK(adminview::lockLabel("replay_pending") == "en attente de vérification");
    CHECK(adminview::lockLabel("replay_rejected") == "refusé à la vérification");
    CHECK(adminview::lockLabel("levels_unpublished") == "niveaux non publiés");
    // A lock a newer server names is never shown as a raw key.
    CHECK(adminview::lockLabel("something_new").empty());
    const Json locks = Json::array({"profile_private", "something_new", "presence_hidden", "profile_private"});
    const auto words = adminview::lockLabels(locks);
    CHECK(words.size() == 2);
    CHECK(adminview::joinLabels(words) == "profil masqué · présence masquée");
    // A single string (the 2.75 `lock` of an old Journal entry) is read too.
    CHECK(adminview::lockLabels(Json("account_inactive")).size() == 1);
    CHECK(adminview::lockLabels(Json()).empty());
    CHECK(adminview::joinLabels({}).empty());
}

TEST_CASE("admin view: only a VERIFIED record or replay counts as verified") {
    CHECK(adminview::countsAsVerified("VERIFIED"));
    CHECK(!adminview::countsAsVerified("PENDING"));
    CHECK(!adminview::countsAsVerified("REJECTED"));
    CHECK(!adminview::countsAsVerified(""));
    CHECK(!adminview::countsAsVerified("verified"));  // the server's words exactly
    CHECK(adminview::statusLabel("PENDING") == "en attente de vérification");
    CHECK(adminview::statusLabel("REJECTED") == "refusé à la vérification");
    CHECK(adminview::statusChip("PENDING") == "en attente");
    CHECK(adminview::statusChip("REJECTED") == "refusé");
    CHECK(adminview::statusChip("UNKNOWN").empty());
}

TEST_CASE("admin view: the mark of a replay comes from the server's headers only") {
    // An ordinary replay (or a server before 2.75.3): no mark.
    CHECK(adminview::replayNote("", "").empty());
    CHECK(adminview::replayNote("", "VERIFIED").empty());
    // 2.75: a privacy lock lifted.
    CHECK(adminview::replayNote("profile_private", "") == "Vu en tant qu'admin (profil masqué)");
    CHECK(adminview::replayNote("account_inactive", "") == "Vu en tant qu'admin (compte inactif)");
    // 2.75.3: a replay not verified yet, or refused, with or without a privacy lock.
    CHECK(adminview::replayNote("", "PENDING") == "Vu en tant qu'admin (en attente de vérification)");
    CHECK(adminview::replayNote("profile_private,account_inactive", "REJECTED") ==
          "Vu en tant qu'admin (profil masqué · compte inactif · refusé à la vérification)");
    // An unknown lock alone says nothing invented.
    CHECK(adminview::replayNote("future_lock", "").empty());
}

TEST_CASE("admin view: a record's admin_view gives the same mark") {
    CHECK(adminview::recordNote(Json()).empty());
    Json av = {{"locks", Json::array({"profile_private"})}, {"status", "VERIFIED"}};
    CHECK(adminview::recordNote(av) == "Vu en tant qu'admin (profil masqué)");
    av["status"] = "PENDING";
    CHECK(adminview::recordNote(av) == "Vu en tant qu'admin (profil masqué · en attente de vérification)");
    // The lock and the status naming the same thing are said once.
    av["locks"] = Json::array({"replay_pending"});
    CHECK(adminview::recordNote(av) == "Vu en tant qu'admin (en attente de vérification)");
    // A public profile's verified record: nothing to mark.
    CHECK(adminview::recordNote(Json{{"locks", Json::array()}, {"status", "VERIFIED"}}).empty());
}

TEST_CASE("admin view: the Journal groups the consultations, and a hidden country stays hidden") {
    CHECK(adminview::groupedText(1, "16/11 · 02:14").empty());
    CHECK(adminview::groupedText(0, "").empty());
    CHECK(adminview::groupedText(3, "16/11 · 02:14") == "3 consultations, la dernière le 16/11 · 02:14");
    // The player's consent on the location: only the setting is told, never the country it hides.
    CHECK(adminview::locationSetting("hidden").find("caché aussi aux administrateurs") != std::string::npos);
    CHECK(adminview::locationSetting("").find("caché aussi aux administrateurs") != std::string::npos);
    CHECK(adminview::locationSetting("country") == "montré (classements)");
    CHECK(adminview::locationSetting("globe") == "montré (classements et globe)");
}
