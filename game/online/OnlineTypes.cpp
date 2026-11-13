#include "game/online/OnlineTypes.h"

#include <cstdio>

namespace cosmic {

const char* onlineStateLabel(OnlineState s) {
    switch (s) {
        case OnlineState::Disabled: return "Désactivé";
        case OnlineState::NotConfigured: return "Serveur non configuré";
        case OnlineState::Unsupported: return "Réseau indisponible";
        case OnlineState::Idle: return "Hors connexion";
        case OnlineState::Connecting: return "Connexion…";
        case OnlineState::Online: return "En ligne";
        case OnlineState::Offline: return "Hors ligne";
    }
    return "";
}

const char* categoryId(RunCategory c) {
    switch (c) {
        case RunCategory::AnyPercent: return "ANY_PERCENT";
        case RunCategory::HundredPercent: return "HUNDRED_PERCENT";
        case RunCategory::NoDeath: return "NO_DEATH";
        case RunCategory::NoDamage: return "NO_DAMAGE";
        case RunCategory::Speedrun: return "SPEEDRUN";
    }
    return "ANY_PERCENT";
}

const char* categoryLabel(RunCategory c) {
    switch (c) {
        case RunCategory::AnyPercent: return "ANY%";
        case RunCategory::HundredPercent: return "100%";
        case RunCategory::NoDeath: return "NO DEATH";
        case RunCategory::NoDamage: return "NO DAMAGE";
        case RunCategory::Speedrun: return "SPEEDRUN";
    }
    return "ANY%";
}

const char* categoryRule(RunCategory c) {
    switch (c) {
        case RunCategory::AnyPercent: return "Terminer le niveau le plus vite possible.";
        case RunCategory::HundredPercent: return "Terminer avec tous les éclats et reliques du niveau.";
        case RunCategory::NoDeath: return "Terminer sans mourir.";
        case RunCategory::NoDamage: return "Terminer sans subir le moindre dégât.";
        case RunCategory::Speedrun: return "Run d'une traite depuis le début, replay obligatoire et vérifié.";
    }
    return "";
}

bool categoryFromId(const std::string& id, RunCategory& out) {
    for (RunCategory c : kAllCategories)
        if (id == categoryId(c)) {
            out = c;
            return true;
        }
    return false;
}

const char* recordStatusId(RecordStatus s) {
    switch (s) {
        case RecordStatus::Local: return "LOCAL";
        case RecordStatus::Pending: return "PENDING";
        case RecordStatus::Verified: return "VERIFIED";
        case RecordStatus::Rejected: return "REJECTED";
    }
    return "LOCAL";
}

const char* recordStatusLabel(RecordStatus s) {
    switch (s) {
        case RecordStatus::Local: return "LOCAL";
        case RecordStatus::Pending: return "EN ATTENTE";
        case RecordStatus::Verified: return "VÉRIFIÉ";
        case RecordStatus::Rejected: return "REFUSÉ";
    }
    return "LOCAL";
}

RecordStatus recordStatusFromServer(const std::string& s) {
    if (s == "VERIFIED") return RecordStatus::Verified;
    if (s == "REJECTED") return RecordStatus::Rejected;
    if (s == "PENDING") return RecordStatus::Pending;
    return RecordStatus::Local;
}

ApiResult parseApiResponse(const HttpResponse& r) {
    ApiResult out;
    out.status = r.status;
    out.net = r.error;
    if (r.networkFailure()) {
        out.code = "NETWORK";
        out.message = std::string("Réseau : ") + netErrorLabel(r.error);
        return out;
    }
    Json j = Json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("ok") || !j["ok"].is_boolean()) {
        out.code = "MALFORMED_RESPONSE";
        out.message = "Réponse du serveur illisible (HTTP " + std::to_string(r.status) + ").";
        return out;
    }
    if (j["ok"].get<bool>()) {
        if (r.status < 200 || r.status >= 300 || !j.contains("data")) {
            out.code = "MALFORMED_RESPONSE";
            out.message = "Réponse du serveur incohérente.";
            return out;
        }
        out.ok = true;
        out.data = j["data"];
        if (j.contains("meta")) out.meta = j["meta"];
        return out;
    }
    const Json& e = j.contains("error") && j["error"].is_object() ? j["error"] : Json::object();
    out.code = jstr(e, "code", "UNKNOWN_ERROR");
    out.message = jstr(e, "message", "Erreur du serveur.");
    if (e.contains("details")) out.details = e["details"];
    return out;
}

std::string formatRunTime(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t minutes = ms / 60000;
    int64_t seconds = (ms / 1000) % 60;
    int64_t millis = ms % 1000;
    char buf[32];
    if (minutes >= 60)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%03lld", static_cast<long long>(minutes / 60), static_cast<long long>(minutes % 60),
                      static_cast<long long>(seconds), static_cast<long long>(millis));
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld.%03lld", static_cast<long long>(minutes), static_cast<long long>(seconds),
                      static_cast<long long>(millis));
    return buf;
}

}  // namespace cosmic
