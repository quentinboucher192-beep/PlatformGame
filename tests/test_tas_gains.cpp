// 2.99 (docs/TAS.md « Où est le gain », « Les guides », « Vérification au fil de l'eau »): the
// splits of every verified best run in cosmic_tas's progress file and on the training curve (kept,
// sent with the heartbeat, read back from files written before), the segment where each gain is,
// the players' runs that guide a training (their splits: the references of the checkpoints; the
// sum of the best segments; guidedBy on the run's sheet).

#include <string>
#include <vector>

#include "game/online/LevelFingerprint.h"
#include "game/tas/TasCurve.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasProgress.h"
#include "game/tas/TasTraining.h"
#include "tests/TestFramework.h"
#include "ui/screens/AdminTas.h"

using namespace cosmic;

TEST_CASE("tas gains: the segment of a gain, from the splits of two runs") {
    // CP1 at 9 s, CP2 at 20 s, CP3 at 30 s, the exit at 40 s; then 16 ms gained between CP2 and CP3.
    const std::vector<int64_t> a = {9000, 20000, 30000}, b = {9000, 20000, 29984};
    const auto d = tasSegmentDeltas(a, 40000, b, 39984);
    CHECK(d.size() == 4);
    CHECK(d == std::vector<int64_t>({0, 0, -16, 0}));
    CHECK(tasSegmentName(0, 3) == "Départ → CP1");
    CHECK(tasSegmentName(2, 3) == "CP2 → CP3");
    CHECK(tasSegmentName(3, 3) == "CP3 → Arrivée");
    // A gain early, a loss later: both said.
    CHECK(tasSegmentDeltas({9000, 20000}, 30000, {8900, 20010}, 29990) == std::vector<int64_t>({-100, 110, -20}));
    // Not comparable: another number of checkpoints, one not reached, no time.
    CHECK(tasSegmentDeltas(a, 40000, {9000, 20000}, 39984).empty());
    CHECK(tasSegmentDeltas(a, 40000, {9000, 0, 29984}, 39984).empty());
    CHECK(tasSegmentDeltas({}, 40000, {}, 39984).empty());
    // Read back only when coherent.
    CHECK(tasSplitsFromJson(Json::array({9000, 0, 30000}), 40000).has_value());
    CHECK(!tasSplitsFromJson(Json::array({9000, 8000}), 40000));   // going back
    CHECK(!tasSplitsFromJson(Json::array({9000, 41000}), 40000));  // beyond the run
    CHECK(!tasSplitsFromJson(Json::array({-1}), 40000));
    CHECK(!tasSplitsFromJson(Json("x"), 40000));
}

TEST_CASE("tas gains: the progress file and the curve keep the splits, the source and the guides; files written before still read") {
    TasProgress p;
    p.levelId = "sylvanox_01";
    p.bestMs = 39984;
    p.bestSplits = {9000, 20000, 29984};
    p.bestSource = "splice";
    p.verifying = true;
    p.candidateMs = 39970;
    p.guides = {{"NovaRider", 38000, {8000, 17000, 27000}}};
    auto rp = TasProgress::parse(p.toJson().dump());
    CHECK(rp && rp->bestSplits == p.bestSplits && rp->bestSource == "splice" && rp->verifying && rp->guides == p.guides);
    // A file written before 2.99: none of them.
    Json old = p.toJson();
    for (const char* k : {"bestSplits", "bestSource", "verifying", "guides"}) old.erase(k);
    auto op = TasProgress::fromJson(old);
    CHECK(op && op->bestSplits.empty() && op->bestSource.empty() && !op->verifying && op->guides.empty());
    // Incoherent splits are left out, the rest is read.
    Json bad = p.toJson();
    bad["bestSplits"] = Json::array({9000, 50000});
    auto bp = TasProgress::fromJson(bad);
    CHECK(bp && bp->bestSplits.empty() && bp->bestMs == 39984);

    // The curve: each point with its splits and source, the version with its references.
    TasCurve c;
    c.levelId = "sylvanox_01";
    const std::string h(64, 'e');
    TasCurvePoint s0;
    s0.kind = TasCurveKind::Start;
    s0.timeMs = 40000;
    s0.splits = {9000, 20000, 30000};
    TasCurvePoint s1;
    s1.kind = TasCurveKind::Improvement;
    s1.timeMs = 39984;
    s1.trainingMs = 60000;
    s1.splits = {9000, 20000, 29984};
    s1.source = "splice";
    CHECK(c.addBest(h, kSimulationVersion, s0) && c.addBest(h, kSimulationVersion, s1));
    c.version(h, kSimulationVersion).references = p.guides;
    auto rc = TasCurve::fromJson(c.toJson());
    CHECK(rc && rc->find(h, kSimulationVersion));
    if (!rc || !rc->find(h, kSimulationVersion)) return;
    const TasCurveVersion& v = *rc->find(h, kSimulationVersion);
    CHECK(v.points.size() == 2 && v.points[1].splits == s1.splits && v.points[1].source == "splice" && v.references == p.guides);
    CHECK(v.previousBest(1) == &v.points[0] && v.previousBest(0) == nullptr);
    // A curve file written before 2.99: points without splits, no references.
    Json oj = c.toJson();
    for (Json& x : oj["versions"]) {
        x.erase("references");
        for (Json& q : x["points"]) q.erase("splits"), q.erase("source");
    }
    auto oc = TasCurve::fromJson(oj);
    CHECK(oc && oc->find(h, kSimulationVersion) && oc->find(h, kSimulationVersion)->points[1].splits.empty() &&
          oc->find(h, kSimulationVersion)->references.empty());
    // The server's points carry them too.
    auto sp = TasCurvePoint::fromServer(Json{{"kind", "improvement"}, {"time_ms", 39984}, {"training_ms", 1}, {"splits", Json::array({9000, 20000, 29984})}, {"source", "beam"}});
    CHECK(sp && sp->splits.size() == 3 && sp->source == "beam");

    // The row says where the last gain is; the heartbeat carries the splits (with the clouds only).
    CHECK(tasadmin::lastGainText(&*rc, h) == "dernier gain −16 ms · CP2 → CP3");
    TasCurveVersion pv;
    TasCurvePoint pending = s1;
    pending.share = TasShare::Pending;
    pending.leaseId = "L";
    pending.foundAtMs = 5;
    pv.points.push_back(pending);
    const auto sent = TasTraining::curveSend(pv, "L", 100000);
    CHECK(sent.points.size() == 1 && sent.points[0]["splits"].size() == 3 && sent.points[0]["source"] == "splice");
    const auto plain = TasTraining::curveSend(pv, "L", 100000, false);
    CHECK(plain.points.size() == 1 && !plain.points[0].contains("splits") && !plain.points[0].contains("source"));
}

TEST_CASE("tas gains: the sum of the best segments of the guides and of the machine's best run") {
    TasCurveVersion v;
    TasCurvePoint best;
    best.kind = TasCurveKind::Improvement;
    best.timeMs = 86658;
    // The machine: slow early, fast on the boss (the shape of sylvanox_01 against a player's PB).
    best.splits = {11025, 30108, 39625, 51400, 64633, 83492};
    v.points.push_back(best);
    v.references = {{"Votre PB", 89201, {9225, 24250, 33042, 44075, 56417, 85884}}};
    // Segment by segment the fastest: the player's first five, the machine's boss and exit.
    const int64_t sob = tasSumOfBest(v, 0);
    CHECK(sob == 9225 + 15025 + 8792 + 11033 + 12342 + (83492 - 64633) + (86658 - 83492));
    CHECK(tasSumOfBest(v, 1) == 9225);
    CHECK(tasSumOfBest(v, 2) == 9225 + 15025);
    // Without a reference: nothing.
    TasCurveVersion alone;
    alone.points.push_back(best);
    CHECK(tasSumOfBest(alone, 0) == 0);
    CHECK(tasSourceLabel("splice") == "l'assemblage des guides");
}

TEST_CASE("tas gains: guidedBy on the run's sheet, written and read back") {
    TasRecord r;
    r.levelId = "sylvanox_01";
    r.levelHash = std::string(64, 'a');
    r.simVersion = kSimulationVersion;
    r.timeMs = 86600;
    r.replay = tasReplayFileName(r.category);
    r.guidedBy = {{"NovaRider", 78442, "c0ffee"}, {"Votre PB", 0, "abc"}};
    auto back = TasRecord::fromJson(r.toJson());
    CHECK(back && back->guidedBy == r.guidedBy);
    // A sheet without guides has no field at all (the sheets written before stay byte for byte).
    r.guidedBy.clear();
    CHECK(!r.toJson().contains("guidedBy"));
    auto plain = TasRecord::fromJson(r.toJson());
    CHECK(plain && plain->guidedBy.empty());
}
