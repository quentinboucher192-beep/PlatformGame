#include "ui/widgets/HitPartsView.h"

#include <algorithm>
#include <cmath>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
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
    }
    return out;
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
    // sprite of any colour (grey armour on a grey leg); the part chosen in the editor outlined in white.
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

void HitPartsView::draw(GameServices& g, const Rect& area, const EnemyDefinition& d, float dt) {
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    if (of_ != d.id) {
        of_ = d.id;
        phase_ = pose_ = 0;
        t_ = 0.0f;
    }
    t_ += std::clamp(dt, 0.0f, 0.1f);
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
        if (c >= 0) phase_ = c;
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
            t_ = 0.0f;
        }
        y = bottom + ui.s(10);
    }
    const float legendW = std::min(ui.s(360), w * 0.4f);
    const Rect stage{x, y, w - legendW - ui.s(12), std::max(ui.s(120), area.bottom() - y)};
    const Rect leg{stage.right() + ui.s(12), y, legendW, stage.h};
    r.fillRoundedRect(stage, ui.s(10), Color{11, 18, 38, 255});
    r.fillRoundedRect(leg, ui.s(10), Color{18, 24, 52, 240});
    const HitPartsSheet* parts = HitPartsSheet::of(sheetPath);
    const PartRules& rules = PartRules::of(d.id);
    // The frame of its pose now (its clip's own durations, again and again).
    const AnimationClip* clip = set && !poses.empty() ? set->clip(poses[static_cast<size_t>(pose_)]) : nullptr;
    int frame = 0;
    if (clip && clip->frameCount() > 0) {
        float tc = std::fmod(t_, std::max(0.05f, clip->totalDuration()));
        while (frame + 1 < clip->frameCount() && tc >= clip->durations[static_cast<size_t>(frame)]) tc -= clip->durations[static_cast<size_t>(frame++)];
    }
    // Its sprite as the game places it: its feet at (0, 0), its pivot × its scale.
    const Rect box = Rect::fromBottomCenter({0.0f, 0.0f}, d.size);
    Rect win = box;
    Rect sprite{};
    if (set) {
        sprite = {-set->pivot.x * d.scale, -set->pivot.y * d.scale, static_cast<float>(set->frameWidth) * d.scale, static_cast<float>(set->frameHeight) * d.scale};
        win = win.united(sprite);
    }
    const float margin = std::max(win.w, win.h) * 0.04f;
    win = {win.x - margin, win.y - margin, win.w + 2 * margin, win.h + 2 * margin};
    const float k = std::min((stage.w - ui.s(16)) / std::max(1.0f, win.w), (stage.h - ui.s(16)) / std::max(1.0f, win.h));
    const Vec2 o{stage.center().x - win.center().x * k, stage.center().y - win.center().y * k};
    auto at = [&](Vec2 p) { return Vec2{o.x + p.x * k, o.y + p.y * k}; };
    r.pushClip(stage);
    if (set && set->texture && clip) {
        const Rect src = set->frameRect(*clip, frame);
        r.drawTexture(*set->texture, &src, {o.x + sprite.x * k, o.y + sprite.y * k, sprite.w * k, sprite.h * k}, Color::white());
    }
    const std::vector<HitPartPiece>* pieces = parts && set && clip ? parts->frame(*set, *clip, frame) : nullptr;
    if (pieces && set) {
        const Vec2 pivot = set->pivot;
        const float sc = d.scale;
        drawHitPieces(r, std::max(1.5f, ui.s(2.0f)), *parts, *pieces, rules, [&](Vec2 v) { return at({(v.x - pivot.x) * sc, (v.y - pivot.y) * sc}); });
    }
    // Its box (the fight's, or the old one when it fights by parts), then its feet.
    const Vec2 b0 = at({box.x, box.y}), b1 = at({box.right(), box.bottom()});
    r.drawRect({b0.x, b0.y, b1.x - b0.x, b1.y - b0.y}, Color{255, 255, 255, 170}, std::max(1.0f, ui.s(1.5f)));
    const Vec2 f0 = at({0.0f, 0.0f});
    const float arm = ui.s(8), th = std::max(1.5f, ui.s(2.0f));
    r.drawLine({f0.x - arm, f0.y}, {f0.x + arm, f0.y}, Color{255, 255, 255, 220}, th);
    r.drawLine({f0.x, f0.y - arm}, {f0.x, f0.y + arm}, Color{255, 255, 255, 220}, th);
    r.popClip();
    if (clip)
        fit(ui, poseLabel(poses[static_cast<size_t>(pose_)]) + " · image " + std::to_string(frame + 1) + " / " + std::to_string(clip->frameCount()),
            {stage.x + ui.s(10), stage.y + ui.s(14)}, fz(ui, 13), stage.w - ui.s(20), kDim, FontFace::Body);
    // The legend: each part, its role and its multiplier; then the box; then how its fight uses them.
    const float lx = leg.x + ui.s(12), lw = leg.w - ui.s(24), sw = ui.s(16);
    float ly = leg.y + ui.s(12);
    fit(ui, parts ? "Ses parties" : "Pas encore de parties", {lx, ly + ui.s(9)}, fz(ui, 15), lw, kGold, FontFace::Bold);
    ly += ui.s(28);
    std::vector<std::string> shown = parts ? parts->parts : std::vector<std::string>{};
    const float rowH = ui.s(22);
    for (size_t i = 0; i < shown.size(); ++i) {
        if (ly + rowH > leg.bottom() - ui.s(96)) {
            fit(ui, "… et " + std::to_string(shown.size() - i) + " de plus", {lx, ly + ui.s(9)}, fz(ui, 12.5f), lw, kDim);
            ly += rowH;
            break;
        }
        const PartRule rule = rules.ruleOr(shown[i]);
        drawPartSwatch(r, {lx, ly + ui.s(2), sw, sw}, rule.role, ui.s(1.0f));  // its colour and its pattern (2.76.2)
        const std::string right = std::string(partRoleLabel(rule.role)) + " · " + partDamageLabel(rule);
        const float rw = std::min(lw * 0.62f, textW(ui, right, fz(ui, 12.5f), FontFace::Body) + ui.s(4));
        fit(ui, right, {lx + lw, ly + ui.s(10)}, fz(ui, 12.5f), rw, kDim, FontFace::Body, Align::Right);
        fit(ui, capitalized(shown[i]), {lx + sw + ui.s(8), ly + ui.s(10)}, fz(ui, 13.5f), lw - sw - ui.s(16) - rw, kText, FontFace::Bold);
        ly += rowH;
    }
    ly += ui.s(6);
    const bool combat = rules.fromVersion > 0 && rules.fromVersion <= kSimulationVersion;
    const std::string boxLine = combat ? "Cadre blanc : son ancienne boîte (les replays d'avant)" : "Cadre blanc : sa boîte, qui prend et donne les coups";
    const std::string how = !parts ? "Son combat se joue sur sa boîte."
                            : combat ? "Combat par parties : vos coups touchent la partie visée (point faible d'abord), seules les parties rouges blessent."
                                     : "Parties : affichage ; combat par boîte jusqu'à son étape.";
    for (const std::string& line : {boxLine, how}) {
        const auto ls = wrap(ui, line, FontFace::Body, fz(ui, 12.5f), lw, 3);
        if (ly + static_cast<float>(ls.size()) * ui.s(17) > leg.bottom() - ui.s(6)) break;
        drawLines(ui, ls, {lx, ly}, fz(ui, 12.5f), ui.s(17), line == how && combat ? kGreen : kDim);
        ly += static_cast<float>(ls.size()) * ui.s(17) + ui.s(4);
    }
}

}  // namespace cosmic
