#include "ui/widgets/HeartSheetView.h"

#include <algorithm>
#include <map>

#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "game/GameServices.h"
#include "game/online/AsterionBody.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/AsterionFigure.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::heartsheet {

namespace {

const Color kGold{255, 205, 110, 255};
const Color kText{226, 232, 248, 255};
const Color kMuted{150, 160, 196, 255};
const Color kPanel{24, 34, 68, 235};
const Color kGreen{110, 225, 150, 255};
const Color kRed{255, 100, 100, 255};
const Color kInk{10, 16, 30, 255};

constexpr float kLine = 18.0f, kCardLine = 19.0f;

std::vector<std::string> wrapped(UIContext& ui, const std::string& t, float size, float w, int maxLines) {
    return raidui::wrap(ui, t, FontFace::Body, size, w, maxLines);
}

}  // namespace

int State::relicCount() const {
    if (real) return realRelics;
    int n = 0;
    for (int k = 0; k < 8; ++k) n += (relics >> k) & 1;
    return n;
}

State realState(const raid::RaidSheets& s, const Json& titan, bool* ok) {
    State st;
    const bool running = titan.is_object() && jstr(titan.value("event", Json::object()), "state") == "en_cours";
    const Json heart = titan.is_object() ? titan.value("heart", Json()) : Json();
    if (!running || !heart.is_object() || !heart.contains("modifiers")) {
        if (ok) *ok = false;
        return st;
    }
    for (size_t k = 0; k < s.heartModifiers.size() && k < 8; ++k)
        for (const Json& m : heart.value("modifiers", Json::array()))
            if (jstr(m, "id") == s.heartModifiers[k].id && jget<bool>(m, "on", false)) st.fallen |= static_cast<uint8_t>(1u << k);
    st.real = true;
    st.realAvailable = true;
    st.realRelics = std::clamp(jget<int>(heart, "relics", 0), 0, 7);
    if (ok) *ok = true;
    return st;
}

Layout layoutHeart(UIContext& ui, const raid::RaidSheets& s, const State& st, const Rect& area) {
    Layout L;
    const float gap = ui.s(16), pad = ui.s(14);
    L.stacked = area.w < ui.s(900);
    const float colW = L.stacked ? area.w : (area.w - 2.0f * gap) / 3.0f;
    const int count = static_cast<int>(s.heartModifiers.size());
    int fallen = 0;
    for (int k = 0; k < count; ++k) fallen += st.down(k) ? 1 : 0;

    // « Ce que le clan a accompli ».
    {
        const float x = area.x + pad, w = colW - 2.0f * pad;
        float y = area.y + pad;
        L.leftTitle = {x, y, w, ui.s(26)};
        y = L.leftTitle.bottom() + ui.s(4);
        const std::string mode = st.real ? "État réel du clan, lu sur la carte du titan de l'événement en cours."
                                 : st.realAvailable
                                     ? "Simulation : changez une partie ou une relique ; R ou « État réel » rétablit l'état du clan."
                                     : "Simulation libre (aucun raid en cours) : clic, Entrée ou A sur une partie, ou sur ses boutons.";
        L.modeLines = wrapped(ui, mode, 13, w, 3);
        L.modeR = {x, y, w, static_cast<float>(L.modeLines.size()) * ui.s(kLine)};
        y = L.modeR.bottom() + ui.s(8);
        // Tall enough for the titan at twice its pixels on a large screen (its sprite: 160 × 128).
        L.figure = {x, y, w, std::min(ui.s(270), std::max(ui.s(130), w * 0.9f))};
        y = L.figure.bottom() + ui.s(10);
        for (int k = 0; k < count; ++k) {
            const raid::HeartModifier& m = s.heartModifiers[static_cast<size_t>(k)];
            Part p;
            p.part = m.part;
            p.name = m.partName;
            p.row = {x, y, w, ui.s(32)};
            const float bh = ui.s(28), by = y + ui.s(2);
            const float relicW = std::min(ui.s(112), w * 0.32f), fallW = std::min(ui.s(96), w * 0.27f);
            p.relicBtn = {p.row.right() - relicW, by, relicW, bh};
            p.fallBtn = {p.relicBtn.x - ui.s(6) - fallW, by, fallW, bh};
            p.name_ = {x, y, std::max(0.0f, p.fallBtn.x - ui.s(8) - x), p.row.h};
            L.parts.push_back(std::move(p));
            y += ui.s(34);
        }
        L.totalsText = std::to_string(fallen) + " / " + std::to_string(count) + " parties tombées · " + std::to_string(st.relicCount()) + " / " +
                       std::to_string(count) + " reliques";
        L.totals = {x, y + ui.s(4), w, ui.s(26)};
        y = L.totals.bottom() + ui.s(2);
        std::string honest = "En raid, le Cœur s'ouvre quand les 7 Gigaboss sont tombés.";
        if (st.real) honest += " La carte du titan donne le nombre de reliques, pas leurs branches.";
        L.honest = wrapped(ui, honest, 13, w, 3);
        L.honestR = {x, y, w, static_cast<float>(L.honest.size()) * ui.s(kLine)};
        y = L.honestR.bottom() + ui.s(8);
        if (st.realAvailable) {
            L.resetBtn = {x, y, w, ui.s(36)};
            y = L.resetBtn.bottom() + ui.s(4);
        }
        L.left = {area.x, area.y, colW, y + pad - area.y};
    }
    // « Statistiques ».
    {
        const float mx = L.stacked ? area.x : area.x + colW + gap;
        const float my = L.stacked ? L.left.bottom() + gap : area.y;
        L.stats = gbstats::layoutStats(ui, s.heart.stats, mx + pad, my + pad, colW - 2.0f * pad, gbstats::Mode::Heart, st.relicCount());
        L.middle = {mx, my, colW, L.stats.height + 2.0f * pad};
    }
    // « Ses attaques selon les parties tombées ».
    {
        const float rx = L.stacked ? area.x : area.x + 2.0f * (colW + gap);
        const float ry = L.stacked ? L.middle.bottom() + gap : area.y;
        const float x = rx + pad, w = colW - 2.0f * pad;
        float y = ry + pad;
        L.rightTitle = {x, y, w, ui.s(26)};
        y = L.rightTitle.bottom() + ui.s(8);
        const float cpad = ui.s(10), icon = ui.s(24);
        for (int k = 0; k < count; ++k) {
            const raid::HeartModifier& m = s.heartModifiers[static_cast<size_t>(k)];
            Card c;
            c.weakened = st.down(k);
            const float tx = x + cpad + icon + cpad, tw = x + w - cpad - tx;
            c.lines = wrapped(ui, m.label, 14, tw, 3);
            c.tagText = (c.weakened ? "tombé : " : "si tombé : ") + m.partName + (m.boss.empty() ? std::string() : " · " + m.boss);
            float cy = y + cpad;
            for (size_t i = 0; i < c.lines.size(); ++i) {
                c.lineR.push_back({tx, cy, tw, ui.s(kCardLine)});
                cy += ui.s(kCardLine);
            }
            c.tag = {tx, cy + ui.s(2), tw, ui.s(18)};
            c.r = {x, y, w, c.tag.bottom() + cpad - y};
            c.icon = {x + cpad, c.r.y + (c.r.h - icon) * 0.5f, icon, icon};
            y = c.r.bottom() + ui.s(8);
            L.cards.push_back(std::move(c));
        }
        L.legend = wrapped(ui,
                           "Vert : affaibli par la chute de la partie. Rouge : encore actif. Les liens « modificateur » du titan agissent sur le Cœur dans son "
                           "arène.",
                           12, w, 4);
        L.legendR = {x, y, w, static_cast<float>(L.legend.size()) * ui.s(17)};
        y = L.legendR.bottom();
        L.right = {rx, ry, colW, y + pad - ry};
    }
    if (!L.stacked) {
        const float h = std::max({L.left.h, L.middle.h, L.right.h});
        L.left.h = L.middle.h = L.right.h = h;
        L.height = h;
    } else {
        L.height = L.right.bottom() - area.y;
    }
    return L;
}

Action drawHeart(GameServices& g, const raid::RaidSheets& s, const State& st, const Layout& L, AsterionFigure& figure, gbstats::Tip& tip) {
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    Action act;
    for (const Rect& p : {L.left, L.middle, L.right}) {
        r.fillRoundedRect(p, ui.s(14), kPanel);
        r.drawRoundedRect(p, ui.s(14), Color{44, 58, 104, 255}, ui.s(1.2f));
    }
    raidui::fit(ui, "CE QUE LE CLAN A ACCOMPLI", {L.leftTitle.x, L.leftTitle.center().y}, 16, L.leftTitle.w, kGold, FontFace::Bold);
    {
        i18n::Verbatim shown(true);
        raidui::drawLines(ui, L.modeLines, {L.modeR.x, L.modeR.y}, 13, ui.s(kLine), st.real ? kGreen : kMuted);
    }
    // The titan: its parts fallen crossed, its relics ◆; a part clicked (or activated) falls or stands again.
    std::map<std::string, asterion::State> states;
    std::vector<std::string> openable;
    for (size_t k = 0; k < L.parts.size(); ++k) {
        states[L.parts[k].part] = st.down(static_cast<int>(k)) ? asterion::State::Destroyed : asterion::State::Intact;
        openable.push_back(L.parts[k].part);
    }
    const std::string hit = figure.draw(g, "heart/titan", L.figure, states, openable);
    for (size_t k = 0; k < L.parts.size(); ++k) {
        if (!hit.empty() && hit == L.parts[k].part) act.toggleFall = static_cast<int>(k);
        Vec2 a;
        if (!figure.anchorOf(L.parts[k].part, a)) continue;
        const float d = ui.s(9);
        if (st.down(static_cast<int>(k))) {
            r.drawLine({a.x - d, a.y - d}, {a.x + d, a.y + d}, kGreen, ui.s(3));
            r.drawLine({a.x - d, a.y + d}, {a.x + d, a.y - d}, kGreen, ui.s(3));
        }
        if (!st.real && ((st.relics >> k) & 1u)) {
            const Vec2 c{a.x + ui.s(14), a.y - ui.s(14)};
            r.fillConvexPolygon({{c.x, c.y - ui.s(8)}, {c.x + ui.s(8), c.y}, {c.x, c.y + ui.s(8)}, {c.x - ui.s(8), c.y}}, kGold);
        }
    }
    for (size_t k = 0; k < L.parts.size(); ++k) {
        const Part& p = L.parts[k];
        const bool down = st.down(static_cast<int>(k));
        raidui::fit(ui, p.name, {p.name_.x, p.name_.center().y}, 14, p.name_.w, down ? kGreen : kText, FontFace::Bold);
        UIContext::ButtonStyle bs;
        bs.small = true;
        bs.selected = down;
        const std::string fall = down ? "Tombé" : "Debout";
        bs.fontSize = std::max(ui.fitFont(fall, FontFace::Bold, 14, p.fallBtn.w - ui.s(12), 0.7f), 11.5f / ui.s(1.0f));
        if (ui.button("heart/fall" + std::to_string(k), p.fallBtn, fall, bs)) act.toggleFall = static_cast<int>(k);
        const bool relic = !st.real && ((st.relics >> k) & 1u);
        const std::string rl = st.real ? "◆ ?" : relic ? "◆ Relique" : "Sans relique";
        UIContext::ButtonStyle rs;
        rs.small = true;
        rs.selected = relic;
        rs.fontSize = std::max(ui.fitFont(rl, FontFace::Bold, 14, p.relicBtn.w - ui.s(12), 0.7f), 11.5f / ui.s(1.0f));
        if (ui.button("heart/relic" + std::to_string(k), p.relicBtn, rl, rs)) act.toggleRelic = static_cast<int>(k);
    }
    raidui::fit(ui, L.totalsText, {L.totals.x, L.totals.center().y}, 16, L.totals.w, kText, FontFace::Bold);
    {
        i18n::Verbatim shown(true);
        raidui::drawLines(ui, L.honest, {L.honestR.x, L.honestR.y}, 13, ui.s(kLine), kMuted);
    }
    if (!L.resetBtn.empty()) {
        UIContext::ButtonStyle bs;
        bs.small = true;
        bs.enabled = !st.real;
        const std::string t = "État réel du clan (R)";
        bs.fontSize = std::max(ui.fitFont(t, FontFace::Bold, 15, L.resetBtn.w - ui.s(16), 0.7f), 11.5f / ui.s(1.0f));
        if (ui.button("heart/reset", L.resetBtn, t, bs)) act.reset = true;
        if (!st.real && ui.acceptsInput() && ui.input().keyPressed(SDL_SCANCODE_R)) act.reset = true;
    }
    // The statistics.
    gbstats::drawStats(ui, L.stats, "heart/stats", tip);
    // Its attacks by the parts fallen.
    raidui::fit(ui, "SES ATTAQUES SELON LES PARTIES TOMBÉES", {L.rightTitle.x, L.rightTitle.center().y}, 16, L.rightTitle.w, kGold, FontFace::Bold);
    for (const Card& c : L.cards) {
        const Color col = c.weakened ? kGreen : kRed;
        r.fillRoundedRect(c.r, ui.s(10), Color{22, 32, 64, 240});
        r.drawRoundedRect(c.r, ui.s(10), col, ui.s(1.6f));
        r.fillCircle(c.icon.center(), c.icon.w * 0.5f, col);
        if (c.weakened) {  // a tick drawn (the font has no ✓)
            const Vec2 o = c.icon.center();
            const float u = c.icon.w / 24.0f;
            r.drawLine({o.x - 6.0f * u, o.y}, {o.x - 2.0f * u, o.y + 5.0f * u}, kInk, std::max(1.5f, 2.6f * u));
            r.drawLine({o.x - 2.0f * u, o.y + 5.0f * u}, {o.x + 6.5f * u, o.y - 5.0f * u}, kInk, std::max(1.5f, 2.6f * u));
        } else {
            ui.label("!", c.icon.center(), 15, kInk, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        {
            i18n::Verbatim shown(true);
            for (size_t i = 0; i < c.lines.size() && i < c.lineR.size(); ++i)
                ui.label(c.lines[i], {c.lineR[i].x, c.lineR[i].center().y}, 14, kText, Align::Left, FontFace::Body, VAlign::Middle);
        }
        raidui::fit(ui, c.tagText, {c.tag.x, c.tag.center().y}, 12, c.tag.w, col, FontFace::Body);
    }
    {
        i18n::Verbatim shown(true);
        raidui::drawLines(ui, L.legend, {L.legendR.x, L.legendR.y}, 12, ui.s(17), kMuted);
    }
    (void)s;
    return act;
}

}  // namespace cosmic::heartsheet
