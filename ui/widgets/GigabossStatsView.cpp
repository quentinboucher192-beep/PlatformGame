#include "ui/widgets/GigabossStatsView.h"

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/raid/RaidBranch.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/HitPartsView.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::gbstats {

namespace {

const Color kGold{255, 210, 122, 255};
const Color kText{238, 240, 255, 255};
const Color kDim{185, 191, 230, 255};
const Color kGreen{124, 240, 168, 255};
const Color kArmour{170, 176, 196, 255};
const Color kRed{255, 120, 140, 255};
const Color kHeart{255, 90, 120, 255};

constexpr float kTitleH = 30.0f, kHeadingH = 26.0f, kRowH = 26.0f, kNoteLine = 18.0f, kChipH = 26.0f;

std::string num(float v, int decimals = 1) { return sheetNumber(v, decimals); }
std::string pv(int v) { return thousands(v) + " PV"; }
std::string playersLabel(int n) { return std::to_string(n) + " joueurs"; }

float textW(UIContext& ui, const std::string& t, FontFace face, float size) { return ui.text().measure(t, face, ui.s(size)).x; }

/// « Point faible × 2 », « Blindée × 0,5 »… (each a model of the dictionaries: « Weak point × 2 »).
std::string partValue(const PartRule& r) {
    switch (r.role) {
        case PartRole::Weak: return "Point faible " + partDamageLabel(r);
        case PartRole::Armour: return "Blindée " + partDamageLabel(r);
        case PartRole::Hurt: return "Blesse";
        case PartRole::Normal: break;
    }
    return "Partie normale " + partDamageLabel(r);
}

Color roleColor(PartRole r) {
    switch (r) {
        case PartRole::Weak: return kGreen;
        case PartRole::Armour: return kArmour;
        case PartRole::Hurt: return kRed;
        case PartRole::Normal: break;
    }
    return kText;
}

}  // namespace

std::string thousands(int v) {
    const bool neg = v < 0;
    std::string d = std::to_string(neg ? -v : v), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ' ';
        out += d[i];
    }
    return neg ? "-" + out : out;
}

Layout layoutStats(UIContext& ui, const raid::SheetStats& s, float x, float y, float w, Mode mode, int relics) {
    Layout L;
    float cy = y;
    const float pad = ui.s(8);
    auto title = [&](const std::string& t, Color c) {
        Item it;
        it.kind = Item::Kind::Title;
        it.label = t;
        it.r = {x, cy, w, ui.s(kTitleH)};
        it.size = ui.fitFont(t, FontFace::Bold, 16, w, 0.7f);
        it.color = c;
        cy = it.r.bottom();
        L.items.push_back(std::move(it));
    };
    auto heading = [&](const std::string& t) {
        Item it;
        it.kind = Item::Kind::Heading;
        it.label = t;
        it.r = {x, cy + ui.s(6), w, ui.s(kHeadingH)};
        it.size = ui.fitFont(t, FontFace::Bold, 15, w, 0.75f);
        it.color = kText;
        cy = it.r.bottom();
        L.items.push_back(std::move(it));
    };
    auto note = [&](const std::string& t, Color c, int maxLines = 3) {
        Item it;
        it.kind = Item::Kind::Note;
        it.lines = raidui::wrap(ui, t, FontFace::Body, 13, w, maxLines);
        if (it.lines.empty()) return;
        it.r = {x, cy, w, static_cast<float>(it.lines.size()) * ui.s(kNoteLine)};
        it.size = 13.0f;
        it.color = c;
        cy = it.r.bottom() + ui.s(2);
        L.items.push_back(std::move(it));
    };
    auto row = [&](const std::string& label, const std::string& value, Color vc, const std::string& tipTitle, std::vector<std::string> tip) {
        Item it;
        it.kind = Item::Kind::Row;
        it.label = label;
        it.value = value;
        it.color = vc;
        it.r = {x, cy, w, ui.s(kRowH)};
        // The value first (whole when it can be: up to 60 % of the row), the label in the room left.
        const float vroom = std::max(ui.s(20), w * 0.6f - pad);
        it.valueSize = ui.fitFont(value, FontFace::Bold, 14, vroom, 0.75f);
        const float vw = std::min(vroom, textW(ui, value, FontFace::Bold, it.valueSize));
        it.valueR = {it.r.right() - pad - vw, it.r.y, vw, it.r.h};
        it.labelR = {it.r.x + pad, it.r.y, std::max(0.0f, it.valueR.x - ui.s(10) - (it.r.x + pad)), it.r.h};
        it.labelSize = ui.fitFont(label, FontFace::Body, 14, std::max(ui.s(10), it.labelR.w), 0.75f);
        it.tipTitle = tipTitle;
        it.tip = std::move(tip);
        cy = it.r.bottom() + ui.s(2);
        L.items.push_back(std::move(it));
    };
    auto bars = [&](int withRelics) {
        // One row per group size: its label, its bar (of the widest health shown), its health, and without relic (grey).
        const float widest = static_cast<float>(std::max(1, sheetHealth(s, raid::SheetStats::kMaxPlayers)));
        float lw = 0.0f, vw = 0.0f, ew = 0.0f;
        for (int n = raid::SheetStats::kMinPlayers; n <= raid::SheetStats::kMaxPlayers; ++n) {
            lw = std::max(lw, textW(ui, playersLabel(n), FontFace::Body, 14));
            vw = std::max(vw, textW(ui, pv(sheetHealth(s, n, withRelics)), FontFace::Bold, 14));
            if (withRelics > 0) ew = std::max(ew, textW(ui, "(" + thousands(sheetHealth(s, n)) + ")", FontFace::Body, 12));
        }
        const float gap = ui.s(10);
        // Too narrow: the texts shrink together (never below 75 %), the bar goes last.
        float k = 1.0f;
        const float need = lw + vw + ew + gap * 3.0f + pad * 2.0f;
        if (need > w) k = std::max(0.75f, (w - gap * 3.0f - pad * 2.0f) / std::max(1.0f, lw + vw + ew));
        lw *= k;
        vw *= k;
        ew *= k;
        float barW = w - pad * 2.0f - lw - vw - ew - gap * (ew > 0.0f ? 3.0f : 2.0f);
        if (barW < ui.s(30)) barW = 0.0f;
        for (int n = raid::SheetStats::kMinPlayers; n <= raid::SheetStats::kMaxPlayers; ++n) {
            Item it;
            it.kind = Item::Kind::Bar;
            const int base = sheetHealth(s, n), hp = sheetHealth(s, n, withRelics);
            it.label = playersLabel(n);
            it.value = pv(hp);
            if (withRelics > 0) it.extra = "(" + thousands(base) + ")";
            it.r = {x, cy, w, ui.s(kRowH)};
            float cx = it.r.x + pad;
            it.labelR = {cx, it.r.y, lw, it.r.h};
            cx += lw + gap;
            if (barW > 0.0f) {
                it.barR = {cx, it.r.y + it.r.h * 0.25f, barW, it.r.h * 0.5f};
                cx += barW + gap;
            }
            it.valueR = {cx, it.r.y, vw, it.r.h};
            cx += vw + gap;
            if (ew > 0.0f) it.extraR = {cx, it.r.y, ew, it.r.h};
            it.labelSize = ui.fitFont(it.label, FontFace::Body, 14, lw, 0.75f);
            it.valueSize = ui.fitFont(it.value, FontFace::Bold, 14, vw, 0.75f);
            it.size = ew > 0.0f ? ui.fitFont(it.extra, FontFace::Body, 12, ew, 0.75f) : 12.0f;
            it.fraction = static_cast<float>(hp) / widest;
            it.fractionBase = static_cast<float>(base) / widest;
            it.color = kHeart;
            it.tipTitle = playersLabel(n);
            it.tip.push_back(thousands(static_cast<int>(std::lround(s.hp))) + " × (1 + " + num(s.hpPerPlayer, 2) + " × " + std::to_string(n - 1) + ") = " + pv(base));
            if (withRelics > 0) {
                const float keep = std::max(s.relicFloor, 1.0f - s.relicShare * static_cast<float>(withRelics));
                it.tip.push_back("Reliques : × " + num(keep, 2) + " = " + pv(hp));
            }
            it.tip.push_back("Sa vie grandit avec les places occupées quand le combat commence (jamais moins : un départ ne l'affaiblit pas).");
            cy = it.r.bottom() + ui.s(2);
            L.items.push_back(std::move(it));
        }
    };
    auto hearts = [&](float amount) { return heartsLabel(amount); };

    title("STATISTIQUES", kGold);
    if (mode == Mode::Heart) {
        heading("Vie de l'instance selon le groupe");
        note(thousands(static_cast<int>(std::lround(s.hp))) + " × (1 + " + num(s.hpPerPlayer, 2) + " × (joueurs − 1)), puis −" + num(s.relicShare * 100.0f) +
                 " % par relique (au moins " + num(s.relicFloor * 100.0f, 0) + " %)",
             kDim, 2);
        bars(relics);
        if (relics > 0) {
            const float cut = std::min(1.0f - s.relicFloor, s.relicShare * static_cast<float>(relics));
            note("Reliques : −" + num(cut * 100.0f) + " % de vie (" + std::to_string(relics) + (relics > 1 ? " reliques" : " relique") + ") · gris : sans relique", kGold, 2);
        } else {
            note("Aucune relique rapportée : sa vie entière.", kDim, 2);
        }
        heading("Vie partagée par tout le clan");
        note("Chaque instance frappe le même Cœur : sa part est retirée de la vie commune (" + thousands(raid::kHeartUnits) + " dix-millièmes).", kDim, 4);
        heading("Phases partagées");
        {
            Item it;
            it.kind = Item::Kind::Chips;
            float cx = x;
            float rowY = cy;
            for (size_t i = 0; i < s.phases.size(); ++i) {
                const raid::SheetStats::Phase& p = s.phases[i];
                const float to = i + 1 < s.phases.size() ? s.phases[i + 1].from : 0.0f;
                const std::string t = i + 1 == s.phases.size() ? std::to_string(p.number) + " · " + p.name
                                                                : std::to_string(p.number) + " · " + num(p.from * 100.0f, 0) + "–" + num(to * 100.0f, 0) + " %";
                const float cw = std::min(w, textW(ui, t, FontFace::Bold, 13) + ui.s(20));
                if (cx > x && cx + cw > x + w) {
                    cx = x;
                    rowY += ui.s(kChipH) + ui.s(6);
                }
                it.chips.push_back({cx, rowY, cw, ui.s(kChipH)});
                it.lines.push_back(t);
                it.tip.push_back("Phase " + std::to_string(p.number) + " · " + p.name + " : de " + num(p.from * 100.0f, 0) + " % à " + num(to * 100.0f, 0) +
                                 " % de la vie commune (la même pour tous les groupes).");
                cx += cw + ui.s(8);
            }
            if (!it.chips.empty()) {
                it.r = {x, cy, w, rowY + ui.s(kChipH) - cy};
                it.size = 13.0f;
                cy = it.r.bottom() + ui.s(4);
                L.items.push_back(std::move(it));
            }
        }
        row("Contact", hearts(s.contact), kRed, "Contact",
            {"Toucher son corps : " + num(s.contact) + " de dégâts, soit " + hearts(s.contact) + " (au moins un cœur, arrondi ; facteur de dégâts × 1)."});
        if (s.lastStand > 0) note("Dernier souffle avant de tomber : " + std::to_string(s.lastStand) + (s.lastStand > 1 ? " attaques à survivre." : " attaque à survivre."), kDim, 2);
        L.height = cy - y;
        return L;
    }
    note("Pour un groupe de 2 à 8 joueurs. Survolez un chiffre, ou donnez-lui le focus (flèches, croix), pour son détail.", kDim, 2);
    heading("Points de vie selon le groupe");
    note(thousands(static_cast<int>(std::lround(s.hp))) + " × (1 + " + num(s.hpPerPlayer, 2) + " × (joueurs − 1)) : sa vie grandit avec les places occupées.", kDim, 2);
    bars(0);
    if (s.relicShare > 0.0f)
        row("Reliques", "−" + num(s.relicShare * 100.0f) + " % chacune", kGold, "Reliques",
            {"Chaque relique des branches validée lui retire " + num(s.relicShare * 100.0f) + " % de sa vie (au moins " + num(s.relicFloor * 100.0f, 0) + " % gardés)."});
    heading("Phases");
    for (const raid::SheetStats::Phase& p : s.phases) {
        std::vector<std::string> tip;
        tip.push_back(p.number == 1 ? std::string("Dès le début du combat.")
                                    : "Elle commence quand sa vie tombe à " + num(p.from * 100.0f) + " % : " + pv(p.health.front()) + " à 2 joueurs, " +
                                          pv(p.health.back()) + " à 8 joueurs.");
        if (!p.states.empty()) {
            std::string st;
            for (const std::string& v : p.states) st += (st.empty() ? "" : ", ") + v;
            tip.push_back("États : " + st);
        }
        if (p.speed != 1.0f) tip.push_back("Vitesse × " + num(p.speed, 2));
        if (p.enrage > 0.0f) tip.push_back("Enrage après " + num(p.enrage, 0) + " s dans cette phase");
        row("Phase " + std::to_string(p.number) + " · " + p.name, p.number == 1 ? std::string("dès 100 %") : "à " + num(p.from * 100.0f) + " %", kGold,
            "Phase " + std::to_string(p.number) + " · " + p.name, std::move(tip));
        std::string detail;
        if (!p.health.empty()) detail = "2 joueurs : " + pv(p.health.front()) + " · 8 joueurs : " + pv(p.health.back());
        for (const std::string& v : p.states) detail += (detail.empty() ? "" : " · ") + v;
        note(detail, kDim, 2);
    }
    heading("Dégâts reçus (en cœurs)");
    row("Contact", hearts(s.contact), kRed, "Contact",
        {"Toucher son corps : " + num(s.contact) + " de dégâts, soit " + hearts(s.contact) + " (au moins un cœur, arrondi ; facteur de dégâts × 1)."});
    for (const raid::SheetStats::Blow& b : s.blows)
        row(b.name, hearts(b.damage), b.damage >= 2.0f ? kRed : kText, b.name,
            {std::string(b.mechanism ? "Coup de ses règles de raid (son antre)" : "Attaque de sa définition") + " : " + num(b.damage) + " de dégâts, soit " +
             hearts(b.damage) + "."});
    heading("Multiplicateurs par partie");
    if (s.parts.empty()) note("Ses parties ne sont pas encore décrites : tout son corps compte × 1.", kDim, 2);
    for (const PartRule& r : s.parts) {
        const bool hurt = r.role == PartRole::Hurt;
        row(partNameLabel(r.name), partValue(r), roleColor(r.role), partNameLabel(r.name),
            {hurt ? "Elle ne prend aucun coup : la toucher blesse (" + hearts(s.contact) + ")."
                  : "Les coups portés sur cette partie comptent " + partDamageLabel(r) + " (" + partRoleLabel(r.role) + ")."});
    }
    if (!s.parts.empty() && !s.byParts) note("Son combat se joue encore sur sa boîte : ces parties sont montrées, elles ne changent pas encore vos coups.", kDim, 3);
    row("À nu", "× " + num(s.exposedMult, 2) + " pendant " + num(s.exposed) + " s", kGreen, "À nu",
        {"Son mécanisme déjoué, il est à nu : les coups comptent × " + num(s.exposedMult, 2) + " pendant " + num(s.exposed) + " s (règles de son antre)."});
    if (s.weakShare > 0.0f)
        row("Point faible brisé", "−" + num(s.weakShare * 100.0f) + " % de sa vie", kGreen, "Point faible brisé",
            {"Chaque point faible brisé lui retire " + num(s.weakShare * 100.0f) + " % de sa vie, et compte pour qui le brise."});
    heading("Autres caractéristiques");
    row("Vitesse", num(s.speed, 0) + " px/s" + (s.chase != 1.0f ? " · × " + num(s.chase, 2) + " en poursuite" : std::string()), kText, "Vitesse",
        {"Sa vitesse de déplacement (pixels par seconde), × " + num(s.chase, 2) + " quand il poursuit un joueur ; chaque phase peut l'accélérer."});
    row("Taille", num(s.size.x, 0) + " × " + num(s.size.y, 0) + " px", kText, "Taille",
        {"Sa boîte : " + num(s.size.x, 0) + " × " + num(s.size.y, 0) + " pixels ; son dessin est agrandi × " + num(s.scale, 2) + "."});
    // Its time limit: the whole fight's, then each phase's (it enrages past it).
    std::string enrage = s.enrage > 0.0f ? "après " + num(s.enrage, 0) + " s" : std::string();
    for (const raid::SheetStats::Phase& p : s.phases)
        if (p.enrage > 0.0f) enrage += (enrage.empty() ? "" : " · ") + ("phase " + std::to_string(p.number) + " : " + num(p.enrage, 0) + " s");
    const bool limited = !enrage.empty();
    row("Limite de temps (enrage)", limited ? enrage : std::string("aucune"), limited ? kRed : kDim, "Limite de temps",
        {limited ? "Passé ce temps de combat (dans la phase dite), il enrage : plus rapide, des recharges plus courtes."
                 : "Aucune : il n'enrage pas avec le temps ; seul le groupe change son rythme."});
    row("Rythme selon le groupe", "÷ (1 + " + num(s.pressurePerPlayer, 2) + " × (joueurs − 1))", kText, "Rythme selon le groupe",
        {"Ses temps de recharge raccourcissent avec les joueurs en jeu : ÷ " + num(1.0f + s.pressurePerPlayer * 7.0f, 2) + " à 8 joueurs."});
    if (s.weakHpPerPlayer > 0.0f)
        row("Ses cibles selon le groupe", "× (1 + " + num(s.weakHpPerPlayer, 2) + " × (joueurs − 1))", kText, "Ses cibles selon le groupe",
            {"La vie de ses points faibles et des cibles de ses mécanismes grandit aussi : × " + num(1.0f + s.weakHpPerPlayer * 7.0f, 2) + " à 8 joueurs."});
    if (s.lastStand > 0)
        row("Dernier souffle", std::to_string(s.lastStand) + (s.lastStand > 1 ? " attaques à survivre" : " attaque à survivre"), kGold, "Dernier souffle",
            {"À 0 PV, il ne peut plus être blessé : ses dernières attaques, puis il tombe."});
    L.height = cy - y;
    return L;
}

void drawStats(UIContext& ui, const Layout& L, const std::string& id, Tip& tip) {
    Renderer& r = ui.r();
    for (size_t i = 0; i < L.items.size(); ++i) {
        const Item& it = L.items[i];
        const std::string name = id + std::to_string(i);
        bool lit = false;
        if ((it.kind == Item::Kind::Row || it.kind == Item::Kind::Bar) && !it.tip.empty()) {
            bool hovered = false;
            ui.hotspot(name, it.r, &hovered);
            lit = hovered || ui.focused(name);
            if (lit) tip = {it.tipTitle, it.tip, it.r, true};
        }
        switch (it.kind) {
            case Item::Kind::Title:
            case Item::Kind::Heading:
                ui.label(ui.elide(it.label, FontFace::Bold, it.size, it.r.w), {it.r.x, it.r.center().y}, it.size, it.color, Align::Left, FontFace::Bold,
                         VAlign::Middle);
                break;
            case Item::Kind::Note: {
                i18n::Verbatim shown(true);  // wrapped (translated) already
                float y = it.r.y;
                for (const std::string& l : it.lines) {
                    ui.label(l, {it.r.x, y + ui.s(kNoteLine) * 0.5f}, it.size, it.color, Align::Left, FontFace::Body, VAlign::Middle);
                    y += ui.s(kNoteLine);
                }
                break;
            }
            case Item::Kind::Row:
                r.fillRoundedRect(it.r, ui.s(6), lit ? Color{40, 52, 104, 235} : Color{16, 24, 48, 190});
                if (lit) r.drawRoundedRect(it.r, ui.s(6), kGold.withAlpha(200), ui.s(1.2f));
                if (it.labelR.w > ui.s(8))
                    ui.label(ui.elide(it.label, FontFace::Body, it.labelSize, it.labelR.w), {it.labelR.x, it.r.center().y}, it.labelSize, kDim, Align::Left,
                             FontFace::Body, VAlign::Middle);
                ui.label(ui.elide(it.value, FontFace::Bold, it.valueSize, it.valueR.w), {it.valueR.right(), it.r.center().y}, it.valueSize, it.color,
                         Align::Right, FontFace::Bold, VAlign::Middle);
                break;
            case Item::Kind::Bar: {
                if (lit) {
                    r.fillRoundedRect(it.r, ui.s(6), Color{40, 52, 104, 235});
                    r.drawRoundedRect(it.r, ui.s(6), kGold.withAlpha(200), ui.s(1.2f));
                }
                ui.label(ui.elide(it.label, FontFace::Body, it.labelSize, it.labelR.w), {it.labelR.x, it.r.center().y}, it.labelSize, kText, Align::Left,
                         FontFace::Body, VAlign::Middle);
                if (it.barR.w > 0.0f) {
                    const float rad = it.barR.h * 0.5f;
                    r.fillRoundedRect(it.barR, rad, Color{34, 44, 82, 255});
                    if (it.fractionBase > it.fraction)
                        r.fillRoundedRect({it.barR.x, it.barR.y, std::max(it.barR.h, it.barR.w * std::clamp(it.fractionBase, 0.0f, 1.0f)), it.barR.h}, rad,
                                          Color{110, 64, 86, 255});
                    r.fillRoundedRect({it.barR.x, it.barR.y, std::max(it.barR.h, it.barR.w * std::clamp(it.fraction, 0.0f, 1.0f)), it.barR.h}, rad, it.color);
                }
                ui.label(ui.elide(it.value, FontFace::Bold, it.valueSize, it.valueR.w), {it.valueR.x, it.r.center().y}, it.valueSize, kText, Align::Left,
                         FontFace::Bold, VAlign::Middle);
                if (!it.extra.empty() && it.extraR.w > 0.0f)
                    ui.label(ui.elide(it.extra, FontFace::Body, it.size, it.extraR.w), {it.extraR.x, it.r.center().y}, it.size, kDim, Align::Left,
                             FontFace::Body, VAlign::Middle);
                break;
            }
            case Item::Kind::Chips: {
                static const Color kPhase[4] = {{120, 200, 255, 255}, {200, 160, 255, 255}, {255, 160, 110, 255}, {255, 90, 120, 255}};
                for (size_t k = 0; k < it.chips.size(); ++k) {
                    const Rect& c = it.chips[k];
                    const std::string cname = name + "c" + std::to_string(k);
                    bool hovered = false;
                    ui.hotspot(cname, c, &hovered);
                    const bool on = hovered || ui.focused(cname);
                    if (on && k < it.tip.size()) tip = {it.lines[k], {it.tip[k]}, c, true};
                    const Color col = kPhase[std::min<size_t>(k, 3)];
                    r.fillRoundedRect(c, ui.s(8), on ? Color{48, 60, 116, 245} : Color{30, 40, 76, 240});
                    r.drawRoundedRect(c, ui.s(8), col, ui.s(on ? 2.0f : 1.4f));
                    const float fs = ui.fitFont(it.lines[k], FontFace::Bold, it.size, c.w - ui.s(12), 0.75f);
                    ui.label(ui.elide(it.lines[k], FontFace::Bold, fs, c.w - ui.s(12)), c.center(), fs, kText, Align::Center, FontFace::Bold, VAlign::Middle);
                }
                break;
            }
        }
    }
}

Rect tipBox(UIContext& ui, const Tip& tip, std::vector<std::vector<std::string>>* wrapped) {
    const float W = ui.screenW(), H = ui.screenH(), margin = ui.s(8);
    const float w = std::min(ui.s(380), W - 2.0f * margin), pad = ui.s(12), inner = w - 2.0f * pad;
    float h = pad + ui.s(26);
    std::vector<std::vector<std::string>> lines;
    for (const std::string& l : tip.lines) {
        lines.push_back(raidui::wrap(ui, l, FontFace::Body, 13, inner, 4));
        h += static_cast<float>(lines.back().size()) * ui.s(18) + ui.s(4);
    }
    h += pad;
    // Beside its anchor (at its right, else at its left, else under it), inside the screen.
    float x = tip.anchor.right() + ui.s(12);
    if (x + w > W - margin) x = tip.anchor.x - ui.s(12) - w;
    if (x < margin) x = std::clamp(tip.anchor.x, margin, std::max(margin, W - margin - w));
    float y = std::clamp(tip.anchor.y, margin, std::max(margin, H - margin - h));
    if (x < tip.anchor.right() && x + w > tip.anchor.x) y = std::clamp(tip.anchor.bottom() + ui.s(8), margin, std::max(margin, H - margin - h));
    if (wrapped) *wrapped = std::move(lines);
    return {x, y, w, h};
}

void drawTip(UIContext& ui, Tip& tip) {
    if (!tip.on) return;
    std::vector<std::vector<std::string>> lines;
    const Rect box = tipBox(ui, tip, &lines);
    Renderer& r = ui.r();
    r.fillRoundedRect(box.expanded(ui.s(1)), ui.s(11), Color{2, 4, 10, 200});
    r.fillRoundedRect(box, ui.s(10), Color{12, 16, 38, 250});
    r.drawRoundedRect(box, ui.s(10), kGold, std::max(1.0f, ui.s(1.4f)));
    const float pad = ui.s(12), inner = box.w - 2.0f * pad;
    float y = box.y + pad;
    raidui::fit(ui, tip.title, {box.x + pad, y + ui.s(13)}, 15, inner, kGold, FontFace::Bold);
    y += ui.s(26);
    for (const auto& ls : lines) {
        y += raidui::drawLines(ui, ls, {box.x + pad, y}, 13, ui.s(18), kText) + ui.s(4);
    }
    tip.on = false;
}

}  // namespace cosmic::gbstats
