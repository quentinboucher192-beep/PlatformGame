// Statistiques › Niveaux and › Passages difficiles (2.54; 2.91: on the levels' own pictures).
//  * Niveaux (classic levels, not EXTRÊME): the level's banner, its Sum of Best (each segment's best and
//    where it was made), its record, the time it could still gain, its finishes, its regularity; a row
//    per segment of the record's route — its picture (the level drawn between its two checkpoints),
//    what is met there (creatures, boss, tablets, stations, fragile slabs), its best time, its
//    consistency, its training — then the record and the Sum of Best over time (the time to gain as the
//    band between them), the zones to work on, the consistency per segment, the regularity of the runs;
//  * Passages difficiles: where the player falls in a level — the heat of its 40 slices over the level's
//    own picture, slice by slice, with its checkpoints — and the 5 hardest passages of every level and
//    boss phase, each with its picture and its training.

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/levels/LevelData.h"
#include "game/planets/PlanetData.h"
#include "game/powers/PowerDefinition.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/StatisticsScene.h"

namespace cosmic {

using charts::Bar;
using charts::LineSpec;
using charts::Series;
using playstats::kSlices;
using namespace statsui;

// ------------------------------------------------------------------ the level chosen

std::string StatisticsScene::levelName(const std::string& id) const {
    if (const LevelEntry* e = g_.content.level(id); e && !e->name.empty()) return e->name;
    if (auto it = data().runs.find(id); it != data().runs.end() && !it->second.name.empty()) return it->second.name;
    if (auto il = data().levels.find(id); il != data().levels.end() && !il->second.name.empty()) return il->second.name;
    return id;
}

std::string StatisticsScene::levelShort(const std::string& id) const {
    auto [planet, n] = statsart::levelPlace(g_, id);
    if (const PlanetData* p = g_.content.planet(planet)) return n > 0 ? p->name + " " + std::to_string(n) : p->name;
    return "Autre niveau";
}

void StatisticsScene::trainSegment(const std::string& levelId, const std::string& fromCheckpoint) {
    GhostSetupRequest req;
    req.training = true;
    req.trainingCheckpoint = fromCheckpoint;
    g_.app.pushScene(makeGhostSetup(g_, levelId, std::move(req)));
}

void StatisticsScene::levelPicture(const std::string& levelId, float a, float b, const Rect& r) {
    // Asked at the size it is shown (whole pixels, rounded to 8: a few sizes only), drawn over the next frames.
    const int w = std::max(8, static_cast<int>(std::lround(r.w / 8.0f)) * 8), h = std::max(8, static_cast<int>(std::lround(r.h / 8.0f)) * 8);
    if (const Texture* t = art_.stretch(levelId, a, b, w, h)) g_.app.renderer().drawTexture(*t, nullptr, r);
    else picturePending(r, art_.failed(levelId));
}

float StatisticsScene::chooseLevel(float x, float y, float w, const std::string& id, std::string& want, const std::function<int(const std::string&)>& lastDay,
                                   const std::string& info) {
    UIContext& ui = g_.ui;
    const auto groups = statisticsLevelGroups(g_, [&](const std::string& l) { return lastDay(l) >= 0; });
    if (groups.empty()) return 0.0f;
    // Every level in the order of the campaign: ‹ › go from one to the next, planets crossed.
    std::vector<std::pair<int, int>> all;  // (group, level)
    for (size_t a = 0; a < groups.size(); ++a)
        for (size_t b = 0; b < groups[a].levels.size(); ++b) all.push_back({static_cast<int>(a), static_cast<int>(b)});
    auto idOf = [&](size_t k) -> const std::string& { return groups[static_cast<size_t>(all[k].first)].levels[static_cast<size_t>(all[k].second)]; };
    int at = -1;
    for (size_t k = 0; k < all.size(); ++k)
        if (idOf(k) == want) at = static_cast<int>(k);
    if (at < 0) {  // the last level played, else the campaign's first
        int best = -1;
        at = 0;
        for (size_t k = 0; k < all.size(); ++k)
            if (lastDay(idOf(k)) > best) best = lastDay(idOf(k)), at = static_cast<int>(k);
    }
    const int n = static_cast<int>(all.size());
    const float bh = ui.s(40), nb = ui.s(48);
    UIContext::ButtonStyle nav;
    nav.small = true;
    nav.enabled = n > 1;
    if (cframe::button(ui, id + "prev", {x, y, nb, bh}, "‹", nav)) at = (at + n - 1) % n;
    // The planets (and « Autres niveaux »), at the right when they fit, else on a line of their own.
    UIContext::ButtonStyle chip;
    chip.small = true;
    chip.fontSize = 15;
    std::vector<float> cw;
    float chipsW = 0.0f;
    for (const auto& grp : groups) {
        cw.push_back(ui.text().measure(grp.name, FontFace::Bold, ui.s(15)).x + ui.s(26));
        chipsW += cw.back() + ui.s(6);
    }
    const float nameRoomMin = ui.s(420);
    const bool chipsBeside = x + nb * 2 + ui.s(100) + nameRoomMin + chipsW <= x + w;
    float chipsY = chipsBeside ? y : y + bh + ui.s(10);
    float cx = chipsBeside ? x + w - chipsW + ui.s(6) : x;
    const int curGroup = all[static_cast<size_t>(at)].first;
    for (size_t a = 0; a < groups.size(); ++a) {
        if (!chipsBeside && cx > x && cx + cw[a] > x + w) {
            cx = x;
            chipsY += bh + ui.s(6);
        }
        chip.selected = static_cast<int>(a) == curGroup;
        const float bwid = std::min(cw[a], w);
        if (cframe::button(ui, id + "planet" + std::to_string(a), {cx, chipsY, bwid, ui.s(36)}, ui.elide(groups[a].name, FontFace::Bold, 15, bwid - ui.s(16)),
                           chip) &&
            static_cast<int>(a) != curGroup) {
            // Its last level played, else its first.
            int best = -1, pick = -1;
            for (size_t k = 0; k < all.size(); ++k)
                if (all[k].first == static_cast<int>(a)) {
                    if (pick < 0) pick = static_cast<int>(k);
                    if (lastDay(idOf(k)) > best) best = lastDay(idOf(k)), pick = static_cast<int>(k);
                }
            if (pick >= 0) at = pick;
        }
        cx += bwid + ui.s(6);
    }
    const float rightEdge = chipsBeside ? x + w - chipsW - ui.s(10) : x + w;
    // The level: its planet's banner, its name; then ›, then what is said of it.
    const std::string& lid = idOf(static_cast<size_t>(at));
    float lx = x + nb + ui.s(10);
    const Rect thumb{lx, y, ui.s(72), bh};
    ui.r().fillRoundedRect(thumb, ui.s(6), Color{11, 16, 32, 255});
    if (statsart::drawPlanetBanner(g_, g_.content.campaignPlanetOf(lid), thumb)) ui.r().drawRoundedRect(thumb, ui.s(6), kCardLine, 1.0f);
    lx += thumb.w + ui.s(10);
    const std::string name = levelShort(lid) + " · " + levelName(lid);
    const float infoW = info.empty() ? 0.0f : ui.text().measure(info, FontFace::Body, ui.s(fz(14))).x;
    const float nameRoom = std::max(ui.s(80), rightEdge - lx - nb - ui.s(20) - (infoW > 0.0f ? std::min(infoW, ui.s(360)) + ui.s(14) : 0.0f));
    {
        i18n::Verbatim asIs(!g_.content.level(lid));  // a player's level: its name as written
        const float nf = ui.fitFont(name, FontFace::Bold, fz(18), nameRoom, 0.75f);
        const std::string shown = ui.elide(name, FontFace::Bold, nf, nameRoom);
        ui.label(shown, {lx, y + bh * 0.5f}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        lx += ui.text().measure(shown, FontFace::Bold, ui.s(nf)).x + ui.s(10);
    }
    if (cframe::button(ui, id + "next", {lx, y, nb, bh}, "›", nav)) at = (at + 1) % n;
    lx += nb + ui.s(14);
    if (!info.empty() && rightEdge - lx > ui.s(60)) {
        const float f = ui.fitFont(info, FontFace::Body, fz(14), rightEdge - lx, 0.8f);
        ui.label(ui.elide(info, FontFace::Body, f, rightEdge - lx), {lx, y + bh * 0.5f}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    want = idOf(static_cast<size_t>(std::clamp(at, 0, n - 1)));
    return (chipsBeside ? bh : chipsY + ui.s(36) - y) + ui.s(16);
}

// ------------------------------------------------------------------ Niveaux

void StatisticsScene::segmentChips(const statsart::LevelInfo* li, float a, float b, float x, float y, float w, float h) {
    UIContext& ui = g_.ui;
    if (!li) return;
    const auto groups = li->between(a, b);
    const float f = fz(13), gap = ui.s(8);
    float cx = x;
    for (const auto& g : groups) {
        using K = statsart::LevelInfo::Thing::Kind;
        std::string text;
        float iw = h - ui.s(8);
        if (g.kind == K::Boss) text = "BOSS · " + bossName(g.id);
        else if (g.kind == K::Tablet) text = "tablette";
        else if (g.kind == K::Station) text = "station";
        else if (g.kind == K::Crumble) text = "×" + std::to_string(g.count) + (g.count > 1 ? " dalles fragiles" : " dalle fragile"), iw = h * 1.4f;
        else text = "×" + std::to_string(g.count);
        const float tw = ui.text().measure(text, FontFace::Body, ui.s(f)).x;
        const float chipW = ui.s(6) + iw + ui.s(4) + tw + ui.s(8);
        if (cx + chipW > x + w) break;  // what fits; the rest is in the level
        const Rect c{cx, y, chipW, h};
        ui.r().fillRoundedRect(c, ui.s(7), g.kind == K::Boss ? Color{52, 16, 30, 230} : Color{5, 8, 18, 200});
        ui.r().drawRoundedRect(c, ui.s(7), g.kind == K::Boss ? ui.theme().danger.withAlpha(170) : Color{38, 53, 90, 255}, std::max(1.0f, ui.s(1.0f)));
        const Rect ib{c.x + ui.s(4), c.y + ui.s(3), iw, h - ui.s(6)};
        ui.r().pushClip(c);
        if (g.kind == K::Enemy || g.kind == K::Boss) statsart::drawEnemy(g_, g.id, ib, 0.0f, "idle", true);
        else if (g.kind == K::Tablet || g.kind == K::Station) {
            if (const PowerDefinition* p = g_.content.power(g.id); p && p->icon >= 0) {
                const float s = std::min(ib.w, ib.h);
                ui.icon("ui/icons_powers.png", p->icon, 32, Rect::fromCenter(ib.center(), {s, s}));
            }
        } else if (TexturePtr t = g_.app.resources().texture("sprites/objects/crumble_platform.png")) {
            const Rect src{0, 0, 48, 16};
            const float k = std::min(ib.w / src.w, ib.h / src.h);
            g_.app.renderer().drawTexture(*t, &src, Rect::fromCenter(ib.center(), {src.w * k, src.h * k}));
        }
        ui.r().popClip();
        ui.label(text, {ib.right() + ui.s(4), c.center().y + ui.s(3)}, f, g.kind == K::Boss ? ui.theme().text : kText2, Align::Left,
                 g.kind == K::Boss ? FontFace::Bold : FontFace::Body, VAlign::Middle);
        cx += chipW + gap;
    }
}

float StatisticsScene::drawLevels(float x, float y0, float w) {
    UIContext& ui = g_.ui;
    float y = y0;
    auto lastDay = [this](const std::string& id) {
        auto it = data().runs.find(id);
        return it != data().runs.end() && (it->second.attempts > 0 || !it->second.segments.empty()) ? it->second.lastDay : -1;
    };
    y += chooseLevel(x, y, w, "runs", wantRuns_, lastDay, {});
    if (lastDay(wantRuns_) < 0) {
        y += note(x, y, w,
                  "Aucune partie de ce niveau n'est encore enregistrée. Jouez-le depuis son début (hors EXTRÊME) : chaque segment entre deux "
                  "points de passage est chronométré, ses temps forment votre « somme des meilleurs » (SOB).");
        return y - y0;
    }
    const std::string& levelId = wantRuns_;
    const LevelRuns& r = data().runs.at(levelId);
    const statsart::LevelInfo* li = art_.info(levelId);
    const float sob = r.sob();
    const std::vector<playstats::SobPart> parts = playstats::sobParts(r);
    const std::vector<SegmentReport> reps = segmentReports(r);
    const Regularity reg = regularity(r, today_);
    const float gap = ui.s(16);
    auto checkpointName = [&](const std::string& id, bool end) { return li ? li->checkpointName(id, end) : (id.empty() ? std::string(end ? "Arrivée" : "Départ") : std::string("Point de passage")); };
    auto atOf = [&](const std::string& id, bool end) { return li ? li->at(id, end) : (end ? 1.0f : 0.0f); };
    // ----- The hero: the level's banner, its Sum of Best, four tiles.
    {
        const float hh = ui.s(236);
        const bool wide = w >= ui.s(1500);
        const float bannerW = wide ? w * 0.228f : w * 0.4f, sobW = wide ? w * 0.48f : w - bannerW - gap;
        // The banner.
        {
            const Rect b{x, y, bannerW, hh};
            ui.r().fillRoundedRect(b, ui.s(12), Color{11, 16, 32, 255});
            ui.r().pushClip(b.expanded(-ui.s(1)));
            statsart::drawPlanetBanner(g_, g_.content.campaignPlanetOf(levelId), b);
            ui.r().fillGradientV({b.x, b.y + b.h * 0.4f, b.w, b.h * 0.6f}, Color{5, 7, 15, 0}, Color{5, 7, 15, 235});
            ui.r().popClip();
            ui.r().drawRoundedRect(b, ui.s(12), kCardLine, std::max(1.0f, ui.s(1.0f)));
            const float pad = ui.s(18), room = b.w - 2 * pad;
            const float nf = 24.0f, nh = lineHeight(ui, FontFace::Title, ui.s(nf));
            const std::string cap = levelShort(levelId) + " · " + std::to_string(r.attempts) + (r.attempts > 1 ? " tentatives" : " tentative");
            kicker(cap, {b.x + pad, b.bottom() - pad - nh - ui.s(6) - lineHeight(ui, FontFace::Bold, ui.s(fz(12)))}, ui.theme().accent, 12, room, true);
            i18n::Verbatim asIs(!g_.content.level(levelId));
            const std::string nm = levelName(levelId);
            const float f = ui.fitFont(nm, FontFace::Title, nf, room, 0.55f);
            ui.label(ui.elide(nm, FontFace::Title, f, room), {b.x + pad, b.bottom() - pad - nh}, f, ui.theme().text, Align::Left, FontFace::Title);
        }
        // The Sum of Best: one, every source together; the share of each segment and where it was made.
        {
            const Rect c{x + bannerW + gap, y, sobW, hh};
            ui.r().fillRoundedRect(c, ui.s(12), Color{30, 26, 14, 235});
            ui.r().drawRoundedRect(c, ui.s(12), kGold.withAlpha(110), std::max(1.0f, ui.s(1.2f)));
            const float pad = ui.s(20), room = c.w - 2 * pad;
            float yy = c.y + ui.s(16);
            kicker("SOMME DES MEILLEURS (SOB) · TOUTES SOURCES", {c.x + pad, yy}, Color{243, 230, 189, 255}, 14, room);
            yy += lineHeight(ui, FontFace::Bold, ui.s(fz(14))) + ui.s(4);
            const float vf = 56.0f;
            const std::string v = sob > 0.0f ? clockLong(sob) : std::string("—");
            const float vw = bigValue(v, {c.x + pad, yy}, vf, room * 0.5f, kGold);
            const std::string expl = sob > 0.0f ? "Le meilleur temps de chaque segment du parcours de votre record, en partie complète ou à l'entraînement par "
                                                  "segment (même version du niveau)."
                                                : "Finissez le niveau depuis son début : chaque segment de votre record compte son meilleur temps.";
            const float ex = c.x + pad + vw + ui.s(22), ew = c.right() - pad - ex;
            const float vh = lineHeight(ui, FontFace::Title, ui.s(vf));
            if (ew > ui.s(120)) {
                const float eh = ui.text().measureWrapped(expl, FontFace::Body, ui.s(fz(14)), ew).y;
                ui.labelWrapped(expl, {ex, yy + std::max(0.0f, (vh - eh) * 0.5f)}, ew, fz(14), kText2);
            }
            yy += vh + ui.s(12);
            // The segments, in the order of the route, the width of their time, coloured by source.
            const Rect bar{c.x + pad, yy, room, ui.s(34)};
            std::vector<charts::Part> ps;
            int runs = 0, trains = 0;
            for (const auto& p : parts) {
                if (p.best <= 0.0f) continue;
                const bool tr = p.source == playstats::SobSource::Training;
                (tr ? trains : runs)++;
                const std::string seg = checkpointName(p.from, false) + " → " + checkpointName(p.to, true);
                std::string t = clockTime(p.best);
                if (t.size() > 2 && t.compare(t.size() - 2, 2, " s") == 0) t.resize(t.size() - 2);  // « 31,20 », « 1:02,35 »
                ps.push_back({p.best, tr ? kSrcTraining : kSrcRun, std::to_string(p.index + 1) + " · " + t,
                              "Segment " + std::to_string(p.index + 1) + " : " + seg,
                              {{charts::kNoSwatch, "Meilleur", clockTime(p.best)}, {charts::kNoSwatch, "Fait", tr ? std::string("à l'entraînement") : std::string("en partie")}}});
            }
            if (ps.empty()) picturePending(bar, true);
            else charts::stackedBar(ui, "c/sobparts", bar, ps, cursorOf("sobparts"), tip_);
            yy += bar.h + ui.s(12);
            std::vector<LegendItem> leg;
            if (runs) leg.push_back({kSrcRun, "Partie : " + std::to_string(runs) + (runs > 1 ? " segments" : " segment"), 2, {}});
            if (trains) leg.push_back({kSrcTraining, "Entraînement : " + std::to_string(trains), 2, {}});
            const std::string ver = "Version actuelle du niveau";
            const float verW = ui.text().measure(ver, FontFace::Body, ui.s(fz(13))).x;
            const float lw = legend(c.x + pad, yy, room - verW - ui.s(16), leg, false);
            (void)lw;
            legend(c.x + pad, yy, room - verW - ui.s(16), leg);
            if (legendWidth(leg) + verW + ui.s(16) <= room) ui.label(ver, {c.right() - pad, yy}, fz(13), ui.theme().textDim, Align::Right);
        }
        // Four tiles.
        {
            const Rect t{x + bannerW + sobW + 2 * gap, y, w - bannerW - sobW - 2 * gap, hh};
            const Rect area = wide ? t : Rect{x, y + hh + gap, w, hh};
            std::string pbDay;
            for (const auto& h : r.history)
                if (std::abs(h.pb - r.pb) < 0.005f) {
                    pbDay = dayShort(h.day);
                    break;
                }
            const float tg = ui.s(12), tw = (area.w - tg) / 2.0f, th = (area.h - tg) / 2.0f;
            auto tile = [&](const Rect& rr, const std::string& k, const std::string& v, const std::string& d, Color accent) {
                ui.r().fillRoundedRect(rr, ui.s(12), kCard);
                ui.r().drawRoundedRect(rr, ui.s(12), accent.a ? accent.withAlpha(120) : kCardLine, std::max(1.0f, ui.s(1.0f)));
                const float pad = ui.s(16), room = rr.w - 2 * pad;
                float yy = rr.y + ui.s(12);
                kicker(k, {rr.x + pad, yy}, accent.a ? accent : ui.theme().textDim, 13, room);
                yy += lineHeight(ui, FontFace::Bold, ui.s(fz(13))) + ui.s(4);
                bigValue(v, {rr.x + pad, yy}, 30, room, accent.a ? accent : ui.theme().text);
                const float df = ui.fitFont(d, FontFace::Body, fz(13), room, 0.85f);
                ui.label(ui.elide(d, FontFace::Body, df, room), {rr.x + pad, rr.bottom() - ui.s(12)}, df, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Bottom);
            };
            tile({area.x, area.y, tw, th}, "RECORD (PB)", clockLong(r.pb), pbDay.empty() ? std::string("meilleure partie complète") : "partie complète du " + pbDay,
                 {0, 0, 0, 0});
            tile({area.x + tw + tg, area.y, tw, th}, "TEMPS PERDU POSSIBLE", r.pb > 0.0f && sob > 0.0f ? frNum(std::max(0.0f, r.pb - sob), 3) + " s" : "—",
                 "PB − SOB, en théorie", ui.theme().warm);
            tile({area.x, area.y + th + tg, tw, th}, "ARRIVÉES", std::to_string(r.finished) + " / " + std::to_string(r.attempts),
                 r.attempts > 0 ? pct(static_cast<double>(r.finished) / r.attempts) + " des tentatives" : std::string("—"), {0, 0, 0, 0});
            tile({area.x + tw + tg, area.y + th + tg, tw, th}, "RÉGULARITÉ", reg.sd ? "± " + frNum(*reg.sd, 1) + " s" : std::string("—"),
                 "écart type des 20 dernières parties", {0, 0, 0, 0});
            y += wide ? hh + gap : 2 * hh + 2 * gap;
        }
    }
    // ----- A row per segment of the record's route.
    {
        const float cols[7] = {0.17f, 0.245f, 0.15f, 0.09f, 0.12f, 0.105f, 0.12f};  // picture, passage, best, in the PB, lost, consistency, actions
        float colX[8];
        colX[0] = x;
        for (int i = 0; i < 7; ++i) colX[i + 1] = colX[i] + w * cols[i];
        const char* heads[6] = {"SEGMENT (APERÇU DU NIVEAU)", "PASSAGE · CE QU'ON Y TROUVE", "MEILLEUR (DANS LA SOB)", "DANS LE PB", "TEMPS PERDU", "CONSTANCE"};
        const float hf = fz(12);
        for (int i = 0; i < 6; ++i) {
            const float room = colX[i + 1] - colX[i] - ui.s(12);
            kicker(heads[i], {colX[i] + (i == 0 ? ui.s(14) : ui.s(6)), y}, ui.theme().textDim, 12, room);
        }
        y += lineHeight(ui, FontFace::Bold, ui.s(hf)) + ui.s(10);
        std::vector<const SegmentReport*> repOf;
        for (const auto& p : parts) {
            const SegmentReport* found = nullptr;
            for (const auto& s : reps)
                if (s.from == p.from && s.to == p.to) found = &s;
            repOf.push_back(found);
        }
        // Without a record, the segments as kept.
        std::vector<playstats::SobPart> rows = parts;
        if (rows.empty())
            for (const auto& s : reps) rows.push_back({s.from, s.to, s.index, s.best, playstats::SobSource::Run}), repOf.push_back(&s);
        float worstLost = 0.0f;
        for (const SegmentReport* s : repOf)
            if (s) worstLost = std::max(worstLost, s->lost);
        const float rowH = ui.s(112), rg = ui.s(10);
        for (size_t i = 0; i < rows.size(); ++i) {
            const playstats::SobPart& p = rows[i];
            const SegmentReport* rep = i < repOf.size() ? repOf[i] : nullptr;
            const Rect row{x, y, w, rowH};
            const std::string wid = "seg" + std::to_string(i);
            const bool lit = ui.focused(wid + "/train") || ui.mouseOver(row);
            ui.r().fillRoundedRect(row, ui.s(12), kCard);
            ui.r().drawRoundedRect(row, ui.s(12), lit ? ui.theme().accent : kCardLine, std::max(1.0f, ui.s(lit ? 1.5f : 1.0f)));
            const float a = atOf(p.from, false), b = atOf(p.to, true);
            // The picture: the level drawn between the two checkpoints, its number.
            const Rect pic{colX[0] + ui.s(2), row.y + ui.s(2), colX[1] - colX[0] - ui.s(4), rowH - ui.s(4)};
            ui.r().pushClip(pic);
            levelPicture(levelId, a, b, pic);
            ui.r().popClip();
            {
                const std::string no = std::to_string(p.index + 1);
                const Rect badge{pic.x + ui.s(8), pic.y + ui.s(8), ui.s(28), ui.s(28)};
                ui.r().fillRoundedRect(badge, ui.s(6), Color{5, 7, 15, 220});
                ui.label(no, badge.center(), 16, ui.theme().text, Align::Center, FontFace::Title, VAlign::Middle);
            }
            const float cy = row.y + ui.s(16);
            // The passage: where it goes, what is met there.
            {
                const float px0 = colX[1] + ui.s(16), room = colX[2] - px0 - ui.s(12);
                const std::string nm = checkpointName(p.from, false) + " → " + checkpointName(p.to, true);
                const float nf = ui.fitFont(nm, FontFace::Bold, fz(18), room, 0.8f);
                ui.label(ui.elide(nm, FontFace::Bold, nf, room), {px0, cy}, nf, ui.theme().text, Align::Left, FontFace::Bold);
                segmentChips(li, a, b, px0, row.bottom() - ui.s(14) - ui.s(42), room, ui.s(42));
            }
            // The best (its part of the Sum of Best) and where it was made.
            {
                const float bx = colX[2] + ui.s(6), room = colX[3] - bx - ui.s(8);
                bigValue(clockLong(p.best), {bx, cy}, 26, room, kGold);
                if (p.source != playstats::SobSource::None) {
                    const bool tr = p.source == playstats::SobSource::Training;
                    const std::string src = tr ? "Entraînement" : "Partie";
                    const float sf = fz(13);
                    const float sw = ui.text().measure(src, FontFace::Bold, ui.s(sf)).x + ui.s(18);
                    const Rect pr{bx, cy + lineHeight(ui, FontFace::Title, ui.s(26)) + ui.s(8), std::min(sw, room), lineHeight(ui, FontFace::Bold, ui.s(sf)) + ui.s(6)};
                    const Color sc = tr ? kSrcTraining : kSrcRun;
                    ui.r().fillRoundedRect(pr, pr.h * 0.5f, sc.withAlpha(60));
                    ui.r().drawRoundedRect(pr, pr.h * 0.5f, sc, std::max(1.0f, ui.s(1.0f)));
                    ui.label(ui.elide(src, FontFace::Bold, sf, pr.w - ui.s(12)), pr.center(), sf, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
                }
            }
            // In the PB: the segment's time in the record run (not kept by these statistics yet).
            ui.label("—", {colX[3] + ui.s(6), row.center().y}, 22, ui.theme().textDim, Align::Left, FontFace::Title, VAlign::Middle);
            // Lost on average: the median of its last times − its best.
            {
                const float lx = colX[4] + ui.s(6), room = colX[5] - lx - ui.s(12);
                if (rep && rep->count > 0) {
                    const bool worst = rep->lost > 0.0f && rep->lost >= worstLost - 1e-4f;
                    ui.label("+" + frNum(rep->lost, 3) + " s", {lx, cy}, fz(18), worst ? ui.theme().danger : ui.theme().text, Align::Left, FontFace::Bold);
                    const Rect tr{lx, cy + lineHeight(ui, FontFace::Bold, ui.s(fz(18))) + ui.s(8), room, ui.s(8)};
                    ui.r().fillRoundedRect(tr, ui.s(4), kTrack);
                    const float k = worstLost > 0.0f ? std::clamp(rep->lost / worstLost, 0.0f, 1.0f) : 0.0f;
                    ui.r().fillRoundedRect({tr.x, tr.y, std::max(ui.s(6), tr.w * k), tr.h}, ui.s(4), worst ? ui.theme().danger : ui.theme().warm);
                    const float mf = fz(12);
                    ui.label(ui.elide("médiane − meilleur", FontFace::Body, mf, room), {lx, tr.bottom() + ui.s(6)}, mf, ui.theme().textDim);
                } else {
                    ui.label("—", {lx, row.center().y}, fz(18), ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
                }
            }
            // Consistency.
            {
                const float kx = colX[5] + ui.s(6), room = colX[6] - kx - ui.s(8);
                if (rep && rep->count > 0) {
                    ui.label(frNum(rep->spread * 100.0, 1) + " %", {kx, cy}, fz(18), ui.theme().text, Align::Left, FontFace::Bold);
                    const std::string d = "dispersion · " + std::to_string(rep->count) + (rep->count > 1 ? " passages" : " passage");
                    const float df = ui.fitFont(d, FontFace::Body, fz(13), room, 0.8f);
                    ui.label(ui.elide(d, FontFace::Body, df, room), {kx, cy + lineHeight(ui, FontFace::Bold, ui.s(fz(18))) + ui.s(6)}, df, ui.theme().textDim);
                } else {
                    ui.label("—", {kx, row.center().y}, fz(18), ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
                }
            }
            // Its training (the section training from its first checkpoint).
            {
                UIContext::ButtonStyle st;
                st.small = true;
                st.fontSize = 16;
                st.enabled = g_.content.level(levelId) != nullptr;
                const Rect br{colX[6] + ui.s(4), row.center().y - ui.s(20), x + w - ui.s(14) - colX[6] - ui.s(4), ui.s(40)};
                if (cframe::button(ui, wid + "/train", br, ui.elide("S'entraîner", FontFace::Bold, 16, br.w - ui.s(16)), st)) trainSegment(levelId, p.from);
            }
            y += rowH + rg;
        }
        y += ui.s(6);
    }
    // ----- The record and the Sum of Best over time; the zones to work on.
    const bool side = w >= ui.s(1300);
    {
        const float ch = ui.s(410);
        const float lw = side ? (w - gap) * 0.613f : w;
        // Evolution.
        {
            Series pb{"Record (PB)", ui.theme().accent, Series::Kind::Step, false, {}};
            Series so{"Somme des meilleurs", kSeries[0], Series::Kind::Step, false, {}};
            float lo = 1e9f, hi = 0.0f;
            for (const auto& h : r.history) {
                if (h.pb > 0.0f) pb.pts.push_back({static_cast<float>(h.day), h.pb}), lo = std::min(lo, h.pb), hi = std::max(hi, h.pb);
                if (h.sob > 0.0f) so.pts.push_back({static_cast<float>(h.day), h.sob}), lo = std::min(lo, h.sob), hi = std::max(hi, h.sob);
            }
            // Up to today: the values stay what they were.
            if (!pb.pts.empty() && pb.pts.back().x < static_cast<float>(today_)) pb.pts.push_back({static_cast<float>(today_), pb.pts.back().y});
            if (!so.pts.empty() && so.pts.back().x < static_cast<float>(today_)) so.pts.push_back({static_cast<float>(today_), so.pts.back().y});
            if (!pb.pts.empty()) pb.endLabel = clockTime(pb.pts.back().y);
            if (!so.pts.empty()) so.endLabel = clockTime(so.pts.back().y);
            LineSpec spec;
            spec.series = {pb, so};
            spec.bands.push_back({0, 1, ui.theme().warm.withAlpha(40)});
            spec.endLabels = true;
            const float pad = hi > lo ? std::max(1.0f, (hi - lo) * 0.2f) : 5.0f;
            spec.yMin = hi > 0.0f ? std::max(0.0f, std::floor((lo - pad) / 5.0f) * 5.0f) : 0.0f;
            spec.yMax = hi > 0.0f ? std::ceil((hi + pad) / 5.0f) * 5.0f : 60.0f;
            spec.yLabel = [](float v) { return clockTime(v); };
            spec.xLabel = [](float v) { return dayShort(static_cast<int>(v)); };
            auto hist = r.history;
            const Color pbc = ui.theme().accent, sbc = kSeries[0], gapc = ui.theme().warm;
            spec.tip = [hist, pbc, sbc, gapc](float xv, Tip& t) {
                t.title = dayNamed(static_cast<int>(xv));
                const SobPoint* p = nullptr;
                for (const auto& h : hist)
                    if (h.day <= static_cast<int>(xv)) p = &h;
                if (!p) return;
                t.rows.push_back({pbc, "Record (PB)", clockTime(p->pb)});
                t.rows.push_back({sbc, "Somme des meilleurs", clockTime(p->sob)});
                if (p->pb > 0.0f && p->sob > 0.0f) t.rows.push_back({gapc, "Temps gagnable", clockTime(std::max(0.0f, p->pb - p->sob))});
            };
            std::vector<std::vector<std::string>> rows;
            for (auto it = r.history.rbegin(); it != r.history.rend(); ++it)
                rows.push_back({dayLong(it->day), clockTime(it->pb), clockTime(it->sob),
                                it->pb > 0.0f && it->sob > 0.0f ? clockTime(std::max(0.0f, it->pb - it->sob)) : std::string("—")});
            const std::vector<LegendItem> leg = {{ui.theme().accent, "Record (PB)", 0, {}}, {kSeries[0], "Somme des meilleurs", 0, {}},
                                                 {ui.theme().warm.withAlpha(140), "Temps gagnable", 2, {}}};
            CardHead h;
            h.title = "ÉVOLUTION DU RECORD ET DE LA SOMME DES MEILLEURS";
            h.sub = "Un point à chaque date où l'un des deux a changé · la bande = temps gagnable (PB − SOB)";
            h.table = "sob";
            h.extra = [&](float right, float yy, bool draw) {
                const float l = legendWidth(leg);
                if (draw) legend(right - l, yy + ui.s(6), l + ui.s(2), leg);
                return l;
            };
            Rect in = card({x, y, lw, ch}, h);
            if (tables_["sob"]) tableIn("sob", in, {"Jour", "Record (PB)", "Somme des meilleurs", "Temps gagnable"}, rows);
            else if (pb.pts.empty() && so.pts.empty()) emptyIn("sob", in, "Finissez le niveau depuis son début pour voir votre record et votre somme des meilleurs.");
            else charts::lineChart(ui, "c/sob", in, spec, cursorOf("sob"), tip_);
        }
        // Zones to work on: the segments where the most time is lost, with their picture.
        {
            const Rect zr = side ? Rect{x + lw + gap, y, w - lw - gap, ch} : Rect{x, y + ch + gap, w, ch};
            CardHead h;
            h.title = "ZONES À TRAVAILLER";
            h.sub = "Temps perdu en moyenne : médiane des derniers temps − meilleur";
            h.table = "zones";
            std::vector<SegmentReport> ranked = reps;
            std::stable_sort(ranked.begin(), ranked.end(), [](const SegmentReport& a, const SegmentReport& b) { return a.lost > b.lost; });
            Rect in = card(zr, h);
            std::vector<charts::IconBar> bars;
            std::vector<std::vector<std::string>> trows;
            const float most = ranked.empty() ? 1.0f : std::max(0.05f, ranked.front().lost);
            for (size_t i = 0; i < ranked.size(); ++i) {
                const SegmentReport& s = ranked[i];
                const std::string nm = std::to_string(s.index + 1) + " · " + checkpointName(s.from, false) + " → " + checkpointName(s.to, true);
                const float a = atOf(s.from, false), b = atOf(s.to, true);
                bars.push_back({nm, s.lost, "+" + frNum(s.lost, 1) + " s", {}, i == 0 && s.lost > 0.0f ? ui.theme().danger : ui.theme().warm,
                                [this, levelId, a, b](const Rect& box) { levelPicture(levelId, a, b, box); }, "Segment " + nm,
                                {{charts::kNoSwatch, "Meilleur", clockTime(s.best)}, {charts::kNoSwatch, "Médiane", clockTime(s.median)},
                                 {charts::kNoSwatch, "Perdu en moyenne", clockTime(s.lost)},
                                 {charts::kNoSwatch, "Temps gardés", std::to_string(s.count)}}});
                trows.push_back({nm, clockTime(s.best), clockTime(s.median), clockTime(s.lost), std::to_string(s.count)});
            }
            charts::IconBarsStyle st;
            st.rowH = 64.0f;
            st.iconW = 120.0f;
            st.iconH = 54.0f;
            st.iconFrame = false;
            if (tables_["zones"]) tableIn("zones", in, {"Segment", "Meilleur", "Médiane", "Perdu", "Temps"}, trows);
            else if (bars.empty()) emptyIn("zones", in, "Pas encore de segment chronométré.");
            else {
                const size_t fit = std::max<size_t>(1, static_cast<size_t>(in.h / ui.s(st.rowH)));
                if (bars.size() > fit) bars.resize(fit);  // the worst ones (the table has every one)
                charts::iconBars(ui, "c/zones", {in.x, in.y, in.w, charts::iconBarsHeight(ui, bars.size(), st)}, bars, most, cursorOf("zones"), tip_, st);
            }
        }
        y += side ? ch + gap : 2 * ch + 2 * gap;
    }
    // ----- Consistency per segment; regularity of the full runs.
    {
        const float ch = ui.s(400);
        const float lw = side ? (w - gap) * 0.476f : w;
        {
            CardHead h;
            h.title = "CONSTANCE PAR SEGMENT";
            h.sub = "Plus la dispersion est faible, plus vos temps se ressemblent";
            h.table = "consistency";
            Rect in = card({x, y, lw, ch}, h);
            std::vector<Bar> spread, near;
            std::vector<std::vector<std::string>> rows;
            float maxSpread = 0.05f;
            for (const auto& s : reps) maxSpread = std::max(maxSpread, s.spread);
            for (const auto& s : reps) {
                const std::string label = std::to_string(s.index + 1);
                const std::string title = "Segment " + label + " : " + checkpointName(s.from, false) + " → " + checkpointName(s.to, true);
                spread.push_back({label, s.spread, frNum(s.spread * 100.0, 1) + " %", ui.theme().accent, title,
                                  {{charts::kNoSwatch, "Écart interquartile ÷ médiane", pct(s.spread)}, {charts::kNoSwatch, "Temps gardés", std::to_string(s.count)}}});
                const int n = static_cast<int>(std::lround(s.nearBest * static_cast<float>(s.count)));
                near.push_back({label, s.nearBest, pct(s.nearBest), kSeries[0], title,
                                {{charts::kNoSwatch, "À moins de 5 % du meilleur", std::to_string(n) + " sur " + std::to_string(s.count)},
                                 {charts::kNoSwatch, "Meilleur", clockTime(s.best)}}});
                rows.push_back({title, pct(s.spread), pct(s.nearBest), std::to_string(s.count)});
            }
            if (tables_["consistency"]) {
                tableIn("consistency", in, {"Segment", "Dispersion", "À moins de 5 % du meilleur", "Temps"}, rows);
            } else if (reps.empty()) {
                emptyIn("spread", in, "Pas encore de segment chronométré.");
            } else {
                const float cg = ui.s(24), cw = (in.w - cg) / 2.0f;
                const float kh = lineHeight(ui, FontFace::Bold, ui.s(fz(12))) + ui.s(10);
                kicker("DISPERSION (ÉCART INTERQUARTILE ÷ MÉDIANE)", {in.x, in.y}, ui.theme().textDim, 12, cw);
                kicker("TEMPS À MOINS DE 5 % DU MEILLEUR", {in.x + cw + cg, in.y}, ui.theme().textDim, 12, cw);
                const float bh = std::min(charts::hBarsHeight(ui, reps.size()), in.h - kh);
                charts::hBars(ui, "c/spread", {in.x, in.y + kh, cw, bh}, spread, maxSpread, cursorOf("spread"), tip_);
                charts::hBars(ui, "c/near", {in.x + cw + cg, in.y + kh, cw, bh}, near, 1.0f, cursorOf("near"), tip_);
            }
        }
        // Regularity.
        {
            const Rect rr = side ? Rect{x + lw + gap, y, w - lw - gap, ch} : Rect{x, y + ch + gap, w, ch};
            CardHead h;
            h.title = "RÉGULARITÉ DES PARTIES COMPLÈTES";
            h.sub = "Temps des 20 dernières parties complètes, et la médiane des 5 dernières";
            h.table = "regularity";
            Rect in = card(rr, h);
            const std::string trend = !reg.sd ? std::string("au moins 3 parties complètes")
                                      : !reg.sdBefore ? std::string("pas assez de parties il y a 30 jours")
                                      : reg.trend > 0 ? std::string("▼ plus régulier qu'il y a 30 j")
                                      : reg.trend < 0 ? std::string("▲ moins régulier qu'il y a 30 j")
                                                      : std::string("= aussi régulier qu'il y a 30 j");
            std::vector<float> lastTimes;
            for (const auto& f : reg.last) lastTimes.push_back(f.time);
            const float tg = ui.s(10), tw = (in.w - 2 * tg) / 3.0f, th = ui.s(84);
            miniTile({in.x, in.y, tw, th}, "ARRIVÉES AU BOUT", reg.attempts > 0 ? pct(static_cast<double>(reg.finished) / reg.attempts) : std::string("—"),
                     std::to_string(reg.finished) + " sur " + std::to_string(reg.attempts) + " tentatives");
            miniTile({in.x + tw + tg, in.y, tw, th}, "ÉCART TYPE (20)", reg.sd ? frNum(*reg.sd, 1) + " s" : std::string("—"), trend,
                     reg.trend > 0 ? ui.theme().success : reg.trend < 0 ? ui.theme().warm : Color{0, 0, 0, 0});
            miniTile({in.x + 2 * (tw + tg), in.y, tw, th}, "MÉDIANE (20)", lastTimes.empty() ? std::string("—") : clockTime(medianOf(lastTimes)), "temps d'arrivée");
            const Rect plot{in.x, in.y + th + ui.s(12), in.w, in.h - th - ui.s(12)};
            Series dots{"Chaque partie", ui.theme().accent, Series::Kind::Dots, false, {}};
            Series med{"Médiane des 5 dernières", ui.theme().text, Series::Kind::Line, false, {}};
            Series pbLine{"PB", ui.theme().accent, Series::Kind::Line, false, {}};
            pbLine.dashed = true;
            float lo = 1e9f, hi = 0.0f;
            const int firstN = r.finished - static_cast<int>(reg.last.size()) + 1;
            std::vector<std::vector<std::string>> rows;
            for (size_t i = 0; i < reg.last.size(); ++i) {
                const float n = static_cast<float>(firstN + static_cast<int>(i));
                dots.pts.push_back({n, reg.last[i].time});
                med.pts.push_back({n, reg.rollingMedian[i]});
                lo = std::min(lo, reg.last[i].time);
                hi = std::max(hi, reg.last[i].time);
            }
            if (r.pb > 0.0f && !reg.last.empty()) {
                pbLine.pts = {{static_cast<float>(firstN), r.pb}, {static_cast<float>(firstN + static_cast<int>(reg.last.size()) - 1), r.pb}};
                pbLine.endLabel = "PB";
                lo = std::min(lo, r.pb);
            }
            for (size_t i = reg.last.size(); i-- > 0;)
                rows.push_back({std::to_string(firstN + static_cast<int>(i)), dayLong(reg.last[i].day), clockTime(reg.last[i].time), clockTime(reg.rollingMedian[i])});
            LineSpec spec;
            spec.series = {dots, med};
            if (!pbLine.pts.empty()) spec.series.push_back(pbLine);
            spec.integerX = true;
            spec.endLabels = !pbLine.pts.empty();
            if (spec.endLabels) spec.series[1].endLabel = " ";  // only « PB » is written at the end
            const float pad = std::max(1.0f, (hi - lo) * 0.25f);
            spec.yMin = reg.last.empty() ? 0.0f : std::max(0.0f, std::floor(lo - pad));
            spec.yMax = reg.last.empty() ? 60.0f : std::ceil(hi + pad);
            spec.yLabel = [](float v) { return clockTime(v); };
            spec.xLabel = [](float v) { return "n° " + std::to_string(static_cast<int>(std::lround(v))); };
            auto last = reg.last;
            auto medians = reg.rollingMedian;
            const float pbv = r.pb;
            spec.tip = [last, medians, firstN, pbv](float xv, Tip& t) {
                const int i = static_cast<int>(std::lround(xv)) - firstN;
                if (i < 0 || i >= static_cast<int>(last.size())) return;
                t.title = "Partie n° " + std::to_string(static_cast<int>(std::lround(xv))) + " · " + dayShort(last[static_cast<size_t>(i)].day);
                const float tm = last[static_cast<size_t>(i)].time;
                t.rows.push_back({charts::kNoSwatch, "Temps", clockTime(tm) + (pbv > 0.0f && std::abs(tm - pbv) < 0.005f ? " (record)" : "")});
                t.rows.push_back({charts::kNoSwatch, "Médiane des 5", clockTime(medians[static_cast<size_t>(i)])});
            };
            if (tables_["regularity"]) tableIn("regularity", plot, {"Partie", "Jour", "Temps", "Médiane des 5 dernières"}, rows);
            else if (reg.last.empty()) emptyIn("regularity", plot, "Aucune partie complète pour l'instant.");
            else charts::lineChart(ui, "c/regularity", plot, spec, cursorOf("regularity"), tip_);
        }
        y += side ? ch + gap : 2 * ch + 2 * gap;
    }
    y += note(x, y, w,
              "Comment c'est calculé : seuls comptent les segments complets (d'un point de passage au suivant, ou jusqu'à l'arrivée) des parties "
              "de la campagne jouées depuis le début d'un niveau classique (hors EXTRÊME), ni entraînement, ni replay, ni mode Invincible. Les "
              "temps viennent du chronomètre du niveau (les mêmes que la course aux fantômes). Somme des meilleurs (SOB) = meilleur temps de "
              "chaque segment du parcours de votre record, parties complètes et entraînement par segment (même version du niveau) confondus, "
              "additionnés ; l'entraînement ne compte que là : jamais dans le record, les temps des segments ni la régularité. Temps "
              "gagnable = record − SOB. Temps perdu = médiane des 50 derniers "
              "temps du segment − son meilleur temps. Dispersion = écart interquartile ÷ médiane. Régularité : écart type des 20 dernières "
              "parties complètes, comparé à celui des 20 parties d'avant il y a 30 jours (plus de 10 % d'écart pour parler d'un changement). "
              "Une nouvelle version du niveau repart de zéro. Les aperçus sont le vrai rendu du niveau entre deux points de passage.");
    return y - y0;
}

// ------------------------------------------------------------------ Passages difficiles

void StatisticsScene::hardCard(const Rect& r, const HardPassage& h, int rank) {
    UIContext& ui = g_.ui;
    const bool boss = h.kind == HardPassage::Kind::BossPhase;
    const std::string wid = "hard" + std::to_string(rank);
    const bool lit = ui.focused(wid) || ui.mouseOver(r);
    cardFrame(r, {0, 0, 0, 0}, lit);
    const statsart::LevelInfo* li = boss ? nullptr : art_.info(h.id);
    // Its picture: the boss on its planet, or the level where the passage is.
    const Rect im{r.x + ui.s(1), r.y + ui.s(1), r.w - ui.s(2), std::min(ui.s(150), r.h * 0.4f)};
    ui.r().pushClip(im);
    ui.r().fillRect(im, Color{11, 16, 32, 255});
    if (boss) {
        statsart::drawPlanetBanner(g_, statsart::bossPlace(g_, atlas_.get(), h.id).planetId, im, Color{255, 255, 255, 115});
        bossPortrait(h.id, {im.x + im.w * 0.15f, im.y + ui.s(8), im.w * 0.7f, im.h - ui.s(12)}, anim_ + static_cast<float>(rank) * 0.3f);
    } else {
        levelPicture(h.id, static_cast<float>(h.from) / kSlices, static_cast<float>(h.to + 1) / kSlices, im);
    }
    ui.r().popClip();
    {
        const Rect badge{im.x + ui.s(10), im.y + ui.s(10), ui.s(34), ui.s(34)};
        ui.r().fillRoundedRect(badge, ui.s(8), Color{5, 7, 15, 220});
        ui.label(std::to_string(rank + 1), badge.center(), 20, ui.theme().text, Align::Center, FontFace::Title, VAlign::Middle);
    }
    const float pad = ui.s(14), room = r.w - 2 * pad;
    float y = im.bottom() + ui.s(12);
    std::string name, l1, l2, from;
    if (boss) {
        const EnemyDefinition* def = g_.content.enemy(h.id);
        const std::string pname = def && h.phase < static_cast<int>(def->phases.size()) ? def->phases[static_cast<size_t>(h.phase)].name : std::string();
        name = h.name + " — phase " + std::to_string(h.phase + 1) + (pname.empty() ? "" : " : " + pname);
        l1 = std::to_string(h.deaths) + (h.deaths > 1 ? " défaites" : " défaite") + " sur " + std::to_string(h.passes) + (h.passes > 1 ? " arrivées" : " arrivée");
        l2 = h.attack.empty() ? std::string() : "Attaque fatale : " + h.attack;
    } else {
        name = levelShort(h.id) + " · tranches " + std::to_string(static_cast<int>(std::lround(h.from * 2.5))) + "–" +
               std::to_string(static_cast<int>(std::lround((h.to + 1) * 2.5))) + " %";
        l1 = std::to_string(h.deaths) + (h.deaths > 1 ? " morts" : " mort") + " sur " + std::to_string(h.passes) + (h.passes > 1 ? " passages" : " passage");
        if (li) {
            const float at = (static_cast<float>(h.from) + 0.5f) / kSlices;
            std::string a, b;
            bool found = false;
            for (const auto& [cpAt, id] : li->checkpoints) {
                if (cpAt <= at + 0.001f) a = id;
                else if (!found) b = id, found = true;
            }
            const float endAt = static_cast<float>(h.to + 1) / kSlices;
            from = a;
            l2 = li->checkpointName(a, false) + " → " + li->checkpointName(found ? b : std::string(), true);
            (void)endAt;
        }
    }
    const float nf = ui.fitFont(name, FontFace::Bold, fz(17), room, 0.75f);
    ui.label(ui.elide(name, FontFace::Bold, nf, room), {r.x + pad, y}, nf, ui.theme().text, Align::Left, FontFace::Bold);
    y += lineHeight(ui, FontFace::Bold, ui.s(nf)) + ui.s(4);
    for (const std::string& l : {l1, l2}) {
        if (l.empty()) continue;
        const float lf = ui.fitFont(l, FontFace::Body, fz(13), room, 0.85f);
        ui.label(ui.elide(l, FontFace::Body, lf, room), {r.x + pad, y}, lf, ui.theme().textDim);
        y += lineHeight(ui, FontFace::Body, ui.s(lf)) + ui.s(3);
    }
    y = std::max(y, r.bottom() - ui.s(14) - ui.s(42) - ui.s(14) - ui.s(10) - ui.s(10) - lineHeight(ui, FontFace::Body, ui.s(fz(14))));
    // Its difficulty: written, and its gauge.
    ui.label("Difficulté", {r.x + pad, y}, fz(14), ui.theme().textDim);
    ui.label(pct(h.difficulty), {r.right() - pad, y}, fz(14), ui.theme().text, Align::Right, FontFace::Bold);
    y += lineHeight(ui, FontFace::Body, ui.s(fz(14))) + ui.s(8);
    const Rect gauge{r.x + pad, y, room, ui.s(10)};
    ui.r().fillRoundedRect(gauge, ui.s(5), kTrack);
    const float gw = std::max(ui.s(8), gauge.w * std::clamp(h.difficulty, 0.0f, 1.0f));
    ui.r().fillRoundedRect({gauge.x, gauge.y, gw, gauge.h}, ui.s(5), ui.theme().warm);
    ui.r().fillGradientH({gauge.x + ui.s(4), gauge.y + ui.s(2), gw - ui.s(8), gauge.h - ui.s(4)}, ui.theme().warm, ui.theme().danger);
    // Its training.
    const std::string bt = boss ? "S'entraîner sur cette phase" : "S'entraîner sur ce segment";
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 15;
    st.enabled = boss ? g_.content.enemy(h.id) != nullptr : g_.content.level(h.id) != nullptr && li != nullptr;
    const Rect br{r.x + pad, r.bottom() - ui.s(14) - ui.s(42), room, ui.s(42)};
    const float bf = ui.fitFont(bt, FontFace::Bold, 15, br.w - ui.s(16), 0.75f);
    st.fontSize = bf;
    if (cframe::button(ui, wid, br, ui.elide(bt, FontFace::Bold, bf, br.w - ui.s(16)), st)) {
        if (boss) g_.app.switchScene(makeBossTraining(g_, h.id, h.phase), 0.3f);
        else trainSegment(h.id, from);  // the section training from the last checkpoint before the passage
    }
}

float StatisticsScene::drawHard(float x, float y0, float w) {
    UIContext& ui = g_.ui;
    float y = y0;
    auto lastDay = [this](const std::string& id) {
        auto it = data().levels.find(id);
        return it != data().levels.end() && it->second.any() ? it->second.lastDay : -1;
    };
    std::string info;
    if (auto it = data().levels.find(wantSlices_); it != data().levels.end()) {
        int deaths = 0;
        for (int d : it->second.deaths) deaths += d;
        info = std::to_string(deaths) + (deaths > 1 ? " morts dans ce niveau" : " mort dans ce niveau");
    }
    y += chooseLevel(x, y, w, "slices", wantSlices_, lastDay, info);
    const float gap = ui.s(16);
    if (lastDay(wantSlices_) < 0) {
        y += note(x, y, w, "Aucun passage n'est encore enregistré dans ce niveau : jouez-le pour voir où vous tombez.") + gap;
    } else {
        const LevelSlices& l = data().levels.at(wantSlices_);
        const statsart::LevelInfo* li = art_.info(wantSlices_);
        const auto ds = sliceDifficulties(l, today_);
        float mx = 0.05f;
        for (int i = 0; i < kSlices; ++i) mx = std::max(mx, ds[static_cast<size_t>(i)]);
        const std::vector<LegendItem> ramp = {{Color{0, 0, 0, 0}, "peu", 4, {}},
                                              {charts::heatSolid(0.1f), "", 2, {}},
                                              {charts::heatSolid(0.4f), "", 2, {}},
                                              {charts::heatSolid(0.7f), "", 2, {}},
                                              {charts::heatSolid(1.0f), "beaucoup de morts", 2, {}}};
        CardHead h;
        h.title = "OÙ VOUS TOMBEZ";
        h.sub = "Le niveau découpé en 40 tranches, sur son vrai décor : plus c'est chaud, plus on y meurt souvent";
        h.table = "heat";
        h.tableOff = "Carte";
        h.extra = [&](float right, float yy, bool draw) {
            const float lw = legendWidth(ramp);
            if (draw) legend(right - lw, yy + ui.s(6), lw + ui.s(2), ramp);
            return lw;
        };
        const float ch = ui.s(330);
        Rect in = card({x, y, w, ch}, h);
        std::vector<std::string> segOf(kSlices);  // the segment of each slice (« Racines → Clairière »)
        if (li)
            for (int i = 0; i < kSlices; ++i) {
                const float at = (static_cast<float>(i) + 0.5f) / kSlices;
                std::string a, b;
                bool found = false;
                for (const auto& [cpAt, id] : li->checkpoints) {
                    if (cpAt <= at) a = id;
                    else if (!found) b = id, found = true;
                }
                segOf[static_cast<size_t>(i)] = li->checkpointName(a, false) + " → " + li->checkpointName(found ? b : std::string(), true);
            }
        if (tables_["heat"]) {
            std::vector<std::vector<std::string>> rows;
            for (int i = 0; i < kSlices; ++i) {
                const size_t k = static_cast<size_t>(i);
                if (l.passes[k] <= 0) continue;
                rows.push_back({std::to_string(static_cast<int>(std::lround(i * 2.5))) + "–" + std::to_string(static_cast<int>(std::lround((i + 1) * 2.5))) + " %",
                                segOf[k].empty() ? std::string("—") : segOf[k], std::to_string(l.passes[k]), std::to_string(l.deaths[k]), pct(ds[k])});
            }
            tableIn("heat", in, {"Tranche", "Segment", "Passages", "Morts", "Difficulté"}, rows);
        } else {
            // The level's own picture, slice by slice, the heat over it; its checkpoints under it; the
            // difficulty of each slice in bars under them.
            const float af = fz(13), lh = lineHeight(ui, FontFace::Body, ui.s(af));
            const Rect strip{in.x, in.y, in.w, std::min(ui.s(107), in.h * 0.45f)};
            const float cw = strip.w / kSlices;
            const Rect bars{in.x, strip.bottom() + ui.s(12) + lh + ui.s(14), in.w, in.bottom() - (strip.bottom() + ui.s(12) + lh + ui.s(14))};
            bool hovered = false;
            const Rect hit{in.x, strip.y, in.w, in.bottom() - strip.y};
            ui.hotspot("c/heat", hit, &hovered);
            const bool focused = ui.focused("c/heat");
            int& cursor = cursorOf("heat");
            if (focused) {
                if (cursor < 0 || cursor >= kSlices) cursor = kSlices / 2;
                if (ui.acceptsInput()) {
                    if (ui.input().menuPressed(Action::MenuLeft)) cursor = std::max(0, cursor - 1);
                    if (ui.input().menuPressed(Action::MenuRight)) cursor = std::min(kSlices - 1, cursor + 1);
                    ui.consumeNavigation(true, false);
                }
            }
            ui.r().fillRoundedRect(strip, ui.s(8), Color{11, 16, 32, 255});
            const int cellPx = std::max(8, static_cast<int>(std::lround(cw)));
            const int stripPx = std::max(16, static_cast<int>(std::lround(strip.h / 8.0f)) * 8);
            if (const Texture* t = art_.sliceStrip(wantSlices_, cellPx, stripPx)) {
                ui.r().pushClip(strip);
                g_.app.renderer().drawTexture(*t, nullptr, strip, Color{215, 215, 215, 255});
                ui.r().popClip();
            } else {
                picturePending(strip, art_.failed(wantSlices_));
                // Meanwhile (or for a level that cannot be drawn), its relief.
                if (li && li->relief.size() >= 2) {
                    std::vector<Vec2> pts;
                    for (size_t i = 0; i < li->relief.size(); ++i)
                        pts.push_back({strip.x + strip.w * static_cast<float>(i) / static_cast<float>(li->relief.size() - 1),
                                       strip.bottom() - std::clamp(li->relief[i], 0.0f, 1.0f) * strip.h * 0.85f});
                    for (size_t i = 1; i < pts.size(); ++i) ui.r().drawLine(pts[i - 1], pts[i], Color{75, 90, 128, 255}, std::max(1.0f, ui.s(1.5f)));
                }
            }
            for (int i = 0; i < kSlices; ++i) {
                const float v = ds[static_cast<size_t>(i)];
                if (v < 0.0f) continue;
                const Color c = charts::heatOver(v / mx);
                ui.r().fillRect({strip.x + cw * static_cast<float>(i), strip.y, cw - 1.0f, strip.h}, c.withAlpha(static_cast<uint8_t>(c.a * 0.55f)));
                ui.r().fillRect({strip.x + cw * static_cast<float>(i), strip.y, cw - 1.0f, strip.h}, c, BlendMode::Additive);
            }
            ui.r().drawRoundedRect(strip, ui.s(8), Color{38, 53, 90, 255}, std::max(1.0f, ui.s(1.0f)));
            // The checkpoints: a tick and its name (measured: a name that would touch the previous one is left out).
            {
                std::vector<std::pair<float, std::string>> marks = {{0.0f, "Départ"}};
                if (li)
                    for (const auto& [at, id] : li->checkpoints) marks.push_back({at, li->checkpointName(id, false)});
                marks.push_back({1.0f, "Arrivée"});
                float lastRight = -1e9f;
                const float ty = strip.bottom() + ui.s(2);
                for (size_t k = 0; k < marks.size(); ++k) {
                    const float mxp = strip.x + strip.w * std::clamp(marks[k].first, 0.0f, 1.0f);
                    ui.r().fillRect({mxp - ui.s(1), ty, ui.s(2), ui.s(10)}, ui.theme().textDim);
                    const float tw = ui.text().measure(marks[k].second, FontFace::Body, ui.s(af)).x;
                    float left = k == 0 ? mxp : k + 1 == marks.size() ? mxp - tw : mxp - tw * 0.5f;
                    left = std::clamp(left, strip.x, strip.right() - tw);
                    // The finish always shows: a checkpoint too close to it gives way.
                    if (k + 1 < marks.size() && left + tw + ui.s(12) > strip.right() - ui.text().measure("Arrivée", FontFace::Body, ui.s(af)).x) continue;
                    if (left < lastRight + ui.s(12)) continue;
                    ui.label(marks[k].second, {left, ty + ui.s(12)}, af, kText2);
                    lastRight = left + tw;
                }
            }
            // The bars.
            int at = -1;
            const Vec2 mp = ui.input().mousePos();
            if (hovered) at = std::clamp(static_cast<int>((mp.x - in.x) / cw), 0, kSlices - 1), cursor = at;
            else if (focused) at = cursor;
            for (int i = 0; i < kSlices; ++i) {
                const size_t k = static_cast<size_t>(i);
                const float bx = bars.x + cw * static_cast<float>(i) + ui.s(3), bw = std::max(1.0f, cw - ui.s(6));
                if (ds[k] < 0.0f) {
                    ui.r().fillRect({bx, bars.bottom() - ui.s(2), bw, ui.s(2)}, Color{38, 53, 90, 255});
                    continue;
                }
                const float bh = std::max(ui.s(3), bars.h * std::clamp(ds[k] / mx, 0.0f, 1.0f));
                ui.r().fillRoundedRect({bx, bars.bottom() - bh, bw, bh}, std::min(ui.s(3), bh * 0.5f), charts::heatSolid(ds[k] / mx));
            }
            if (focused) ui.r().drawRoundedRect(hit.expanded(ui.s(4)), ui.s(8), ui.theme().accent.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
            if (at >= 0) {
                const size_t k = static_cast<size_t>(at);
                const Rect c{strip.x + cw * static_cast<float>(at), strip.y, cw, strip.h};
                ui.r().drawRect(c, Color::white(), std::max(1.0f, ui.s(2.0f)));
                ui.r().drawRect({bars.x + cw * static_cast<float>(at), bars.y, cw, bars.h}, kText2.withAlpha(90), 1.0f);
                tip_ = Tip{};
                tip_.show = true;
                tip_.anchor = {c.center().x, c.y};
                tip_.title = "Tranche " + std::to_string(static_cast<int>(std::lround(at * 2.5f))) + "–" + std::to_string(static_cast<int>(std::lround((at + 1) * 2.5f))) +
                             " %" + (segOf[k].empty() ? std::string() : " · " + segOf[k]);
                tip_.rows.push_back({charts::kNoSwatch, "Morts / passages", std::to_string(l.deaths[k]) + " sur " + std::to_string(l.passes[k])});
                if (ds[k] >= 0.0f) tip_.rows.push_back({charts::kNoSwatch, "Difficulté (lissée)", pct(ds[k])});
            }
        }
        y += ch + gap;
    }
    // The 5 hardest passages, every level and boss phase together, each with its picture.
    const auto top = hardestPassages(data(), today_, 5);
    y += sectionTitle(x, y, w, "VOS 5 PASSAGES LES PLUS DIFFICILES", "tous les niveaux et toutes les phases de boss", {}, {}) + ui.s(12);
    if (top.empty()) {
        y += note(x, y, w, "Aucun passage ne se détache encore (il en faut au moins 5 passages pour en juger).") + gap;
    } else {
        const int per = w >= ui.s(1500) ? 5 : w >= ui.s(900) ? 3 : 1;
        const float cg = ui.s(16), cw = (w - cg * static_cast<float>(per - 1)) / static_cast<float>(per), ch = ui.s(390);
        for (size_t i = 0; i < top.size(); ++i) {
            const Rect r{x + (cw + cg) * static_cast<float>(static_cast<int>(i) % per), y + (ch + cg) * static_cast<float>(static_cast<int>(i) / per), cw, ch};
            hardCard(r, top[i], static_cast<int>(i));
        }
        y += (ch + cg) * static_cast<float>((static_cast<int>(top.size()) + per - 1) / per);
    }
    y += note(x, y, w,
              "Algorithme : pour chaque tranche, difficulté = (morts + 1) ÷ (passages + 2), un lissage qui évite qu'une seule mort rende une "
              "tranche « impossible ». Les parties récentes pèsent plus (demi-vie de 14 jours) pour suivre vos progrès. Les tranches voisines "
              "au-dessus de 2 × la médiane du niveau sont fusionnées en un passage, retenu seulement avec au moins 5 passages ; sa difficulté = "
              "1 − le produit des (1 − difficulté de chaque tranche). Phase de boss : défaites dans la phase ÷ tentatives arrivées à la phase "
              "(au moins 5). Le classement mélange tous les niveaux et les phases de boss. « S'entraîner sur ce segment » ouvre l'entraînement "
              "par segment depuis le point de passage précédent ; « S'entraîner sur cette phase », l'entraînement contre le boss sur cette phase. "
              "La carte est le vrai rendu du niveau, tranche par tranche le long de son parcours.");
    return y - y0;
}

}  // namespace cosmic
