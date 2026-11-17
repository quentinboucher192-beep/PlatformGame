// 2.62: the team attempts of the « course à la vérification » (Duo / Trio, docs/COOP.md § 6).

#include <cstdio>

#include "game/online/OnlineService.h"

namespace cosmic {

void OnlineService::submitCoopAttempt(RaceAttempt a, const std::string& mode, std::vector<std::string> players, std::vector<uint8_t> replay) {
    if (!loggedIn() || !usable()) return;
    Json body = {{"kind", a.kind},         {"level_id", a.levelId}, {"attempt_id", a.attemptId}, {"time_ms", a.timeMs}, {"finished", a.finished},
                 {"bosses", a.bosses},     {"waves", a.waves},      {"mode", mode},              {"players", players}};
    if (a.kind == "official") body["level_hash"] = a.levelHash;
    else body["version"] = a.version;
    const std::string title = a.title;
    const std::string label = players.size() >= 3 ? "Trio" : "Duo";
    send("POST", "/api/v1/coop/runs", body, [this, title, label, replay = std::move(replay)](const ApiResult& r) mutable {
        if (!r.ok) {
            notify(Notification::Kind::Warning, "Tentative d'équipe non envoyée", r.message);
            return;
        }
        const std::string upload = jstr(r.data, "upload");
        if (upload.rfind("/api/v1/coop/runs/", 0) != 0) return;
        sendBytes("PUT", upload, std::string(replay.begin(), replay.end()), [this, title, label](const ApiResult& u) {
            if (!u.ok) {
                notify(Notification::Kind::Warning, "Tentative d'équipe non envoyée", u.message);
                return;
            }
            const Json& run = u.data.value("run", Json::object());
            if (jstr(run, "status") != "VERIFIED") {
                std::string why;
                for (const auto& x : run.value("reasons", Json::array()))
                    if (x.is_string()) why += (why.empty() ? "" : ", ") + x.get<std::string>();
                notify(Notification::Kind::Warning, "Tentative d'équipe refusée", "Replay non conforme" + (why.empty() ? std::string{} : " : " + why) + ".");
                return;
            }
            char sc[32];
            std::snprintf(sc, sizeof sc, "%.1f", jget<double>(run, "score", 0.0));
            std::string s = sc;
            for (char& c : s)
                if (c == '.') c = ',';
            notify(Notification::Kind::Success, "Tentative d'équipe vérifiée",
                   "« " + title + " » en " + label + " : " + s + " %. Classée dès que chaque joueur l'a confirmée depuis son compte.");
        });
    });
}

void OnlineService::confirmCoopAttempt(const std::string& attemptId) {
    if (!loggedIn() || !usable() || attemptId.size() != 36) return;
    send("POST", "/api/v1/coop/attempts/" + attemptId + "/confirm", Json::object(), [](const ApiResult&) {});
}

}  // namespace cosmic
