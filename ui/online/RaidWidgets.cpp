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

int chips(UIContext& ui, const std::string& id, float x, float y, float right, const std::vector<std::string>& labels, const std::vector<bool>& on,
          float& bottom, bool enabled, int iconAt, const std::function<void(const Rect&)>& icon) {
    int clicked = -1;
    const float h = ui.s(36), gap = ui.s(8), iconS = h - ui.s(14), iconGap = ui.s(8), pad = ui.s(13);
    float cx = x, cy = y;
    for (size_t i = 0; i < labels.size(); ++i) {
        const std::string& text = labels[i];
        const bool withIcon = icon && static_cast<int>(i) == iconAt;
        const float extra = withIcon ? iconS + iconGap : 0.0f;
        float w = std::min(textW(ui, text, 14, FontFace::Bold) + ui.s(26) + extra, right - x);
        if (cx > x && cx + w > right) {
            cx = x;
            cy += h + gap;
        }
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 14;
        st.selected = i < on.size() && on[i];
        st.enabled = enabled;
        const Rect cr{cx, cy, w, h};
        if (ui.button(id + "/" + std::to_string(i), cr, withIcon ? std::string() : text, st)) clicked = static_cast<int>(i);
        if (withIcon) {
            // 2.83: the icon, then the label measured into what is left (the button's own padding kept).
            icon({cr.x + pad, cr.center().y - iconS * 0.5f, iconS, iconS});
            fit(ui, text, {cr.x + pad + extra, cr.center().y}, 14, std::max(0.0f, cr.w - pad * 2.0f - extra), kText, FontFace::Bold);
        }
        cx += w + gap;
    }
    bottom = cy + h;
    return clicked;
}

std::vector<Rect> tabRects(UIContext& ui, float x, float y, float right, const std::vector<std::string>& labels, float& bottom) {
    const float h = ui.s(36), gap = ui.s(8);
    float w = 0.0f;
    for (const std::string& t : labels) w = std::max(w, textW(ui, t, 14, FontFace::Bold) + ui.s(26));
    w = std::min(w, std::max(0.0f, right - x));
    std::vector<Rect> out;
    float cx = x, cy = y;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (cx > x && cx + w > right + 0.5f) {
            cx = x;
            cy += h + gap;
        }
        out.push_back({cx, cy, w, h});
        cx += w + gap;
    }
    bottom = labels.empty() ? y : cy + h;
    return out;
}

float dropdown(UIContext& ui, const std::string& id, const Rect& r, const std::string& caption, const std::vector<std::string>& options, int& index,
               bool& open, const std::function<void(int, const Rect&)>& icon) {
    const int n = static_cast<int>(options.size());
    index = n > 0 ? std::clamp(index, 0, n - 1) : 0;
    const std::string choice = n > 0 ? i18n::tr(options[static_cast<size_t>(index)]) : std::string("—");
    const std::string shown =
        i18n::keep(caption.empty() ? choice : i18n::tr(caption) + (i18n::language() == Language::English ? ": " : " : ") + choice);
    UIContext::ButtonStyle st;
    st.small = true;
    if (ui.button(id + "/head", r, "", st) && n > 0) open = !open;
    // 2.83: the choice's icon; the text at the left, measured into what the arrow leaves; the arrow drawn at the right.
    const float arrowW = ui.s(14), pad = ui.s(14);
    float textX = r.x + pad;
    if (icon && n > 0) {
        const float is = std::max(0.0f, r.h - ui.s(14));
        icon(index, {textX, r.center().y - is * 0.5f, is, is});
        textX += is + ui.s(10);
    }
    fit(ui, shown, {textX, r.center().y}, 15, std::max(0.0f, r.right() - textX - pad * 2.0f - arrowW), kText, FontFace::Bold, Align::Left);
    const Vec2 c{r.right() - pad - arrowW * 0.5f, r.center().y};
    const float hw = arrowW * 0.5f, hh = ui.s(4);
    if (open) ui.r().fillTriangle({c.x - hw, c.y + hh}, {c.x + hw, c.y + hh}, {c.x, c.y - hh}, kGold);
    else ui.r().fillTriangle({c.x - hw, c.y - hh}, {c.x + hw, c.y - hh}, {c.x, c.y + hh}, kGold);
    float y = r.bottom();
    if (!open) return y;
    const float rowH = std::max(ui.s(36), r.h - ui.s(8)), gap = ui.s(4);
    for (int i = 0; i < n; ++i) {
        y += gap;
        UIContext::ButtonStyle os;
        os.small = true;
        os.selected = i == index;
        const Rect row{r.x + ui.s(12), y, r.w - ui.s(12), rowH};
        const std::string& text = options[static_cast<size_t>(i)];
        if (!icon) os.fontSize = ui.fitFont(text, FontFace::Bold, 15, std::max(ui.s(20), r.w - ui.s(40)));
        if (ui.button(id + "/opt" + std::to_string(i), row, icon ? std::string() : text, os)) {
            index = i;
            open = false;
        }
        if (icon) {
            // 2.83: its icon, then its text at the left (measured into what is left).
            const float is = std::max(0.0f, rowH - ui.s(12));
            icon(i, {row.x + pad, row.center().y - is * 0.5f, is, is});
            const float tx = row.x + pad + is + ui.s(10);
            fit(ui, text, {tx, row.center().y}, 15, std::max(0.0f, row.right() - pad - tx), kText, FontFace::Bold);
        }
        y += rowH;
    }
    return y;
}

int tabs(UIContext& ui, const std::string& id, float x, float y, float right, const std::vector<std::string>& labels, int selected, float& bottom) {
    int clicked = -1;
    const std::vector<Rect> rects = tabRects(ui, x, y, right, labels, bottom);
    for (size_t i = 0; i < labels.size(); ++i) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = ui.fitFont(labels[i], FontFace::Bold, 14, std::max(ui.s(20), rects[i].w - ui.s(16)));
        st.selected = static_cast<int>(i) == selected;
        if (ui.button(id + "/" + std::to_string(i), rects[i], labels[i], st)) clicked = static_cast<int>(i);
    }
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
