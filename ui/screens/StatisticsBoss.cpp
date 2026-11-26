// Statistiques › Boss (2.54; 2.91: every boss on its planet's banner): three bosses at a time — the ones
// fought first, the most recent first, then every other boss of the game — each with its portrait (its
// sprite) on the banner of the planet where it is fought; their best progress attempt after attempt
// (one colour per boss, its name at the end of its line, a star at its first victory); the sheet of
// the boss chosen and where its fights stop (defeats per phase), with its training on the hardest
// phase and its full sheet (« Fiches des boss », BossStatsScene).

#include <algorithm>
#include <cmath>

#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/planets/PlanetData.h"
#include "game/records/BossStats.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/StatisticsScene.h"

namespace cosmic {

using charts::Bar;
using charts::LineSpec;
using charts::Series;
using namespace statsui;

std::string StatisticsScene::bossName(const std::string& id) const {
    auto it = data().bosses.find(id);
    if (it != data().bosses.end() && !it->second.name.empty()) return it->second.name;
    if (const EnemyDefinition* d = g_.content.enemy(id)) return d->name;
    return id;
}

void StatisticsScene::bossPortrait(const std::string& id, const Rect& box, float t) {
    statsart::drawEnemy(g_, id, box, t, "idle", true);
}

void StatisticsScene::bossBanner(const Rect& r, const std::string& id, Color stripe, bool selected, int index) {
    UIContext& ui = g_.ui;
    const std::string wid = "boss" + std::to_string(index);
    bool hovered = false;
    if (ui.hotspot(wid, r, &hovered)) selBoss_ = id;
    const bool lit = selected || hovered || ui.focused(wid);
    ui.r().fillRoundedRect(r, ui.s(12), kCard);
    // The planet where it is fought, behind it; its colour (the curve's) at the left.
    const statsart::BossPlace place = statsart::bossPlace(g_, atlas_.get(), id);
    ui.r().pushClip(r.expanded(-ui.s(1)));
    statsart::drawPlanetBanner(g_, place.planetId, r, Color{255, 255, 255, 95});
    ui.r().fillGradientH({r.x + r.w * 0.35f, r.y, r.w * 0.65f, r.h}, Color{8, 11, 24, 40}, Color{8, 11, 24, 200});
    const float portraitW = std::min(r.w * 0.42f, ui.s(260));
    bossPortrait(id, {r.x + ui.s(18), r.y + ui.s(12), portraitW, r.h - ui.s(20)}, anim_ + static_cast<float>(index) * 0.41f);
    ui.r().popClip();
    if (stripe.a) ui.r().fillRoundedRect({r.x, r.y + ui.s(8), ui.s(6), r.h - ui.s(16)}, ui.s(3), stripe);
    if (selected) {
        ui.r().drawRoundedRect(r.expanded(ui.s(2)), ui.s(14), ui.theme().accent.withAlpha(80), std::max(1.0f, ui.s(3.0f)));
        ui.r().drawRoundedRect(r, ui.s(12), ui.theme().accent, std::max(1.0f, ui.s(1.5f)));
    } else {
        ui.r().drawRoundedRect(r, ui.s(12), lit ? ui.theme().accent.withAlpha(150) : kCardLine, std::max(1.0f, ui.s(1.0f)));
    }
    // Its name, its title and planet, four numbers.
    const float ix = r.x + ui.s(18) + portraitW + ui.s(12), room = r.right() - ui.s(16) - ix;
    if (room < ui.s(60)) return;
    float y = r.y + ui.s(14);
    {
        const std::string n = bossName(id);
        const float nf = ui.fitFont(n, FontFace::Title, 22, room, 0.6f);
        ui.label(ui.elide(n, FontFace::Title, nf, room), {ix, y}, nf, ui.theme().text, Align::Left, FontFace::Title);
        y += lineHeight(ui, FontFace::Title, ui.s(nf)) + ui.s(2);
    }
    const EnemyDefinition* def = g_.content.enemy(id);
    std::string sub = def && !def->bossTitle.empty() && def->bossTitle != def->name ? def->bossTitle : std::string();
    if (const PlanetData* p = g_.content.planet(place.planetId)) {
        const std::string where = p->name + (place.levelNumber > 0 ? " " + std::to_string(place.levelNumber) : std::string());
        sub = sub.empty() ? where : sub + " · " + where;
    }
    if (!sub.empty()) {
        kicker(sub, {ix, y}, ui.theme().textDim, 12, room, true);
        y += lineHeight(ui, FontFace::Bold, ui.s(fz(12))) + ui.s(10);
    } else {
        y += ui.s(10);
    }
    const auto it = data().bosses.find(id);
    const BossRecord* rec = g_.bossStats.find(id);
    std::string attempts = "0", wins = rec ? std::to_string(rec->kills) : std::string("0"), best = "—", after = "pas encore";
    bool won = false;
    if (it != data().bosses.end()) {
        const BossHistory& b = it->second;
        attempts = std::to_string(b.attempts);
        wins = std::to_string(b.wins);
        won = b.best() >= 0.999f;
        best = won ? std::string("vaincu") : pct(b.best());
        after = b.firstWin > 0 ? std::to_string(b.firstWin) + (b.firstWin > 1 ? " tentatives" : " tentative")
                : rec && rec->kills > 0 ? std::string("avant le suivi")
                                        : std::string("pas encore");
    } else if (rec && rec->kills > 0) {
        after = "avant le suivi";
    }
    const std::pair<std::string, std::string> cells[4] = {{"Tentatives", attempts}, {"Victoires", wins}, {"Meilleur", best}, {"Victoire après", after}};
    const float cw = (room - ui.s(10)) / 2.0f;
    const float kf = fz(12), vf = fz(18), kh = lineHeight(ui, FontFace::Body, ui.s(kf)), vh = lineHeight(ui, FontFace::Bold, ui.s(vf));
    for (int i = 0; i < 4; ++i) {
        const float cx = ix + (cw + ui.s(10)) * static_cast<float>(i % 2), cy = y + (kh + vh + ui.s(8)) * static_cast<float>(i / 2);
        if (cy + kh + vh > r.bottom() - ui.s(6)) break;  // a small card: what fits
        ui.label(ui.elide(cells[i].first, FontFace::Body, kf, cw), {cx, cy}, kf, ui.theme().textDim);
        const float f = ui.fitFont(cells[i].second, FontFace::Bold, vf, cw, 0.7f);
        ui.label(ui.elide(cells[i].second, FontFace::Bold, f, cw), {cx, cy + kh}, f, i == 2 && won ? kGold : ui.theme().text, Align::Left, FontFace::Bold);
    }
}

void StatisticsScene::bossSheet(const Rect& r, const std::string& id, Color swatch) {
    UIContext& ui = g_.ui;
    cardFrame(r);
    const float pad = ui.s(18);
    float y = r.y + ui.s(16);
    // Its name, then its eight lines in two columns.
    float x = r.x + pad;
    if (swatch.a) {
        ui.r().fillRoundedRect({x, y + ui.s(2), ui.s(14), ui.s(14)}, ui.s(3), swatch);
        x += ui.s(24);
    }
    const float nw = kicker(bossName(id), {x, y}, ui.theme().text, 15, r.right() - pad - x - ui.s(60), true);
    const float ff = fz(14);
    if (x + nw + ui.s(10) + ui.text().measure("fiche", FontFace::Body, ui.s(ff)).x <= r.right() - pad)
        ui.label("fiche", {x + nw + ui.s(10), y + lineHeight(ui, FontFace::Bold, ui.s(fz(15))) * 0.5f}, ff, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
    y += lineHeight(ui, FontFace::Bold, ui.s(fz(15))) + ui.s(10);
    const auto it = data().bosses.find(id);
    const BossRecord* rec = g_.bossStats.find(id);
    std::vector<std::pair<std::string, std::string>> rows;
    bool won = false;
    if (it != data().bosses.end()) {
        const BossHistory& b = it->second;
        const int hp = hardestPhase(b);
        const std::string fatal = commonFatal(b);
        won = b.best() >= 0.999f;
        const std::string winAfter = b.firstWin > 0 ? std::to_string(b.firstWin) + (b.firstWin > 1 ? " tentatives" : " tentative")
                                     : rec && rec->kills > 0 ? std::string("avant le suivi")
                                                             : std::string("pas encore");
        rows = {{"Tentatives", std::to_string(b.attempts)},
                {"Victoires", std::to_string(b.wins)},
                {"Meilleur", won ? std::string("vaincu") : pct(b.best())},
                {"Victoire après", winAfter},
                {"Phase la plus dure", hp >= 0 ? std::to_string(hp + 1) + " / " + std::to_string(b.phases) : std::string("—")},
                {"Attaque fatale", fatal.empty() ? std::string("—") : fatal},
                {"Victoires (au total)", rec ? std::to_string(rec->kills) : std::string("0")},
                {"Phases sans dégât", rec ? std::to_string(rec->noHitPhases()) : std::string("0")}};
    } else {
        rows = {{"Tentatives", "0"},
                {"Victoires", "0"},
                {"Meilleur", "—"},
                {"Victoire après", rec && rec->kills > 0 ? std::string("avant le suivi") : std::string("pas encore")},
                {"Phase la plus dure", "—"},
                {"Attaque fatale", "—"},
                {"Victoires (au total)", rec ? std::to_string(rec->kills) : std::string("0")},
                {"Phases sans dégât", rec ? std::to_string(rec->noHitPhases()) : std::string("0")}};
    }
    const float colGap = ui.s(22), cw = (r.w - 2 * pad - colGap) / 2.0f, rowH = ui.s(33), f = fz(15);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float cx = r.x + pad + (cw + colGap) * static_cast<float>(i / 4), cy = y + rowH * static_cast<float>(i % 4);
        const auto& [k, v] = rows[i];
        const float kw = std::min(cw * 0.62f, ui.text().measure(k, FontFace::Body, ui.s(f)).x);
        ui.label(ui.elide(k, FontFace::Body, f, kw), {cx, cy + rowH * 0.5f}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        const float room = cw - kw - ui.s(12);
        const float vf = ui.fitFont(v, FontFace::Bold, f, room, 0.75f);
        ui.label(ui.elide(v, FontFace::Bold, vf, room), {cx + cw, cy + rowH * 0.5f}, vf, i == 2 && won ? kGold : ui.theme().text, Align::Right,
                 FontFace::Bold, VAlign::Middle);
        ui.r().fillRect({cx, cy + rowH - 1.0f, cw, 1.0f}, kCardLine.withAlpha(30));
    }
    y += rowH * 4.0f + ui.s(18);
    // Where its fights stop: defeats per phase, against the attempts that reached it.
    {
        const float tw = tableSwitch("phases", r.right() - pad, y - ui.s(4), false, "Tableau", "Barres");
        const float room = r.right() - pad - tw - ui.s(12) - (r.x + pad);
        kicker("OÙ LE COMBAT S'ARRÊTE", {r.x + pad, y}, ui.theme().text, 15, room);
        tableSwitch("phases", r.right() - pad, y - ui.s(4), true, "Tableau", "Barres");
        y += lineHeight(ui, FontFace::Bold, ui.s(fz(15))) + ui.s(2);
        const std::string sub = "Défaites par phase, sur les tentatives arrivées jusque-là";
        ui.label(ui.elide(sub, FontFace::Body, fz(14), room), {r.x + pad, y}, fz(14), ui.theme().textDim);
        y += lineHeight(ui, FontFace::Body, ui.s(fz(14))) + ui.s(12);
    }
    const float btnH = ui.s(44);
    const Rect plot{r.x + pad, y, r.w - 2 * pad, r.bottom() - ui.s(16) - btnH - ui.s(14) - y};
    const EnemyDefinition* def = g_.content.enemy(id);
    int train = -2;  // the phase offered to train on (-2: the last choice for this boss)
    if (it == data().bosses.end()) {
        emptyIn("phases", plot, "Aucune tentative contre ce boss n'est encore enregistrée.");
    } else {
        const BossHistory& b = it->second;
        const int hp = hardestPhase(b);
        train = hp;
        std::vector<Bar> bars;
        std::vector<std::vector<std::string>> trows;
        for (int p = 0; p < b.phases; ++p) {
            const int d = p < static_cast<int>(b.defeats.size()) ? b.defeats[static_cast<size_t>(p)] : 0;
            const int re = p < static_cast<int>(b.reached.size()) ? b.reached[static_cast<size_t>(p)] : 0;
            const std::string pname = def && p < static_cast<int>(def->phases.size()) ? def->phases[static_cast<size_t>(p)].name : std::string();
            const std::string title = bossName(id) + " — phase " + std::to_string(p + 1) + (pname.empty() ? "" : " : " + pname);
            Bar bar{"P" + std::to_string(p + 1) + (pname.empty() ? "" : " · " + pname), static_cast<float>(d), std::to_string(d),
                    p == hp ? ui.theme().danger : ui.theme().warm.withAlpha(220), title,
                    {{charts::kNoSwatch, "Défaites", std::to_string(d)}, {charts::kNoSwatch, "Tentatives arrivées là", std::to_string(re)},
                     {charts::kNoSwatch, "Taux de défaite", re > 0 ? pct(static_cast<double>(d) / re) : std::string("—")}}};
            bar.topText = std::to_string(d) + " sur " + std::to_string(re) + (re > 0 ? " · " + pct(static_cast<double>(d) / re) : std::string());
            if (p == hp && b.phases > 1) bar.subLabel = "la plus dure";
            bars.push_back(std::move(bar));
            trows.push_back({"Phase " + std::to_string(p + 1) + (pname.empty() ? "" : " : " + pname), std::to_string(d), std::to_string(re)});
        }
        if (tables_["phases"]) tableIn("phases", plot, {"Phase", "Défaites", "Tentatives arrivées là"}, trows);
        else charts::vBars(ui, "c/phases", plot, bars, cursorOf("phases"), tip_, [](float v) { return std::to_string(static_cast<int>(std::lround(v))); });
    }
    // Its training (on its hardest phase), its full sheet.
    UIContext::ButtonStyle pri, st;
    pri.primary = true;
    pri.fontSize = 17;
    pri.enabled = def != nullptr;
    st.fontSize = 17;
    const std::string t1 = train >= 0 ? "S'entraîner sur la phase " + std::to_string(train + 1) : std::string("S'entraîner contre ce boss");
    const std::string t2 = "Fiche complète";
    const float w1 = ui.text().measure(t1, FontFace::Bold, ui.s(17)).x + ui.s(36), w2 = ui.text().measure(t2, FontFace::Bold, ui.s(17)).x + ui.s(36);
    const float by = r.bottom() - ui.s(16) - btnH;
    const float avail = r.w - 2 * pad;
    const float b1 = std::min(w1, avail - (w2 + ui.s(12) <= avail * 0.5f ? w2 + ui.s(12) : 0.0f));
    if (cframe::button(ui, "trainphase", {r.x + pad, by, std::max(ui.s(60), b1), btnH}, t1, pri)) g_.app.switchScene(makeBossTraining(g_, id, train), 0.3f);
    if (r.x + pad + b1 + ui.s(12) + w2 <= r.right() - pad && cframe::button(ui, "bosssheet", {r.x + pad + b1 + ui.s(12), by, w2, btnH}, t2, st))
        g_.app.switchScene(makeBossStats(g_, id), 0.3f);
}

float StatisticsScene::drawBoss(float x, float y0, float w) {
    UIContext& ui = g_.ui;
    float y = y0;
    const std::vector<std::string> order = bossesByRecency(data());
    const std::vector<std::string> every = statisticsBosses(g_);  // 2.84: every boss, the ones fought first
    // The top row: the pages of three, then the boss statistics of before (BossStatsScene), always reachable.
    const int groups = std::max(1, (static_cast<int>(every.size()) + 2) / 3);
    if (group_ < 0) {  // the page of the boss chosen elsewhere (« Récemment joué », --stats boss=)
        auto pos = std::find(every.begin(), every.end(), selBoss_);
        group_ = pos == every.end() ? 0 : static_cast<int>(pos - every.begin()) / 3;
    }
    group_ = std::clamp(group_, 0, groups - 1);
    {
        const float bh = ui.s(40);
        UIContext::ButtonStyle st;
        st.fontSize = 17;
        const std::string t = "Fiches des boss : lieux, animations, entraînement";
        const float bw = std::min(w * 0.5f, ui.text().measure(t, FontFace::Bold, ui.s(17)).x + ui.s(40));
        const Rect br{x + w - bw, y, bw, bh};
        if (cframe::button(ui, "bossstats", br, ui.elide(t, FontFace::Bold, 17, bw - ui.s(24)), st)) g_.app.switchScene(makeBossStats(g_, selBoss_), 0.3f);
        ui.newBadge("bossstats", br, "stats.bosses");
        float lx = x;
        if (groups > 1) {
            UIContext::ButtonStyle nav;
            nav.small = true;
            nav.enabled = group_ > 0;
            if (cframe::button(ui, "grpprev", {lx, y, ui.s(48), bh}, "‹", nav)) --group_, selBoss_.clear();
            nav.enabled = group_ + 1 < groups;
            if (cframe::button(ui, "grpnext", {lx + ui.s(56), y, ui.s(48), bh}, "›", nav)) ++group_, selBoss_.clear();
            group_ = std::clamp(group_, 0, groups - 1);
            lx += ui.s(118);
        }
        if (!every.empty()) {
            const int a = group_ * 3 + 1, b = std::min(static_cast<int>(every.size()), group_ * 3 + 3);
            const bool fought = b <= static_cast<int>(order.size());
            const std::string t2 = "Boss " + std::to_string(a) + " à " + std::to_string(b) + " sur " + std::to_string(every.size()) +
                                   (fought ? ", du plus récent au plus ancien" : ", les boss combattus d'abord");
            const float room = br.x - ui.s(16) - lx;
            if (room > ui.s(80)) {
                const float f = ui.fitFont(t2, FontFace::Body, fz(15), room, 0.8f);
                ui.label(ui.elide(t2, FontFace::Body, f, room), {lx, y + bh * 0.5f}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            }
        }
        y += bh + ui.s(16);
    }
    if (every.empty()) {
        y += note(x, y, w, "Aucun boss dans le contenu du jeu.");
        return y - y0;
    }
    if (order.empty()) {
        y += note(x, y, w,
                  "Aucune tentative contre un boss n'est encore enregistrée. Chaque combat compte dès maintenant : la part des points de vie "
                  "retirée, la phase atteinte et l'attaque fatale. Les statistiques de boss d'avant (victoires, phases sans dégât) restent dans "
                  "les fiches des boss.") +
             ui.s(14);
    }
    const std::vector<std::string> shown(every.begin() + group_ * 3, every.begin() + std::min<int>(static_cast<int>(every.size()), group_ * 3 + 3));
    if (std::find(shown.begin(), shown.end(), selBoss_) == shown.end()) selBoss_ = shown.front();
    // The banners: one colour per boss of the page (the curve's), the same colour wherever it is.
    const float gap = ui.s(16);
    const int perRow = w >= ui.s(1100) ? 3 : 1;
    const float bw = (w - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow), bh = ui.s(214);
    for (size_t i = 0; i < shown.size(); ++i) {
        const Rect r{x + (bw + gap) * static_cast<float>(static_cast<int>(i) % perRow), y + (bh + gap) * static_cast<float>(static_cast<int>(i) / perRow), bw, bh};
        bossBanner(r, shown[i], kSeries[i], shown[i] == selBoss_, static_cast<int>(i));
    }
    y += (bh + gap) * static_cast<float>((static_cast<int>(shown.size()) + perRow - 1) / perRow);
    // The progression of the bosses of the page that were fought, and the sheet of the one chosen.
    const bool side = w >= ui.s(1300);
    const float chartW = side ? (w - gap) * 0.613f : w, sheetW = side ? w - chartW - gap : w;
    const float ch = ui.s(596);
    {
        CardHead h;
        h.title = "PROGRESSION SUR CHAQUE BOSS";
        h.sub = "Meilleure part des points de vie retirés, tentative après tentative";
        h.table = "bosscurve";
        std::vector<std::vector<std::pair<int, float>>> curves;
        std::vector<std::string> names, ids;
        std::vector<Color> colors;
        std::vector<int> firstWins;
        LineSpec spec;
        int maxN = 0;
        std::vector<LegendItem> leg;
        auto star = [this](const Rect& b) { g_.ui.icon("ui/icons_hud.png", 8, 16, b); };
        for (size_t i = 0; i < shown.size(); ++i) {
            auto it = data().bosses.find(shown[i]);
            if (it == data().bosses.end()) continue;
            const BossHistory& b = it->second;
            curves.push_back(bossBestCurve(b));
            Series s{bossName(shown[i]), kSeries[i], Series::Kind::Step, false, {}};
            for (auto [n, best] : curves.back()) {
                s.pts.push_back({static_cast<float>(n), best});
                maxN = std::max(maxN, n);
            }
            const float last = curves.back().empty() ? 0.0f : curves.back().back().second;
            s.endLabel = last >= 0.999f ? s.name : s.name + " · " + pct(last);
            spec.series.push_back(s);
            names.push_back(s.name);
            ids.push_back(shown[i]);
            colors.push_back(kSeries[i]);
            leg.push_back({kSeries[i], s.name, 0, {}});
            const int fw = b.firstWin >= b.firstKept() ? b.firstWin : 0;
            firstWins.push_back(fw);
            if (fw > 0) spec.marks.push_back({{static_cast<float>(fw), 1.0f}, star});
        }
        if (!spec.marks.empty()) leg.push_back({Color::white(), "première victoire", 3, star});
        h.extra = [&](float right, float yy, bool draw) {
            const float lw = legendWidth(leg);
            if (draw) legend(right - lw, yy + ui.s(6), lw + ui.s(2), leg);
            return lw;
        };
        spec.yMin = 0.0f;
        spec.yMax = 1.0f;
        spec.integerX = true;
        spec.endLabels = true;
        spec.yLabel = [](float v) { return pct(v); };
        spec.xLabel = [](float v) { return "n° " + std::to_string(static_cast<int>(std::lround(v))); };
        spec.tip = [curves, names, colors, firstWins](float xv, Tip& t) {
            const int n = static_cast<int>(std::lround(xv));
            t.title = "Tentative " + std::to_string(n);
            for (size_t i = 0; i < firstWins.size(); ++i)
                if (firstWins[i] == n) t.title += " · première victoire";
            for (size_t i = 0; i < curves.size(); ++i) {
                const std::pair<int, float>* p = nullptr;
                for (const auto& q : curves[i])
                    if (q.first <= n) p = &q;
                t.rows.push_back({colors[i], names[i], !p ? std::string("—") : p->second >= 0.999f ? std::string("100 % (vaincu)") : pct(p->second)});
            }
        };
        std::vector<std::string> head = {"Tentative"};
        for (const auto& n : names) head.push_back(n);
        std::vector<std::vector<std::string>> rows;
        for (int n = maxN; n >= 1; --n) {
            std::vector<std::string> row = {std::to_string(n)};
            bool any = false;
            for (const auto& cv : curves) {
                std::string v = "—";
                for (const auto& q : cv)
                    if (q.first == n) {
                        v = q.second >= 0.999f ? "100 % (vaincu)" : pct(q.second);
                        any = true;
                    }
                row.push_back(v);
            }
            if (any) rows.push_back(row);
        }
        Rect in = card({x, y, chartW, ch}, h);
        if (tables_["bosscurve"]) tableIn("bosscurve", in, head, rows);
        else if (curves.empty()) emptyIn("bosscurve", in, "Aucune tentative enregistrée contre les boss de cette page.");
        else charts::lineChart(ui, "c/bosscurve", in, spec, cursorOf("bosscurve"), tip_);
    }
    {
        const auto pos = std::find(shown.begin(), shown.end(), selBoss_);
        const Rect r = side ? Rect{x + chartW + gap, y, sheetW, ch} : Rect{x, y + ch + gap, sheetW, ch};
        bossSheet(r, selBoss_, pos != shown.end() ? kSeries[pos - shown.begin()] : Color{0, 0, 0, 0});
    }
    y += side ? ch + gap : 2 * ch + 2 * gap;
    y += note(x, y, w,
              "Progression : à chaque tentative est enregistrée la part des points de vie totaux du boss (toutes phases) retirée au moment de la "
              "défaite ou de l'abandon ; la courbe montre le meilleur résultat obtenu jusque-là, une victoire vaut 100 % (l'étoile marque la "
              "première). Une tentative reprise après une mort retrouve le boss avec ses blessures. Les 200 dernières tentatives sont gardées "
              "par boss. Les victoires et phases sans dégât d'avant le suivi viennent des statistiques de boss existantes. La couleur d'un boss "
              "suit le boss sur la page, jamais son rang.");
    return y - y0;
}

}  // namespace cosmic
