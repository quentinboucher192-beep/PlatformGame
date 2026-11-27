#include "game/tas/TasProgress.h"

#include <algorithm>

namespace cosmic {

namespace {

struct PhaseName {
    TasPhase phase;
    const char* id;
};
constexpr PhaseName kPhases[] = {{TasPhase::Starting, "starting"},
                                 {TasPhase::Searching, "searching"},
                                 {TasPhase::Verifying, "verifying"},
                                 {TasPhase::Stopped, "stopped"},
                                 {TasPhase::Failed, "failed"}};

}  // namespace

const char* tasPhaseId(TasPhase p) {
    for (const PhaseName& n : kPhases)
        if (n.phase == p) return n.id;
    return "starting";
}

bool tasPhaseFromId(const std::string& id, TasPhase& out) {
    for (const PhaseName& n : kPhases)
        if (id == n.id) {
            out = n.phase;
            return true;
        }
    return false;
}

Json TasProgress::toJson() const {
    return {{"format", kTasProgressFormat},
            {"phase", tasPhaseId(phase)},
            {"level", levelId},
            {"category", categoryId(category)},
            {"elapsedMs", elapsedMs},
            {"explored", explored},
            {"seedMs", seedMs},
            {"bestMs", bestMs},
            {"candidateMs", candidateMs},
            {"improvements", improvements},
            {"round", round},
            {"threads", threads},
            {"sequence", sequence},
            {"message", message}};
}

std::optional<TasProgress> TasProgress::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<TasProgress> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("pas un objet JSON");
    if (jstr(j, "format") != kTasProgressFormat) return fail("format inconnu");
    TasProgress p;
    if (!tasPhaseFromId(jstr(j, "phase"), p.phase)) return fail("phase inconnue");
    p.levelId = jstr(j, "level");
    if (p.levelId.empty()) return fail("niveau absent");
    if (!categoryFromId(jstr(j, "category"), p.category)) return fail("catégorie inconnue");
    auto count = [&](const char* k) { return std::max<int64_t>(0, jget<int64_t>(j, k, 0)); };
    p.elapsedMs = count("elapsedMs");
    p.explored = count("explored");
    p.seedMs = count("seedMs");
    p.bestMs = count("bestMs");
    p.candidateMs = count("candidateMs");
    p.improvements = std::max(0, jget<int>(j, "improvements", 0));
    p.round = std::max(0, jget<int>(j, "round", 0));
    p.threads = std::max(0, jget<int>(j, "threads", 0));
    p.sequence = count("sequence");
    p.message = jstr(j, "message");
    if (p.message.size() > 400) {
        size_t n = 400;  // never in the middle of a UTF-8 character
        while (n > 0 && (static_cast<unsigned char>(p.message[n]) & 0xC0) == 0x80) --n;
        p.message.resize(n);
    }
    return p;
}

std::optional<TasProgress> TasProgress::parse(const std::string& text, std::string* error) {
    Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "JSON invalide";
        return std::nullopt;
    }
    return fromJson(j, error);
}

}  // namespace cosmic
