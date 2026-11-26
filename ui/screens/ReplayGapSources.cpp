#include "ui/screens/ReplayGapSources.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <system_error>

#include "game/GameServices.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/ReplayCompare.h"
#include "game/ghost/TrainingSegments.h"

namespace cosmic::replaygap {

namespace {

/// The replay file `p` when it is still on this computer (UTF-8), else empty.
std::string present(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec) ? Paths::toUtf8(p) : std::string{};
}

/// A replay of the player's own saves, read for the display only: readable, a trajectory, and
/// positions that are numbers (a damaged or edited file is left out, never trusted further).
std::shared_ptr<const ReplayData> readOwn(const std::string& file) {
    if (file.empty()) return nullptr;
    auto rep = GhostStore::loadReplay(Paths::fromUtf8(file));
    if (!rep || rep->frames.size() < 2 || rep->sampleHz < 5 || rep->sampleHz > 240) return nullptr;
    for (const ReplayFrame& f : rep->frames)
        if (!std::isfinite(f.x) || !std::isfinite(f.y)) return nullptr;
    return std::make_shared<const ReplayData>(std::move(*rep));
}

/// A whole run as the reference (the record, another replay): its checkpoints matched with the
/// watched run's by position, as the comparison does.
LoadedRef wholeRun(const RefRequest& req, std::shared_ptr<const ReplayData> keep, std::string name) {
    LoadedRef out;
    const ReplayData& other = *keep;
    if (GhostCheck c = checkGhostReplay(other, "", req.bounds); !c.ok) {
        out.why = "Replay de référence refusé : " + c.reason;
        return out;
    }
    ReplayComparison cmp;
    cmp.set(CompareRun::fromReplay(*req.run, GhostMeta{}), CompareRun::fromReplay(other, req.meta));
    out.ref = gapReferenceOf(cmp, RouteRun::fromReplay(other, req.route));  // invalid without a route: the checkpoints only
    out.ghost.push_back({std::make_shared<const GhostTrack>(cmp.b().track), 0.0, 0.0, cmp.b().duration()});
    out.name = std::move(name);
    out.sameRun = !other.attemptId.empty() && other.attemptId == req.run->attemptId;
    out.replay = std::move(keep);
    out.ok = true;
    return out;
}

LoadedRef sumOfBest(const RefRequest& req) {
    LoadedRef out;
    out.name = "Somme des meilleurs";
    const size_t n = req.sob.size();
    if (n == 0) {
        out.why = "Pas encore de somme des meilleurs pour cette version : terminez le niveau ou entraînez ses segments.";
        return out;
    }
    // Each best segment from the replay that holds it (one replay may hold several of them).
    struct Held {
        RouteRun run;
        std::shared_ptr<const GhostTrack> track;
    };
    std::map<std::string, Held> held;
    std::vector<SobPiece> pieces(n);
    for (size_t i = 0; i < n; ++i) {
        const RefRequest::SobSegment& s = req.sob[i];
        pieces[i].duration = s.best >= 0.0f ? s.best : -1.0;
        if (s.file.empty()) continue;
        auto it = held.find(s.file);
        if (it == held.end()) {
            Held h;
            if (auto rep = readOwn(s.file)) {
                h.run = RouteRun::fromReplay(*rep, req.route);
                auto t = std::make_shared<GhostTrack>();
                t->load(*rep);
                h.track = t;
            }
            it = held.emplace(s.file, std::move(h)).first;
        }
        if (!it->second.run.valid() || !(s.end > s.start)) continue;
        pieces[i].run = &it->second.run;
        pieces[i].start = s.start;
        pieces[i].end = s.end;
    }
    // The route distance of each boundary: the checkpoint of that id (where the level has it).
    std::vector<float> reach(n + 1, -1.0f);
    reach[0] = 0.0f;
    reach[n] = req.route.length;
    for (size_t i = 1; i < n; ++i) {
        for (const auto& [cid, feet] : req.checkpoints)
            if (!req.sob[i].from.empty() && cid == req.sob[i].from) {
                size_t hint = 0;
                reach[i] = routeDistanceAt(req.route, feet, hint, 400.0f);
            }
        // Else where the replays of its two segments stood there.
        if (reach[i] < 0.0f && pieces[i].run) reach[i] = pieces[i].run->reachAt(pieces[i].start);
        if (reach[i] < 0.0f && pieces[i - 1].run) reach[i] = pieces[i - 1].run->reachAt(pieces[i - 1].end);
    }
    const bool reachKnown = req.route.valid() && std::all_of(reach.begin(), reach.end(), [](float r) { return r >= 0.0f; });
    const SobRun sob = buildSobRun(pieces, reachKnown ? reach : std::vector<float>{}, req.route.length);
    // The watched run's checkpoints, by id (where it stood at each split), matched with the sum's.
    GhostTrack mine;
    mine.load(*req.run);
    const std::vector<int64_t> splits = replaySplitsMs(*req.run);
    std::vector<std::string> sobIds;
    for (size_t i = 1; i < n; ++i) sobIds.push_back(req.sob[i].from);
    out.ref.run = sob.run;
    out.ref.atSplitsMs = sobAtSplits(sob, splits.size(), checkpointsOfTrack(mine, req.checkpoints), sobIds);
    out.ref.finishMs = sob.boundaryMs[n];
    out.segments = static_cast<int>(n);
    out.withReplay = sob.withReplay;
    if (sob.run.valid())
        for (size_t i = 0; i < n; ++i) {
            const auto it = held.find(req.sob[i].file);
            if (it == held.end() || !it->second.track || sob.boundaryMs[i] < 0) continue;
            out.ghost.push_back({it->second.track, static_cast<double>(sob.boundaryMs[i]) / 1000.0, pieces[i].start, pieces[i].end});
        }
    out.ok = true;
    return out;
}

}  // namespace

bool LoadedRef::poseAt(double t, GhostTrack::Pose& out) const {
    for (const GhostPiece& p : ghost) {
        const double len = p.end - p.start;
        if (!p.track || t < p.from - 1e-6 || t > p.from + len + 1e-6) continue;
        out = p.track->poseAt(static_cast<float>(p.start + std::clamp(t - p.from, 0.0, len)));
        return out.visible;
    }
    // A whole run waits at its start before its time and at the exit once finished.
    if (ghost.size() == 1 && ghost[0].track && ghost[0].from == 0.0 && ghost[0].start == 0.0) {
        out = ghost[0].track->poseAt(static_cast<float>(std::clamp(t, 0.0, ghost[0].end)));
        return out.visible;
    }
    return false;
}

std::string shortDate(const std::string& date) {
    if (date.size() < 10 || date[4] != '-' || date[7] != '-') return {};
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (date[static_cast<size_t>(i)] < '0' || date[static_cast<size_t>(i)] > '9') return {};
    return date.substr(8, 2) + "/" + date.substr(5, 2);
}

RefRequest prepareRecord(GameServices& g, const std::string& levelId, const std::string& levelHash) {
    RefRequest req;
    req.kind = RefKind::Record;
    for (GhostEntry& e : g.ghosts.localGhosts(levelId))
        if (e.meta.source == "pb" && e.meta.levelHash == levelHash) {
            req.file = e.file;
            req.meta = e.meta;
            break;
        }
    if (req.file.empty()) req.missing = "Aucun record (PB) de cette version du niveau sur cet ordinateur.";
    return req;
}

RefRequest prepareSumOfBest(GameServices& g, const std::string& levelId, const std::string& levelHash) {
    RefRequest req;
    req.kind = RefKind::SumOfBest;
    const std::string key = GhostStore::levelKey(levelId, levelHash);
    for (const PersonalBests::Segment& s : g.bests.segments(key)) {
        RefRequest::SobSegment seg;
        seg.from = s.from;
        seg.to = s.to;
        seg.best = s.best();
        if (s.record && s.record->replay.has()) {
            seg.file = present(g.bests.replayPath(key, s.record->replay.file));
            seg.start = s.record->replay.start;
            seg.end = s.record->replay.end;
        }
        req.sob.push_back(std::move(seg));
    }
    return req;
}

LoadedRef loadReference(RefRequest req) {
    LoadedRef out;
    if (!req.run) {
        out.why = "Aucun replay.";
        return out;
    }
    if (!req.missing.empty()) {
        out.why = req.missing;
        return out;
    }
    switch (req.kind) {
        case RefKind::Record: {
            std::string err;
            auto rep = GhostStore::loadReplay(req.file, &err);
            if (!rep) {
                out.why = "Record illisible : " + err;
                return out;
            }
            const std::string d = shortDate(req.meta.date);
            return wholeRun(req, std::make_shared<const ReplayData>(std::move(*rep)), d.empty() ? std::string("Record (PB)") : "Record (PB) · " + d);
        }
        case RefKind::Other: {
            if (!req.memory) {
                out.why = "Choisissez un replay.";
                return out;
            }
            return wholeRun(req, req.memory, req.meta.label.empty() ? req.meta.sourceLabel() : req.meta.label);
        }
        case RefKind::SumOfBest: return sumOfBest(req);
    }
    return out;
}

}  // namespace cosmic::replaygap
