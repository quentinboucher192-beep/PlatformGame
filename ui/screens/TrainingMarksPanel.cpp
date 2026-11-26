#include "ui/screens/TrainingMarksPanel.h"

#include <algorithm>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/ghost/GhostRace.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGate{255, 200, 80, 255};

/// « 21/11 » from a date « 2026-11-21 10:00 » ("" when it is not one).
std::string dayMonth(const std::string& date) { return date.size() >= 10 ? date.substr(8, 2) + "/" + date.substr(5, 2) : std::string{}; }

}  // namespace

void TrainingMarksPanel::draw(GameServices& g, const Rect& area, const Context& c) {
    UIContext& ui = g.ui;
    const float stripH = ui.s(72);
    drawStrip(g, {area.x, area.y, area.w, stripH}, c);
    const float top = area.y + stripH + ui.s(18);
    const float gap = ui.s(24);
    const float colW = (area.w - gap) * 0.5f;
    drawStartPos(g, {area.x, top, colW, area.bottom() - top}, c);
    drawMeasures(g, {area.x + colW + gap, top, colW, area.bottom() - top}, c);
}

void TrainingMarksPanel::drawStrip(GameServices& g, const Rect& r, const Context& c) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    const Rect bar{r.x, r.y + ui.s(6), r.w, r.h - ui.s(30)};
    ui.r().fillRoundedRect(bar, ui.s(8), Color{20, 28, 54, 230});
    ui.r().drawRoundedRect(bar, ui.s(8), th.accent.withAlpha(60), std::max(1.0f, ui.s(1.0f)));
    const Rect& lb = c.levelBounds;
    auto xOf = [&](float worldX) {
        const float f = lb.w > 1.0f ? std::clamp((worldX - lb.x) / lb.w, 0.0f, 1.0f) : 0.5f;
        return bar.x + ui.s(10) + (bar.w - ui.s(20)) * f;
    };
    const float fs = fz(ui, 12);
    // The checkpoints: thin ticks.
    for (const auto& [x, name] : c.checkpoints) {
        const float px = xOf(x);
        ui.r().fillRect({px - 1.0f, bar.y + ui.s(8), 2.0f, bar.h - ui.s(16)}, th.textDim.withAlpha(110));
    }
    // The StartPos (above) and the measure gates (below), their names under the strip, never one
    // over another: a name that would cover the previous one goes down a row.
    struct Tag {
        float x;
        std::string text;
        Color color;
    };
    std::vector<Tag> tags;
    const std::vector<StartPosEntry> sps = g.startPositions.list(c.levelKey);
    for (size_t i = 0; i < sps.size(); ++i) tags.push_back({xOf(sps[i].state.feet.x), "SP " + std::to_string(i + 1), th.accent});
    for (const MeasurePair& p : g.measures.list(c.levelKey)) {
        tags.push_back({xOf(p.a.at.x), "A" + std::to_string(p.id), kGate});
        if (p.b) tags.push_back({xOf(p.b->at.x), "B" + std::to_string(p.id), kGate});
    }
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.x < b.x; });
    float rowEnd[2] = {-1e9f, -1e9f};
    for (const Tag& t : tags) {
        ui.r().fillRect({t.x - 1.0f, bar.y + ui.s(3), 2.0f, bar.h - ui.s(6)}, t.color.withAlpha(220));
        const float w = ui.text().measure(t.text, FontFace::Bold, ui.s(fs)).x + ui.s(10);
        int row = rowEnd[0] <= t.x - w * 0.5f ? 0 : rowEnd[1] <= t.x - w * 0.5f ? 1 : -1;
        if (row < 0) continue;  // three names in one place: the strip's ticks stay, the third name goes
        const float y = bar.bottom() + ui.s(4) + static_cast<float>(row) * ui.s(fs + 6.0f);
        const Rect pill{std::clamp(t.x - w * 0.5f, r.x, r.right() - w), y, w, ui.s(fs + 5.0f)};
        ui.r().fillRoundedRect(pill, ui.s(4), Color{8, 12, 26, 235});
        ui.r().drawRoundedRect(pill, ui.s(4), t.color.withAlpha(200), 1.0f);
        ui.label(t.text, pill.center(), fs, t.color, Align::Center, FontFace::Bold, VAlign::Middle);
        rowEnd[row] = pill.right() + ui.s(4);
    }
}

void TrainingMarksPanel::drawStartPos(GameServices& g, const Rect& r, const Context& c) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    const std::vector<StartPosEntry> list = g.startPositions.list(c.levelKey);
    const int active = g.startPositions.activeId(c.levelKey);
    if (!list.empty() && std::none_of(list.begin(), list.end(), [&](const StartPosEntry& e) { return e.id == selectedSp_; }))
        selectedSp_ = active > 0 ? active : list.front().id;
    const float hf = fz(ui, 15), f = fz(ui, 15), sf = fz(ui, 13);
    float y = r.y;
    // Title and its key.
    const std::string title = "STARTPOS (" + std::to_string(list.size()) + ")";
    const float kh = ui.s(26);
    const std::string hint = "Poser ici";
    const float hintW = Glyphs::measureAction(ui, Action::StartPosPlace, kh) + ui.s(8) + ui.text().measure(hint, FontFace::Body, ui.s(sf)).x;
    ui.label(ui.elide(title, FontFace::Bold, hf, r.w - hintW - ui.s(12)), {r.x, y + kh * 0.5f}, hf, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
    const float hx = r.right() - hintW;
    const float kw = Glyphs::drawAction(ui, Action::StartPosPlace, {hx, y + kh * 0.5f}, kh);
    ui.label(hint, {hx + kw + ui.s(8), y + kh * 0.5f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    y += kh + ui.s(12);
    // The buttons and the notes at the bottom; the list takes the room left.
    const float notesH = ui.s(sf + 10.0f) * 2.0f;
    const float buttonsH = ui.s(46);
    const Rect listRect{r.x, y, r.w, std::max(ui.s(60), r.bottom() - y - buttonsH - notesH - ui.s(24))};
    const float rh = ui.s(56), gap = ui.s(6);
    ui.beginScroll("splist", listRect, std::max(1.0f, static_cast<float>(list.size())) * (rh + gap));
    float ry = listRect.y - ui.scrollOffset();
    if (list.empty())
        ui.labelWrapped("Aucune startpos pour cette version du niveau. En entraînement, posez-en une là où Nova se trouve ; les essais suivants partiront "
                        "de là.",
                        {listRect.x + ui.s(6), ry + ui.s(6)}, listRect.w - ui.s(20), sf, th.textDim);
    for (size_t i = 0; i < list.size(); ++i, ry += rh + gap) {
        const StartPosEntry& e = list[i];
        const Rect row{listRect.x, ry, listRect.w - ui.s(14), rh};
        UIContext::ButtonStyle st;
        st.selected = e.id == selectedSp_;
        if (ui.button("sp" + std::to_string(e.id), row, "", st)) selectedSp_ = e.id;
        if (row.bottom() < listRect.y || row.y > listRect.bottom()) continue;
        // « SP n » tag, the place, then the date and the attempts; the powers and hearts at the right.
        const std::string tag = "SP " + std::to_string(i + 1);
        const float tw = ui.text().measure(tag, FontFace::Bold, ui.s(f)).x + ui.s(14);
        const Rect tr{row.x + ui.s(10), row.center().y - ui.s(14), tw, ui.s(28)};
        ui.r().fillRoundedRect(tr, ui.s(5), e.id == active ? th.accent : th.accent.withAlpha(60));
        ui.label(tag, tr.center(), f, e.id == active ? Color{8, 12, 26, 255} : th.accent, Align::Center, FontFace::Bold, VAlign::Middle);
        const float ic = ui.s(22);
        const float heartsW = ui.text().measure("20 cœurs", FontFace::Bold, ui.s(sf)).x;
        const float rightW = static_cast<float>(std::min<size_t>(e.state.powers.size(), 4)) * (ic + ui.s(3)) + ui.s(10) + heartsW;
        const float tx = tr.right() + ui.s(12), room = row.right() - ui.s(12) - rightW - tx;
        std::string where = c.where ? c.where(e.state.feet) : std::string();
        if (where.empty()) where = "x " + std::to_string(static_cast<int>(std::lround(metersOf(e.state.feet.x - c.levelBounds.x)))) + " m";
        ui.label(ui.elide(where, FontFace::Bold, f, room), {tx, row.y + rh * 0.32f}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string sub = (e.id == active ? std::string("active · ") : std::string()) + "posée le " + dayMonth(e.date) + " · " +
                          std::to_string(e.attempts) + (e.attempts > 1 ? " essais depuis" : " essai depuis");
        ui.label(ui.elide(sub, FontFace::Body, sf, room), {tx, row.y + rh * 0.72f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        float px = row.right() - ui.s(12) - rightW;
        for (size_t k = 0; k < e.state.powers.size() && k < 4; ++k)
            if (const PowerDefinition* d = g.content.power(e.state.powers[k])) {
                ui.icon("ui/icons_powers.png", d->icon, 32, {px, row.center().y - ic * 0.5f, ic, ic});
                px += ic + ui.s(3);
            }
        const std::string hearts = std::to_string(e.state.health) + (e.state.health > 1 ? " cœurs" : " cœur");
        ui.label(ui.elide(hearts, FontFace::Bold, sf, heartsW), {row.right() - ui.s(12), row.center().y}, sf, th.danger, Align::Right, FontFace::Bold,
                 VAlign::Middle);
    }
    ui.endScroll();
    // Buttons.
    y = listRect.bottom() + ui.s(12);
    const StartPosEntry* sel = nullptr;
    for (const StartPosEntry& e : list)
        if (e.id == selectedSp_) sel = &e;
    const int selRank = sel ? g.startPositions.rank(c.levelKey, sel->id) : -1;
    const float bgap = ui.s(10);
    const int buttons = c.startFrom ? 4 : 2;
    const float bw = (r.w - bgap * static_cast<float>(buttons - 1)) / static_cast<float>(buttons);
    float bx = r.x;
    auto fitted = [&](UIContext::ButtonStyle st, const std::string& label) {
        st.small = true;
        st.fontSize = ui.fitFont(label, FontFace::Bold, 16, bw - ui.s(16), 0.55f);
        return st;
    };
    if (c.startFrom) {
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = sel != nullptr;
        const std::string go = sel ? "Partir de SP " + std::to_string(selRank + 1) : std::string("Partir d'une startpos");
        if (ui.button("spgo", {bx, y, bw, buttonsH}, go, fitted(p, go)) && sel) c.startFrom(sel->id);
        bx += bw + bgap;
        const std::string none = "Départ de l'entraînement";
        if (ui.button("spnone", {bx, y, bw, buttonsH}, none, fitted({}, none))) c.startFrom(0);
        bx += bw + bgap;
    }
    UIContext::ButtonStyle del;
    del.enabled = sel != nullptr;
    const std::string delLabel = "Supprimer";
    if (ui.button("spdel", {bx, y, bw, buttonsH}, delLabel, fitted(del, delLabel)) && sel) {
        g.startPositions.remove(c.levelKey, sel->id);
        g.startPositions.save();
        selectedSp_ = 0;
    }
    bx += bw + bgap;
    UIContext::ButtonStyle clr;
    clr.enabled = !list.empty();
    clr.danger = confirmClear_;
    const std::string clrLabel = confirmClear_ ? "Confirmer : tout effacer" : "Tout effacer";
    if (ui.button("spclear", {bx, y, bw, buttonsH}, clrLabel, fitted(clr, clrLabel)) && !list.empty()) {
        if (confirmClear_) {
            g.startPositions.clear(c.levelKey);
            g.startPositions.save();
            confirmClear_ = false;
        } else {
            confirmClear_ = true;
        }
    }
    if (list.empty()) confirmClear_ = false;
    // What a StartPos takes, and what starts again.
    y += buttonsH + ui.s(12);
    const float half = r.w * 0.5f;
    auto note = [&](float x, float yy, const std::string& lead, const std::string& text) {
        const float lw = std::min(ui.text().measure(lead, FontFace::Bold, ui.s(sf)).x, half * 0.45f);
        ui.label(ui.elide(lead, FontFace::Bold, sf, lw), {x, yy}, sf, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(ui.elide(text, FontFace::Body, sf, half - lw - ui.s(18)), {x + lw + ui.s(8), yy}, sf, th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
    };
    const float lh = ui.s(sf + 10.0f);
    note(r.x, y + lh * 0.5f, "Pris à la pose", "position, direction de Nova");
    note(r.x + half, y + lh * 0.5f, "et aussi", "pouvoirs, cœurs, énergie");
    note(r.x, y + lh * 1.5f, "Remis à zéro", "recharges, élan (départ arrêté)");
    note(r.x + half, y + lh * 1.5f, "Le niveau", "ennemis et mécanismes au départ");
}

void TrainingMarksPanel::drawMeasures(GameServices& g, const Rect& r, const Context& c) {
    UIContext& ui = g.ui;
    const UITheme& th = ui.theme();
    const std::vector<MeasurePair> list = g.measures.list(c.levelKey);
    if (!list.empty() && std::none_of(list.begin(), list.end(), [&](const MeasurePair& p) { return p.id == selectedPair_; })) selectedPair_ = list.front().id;
    const float hf = fz(ui, 15), f = fz(ui, 15), sf = fz(ui, 13);
    float y = r.y;
    int complete = 0;
    for (const MeasurePair& p : list) complete += p.complete() ? 1 : 0;
    const std::string title = "POINTS DE MESURE (" + std::to_string(complete) + (complete > 1 ? " PAIRES)" : " PAIRE)");
    const float kh = ui.s(26);
    const std::string hint = "Poser A, puis B";
    const float hintW = Glyphs::measureAction(ui, Action::MeasurePlace, kh) + ui.s(8) + ui.text().measure(hint, FontFace::Body, ui.s(sf)).x;
    ui.label(ui.elide(title, FontFace::Bold, hf, r.w - hintW - ui.s(12)), {r.x, y + kh * 0.5f}, hf, kGate, Align::Left, FontFace::Bold, VAlign::Middle);
    const float hx = r.right() - hintW;
    const float kw = Glyphs::drawAction(ui, Action::MeasurePlace, {hx, y + kh * 0.5f}, kh);
    ui.label(hint, {hx + kw + ui.s(8), y + kh * 0.5f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    y += kh + ui.s(12);
    // The bottom: buttons, three options, the note; the list takes the room left.
    const float buttonsH = ui.s(46), toggleH = ui.s(44);
    const std::string warn = "Gardés sur cet ordinateur, pour cette version du niveau. Si le niveau change, les anciens repères restent avec "
                             "l'ancienne version et leurs temps ne sont plus comparés.";
    const float warnH = ui.text().measureWrapped(warn, FontFace::Body, ui.s(sf), r.w).y;
    const Rect listRect{r.x, y, r.w, std::max(ui.s(60), r.bottom() - y - buttonsH - toggleH * 3.0f - warnH - ui.s(40))};
    const float rh = ui.s(56), gap = ui.s(6);
    ui.beginScroll("mlist", listRect, std::max(1.0f, static_cast<float>(list.size())) * (rh + gap));
    float ry = listRect.y - ui.scrollOffset();
    if (list.empty())
        ui.labelWrapped("Aucun point de mesure. En entraînement, posez A puis B : franchir A lance un chrono, franchir B l'arrête.",
                        {listRect.x + ui.s(6), ry + ui.s(6)}, listRect.w - ui.s(20), sf, th.textDim);
    for (const MeasurePair& p : list) {
        const Rect row{listRect.x, ry, listRect.w - ui.s(14), rh};
        ry += rh + gap;
        const std::string n = std::to_string(p.id);
        UIContext::ButtonStyle st;
        st.selected = p.id == selectedPair_;
        if (renaming_ == p.id) {
            // The new name, typed in place (Entrée validates).
            const Rect field{row.x + ui.s(8), row.y + ui.s(6), row.w - ui.s(16), row.h - ui.s(12)};
            ui.setFocus("mrename");
            if (ui.textField("mrename", field, renameText_, "Mesure " + n, MeasureStore::kNameMax)) {
                g.measures.rename(c.levelKey, p.id, renameText_);
                g.measures.save();
                renaming_ = 0;
            }
            continue;
        }
        if (ui.button("m" + n, row, "", st)) selectedPair_ = p.id;
        if (row.bottom() < listRect.y || row.y > listRect.bottom()) continue;
        const std::string tag = p.complete() ? "A" + n + "→B" + n : "A" + n + "…";
        const float tw = ui.text().measure(tag, FontFace::Bold, ui.s(f)).x + ui.s(14);
        const Rect tr{row.x + ui.s(10), row.center().y - ui.s(14), tw, ui.s(28)};
        ui.r().fillRoundedRect(tr, ui.s(5), kGate.withAlpha(p.complete() ? 230 : 90));
        ui.label(tag, tr.center(), f, Color{40, 28, 8, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        const std::string best = p.best > 0.0f ? formatGhostSeconds(p.best) : std::string("—");
        const float bestW = ui.text().measure(best, FontFace::Bold, ui.s(f)).x;
        const float barsW = ui.s(70);
        const float tx = tr.right() + ui.s(12), room = row.right() - ui.s(12) - barsW - ui.s(14) - bestW - ui.s(14) - tx;
        {
            i18n::Verbatim words;  // as the player named it
            const std::string name = p.name.empty() ? std::string() : p.name;
            if (!name.empty())
                ui.label(ui.elide(name, FontFace::Bold, f, room), {tx, row.y + rh * 0.32f}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        }
        if (p.name.empty())
            ui.label(ui.elide("Mesure " + n, FontFace::Bold, f, room), {tx, row.y + rh * 0.32f}, f, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        const std::string sub = !p.complete()          ? std::string("B pas encore posé")
                                : p.last.empty()       ? std::string("pas encore d'essai")
                                                       : std::to_string(p.attempts) + (p.attempts > 1 ? " essais" : " essai") + " · dernier " +
                                                       formatGhostSeconds(p.last.back());
        ui.label(ui.elide(sub, FontFace::Body, sf, room), {tx, row.y + rh * 0.72f}, sf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(best, {row.right() - ui.s(12) - barsW - ui.s(14), row.center().y}, f, p.best > 0.0f ? Color{255, 214, 90, 255} : th.textDim,
                 Align::Right, FontFace::Bold, VAlign::Middle);
        // The last times as small bars (the best in gold).
        const size_t count = std::min<size_t>(p.last.size(), 7);
        if (count > 0) {
            float lo = 1e9f, hi = 0.0f;
            for (size_t i = p.last.size() - count; i < p.last.size(); ++i) lo = std::min(lo, p.last[i]), hi = std::max(hi, p.last[i]);
            const float slot = barsW / 7.0f, bh = rh - ui.s(22);
            for (size_t k = 0; k < count; ++k) {
                const float v = p.last[p.last.size() - count + k];
                const float h = bh * (hi > lo ? 0.35f + 0.65f * (v - lo) / (hi - lo) : 0.6f);
                const bool isBest = p.best > 0.0f && std::llround(v * 1000.0f) == std::llround(p.best * 1000.0f);
                ui.r().fillRect({row.right() - ui.s(12) - barsW + slot * static_cast<float>(k), row.bottom() - ui.s(11) - h, slot * 0.7f, h},
                                isBest ? Color{255, 214, 90, 255} : th.textDim.withAlpha(150));
            }
        }
    }
    ui.endScroll();
    // Rename, move, delete.
    y = listRect.bottom() + ui.s(12);
    const MeasurePair* sel = g.measures.find(c.levelKey, selectedPair_);
    const float bgap = ui.s(10);
    const int buttons = c.movePair ? 3 : 2;
    const float bw = (r.w - bgap * static_cast<float>(buttons - 1)) / static_cast<float>(buttons);
    float bx = r.x;
    auto fitted = [&](UIContext::ButtonStyle st, const std::string& label) {
        st.small = true;
        st.fontSize = ui.fitFont(label, FontFace::Bold, 16, bw - ui.s(16), 0.55f);
        return st;
    };
    UIContext::ButtonStyle on;
    on.enabled = sel != nullptr && renaming_ == 0;
    const std::string ren = "Renommer";
    if (ui.button("mren", {bx, y, bw, buttonsH}, ren, fitted(on, ren)) && on.enabled) {
        renaming_ = sel->id;
        renameText_ = sel->name;
    }
    bx += bw + bgap;
    if (c.movePair) {
        const std::string mv = "Déplacer A / B";
        if (ui.button("mmove", {bx, y, bw, buttonsH}, mv, fitted(on, mv)) && on.enabled) c.movePair(sel->id);
        bx += bw + bgap;
    }
    const std::string del = "Supprimer";
    if (ui.button("mdel", {bx, y, bw, buttonsH}, del, fitted(on, del)) && on.enabled) {
        g.measures.remove(c.levelKey, sel->id);
        g.measures.save();
        selectedPair_ = 0;
    }
    // The options (kept with the measures and the StartPos).
    y += buttonsH + ui.s(10);
    bool showSp = g.startPositions.show, showGates = g.measures.showGates, inReplays = g.measures.inReplays;
    if (ui.toggle("optsp", {r.x, y, r.w, toggleH - ui.s(4)}, "Montrer les startpos dans le niveau (rémanence)", showSp)) {
        g.startPositions.show = showSp;
        g.startPositions.save();
    }
    y += toggleH;
    if (ui.toggle("optgates", {r.x, y, r.w, toggleH - ui.s(4)}, "Montrer les portes de mesure", showGates)) {
        g.measures.showGates = showGates;
        g.measures.save();
    }
    y += toggleH;
    if (ui.toggle("optreplays", {r.x, y, r.w, toggleH - ui.s(4)}, "Mesurer aussi dans les replays (les vôtres et ceux en ligne)", inReplays)) {
        g.measures.inReplays = inReplays;
        g.measures.save();
    }
    y += toggleH + ui.s(6);
    ui.labelWrapped(warn, {r.x, y}, r.w, sf, th.warm);
    if (renaming_ > 0 && ui.input().menuPressed(Action::MenuBack)) renaming_ = 0;
}

}  // namespace cosmic
