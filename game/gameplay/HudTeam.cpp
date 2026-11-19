// 2.62: the HUD of a team (docs/COOP.md): the team panel in place of one player's vitals and
// powers, the names over the players, the souls and their revival. Every text is measured and
// placed after the previous one, fitted (smaller font) or shortened when the room is short:
// nothing overlaps.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/powers/Powers.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {
constexpr const char* kHudIcons = "ui/icons_hud.png";
enum HudIcon { HeartFull = 0, HeartEmpty = 1 };

TextStyle teamStyle(UIContext& ui, float size, Color c, FontFace f, Align a, VAlign v) {
    TextStyle s;
    s.size = ui.s(size);
    s.color = c;
    s.face = f;
    s.align = a;
    s.valign = v;
    s.shadow = true;
    s.shadowOffset = ui.s(1.5f);
    return s;
}

/// « 1,2 s » (French decimal comma).
std::string seconds1(float s) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f s", static_cast<double>(std::max(0.0f, s)));
    std::string out = b;
    for (char& c : out)
        if (c == '.') c = ',';
    return out;
}

/// Text fitted into `room` pixels: a smaller font first, then shortened with « … ».
struct Fitted {
    std::string text;
    float size = 0.0f;  // unscaled
};
Fitted fitText(UIContext& ui, const std::string& t, FontFace face, float size, float room) {
    Fitted f;
    f.size = ui.fitFont(t, face, size, room, 0.7f);
    f.text = ui.elide(t, face, f.size, room);
    return f;
}
}  // namespace

std::string Hud::teamName(int i) const {
    if (i >= 0 && i < static_cast<int>(team_.names.size()) && !team_.names[static_cast<size_t>(i)].empty())
        return team_.names[static_cast<size_t>(i)];
    return "Joueur " + std::to_string(i + 1);
}

Color Hud::teamColor(int i) const {
    static const Color kDefault[3] = {{62, 230, 214, 255}, {255, 122, 184, 255}, {255, 210, 122, 255}};
    if (i >= 0 && i < static_cast<int>(team_.colors.size())) return team_.colors[static_cast<size_t>(i)];
    return kDefault[std::clamp(i, 0, 2)];
}

float Hud::teamPanelHeight(float unit, int players, int notices) {
    return unit * (static_cast<float>(players) * 64.0f + 34.0f + static_cast<float>(notices) * 46.0f);
}

void Hud::drawTeamPanel(UIContext& ui, GameWorld& w) {
    using Presence = GameWorld::Presence;
    Renderer& r = ui.r();
    const float pad = ui.s(12);
    const float W = std::min(ui.s(430), ui.screenW() * 0.36f);
    const float x = ui.s(20);
    float y = ui.s(18);
    const float rowH = ui.s(58);
    const Color gold{255, 210, 122, 255}, violet{201, 179, 255, 255}, dim{143, 155, 189, 255}, text{232, 238, 252, 255};
    Rect drawn{x, y, W, 0.0f};

    for (int i = 0; i < w.teamSize(); ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        const Player& p = w.playerAt(i);
        const Color c = teamColor(i);
        // Its state, in words, and the colour of the row's frame.
        std::string state;
        Color stateColor = c, frame{34, 48, 90, 255};
        switch (m.presence) {
            case Presence::Playing:
                if (p.state() == PlayerState::Dead) state = "tombe…";
                else if (p.respawning()) state = "réapparaît";
                else if (p.cursed()) state = "maudit · " + seconds1(p.curseTime());
                else state = "en jeu";
                break;
            case Presence::Down:
                stateColor = gold;
                frame = gold;
                if (!w.canRevive()) state = "à terre";
                else if (m.revive > 0.0f) state = "relevé dans " + seconds1(GameWorld::kReviveSeconds - m.revive);
                else state = "à terre · son âme attend";
                break;
            case Presence::Standby:
                stateColor = violet;
                frame = Color{166, 107, 255, 255};
                state = "déconnecté · " + std::to_string((GameWorld::kStandbyLimitTicks - m.standbyTicks + 119) / 120) + " s";
                break;
            case Presence::Gone:
                stateColor = dim;
                state = "parti";
                break;
            case Presence::Vacant:
                stateColor = dim;
                state = "place libre";
                break;
        }
        const Rect row{x, y, W, rowH};
        r.fillRoundedRect(row, ui.s(10), {7, 11, 26, 215});
        r.drawRoundedRect(row, ui.s(10), frame.withAlpha(200), ui.s(1.5f));
        const float x0 = x + pad, right = x + W - pad;
        // Line 1: its colour, its name, its state at the right (never over the name).
        const float line1 = y + ui.s(16);
        r.fillCircle({x0 + ui.s(6), line1}, ui.s(6), c);
        const float nameX = x0 + ui.s(18);
        const float inner = right - nameX;
        Fitted st = fitText(ui, state, FontFace::Bold, 15, inner * 0.55f);
        const float stateW = ui.text().measure(st.text, FontFace::Bold, ui.s(st.size)).x;
        Fitted nm = fitText(ui, teamName(i), FontFace::Bold, 19, std::max(ui.s(20), inner - stateW - ui.s(12)));
        ui.text().draw(nm.text, {nameX, line1}, teamStyle(ui, nm.size, text, FontFace::Bold, Align::Left, VAlign::Middle));
        ui.text().draw(st.text, {right, line1}, teamStyle(ui, st.size, stateColor, FontFace::Bold, Align::Right, VAlign::Middle));
        // Line 2: hearts, energy, powers (each in its own band, measured).
        const float line2 = y + ui.s(41);
        const int hearts = std::max(1, p.maxHealth());
        const float heartBand = ui.s(170);
        const float hs = std::min(ui.s(15), (heartBand + ui.s(2)) / static_cast<float>(hearts) - ui.s(2));
        const bool shown = m.presence == Presence::Playing || m.presence == Presence::Standby;
        for (int k = 0; k < hearts; ++k) {
            const Rect hr{x0 + static_cast<float>(k) * (hs + ui.s(2)), line2 - hs * 0.5f, hs, hs};
            ui.icon(kHudIcons, shown && k < p.health ? HeartFull : HeartEmpty, 16, hr, shown ? Color::white() : Color{255, 255, 255, 110});
        }
        const Rect bar{x0 + heartBand + ui.s(10), line2 - ui.s(3), ui.s(56), ui.s(6)};
        r.fillRoundedRect(bar.expanded(ui.s(1.5f)), ui.s(3), {4, 7, 18, 220});
        const float ek = std::clamp(p.energy / std::max(1.0f, p.config().maxEnergy), 0.0f, 1.0f);
        if (shown) r.fillRoundedRect({bar.x, bar.y, bar.w * ek, bar.h}, ui.s(3), {90, 240, 220, 255});
        float px = bar.right() + ui.s(10);
        const float icon = ui.s(18), gap = ui.s(3);
        for (const auto& pw : p.powers()) {
            if (px + icon > right) break;  // no room left: the rest is not drawn
            const bool ready = shown && pw->ready(p);
            ui.icon("ui/icons_powers.png", pw->def().icon, 32, {px, line2 - icon * 0.5f, icon, icon},
                    ready ? Color::white() : Color{110, 120, 140, 200});
            px += icon + gap;
        }
        drawn.h = row.bottom() - drawn.y;
        y = row.bottom() + ui.s(6);
    }

    // The team's revivals, then what waits (standby, gone, the network).
    auto line = [&](const std::string& t, Color c) {
        const float room = W - pad * 2.0f;
        const float fs = ui.fitFont(t, FontFace::Bold, 15, room, 0.8f);
        // Two lines at most: wrapped by the UI, measured to move on.
        const Vec2 used = ui.labelWrapped(t, {x + pad, y + ui.s(6)}, room, fs, c);
        const Rect box{x, y, W, used.y + ui.s(12)};
        r.drawRoundedRect(box, ui.s(8), c.withAlpha(120), ui.s(1.2f));
        drawn.h = box.bottom() - drawn.y;
        y = box.bottom() + ui.s(6);
    };
    const int used = w.revivesUsed();
    if (w.revivesUnlimited())  // 2.75 (raids): no limit while a teammate is in play; the whole group down: eliminated
        line("Réanimations illimitées (" + std::to_string(w.revivalsDone()) +
                 " faites) · tout le groupe à terre : groupe éliminé, mode spectateur",
             gold);
    else
        line("Réanimations de l'équipe : " + std::to_string(used) + (used > 1 ? " utilisées" : " utilisée") + " sur " +
                 std::to_string(GameWorld::kTeamRevives),
             gold);
    for (int i = 0; i < w.teamSize(); ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        if (m.presence == Presence::Standby)
            line(teamName(i) + " est déconnecté : intouchable, il revient près de l'équipe s'il se reconnecte dans " +
                     std::to_string((GameWorld::kStandbyLimitTicks - m.standbyTicks + 119) / 120) + " s.",
                 violet);
        else if (m.presence == Presence::Gone)
            line(teamName(i) + " est parti : l'équipe continue, la tentative sera classée incomplète.", dim);
    }
    if (!team_.waitingFor.empty()) line(team_.waitingFor, text);
    for (const auto& note : team_.deviceNotes) line(note, gold);  // 2.65: no pause for these
    teamPanel_ = drawn;
}

void Hud::drawTeamLabels(UIContext& ui, GameWorld& w, const WorldView& view) {
    using Presence = GameWorld::Presence;
    Renderer& r = ui.r();
    std::vector<Rect> taken;
    if (!teamPanel_.empty()) taken.push_back(teamPanel_);
    // A label centred on `anchor` (its bottom), moved up until it covers no other one.
    auto put = [&](const std::string& t, Vec2 anchor, Color c, float size, bool framed) {
        const float room = ui.s(340);
        Fitted f = fitText(ui, t, FontFace::Bold, size, room);
        const Vec2 sz = ui.text().measure(f.text, FontFace::Bold, ui.s(f.size));
        const float padX = framed ? ui.s(8) : ui.s(3), padY = framed ? ui.s(4) : ui.s(1);
        Rect box{anchor.x - sz.x * 0.5f - padX, anchor.y - sz.y - padY * 2.0f, sz.x + padX * 2.0f, sz.y + padY * 2.0f};
        box.x = std::clamp(box.x, ui.s(4), std::max(ui.s(4), ui.screenW() - box.w - ui.s(4)));
        for (int guard = 0; guard < 8; ++guard) {
            bool hit = false;
            for (const Rect& o : taken)
                if (o.intersects(box)) {
                    box.y = o.y - box.h - ui.s(2);
                    hit = true;
                }
            if (!hit) break;
        }
        if (box.y < ui.s(2) || box.bottom() > ui.screenH()) return;  // out of the screen: not drawn
        taken.push_back(box);
        if (framed) {
            r.fillRoundedRect(box, ui.s(6), {7, 11, 26, 215});
            r.drawRoundedRect(box, ui.s(6), c.withAlpha(200), ui.s(1.2f));
        }
        ui.text().draw(f.text, {box.center().x, box.center().y}, teamStyle(ui, f.size, c, FontFace::Bold, Align::Center, VAlign::Middle));
    };
    // The nearest ally in play to `at` (who is reviving a soul).
    auto rescuer = [&](Vec2 at) {
        int best = -1;
        float bd = 0.0f;
        for (int j = 0; j < w.teamSize(); ++j) {
            if (w.member(j).presence != Presence::Playing || !w.playerAt(j).alive()) continue;
            const Vec2 d = w.playerAt(j).feet() - at;
            const float d2 = d.x * d.x + d.y * d.y;
            if (best < 0 || d2 < bd) {
                best = j;
                bd = d2;
            }
        }
        return best;
    };
    for (int i = 0; i < w.teamSize(); ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        const Player& p = w.playerAt(i);
        const Color c = teamColor(i);
        if (m.presence == Presence::Playing && p.state() != PlayerState::Hidden) {
            put(teamName(i), view.toScreen({p.center().x, p.box.y - 4.0f}), c, 14, false);
        } else if (m.presence == Presence::Down) {
            const Vec2 at = view.toScreen(m.soul + Vec2{0.0f, -34.0f});
            std::string t;
            if (!w.canRevive()) {
                t = teamName(i) + " à terre : plus de réanimation";
            } else if (m.revive > 0.0f) {
                const int who = rescuer(m.soul);
                t = (who >= 0 ? teamName(who) + " relève " : "Réanimation de ") + teamName(i) + " : " +
                    seconds1(GameWorld::kReviveSeconds - m.revive);
            } else {
                t = teamName(i) + " à terre : restez 2 s près de son âme";
            }
            put(t, at, Color{255, 210, 122, 255}, 15, true);
        } else if (m.presence == Presence::Standby) {
            put(teamName(i) + " en veille", view.toScreen({p.center().x, p.box.y - 4.0f}), Color{201, 179, 255, 255}, 14, true);
        }
    }
}

}  // namespace cosmic
