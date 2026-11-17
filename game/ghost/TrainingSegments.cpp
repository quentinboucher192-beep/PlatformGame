#include "game/ghost/TrainingSegments.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"
#include "game/ghost/Ghost.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/KeptReplays.h"

namespace cosmic {

namespace {

using Ends = std::pair<std::string, std::string>;

/// A checkpoint id or a level key as read back from a file: short, no control character.
bool validId(const std::string& s) {
    if (s.size() > 96) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool validSeconds(float s) { return std::isfinite(s) && s > 0.0f && s <= 7200.0f; }

long long ms(float s) { return std::llround(static_cast<double>(s) * 1000.0); }

/// Every segment of a run that went through `ids` (its splits' checkpoints) into `out`,
/// keeping the shortest one per (from, to).
void noteSegments(std::map<Ends, float>& out, const std::vector<std::string>& ids, const std::vector<float>& segments) {
    if (segments.size() != ids.size() + 1) return;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (!(segments[i] >= 0.0f) || !std::isfinite(segments[i])) continue;
        Ends e{i == 0 ? std::string{} : ids[i - 1], i == ids.size() ? std::string{} : ids[i]};
        auto [it, fresh] = out.try_emplace(e, segments[i]);
        if (!fresh && segments[i] < it->second) it->second = segments[i];
    }
}

float lookup(const std::map<Ends, float>& m, const std::string& from, const std::string& to) {
    auto it = m.find({from, to});
    return it == m.end() ? -1.0f : it->second;
}

}  // namespace

// ------------------------------------------------------------------ matching

int segmentIndexIn(const std::vector<std::string>& checkpoints, const std::string& from, const std::string& to) {
    const int n = static_cast<int>(checkpoints.size());
    // Segment i goes from boundary i to boundary i + 1: boundary 0 is the start, boundary
    // k (1..n) the k-th checkpoint reached, boundary n + 1 the exit.
    int i = -1;
    if (from.empty()) {
        i = 0;
    } else {
        for (int k = 0; k < n; ++k)
            if (checkpoints[static_cast<size_t>(k)] == from) {
                i = k + 1;
                break;
            }
        if (i < 0) return -1;
    }
    const bool toExit = i == n;
    if (to.empty()) return toExit ? i : -1;
    if (toExit || checkpoints[static_cast<size_t>(i)] != to) return -1;
    return i;
}

std::optional<std::vector<std::string>> resolveRunCheckpoints(const std::vector<std::string>& recorded, size_t splitCount,
                                                              const std::vector<std::string>& route) {
    if (recorded.size() == splitCount) return recorded;  // recorded with it (or no split at all)
    if (splitCount == route.size()) return route;        // one split per checkpoint: the route's order
    return std::nullopt;
}

std::vector<std::string> checkpointsOfTrack(const GhostTrack& track, const std::vector<std::pair<std::string, Vec2>>& checkpoints) {
    std::vector<std::string> out;
    for (float s : track.splits()) {
        Vec2 at = track.poseAt(s).feet;
        const std::string* id = nullptr;
        float bd = 320.0f * 320.0f;
        for (const auto& [cid, feet] : checkpoints) {
            float d = (feet - at).lengthSq();
            if (d < bd) {
                bd = d;
                id = &cid;
            }
        }
        if (!id || std::find(out.begin(), out.end(), *id) != out.end()) return {};
        out.push_back(*id);
    }
    return out;
}

float sumOfBest(const std::vector<float>& bestSegments) {
    if (bestSegments.empty()) return -1.0f;
    double sum = 0.0;
    for (float b : bestSegments) {
        if (!(b >= 0.0f) || !std::isfinite(b)) return -1.0f;
        sum += b;
    }
    return static_cast<float>(sum);
}

std::vector<float> withTrainingBests(std::vector<float> segments, const std::vector<std::string>& ids, const TrainingBests& training,
                                     std::vector<bool>* fromTraining) {
    if (fromTraining) fromTraining->assign(segments.size(), false);
    if (segments.size() != ids.size() + 1) return segments;  // the route's segments are not known
    for (size_t i = 0; i < segments.size(); ++i) {
        const std::string from = i == 0 ? std::string{} : ids[i - 1], to = i == ids.size() ? std::string{} : ids[i];
        float t = lookup(training, from, to);
        if (!validSeconds(t)) continue;
        // Compared to the millisecond (what the timer shows): a tie keeps the full run's.
        if (segments[i] < 0.0f || ms(t) < ms(segments[i])) {
            segments[i] = t;
            if (fromTraining) (*fromTraining)[i] = true;
        }
    }
    return segments;
}

float SegmentReferences::bestOf(const std::string& from, const std::string& to) const { return lookup(best, from, to); }
float SegmentReferences::pbOf(const std::string& from, const std::string& to) const { return lookup(pb, from, to); }

SegmentReferences buildSegmentReferences(const std::vector<float>& gold, const std::vector<std::string>& goldCheckpoints,
                                         const std::vector<SegmentReferences::RunInput>& runs, const SegmentReferences::RunInput& pb,
                                         const std::vector<std::string>& route, const TrainingBests& training) {
    SegmentReferences r;
    const bool hasPb = validSeconds(pb.time);
    std::vector<float> pbSegs;
    std::optional<std::vector<std::string>> pbIds;
    if (hasPb) {
        r.pbTime = pb.time;
        pbSegs = segmentDurations(pb.splits, pb.time);
        pbIds = resolveRunCheckpoints(pb.checkpoints, pb.splits.size(), route);
        if (pbIds) {
            noteSegments(r.pb, *pbIds, pbSegs);
            noteSegments(r.best, *pbIds, pbSegs);
        }
    }
    // The gold route (the segment history's best segments), completed with the PB's segments
    // when the PB went through the same checkpoints: the Sum of Best.
    std::vector<float> sob;
    std::optional<std::vector<std::string>> sobIds;  // the checkpoints of the Sum of Best's route
    if (!gold.empty()) {
        const size_t splits = gold.size() - 1;
        auto ids = resolveRunCheckpoints(goldCheckpoints, splits, route);
        if (ids) noteSegments(r.best, *ids, gold);
        sob = gold;
        sobIds = ids;
        if (hasPb && pb.splits.size() == splits && (!ids || !pbIds || *ids == *pbIds)) {
            r.pbOnSobRoute = true;
            if (!sobIds) sobIds = pbIds;
            for (size_t i = 0; i < sob.size() && i < pbSegs.size(); ++i)
                if (sob[i] < 0.0f || pbSegs[i] < sob[i]) sob[i] = pbSegs[i];
        }
    } else if (hasPb) {
        sob = pbSegs;  // one run known: its segments are the best ones
        sobIds = pbIds;
        r.pbOnSobRoute = true;
    } else if (!training.empty()) {
        // No full run yet: the route's segments, from the trainings only.
        sob.assign(route.size() + 1, -1.0f);
        sobIds = route;
    }
    // 2.60.3: « tout confondu », the trainings' best times of the same segments too.
    if (sobIds) sob = withTrainingBests(std::move(sob), *sobIds, training);
    r.sob = sumOfBest(sob);
    r.sobSegments = static_cast<int>(sob.size());
    // Every run kept: the segments it went straight through.
    for (const auto& run : runs) {
        if (!validSeconds(run.time)) continue;
        if (auto ids = resolveRunCheckpoints(run.checkpoints, run.splits.size(), route))
            noteSegments(r.best, *ids, segmentDurations(run.splits, run.time));
    }
    r.anyRun = hasPb || !gold.empty() || !runs.empty();
    return r;
}

// ------------------------------------------------------------------ training records

TrainingRecords::TrainingRecords(fs::path file, fs::path replayDir) : file_(std::move(file)), replayDir_(std::move(replayDir)) {}

void TrainingRecords::load() {
    levels_.clear();
    recovered_ = false;
    readOnly_ = false;
    if (file_.empty()) {
        file_ = Paths::savesDir() / "training.json";
        if (replayDir_.empty()) replayDir_ = Paths::userDir() / "ghosts" / "training";
    }
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;  // no training finished yet
    std::optional<Json> j;
    if (auto text = FileUtil::readText(file_)) {
        try {
            j = Json::parse(*text);
        } catch (const Json::exception&) {
        }
    }
    const int version = j && j->is_object() ? jget<int>(*j, "version", 0) : 0;
    if (!j || !j->is_object() || jstr(*j, "format") != "cosmic.training" || version < 1 || !j->contains("levels") ||
        !(*j)["levels"].is_object()) {
        // Unreadable: set aside (never lost), and the history starts again.
        fs::path bad = file_;
        bad += ".corrupt";
        fs::remove(bad, ec);
        fs::rename(file_, bad, ec);
        recovered_ = true;
        return;
    }
    readOnly_ = version > kVersion;  // a newer game's file: read what is known, never overwritten
    for (const auto& [key, lj] : (*j)["levels"].items()) {
        if (levels_.size() >= kLevelsKept) break;
        if (key.empty() || !validId(key) || !lj.is_object() || !lj.contains("segments") || !lj["segments"].is_array()) continue;
        Level l;
        l.touched = jstr(lj, "touched").substr(0, 32);
        for (const Json& sj : lj["segments"]) {
            if (l.segments.size() >= kSegmentsPerLevel) break;
            if (!sj.is_object() || !sj.contains("from") || !sj.contains("to") || !sj["from"].is_string() || !sj["to"].is_string()) continue;
            std::string from = sj["from"].get<std::string>(), to = sj["to"].get<std::string>();
            Entry e;
            e.best = jget<float>(sj, "best", -1.0f);
            e.attempts = std::clamp(jget<int>(sj, "attempts", 0), 0, 1000000);
            e.date = jstr(sj, "date").substr(0, 32);
            if (sj.contains("powers") && sj["powers"].is_array() && sj["powers"].size() <= kMaxPowers) {
                e.customPowers = true;
                for (const Json& p : sj["powers"]) {
                    std::string id = p.is_string() ? p.get<std::string>() : std::string{};
                    if (id.empty() || !validId(id) || std::find(e.powers.begin(), e.powers.end(), id) != e.powers.end()) {
                        e.customPowers = false;  // damaged: the route's powers
                        e.powers.clear();
                        break;
                    }
                    e.powers.push_back(std::move(id));
                }
            }
            if (!validId(from) || !validId(to)) continue;
            // 2.60.3: the best attempt's replay and its start (dropped when damaged).
            if (validSeconds(e.best) && sj.contains("replay")) {
                std::string name = jstr(sj, "replay");
                auto start = SectionTrainingStart::fromJson(jfield(sj, "start", Json()));
                if (keptreplays::validName(name) && start && start->checkpointId == from) {
                    e.replay = std::move(name);
                    e.start = std::move(*start);
                }
            }
            if (validSeconds(e.best)) {
                e.attempts = std::max(1, e.attempts);
            } else {
                e.best = -1.0f;
                e.attempts = 0;
                if (!e.customPowers) continue;  // nothing left to keep
            }
            l.segments[{from, to}] = std::move(e);
        }
        if (!l.segments.empty()) levels_[key] = std::move(l);
    }
}

bool TrainingRecords::save() {
    if (file_.empty() || readOnly_) return false;
    Json levels = Json::object();
    for (const auto& [key, l] : levels_) {
        Json segs = Json::array();
        for (const auto& [ends, e] : l.segments) {
            Json sj{{"from", ends.first}, {"to", ends.second}, {"best", e.best}, {"attempts", e.attempts}, {"date", e.date}};
            if (e.customPowers) sj["powers"] = e.powers;
            if (!e.replay.empty()) {
                sj["replay"] = e.replay;
                sj["start"] = e.start.toJson();
            }
            segs.push_back(std::move(sj));
        }
        levels[key] = {{"touched", l.touched}, {"segments", segs}};
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    // Version 1 still: the replays (2.60.3) are optional fields older games ignore.
    Json root{{"format", "cosmic.training"}, {"version", kVersion}, {"levels", levels}};
    bool ok = FileUtil::writeTextAtomic(file_, root.dump(1, ' ', false, Json::error_handler_t::replace) + "\n");
    // Only once the file that names them is written: a replay is never deleted while it is named.
    if (ok) collectReplays();
    return ok;
}

fs::path TrainingRecords::replayPath(const std::string& levelKey, const std::string& replay) const {
    fs::path dir = keptreplays::levelDir(replayDir_, levelKey);
    if (dir.empty() || !keptreplays::validName(replay)) return {};
    return dir / replay;
}

void TrainingRecords::collectReplays() {
    if (replayDir_.empty()) return;
    std::map<std::string, std::set<std::string>> used;
    std::map<std::string, std::string> keyOf;  // folder → level key
    for (const auto& [key, l] : levels_) {
        std::string dir = keptreplays::safeName(key, 96);
        if (dir.empty()) continue;
        keyOf[dir] = key;
        for (const auto& [ends, e] : l.segments)
            if (!e.replay.empty()) used[dir].insert(e.replay);
    }
    auto sizes = keptreplays::collect(replayDir_, used);
    // Bounded: beyond kMaxReplayBytes, the least recently trained levels lose their replays.
    uint64_t total = 0;
    for (const auto& sz : sizes) total += sz.second;
    if (total <= kMaxReplayBytes) return;
    auto level = [&](const std::string& dir) {
        auto k = keyOf.find(dir);
        return k == keyOf.end() ? levels_.end() : levels_.find(k->second);
    };
    std::sort(sizes.begin(), sizes.end(), [&](const auto& a, const auto& b) {
        auto la = level(a.first), lb = level(b.first);
        return (la == levels_.end() ? std::string{} : la->second.touched) < (lb == levels_.end() ? std::string{} : lb->second.touched);
    });
    bool dropped = false;
    for (const auto& [dir, bytes] : sizes) {
        if (total <= kMaxReplayBytes) break;
        if (auto it = level(dir); it != levels_.end())
            for (auto& [ends, e] : it->second.segments) {
                e.replay.clear();
                e.start = SectionTrainingStart{};
            }
        std::error_code ec;
        fs::remove_all(replayDir_ / Paths::fromUtf8(dir), ec);
        total -= std::min(total, bytes);
        dropped = true;
    }
    if (dropped) save();  // the file must stop naming them
}

TrainingRecords::Entry* TrainingRecords::entry(const std::string& levelKey, const std::string& from, const std::string& to,
                                                const std::string& date) {
    if (levelKey.empty() || !validId(levelKey) || !validId(from) || !validId(to)) return nullptr;
    auto lit = levels_.find(levelKey);
    if (lit == levels_.end()) {
        // Bounded file: the least recently trained levels go first.
        while (levels_.size() >= kLevelsKept) {
            auto oldest = std::min_element(levels_.begin(), levels_.end(), [](const auto& a, const auto& b) { return a.second.touched < b.second.touched; });
            levels_.erase(oldest);
        }
        lit = levels_.emplace(levelKey, Level{}).first;
    }
    Level& l = lit->second;
    l.touched = date.substr(0, 32);
    Ends ends{from, to};
    if (!l.segments.count(ends) && l.segments.size() >= kSegmentsPerLevel) {
        auto oldest = std::min_element(l.segments.begin(), l.segments.end(), [](const auto& a, const auto& b) { return a.second.date < b.second.date; });
        l.segments.erase(oldest);
    }
    return &l.segments[ends];
}

bool TrainingRecords::add(const std::string& levelKey, const std::string& from, const std::string& to, float seconds, const std::string& date,
                          const std::vector<uint8_t>* replay, const SectionTrainingStart* start) {
    if (!validSeconds(seconds)) return false;
    Entry* ep = entry(levelKey, from, to, date);
    if (!ep) return false;
    Entry& e = *ep;
    e.attempts = std::min(e.attempts + 1, 1000000);
    // Compared to the millisecond (what the timer shows): a tie is not a new best.
    const bool better = e.best < 0.0f || ms(seconds) < ms(e.best);
    if (better) {
        e.best = seconds;
        e.date = date.substr(0, 32);
        // 2.60.3: its replay (the previous best's no longer holds it, kept or not).
        e.replay.clear();
        e.start = SectionTrainingStart{};
        if (replay && start && start->checkpointId == from) {
            e.replay = keptreplays::write(replayDir_, levelKey, *replay, date);
            if (!e.replay.empty()) e.start = *start;
        }
    }
    return better;
}

void TrainingRecords::setPowers(const std::string& levelKey, const std::string& from, const std::string& to,
                                const std::vector<std::string>* powers, const std::string& date) {
    if (!powers) {
        // Back to the route's powers: forgotten (and the entry with it when nothing else is kept).
        auto lit = levels_.find(levelKey);
        if (lit == levels_.end()) return;
        auto it = lit->second.segments.find({from, to});
        if (it == lit->second.segments.end()) return;
        it->second.customPowers = false;
        it->second.powers.clear();
        if (it->second.best < 0.0f) lit->second.segments.erase(it);
        if (lit->second.segments.empty()) levels_.erase(lit);
        return;
    }
    std::vector<std::string> kept;
    for (const std::string& id : *powers)
        if (!id.empty() && validId(id) && std::find(kept.begin(), kept.end(), id) == kept.end() && kept.size() < kMaxPowers) kept.push_back(id);
    if (Entry* e = entry(levelKey, from, to, date)) {
        e->customPowers = true;
        e->powers = std::move(kept);
    }
}

const std::vector<std::string>* TrainingRecords::powers(const std::string& levelKey, const std::string& from, const std::string& to) const {
    const Entry* e = get(levelKey, from, to);
    return e && e->customPowers ? &e->powers : nullptr;
}

TrainingBests TrainingRecords::bests(const std::string& levelKey) const {
    TrainingBests out;
    auto lit = levels_.find(levelKey);
    if (lit == levels_.end()) return out;
    for (const auto& [ends, e] : lit->second.segments)
        if (validSeconds(e.best)) out[ends] = e.best;
    return out;
}

const TrainingRecords::Entry* TrainingRecords::get(const std::string& levelKey, const std::string& from, const std::string& to) const {
    auto lit = levels_.find(levelKey);
    if (lit == levels_.end()) return nullptr;
    auto it = lit->second.segments.find({from, to});
    return it == lit->second.segments.end() ? nullptr : &it->second;
}

}  // namespace cosmic
