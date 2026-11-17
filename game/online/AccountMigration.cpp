#include "game/online/AccountMigration.h"

#include <algorithm>
#include <cctype>

#include "game/online/OnlineService.h"
#include "packaging/Sha256.h"

namespace cosmic {

// ------------------------------------------------------------------ what the server offers

void MigrationState::update(OnlineService& online, float dt) {
    const std::string key = online.usable() && online.loggedIn() && !online.playerId().empty()
                                ? online.config().serverUrl + "|" + online.playerId()
                                : std::string();
    if (key != key_) {
        // Another account, another server, or logged out: nothing known any more.
        key_ = key;
        available_ = answered_ = busy_ = false;
        wait_ = 0.0f;
        restore_ = Json();
        ++generation_;
    }
    if (key_.empty() || answered_ || busy_) return;
    wait_ = std::max(0.0f, wait_ - dt);
    if (wait_ > 0.0f || online.state() == OnlineState::Offline) return;
    busy_ = true;
    const int gen = generation_;
    OnlineService* svc = &online;
    online.get("/api/v1/players/me/migration", OnlineService::guard(life_, [this, gen, svc](const ApiResult& r) {
                   if (gen != generation_) return;
                   busy_ = false;
                   if (r.ok) {
                       available_ = answered_ = true;
                       refreshRestore(*svc);
                   } else if (r.status == 404 || r.status == 405) {
                       answered_ = true;  // a server older than 2.50: the route does not exist
                   } else {
                       wait_ = 60.0f;  // network, server error, session: asked again later
                   }
               }),
               false);
}

void MigrationState::setPendingRestore(const Json& restore) { restore_ = restore.is_object() ? restore : Json(); }

void MigrationState::refreshRestore(OnlineService& online, std::function<void()> then) {
    if (!available_) {
        if (then) then();
        return;
    }
    const int gen = generation_;
    online.get("/api/v1/players/me/progress-restore", OnlineService::guard(life_, [this, gen, then](const ApiResult& r) {
                   if (gen == generation_ && r.ok) setPendingRestore(jfield(r.data, "restore", Json()));
                   if (then) then();
               }),
               false);
}

// ------------------------------------------------------------------ administration

std::string migrationProblemText(const std::string& code) {
    if (code == "SOURCE_CLOSED") return "Le compte source est fermé (ses données ont déjà été migrées).";
    if (code == "SOURCE_INACTIVE") return "Le compte source est fermé ou suspendu.";
    if (code == "TARGET_NOT_FOUND") return "Compte cible introuvable.";
    if (code == "SAME_ACCOUNT") return "Le compte cible est le compte source.";
    if (code == "TARGET_INACTIVE") return "Le compte cible est fermé ou suspendu.";
    if (code == "LAST_ADMIN") return "C'est le dernier compte admin : il ne peut pas être fermé.";
    if (code == "OWN_ACCOUNT") return "Vous ne pouvez pas fermer votre propre compte.";
    return "Migration refusée par le serveur.";
}

std::vector<std::string> migrationBlockingProblems(const Json& problems, bool closeSource) {
    std::vector<std::string> out;
    if (!problems.is_array()) return out;
    for (const Json& p : problems) {
        if (!p.is_string()) continue;
        const std::string code = p.get<std::string>();
        if (code == "LAST_ADMIN" && !closeSource) continue;
        if (std::find(out.begin(), out.end(), code) == out.end()) out.push_back(code);
    }
    return out;
}

// ------------------------------------------------------------------ the progression to restore

RestoreDownload readRestoreDownload(const HttpResponse& r) {
    RestoreDownload out;
    if (r.networkFailure()) {
        out.error = "Cosmic Online ne répond pas : réessayez plus tard.";
        return out;
    }
    if (r.status != 200) {
        // Errors come in the API's envelope, with the server's French message.
        const ApiResult e = parseApiResponse(r);
        out.gone = r.status == 404;
        if (!e.ok && e.code != "MALFORMED_RESPONSE" && !e.message.empty()) out.error = e.message;
        else if (out.gone) out.error = "Aucune progression n'attend plus d'être restaurée.";
        else out.error = "Cosmic Online ne répond pas : réessayez plus tard (HTTP " + std::to_string(r.status) + ").";
        return out;
    }
    out.sha256 = Sha256::hex(r.body);
    std::string expected = r.header("x-progress-sha256");
    std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!expected.empty() && expected != out.sha256) {
        out.error = "Le fichier reçu est incomplet ou abîmé : réessayez.";
        return out;
    }
    const Json j = Json::parse(r.body, nullptr, false);
    const bool formatOk = j.is_object() && (!j.contains("format") || jstr(j, "format") == "cosmic.progress_copy");
    if (!formatOk || !j.contains("files") || !j["files"].is_object()) {
        out.error = "Copie de la progression illisible.";
        return out;
    }
    out.files = j["files"];
    out.gameVersion = jstr(j, "game_version");
    out.ok = true;
    return out;
}

}  // namespace cosmic
