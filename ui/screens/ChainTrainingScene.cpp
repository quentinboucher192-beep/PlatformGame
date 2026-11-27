// 2.91 — ENTRAÎNEMENT › « Segments et enchaînements » and « Startpos et mesures »
// (docs/GHOST_RACING.md § « Une seule somme des meilleurs… »).
//
// The grid of every contiguous range of segments of the level version (rows: where it starts,
// columns: where it ends): the diagonal holds each segment's best (its part of the ONE Sum of
// Best), the cells above it the records of the enchaînements, each with its gap to the sum of its
// segments' bests (« OR » when it equals it) and how many attempts crossed it (runs, trainings,
// verified online runs). The chosen range: its record, the sum of its bests, what is still to
// gain, its segments and where each best was done; « Lancer l'enchaînement » trains it (a section
// training from its first checkpoint to its last), « Regarder » plays its bests one after the
// other, « Fantôme » races its record's replay. Every cell is a focusable widget: arrows / d-pad /
// mouse choose it, Entrée / A launches it.
//
// « Startpos et mesures »: TrainingMarksPanel (start an attempt of the chosen range from a StartPos;
// 2.92: add, move and delete the StartPos and the measure points there).
//
// 2.92: both strips show the level itself (TrainingStrip: its picture made off the frame's path by
// the pictures of the « Statistiques » screen, the schematic bar until it is ready).

#include <algorithm>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/SobMontage.h"
#include "game/player/PlayerConfig.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/StatisticsArt.h"
#include "ui/screens/TrainingMarksPanel.h"
#include "ui/screens/TrainingStrip.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGold{255, 214, 90, 255};

long long msOf(float s) { return std::llround(static_cast<double>(s) * 1000.0); }

std::string dayMonth(const std::string& date) { return date.size() >= 10 ? date.substr(8, 2) + "/" + date.substr(5, 2) : std::string{}; }

class ChainTrainingScene : public Scene {
public:
    ChainTrainingScene(GameServices& g, ChainTrainingRequest req) : g_(g), req_(std::move(req)), art_(g) {}
    // The whole screen (never over another interactive screen: its UI frame and focus stay this one's).
    bool isOverlay() const override { return false; }
    // The level's pictures: a few milliseconds per frame, before the frame is drawn.
    void update(float) override { art_.update(); }

    void onEnter() override {
        key_ = GhostStore::levelKey(req_.levelId, req_.levelHash);
        n_ = static_cast<int>(req_.checkpoints.size());
        // The level's own route is its Sum of Best's.
        std::vector<std::string> route;
        for (const auto& c : req_.checkpoints) route.push_back(c.id);
        if (auto known = g_.bests.route(key_); !known || *known != route) {
            g_.bests.setRoute(key_, route);
            g_.bests.save();
        }
        from_ = 0;
        to_ = 1;
        if (!req_.pickFirst) {
            from_ = boundaryOf(req_.from, true);
            to_ = boundaryOf(req_.to, false);
            if (from_ < 0 || to_ <= from_) from_ = 0, to_ = 1;
        }
        tab_ = std::clamp(req_.tab, 0, 1);
        g_.ui.setFocus("chains/" + cellName(from_, to_));
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 255});
        const float m = ui.s(40);
        const Rect panel{m, ui.s(28), ui.screenW() - 2 * m, ui.screenH() - ui.s(56)};
        ui.panel(panel, true, 18);
        ui.pushId("chains");
        const float x = panel.x + ui.s(28), w = panel.w - ui.s(56);
        float y = panel.y + ui.s(22);
        // Title, tabs, the level, « Retour ».
        const float bw = ui.s(200);
        UIContext::ButtonStyle bs;
        bs.small = true;
        const bool back = ui.button("back", {panel.right() - ui.s(28) - bw, y, bw, ui.s(46)}, "Retour", bs);
        const Vec2 ts = ui.label("ENTRAÎNEMENT", {x, y + ui.s(23)}, ui.fitFont("ENTRAÎNEMENT", FontFace::Title, 34, w * 0.25f, 0.6f), th.text, Align::Left,
                                 FontFace::Title, VAlign::Middle);
        const float tabsX = x + ts.x + ui.s(24);
        const float tabsW = std::min(ui.s(640), panel.right() - ui.s(28) - bw - ui.s(20) - tabsX);
        int tab = tab_;
        ui.tabs("tabs", {tabsX, y, tabsW * 0.72f, ui.s(46)}, {"Segments et enchaînements", "Startpos et mesures"}, tab);
        if (!(tab_ == 1 && marks_.holdsShoulders(ui))) tab_ = tab;  // LB / RB held over the preview: its 8-pixel step
        const float nameX = tabsX + tabsW * 0.72f + ui.s(16), nameRoom = panel.right() - ui.s(28) - bw - ui.s(16) - nameX;
        if (nameRoom > ui.s(60))
            ui.label(ui.elide(req_.levelName, FontFace::Body, fz(ui, 17), nameRoom), {nameX, y + ui.s(23)}, fz(ui, 17), th.textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        y += ui.s(66);
        if (tab_ == 0) {
            drawStrip({x, y, w, ui.s(116)});
            y += ui.s(130);
            const float hintH = ui.s(30);
            const float leftW = w * 0.58f, gap = ui.s(22);
            drawGrid({x, y, leftW, panel.bottom() - y - hintH - ui.s(16)});
            drawChosen({x + leftW + gap, y, w - leftW - gap, panel.bottom() - y - hintH - ui.s(16)});
            const std::string hint = "Souris : un clic sur une case choisit cette combinaison ; Entrée (A) la lance.";
            ui.label(ui.elide(hint, FontFace::Body, fz(ui, 14), w), {x, panel.bottom() - ui.s(26)}, fz(ui, 14), th.textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
        } else {
            TrainingMarksPanel::Context c;
            c.levelKey = key_;
            c.levelId = req_.levelId;
            c.art = &art_;
            c.levelBounds = req_.levelBounds;
            for (const auto& cp : req_.checkpoints) c.checkpoints.push_back({cp.feet.x, cp.label});
            c.where = [this](Vec2 feet) { return whereByX(feet); };
            c.startFrom = [this](int id) { launch(id); };
            c.placedState = [this](Vec2 feet) { return placedStartPos(feet); };
            marks_.draw(g_, {x, y, w, panel.bottom() - y - ui.s(20)}, c);
        }
        ui.popId();
        if (back || (ui.backPressed() && !(tab_ == 1 && marks_.editing()))) g_.app.popScene();
        ui.end();
    }

private:
    // ------------------------------------------------------------ the route

    /// Boundary k: 0 the start, 1..n the checkpoints, n + 1 the exit.
    std::string boundaryId(int k) const { return k <= 0 || k > n_ ? std::string{} : req_.checkpoints[static_cast<size_t>(k - 1)].id; }
    std::string boundaryName(int k) const {
        if (k <= 0) return "Départ";
        if (k > n_) return "Arrivée";
        const auto& c = req_.checkpoints[static_cast<size_t>(k - 1)];
        return c.label.empty() ? "Point " + std::to_string(k) : c.label;
    }
    int boundaryOf(const std::string& id, bool start) const {
        if (id.empty()) return start ? 0 : n_ + 1;
        for (int k = 1; k <= n_; ++k)
            if (req_.checkpoints[static_cast<size_t>(k - 1)].id == id) return k;
        return -1;
    }
    float boundaryAt(int k) const { return k <= 0 ? 0.0f : k > n_ ? 1.0f : req_.checkpoints[static_cast<size_t>(k - 1)].at; }
    static std::string cellName(int i, int j) { return "c" + std::to_string(i) + "_" + std::to_string(j); }
    const PersonalBests::Record* rec(int i, int j) const { return g_.bests.record(key_, boundaryId(i), boundaryId(j)); }

    /// « Racines → Clairière · 30 % » for a place in the level, by its x between the checkpoints.
    std::string whereByX(Vec2 feet) const {
        const Rect& lb = req_.levelBounds;
        float prevX = lb.x;
        for (int k = 1; k <= n_ + 1; ++k) {
            const float nx = k <= n_ ? req_.checkpoints[static_cast<size_t>(k - 1)].feet.x : lb.right();
            if (feet.x <= nx || k == n_ + 1) {
                const int pct = nx > prevX ? std::clamp(static_cast<int>(std::lround((feet.x - prevX) / (nx - prevX) * 100.0f)), 0, 100) : 0;
                return boundaryName(k - 1) + " → " + boundaryName(k) + " · " + std::to_string(pct) + " %";
            }
            prevX = nx;
        }
        return {};
    }

    // ------------------------------------------------------------ drawing

    /// The level as a strip (TrainingStrip: its picture once made, the schematic bar until then): its
    /// segments between the checkpoints where they stand in the level (the chosen range lit), the
    /// names of the boundaries along its top, the segments' numbers along its bottom, each on its
    /// measured background and only where it holds between its neighbours.
    void drawStrip(const Rect& r) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const float fs = fz(ui, 14);
        const bool picture = strip_.draw(g_, &art_, req_.levelId, req_.levelBounds, r);
        // The boundaries, left to right: the level's sides, its checkpoints where they stand.
        std::vector<float> xs(static_cast<size_t>(n_) + 2);
        xs[0] = r.x;
        for (int k = 1; k <= n_; ++k)
            xs[static_cast<size_t>(k)] = std::clamp(strip_.xOf(req_.checkpoints[static_cast<size_t>(k - 1)].feet.x), xs[static_cast<size_t>(k - 1)], r.right());
        xs[static_cast<size_t>(n_) + 1] = r.right();
        auto xOf = [&](int k) { return xs[static_cast<size_t>(std::clamp(k, 0, n_ + 1))]; };
        const Rect lit{xOf(from_), r.y, std::max(2.0f, xOf(to_) - xOf(from_)), r.h};
        if (picture) {
            // Out of the range: darker; the range itself: barely tinted, its edges lit.
            const Color dim{4, 6, 14, 150};
            if (lit.x > r.x) ui.r().fillRect({r.x, r.y, lit.x - r.x, r.h}, dim);
            if (lit.right() < r.right()) ui.r().fillRect({lit.right(), r.y, r.right() - lit.right(), r.h}, dim);
            ui.r().fillRect(lit, th.accent.withAlpha(22));
        } else {
            ui.r().fillRoundedRect(lit, ui.s(6), th.accent.withAlpha(60));
        }
        ui.r().drawRoundedRect(lit, ui.s(picture ? 3 : 6), th.accent, std::max(1.5f, ui.s(2.0f)));
        strip_.note(lit);
        // The separators: a light line on a dark one, seen on any ground.
        const float lw = std::max(1.0f, ui.s(1.5f));
        for (int k = 1; k <= n_; ++k) {
            const Rect line{xOf(k) - lw * 0.5f, r.y, lw, r.h};
            if (picture) ui.r().fillRect({line.x - lw, r.y, lw * 3.0f, r.h}, Color{4, 6, 14, 170});
            ui.r().fillRect(line, th.textDim.withAlpha(picture ? 230 : 140));
            strip_.note(line);
        }
        // A name on its background, measured: `x` its left (Left), its right (Right) or its middle.
        const float pad = ui.s(6), ph = ui.s(fs + 8.0f);
        auto pill = [&](const std::string& text, FontFace face, Color color, bool on, float x, float y, Align al) {
            const float tw = ui.text().measure(text, face, ui.s(fs)).x;
            const float pw = tw + pad * 2.0f;
            float px = al == Align::Left ? x : al == Align::Right ? x - pw : x - pw * 0.5f;
            px = std::clamp(px, r.x + ui.s(2), std::max(r.x + ui.s(2), r.right() - ui.s(2) - pw));
            const Rect box{px, y, pw, ph};
            ui.r().fillRoundedRect(box, ui.s(4), Color{8, 12, 26, 225});
            ui.r().drawRoundedRect(box, ui.s(4), on ? th.accent : th.textDim.withAlpha(90), 1.0f);
            ui.label(text, box.center(), fs, color, Align::Center, face, VAlign::Middle);
            strip_.note(box);
        };
        // The boundaries' names along the top: the start's runs right from it, the exit's left,
        // the others are centred, each within the room between its neighbours (dropped without).
        for (int k = 0; k <= n_ + 1; ++k) {
            const float px = xOf(k);
            float room = 0.0f;
            if (k == 0) room = (xOf(1) - px) * 0.5f - ui.s(6);
            else if (k == n_ + 1) room = (px - xOf(n_)) * 0.5f - ui.s(6);
            else room = std::min(px - xOf(k - 1), xOf(k + 1) - px) - ui.s(6);
            room -= pad * 2.0f + ui.s(4);
            if (room < ui.s(30)) continue;
            const bool on = k == from_ || k == to_;
            const FontFace face = on ? FontFace::Bold : FontFace::Body;
            const Align al = k == 0 ? Align::Left : k == n_ + 1 ? Align::Right : Align::Center;
            const float ax = k == 0 ? px + ui.s(4) : k == n_ + 1 ? px - ui.s(4) : px;
            pill(ui.elide(boundaryName(k), face, fs, room), face, on ? th.accent : th.text, on, ax, r.y + ui.s(4), al);
        }
        // The segments' numbers along the bottom, where they hold.
        for (int k = 0; k <= n_; ++k) {
            const float a = xOf(k), b = xOf(k + 1);
            const std::string label = "Segment " + std::to_string(k + 1);
            if (b - a < ui.text().measure(label, FontFace::Body, ui.s(fs)).x + pad * 2.0f + ui.s(10)) continue;
            const bool in = k >= from_ && k < to_;
            pill(label, FontFace::Body, in ? th.accent : th.textDim, in, (a + b) * 0.5f, r.bottom() - ui.s(4) - ph, Align::Center);
        }
    }

    /// The state a StartPos placed here from the menu starts with: a section training of the
    /// segment it lies in (game/gameplay/StartPos.h: segmentStartPos), as « Lancer » gives it.
    StartPosState placedStartPos(Vec2 feet) {
        float at = (feet.x - req_.levelBounds.x) / std::max(1.0f, req_.levelBounds.w);
        if (const statsart::LevelInfo* info = art_.info(req_.levelId); info && info->track.valid()) at = info->track.fractionAt(feet);
        int k = 0;  // the boundary it follows
        for (int i = 1; i <= n_; ++i)
            if (boundaryAt(i) <= at) k = i;
        std::vector<std::string> reached;
        for (int i = 1; i <= k; ++i) reached.push_back(boundaryId(i));
        std::vector<std::string> tablets;
        for (const auto& [t, power] : req_.tablets)
            if (k > 0 && t <= boundaryAt(k)) tablets.push_back(power);
        const PersonalBests::Range range{boundaryId(k), boundaryId(k + 1)};
        const PlayerConfig cfg = PlayerConfig::fromJson(g_.content.playerConfig());
        return segmentStartPos(feet, std::move(reached), req_.startPowers, tablets, req_.powerSlots, g_.bests.powers(key_, range), cfg.maxHealth,
                               cfg.maxEnergy);
    }

    void drawGrid(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.r().fillRoundedRect(area, ui.s(12), Color{10, 14, 30, 200});
        ui.r().drawRoundedRect(area, ui.s(12), th.accent.withAlpha(50), 1.0f);
        const float pad = ui.s(16);
        float y = area.y + pad;
        const std::string title = "RECORDS DE TOUTES LES COMBINAISONS (SEGMENTS QUI SE SUIVENT)";
        ui.label(ui.elide(title, FontFace::Bold, fz(ui, 14), area.w - pad * 2), {area.x + pad, y + ui.s(10)}, fz(ui, 14), th.accent, Align::Left,
                 FontFace::Bold, VAlign::Middle);
        y += ui.s(30);
        const float legendH = ui.s(46);
        const int cols = n_ + 1;  // the ends: the checkpoints, then the exit
        const float rowHeadW = std::min(ui.s(150), area.w * 0.2f), headH = ui.s(28);
        const Rect grid{area.x + pad + rowHeadW, y + headH, area.w - pad * 2 - rowHeadW, area.bottom() - y - headH - legendH - pad};
        const float cw = grid.w / static_cast<float>(cols), ch = std::min(grid.h / static_cast<float>(cols), ui.s(110));
        const float hf = fz(ui, 13);
        // Headers: « départ ↓ · arrivée → », the ends' names above, the starts' names at the left.
        ui.label(ui.elide("départ ↓ · arrivée →", FontFace::Body, hf, rowHeadW - ui.s(6)), {area.x + pad, y + headH * 0.5f}, hf, th.textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        for (int j = 1; j <= n_ + 1; ++j)
            ui.label(ui.elide(boundaryName(j), FontFace::Bold, hf, cw - ui.s(8)), {grid.x + cw * (static_cast<float>(j) - 0.5f), y + headH * 0.5f}, hf,
                     j == to_ ? th.accent : th.text, Align::Center, FontFace::Bold, VAlign::Middle);
        for (int i = 0; i <= n_; ++i)
            ui.label(ui.elide(boundaryName(i), FontFace::Bold, hf, rowHeadW - ui.s(10)), {grid.x - ui.s(10), grid.y + ch * (static_cast<float>(i) + 0.5f)},
                     hf, i == from_ ? th.accent : th.text, Align::Right, FontFace::Bold, VAlign::Middle);
        // The cells: (i, j) for every start i before every end j.
        const float tf = fz(ui, std::clamp(ch / ui.s(1.0f) * 0.22f, 13.0f, 22.0f)), sf = fz(ui, std::clamp(ch / ui.s(1.0f) * 0.13f, 11.0f, 14.0f));
        for (int i = 0; i <= n_; ++i)
            for (int j = i + 1; j <= n_ + 1; ++j) {
                const Rect cell{grid.x + cw * static_cast<float>(j - 1) + ui.s(3), grid.y + ch * static_cast<float>(i) + ui.s(3), cw - ui.s(6), ch - ui.s(6)};
                const std::string name = cellName(i, j);
                // A click chooses the cell; Entrée / A on it (keyboard, gamepad) launches it.
                if (ui.hotspot(name, cell)) {
                    from_ = i, to_ = j;
                    if (!ui.usingPointer()) {
                        launch(0);
                        return;
                    }
                }
                if (ui.focused(name) && (from_ != i || to_ != j)) from_ = i, to_ = j;
                drawCell(cell, i, j, tf, sf);
            }
        // Legend.
        const float ly = area.bottom() - pad - legendH;
        const float lf = fz(ui, 13), half = (area.w - pad * 2) * 0.5f;
        auto legend = [&](float lx, float yy, Color sw, const std::string& text) {
            ui.r().fillRect({lx, yy - ui.s(6), ui.s(12), ui.s(12)}, sw);
            ui.label(ui.elide(text, FontFace::Body, lf, half - ui.s(24)), {lx + ui.s(18), yy}, lf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        };
        legend(area.x + pad, ly + ui.s(10), Color{140, 120, 60, 255}, "Diagonale : meilleur segment (sa part de la SOB)");
        legend(area.x + pad + half, ly + ui.s(10), kGold, "OR : enchaînement égal à la somme de ses meilleurs");
        legend(area.x + pad, ly + ui.s(34), th.danger, "+x s : écart à cette somme");
        legend(area.x + pad + half, ly + ui.s(34), th.textDim, "Passages : parties, entraînements et en ligne");
    }

    void drawCell(const Rect& cell, int i, int j, float tf, float sf) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const PersonalBests::Record* r = rec(i, j);
        const bool diagonal = j == i + 1;
        const float sum = diagonal ? (r ? r->best : -1.0f) : g_.bests.sumOfBests(key_, i, j);
        const bool known = r && r->best > 0.0f;
        const bool gold = known && !diagonal && sum > 0.0f && msOf(r->best) <= msOf(sum);
        const bool chosen = i == from_ && j == to_;
        const bool focused = ui.focused(cellName(i, j));
        Color fill = diagonal ? Color{48, 40, 16, 230} : Color{18, 26, 50, 230};
        if (gold) fill = Color{60, 48, 12, 240};
        ui.r().fillRoundedRect(cell, ui.s(6), fill);
        Color edge = diagonal || gold ? kGold.withAlpha(gold ? 230 : 120) : th.accent.withAlpha(40);
        if (chosen || focused) edge = th.accent;
        ui.r().drawRoundedRect(cell, ui.s(6), edge, chosen || focused ? std::max(2.0f, ui.s(2.0f)) : 1.0f);
        const float pad = ui.s(8);
        const float room = cell.w - pad * 2;
        // The tag (PB, OR) at the top right, the time at the top left.
        std::string tag;
        if (known && i == 0 && j == n_ + 1 && (r->source == BestSource::Run || r->source == BestSource::Online)) tag = "PB";
        else if (gold) tag = "OR";
        float tagW = 0.0f;
        if (!tag.empty()) {
            tagW = ui.text().measure(tag, FontFace::Bold, ui.s(sf)).x + ui.s(10);
            if (tagW < room * 0.45f) {
                const Rect tr{cell.right() - pad - tagW, cell.y + pad, tagW, ui.s(sf + 6.0f)};
                ui.r().fillRoundedRect(tr, ui.s(4), gold ? kGold : th.textDim.withAlpha(120));
                ui.label(tag, tr.center(), sf, Color{20, 16, 4, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
            } else {
                tagW = 0.0f;
            }
        }
        const std::string time = known ? formatGhostTime(r->best) : "—";
        const float f = ui.fitFont(time, FontFace::Bold, tf, room - tagW - ui.s(4), 0.6f);
        ui.label(ui.elide(time, FontFace::Bold, f, room - tagW - ui.s(4)), {cell.x + pad, cell.y + pad + ui.s(f) * 0.55f}, f,
                 diagonal || gold ? kGold : th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        // Then what it is, and how many attempts crossed it (when the cell has the room).
        const float lineH = ui.s(sf + 5.0f);
        float ly = cell.y + pad + ui.s(f) * 1.1f + lineH * 0.5f;
        if (ly + lineH * 0.5f > cell.bottom() - ui.s(2)) return;
        std::string what;
        Color wc = th.textDim;
        if (diagonal) {
            what = "segment seul";
            wc = kGold;
        } else if (gold) {
            what = "= somme des meilleurs";
            wc = kGold;
        } else if (known && sum > 0.0f) {
            what = formatGhostDelta(r->best - sum);
            wc = th.danger;
        }
        if (!what.empty()) ui.label(ui.elide(what, FontFace::Body, sf, room), {cell.x + pad, ly}, sf, wc, Align::Left, FontFace::Body, VAlign::Middle);
        ly += lineH;
        if (ly + lineH * 0.5f > cell.bottom() - ui.s(2) || !r) return;
        const bool full = i == 0 && j == n_ + 1;
        const std::string count = std::to_string(r->passages) + (full ? (r->passages > 1 ? " arrivées" : " arrivée") : (r->passages > 1 ? " passages" : " passage"));
        ui.label(ui.elide(count, FontFace::Body, sf, room), {cell.x + pad, ly}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }

    /// The chosen range: its ends, its numbers, its segments, how an attempt starts, the buttons.
    void drawChosen(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.r().fillRoundedRect(area, ui.s(12), Color{10, 14, 30, 200});
        ui.r().drawRoundedRect(area, ui.s(12), th.accent.withAlpha(50), 1.0f);
        const float pad = ui.s(18), x = area.x + pad, w = area.w - pad * 2;
        float y = area.y + pad;
        const int segs = to_ - from_;
        ui.label("ENCHAÎNEMENT CHOISI", {x, y + ui.s(9)}, fz(ui, 14), th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(28);
        const std::string name = boundaryName(from_) + " → " + boundaryName(to_);
        const float nf = ui.fitFont(name, FontFace::Bold, 24, w, 0.6f);
        ui.label(ui.elide(name, FontFace::Bold, nf, w), {x, y + ui.s(nf) * 0.55f}, nf, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(nf) * 1.2f + ui.s(4);
        std::string which;
        for (int k = from_; k < to_; ++k) which += (k == from_ ? "" : k + 1 == to_ ? " et " : ", ") + std::to_string(k + 1);
        const std::string sub = segs > 1 ? std::to_string(segs) + " segments à la suite (" + which + ")" : "Un segment (" + which + ")";
        ui.label(ui.elide(sub, FontFace::Body, fz(ui, 15), w), {x, y + ui.s(9)}, fz(ui, 15), th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(30);
        // Départ / Arrivée: each end chosen among the boundaries (the end always after the start).
        std::vector<std::string> starts, ends;
        for (int k = 0; k <= n_; ++k) starts.push_back(boundaryName(k));
        for (int k = 1; k <= n_ + 1; ++k) ends.push_back(boundaryName(k));
        int fi = from_, ti = to_ - 1;
        if (ui.cycle("from", {x, y, w, ui.s(44)}, "Départ", fi, starts)) {
            from_ = fi;
            if (to_ <= from_) to_ = from_ + 1;
        }
        y += ui.s(50);
        if (ui.cycle("to", {x, y, w, ui.s(44)}, "Arrivée", ti, ends)) {
            to_ = ti + 1;
            if (to_ <= from_) from_ = to_ - 1;
        }
        y += ui.s(56);
        // Its record, the sum of its bests, what is still to gain.
        const PersonalBests::Record* r = rec(from_, to_);
        const float sum = g_.bests.sumOfBests(key_, from_, to_);
        const float bwid = (w - ui.s(20)) / 3.0f, bh = ui.s(84);
        std::string bestSub = r && r->best > 0.0f ? i18n::tr(bestSourceLabel(r->source)) + (dayMonth(r->date).empty() ? "" : " " + dayMonth(r->date)) : "pas encore";
        std::string parts;
        for (int k = from_; k < to_; ++k) parts += (k == from_ ? "segments " : " + ") + std::to_string(k + 1);
        if (segs == 1) parts = "le segment seul";
        const float gain = r && r->best > 0.0f && sum > 0.0f ? r->best - sum : -1.0f;
        statBox({x, y, bwid, bh}, "MEILLEUR", r && r->best > 0.0f ? formatGhostTime(r->best) : "—", bestSub, th.text);
        statBox({x + bwid + ui.s(10), y, bwid, bh}, "SOMME DES MEILL.", sum > 0.0f ? formatGhostTime(sum) : "incomplète", parts, kGold);
        statBox({x + (bwid + ui.s(10)) * 2.0f, y, bwid, bh}, "À GAGNER", gain >= 0.0f ? formatGhostSeconds(gain) : "—", "meilleur − somme", th.warm);
        y += bh + ui.s(14);
        // Its segments: each best, where it was done.
        const float rowH = ui.s(30);
        const float bottomH = ui.s(54) + ui.s(16) + ui.s(4 * 22 + 40);
        const int shown = std::max(1, std::min(segs, static_cast<int>((area.bottom() - pad - bottomH - y) / rowH)));
        for (int k = from_; k < from_ + shown && k < to_; ++k) {
            const PersonalBests::Record* s = rec(k, k + 1);
            const std::string label = std::to_string(k + 1) + " · " + boundaryName(k) + " → " + boundaryName(k + 1);
            const std::string src = s && s->best > 0.0f ? i18n::tr(bestSourceLabel(s->source)) : std::string();
            const float srcW = src.empty() ? 0.0f : ui.text().measure(src, FontFace::Bold, ui.s(fz(ui, 13))).x + ui.s(16);
            const std::string t = s && s->best > 0.0f ? formatGhostTime(s->best) : "—";
            const float tw = ui.text().measure(t, FontFace::Bold, ui.s(fz(ui, 15))).x;
            const float room = w - srcW - tw - ui.s(24);
            const float cy = y + rowH * 0.5f;
            ui.label(ui.elide(label, FontFace::Body, fz(ui, 15), room), {x, cy}, fz(ui, 15), th.text, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(t, {x + w - srcW - ui.s(10), cy}, fz(ui, 15), kGold, Align::Right, FontFace::Bold, VAlign::Middle);
            if (!src.empty()) {
                const Rect pill{x + w - srcW + ui.s(2), cy - ui.s(11), srcW - ui.s(2), ui.s(22)};
                ui.r().drawRoundedRect(pill, ui.s(11), th.accent.withAlpha(160), 1.0f);
                ui.label(src, pill.center(), fz(ui, 13), th.accent, Align::Center, FontFace::Bold, VAlign::Middle);
            }
            y += rowH;
        }
        if (shown < segs)
            ui.label("… et " + std::to_string(segs - shown) + " de plus", {x, y + ui.s(10)}, fz(ui, 13), th.textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
        // How an attempt starts.
        float hy = area.bottom() - pad - ui.s(54) - ui.s(16) - ui.s(4 * 22 + 40);
        ui.label("COMMENT DÉMARRE UN ENCHAÎNEMENT", {x, hy + ui.s(10)}, fz(ui, 13), th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        hy += ui.s(30);
        const std::string steps[4] = {"Nova apparaît au point " + boundaryName(from_) + ", comme l'entraînement par segment.",
                                      "Pouvoirs : ceux du départ et des tablettes d'avant (ou ceux choisis pour ce passage).",
                                      "Chrono à l'apparition ; chaque point traversé donne un temps de passage.",
                                      "Fin à " + boundaryName(to_) + ", puis l'essai recommence seul (R ou Vue)."};
        for (int k = 0; k < 4; ++k) {
            ui.label(std::to_string(k + 1), {x, hy + ui.s(11)}, fz(ui, 13), th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label(ui.elide(steps[k], FontFace::Body, fz(ui, 13), w - ui.s(22)), {x + ui.s(20), hy + ui.s(11)}, fz(ui, 13), th.textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            hy += ui.s(22);
        }
        // Launch, watch, the record's ghost.
        const float by = area.bottom() - pad - ui.s(54);
        const float g = ui.s(10);
        const float lw = w * 0.44f, ow = (w - lw - g * 2) * 0.5f;
        UIContext::ButtonStyle p;
        p.primary = true;
        const std::string go = "Lancer l'enchaînement";
        p.fontSize = ui.fitFont(go, FontFace::Bold, 18, lw - ui.s(20), 0.6f);
        if (ui.button("launch", {x, by, lw, ui.s(54)}, go, p)) launch(0);
        const MontagePlan plan = buildMontagePlan(g_.bests, key_, {}, from_, to_);
        UIContext::ButtonStyle ws;
        ws.enabled = plan.withReplay > 0;
        const std::string watch = "Regarder";
        ws.fontSize = ui.fitFont(watch, FontFace::Bold, 18, ow - ui.s(20), 0.6f);
        if (ui.button("watch", {x + lw + g, by, ow, ui.s(54)}, watch, ws) && ws.enabled) openMontage(plan);
        UIContext::ButtonStyle gs;
        gs.selected = ghost_;
        gs.enabled = r && r->replay.has();
        const std::string ghost = ghost_ ? "Fantôme : oui" : "Fantôme : non";
        gs.fontSize = ui.fitFont(ghost, FontFace::Bold, 18, ow - ui.s(20), 0.6f);
        if (ui.button("ghost", {x + lw + g + ow + g, by, ow, ui.s(54)}, ghost, gs) && gs.enabled) ghost_ = !ghost_;
    }

    void statBox(const Rect& r, const std::string& head, const std::string& value, const std::string& sub, Color vc) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.r().fillRoundedRect(r, ui.s(8), Color{14, 20, 40, 230});
        ui.r().drawRoundedRect(r, ui.s(8), th.accent.withAlpha(50), 1.0f);
        const float pad = ui.s(10), room = r.w - pad * 2;
        ui.label(ui.elide(head, FontFace::Bold, fz(ui, 12), room), {r.x + pad, r.y + ui.s(14)}, fz(ui, 12), th.textDim, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        const float vf = ui.fitFont(value, FontFace::Title, 26, room, 0.55f);
        ui.label(ui.elide(value, FontFace::Title, vf, room), {r.x + pad, r.center().y + ui.s(2)}, vf, vc, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label(ui.elide(sub, FontFace::Body, fz(ui, 12), room), {r.x + pad, r.bottom() - ui.s(13)}, fz(ui, 12), th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
    }

    // ------------------------------------------------------------ actions

    /// The chosen range as a section training (from a StartPos `startPos` when given).
    void launch(int startPos) {
        PlayOptions o;
        o.training = true;
        o.trainingFromChains = true;
        o.fromOnline = req_.fromOnline;
        o.ghosts = req_.ghosts;
        o.ghostsChosen = true;
        o.trainingBestGhost = ghost_;
        o.trainingCheckpoint = boundaryId(from_);
        o.trainingEnd = boundaryId(to_);
        if (from_ > 0) o.spawn = req_.checkpoints[static_cast<size_t>(from_ - 1)].feet;
        // The powers: those chosen for this range, else the route's (the tablets before its start).
        const PersonalBests::Range range{o.trainingCheckpoint, o.trainingEnd};
        if (const std::vector<std::string>* chosen = g_.bests.powers(key_, range)) {
            o.trainingPowers = *chosen;
            o.trainingPowersChosen = true;
        } else {
            const float at = boundaryAt(from_);
            for (const auto& [t, power] : req_.tablets)
                if (from_ > 0 && t <= at) o.trainingPowers.push_back(power);
        }
        if (const PersonalBests::Record* r = g_.bests.record(key_, range)) o.trainingBest = r->best;
        if (startPos > 0)
            if (const StartPosEntry* e = g_.startPositions.find(key_, startPos)) {
                o.trainingStartPos = e->state;
                o.trainingStartPosId = e->id;
                g_.startPositions.setActive(key_, e->id);
                g_.startPositions.noteAttempt(key_, e->id);
                g_.startPositions.save();
            }
        g_.app.switchScene(makeGameScene(g_, req_.levelId, std::move(o)), 0.4f);
    }

    void openMontage(const MontagePlan& plan) {
        SobMontageRequest req;
        req.levelId = req_.levelId;
        req.levelHash = req_.levelHash;
        req.levelName = req_.levelName;
        req.plan = plan;
        for (const MontageSegment& s : plan.segments) req.names.push_back(boundaryName(s.index) + "  →  " + boundaryName(s.index + 1));
        g_.app.pushScene(makeSobMontage(g_, std::move(req)));
    }

    GameServices& g_;
    ChainTrainingRequest req_;
    std::string key_;
    int n_ = 0;
    int from_ = 0, to_ = 1;
    int tab_ = 0;
    bool ghost_ = true;
    statsart::LevelArt art_;  // 2.92: the level's pictures (both strips, the preview)
    TrainingStrip strip_{"segments"};
    TrainingMarksPanel marks_;
};

}  // namespace

std::unique_ptr<Scene> makeChainTraining(GameServices& g, ChainTrainingRequest req) { return std::make_unique<ChainTrainingScene>(g, std::move(req)); }

}  // namespace cosmic
