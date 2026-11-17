#include "ui/online/RaidWidgets.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/online/OnlineSession.h"

namespace cosmic::raidui {

Vec2 fit(UIContext& ui, const std::string& t, Vec2 pos, float size, float room, Color c, FontFace face, Align a, bool verbatim) {
    if (t.empty() || room <= ui.s(10)) return {};
    i18n::Verbatim v(verbatim);
    const float f = ui.fitFont(t, face, size, room, 0.75f);
    return ui.label(ui.elide(t, face, f, room), pos, f, c, a, face, VAlign::Middle);
}

float textW(UIContext& ui, const std::string& t, float size, FontFace face, bool verbatim) {
    i18n::Verbatim v(verbatim);
    return ui.text().measure(t, face, ui.s(size)).x;
}

float pill(UIContext& ui, float x, float cy, const std::string& t, Color fill, Color text, float maxRight, float size) {
    const float w = textW(ui, t, size, FontFace::Bold) + ui.s(22);
    if (t.empty() || x + w > maxRight) return 0.0f;
    const Rect r{x, cy - ui.s(14), w, ui.s(28)};
    ui.r().fillRoundedRect(r, ui.s(8), fill);
    ui.label(t, r.center(), size, text, Align::Center, FontFace::Bold, VAlign::Middle);
    return w;
}

std::vector<std::string> wrap(UIContext& ui, const std::string& t, FontFace face, float size, float width, int maxLines, bool verbatim) {
    std::vector<std::string> out;
    if (t.empty() || maxLines <= 0 || width <= ui.s(10)) return out;
    const std::string shown = verbatim ? t : i18n::tr(t);
    i18n::Verbatim v(true);
    const float px = ui.s(size);
    auto w = [&](const std::string& s) { return ui.text().measure(s, face, px).x; };
    std::vector<std::string> words;
    std::string cur;
    for (char ch : shown) {
        if (ch == ' ' || ch == '\n') {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
        } else cur += ch;
    }
    if (!cur.empty()) words.push_back(cur);
    std::string line;
    size_t i = 0;
    for (; i < words.size(); ++i) {
        const std::string next = line.empty() ? words[i] : line + " " + words[i];
        if (w(next) <= width) {
            line = next;
            continue;
        }
        if (line.empty()) {  // one word wider than the room: shortened
            out.push_back(ui.elide(words[i], face, size, width));
            ++i;
        } else out.push_back(line);
        line.clear();
        if (static_cast<int>(out.size()) == maxLines) break;
        --i;  // the word starts the next line
    }
    if (static_cast<int>(out.size()) < maxLines && !line.empty()) {
        out.push_back(line);
        line.clear();
        i = words.size();
    }
    // Words left: the last line ends with « … ».
    if (i < words.size() && !out.empty()) {
        std::string last = out.back();
        for (size_t k = i; k < words.size() && w(last) < width * 1.5f; ++k) last += " " + words[k];
        out.back() = ui.elide(last + " …", face, size, width);
    }
    return out;
}

float drawLines(UIContext& ui, const std::vector<std::string>& lines, Vec2 pos, float size, float lineH, Color c, FontFace face) {
    i18n::Verbatim v(true);
    float y = pos.y;
    for (const std::string& l : lines) {
        ui.label(l, {pos.x, y + lineH * 0.5f}, size, c, Align::Left, face, VAlign::Middle);
        y += lineH;
    }
    return y - pos.y;
}

float section(UIContext& ui, const Rect& r, const std::string& title, Color titleColor) {
    ui.r().fillRoundedRect(r, ui.s(14), kPanel);
    ui.r().drawRoundedRect(r, ui.s(14), kBorder, ui.s(1.2f));
    fit(ui, title, {r.x + ui.s(14), r.y + ui.s(24)}, 15, r.w - ui.s(28), titleColor, FontFace::Title);
    return r.y + ui.s(46);
}

Color stateFill(raids::State s) {
    switch (s) {
        case raids::State::Open: return {31, 94, 58, 255};
        case raids::State::Scheduled: return {42, 58, 122, 255};
        case raids::State::Imminent: return {122, 74, 26, 255};
        case raids::State::Running: return {122, 26, 26, 255};
        case raids::State::Cancelled: return {70, 30, 44, 255};
        default: return {42, 47, 90, 255};
    }
}

Color stateText(raids::State s) {
    switch (s) {
        case raids::State::Open: return {184, 245, 204, 255};
        case raids::State::Imminent: return {255, 214, 160, 255};
        case raids::State::Cancelled: return {255, 176, 176, 255};
        default: return kText;
    }
}

void drawTitan(UIContext& ui, const Rect& r, float time) {
    // Proportions of the maquette (420 × 300): head, core, arms, legs, the burning eye.
    const float k = std::min(r.w / 420.0f, r.h / 300.0f);
    const float ox = r.x + (r.w - 420.0f * k) * 0.5f, oy = r.y + (r.h - 300.0f * k) * 0.5f;
    auto P = [&](float x, float y) { return Vec2{ox + x * k, oy + y * k}; };
    const Color limb{74, 26, 82, 255};
    const float pulse = 0.5f + 0.5f * std::sin(time * 1.6f);
    auto bar = [&](Vec2 a, Vec2 b, float wpx) { ui.r().drawLine(a, b, limb, wpx * k); ui.r().fillCircle(a, wpx * 0.5f * k, limb); ui.r().fillCircle(b, wpx * 0.5f * k, limb); };
    bar(P(55, 140), P(175, 100), 26);   // arms
    bar(P(245, 100), P(365, 140), 26);
    bar(P(170, 250), P(160, 300), 30);  // legs
    bar(P(250, 250), P(260, 300), 30);
    ui.r().fillRoundedRect({ox + 160 * k, oy + 200 * k, 100 * k, 60 * k}, 30 * k, limb);
    ui.r().drawGlow(P(210, 144), 90 * k, Color{255, 106, 61, static_cast<uint8_t>(60 + 40 * pulse)});
    ui.r().fillRoundedRect({ox + 150 * k, oy + 84 * k, 120 * k, 120 * k}, 40 * k, Color{150, 42, 70, 255});
    ui.r().fillRoundedRect({ox + 162 * k, oy + 96 * k, 96 * k, 96 * k}, 34 * k, Color{220, 90, 60, 255});
    ui.r().drawGlow(P(210, 46), 50 * k, Color{255, 90, 176, 70});
    ui.r().fillCircle(P(210, 46), 40 * k, Color{122, 42, 138, 255});
    ui.r().fillCircle(P(210, 46), 26 * k, Color{255, 176, 208, 255});
    ui.r().drawGlow(P(210, 142), 30 * k, Color{255, 210, 122, static_cast<uint8_t>(90 + 80 * pulse)});
    ui.r().fillCircle(P(210, 142), 14 * k, Color{255, 224, 138, 255});
}

int chips(UIContext& ui, const std::string& id, float x, float y, float right, const std::vector<std::string>& labels, const std::vector<bool>& on,
          float& bottom, bool enabled) {
    int clicked = -1;
    const float h = ui.s(36), gap = ui.s(8);
    float cx = x, cy = y;
    for (size_t i = 0; i < labels.size(); ++i) {
        const std::string& text = labels[i];
        float w = std::min(textW(ui, text, 14, FontFace::Bold) + ui.s(26), right - x);
        if (cx > x && cx + w > right) {
            cx = x;
            cy += h + gap;
        }
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 14;
        st.selected = i < on.size() && on[i];
        st.enabled = enabled;
        if (ui.button(id + "/" + std::to_string(i), {cx, cy, w, h}, text, st)) clicked = static_cast<int>(i);
        cx += w + gap;
    }
    bottom = cy + h;
    return clicked;
}

int64_t nowUtc(int64_t skew) {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + skew;
}

int64_t skewOf(const Json& data) {
    const auto t = raids::parseUtc(jstr(data, "server_time"));
    if (!t) return 0;
    const int64_t d = *t - nowUtc(0);
    // A few seconds are the trip itself; beyond a day the clock is not trusted either way.
    return std::llabs(d) < 3 || std::llabs(d) > 86400 ? 0 : d;
}

std::string whenLine(int64_t utc) {
    const raids::Civil c = raids::toSystemLocal(utc);
    const raids::Civil u = raids::civilAt(utc, 0);
    std::string d = raids::dateLong(c);
    if (!d.empty()) d[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(d[0])));
    return d + " · " + raids::hhmm(c) + " à votre heure (" + raids::offsetLabel(c.offsetMinutes) + ") · " + raids::hhmm(u) + " UTC";
}

std::string whenShort(int64_t utc) {
    const raids::Civil c = raids::toSystemLocal(utc);
    return raids::dateShort(c) + " " + raids::hhmm(c);
}

}  // namespace cosmic::raidui
