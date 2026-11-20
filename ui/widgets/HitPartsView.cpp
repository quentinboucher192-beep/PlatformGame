#include "ui/widgets/HitPartsView.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/input/InputManager.h"
#include "engine/input/InputTypes.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/online/LevelFingerprint.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

using namespace raidui;

float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

std::string capitalized(const std::string& s) {
    // The parts are French words: their first letter as a capital (« tête » → « Tête », « épaule » → « Épaule »:
    // a Latin-1 letter in UTF-8, C3 A0…BE, has its capital 0x20 lower).
    std::string out = s;
    if (out.empty()) return out;
    const auto b0 = static_cast<unsigned char>(out[0]);
    if (b0 >= 'a' && b0 <= 'z') {
        out[0] = static_cast<char>(b0 - 'a' + 'A');
    } else if (b0 == 0xC3 && out.size() > 1) {
        const auto b1 = static_cast<unsigned char>(out[1]);
        if (b1 >= 0xA0 && b1 <= 0xBE && b1 != 0xB7) out[1] = static_cast<char>(b1 - 0x20);
    } else if (b0 == 0xC5 && out.size() > 1 && static_cast<unsigned char>(out[1]) == 0x93) {
        out[1] = static_cast<char>(0x92);  // 2.76.2: « œil » → « Œil »
    }
    return out;
}

/// Twice the area of a convex polygon.
float area2(const std::vector<Vec2>& p) {
    float a = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2 u = p[i], v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return std::abs(a);
}

/// « × 1 », « × 2,5 »: the zoom as the bar shows it (French decimal comma).
std::string zoomLabel(float z) {
    const int t = static_cast<int>(std::lround(z * 10.0f));
    std::string s = "× " + std::to_string(t / 10);
    if (t % 10) s += "," + std::to_string(t % 10);
    return s;
}

/// The hearts a blow of the engine takes (Player::hurt: rounded, at least one), at the level's factor × 1.
std::string strikeHearts(float amount) {
    const int n = std::max(1, static_cast<int>(std::lround(amount)));
    return std::to_string(n) + (n > 1 ? " cœurs" : " cœur");
}

}  // namespace

Color partRoleColor(PartRole r) {
    switch (r) {
        case PartRole::Weak: return Color{124, 240, 168, 255};
        case PartRole::Armour: return Color{170, 176, 196, 255};
        case PartRole::Hurt: return Color{255, 84, 84, 255};
        case PartRole::Normal: break;
    }
    return Color{110, 200, 255, 255};
}

Color partColor(const PartRules& rules, const std::string& name) { return partRoleColor(rules.ruleOr(name).role); }

std::string partNameLabel(const std::string& name) { return capitalized(name); }

std::vector<std::string> hurtDuringNames(const PartRule& rule, const EnemyDefinition& d) {
    std::vector<std::string> out;
    for (const std::string& id : rule.hurtDuring)
        for (const AttackDef& a : d.attacks)
            if (a.id == id && !a.name.empty() && std::find(out.begin(), out.end(), a.name) == out.end()) out.push_back(a.name);
    return out;
}

std::string hurtDuringLabel(const PartRule& rule, const EnemyDefinition& d) {
    if (rule.role == PartRole::Hurt) return {};
    const std::vector<std::string> names = hurtDuringNames(rule, d);
    if (names.empty()) return {};
    // « · » between the names: each one translated by itself when shown (I18n: the parts of a text).
    std::string list;
    for (const std::string& n : names) list += (list.empty() ? "" : " · ") + n;
    return "blesse aussi pendant : " + list;
}

bool partIsPlural(const std::string& name) {
    const std::string first = name.substr(0, name.find(' '));
    if (first == "bras" || first == "avant-bras" || first == "corps") return false;  // the same in the singular
    return !first.empty() && (first.back() == 's' || first.back() == 'x');
}

std::string partHurtsAlsoLine(const PartRule& rule, const EnemyDefinition& d) {
    if (rule.role == PartRole::Hurt) return {};
    const std::vector<std::string> names = hurtDuringNames(rule, d);
    if (names.empty()) return {};
    std::string list;
    for (const std::string& n : names) list += (list.empty() ? "" : " · ") + n;
    // The verb agrees with the part (« Jointures blessent », « Tête blesse »).
    return partNameLabel(rule.name) + (partIsPlural(rule.name) ? " blessent aussi pendant : " : " blesse aussi pendant : ") + list;
}

// ------------------------------------------------------------------ what a part does

PartFacts partFacts(const std::string& part, const PartRules& rules, const EnemyDefinition& d) {
    PartFacts f;
    f.part = part;
    const PartRule rule = rules.ruleOr(part);
    f.role = rule.role;
    f.fromVersion = rules.fromVersion;
    f.byParts = rules.fromVersion > 0 && rules.fromVersion <= kSimulationVersion;
    f.dealt = rule.role == PartRole::Hurt ? 0.0f : std::max(0.0f, rule.damage);
    f.contact = std::max(0.0f, d.contactDamage);
    // As the engine strikes (Enemy::performAttackActive, rules of 17): during an attack with damage, its rush
    // (lunge) whatever its damage, a part that hurts and the parts of its `hurtDuring` strike with the attack's damage.
    auto add = [&](const AttackDef& a) {
        if (a.name.empty()) return;
        for (const PartFacts::During& x : f.during)
            if (x.attack == a.name && x.damage == a.damage) return;
        f.during.push_back({a.name, a.damage, a.type == AttackType::Lunge || a.damage > 0.0f});
    };
    for (const AttackDef& a : d.attacks) {
        if (rule.role == PartRole::Hurt) {
            if (a.type == AttackType::Lunge || a.damage > 0.0f) add(a);
        } else if (std::find(rule.hurtDuring.begin(), rule.hurtDuring.end(), a.id) != rule.hurtDuring.end()) {
            add(a);
        }
    }
    return f;
}

std::string heartsLabel(float amount) { return amount <= 0.0f ? std::string("aucun") : strikeHearts(amount); }

PartTip partTip(const PartFacts& f, const std::string& context) {
    PartTip t;
    t.title = partNameLabel(f.part);
    t.role = f.role;
    t.byParts = f.byParts;
    if (!context.empty()) t.lines.push_back(context);
    if (!f.byParts) {
        // Honestly: its sheet shows its parts, its fight is played on its box.
        t.lines.push_back(f.fromVersion > kSimulationVersion
                              ? "Il se battra par ses parties avec une prochaine version : son combat se joue encore sur sa boîte."
                              : "Son combat se joue encore sur sa boîte : cette partie est montrée, elle ne change ni vos coups ni son contact.");
        t.lines.push_back("Dégâts infligés au boss : × 1 (sur toute sa boîte)");
        t.lines.push_back("Dégâts reçus au contact : " + heartsLabel(f.contact) + " (au contact de sa boîte)");
        return t;
    }
    t.lines.push_back(std::string("Rôle : ") + partRoleLabel(f.role));
    PartRule r;
    r.role = f.role;
    r.damage = f.dealt;
    t.lines.push_back("Dégâts infligés au boss : " + (f.role == PartRole::Hurt || f.dealt <= 0.0f ? std::string("aucun") : partDamageLabel(r)));
    t.lines.push_back("Dégâts reçus au contact : " + heartsLabel(f.contact));
    auto value = [](const PartFacts::During& x) { return x.strikes ? strikeHearts(x.damage) : std::string("aucun"); };
    if (f.during.size() <= 8) {
        for (const PartFacts::During& x : f.during) t.lines.push_back("Pendant " + x.attack + " : " + value(x));
    } else {
        // Many attacks (a part that hurts during each of them): one line per damage, the attacks named.
        std::vector<std::pair<std::string, std::string>> groups;  // value → names
        for (const PartFacts::During& x : f.during) {
            const std::string v = value(x);
            auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == v; });
            if (it == groups.end()) groups.push_back({v, x.attack});
            else it->second += " · " + x.attack;
        }
        for (const auto& [v, names] : groups) t.lines.push_back("Pendant " + names + " : " + v);
    }
    t.lines.push_back("En cœurs perdus, pour un niveau aux dégâts × 1.");
    return t;
}

// ------------------------------------------------------------------ patterns

namespace {

/// The chord of the line {p : n·p = c} (n of length 1) through a convex polygon: false when it misses it.
bool chord(const std::vector<Vec2>& poly, Vec2 n, float c, Vec2& a, Vec2& b) {
    const Vec2 d{-n.y, n.x};
    bool any = false;
    float lo = 0.0f, hi = 0.0f;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 p = poly[i], q = poly[(i + 1) % poly.size()];
        const float dp = p.x * n.x + p.y * n.y - c, dq = q.x * n.x + q.y * n.y - c;
        if ((dp > 0.0f && dq > 0.0f) || (dp < 0.0f && dq < 0.0f) || dp == dq) continue;
        const float t = dp / (dp - dq);
        const Vec2 x{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t};
        const float s = x.x * d.x + x.y * d.y;
        lo = any ? std::min(lo, s) : s;
        hi = any ? std::max(hi, s) : s;
        any = true;
    }
    if (!any || hi - lo < 1.0f) return false;
    a = {n.x * c + d.x * lo, n.y * c + d.y * lo};
    b = {n.x * c + d.x * hi, n.y * c + d.y * hi};
    return true;
}

/// Parallel lines across a convex polygon, anchored to the screen (two pieces of a part line up).
void hatch(Renderer& r, const std::vector<Vec2>& poly, Vec2 n, float spacing, Color c, float width, float unit) {
    float lo = 0.0f, hi = 0.0f;
    for (size_t i = 0; i < poly.size(); ++i) {
        const float s = poly[i].x * n.x + poly[i].y * n.y;
        lo = i ? std::min(lo, s) : s;
        hi = i ? std::max(hi, s) : s;
    }
    // Each line over a dark one, thin enough that the sprite still shows between them.
    const Color under{8, 10, 18, 130};
    for (float k = std::ceil(lo / spacing) * spacing; k <= hi; k += spacing) {
        Vec2 a, b;
        if (!chord(poly, n, k, a, b)) continue;
        r.drawLine(a, b, under, width + unit);
        r.drawLine(a, b, c, width);
    }
}

}  // namespace

void drawPartPattern(Renderer& r, const std::vector<Vec2>& poly, PartRole role, float unit) {
    if (poly.size() < 3) return;
    unit = std::max(0.75f, unit);
    const float sp = std::max(6.0f, 8.0f * unit), thin = std::max(1.0f, unit);
    const float h = 0.70710678f;
    const Color c = partRoleColor(role).withAlpha(220);
    switch (role) {
        case PartRole::Weak:  // a cross-hatch: the target
            hatch(r, poly, {h, h}, sp * 1.4f, c, thin, unit);
            hatch(r, poly, {h, -h}, sp * 1.4f, c, thin, unit);
            break;
        case PartRole::Normal: hatch(r, poly, {1.0f, 0.0f}, sp * 1.6f, c, thin, unit); break;  // upright stripes
        case PartRole::Armour: hatch(r, poly, {0.0f, 1.0f}, sp * 0.9f, c, thin, unit); break;  // plates, close together
        case PartRole::Hurt: hatch(r, poly, {h, -h}, sp * 1.6f, c, thin * 2.0f, unit); break;  // thick warning stripes
    }
}

void drawPartOutline(Renderer& r, const std::vector<Vec2>& poly, Color c, float width, float unit) {
    for (size_t i = 0; i < poly.size(); ++i) r.drawLine(poly[i], poly[(i + 1) % poly.size()], Color{8, 10, 18, 200}, width + 2.0f * std::max(0.75f, unit));
    for (size_t i = 0; i < poly.size(); ++i) r.drawLine(poly[i], poly[(i + 1) % poly.size()], c, width);
}

void drawPartSwatch(Renderer& r, const Rect& box, PartRole role, float unit) {
    const std::vector<Vec2> sq = {{box.x, box.y}, {box.right(), box.y}, {box.right(), box.bottom()}, {box.x, box.bottom()}};
    r.fillConvexPolygon(sq, partRoleColor(role).withAlpha(120));
    drawPartPattern(r, sq, role, unit);
    drawPartOutline(r, sq, partRoleColor(role), std::max(1.5f, 1.5f * unit), unit);
}

void drawHitPieces(Renderer& r, float lineWidth, const HitPartsSheet& sheet, const std::vector<HitPartPiece>& pieces, const PartRules& rules,
                   const std::function<Vec2(Vec2)>& toScreen, uint8_t fill, const std::string& highlight) {
    // 2.76.2: each part its role's colour, its pattern and a thick outline over a dark one, so that it reads on a
    // sprite of any colour (grey armour on a grey leg); the part chosen (or under the mouse) outlined in white.
    const float unit = lineWidth * 0.5f, width = lineWidth * 1.5f;
    std::vector<Vec2> pts;
    for (int pass = 0; pass < 2; ++pass)
        for (const HitPartPiece& p : pieces) {
            if (p.part < 0 || p.part >= static_cast<int>(sheet.parts.size()) || p.pts.size() < 3) continue;
            const std::string& name = sheet.parts[static_cast<size_t>(p.part)];
            const bool chosen = !highlight.empty() && name == highlight;
            if ((pass == 1) != chosen) continue;  // the chosen part over the others
            const PartRole role = rules.ruleOr(name).role;
            const Color c = partRoleColor(role);
            pts.clear();
            for (const Vec2& v : p.pts) pts.push_back(toScreen(v));
            r.fillConvexPolygon(pts, c.withAlpha(chosen ? static_cast<uint8_t>(std::min(255, fill + 50)) : fill));
            drawPartPattern(r, pts, role, unit);
            drawPartOutline(r, pts, chosen ? Color::white() : c, chosen ? width * 1.5f : width, unit);
        }
}

std::string poseLabel(const std::string& clip) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"idle", "Repos"},       {"move", "Vol"},          {"walk", "Marche"},       {"run", "Course"},        {"telegraph", "Élan"},
        {"attack", "Attaque"},   {"special", "Attaque spéciale"}, {"summon", "Invocation"}, {"roar", "Cri"},     {"hurt", "Touché"},
        {"crouch", "Accroupi"},  {"leap", "Saut"},         {"fall", "Chute"},        {"land", "Atterrissage"}, {"quake", "Séisme"},
        {"curl", "Repli"},       {"roll", "Roulade"},      {"uncurl", "Dépli"},      {"dizzy", "Étourdi"},     {"death", "Mort"},
        {"barrage", "Barrage"},  {"beam", "Rayon"},        {"bite", "Morsure"},      {"breath", "Souffle"},    {"charge", "Charge"},
        {"dive", "Plongeon"},    {"laser", "Laser"},       {"meteor", "Météores"},   {"slam", "Frappe au sol"}, {"spores", "Spores"},
        {"vines", "Lianes"},     {"volley", "Salve"},      {"teleport_in", "Apparition"}, {"teleport_out", "Disparition"},
    };
    for (const auto& [id, name] : kNames)
        if (clip == id) return name;
    // « telegraph_<x> »: the wind-up of <x>.
    if (clip.rfind("telegraph_", 0) == 0) {
        const std::string of = clip.substr(10);
        for (const auto& [id, name] : kNames)
            if (of == id) return std::string("Élan · ") + name;
    }
    return clip;
}

std::vector<std::string> poseClips(const std::vector<std::string>& clips) {
    static const char* kOrder[] = {"idle", "move", "walk", "run", "telegraph", "attack", "telegraph_special", "special", "summon", "roar",
                                   "crouch", "leap", "fall", "land", "quake", "curl", "roll", "uncurl", "dizzy", "hurt"};
    std::vector<std::string> out;
    for (const char* id : kOrder)
        if (std::find(clips.begin(), clips.end(), id) != clips.end()) out.push_back(id);
    for (const std::string& c : clips)
        if (c != "death" && std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    return out;
}

// ------------------------------------------------------------------ the legend

std::vector<HitLegendLine> partsLegend(const HitPartsSheet* sheet, const PartRules& rules, const EnemyDefinition& d, const bool* extraRoles, bool title) {
    std::vector<HitLegendLine> out;
    const bool byParts = rules.fromVersion > 0 && rules.fromVersion <= kSimulationVersion;
    auto line = [](std::string text, Color c, int role, FontFace face, float size, bool sub, float gap) {
        HitLegendLine l;
        l.text = std::move(text);
        l.color = c;
        l.role = role;
        l.face = face;
        l.size = size;
        l.sub = sub;
        l.gapBefore = gap;
        return l;
    };
    if (title) out.push_back(line(sheet ? "Ses parties" : "Pas encore de parties", kGold, -1, FontFace::Bold, 15.0f, false, 0.0f));
    if (!sheet) {
        out.push_back(line("Son combat se joue sur sa boîte.", kDim, -1, FontFace::Body, 12.5f, false, 4.0f));
        return out;
    }
    // One entry per role: its colour and its pattern, its multiplier when its parts share one, its parts named under it.
    for (PartRole role : {PartRole::Weak, PartRole::Normal, PartRole::Armour, PartRole::Hurt}) {
        std::vector<std::string> names, mults;
        for (const std::string& p : sheet->parts) {
            const PartRule r = rules.ruleOr(p);
            if (r.role != role) continue;
            names.push_back(partNameLabel(p));
            mults.push_back(partDamageLabel(r));
        }
        const bool extra = extraRoles && extraRoles[static_cast<int>(role)];
        if (names.empty() && !extra) continue;
        bool same = true;
        for (const std::string& m : mults) same = same && m == mults.front();
        std::string head = partRoleLabel(role);
        const bool mult = byParts && role != PartRole::Hurt;
        if (mult && same && !mults.empty()) head += " · " + mults.front();  // « · »: each piece translated
        out.push_back(line(head, kText, static_cast<int>(role), FontFace::Bold, 13.0f, false, out.empty() ? 0.0f : 6.0f));
        if (!names.empty()) {
            std::string list;
            for (size_t i = 0; i < names.size(); ++i) list += (i ? " · " : "") + names[i] + (mult && !same ? " " + mults[i] : std::string());
            out.push_back(line(list, kDim, -1, FontFace::Body, 12.5f, true, 0.0f));
        }
    }
    if (byParts)
        for (const std::string& p : sheet->parts) {
            const PartRule r = rules.ruleOr(p);
            if (const std::string also = partHurtsAlsoLine(r, d); !also.empty())
                out.push_back(line(also, kPeach, static_cast<int>(r.role), FontFace::Body, 12.5f, false, 6.0f));
        }
    // How its fight uses them: the sentence whole (wrapped, never shortened).
    out.push_back(line(byParts ? "Combat par parties : vos coups touchent la partie visée (point faible d'abord). Tout le corps blesse au contact ; les "
                                 "parties rouges blessent aussi pendant leurs attaques."
                               : "Parties : affichage ; son combat se joue sur sa boîte.",
                       byParts ? kGreen : kDim, -1, FontFace::Body, 12.5f, false, 8.0f));
    return out;
}

void drawHitLegend(UIContext& ui, const std::string& id, const Rect& box, const std::vector<HitLegendLine>& lines, HitStageLayout* L) {
    Renderer& r = ui.r();
    r.fillRoundedRect(box, ui.s(10), Color{18, 24, 52, 240});
    const float pad = ui.s(12), sw = ui.s(16), sg = ui.s(8), lane = ui.s(12);
    const Rect view{box.x + ui.s(2), box.y + ui.s(4), box.w - ui.s(4), std::max(1.0f, box.h - ui.s(8))};
    const float x0 = box.x + pad, right = box.right() - pad - lane;  // the scroll bar's lane kept free
    struct Placed {
        float y, h, tx, tw, px, th;
        bool swatch;
    };
    std::vector<Placed> placed;
    float y = pad - ui.s(4);
    for (const HitLegendLine& l : lines) {
        y += ui.s(l.gapBefore);
        const bool swatch = l.role >= 0 || l.swatch.a > 0;
        const float tx = swatch || l.sub ? x0 + sw + sg : x0;
        const float tw = std::max(ui.s(40), right - tx);
        const float px = ui.s(fz(ui, l.size));
        const Vec2 m = ui.text().measureWrapped(l.text, l.face, px, tw);
        const float h = std::max(swatch ? sw + ui.s(2) : 0.0f, m.y);
        placed.push_back({y, h, tx, tw, px, m.y, swatch});
        y += h + ui.s(4);
    }
    const float content = y + pad - ui.s(4);
    const bool scrolls = content > view.h + 0.5f;
    // Focused (keys, gamepad), ↑ ↓ scroll it; at its end the focus goes on.
    const std::string focusId = id + "/focus";
    bool focused = false;
    if (scrolls) {
        ui.hotspot(focusId, box);
        focused = ui.focused(focusId);
        if (ui.mouseOver(box)) ui.claimWheel(view);
    }
    ui.beginScroll(id, view, content);
    const float off = ui.scrollOffset();
    if (focused && ui.acceptsInput()) {
        InputManager& in = ui.input();
        const float maxOff = std::max(0.0f, content - view.h);
        if (in.menuPressed(Action::MenuDown) && off < maxOff - 0.5f) {
            ui.setScroll(id, off + ui.s(48));
            ui.consumeNavigation(false, true);
        } else if (in.menuPressed(Action::MenuUp) && off > 0.5f) {
            ui.setScroll(id, off - ui.s(48));
            ui.consumeNavigation(false, true);
        }
    }
    if (L) {
        L->legend = box;
        L->legendContent = content;
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        const HitLegendLine& l = lines[i];
        const Placed& p = placed[i];
        const float ly = view.y + p.y - off;
        if (ly + p.h >= view.y && ly <= view.bottom()) {
            if (l.role >= 0) drawPartSwatch(r, {x0, ly + ui.s(1), sw, sw}, static_cast<PartRole>(l.role), ui.s(1.0f));
            else if (l.swatch.a > 0) r.fillRect({x0, ly + ui.s(1), sw, sw}, l.swatch);
            TextStyle st;
            st.face = l.face;
            st.size = p.px;
            st.color = l.color;
            ui.text().drawWrapped(l.text, {p.tx, ly}, p.tw, st);
        }
        if (L) {
            L->texts.push_back({l.text, {p.tx, ly, p.tw, p.th}, p.px, l.face});
            L->legendBoxes.push_back({p.swatch ? x0 : p.tx, ly, p.tx + p.tw - (p.swatch ? x0 : p.tx), p.h});
        }
    }
    ui.endScroll();
    if (focused) r.drawRoundedRect(box, ui.s(10), ui.theme().accent, std::max(1.0f, ui.s(2.0f)));
}

// ------------------------------------------------------------------ the stage

namespace {

HitStageLayout gLast;

struct CaptureRequest {
    bool pending = false, frameDone = false;
    float zoom = 1.0f;
    std::string part;
    int frame = 0;
};
CaptureRequest gCapture;

}  // namespace

const HitStageLayout& HitStage::last() { return gLast; }

void HitStage::requestCapture(float zoom, const std::string& part, int frame) {
    gCapture = CaptureRequest{};
    gCapture.pending = true;
    gCapture.zoom = zoom;
    gCapture.part = part;
    gCapture.frame = frame;
}

void HitStage::advance(float dt) {
    if (!paused_) t_ += std::clamp(dt, 0.0f, 0.1f);
    layout_ = HitStageLayout{};  // a new frame of the diagram
    drawn_ = true;
    tipSet_ = false;
}

int HitStage::frame(const std::vector<float>& durations, float rest) {
    durations_ = durations;
    rest_ = std::max(0.0f, rest);
    frames_ = static_cast<int>(durations.size());
    if (gCapture.pending && !gCapture.frameDone) {
        gCapture.frameDone = true;
        if (gCapture.frame > 0) {
            paused_ = true;
            frame_ = gCapture.frame - 1;
        }
    }
    if (frames_ == 0) {
        frame_ = 0;
    } else if (paused_) {
        frame_ = std::clamp(frame_, 0, frames_ - 1);
    } else {
        float total = rest_;
        for (float d : durations_) total += std::max(0.0f, d);
        float tc = std::fmod(t_, std::max(0.05f, total));
        int f = 0;
        while (f + 1 < frames_ && tc >= durations_[static_cast<size_t>(f)]) tc -= durations_[static_cast<size_t>(f++)];
        frame_ = f;
    }
    layout_.paused = paused_;
    layout_.frame = frame_;
    layout_.frames = frames_;
    return frame_;
}

void HitStage::restart() {
    t_ = 0.0f;
    frame_ = 0;
}

void HitStage::reset() {
    restart();
    paused_ = false;
    recenter();
    chosen_.clear();
    shown_.clear();
    keyTip_ = false;
}

void HitStage::setPaused(bool paused) {
    if (paused == paused_) return;
    paused_ = paused;
    if (!paused_) {
        // Playing again from the frame it stopped on.
        float t = 0.0f;
        for (int i = 0; i < frame_ && i < static_cast<int>(durations_.size()); ++i) t += durations_[static_cast<size_t>(i)];
        t_ = t;
    }
}

void HitStage::togglePause() { setPaused(!paused_); }

void HitStage::step(int delta) {
    setPaused(true);
    if (frames_ <= 0) return;
    frame_ = ((frame_ + delta) % frames_ + frames_) % frames_;
    layout_.frame = frame_;
}

float HitStage::clampZoom(float z) const { return std::clamp(z, kMinZoom, kMaxZoom); }

void HitStage::clampPan() {
    // The centre of the view stays on the drawing: it never leaves the frame entirely.
    const float hx = world_.w * k_ * zoom_ * 0.5f, hy = world_.h * k_ * zoom_ * 0.5f;
    pan_.x = std::clamp(pan_.x, -hx, hx);
    pan_.y = std::clamp(pan_.y, -hy, hy);
}

void HitStage::place(const Rect& view, const Rect& world, float margin) {
    view_ = view;
    world_ = world;
    k_ = std::max(1e-4f, std::min((view.w - 2.0f * margin) / std::max(1.0f, world.w), (view.h - 2.0f * margin) / std::max(1.0f, world.h)));
    clampPan();
    layout_.view = view;
    layout_.zoom = zoom_;
}

void HitStage::setZoom(float zoom, Vec2 around) {
    const float z0 = zoom_, z1 = clampZoom(zoom);
    if (z1 == z0) return;
    // The point of the drawing under `around` stays under it.
    const Vec2 c = view_.center();
    const Vec2 rel{around.x - c.x - pan_.x, around.y - c.y - pan_.y};
    pan_ = {around.x - c.x - rel.x * (z1 / z0), around.y - c.y - rel.y * (z1 / z0)};
    zoom_ = z1;
    clampPan();
    layout_.zoom = zoom_;
}

void HitStage::zoomBy(float factor, Vec2 around) { setZoom(zoom_ * factor, around); }

void HitStage::panBy(Vec2 d) {
    pan_ = {pan_.x + d.x, pan_.y + d.y};
    clampPan();
}

void HitStage::recenter() {
    zoom_ = 1.0f;
    pan_ = {0.0f, 0.0f};
    layout_.zoom = zoom_;
}

Vec2 HitStage::toScreen(Vec2 p) const {
    const Vec2 c = view_.center(), wc = world_.center();
    const float k = k_ * zoom_;
    return {c.x + pan_.x + (p.x - wc.x) * k, c.y + pan_.y + (p.y - wc.y) * k};
}

Rect HitStage::toScreen(const Rect& r) const {
    const Vec2 a = toScreen(Vec2{r.x, r.y});
    const float k = k_ * zoom_;
    return {a.x, a.y, r.w * k, r.h * k};
}

bool HitStage::focused(const UIContext& ui) const { return !id_.empty() && ui.focused(id_); }

bool HitStage::input(UIContext& ui, const std::string& id, float dt) {
    id_ = id;
    InputManager& in = ui.input();
    bool hovered = false;
    const bool activated = ui.hotspot(id, view_, &hovered);
    if (!ui.acceptsInput()) {
        pressed_ = dragging_ = false;
        return false;
    }
    const Vec2 m = in.mousePos();
    const bool over = ui.mouseOver(view_);
    bool click = false;
    // Espace, Entrée, A (the view focused): pause and play; a click of the mouse is not one.
    if (activated && !in.mouseReleased(SDL_BUTTON_LEFT)) togglePause();
    if (over) {
        ui.claimWheel(view_);  // the page under it does not scroll with the wheel here
        if (in.wheel() != 0.0f) zoomBy(std::pow(1.2f, in.wheel()), m);
        if (in.mousePressed(SDL_BUTTON_LEFT)) {
            pressed_ = true;
            dragging_ = false;
            dragged_ = 0.0f;
        }
        if (in.mouseMovedThisFrame()) keyTip_ = captureTip_ = false;
    }
    if (pressed_) {
        if (in.mouseDown(SDL_BUTTON_LEFT)) {
            const Vec2 d = in.mouseDelta();
            dragged_ += std::abs(d.x) + std::abs(d.y);
            if (dragged_ > ui.s(5)) dragging_ = true;
            if (dragging_) panBy(d);
        } else {
            if (!dragging_ && over) click = true;
            pressed_ = dragging_ = false;
        }
    }
    // Tab (not typing): the view takes the focus and the next part (Maj: the previous one).
    if (in.keyPressed(SDL_SCANCODE_TAB) && !ui.editingText()) {
        if (!ui.focused(id)) ui.setFocus(id);
        cycle_ = in.shift() ? -1 : 1;
    }
    const bool north = in.rawPadButton(SDL_GAMEPAD_BUTTON_NORTH), west = in.rawPadButton(SDL_GAMEPAD_BUTTON_WEST);
    if (ui.focused(id)) {
        const Vec2 ls = in.leftStick();
        const bool stickPan = zoom_ > 1.001f && (std::abs(ls.x) > 0.2f || std::abs(ls.y) > 0.2f);
        if (stickPan) {
            panBy({-ls.x * ui.s(760) * dt, -ls.y * ui.s(760) * dt});
            ui.consumeNavigation(true, true);
        } else if (in.shift()) {
            // Maj + flèches: the drawing moves (the view looks that way).
            const float dx = (in.keyDown(SDL_SCANCODE_RIGHT) ? 1.0f : 0.0f) - (in.keyDown(SDL_SCANCODE_LEFT) ? 1.0f : 0.0f);
            const float dy = (in.keyDown(SDL_SCANCODE_DOWN) ? 1.0f : 0.0f) - (in.keyDown(SDL_SCANCODE_UP) ? 1.0f : 0.0f);
            panBy({-dx * ui.s(620) * dt, -dy * ui.s(620) * dt});
            ui.consumeNavigation(true, true);
        } else if (paused_) {
            if (in.menuPressed(Action::MenuLeft)) {
                step(-1);
                ui.consumeNavigation(true, false);
            } else if (in.menuPressed(Action::MenuRight)) {
                step(1);
                ui.consumeNavigation(true, false);
            }
        }
        if (in.shortcutPressed(SDLK_PLUS) || in.shortcutPressed(SDLK_EQUALS) || in.keyPressed(SDL_SCANCODE_KP_PLUS)) zoomBy(1.25f);
        if (in.shortcutPressed(SDLK_MINUS) || in.keyPressed(SDL_SCANCODE_KP_MINUS)) zoomBy(0.8f);
        // The triggers (RT closer, LT further) and the right stick zoom around the centre.
        const float lt = in.leftTrigger() > 0.08f ? in.leftTrigger() : 0.0f, rt = in.rightTrigger() > 0.08f ? in.rightTrigger() : 0.0f;
        const float ry = std::abs(in.rightStick().y) > 0.2f ? in.rightStick().y : 0.0f;
        const float rate = (rt - lt) * 1.6f - ry * 2.0f;
        if (rate != 0.0f) zoomBy(std::exp(rate * dt));
        // Y the next part, X the previous one.
        if (north && !padNorth_) cycle_ = 1;
        if (west && !padWest_) cycle_ = -1;
    } else if (!captureTip_) {
        keyTip_ = false;  // the focus gone: the part chosen with the keys has no tooltip any more
    }
    if (ui.focused(id) && in.anyMenuInput()) captureTip_ = false;
    padNorth_ = north;
    padWest_ = west;
    if (cycle_ != 0) keyTip_ = true;
    layout_.zoom = zoom_;
    layout_.paused = paused_;
    return click;
}

void HitStage::drawFocus(UIContext& ui) const {
    if (focused(ui)) ui.r().drawRoundedRect(view_, ui.s(10), ui.theme().accent, std::max(1.0f, ui.s(2.0f)));
}

void HitStage::parts(UIContext& ui, const HitPartsSheet& sheet, const std::vector<HitPartPiece>& pieces, const std::function<Vec2(Vec2)>& toScreen) {
    struct Placed {
        int part;
        std::vector<Vec2> pts;
        float area;
        Vec2 centroid;
    };
    std::vector<Placed> placed;
    for (const HitPartPiece& p : pieces) {
        if (p.part < 0 || p.part >= static_cast<int>(sheet.parts.size()) || p.pts.size() < 3) continue;
        Placed q{p.part, {}, 0.0f, {}};
        for (const Vec2& v : p.pts) {
            q.pts.push_back(toScreen(v));
            q.centroid = {q.centroid.x + q.pts.back().x, q.centroid.y + q.pts.back().y};
        }
        q.centroid = {q.centroid.x / static_cast<float>(q.pts.size()), q.centroid.y / static_cast<float>(q.pts.size())};
        q.area = area2(q.pts) * 0.5f;
        placed.push_back(std::move(q));
    }
    // The parts of this frame, in the sheet's order (Tab goes through them).
    std::vector<int> present;
    for (int i = 0; i < static_cast<int>(sheet.parts.size()); ++i)
        if (std::any_of(placed.begin(), placed.end(), [&](const Placed& q) { return q.part == i; })) present.push_back(i);
    auto largest = [&](const std::string& name, Vec2& at) {
        float best = 0.0f;
        for (const Placed& q : placed)
            if (sheet.parts[static_cast<size_t>(q.part)] == name && q.area > best) {
                best = q.area;
                at = q.centroid;
            }
        return best > 0.0f;
    };
    for (int i : present) {
        Vec2 at;
        if (largest(sheet.parts[static_cast<size_t>(i)], at)) layout_.partAt.push_back({sheet.parts[static_cast<size_t>(i)], at});
    }
    // A capture: its zoom around that part, its tooltip.
    if (gCapture.pending) {
        gCapture.pending = false;
        if (!gCapture.part.empty()) {
            chosen_ = gCapture.part;
            keyTip_ = true;
            captureTip_ = true;
        }
        Vec2 at = view_.center();
        if (!chosen_.empty()) largest(chosen_, at);
        setZoom(gCapture.zoom, at);  // the part stays under itself, then comes to the centre of the view
        panBy({view_.center().x - at.x, view_.center().y - at.y});
    }
    if (cycle_ != 0 && !present.empty()) {
        const int n = static_cast<int>(present.size());
        int cur = -1;
        for (int i = 0; i < n; ++i)
            if (sheet.parts[static_cast<size_t>(present[static_cast<size_t>(i)])] == chosen_) cur = i;
        const int next = cur < 0 ? (cycle_ > 0 ? 0 : n - 1) : ((cur + cycle_) % n + n) % n;
        chosen_ = sheet.parts[static_cast<size_t>(present[static_cast<size_t>(next)])];
        captureTip_ = false;
    }
    cycle_ = 0;
    shown_.clear();
    fromMouse_ = false;
    InputManager& in = ui.input();
    const Vec2 m = in.mousePos();
    // The part chosen with Tab / Y / X (or a capture's) while the view has the focus, until the pointer moves over it.
    if (keyTip_ && (focused(ui) || captureTip_) && !chosen_.empty()) {
        Vec2 at;
        if (largest(chosen_, at)) {
            shown_ = chosen_;
            anchor_ = {std::clamp(at.x, view_.x, view_.right()), std::clamp(at.y, view_.y, view_.bottom())};
        }
    }
    if (shown_.empty() && ui.acceptsInput() && ui.usingPointer() && ui.mouseOver(view_) && !dragging_ && !captureTip_) {
        // The smallest piece under the pointer (a claw over the arm).
        float best = 0.0f;
        for (const Placed& q : placed)
            if (convexContains(q.pts, m) && (best == 0.0f || q.area < best)) {
                best = q.area;
                shown_ = sheet.parts[static_cast<size_t>(q.part)];
            }
        if (!shown_.empty()) {
            anchor_ = m;
            fromMouse_ = true;
        }
    }
}

void HitStage::setTip(const PartTip& tip) {
    tip_ = tip;
    tipSet_ = !shown_.empty();
}

void HitStage::addText(const std::string& t, const Rect& r, float px, FontFace face) { layout_.texts.push_back({t, r, px, face}); }

namespace {

/// The bar's elements: their kind and their room (play, previous, frame, next | zoom out, zoom, zoom in, recentre).
struct BarItem {
    int kind;
    Rect r;
};

std::vector<BarItem> barItems(UIContext& ui, int frames, float x, float y, float w, float& height) {
    const float bh = ui.s(36), ib = ui.s(44), zb = ui.s(40), gap = ui.s(8), groupGap = ui.s(18);
    const float px = ui.s(fz(ui, 13));
    // The widest texts they can show (nothing moves while it plays).
    std::string digits = std::to_string(std::max(1, frames));
    for (char& c : digits) c = '8';
    const float frameW = ui.text().measure("image " + digits + " / " + digits, FontFace::Body, px).x + ui.s(10);
    const float zoomW = std::max(ui.text().measure("× 0,5", FontFace::Body, px).x, ui.text().measure("× 8,8", FontFace::Body, px).x) + ui.s(10);
    const float fitW = ui.text().measure("Recentrer", FontFace::Bold, ui.s(15)).x + ui.s(28);
    const float groupA[] = {ib, ib, frameW, ib}, groupB[] = {zb, zoomW, zb, fitW};
    auto width = [&](const float* g) { return g[0] + g[1] + g[2] + g[3] + 3.0f * gap; };
    std::vector<BarItem> out;
    float cx = x, cy = y;
    auto place = [&](const float* g, int first) {
        for (int i = 0; i < 4; ++i) {
            if (cx > x && cx + g[i] > x + w) {  // a narrower room: the next row
                cx = x;
                cy += bh + gap;
            }
            out.push_back({first + i, {cx, cy, g[i], bh}});
            cx += g[i] + gap;
        }
    };
    place(groupA, 0);
    if (cx - gap + groupGap + width(groupB) <= x + w) cx += groupGap - gap;
    else {
        cx = x;
        cy += bh + gap;
    }
    place(groupB, 4);
    height = cy + bh - y;
    return out;
}

void drawPlayIcon(Renderer& r, const Rect& b, bool playing, float unit, Color c) {
    const Vec2 m = b.center();
    const float h = std::min(b.w, b.h) * 0.42f;
    if (playing) {  // pause: two bars
        const float bw = std::max(2.0f, h * 0.32f);
        r.fillRect({m.x - h * 0.42f, m.y - h * 0.5f, bw, h}, c);
        r.fillRect({m.x + h * 0.42f - bw, m.y - h * 0.5f, bw, h}, c);
    } else {
        r.fillConvexPolygon({{m.x - h * 0.38f, m.y - h * 0.5f}, {m.x + h * 0.5f, m.y}, {m.x - h * 0.38f, m.y + h * 0.5f}}, c);
    }
    (void)unit;
}

void drawStepIcon(Renderer& r, const Rect& b, int dir, Color c) {
    const Vec2 m = b.center();
    const float h = std::min(b.w, b.h) * 0.40f, bw = std::max(2.0f, h * 0.22f);
    const float s = static_cast<float>(dir);
    // A triangle toward `dir`, a bar after it.
    r.fillConvexPolygon({{m.x - s * h * 0.42f, m.y - h * 0.5f}, {m.x + s * h * 0.30f, m.y}, {m.x - s * h * 0.42f, m.y + h * 0.5f}}, c);
    const float bx = dir > 0 ? m.x + h * 0.30f : m.x - h * 0.30f - bw;
    r.fillRect({bx, m.y - h * 0.5f, bw, h}, c);
}

}  // namespace

float HitStage::controlsHeight(UIContext& ui, float w) const {
    float h = 0.0f;
    barItems(ui, frames_, 0.0f, 0.0f, w, h);
    return h;
}

float HitStage::controls(UIContext& ui, const std::string& id, float x, float y, float w) {
    Renderer& r = ui.r();
    float h = 0.0f;
    const std::vector<BarItem> items = barItems(ui, frames_, x, y, w, h);
    const float px = ui.s(fz(ui, 13));
    const Color ic = ui.theme().text;
    for (const BarItem& it : items) {
        UIContext::ButtonStyle st;
        st.small = true;
        layout_.boxes.push_back(it.r);
        switch (it.kind) {
            case 0:
                if (ui.button(id + "/play", it.r, "", st)) togglePause();
                drawPlayIcon(r, it.r, !paused_, ui.s(1.0f), ic);
                break;
            case 1:
            case 3:
                st.enabled = frames_ > 1;
                if (ui.button(id + (it.kind == 1 ? "/prev" : "/next"), it.r, "", st)) step(it.kind == 1 ? -1 : 1);
                drawStepIcon(r, it.r, it.kind == 1 ? -1 : 1, frames_ > 1 ? ic : ui.theme().textDim);
                break;
            case 2: {
                const std::string t = "image " + std::to_string(std::min(frame_ + 1, std::max(1, frames_))) + " / " + std::to_string(std::max(1, frames_));
                ui.label(t, it.r.center(), px / ui.scale(), kDim, Align::Center, FontFace::Body, VAlign::Middle);
                addText(t, it.r, px, FontFace::Body);
                break;
            }
            case 4:
            case 6: {
                const bool in = it.kind == 6;
                st.fontSize = 20.0f;
                st.enabled = in ? zoom_ < kMaxZoom - 1e-3f : zoom_ > kMinZoom + 1e-3f;
                if (ui.button(id + (in ? "/zin" : "/zout"), it.r, in ? "+" : "−", st)) zoomBy(in ? 1.25f : 0.8f);
                break;
            }
            case 5: {
                const std::string t = zoomLabel(zoom_);
                ui.label(t, it.r.center(), px / ui.scale(), kText, Align::Center, FontFace::Body, VAlign::Middle);
                addText(t, it.r, px, FontFace::Body);
                break;
            }
            case 7:
                st.fontSize = 15.0f;
                st.enabled = std::abs(zoom_ - 1.0f) > 1e-3f || std::abs(pan_.x) > 0.5f || std::abs(pan_.y) > 0.5f;
                if (ui.button(id + "/fit", it.r, "Recentrer", st)) recenter();
                break;
            default: break;
        }
    }
    layout_.zoom = zoom_;
    layout_.paused = paused_;
    return h;
}

std::string HitStage::helpText(UIContext& ui, int device, bool picks) const {
    if (device == 2) {
        const GamepadStyle st = ui.input().gamepadStyle();
        return padBindingName(PadBinding::button(SDL_GAMEPAD_BUTTON_SOUTH), st) + " : pause · croix : image (en pause) · " +
               padBindingName(PadBinding::axisPos(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), st) + " / " +
               padBindingName(PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), st) + " : zoom · stick gauche : déplacer · " +
               padBindingName(PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH), st) + " : partie suivante";
    }
    if (device == 1) return "Espace : pause · ← → : image (en pause) · + − : zoom · Maj + flèches : déplacer · Tab : partie suivante";
    return picks ? "Survolez une partie : ce qu'elle fait · clic : la choisir · molette : zoom · glisser : déplacer"
                 : "Survolez une partie : ce qu'elle fait · molette : zoom · glisser : déplacer";
}

float HitStage::helpHeight(UIContext& ui, float w, bool picks) const {
    const float px = ui.s(fz(ui, 12.5f));
    float h = 0.0f;
    for (int d = 0; d < 3; ++d) h = std::max(h, ui.text().measureWrapped(helpText(ui, d, picks), FontFace::Body, px, w).y);
    return h;
}

float HitStage::help(UIContext& ui, float x, float y, float w, bool picks) {
    const int device = ui.input().lastDevice() == InputDevice::Gamepad ? 2 : ui.usingPointer() ? 0 : 1;
    const std::string t = helpText(ui, device, picks);
    const float px = ui.s(fz(ui, 12.5f));
    const float h = helpHeight(ui, w, picks);
    const Vec2 m = ui.text().measureWrapped(t, FontFace::Body, px, w);
    TextStyle st;
    st.face = FontFace::Body;
    st.size = px;
    st.color = kDim;
    ui.text().drawWrapped(t, {x, y}, w, st);
    addText(t, {x, y, w, m.y}, px, FontFace::Body);
    layout_.boxes.push_back({x, y, w, h});
    return h;
}

void HitStage::drawTip(UIContext& ui) {
    if (!drawn_) return;
    drawn_ = false;
    layout_.view = view_;
    layout_.zoom = zoom_;
    layout_.paused = paused_;
    layout_.drawnAt = ui.app().frameIndex();
    layout_.tip = {};
    layout_.tipLines.clear();
    layout_.tipPart.clear();
    if (!tipSet_ || shown_.empty()) {
        gLast = layout_;
        return;
    }
    tipSet_ = false;
    Renderer& r = ui.r();
    const float pad = ui.s(10), gap = ui.s(4), sw = ui.s(14), sg = ui.s(8);
    const float W = ui.screenW(), H = ui.screenH(), edge = ui.s(8);
    const float maxW = std::max(ui.s(200), std::min(W * 0.42f, ui.s(560)));
    const float tpx = ui.s(fz(ui, 15)), lpx = ui.s(fz(ui, 13));
    // Measured: its title after its role's swatch, then each line wrapped to the room.
    const float titleRoom = maxW - sw - sg;
    const Vec2 tm = ui.text().measureWrapped(tip_.title, FontFace::Bold, tpx, titleRoom);
    float w = sw + sg + tm.x, h = std::max(sw, tm.y);
    std::vector<Vec2> lm;
    for (const std::string& l : tip_.lines) {
        lm.push_back(ui.text().measureWrapped(l, FontFace::Body, lpx, maxW));
        w = std::max(w, lm.back().x);
        h += gap + lm.back().y;
    }
    w = std::min(maxW, w + ui.s(2));
    const float bw = w + 2.0f * pad, bh = h + 2.0f * pad;
    // Beside the pointer (never over it) or the part chosen with the keys; inside the screen.
    const Vec2 a = anchor_;
    const Rect keep = fromMouse_ ? Rect{a.x - 4.0f, a.y - 4.0f, std::max(28.0f, ui.s(30)), std::max(36.0f, ui.s(38))}
                                 : Rect{a.x - ui.s(14), a.y - ui.s(14), ui.s(28), ui.s(28)};
    const float clampY = std::clamp(a.y - bh * 0.5f, edge, std::max(edge, H - edge - bh));
    const Vec2 tries[] = {{keep.right() + ui.s(4), keep.bottom() + ui.s(2)},
                          {keep.x - ui.s(8) - bw, keep.bottom() + ui.s(2)},
                          {keep.right() + ui.s(4), keep.y - ui.s(8) - bh},
                          {keep.x - ui.s(8) - bw, keep.y - ui.s(8) - bh},
                          {keep.right() + ui.s(6), clampY},
                          {keep.x - ui.s(8) - bw, clampY}};
    Rect box{};
    bool found = false;
    for (const Vec2& p : tries) {
        const Rect b{p.x, p.y, bw, bh};
        if (b.x >= edge && b.y >= edge && b.right() <= W - edge && b.bottom() <= H - edge && !b.intersects(keep)) {
            box = b;
            found = true;
            break;
        }
    }
    if (!found) {
        // The side with the most room, clamped (a tooltip taller than the screen cannot happen: its lines are few).
        const bool right = a.x < W * 0.5f;
        box = {right ? keep.right() + ui.s(6) : keep.x - ui.s(8) - bw, clampY, bw, bh};
        box.x = std::clamp(box.x, edge, std::max(edge, W - edge - bw));
    }
    r.fillRoundedRect(box, ui.s(10), Color{11, 17, 32, 246});
    r.drawRoundedRect(box, ui.s(10), partRoleColor(tip_.role).withAlpha(200), std::max(1.0f, ui.s(1.5f)));
    float cy = box.y + pad;
    const float cx = box.x + pad;
    drawPartSwatch(r, {cx, cy + std::max(0.0f, (std::min(tm.y, tpx * 1.3f) - sw) * 0.5f), sw, sw}, tip_.role, ui.s(1.0f));
    TextStyle st;
    st.face = FontFace::Bold;
    st.size = tpx;
    st.color = ui.theme().text;
    // Wrapped at the widths measured (the widest line: the same breaks as at the room's).
    const float titleW = std::min(titleRoom, tm.x + ui.s(2));
    ui.text().drawWrapped(tip_.title, {cx + sw + sg, cy}, titleW, st);
    addText(tip_.title, {cx + sw + sg, cy, titleW, tm.y}, tpx, FontFace::Bold);
    cy += std::max(sw, tm.y);
    layout_.tipLines.push_back(tip_.title);
    for (size_t i = 0; i < tip_.lines.size(); ++i) {
        cy += gap;
        const std::string& l = tip_.lines[i];
        st.face = FontFace::Body;
        st.size = lpx;
        // Its state first in warm, the last (the unit) dim, the others bright.
        const bool note = i + 1 == tip_.lines.size() && tip_.byParts;
        st.color = note ? kDim : (l.rfind("Pendant ", 0) == 0 ? kPeach : kText);
        ui.text().drawWrapped(l, {cx, cy}, w, st);
        addText(l, {cx, cy, w, lm[i].y}, lpx, FontFace::Body);
        layout_.tipLines.push_back(l);
        cy += lm[i].y;
    }
    layout_.tip = box;
    layout_.tipAnchor = a;
    layout_.tipFromMouse = fromMouse_;
    layout_.tipPart = shown_;
    gLast = layout_;
}

// ------------------------------------------------------------------ « Où frapper » of a boss's sheet

void HitPartsView::draw(GameServices& g, const Rect& area, const EnemyDefinition& d, float dt) {
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    if (of_ != d.id) {
        of_ = d.id;
        phase_ = pose_ = 0;
        stage_.reset();
    }
    stage_.advance(dt);
    // Its sheets: its own, then each phase's that differs (the first phase that wears it).
    std::vector<std::string> sheets = {d.animation};
    std::vector<int> firstPhase = {1};
    for (size_t i = 0; i < d.phases.size(); ++i)
        if (!d.phases[i].animation.empty() && std::find(sheets.begin(), sheets.end(), d.phases[i].animation) == sheets.end()) {
            sheets.push_back(d.phases[i].animation);
            firstPhase.push_back(static_cast<int>(i) + 1);
        }
    phase_ = std::clamp(phase_, 0, static_cast<int>(sheets.size()) - 1);
    float y = area.y;
    const float x = area.x, w = area.w;
    if (sheets.size() > 1) {
        std::vector<std::string> labels;
        std::vector<bool> on;
        for (size_t i = 0; i < sheets.size(); ++i) {
            labels.push_back("Phase " + std::to_string(firstPhase[i]));
            on.push_back(static_cast<int>(i) == phase_);
        }
        float bottom = y;
        const int c = chips(ui, "hitparts/phase", x, y, x + w, labels, on, bottom);
        if (c >= 0 && c != phase_) {
            phase_ = c;
            stage_.restart();
        }
        y = bottom + ui.s(8);
    }
    const std::string sheetPath = sheets[static_cast<size_t>(phase_)];
    AnimationSetPtr set = g.app.resources().animation(sheetPath);
    std::vector<std::string> names;
    if (set)
        for (const auto& [name, clip] : set->clips) names.push_back(name);
    const std::vector<std::string> poses = poseClips(names);
    if (!wanted_.empty()) {
        for (size_t i = 0; i < poses.size(); ++i)
            if (poses[i] == wanted_) pose_ = static_cast<int>(i);
        wanted_.clear();
    }
    pose_ = poses.empty() ? 0 : std::clamp(pose_, 0, static_cast<int>(poses.size()) - 1);
    {
        const float lab = std::min(ui.s(70), textW(ui, "Pose :", fz(ui, 14), FontFace::Bold) + ui.s(12));
        fit(ui, "Pose :", {x, y + ui.s(18)}, fz(ui, 14), lab, kDim, FontFace::Bold);
        std::vector<std::string> labels;
        std::vector<bool> on;
        for (size_t i = 0; i < poses.size(); ++i) {
            labels.push_back(poseLabel(poses[i]));
            on.push_back(static_cast<int>(i) == pose_);
        }
        float bottom = y;
        const int c = chips(ui, "hitparts/pose", x + lab, y, x + w, labels, on, bottom);
        if (c >= 0 && c != pose_) {
            pose_ = c;
            stage_.restart();
        }
        y = bottom + ui.s(10);
    }
    const float gap = ui.s(12);
    const float legendW = std::min(ui.s(360), w * 0.4f), sw = w - legendW - gap;
    const float bottom = std::max(y + ui.s(160), area.bottom());
    const Rect leg{x + sw + gap, y, legendW, bottom - y};
    const HitPartsSheet* parts = HitPartsSheet::of(sheetPath);
    const PartRules& rules = PartRules::of(d.id);
    // The frame of its pose now (its clip's own durations, again and again), or the one stepped to.
    const AnimationClip* clip = set && !poses.empty() ? set->clip(poses[static_cast<size_t>(pose_)]) : nullptr;
    std::vector<float> durations;
    if (clip)
        for (int i = 0; i < clip->frameCount(); ++i) durations.push_back(clip->durations[static_cast<size_t>(i)]);
    const int frame = stage_.frame(durations);
    // The bar, the drawing, its keys.
    const std::string sid = "hitparts/stage";
    float cy = y + stage_.controls(ui, sid, x, y, sw) + ui.s(8);
    const float helpH = stage_.helpHeight(ui, sw);
    const Rect view{x, cy, sw, std::max(ui.s(100), bottom - cy - helpH - ui.s(8))};
    // Its sprite as the game places it: its feet at (0, 0), its pivot × its scale.
    const Rect box = Rect::fromBottomCenter({0.0f, 0.0f}, d.size);
    Rect sprite{};
    Rect win = box;
    if (set) {
        sprite = {-set->pivot.x * d.scale, -set->pivot.y * d.scale, static_cast<float>(set->frameWidth) * d.scale, static_cast<float>(set->frameHeight) * d.scale};
        win = sprite;
    }
    const float margin = std::max(win.w, win.h) * 0.04f;
    win = {win.x - margin, win.y - margin, win.w + 2 * margin, win.h + 2 * margin};
    stage_.place(view, win, ui.s(8));
    stage_.input(ui, sid, dt);
    const std::vector<HitPartPiece>* pieces = parts && set && clip ? parts->frame(*set, *clip, frame) : nullptr;
    const Vec2 pivot = set ? set->pivot : Vec2{};
    const float sc = d.scale;
    auto toScreen = [&](Vec2 v) { return stage_.toScreen(Vec2{(v.x - pivot.x) * sc, (v.y - pivot.y) * sc}); };
    if (pieces) stage_.parts(ui, *parts, *pieces, toScreen);
    r.fillRoundedRect(view, ui.s(10), Color{11, 18, 38, 255});
    r.pushClip(view);
    if (set && set->texture && clip && clip->frameCount() > 0) {
        const Rect src = set->frameRect(*clip, frame);
        r.drawTexture(*set->texture, &src, stage_.toScreen(sprite), Color::white());
    }
    if (pieces) drawHitPieces(r, std::max(1.5f, ui.s(2.0f)), *parts, *pieces, rules, toScreen, 70, stage_.shownPart());
    r.popClip();
    stage_.drawFocus(ui);
    if (!stage_.shownPart().empty()) stage_.setTip(partTip(partFacts(stage_.shownPart(), rules, d)));
    stage_.help(ui, x, view.bottom() + ui.s(8), sw);
    stage_.layout().boxes.push_back(view);
    stage_.layout().boxes.push_back(leg);
    // The legend: one entry per role, its parts named; the parts that also hurt during attacks; how its fight uses them.
    drawHitLegend(ui, "hitparts/legend", leg, partsLegend(parts, rules, d), &stage_.layout());
    stage_.layout().body = stage_.toScreen(box);  // where its box would be: never drawn (tests)
}

void HitPartsView::drawOverlay(GameServices& g) { stage_.drawTip(g.ui); }

}  // namespace cosmic
