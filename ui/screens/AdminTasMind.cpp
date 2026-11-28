// ADMINISTRATION › TAS, 2.101 (docs/TAS.md « L'IA du TAS »): the progress of a training at the
// right of its curve — is it running, its best time and what it gained, where it stands among the
// references (a ruler: faster to the right), where the time is to gain segment by segment, what the
// search does right now, who finds the gains — and, under the curve, the TAS's mind: its thoughts
// (newest first), its essayeurs (one per core, what each one does) and its memory (the
// configurations already solved). Every text measured in a box of its own: nothing over anything.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/tas/TasCurve.h"
#include "game/tas/TasTraining.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/AdminTas.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

using namespace tasadmin;

namespace {

const Color kLine{33, 43, 66, 255};
const Color kCard{24, 34, 62, 235};

/// « −2,150 s » / « +1,800 s » (seconds, French decimal comma).
std::string signedSeconds(int64_t ms, bool plusSign) {
    char b[32];
    std::snprintf(b, sizeof b, "%s%.3f s", ms < 0 ? "−" : plusSign && ms > 0 ? "+" : "", static_cast<double>(ms < 0 ? -ms : ms) / 1000.0);
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

/// « 12.345 » (a segment's time, seconds, as the splits show them).
std::string segmentTime(int64_t ms) {
    char b[24];
    std::snprintf(b, sizeof b, "%.3f", static_cast<double>(ms) / 1000.0);
    return b;
}

/// The segment times of a run (start → CP1, …, CPn → exit) from its splits; empty when one is missing.
std::vector<int64_t> segmentsOf(const std::vector<int64_t>& splits, int64_t timeMs) {
    std::vector<int64_t> out;
    if (timeMs <= 0) return out;
    int64_t prev = 0;
    for (int64_t s : splits) {
        if (s <= prev) return {};
        out.push_back(s - prev);
        prev = s;
    }
    if (timeMs <= prev) return {};
    out.push_back(timeMs - prev);
    return out;
}

}  // namespace

void AdminTas::drawProgressPanel(const Row& row, const TasCurveVersion* v, const Rect& r, bool training, int round, int focusFirst, int focusLast,
                                 const TasProgress* live) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    Renderer& R = ui.r();
    float y = r.y;
    const float gap = ui.s(10);
    auto heading = [&](const std::string& t) {
        text(t, {r.x, y, r.w, ui.s(18)}, 12, th.textDim, FontFace::Bold);
        y += ui.s(20);
    };

    // The best points of this version, in order (each one strictly faster).
    std::vector<const TasCurvePoint*> bests;
    if (v)
        for (const TasCurvePoint& p : v->points)
            if (p.best()) bests.push_back(&p);
    int64_t bestMs = v ? v->bestMs() : 0;
    if (live && live->bestMs > 0 && (bestMs == 0 || live->bestMs < bestMs)) bestMs = live->bestMs;
    const int64_t startMs = !bests.empty() ? bests.front()->timeMs : live ? live->seedMs : 0;

    // 1. Running or not, the round, the cores.
    {
        const float h = ui.s(24);
        std::string pill = training ? (live && live->verifying ? "VÉRIFICATION" : "EN COURS") : "À L'ARRÊT";
        const float pw = ui.text().measure(pill, FontFace::Bold, ui.s(12)).x + ui.s(22);
        const Rect pr{r.x, y, std::min(pw, r.w * 0.45f), h};
        R.fillRoundedRect(pr, h * 0.5f, training ? th.accent.withAlpha(40) : Color{40, 48, 70, 220});
        text(pill, {pr.x + ui.s(8), pr.y, pr.w - ui.s(12), h}, 12, training ? th.accent : th.textDim, FontFace::Bold, Align::Center);
        std::string info;
        if (training && live) {
            info = roundText(round, true) + " · " + focusText(focusFirst, focusLast, false);
            if (live->threads > 0) info += " · " + std::to_string(live->threads) + (live->threads > 1 ? " cœurs" : " cœur");
        } else if (v && v->lastTrainingMs() > 0) {
            info = "Entraîné " + durationText(v->lastTrainingMs()) + " en tout";
        } else {
            info = "Jamais entraîné";
        }
        text(info, {pr.right() + ui.s(10), y, std::max(0.0f, r.right() - pr.right() - ui.s(10)), h}, 13, th.textDim);
        y += h + gap;
    }

    // A short panel (1280 x 720): the best time on two lines, the ruler without its summary, so
    // that the segments keep rows.
    const bool compact = r.h < ui.s(420);

    // 2. The best time verified, what the training gained, its last gain, a faster run being verified.
    if (compact && bestMs > 0 && y + ui.s(56) <= r.bottom()) {
        const bool candidate = live && live->candidateMs > 0 && live->candidateMs < bestMs;
        const float ch = ui.s(candidate ? 74 : 56);
        const Rect card{r.x, y, r.w, ch};
        R.fillRoundedRect(card, ui.s(8), kCard);
        const float px = card.x + ui.s(12), pw = card.w - ui.s(24);
        float cy = card.y + ui.s(6);
        text("MEILLEUR TEMPS VÉRIFIÉ", {px, cy, pw, ui.s(14)}, 10.5f, th.textDim, FontFace::Bold);
        cy += ui.s(16);
        const std::string big = formatRunTime(bestMs);
        const float bw = std::min(pw * 0.5f, ui.text().measure(big, FontFace::Bold, ui.s(22)).x + ui.s(4));
        text(big, {px, cy, bw, ui.s(26)}, 22, th.text, FontFace::Bold);
        if (startMs > bestMs) {
            const std::string d = signedSeconds(bestMs - startMs, false);
            const float dx = px + bw + ui.s(12);
            const float dw = std::min(std::max(0.0f, px + pw - dx), ui.text().measure(d, FontFace::Bold, ui.s(15)).x + ui.s(4));
            text(d, {dx, cy + ui.s(3), dw, ui.s(20)}, 15, th.success, FontFace::Bold);
            const float sx = dx + dw + ui.s(8);
            text("depuis le départ (" + formatRunTime(startMs) + ")", {sx, cy + ui.s(5), std::max(0.0f, px + pw - sx), ui.s(16)}, 11, th.textDim);
        }
        cy += ui.s(28);
        if (candidate) text(formatRunTime(live->candidateMs) + " trouvé, en vérification…", {px, cy, pw, ui.s(16)}, 12, th.warm);
        y += ch + gap;
    } else if (bestMs > 0 && y + ui.s(122) <= r.bottom()) {
        const bool candidate = live && live->candidateMs > 0 && live->candidateMs < bestMs;
        const float ch = ui.s(candidate ? 122 : 104);
        const Rect card{r.x, y, r.w, ch};
        R.fillRoundedRect(card, ui.s(8), kCard);
        const float px = card.x + ui.s(12), pw = card.w - ui.s(24);
        float cy = card.y + ui.s(8);
        text("MEILLEUR TEMPS VÉRIFIÉ", {px, cy, pw, ui.s(16)}, 11, th.textDim, FontFace::Bold);
        cy += ui.s(18);
        const std::string big = formatRunTime(bestMs);
        const float bw = std::min(pw * 0.55f, ui.text().measure(big, FontFace::Bold, ui.s(30)).x + ui.s(4));
        text(big, {px, cy, bw, ui.s(32)}, 30, th.text, FontFace::Bold);
        if (startMs > bestMs) {
            const float rx = px + bw + ui.s(14), rw = std::max(0.0f, px + pw - rx);
            text(signedSeconds(bestMs - startMs, false), {rx, cy - ui.s(2), rw, ui.s(20)}, 17, th.success, FontFace::Bold);
            text("depuis le départ (" + formatRunTime(startMs) + ")", {rx, cy + ui.s(18), rw, ui.s(15)}, 11, th.textDim);
        }
        cy += ui.s(36);
        if (bests.size() >= 2) {
            const TasCurvePoint& last = *bests.back();
            const TasCurvePoint& prev = *bests[bests.size() - 2];
            // Its size and when, then who found it and where (whole sentences: the dictionary's).
            char line[200];
            const std::string g = signedSeconds(last.timeMs - prev.timeMs, false);
            if (v && v->lastTrainingMs() > last.trainingMs)
                std::snprintf(line, sizeof line, "Dernier gain : %s, il y a %s d'entraînement", g.c_str(), durationText(v->lastTrainingMs() - last.trainingMs).c_str());
            else
                std::snprintf(line, sizeof line, "Dernier gain : %s", g.c_str());
            text(line, {px, cy, pw, ui.s(16)}, 12, th.text);
            cy += ui.s(18);
            const auto deltas = tasSegmentDeltas(prev.splits, prev.timeMs, last.splits, last.timeMs);
            size_t k = 0;
            if (!deltas.empty()) k = static_cast<size_t>(std::min_element(deltas.begin(), deltas.end()) - deltas.begin());
            const bool hasWhere = !deltas.empty() && deltas[k] < 0;
            if (!last.source.empty() || hasWhere) {
                if (!last.source.empty() && hasWhere)
                    std::snprintf(line, sizeof line, "trouvé par %s, segment %s", tasSourceLabel(last.source).c_str(), tasSegmentName(k, deltas.size() - 1).c_str());
                else if (!last.source.empty())
                    std::snprintf(line, sizeof line, "trouvé par %s", tasSourceLabel(last.source).c_str());
                else
                    std::snprintf(line, sizeof line, "segment %s", tasSegmentName(k, deltas.size() - 1).c_str());
                text(line, {px, cy, pw, ui.s(16)}, 11, th.textDim);
                cy += ui.s(18);
            }
        }
        if (candidate) text(formatRunTime(live->candidateMs) + " trouvé, en vérification…", {px, cy, pw, ui.s(16)}, 12, th.warm);
        y += ch + gap;
    }

    // 3. Where it stands: a ruler of the references, faster to the right.
    struct Mark {
        std::string label;
        int64_t ms = 0;
        Color c;
        bool now = false, verbatimName = false;
        std::string name;  // a player's name (as written)
    };
    std::vector<Mark> marks;
    if (bestMs > 0) marks.push_back({"Maintenant", bestMs, th.accent, true});
    if (startMs > bestMs) marks.push_back({"Départ", startMs, th.textDim});
    if (row.local.shippedMs > 0 && row.local.shippedMs != bestMs) marks.push_back({"Livré", row.local.shippedMs, th.text});
    if (row.server.publishedMs > 0 && row.server.publishedMs != bestMs && row.server.publishedMs != row.local.shippedMs)
        marks.push_back({"En ligne", row.server.publishedMs, th.text});
    if (v) {
        for (const TasProgress::Reference& ref : v->references)
            if (ref.timeMs > 0) marks.push_back({"", ref.timeMs, th.violet, false, true, ref.player});
        if (const int64_t sob = tasSumOfBest(*v, 0); sob > 0) marks.push_back({"Somme des meilleurs", sob, th.warm});
    }
    if (marks.size() >= 2 && y + ui.s(96) <= r.bottom()) {
        heading("OÙ EN EST-ON ? (plus rapide vers la droite)");
        int64_t lo = marks.front().ms, hi = lo;
        for (const Mark& m : marks) lo = std::min(lo, m.ms), hi = std::max(hi, m.ms);
        const int64_t pad = std::max<int64_t>(200, (hi - lo) / 12);
        lo -= pad;
        hi += pad;
        const float tx0 = r.x + ui.s(4), tx1 = r.right() - ui.s(4);
        auto xOf = [&](int64_t ms) { return tx0 + (tx1 - tx0) * static_cast<float>(hi - ms) / static_cast<float>(hi - lo); };
        const float rowH = ui.s(16);
        const float top = y, trackY = y + rowH + ui.s(8);
        R.fillRoundedRect({tx0, trackY - ui.s(2), tx1 - tx0, ui.s(4)}, ui.s(2), kLine);
        if (startMs > bestMs && bestMs > 0) R.fillRoundedRect({xOf(startMs), trackY - ui.s(2), xOf(bestMs) - xOf(startMs), ui.s(4)}, ui.s(2), th.accent);
        // Labels above (row 0) or below (row 1) the track, the first that does not cover another.
        std::vector<Rect> placed;
        for (const Mark& m : marks) {
            const float x = xOf(m.ms);
            if (m.now) R.fillCircle({x, trackY}, ui.s(6), th.accent);
            else R.fillRect({x - ui.s(1.5f), trackY - ui.s(7), ui.s(3), ui.s(14)}, m.c);
            const std::string time = formatRunTime(m.ms);
            const std::string& word = m.verbatimName ? m.name : m.label;
            const FontFace face = m.now ? FontFace::Bold : FontFace::Body;
            float nameW = 0.0f;
            {
                i18n::Verbatim vb(m.verbatimName);  // a player's name as written, the words translated
                nameW = ui.text().measure(word, face, ui.s(11)).x + ui.s(4);
            }
            const float timeW = ui.text().measure(time, face, ui.s(11)).x + ui.s(4);
            const float w = std::min(r.w * 0.5f, nameW + timeW);
            const float lx = std::clamp(x - w * 0.5f, r.x, r.right() - w);
            for (int row2 = 0; row2 < 2; ++row2) {
                const Rect box{lx, row2 == 0 ? top : trackY + ui.s(10), w, rowH};
                if (std::any_of(placed.begin(), placed.end(), [&](const Rect& o) { return o.expanded(ui.s(3)).intersects(box); })) continue;
                placed.push_back(box);
                const float nw = std::min(nameW, w * 0.6f);
                text(word, {box.x, box.y, nw, rowH}, 11, m.c, face, Align::Left, m.verbatimName);
                text(time, {box.x + nw, box.y, std::max(0.0f, box.w - nw), rowH}, 11, m.c, face);
                ++layout_.rulerMarks;
                break;
            }
        }
        y = trackY + ui.s(10) + rowH + ui.s(4);
        // What is left to gain to the next reference faster than the best time.
        int64_t next = 0;
        for (const Mark& m : marks)
            if (!m.now && m.ms < bestMs && (next == 0 || m.ms > next)) next = m.ms;
        char lineBuf[160] = "";
        if (startMs > bestMs && next > 0)
            std::snprintf(lineBuf, sizeof lineBuf, "Gagné %s · reste %s jusqu'au repère suivant", signedSeconds(startMs - bestMs, false).c_str(),
                          signedSeconds(bestMs - next, false).c_str());
        else if (startMs > bestMs)
            std::snprintf(lineBuf, sizeof lineBuf, "Gagné %s", signedSeconds(startMs - bestMs, false).c_str());
        else if (next > 0)
            std::snprintf(lineBuf, sizeof lineBuf, "Reste %s jusqu'au repère suivant", signedSeconds(bestMs - next, false).c_str());
        const std::string line = compact ? std::string() : std::string(lineBuf);
        if (!line.empty()) {
            text(line, {r.x, y, r.w, ui.s(16)}, 12, th.text);
            y += ui.s(18);
        }
        y += gap;
    }

    // 4. Where to gain time: each segment, the machine's time, the best reference's, the gap.
    const TasCurvePoint* bp = nullptr;
    for (const TasCurvePoint* p : bests)
        if (!p->splits.empty() && (!bp || p->timeMs < bp->timeMs)) bp = p;
    const std::vector<int64_t> mine = bp ? segmentsOf(bp->splits, bp->timeMs) : std::vector<int64_t>();
    if (!mine.empty() && y + ui.s(20) + ui.s(20) * 3 <= r.bottom()) {
        const size_t n = mine.size();
        std::vector<int64_t> ref(n, 0);
        if (v)
            for (const TasProgress::Reference& g : v->references) {
                const auto s = segmentsOf(g.splits, g.timeMs > 0 ? g.timeMs : 0);
                if (s.size() == n)
                    for (size_t k = 0; k < n; ++k) ref[k] = ref[k] == 0 ? s[k] : std::min(ref[k], s[k]);
                else if (g.splits.size() + 1 == n)  // a route only (no time at the exit): its checkpoints
                    for (size_t k = 0; k + 1 < n; ++k) {
                        const int64_t a = k == 0 ? 0 : g.splits[k - 1], b = g.splits[k];
                        if (b > a) ref[k] = ref[k] == 0 ? b - a : std::min(ref[k], b - a);
                    }
            }
        std::vector<int64_t> gained(n, 0);
        const TasCurvePoint* first = nullptr;
        for (const TasCurvePoint* p : bests)
            if (!p->splits.empty()) {
                first = p;
                break;
            }
        if (first && first != bp) {
            const auto d = tasSegmentDeltas(first->splits, first->timeMs, bp->splits, bp->timeMs);
            if (d.size() == n) gained = d;
        }
        heading("OÙ GAGNER DU TEMPS · PAR SEGMENT");
        const float rh = ui.s(20);
        const float c0 = r.w * 0.32f, c1 = r.w * 0.15f, c2 = r.w * 0.15f, c3 = r.w * 0.24f;
        auto cell = [&](int col, float yy) {
            const float xs[] = {r.x, r.x + c0, r.x + c0 + c1, r.x + c0 + c1 + c2, r.x + c0 + c1 + c2 + c3, r.right()};
            return Rect{xs[col], yy, std::max(0.0f, xs[col + 1] - xs[col] - ui.s(6)), rh};
        };
        text("Segment", cell(0, y), 11, th.textDim);
        text("TAS", cell(1, y), 11, th.textDim, FontFace::Body, Align::Right);
        text("Repère", cell(2, y), 11, th.textDim, FontFace::Body, Align::Right);
        text("Écart", cell(3, y), 11, th.textDim, FontFace::Body, Align::Center);
        text("Gagné", cell(4, y), 11, th.textDim, FontFace::Body, Align::Right);
        y += rh;
        int64_t widest = 1;
        for (size_t k = 0; k < n; ++k)
            if (ref[k] > 0) widest = std::max<int64_t>(widest, std::abs(mine[k] - ref[k]));
        // The rows that fit with the legend under them; when all do not: the largest gaps, in their
        // order, then « … » (in a row's place; the legend only if there is room left).
        const int withLegend = std::max(0, static_cast<int>((r.bottom() - y - ui.s(18)) / rh));
        const int room = static_cast<int>(n) <= withLegend ? withLegend : std::max(0, static_cast<int>((r.bottom() - y) / rh));
        std::vector<size_t> rows;
        for (size_t k = 0; k < n; ++k) rows.push_back(k);
        size_t hidden = 0;
        if (static_cast<int>(n) > room) {
            const size_t keep = static_cast<size_t>(std::max(1, room - 1));
            std::stable_sort(rows.begin(), rows.end(), [&](size_t a, size_t b) {
                return std::abs(mine[a] - ref[a]) * (ref[a] > 0) > std::abs(mine[b] - ref[b]) * (ref[b] > 0);
            });
            hidden = n - std::min(n, keep);
            rows.resize(std::min(n, keep));
            std::sort(rows.begin(), rows.end());
        }
        for (size_t k : rows) {
            const bool searched = training && focusFirst > 0 && static_cast<int>(k) + 1 >= focusFirst && static_cast<int>(k) + 1 <= focusLast;
            const Rect nameBox = cell(0, y);
            if (searched) R.fillRoundedRect({r.x, y + ui.s(1), c0 - ui.s(4), rh - ui.s(2)}, ui.s(4), th.accent.withAlpha(30));
            text(tasSegmentName(k, n - 1) + (searched ? " ◆" : ""), {nameBox.x + ui.s(4), nameBox.y, nameBox.w - ui.s(4), rh}, 12, searched ? th.accent : th.text,
                 searched ? FontFace::Bold : FontFace::Body);
            text(segmentTime(mine[k]), cell(1, y), 12, th.text, FontFace::Body, Align::Right);
            text(ref[k] > 0 ? segmentTime(ref[k]) : std::string("—"), cell(2, y), 12, th.violet, FontFace::Body, Align::Right);
            const Rect gc = cell(3, y);
            if (ref[k] > 0) {
                const int64_t d = mine[k] - ref[k];
                const float half = gc.w * 0.28f, mid = gc.x + half;
                R.fillRect({mid, y + ui.s(3), ui.s(1), rh - ui.s(6)}, kLine);
                const float w = half * static_cast<float>(std::min<int64_t>(std::abs(d), widest)) / static_cast<float>(widest);
                if (d > 0) R.fillRect({mid + ui.s(1), y + rh * 0.3f, std::max(ui.s(1), w), rh * 0.4f}, th.danger);
                else if (d < 0) R.fillRect({mid - w, y + rh * 0.3f, std::max(ui.s(1), w), rh * 0.4f}, th.success);
                text(signedSeconds(d, true), {mid + half + ui.s(4), y, std::max(0.0f, gc.right() - mid - half - ui.s(4)), rh}, 11, d > 0 ? th.danger : th.success);
            } else {
                text("—", gc, 12, th.textDim, FontFace::Body, Align::Center);
            }
            text(gained[k] < 0 ? signedSeconds(gained[k], false) : std::string("—"), cell(4, y), 12, gained[k] < 0 ? th.success : th.textDim, FontFace::Body, Align::Right);
            y += rh;
            ++layout_.segmentRows;
        }
        if (hidden > 0) {
            text("… " + std::to_string(hidden) + (hidden > 1 ? " autres segments" : " autre segment"), {r.x, y, r.w, ui.s(16)}, 11, th.textDim);
            y += ui.s(18);
        }
        if (y + ui.s(16) <= r.bottom()) {
            text("Rouge : le TAS est plus lent que le meilleur repère (du temps à gagner). ◆ : cherché maintenant.", {r.x, y, r.w, ui.s(16)}, 11, th.textDim);
            y += ui.s(18);
        }
        y += gap;
    }

    // 5. What the search does now (the windows of the last two minutes).
    if (training && live && !live->windows.empty() && y + ui.s(70) <= r.bottom()) {
        heading("LA RECHERCHE EN CE MOMENT");
        int64_t attempts = 0, finished = 0, broken = 0;
        const int64_t span = std::max<int64_t>(1, live->windows.back().endMs - live->windows.front().startMs);
        std::vector<int64_t> stops;
        for (const TasAttemptWindow& w : live->windows) {
            attempts += w.attempts;
            finished += w.finished;
            broken += w.brokenTotal();
            if (stops.size() < w.stopped.size()) stops.resize(w.stopped.size(), 0);
            for (size_t k = 0; k < w.stopped.size(); ++k) stops[k] += w.stopped[k];
        }
        const float kw = r.w / 3.0f;
        const std::string kv[3] = {countText(attempts * 1000 / span), percentText(finished, attempts), percentText(broken, attempts)};
        const std::string kl[3] = {"essais par seconde", "finissent", brokenWord(row.local.category)};
        for (int i = 0; i < 3; ++i) {
            text(kv[i], {r.x + kw * static_cast<float>(i), y, kw - ui.s(8), ui.s(20)}, 16, th.text, FontFace::Bold);
            text(kl[i], {r.x + kw * static_cast<float>(i), y + ui.s(20), kw - ui.s(8), ui.s(15)}, 11, th.textDim);
        }
        y += ui.s(40);
        // Where the attempts stop: a bar, a part per section (its share written when it fits).
        int64_t total = finished;
        for (int64_t s : stops) total += s;
        if (total > 0 && y + ui.s(22) <= r.bottom()) {
            float x = r.x;
            const Color shades[] = {th.warm, Color{224, 138, 42, 255}, th.violet, Color{120, 160, 230, 255}};
            for (size_t k = 0; k <= stops.size(); ++k) {
                const int64_t val = k < stops.size() ? stops[k] : finished;
                if (val <= 0) continue;
                const float w = r.w * static_cast<float>(val) / static_cast<float>(total);
                const Color c = k < stops.size() ? shades[k % 4] : th.success;
                R.fillRect({x, y, std::max(1.0f, w - ui.s(1)), ui.s(18)}, c);
                const std::string lbl = (k < stops.size() ? "S" + std::to_string(k + 1) : std::string("arrivée")) + " " + percentText(val, total);
                if (ui.text().measure(lbl, FontFace::Bold, ui.s(10)).x + ui.s(6) <= w) text(lbl, {x + ui.s(2), y, w - ui.s(4), ui.s(18)}, 10, th.background, FontFace::Bold, Align::Center);
                x += w;
            }
            y += ui.s(22);
            text("Où s'arrêtent les essais (section par section, puis l'arrivée)", {r.x, y, r.w, ui.s(15)}, 11, th.textDim);
            y += ui.s(17);
        }
        y += gap;
    }

    // 6. Who finds the gains: each strategy's share of the time, what it gained.
    if (live && !live->strategies.empty() && y + ui.s(40) <= r.bottom()) {
        heading("QUI TROUVE LES GAINS");
        int64_t time = 0;
        for (const TasStrategy& s : live->strategies) time += s.timeMs;
        std::vector<const TasStrategy*> order;
        for (const TasStrategy& s : live->strategies) order.push_back(&s);
        std::stable_sort(order.begin(), order.end(), [](const TasStrategy* a, const TasStrategy* b) { return a->gainMs > b->gainMs || (a->gainMs == b->gainMs && a->timeMs > b->timeMs); });
        const float rh = ui.s(18);
        for (const TasStrategy* s : order) {
            if (y + rh > r.bottom()) break;
            const float lw = r.w * 0.3f, gw = r.w * 0.22f;
            text(tasStrategyLabel(s->id), {r.x, y, lw - ui.s(6), rh}, 12, s->id == "trajectory" ? th.violet : th.text);
            const Rect bar{r.x + lw, y + rh * 0.3f, std::max(0.0f, r.w - lw - gw - ui.s(8)), rh * 0.4f};
            R.fillRect(bar, kLine);
            if (time > 0) R.fillRect({bar.x, bar.y, bar.w * static_cast<float>(s->timeMs) / static_cast<float>(time), bar.h}, s->id == "trajectory" ? th.violet : th.accent);
            text(s->gainMs > 0 ? signedSeconds(-s->gainMs, false) : std::string("—"), {r.right() - gw, y, gw, rh}, 12, s->gainMs > 0 ? th.success : th.textDim, FontFace::Body,
                 Align::Right);
            y += rh;
        }
        if (y + ui.s(16) <= r.bottom()) {
            text("Barre : part du temps de calcul ; à droite : ce que la stratégie a fait gagner.", {r.x, y, r.w, ui.s(15)}, 11, th.textDim);
            y += ui.s(17);
        }
    }
}

void AdminTas::drawMind(const TasProgress& p, const Rect& r) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    Renderer& R = ui.r();
    layout_.mind = r;
    R.fillRoundedRect(r, ui.s(8), kCard);
    const Rect in{r.x + ui.s(12), r.y + ui.s(8), r.w - ui.s(24), r.h - ui.s(16)};
    const float split = in.w * 0.56f;
    const Rect left{in.x, in.y, split - ui.s(10), in.h};
    const Rect right{in.x + split + ui.s(10), in.y, in.w - split - ui.s(10), in.h};
    R.fillRect({in.x + split, in.y, ui.s(1), in.h}, kLine);

    // The thoughts, newest first: a word for what it is, the time, the thought wrapped.
    {
        float y = left.y;
        text("L'IA DU TAS", {left.x, y, left.w * 0.4f, ui.s(18)}, 13, th.violet, FontFace::Bold);
        const int cores = static_cast<int>(p.workers.size());
        text(cores > 0 ? "observe, se souvient, décide · " + std::to_string(cores) + (cores > 1 ? " essayeurs" : " essayeur") : std::string("observe, se souvient, décide"),
             {left.x + left.w * 0.4f, y, left.w * 0.6f, ui.s(18)}, 11, th.textDim, FontFace::Body, Align::Right);
        y += ui.s(22);
        if (p.thoughts.empty()) {
            text("Pas encore de pensée : l'IA observe le début de l'entraînement.", {left.x, y, left.w, ui.s(16)}, 12, th.textDim);
        }
        auto kindWord = [](const std::string& k) {
            if (k == "observe") return std::string("Observation");
            if (k == "memory") return std::string("Mémoire");
            if (k == "decide") return std::string("Décision");
            if (k == "gain") return std::string("Gain");
            return std::string("Vérification");
        };
        auto kindColor = [&](const std::string& k) {
            if (k == "memory") return th.violet;
            if (k == "decide") return th.accent;
            if (k == "gain") return th.success;
            if (k == "verify") return th.warm;
            return th.text;
        };
        float tagW = 0.0f;
        for (const char* k : {"observe", "memory", "decide", "gain", "verify"}) tagW = std::max(tagW, ui.text().measure(kindWord(k), FontFace::Bold, ui.s(11)).x);
        tagW = std::min(tagW + ui.s(8), left.w * 0.3f);
        const float timeW = ui.text().measure("00:00:00", FontFace::Body, ui.s(11)).x + ui.s(8);
        const float lineH = ui.s(15);
        for (auto it = p.thoughts.rbegin(); it != p.thoughts.rend(); ++it) {
            const std::string body = tasThoughtText(*it);
            if (body.empty()) continue;
            const float tw = std::max(0.0f, left.w - timeW - tagW);
            const auto lines = raidui::wrap(ui, body, FontFace::Body, 11.5f, tw, 3);
            const float h = lineH * static_cast<float>(std::max<size_t>(1, lines.size()));
            if (y + h > left.bottom()) break;
            const int64_t s = it->atMs / 1000;
            char clock[24];
            std::snprintf(clock, sizeof clock, "%02lld:%02lld:%02lld", static_cast<long long>(s / 3600), static_cast<long long>(s / 60 % 60), static_cast<long long>(s % 60));
            text(clock, {left.x, y, timeW - ui.s(4), lineH}, 11, th.textDim, FontFace::Body, Align::Left, true);
            text(kindWord(it->kind), {left.x + timeW, y, tagW - ui.s(4), lineH}, 11, kindColor(it->kind), FontFace::Bold);
            float ly = y;
            for (const std::string& l : lines) {
                text(l, {left.x + timeW + tagW, ly, tw, lineH}, 11.5f, th.text, FontFace::Body, Align::Left, true);
                ly += lineH;
            }
            y += h + ui.s(5);
            ++layout_.thoughtsDrawn;
        }
    }

    // The essayeurs (one per core), then the memory.
    {
        float y = right.y;
        int64_t attempts = 0;
        for (const TasWorker& w : p.workers) attempts += w.attempts;
        text("ESSAYEURS", {right.x, y, right.w * 0.4f, ui.s(18)}, 13, th.text, FontFace::Bold);
        text(p.elapsedMs > 0 ? countText(p.explored * 1000 / std::max<int64_t>(1, p.elapsedMs)) + " essais par seconde" : std::string(),
             {right.x + right.w * 0.4f, y, right.w * 0.6f, ui.s(18)}, 11, th.textDim, FontFace::Body, Align::Right);
        y += ui.s(22);
        const int cols = 2;
        const float gw = (right.w - ui.s(8)) / static_cast<float>(cols), gh = ui.s(38);
        const float memoryH = p.memorySize > 0 || !p.memoryTop.empty() ? ui.s(92) : ui.s(40);
        const int rowsFit = std::max(1, static_cast<int>((right.bottom() - y - memoryH) / (gh + ui.s(6))));
        const int shown = std::min(static_cast<int>(p.workers.size()), rowsFit * cols);
        for (int i = 0; i < shown; ++i) {
            const TasWorker& w = p.workers[static_cast<size_t>(i)];
            const Rect c{right.x + (gw + ui.s(8)) * static_cast<float>(i % cols), y + (gh + ui.s(6)) * static_cast<float>(i / cols), gw, gh};
            R.fillRoundedRect(c, ui.s(6), Color{16, 24, 46, 240});
            const float half = (c.w - ui.s(12)) * 0.5f;
            text("Essayeur " + std::to_string(i + 1), {c.x + ui.s(6), c.y + ui.s(3), half, ui.s(16)}, 11, th.text, FontFace::Bold);
            text(tasStrategyLabel(w.arm), {c.x + ui.s(6) + half, c.y + ui.s(3), half, ui.s(16)}, 11, w.arm == "trajectory" ? th.violet : th.accent, FontFace::Body, Align::Right);
            text(tasWorkerText(w) + " · " + compactCount(w.attempts), {c.x + ui.s(6), c.y + ui.s(19), c.w - ui.s(12), ui.s(15)}, 10.5f, th.textDim);
            ++layout_.workersDrawn;
        }
        const int rowsUsed = (shown + cols - 1) / cols;
        y += (gh + ui.s(6)) * static_cast<float>(rowsUsed);
        if (shown < static_cast<int>(p.workers.size())) {
            text("… " + std::to_string(p.workers.size() - static_cast<size_t>(shown)) + " autres essayeurs", {right.x, y, right.w, ui.s(15)}, 11, th.textDim);
            y += ui.s(17);
        }
        if (p.workers.empty()) {
            text("Les essayeurs démarrent avec le premier tour.", {right.x, y, right.w, ui.s(16)}, 12, th.textDim);
            y += ui.s(20);
        }
        y += ui.s(6);
        if (y + ui.s(36) <= right.bottom()) {
            text("MÉMOIRE", {right.x, y, right.w * 0.4f, ui.s(18)}, 13, th.violet, FontFace::Bold);
            text(countText(p.memorySize) + (p.memorySize > 1 ? " configurations apprises" : " configuration apprise"), {right.x + right.w * 0.4f, y, right.w * 0.6f, ui.s(18)}, 11,
                 th.textDim, FontFace::Body, Align::Right);
            y += ui.s(20);
            for (const TasMemoryEntry& e : p.memoryTop) {
                if (y + ui.s(16) > right.bottom()) break;
                const float gwid = ui.text().measure("−0,000 s", FontFace::Body, ui.s(11)).x + ui.s(8);
                const float pw2 = e.powers.empty() ? 0.0f : std::min(right.w * 0.28f, ui.text().measure(e.powers, FontFace::Body, ui.s(10.5f)).x + ui.s(8));
                text(tasMemoryText(e), {right.x, y, right.w - gwid - pw2 - ui.s(6), ui.s(16)}, 11, th.text);
                if (pw2 > 0.0f) text(e.powers, {right.right() - gwid - pw2, y, pw2 - ui.s(4), ui.s(16)}, 10.5f, th.violet, FontFace::Body, Align::Left, true);
                text(e.gainMs > 0 ? signedSeconds(-e.gainMs, false) : signedSeconds(-e.leadFrames * 1000 / 120, false), {right.right() - gwid, y, gwid, ui.s(16)}, 11,
                     th.success, FontFace::Body, Align::Right);
                y += ui.s(18);
                ++layout_.memoryDrawn;
            }
            if (p.memoryTop.empty() && y + ui.s(16) <= right.bottom())
                text("Chaque passage résolu y est gardé, pour tous les niveaux.", {right.x, y, right.w, ui.s(16)}, 11, th.textDim);
        }
    }
}

}  // namespace cosmic
