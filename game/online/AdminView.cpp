#include "game/online/AdminView.h"

#include <algorithm>

namespace cosmic::adminview {

std::string lockLabel(const std::string& lock) {
    if (lock == "profile_private") return "profil masqué";
    if (lock == "account_inactive") return "compte inactif";
    if (lock == "presence_hidden") return "présence masquée";
    if (lock == "follows_private") return "abonnements privés";
    if (lock == "replay_pending") return "en attente de vérification";
    if (lock == "replay_rejected") return "refusé à la vérification";
    if (lock == "levels_unpublished") return "niveaux non publiés";
    return {};
}

std::vector<std::string> lockLabels(const Json& locks) {
    std::vector<std::string> out;
    auto add = [&out](const Json& l) {
        if (!l.is_string()) return;
        const std::string w = lockLabel(l.get<std::string>());
        if (!w.empty() && std::find(out.begin(), out.end(), w) == out.end()) out.push_back(w);
    };
    if (locks.is_array())
        for (const Json& l : locks) add(l);
    else
        add(locks);
    return out;
}

std::string joinLabels(const std::vector<std::string>& labels) {
    std::string out;
    for (const std::string& l : labels) out += (out.empty() ? "" : " · ") + l;
    return out;
}

std::string statusLabel(const std::string& status) {
    if (status == "VERIFIED") return "vérifié";
    if (status == "PENDING") return "en attente de vérification";
    if (status == "REJECTED") return "refusé à la vérification";
    return {};
}

std::string statusChip(const std::string& status) {
    if (status == "VERIFIED") return "vérifié";
    if (status == "PENDING") return "en attente";
    if (status == "REJECTED") return "refusé";
    return {};
}

bool countsAsVerified(const std::string& status) { return status == "VERIFIED"; }

namespace {

std::string note(std::vector<std::string> words) {
    if (words.empty()) return {};
    return "Vu en tant qu'admin (" + joinLabels(words) + ")";
}

void addStatus(std::vector<std::string>& words, const std::string& status) {
    if (status.empty() || countsAsVerified(status)) return;
    const std::string w = statusLabel(status);
    if (!w.empty() && std::find(words.begin(), words.end(), w) == words.end()) words.push_back(w);
}

}  // namespace

std::string replayNote(const std::string& lockHeader, const std::string& statusHeader) {
    std::vector<std::string> words;
    // The header may name several locks, separated by commas (« profile_private,account_inactive »).
    size_t i = 0;
    while (i <= lockHeader.size()) {
        size_t j = lockHeader.find(',', i);
        if (j == std::string::npos) j = lockHeader.size();
        std::string one = lockHeader.substr(i, j - i);
        one.erase(0, one.find_first_not_of(' '));
        if (const size_t e = one.find_last_not_of(' '); e != std::string::npos) one.erase(e + 1);
        if (const std::string w = lockLabel(one); !w.empty() && std::find(words.begin(), words.end(), w) == words.end()) words.push_back(w);
        i = j + 1;
    }
    addStatus(words, statusHeader);
    return note(std::move(words));
}

std::string recordNote(const Json& adminView) {
    if (!adminView.is_object()) return {};
    std::vector<std::string> words = lockLabels(adminView.value("locks", Json::array()));
    addStatus(words, jstr(adminView, "status"));
    return note(std::move(words));
}

std::string groupedText(int count, const std::string& lastStamp) {
    if (count <= 1) return {};
    return std::to_string(count) + " consultations, la dernière le " + lastStamp;
}

std::string locationSetting(const std::string& visibility) {
    if (visibility == "globe") return "montré (classements et globe)";
    if (visibility == "country") return "montré (classements)";
    return "masqué par le joueur : caché aussi aux administrateurs";
}

}  // namespace cosmic::adminview
