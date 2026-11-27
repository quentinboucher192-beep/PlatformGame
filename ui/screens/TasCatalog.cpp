#include "ui/screens/TasCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <system_error>

#include "game/GameServices.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/ReplayCompare.h"
#include "game/online/LevelFingerprint.h"

namespace cosmic::tas {

namespace {

size_t indexOf(RunCategory c) {
    for (size_t i = 0; i < kAllCategories.size(); ++i)
        if (kAllCategories[i] == c) return i;
    return 0;
}

}  // namespace

int64_t TasRun::at(const std::string& checkpoint) const {
    if (checkpoint.empty()) return 0;
    for (const TasSplit& s : splits)
        if (s.checkpoint == checkpoint) return s.timeMs;
    return -1;
}

int64_t TasRun::segment(const std::string& from, const std::string& to) const {
    const int64_t a = at(from);
    const int64_t b = to.empty() ? timeMs : at(to);
    if (a < 0 || b < 0 || b <= a) return -1;
    return b - a;
}

GhostEntry TasRun::ghost() const {
    GhostEntry e;
    e.file = replayFile;
    e.meta.source = "tas";
    e.meta.label = "TAS";
    e.meta.levelId = levelId;
    e.meta.levelHash = levelHash;
    e.meta.category = categoryId(category);
    e.meta.levelVersion = levelVersion;
    e.meta.timeMs = timeMs;
    e.meta.date = computedAt;
    for (const TasSplit& s : splits) e.meta.splits.push_back(static_cast<float>(s.timeMs) / 1000.0f);
    std::error_code ec;
    e.bytes = static_cast<uint64_t>(fs::file_size(replayFile, ec));
    if (ec) e.bytes = 0;
    return e;
}

// ------------------------------------------------------------------ catalog

void TasCatalog::setRoot(fs::path root) {
    root_ = std::move(root);
    rootSet_ = true;
    clear();
}

fs::path TasCatalog::root() const { return rootSet_ ? root_ : tasRoot(); }

void TasCatalog::clear() { levels_.clear(); }

const TasRun* TasCatalog::tasFor(const std::string& levelId, RunCategory c, const std::string& levelHash) {
    if (levelId.empty() || levelHash.empty()) return nullptr;
    auto found = levels_.find(levelId);
    if (found == levels_.end() || found->second.hash != levelHash) {
        // Read once per version of the level (the rule of the hidden TAS is game/tas/TasFile.h's tasFor).
        Level l;
        l.hash = levelHash;
        for (size_t i = 0; i < kAllCategories.size(); ++i)
            if (auto r = cosmic::tasFor(levelId, kAllCategories[i], levelHash, root())) {
                TasRun t;
                static_cast<TasRecord&>(t) = std::move(*r);
                t.replayFile = tasReplayPath(t, root());
                l.runs[i] = std::move(t);
            }
        const std::optional<TasRun>& anyRun = l.runs[indexOf(RunCategory::AnyPercent)];
        for (size_t i = 0; i < kAllCategories.size(); ++i)
            if (kAllCategories[i] != RunCategory::AnyPercent && l.runs[i] && anyRun && l.runs[i]->timeMs == anyRun->timeMs) l.runs[i]->sameAsAny = true;
        found = levels_.insert_or_assign(levelId, std::move(l)).first;
    }
    const std::optional<TasRun>& r = found->second.runs[indexOf(c)];
    return r ? &*r : nullptr;
}

const TasRun* TasCatalog::tasFor(GameServices& g, const std::string& levelId, RunCategory c) {
    if (levelId.empty()) return nullptr;
    // Only the levels shipped with the game have a rankings' fingerprint, hence a TAS.
    const LevelFingerprint* fp = g.online.fingerprints().get(g.content, levelId);
    if (!fp || fp->hash.empty()) return nullptr;
    return tasFor(levelId, c, fp->hash);
}

bool TasCatalog::any(GameServices& g, const std::string& levelId) {
    for (RunCategory c : kAllCategories)
        if (tasFor(g, levelId, c)) return true;
    return false;
}

TasCatalog& catalog() {
    static TasCatalog c;
    return c;
}

// ------------------------------------------------------------------ texts

std::string timeText(int64_t ms) { return formatGhostSeconds(static_cast<float>(ms) / 1000.0f); }

std::string gapText(int64_t ms) { return formatDeltaMs(ms); }

std::string dateText(const std::string& iso) {
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return {};
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (iso[static_cast<size_t>(i)] < '0' || iso[static_cast<size_t>(i)] > '9') return {};
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4);
}

std::string countText(uint64_t n) {
    // « 3,4 » / « 412 »: one decimal under ten (French decimal comma).
    auto number = [](double v) {
        char buf[32];
        if (v >= 10.0) std::snprintf(buf, sizeof buf, "%.0f", v);
        else std::snprintf(buf, sizeof buf, "%.1f", v);
        std::string s = buf;
        std::replace(s.begin(), s.end(), '.', ',');
        return s;
    };
    if (n >= 1000000000ULL) {
        const double b = static_cast<double>(n) / 1e9;
        return b >= 2.0 ? std::format("{} milliards", number(b)) : std::format("{} milliard", number(b));
    }
    if (n >= 1000000ULL) {
        const double m = static_cast<double>(n) / 1e6;
        return m >= 2.0 ? std::format("{} millions", number(m)) : std::format("{} million", number(m));
    }
    // « 12 500 »: a space between the thousands.
    const std::string d = std::to_string(n);
    std::string out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ' ';
        out += d[i];
    }
    return out;
}

float myRecord(GameServices& g, const TasRun& t) {
    if (t.category == RunCategory::AnyPercent) {
        if (const PersonalBests::Run* pb = g.bests.pb(GhostStore::levelKey(t.levelId, t.levelHash)); pb && pb->time > 0.0f) return pb->time;
        // My best run of this version kept as a ghost (before the unified store knew it).
        for (const GhostEntry& e : g.ghosts.localGhosts(t.levelId))
            if (e.meta.source == "pb" && e.meta.levelHash == t.levelHash && e.meta.timeMs > 0) return static_cast<float>(e.meta.timeMs) / 1000.0f;
        return -1.0f;
    }
    // The other categories: my runs of this version verified online in that category.
    int64_t best = 0;
    for (const OnlineAttempt& a : g.online.attempts().all()) {
        if (a.levelId != t.levelId || a.levelHash != t.levelHash || a.timeMs <= 0) continue;
        if (!a.ownerId.empty() && a.ownerId != g.online.playerId()) continue;
        const OnlineSubmission* s = a.submission(t.category);
        if (s && s->status == RecordStatus::Verified && (best == 0 || a.timeMs < best)) best = a.timeMs;
    }
    return best > 0 ? static_cast<float>(best) / 1000.0f : -1.0f;
}

}  // namespace cosmic::tas
