#include "ui/screens/PassageWaitView.h"

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "ui/screens/DownedView.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::passagewait {

namespace {
const Color kFill{14, 18, 40, 228};
const Color kEdge{57, 240, 216, 210};  // the cyan of the doors' beams
const Color kText{232, 240, 255, 255};
}  // namespace

std::string line(const std::string& kind, const std::vector<Waited>& players) {
    // The kind's words translated; the names as their players wrote them (never translated).
    std::string out = kind == "door"         ? i18n::tr("Porte : en attente de")
                      : kind == "power_gate" ? i18n::tr("Barrière : en attente de")
                      : kind == "laser"      ? i18n::tr("Laser : en attente de")
                                             : i18n::tr("Passage : en attente de");
    for (size_t i = 0; i < players.size(); ++i) {
        const Waited& p = players[i];
        out += i == 0 ? " " : ", ";
        out += p.name + " (";
        if (p.down) out += i18n::tr("à terre") + ", ";
        out += std::to_string(p.metres) + " m)";
    }
    return i18n::keep(out);
}

std::vector<std::string> lines(const GameWorld& w, const std::function<std::string(int)>& nameOf, size_t maxLines) {
    std::vector<std::string> out;
    std::vector<GameWorld::Straggler> behind;
    for (const auto& e : w.entities()) {
        const PassageClosing* c = passageWaiting(*e);
        if (!c || e->removed) continue;
        w.teamThrough(c->span, c->hasInner ? &c->inner : nullptr, &behind);
        if (behind.empty()) continue;
        std::vector<Waited> players;
        for (const GameWorld::Straggler& s : behind)
            players.push_back({nameOf(s.player), static_cast<int>(std::lround(s.distance / 16.0f)), s.down});  // 16 px: 1 m
        std::string t = line(e->type(), players);
        if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(std::move(t));
        if (out.size() >= maxLines) break;
    }
    return out;
}

Layout placed(UIContext& ui, const std::vector<std::string>& lines, const std::vector<Rect>& taken) {
    Layout l;
    if (lines.empty()) return l;
    i18n::Verbatim shown(true);  // translated already (line())
    const float pad = ui.s(14), w = std::min(ui.s(640), ui.screenW() - ui.s(32)), iw = w - pad * 2.0f;
    const float h = ui.s(8) * 2.0f + static_cast<float>(lines.size()) * ui.s(kLineH);
    const Rect at = downed::place(ui, w, h, taken);
    l.panel = at;
    float y = at.y + ui.s(8);
    for (const std::string& t : lines) {
        const float f = ui.fitFont(t, FontFace::Bold, kLineSize, iw, 0.75f);
        l.sizes.push_back(f);
        l.shown.push_back(ui.text().measure(t, FontFace::Bold, ui.s(f)).x <= iw ? t : ui.elide(t, FontFace::Bold, f, iw));
        l.lineBoxes.push_back({at.x + pad, y, iw, ui.s(kLineH)});
        y += ui.s(kLineH);
    }
    return l;
}

void draw(UIContext& ui, const Layout& l) {
    if (l.panel.empty()) return;
    Renderer& r = ui.r();
    r.fillRoundedRect(l.panel, ui.s(10), kFill);
    r.drawRoundedRect(l.panel, ui.s(10), kEdge, ui.s(1.5f));
    i18n::Verbatim shown(true);
    for (size_t i = 0; i < l.shown.size(); ++i)
        ui.label(l.shown[i], l.lineBoxes[i].center(), l.sizes[i], kText, Align::Center, FontFace::Bold, VAlign::Middle);
}

}  // namespace cosmic::passagewait
