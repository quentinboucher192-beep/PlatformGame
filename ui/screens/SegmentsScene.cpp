// Results → Segments (Ghost Racing): the run checkpoint by checkpoint. For each segment its
// duration in this run, my best duration, my record's, each ghost's, and the cumulative
// gaps; then the history of my runs of this level version. Durations and cumulative times
// are never mixed: each column says which it is.

#include <algorithm>
#include <cmath>

#include "game/GameServices.h"
#include "game/ghost/GhostRace.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

class SegmentsScene : public Scene {
public:
    SegmentsScene(GameServices& g, std::shared_ptr<const SegmentSummary> s) : g_(g), s_(std::move(s)) {}
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("segs/close"); }

    void render(float) override {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 252});
        const float m = ui.s(48);
        Rect panel{m, ui.s(36), ui.screenW() - 2 * m, ui.screenH() - ui.s(72)};
        ui.panel(panel, true, 18);
        ui.pushId("segs");
        float x = panel.x + ui.s(30), w = panel.w - ui.s(60), y = panel.y + ui.s(26);
        ui.label("SEGMENTS", {x, y + ui.s(20)}, 34, th.text, Align::Left, FontFace::Title, VAlign::Middle);
        std::string sub = s_->levelName + "  ·  " + formatGhostTime(s_->time);
        if (s_->pbTime > 0.0f) sub += "  (" + formatGhostDelta(s_->time - s_->pbTime) + " sur mon record d'avant)";
        ui.label(ui.elide(sub, FontFace::Body, fz(ui, 18), w - ui.s(260)), {x, y + ui.s(58)}, fz(ui, 18), th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        UIContext::ButtonStyle cs;
        cs.small = true;
        bool close = ui.button("close", {panel.right() - ui.s(30) - ui.s(200), y, ui.s(200), ui.s(46)}, "Fermer", cs);
        y += ui.s(92);
        float histH = std::min(ui.s(230), (panel.bottom() - y) * 0.36f);
        drawTable({x, y, w, panel.bottom() - y - histH - ui.s(40)});
        drawHistory({x, panel.bottom() - histH - ui.s(20), w, histH});
        ui.popId();
        if (close || ui.backPressed()) g_.app.popScene();
        ui.end();
    }

private:
    /// Duration of segment `i` from cumulative splits and a final time (-1 if unknown).
    static float seg(const std::vector<float>& splits, float final, size_t i) {
        if (i < splits.size()) return splits[i] - (i > 0 ? splits[i - 1] : 0.0f);
        if (i == splits.size() && final > 0.0f) return final - (splits.empty() ? 0.0f : splits.back());
        return -1.0f;
    }
    static float cum(const std::vector<float>& splits, float final, size_t i) {
        if (i < splits.size()) return splits[i];
        if (i == splits.size() && final > 0.0f) return final;
        return -1.0f;
    }

    void drawTable(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const SegmentSummary& s = *s_;
        size_t n = s.splits.size() + 1;  // + the exit
        // Columns: segment | this run (duration, cumulative) | my best | my record | ghosts (max 3).
        size_t ghosts = std::min<size_t>(s.ghosts.size(), 3);
        float nameW = std::min(ui.s(260), area.w * 0.22f);
        int cols = 3 + static_cast<int>(ghosts);
        float cw = (area.w - nameW) / static_cast<float>(cols);
        const float fs = fz(ui, 16), hs = fz(ui, 14);
        float y = area.y;
        auto head = [&](int c, const std::string& t, const std::string& t2, Color col) {
            float cx = area.x + nameW + cw * (static_cast<float>(c) + 1.0f) - ui.s(10);
            ui.label(ui.elide(t, FontFace::Bold, hs, cw - ui.s(16)), {cx, y}, hs, col, Align::Right, FontFace::Bold);
            ui.label(ui.elide(t2, FontFace::Body, fz(ui, 13), cw - ui.s(16)), {cx, y + ui.s(20)}, fz(ui, 13), th.textDim, Align::Right);
        };
        ui.label("Segment", {area.x, y}, hs, th.textDim, Align::Left, FontFace::Bold);
        head(0, "Cette course", "durée · temps", th.accent);
        head(1, "Mon meilleur", "segment", Color{255, 214, 90, 255});
        head(2, "Mon record", "durée de ce segment", th.text);
        for (size_t gi = 0; gi < ghosts; ++gi) head(3 + static_cast<int>(gi), s.ghosts[gi].name, "durée · écart cumulé", s.ghosts[gi].color);
        y += ui.s(46);
        ui.r().fillRect({area.x, y - ui.s(6), area.w, std::max(1.0f, ui.s(1))}, th.accent.withAlpha(60));
        float rh = ui.s(50);
        ui.beginScroll("table", {area.x, y, area.w, area.bottom() - y}, static_cast<float>(n + 1) * rh);
        float ry = y - ui.scrollOffset();
        float sumBest = 0.0f;
        bool sumOk = true;
        for (size_t i = 0; i < n; ++i, ry += rh) {
            std::string name = i < s.names.size() ? s.names[i] : i + 1 == n ? "Arrivée" : "Point " + std::to_string(i + 1);
            float mine = seg(s.splits, s.time, i), mineCum = cum(s.splits, s.time, i);
            float best = i < s.bestSegments.size() ? s.bestSegments[i] : -1.0f;
            float rec = seg(s.pbSplits, s.pbTime, i);
            if (best > 0.0f) sumBest += best;
            else sumOk = false;
            if (i % 2 == 0) ui.r().fillRect({area.x, ry, area.w - ui.s(14), rh}, Color{255, 255, 255, 8});
            ui.label(ui.elide(name, FontFace::Body, fs, nameW - ui.s(10)), {area.x + ui.s(8), ry + rh * 0.5f}, fs, th.text, Align::Left, FontFace::Body,
                     VAlign::Middle);
            auto cell = [&](int c, const std::string& top, Color tc, const std::string& bottom, Color bc) {
                float cx = area.x + nameW + cw * (static_cast<float>(c) + 1.0f) - ui.s(10);
                ui.label(ui.elide(top, FontFace::Bold, fs, cw - ui.s(16)), {cx, ry + ui.s(6)}, fs, tc, Align::Right, FontFace::Bold);
                if (!bottom.empty())
                    ui.label(ui.elide(bottom, FontFace::Body, fz(ui, 13), cw - ui.s(16)), {cx, ry + ui.s(28)}, fz(ui, 13), bc, Align::Right);
            };
            bool gold = mine > 0.0f && best > 0.0f && mine < best - 0.0005f;
            cell(0, mine >= 0.0f ? formatGhostTime(mine) : "—", gold ? Color{255, 214, 90, 255} : th.accent, mineCum >= 0.0f ? formatGhostTime(mineCum) : "",
                 th.textDim);
            cell(1, best > 0.0f ? formatGhostTime(best) : "—", th.text, gold ? "battu !" : (best > 0.0f && mine >= 0.0f ? formatGhostDelta(mine - best, false) : ""),
                 gold ? Color{255, 214, 90, 255} : th.textDim);
            cell(2, rec >= 0.0f ? formatGhostTime(rec) : "—", th.text, rec >= 0.0f && mine >= 0.0f ? formatGhostDelta(mine - rec, false) : "",
                 mine >= 0.0f && rec >= 0.0f ? (mine < rec ? th.success : mine > rec ? th.danger : th.textDim) : th.textDim);
            for (size_t gi = 0; gi < ghosts; ++gi) {
                const auto& gh = s.ghosts[gi];
                float gseg = seg(gh.splits, gh.duration, i), gcum = cum(gh.splits, gh.duration, i);
                std::string bottom;
                Color bc = th.textDim;
                if (gcum >= 0.0f && mineCum >= 0.0f && gh.splits.size() == s.splits.size()) {
                    float d = mineCum - gcum;
                    bottom = formatGhostDelta(d, false);
                    bc = d < -0.0005f ? th.success : d > 0.0005f ? th.danger : th.textDim;
                }
                cell(3 + static_cast<int>(gi), gseg >= 0.0f ? formatGhostTime(gseg) : "—", th.text, bottom, bc);
            }
        }
        // The sum of my best segments (the time a perfect run would make).
        ui.label("Somme de mes meilleurs segments", {area.x + ui.s(8), ry + rh * 0.5f}, fs, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(sumOk && sumBest > 0.0f ? formatGhostTime(sumBest) : "—", {area.x + nameW + cw * 2.0f - ui.s(10), ry + rh * 0.5f}, fs,
                 Color{255, 214, 90, 255}, Align::Right, FontFace::Bold, VAlign::Middle);
        ui.endScroll();
    }

    void drawHistory(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.separator({area.x, area.y - ui.s(10)}, area.w);
        ui.label("Mes dernières courses sur cette version du niveau", {area.x, area.y}, fz(ui, 18), th.text, Align::Left, FontFace::Bold);
        const auto* runs = g_.segments.runs(s_->levelKey);
        float y = area.y + ui.s(34);
        if (!runs || runs->empty()) {
            ui.label("Aucune course enregistrée.", {area.x, y}, fz(ui, 15), th.textDim);
            return;
        }
        float best = 1e9f;
        for (const auto& r : *runs) best = std::min(best, r.time);
        // In columns, most recent first.
        int perCol = std::max(1, static_cast<int>((area.bottom() - y) / ui.s(28)));
        float colW = std::min(ui.s(380), area.w / 3.0f);
        for (size_t i = 0; i < runs->size(); ++i) {
            int col = static_cast<int>(i) / perCol, row = static_cast<int>(i) % perCol;
            if ((col + 1) * colW > area.w + 1.0f) break;
            const auto& r = (*runs)[i];
            float cx = area.x + col * colW, cy = y + row * ui.s(28);
            std::string d = r.date.size() >= 16 ? r.date.substr(8, 2) + "/" + r.date.substr(5, 2) + " " + r.date.substr(11, 5) : r.date;
            ui.label(d, {cx, cy}, fz(ui, 15), th.textDim);
            ui.label(formatGhostTime(r.time), {cx + colW * 0.55f, cy}, fz(ui, 15), r.time <= best ? Color{255, 214, 90, 255} : th.text, Align::Right,
                     FontFace::Bold);
            if (r.time > best) ui.label(formatGhostDelta(r.time - best, false), {cx + colW - ui.s(24), cy}, fz(ui, 14), th.danger, Align::Right);
        }
    }

    GameServices& g_;
    std::shared_ptr<const SegmentSummary> s_;
};

}  // namespace

std::unique_ptr<Scene> makeSegments(GameServices& g, std::shared_ptr<const SegmentSummary> s) {
    return std::make_unique<SegmentsScene>(g, std::move(s));
}

}  // namespace cosmic
