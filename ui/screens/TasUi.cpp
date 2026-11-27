#include "ui/screens/TasUi.h"

#include <algorithm>
#include <map>

#include "engine/core/Application.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/ghost/GhostStore.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::tasui {

void diamond(Renderer& r, Vec2 c, float size, float line, float alpha) {
    const float h = size * 0.5f;
    const Vec2 top{c.x, c.y - h}, right{c.x + h, c.y}, bottom{c.x, c.y + h}, left{c.x - h, c.y};
    const auto a = [&](uint8_t v) { return static_cast<uint8_t>(std::clamp(static_cast<float>(v) * alpha, 0.0f, 255.0f)); };
    r.fillTriangle(top, right, bottom, kTas.withAlpha(a(56)));
    r.fillTriangle(top, bottom, left, kTas.withAlpha(a(56)));
    const Color c2 = kTas.withAlpha(a(255));
    r.drawLine(top, right, c2, line);
    r.drawLine(right, bottom, c2, line);
    r.drawLine(bottom, left, c2, line);
    r.drawLine(left, top, c2, line);
}

void drawGhost(Renderer& r, const Texture& tex, const Rect& src, const Rect& dst, bool flip, float alpha, float viewScale, float time) {
    if (src.h <= 0.0f || dst.h <= 0.0f || alpha <= 0.0f) return;
    const auto a = [&](float v) { return static_cast<uint8_t>(std::clamp(v * alpha, 0.0f, 255.0f)); };
    r.drawWorldGlow({dst.center().x, dst.center().y}, std::max(dst.w, dst.h) * 0.7f, kTas.withAlpha(a(55)));
    // The outline: the silhouette drawn around it, bright.
    const float o = 1.8f / std::max(0.25f, viewScale);
    const Vec2 offs[8] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}, {-o, -o}, {o, -o}, {-o, o}, {o, o}};
    for (Vec2 d : offs) {
        Rect od = dst;
        od.x += d.x;
        od.y += d.y;
        r.drawWorldTexture(tex, &src, od, kTas.withAlpha(a(150)), BlendMode::Additive, flip);
    }
    // Barely filled: the body dark, a faint lime over it.
    r.drawWorldTexture(tex, &src, dst, Color{10, 16, 6, a(150)}, BlendMode::Alpha, flip);
    r.drawWorldTexture(tex, &src, dst, kTas.withAlpha(a(40)), BlendMode::Additive, flip);
    // Scan lines: one row of the frame in three, lit, running down it (masked by the frame itself).
    const float k = dst.h / src.h;
    const int phase = static_cast<int>(time * 12.0f) % 3;
    for (int row = phase; row < static_cast<int>(src.h); row += 3) {
        const Rect s1{src.x, src.y + static_cast<float>(row), src.w, 1.0f};
        const Rect d1{dst.x, dst.y + static_cast<float>(row) * k, dst.w, k};
        r.drawWorldTexture(tex, &s1, d1, kTas.withAlpha(a(185)), BlendMode::Additive, flip);
    }
}

float pill(UIContext& ui, const std::string& text, Vec2 leftMid, float size, bool draw) {
    const Vec2 ts = ui.text().measure(text, FontFace::Bold, ui.s(size));
    const float padX = ui.s(size * 0.75f);
    const Rect p{leftMid.x, leftMid.y - (ts.y + ui.s(size * 0.5f)) * 0.5f, ts.x + 2 * padX, ts.y + ui.s(size * 0.5f)};
    if (draw) {
        ui.r().fillRoundedRect(p, p.h * 0.5f, kTasDeep);
        ui.r().drawRoundedRect(p, p.h * 0.5f, kTasLine, std::max(1.0f, ui.s(1.2f)));
        ui.label(text, {p.x + padX, leftMid.y}, size, kTas, Align::Left, FontFace::Bold, VAlign::Middle);
    }
    return p.w;
}

float keyHint(UIContext& ui, const std::string& key, SDL_GamepadButton pad, const std::string& text, Vec2 leftMid, float height, float fontSize, bool draw) {
    const bool onPad = Glyphs::showsPad(ui);
    float w = 0.0f;
    if (onPad) {
        w = height;
        if (draw) Glyphs::drawPadButton(ui, PadBinding::button(pad), leftMid, height);
    } else {
        const float kw = ui.text().measure(key, FontFace::Bold, height * 0.55f).x;
        w = std::max(height, kw + height * 0.6f);
        if (draw) Glyphs::drawKeycap(ui, key, leftMid, height);
    }
    w += ui.s(8);
    const float tw = ui.text().measure(text, FontFace::Body, ui.s(fontSize)).x;
    if (draw) ui.label(text, {leftMid.x + w, leftMid.y}, fontSize, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    return w + tw;
}

Pieces& pieces(const std::string& screen) {
    static std::map<std::string, Pieces> all;
    return all[screen];
}

std::string categoryName(RunCategory c) { return categoryLabel(c); }

std::string categoryShortRule(RunCategory c) {
    switch (c) {
        case RunCategory::AnyPercent: return "terminer le plus vite possible";
        case RunCategory::HundredPercent: return "tous les éclats et reliques";
        case RunCategory::NoDeath: return "terminer sans mourir";
        case RunCategory::NoDamage: return "terminer sans subir de dégât";
    }
    return {};
}

bool openSheet(GameServices& g, const std::string& levelId, RunCategory c, std::function<void(RunCategory)> onRace) {
    tas::TasCatalog& cat = tas::catalog();
    RunCategory pick = c;
    if (!cat.tasFor(g, levelId, pick)) {
        bool found = false;
        for (RunCategory k : kAllCategories)
            if (cat.tasFor(g, levelId, k)) {
                pick = k;
                found = true;
                break;
            }
        if (!found) return false;
    }
    SheetRequest req;
    req.levelId = levelId;
    req.category = pick;
    req.onRace = std::move(onRace);
    g.app.pushScene(makeSheet(g, std::move(req)));
    return true;
}

bool watch(GameServices& g, const tas::TasRun& t, double at) {
    auto rep = loadTasReplay(t, tas::catalog().root());  // exact, of this level version and simulation only
    if (!rep) return false;
    g.app.pushScene(makeTasReplayViewer(g, std::move(*rep), t, at));
    return true;
}

void race(GameServices& g, const std::string& levelId, RunCategory c) {
    GhostSetupRequest req;
    req.tasCategory = categoryId(c);
    g.app.pushScene(makeGhostSetup(g, levelId, std::move(req)));
}

bool watchSegment(GameServices& g, const tas::TasRun& t, const std::string& from, const std::string& to, const std::string& name) {
    const int64_t a = t.at(from), b = to.empty() ? t.timeMs : t.at(to);
    if (a < 0 || b <= a) return false;
    MontageSegment m;
    m.from = from;
    m.to = to;
    m.duration = static_cast<float>(b - a) / 1000.0f;
    m.date = t.computedAt;
    m.file = Paths::toUtf8(t.replayFile);
    m.start = static_cast<float>(a) / 1000.0f;
    m.end = static_cast<float>(b) / 1000.0f;
    SobMontageRequest req;
    req.levelId = t.levelId;
    req.levelHash = t.levelHash;
    if (const LevelEntry* e = g.content.level(t.levelId)) req.levelName = e->name;
    req.plan.segments = {m};
    req.plan.total = m.duration;
    req.plan.withReplay = 1;
    req.names = {name};
    req.tas = true;
    g.app.pushScene(makeSobMontage(g, std::move(req)));
    return true;
}

}  // namespace cosmic::tasui
