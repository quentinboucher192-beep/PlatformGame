#include "ui/widgets/UIContext.h"

#include <cmath>
#include <cstdio>

#include "engine/core/Application.h"

namespace cosmic {

namespace {
WidgetId fnv(const std::string& s, WidgetId seed = 2166136261u) {
    WidgetId h = seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h ? h : 1;
}
}  // namespace

Renderer& UIContext::r() { return app_->renderer(); }
TextRenderer& UIContext::text() { return app_->text(); }
InputManager& UIContext::input() { return app_->input(); }
float UIContext::screenW() const { return static_cast<float>(app_->renderer().width()); }
float UIContext::screenH() const { return static_cast<float>(app_->renderer().height()); }

WidgetId UIContext::id(const std::string& name) const {
    WidgetId h = 2166136261u;
    for (const auto& s : idStack_) h = fnv(s + "/", h);
    return fnv(name, h);
}

void UIContext::pushId(const std::string& name) { idStack_.push_back(name); }
void UIContext::popId() {
    if (!idStack_.empty()) idStack_.pop_back();
}

void UIContext::begin(Application& app, float dt) {
    app_ = &app;
    if (app.frameIndex() == frame_) return;
    frame_ = app.frameIndex();
    ended_ = false;
    dt_ = dt;
    time_ += dt;
    scale_ = std::max(0.5f, std::min(screenW() / 1920.0f, screenH() / 1080.0f));
    focusables_.clear();
    focusSeen_ = false;
    changed_ = false;
    navConsumedH_ = false;
    InputManager& in = app.input();
    if (in.mouseMovedThisFrame() || in.mousePressed(SDL_BUTTON_LEFT) || in.wheel() != 0.0f) pointerMode_ = true;
    if (in.anyMenuInput()) pointerMode_ = false;
    confirm_ = inputEnabled && editing_ == 0 && in.menuPressed(Action::MenuConfirm);
    if (!in.mouseDown(SDL_BUTTON_LEFT) && !in.mouseReleased(SDL_BUTTON_LEFT)) active_ = 0;
}

bool UIContext::backPressed() const {
    return inputEnabled && editing_ == 0 && app_->input().menuPressed(Action::MenuBack) && !app_->input().capturing();
}

bool UIContext::mouseOver(const Rect& rc) const {
    Vec2 m = app_->input().mousePos();
    if (!rc.contains(m)) return false;
    if (!scrollStack_.empty() && !scrollStack_.back().view.contains(m)) return false;
    return true;
}

bool UIContext::mouseClicked(const Rect& rc) const {
    return inputEnabled && mouseOver(rc) && app_->input().mousePressed(SDL_BUTTON_LEFT);
}

void UIContext::registerFocusable(WidgetId wid, const Rect& rc) {
    if (!inputEnabled) return;
    Rect vis = rc;
    if (!scrollStack_.empty()) {
        // Focusable even if scrolled out; the scroll follows the focus.
        if (focus_ == wid && !scrollStack_.back().view.containsRect(rc)) scrollToFocus_ = wid;
    }
    focusables_.push_back({wid, vis});
    if (wid == focus_) focusSeen_ = true;
}

bool UIContext::clippedOut(const Rect& rc) const {
    if (scrollStack_.empty()) return false;
    return !scrollStack_.back().view.intersects(rc);
}

float UIContext::anim(WidgetId wid, bool on, float speed) {
    float& a = anims_[wid];
    a = approach(a, on ? 1.0f : 0.0f, dt_ * speed);
    return a;
}

bool UIContext::interact(WidgetId wid, const Rect& rc, bool* hoveredOut) {
    registerFocusable(wid, rc);
    if (!inputEnabled) {
        if (hoveredOut) *hoveredOut = false;
        return false;
    }
    InputManager& in = app_->input();
    bool hovered = mouseOver(rc) && editing_ == 0;
    if (hovered && pointerMode_ && in.mouseMovedThisFrame()) focus_ = wid;
    if (hoveredOut) *hoveredOut = hovered;
    bool clicked = false;
    if (hovered && in.mousePressed(SDL_BUTTON_LEFT)) {
        active_ = wid;
        focus_ = wid;
    }
    if (active_ == wid && in.mouseReleased(SDL_BUTTON_LEFT) && hovered) clicked = true;
    if (focus_ == wid && confirm_) {
        clicked = true;
        confirm_ = false;
    }
    return clicked;
}

void UIContext::end() {
    if (ended_) return;
    ended_ = true;
    InputManager& in = app_->input();
    if (!focusSeen_) focus_ = 0;
    if (editing_ != 0 || !inputEnabled || focusables_.empty()) return;
    Vec2 dir;
    if (in.menuPressed(Action::MenuUp)) dir = {0, -1};
    else if (in.menuPressed(Action::MenuDown)) dir = {0, 1};
    else if (in.menuPressed(Action::MenuLeft) && !navConsumedH_) dir = {-1, 0};
    else if (in.menuPressed(Action::MenuRight) && !navConsumedH_) dir = {1, 0};
    if (dir.lengthSq() == 0.0f) return;
    if (focus_ == 0) {
        focus_ = focusables_.front().id;
        return;
    }
    Rect cur;
    for (const auto& f : focusables_)
        if (f.id == focus_) cur = f.rect;
    Vec2 c = cur.center();
    WidgetId best = 0;
    float bestScore = 1e30f;
    for (const auto& f : focusables_) {
        if (f.id == focus_) continue;
        Vec2 d = f.rect.center() - c;
        float primary = d.dot(dir);
        if (primary <= 1.0f) continue;
        float ortho = std::abs(dir.x != 0 ? d.y : d.x);
        // Overlapping on the orthogonal axis counts as aligned.
        bool aligned = dir.x != 0 ? (f.rect.y < cur.bottom() && f.rect.bottom() > cur.y)
                                  : (f.rect.x < cur.right() && f.rect.right() > cur.x);
        float score = primary + (aligned ? 0.0f : ortho * 2.5f + 200.0f);
        if (score < bestScore) {
            bestScore = score;
            best = f.id;
        }
    }
    if (best == 0 && dir.y != 0) {
        // Wrap around vertically.
        const Focusable* pick = nullptr;
        for (const auto& f : focusables_) {
            if (!pick) pick = &f;
            if (dir.y > 0 ? f.rect.y < pick->rect.y : f.rect.y > pick->rect.y) pick = &f;
        }
        if (pick) best = pick->id;
    }
    if (best) {
        focus_ = best;
        app_->resources().playSound("sounds/ui_move.wav", 0.5f);
    }
}

// ================================================================ drawing

void UIContext::panel(const Rect& rc, bool highlight, float radius) {
    Renderer& rr = r();
    float rad = s(radius);
    rr.fillRoundedRectGradient(rc, rad, theme_.panelLight, theme_.panel);
    rr.drawRoundedRect(rc, rad, highlight ? theme_.accent.withAlpha(150) : theme_.border, std::max(1.0f, s(1.5f)));
}

Vec2 UIContext::label(const std::string& t, Vec2 pos, float size, Color c, Align a, FontFace face, VAlign va) {
    TextStyle st;
    st.face = face;
    st.size = s(size);
    st.color = c.a == 0 && c.r == 0 && c.g == 0 && c.b == 0 ? theme_.text : c;
    st.align = a;
    st.valign = va;
    return text().draw(t, pos, st);
}

Vec2 UIContext::labelWrapped(const std::string& t, Vec2 pos, float width, float size, Color c) {
    TextStyle st;
    st.size = s(size);
    st.color = c.a == 0 && c.r == 0 && c.g == 0 && c.b == 0 ? theme_.text : c;
    return text().drawWrapped(t, pos, width, st);
}

void UIContext::separator(Vec2 a, float width) {
    r().fillGradientH({a.x, a.y, width, std::max(1.0f, s(1.5f))}, theme_.accent.withAlpha(0), theme_.accent.withAlpha(90));
}

void UIContext::icon(const std::string& texture, int index, int cell, const Rect& dst, Color tint) {
    auto tex = app_->resources().texture(texture);
    if (!tex) return;
    Rect src{static_cast<float>(index * cell), 0, static_cast<float>(cell), static_cast<float>(cell)};
    r().drawTexture(*tex, &src, dst, tint);
}

void UIContext::drawFrame(const Rect& rc, float hot, bool focusedNow, bool primary, bool danger, bool enabled, bool selected) {
    Renderer& rr = r();
    float rad = s(8);
    Color base = primary ? Color{20, 70, 80, 235} : Color{18, 26, 50, 225};
    if (danger) base = {70, 20, 35, 230};
    if (selected) base = Color::lerp(base, theme_.accent.withAlpha(235), 0.25f);
    Color top = Color::lerp(base, Color{40, 70, 110, 240}, hot * 0.7f);
    if (!enabled) top = base = Color{20, 22, 30, 180};
    rr.fillRoundedRectGradient(rc, rad, top, base);
    Color edge = danger ? theme_.danger : (primary ? theme_.accent : theme_.border);
    edge = Color::lerp(edge.withAlpha(primary ? 120 : 70), theme_.accent, hot);
    rr.drawRoundedRect(rc, rad, edge, std::max(1.0f, s(focusedNow ? 2.5f : 1.5f)));
    if (hot > 0.01f) {
        rr.fillRoundedRect({rc.x + s(3), rc.y + s(6), s(4), rc.h - s(12)}, s(2), theme_.accent.withAlphaF(hot));
        rr.drawGlow(rc.center(), rc.w * 0.6f, theme_.accent.withAlphaF(0.10f * hot));
    }
}

// ================================================================ widgets

bool UIContext::button(const std::string& name, const Rect& rc, const std::string& t, const ButtonStyle& st) {
    WidgetId wid = id(name);
    if (clippedOut(rc)) {
        registerFocusable(wid, rc);
        return false;
    }
    bool hovered = false;
    bool clicked = interact(wid, rc, &hovered) && st.enabled;
    bool isFocus = focus_ == wid;
    float hot = anim(wid, (hovered || isFocus) && st.enabled);
    Rect drawn = rc;
    float grow = s(4) * hot;
    drawn.x -= grow * 0.5f;
    drawn.w += grow;
    drawFrame(drawn, hot, isFocus, st.primary, st.danger, st.enabled, st.selected);
    float fs = st.fontSize > 0 ? st.fontSize : (st.small ? 20.0f : 26.0f);
    float textX = st.align == Align::Center ? drawn.center().x : drawn.x + s(20);
    if (!st.icon.empty() && st.iconIndex >= 0) {
        float is = rc.h * 0.62f;
        Rect ir{drawn.x + s(14), drawn.center().y - is * 0.5f, is, is};
        icon(st.icon, st.iconIndex, st.iconCell, ir, st.enabled ? Color::white() : Color{120, 120, 120, 255});
        if (st.align != Align::Center) textX = ir.right() + s(12);
    }
    Color tc = st.enabled ? Color::lerp(theme_.text, Color::white(), hot) : theme_.textDim;
    label(t, {textX, drawn.center().y}, fs, tc, st.align, FontFace::Bold, VAlign::Middle);
    if (clicked) app_->resources().playSound(st.danger ? "sounds/ui_back.wav" : "sounds/ui_confirm.wav", 0.6f);
    return clicked;
}

bool UIContext::toggle(const std::string& name, const Rect& rc, const std::string& t, bool& value) {
    WidgetId wid = id(name);
    bool hovered = false;
    bool clicked = interact(wid, rc, &hovered);
    bool isFocus = focus_ == wid;
    float hot = anim(wid, hovered || isFocus);
    if (isFocus && (input().menuPressed(Action::MenuLeft) || input().menuPressed(Action::MenuRight)) && inputEnabled) {
        clicked = true;
        navConsumedH_ = true;
    }
    if (clicked) {
        value = !value;
        changed_ = true;
        app_->resources().playSound("sounds/ui_move.wav", 0.6f);
    }
    if (clippedOut(rc)) return clicked;
    drawFrame(rc, hot, isFocus, false, false, true, false);
    label(t, {rc.x + s(20), rc.center().y}, 22, theme_.text, Align::Left, FontFace::Body, VAlign::Middle);
    float on = anim(wid ^ 0x9e3779b9u, value, 10.0f);
    Rect track{rc.right() - s(84), rc.center().y - s(13), s(64), s(26)};
    r().fillRoundedRect(track, s(13), Color::lerp(Color{40, 50, 70, 255}, theme_.accent.withAlpha(200), on));
    Vec2 knob{track.x + s(13) + (track.w - s(26)) * on, track.center().y};
    r().fillCircle(knob, s(10), Color::white());
    return clicked;
}

bool UIContext::slider(const std::string& name, const Rect& rc, const std::string& t, float& value, float minV, float maxV,
                       float step, const char* fmt) {
    WidgetId wid = id(name);
    bool hovered = false;
    interact(wid, rc, &hovered);
    bool isFocus = focus_ == wid;
    float hot = anim(wid, hovered || isFocus);
    bool changedNow = false;
    Rect track{rc.x + rc.w * 0.45f, rc.center().y - s(4), rc.w * 0.42f, s(8)};
    InputManager& in = input();
    if (inputEnabled && isFocus) {
        if (in.menuPressed(Action::MenuLeft)) {
            value = std::max(minV, value - step);
            changedNow = true;
            navConsumedH_ = true;
        }
        if (in.menuPressed(Action::MenuRight)) {
            value = std::min(maxV, value + step);
            changedNow = true;
            navConsumedH_ = true;
        }
    }
    if (inputEnabled && active_ == wid && in.mouseDown(SDL_BUTTON_LEFT)) {
        float k = std::clamp((in.mousePos().x - track.x) / track.w, 0.0f, 1.0f);
        float v = minV + k * (maxV - minV);
        if (step > 0) v = std::round(v / step) * step;
        v = std::clamp(v, minV, maxV);
        if (v != value) {
            value = v;
            changedNow = true;
        }
    }
    if (changedNow) changed_ = true;
    if (clippedOut(rc)) return changedNow;
    drawFrame(rc, hot, isFocus, false, false, true, false);
    label(t, {rc.x + s(20), rc.center().y}, 22, theme_.text, Align::Left, FontFace::Body, VAlign::Middle);
    float k = maxV > minV ? (value - minV) / (maxV - minV) : 0.0f;
    r().fillRoundedRect(track, s(4), {40, 50, 70, 255});
    r().fillRoundedRect({track.x, track.y, track.w * k, track.h}, s(4), theme_.accent);
    r().fillCircle({track.x + track.w * k, track.center().y}, s(9) + s(3) * hot, Color::white());
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, value);
    label(buf, {rc.right() - s(16), rc.center().y}, 20, theme_.textDim, Align::Right, FontFace::Body, VAlign::Middle);
    return changedNow;
}

bool UIContext::sliderInt(const std::string& name, const Rect& rc, const std::string& t, int& value, int minV, int maxV) {
    float v = static_cast<float>(value);
    bool c = slider(name, rc, t, v, static_cast<float>(minV), static_cast<float>(maxV), 1.0f, "%.0f");
    value = static_cast<int>(std::lround(v));
    return c;
}

bool UIContext::cycle(const std::string& name, const Rect& rc, const std::string& t, int& index,
                      const std::vector<std::string>& options) {
    if (options.empty()) return false;
    WidgetId wid = id(name);
    bool hovered = false;
    bool clicked = interact(wid, rc, &hovered);
    bool isFocus = focus_ == wid;
    float hot = anim(wid, hovered || isFocus);
    int n = static_cast<int>(options.size());
    bool changedNow = false;
    InputManager& in = input();
    Rect left{rc.x + rc.w * 0.45f, rc.y, s(36), rc.h};
    Rect right{rc.right() - s(44), rc.y, s(36), rc.h};
    if (inputEnabled && isFocus && in.menuPressed(Action::MenuLeft)) {
        index = (index - 1 + n) % n;
        changedNow = true;
        navConsumedH_ = true;
    } else if (inputEnabled && isFocus && in.menuPressed(Action::MenuRight)) {
        index = (index + 1) % n;
        changedNow = true;
        navConsumedH_ = true;
    } else if (clicked) {
        bool leftSide = mouseOver(left) || (pointerMode_ && in.mousePos().x < rc.x + rc.w * 0.7f && mouseOver(rc));
        index = leftSide && pointerMode_ ? (index - 1 + n) % n : (index + 1) % n;
        changedNow = true;
    }
    index = std::clamp(index, 0, n - 1);
    if (changedNow) {
        changed_ = true;
        app_->resources().playSound("sounds/ui_move.wav", 0.6f);
    }
    if (clippedOut(rc)) return changedNow;
    drawFrame(rc, hot, isFocus, false, false, true, false);
    label(t, {rc.x + s(20), rc.center().y}, 22, theme_.text, Align::Left, FontFace::Body, VAlign::Middle);
    label("<", left.center(), 24, Color::lerp(theme_.textDim, theme_.accent, hot), Align::Center, FontFace::Bold,
          VAlign::Middle);
    label(">", right.center(), 24, Color::lerp(theme_.textDim, theme_.accent, hot), Align::Center, FontFace::Bold,
          VAlign::Middle);
    label(options[index], {(left.right() + right.x) * 0.5f, rc.center().y}, 22, theme_.text, Align::Center,
          FontFace::Bold, VAlign::Middle);
    return changedNow;
}

bool UIContext::textField(const std::string& name, const Rect& rc, std::string& value, const std::string& placeholder,
                          size_t maxLen) {
    WidgetId wid = id(name);
    bool hovered = false;
    bool clicked = interact(wid, rc, &hovered);
    bool isFocus = focus_ == wid;
    InputManager& in = input();
    bool changedNow = false;
    if (clicked && editing_ != wid) {
        editing_ = wid;
        in.startTextInput(app_->window());
    } else if (editing_ == wid) {
        if (!in.textInput().empty()) {
            for (char c : in.textInput())
                if (value.size() < maxLen && static_cast<unsigned char>(c) >= 32) value.push_back(c);
            changedNow = true;
        }
        if (in.keyPressed(SDL_SCANCODE_BACKSPACE) && !value.empty()) {
            // Remove a full UTF-8 code point.
            size_t i = value.size() - 1;
            while (i > 0 && (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80) --i;
            value.erase(i);
            changedNow = true;
        }
        if (in.ctrl() && in.keyPressed(SDL_SCANCODE_V)) {
            if (char* clip = SDL_GetClipboardText()) {
                for (const char* p = clip; *p && value.size() < maxLen; ++p)
                    if (static_cast<unsigned char>(*p) >= 32) value.push_back(*p);
                SDL_free(clip);
                changedNow = true;
            }
        }
        bool clickedOutside = in.mousePressed(SDL_BUTTON_LEFT) && !mouseOver(rc);
        if (in.keyPressed(SDL_SCANCODE_RETURN) || in.keyPressed(SDL_SCANCODE_ESCAPE) || in.keyPressed(SDL_SCANCODE_TAB) ||
            clickedOutside || !inputEnabled) {
            editing_ = 0;
            in.stopTextInput(app_->window());
        }
    }
    if (changedNow) changed_ = true;
    float hot = anim(wid, hovered || isFocus || editing_ == wid);
    if (clippedOut(rc)) return changedNow;
    Renderer& rr = r();
    rr.fillRoundedRect(rc, s(6), editing_ == wid ? Color{10, 16, 32, 255} : Color{14, 20, 38, 235});
    rr.drawRoundedRect(rc, s(6), editing_ == wid ? theme_.accent : Color::lerp(theme_.border, theme_.accent, hot * 0.6f),
                       std::max(1.0f, s(1.5f)));
    rr.pushClip(rc.expanded(-s(4)));
    std::string shown = value.empty() && editing_ != wid ? placeholder : value;
    Color c = value.empty() && editing_ != wid ? theme_.textDim : theme_.text;
    Vec2 sz = label(shown, {rc.x + s(10), rc.center().y}, 20, c, Align::Left, FontFace::Body, VAlign::Middle);
    if (editing_ == wid && std::fmod(time_, 1.0f) < 0.6f) {
        float cx = rc.x + s(10) + (value.empty() ? 0.0f : sz.x) + s(2);
        rr.fillRect({cx, rc.center().y - s(11), s(2), s(22)}, theme_.accent);
    }
    rr.popClip();
    return changedNow;
}

bool UIContext::numberField(const std::string& name, const Rect& rc, float& value, float step, float minV, float maxV,
                            const char* fmt) {
    WidgetId wid = id(name);
    bool hovered = false;
    interact(wid, rc, &hovered);
    bool isFocus = focus_ == wid;
    float hot = anim(wid, hovered || isFocus);
    InputManager& in = input();
    bool changedNow = false;
    Rect minus{rc.x, rc.y, rc.h, rc.h}, plus{rc.right() - rc.h, rc.y, rc.h, rc.h};
    float mult = in.shift() ? 10.0f : (in.ctrl() ? 0.1f : 1.0f);
    if (inputEnabled) {
        if ((isFocus && in.menuPressed(Action::MenuLeft)) || (mouseClicked(minus))) {
            value = std::max(minV, value - step * mult);
            changedNow = true;
            navConsumedH_ = navConsumedH_ || isFocus;
        }
        if ((isFocus && in.menuPressed(Action::MenuRight)) || (mouseClicked(plus))) {
            value = std::min(maxV, value + step * mult);
            changedNow = true;
            navConsumedH_ = navConsumedH_ || isFocus;
        }
        if (hovered && in.wheel() != 0.0f) {
            value = std::clamp(value + step * mult * (in.wheel() > 0 ? 1.0f : -1.0f), minV, maxV);
            changedNow = true;
        }
        // Drag horizontally on the value to scrub.
        if (active_ == wid && in.mouseDown(SDL_BUTTON_LEFT) && std::abs(in.mouseDelta().x) > 0.0f &&
            !minus.contains(in.mousePos()) && !plus.contains(in.mousePos())) {
            value = std::clamp(value + in.mouseDelta().x * step * 0.25f * mult, minV, maxV);
            changedNow = true;
        }
    }
    if (changedNow) changed_ = true;
    if (clippedOut(rc)) return changedNow;
    Renderer& rr = r();
    rr.fillRoundedRect(rc, s(6), Color::lerp(Color{14, 20, 38, 235}, Color{24, 36, 64, 245}, hot));
    rr.drawRoundedRect(rc, s(6), Color::lerp(theme_.border, theme_.accent, hot), std::max(1.0f, s(1.5f)));
    label("-", minus.center(), 22, theme_.textDim, Align::Center, FontFace::Bold, VAlign::Middle);
    label("+", plus.center(), 22, theme_.textDim, Align::Center, FontFace::Bold, VAlign::Middle);
    char buf[48];
    std::snprintf(buf, sizeof(buf), fmt, value);
    label(buf, rc.center(), 20, theme_.text, Align::Center, FontFace::Body, VAlign::Middle);
    return changedNow;
}

bool UIContext::listItem(const std::string& name, const Rect& rc, const std::string& t, bool selected,
                         const std::string& sub, Color accent) {
    WidgetId wid = id(name);
    bool hovered = false;
    bool clicked = interact(wid, rc, &hovered);
    if (clippedOut(rc)) return clicked;
    bool isFocus = focus_ == wid;
    float hot = anim(wid, hovered || isFocus);
    Renderer& rr = r();
    Color bg = selected ? Color{30, 70, 90, 235} : Color{16, 24, 46, 200};
    rr.fillRoundedRect(rc, s(6), Color::lerp(bg, Color{36, 56, 96, 240}, hot * 0.6f));
    if (selected || hot > 0.01f)
        rr.drawRoundedRect(rc, s(6), theme_.accent.withAlphaF(selected ? 0.8f : 0.5f * hot), std::max(1.0f, s(1.5f)));
    if (accent.a) rr.fillRoundedRect({rc.x + s(4), rc.y + s(6), s(4), rc.h - s(12)}, s(2), accent);
    float x = rc.x + s(16);
    if (sub.empty()) {
        label(t, {x, rc.center().y}, 21, theme_.text, Align::Left, FontFace::Body, VAlign::Middle);
    } else {
        label(t, {x, rc.y + rc.h * 0.3f}, 21, theme_.text, Align::Left, FontFace::Bold, VAlign::Middle);
        label(sub, {x, rc.y + rc.h * 0.72f}, 16, theme_.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    if (clicked) app_->resources().playSound("sounds/ui_move.wav", 0.5f);
    return clicked;
}

bool UIContext::tabs(const std::string& name, const Rect& rc, const std::vector<std::string>& labels, int& active) {
    if (labels.empty()) return false;
    int n = static_cast<int>(labels.size());
    bool changedNow = false;
    InputManager& in = input();
    if (inputEnabled && editing_ == 0) {
        if (in.menuPressed(Action::MenuTabPrev)) {
            active = (active - 1 + n) % n;
            changedNow = true;
        }
        if (in.menuPressed(Action::MenuTabNext)) {
            active = (active + 1) % n;
            changedNow = true;
        }
    }
    float w = rc.w / n;
    for (int i = 0; i < n; ++i) {
        Rect tr{rc.x + i * w + s(2), rc.y, w - s(4), rc.h};
        WidgetId wid = id(name + "#" + std::to_string(i));
        bool hovered = mouseOver(tr);
        if (inputEnabled && hovered && in.mousePressed(SDL_BUTTON_LEFT)) {
            active = i;
            changedNow = true;
        }
        float hot = anim(wid, hovered || i == active);
        Renderer& rr = r();
        rr.fillRoundedRect(tr, s(6), Color::lerp(Color{16, 22, 42, 220}, Color{28, 64, 84, 240}, i == active ? 1.0f : hot * 0.4f));
        if (i == active) rr.fillRect({tr.x + s(8), tr.bottom() - s(3), tr.w - s(16), s(3)}, theme_.accent);
        float fs = 21.0f;
        float tw = text().measure(labels[i], FontFace::Bold, s(fs)).x;
        if (tw > tr.w - s(10)) fs *= std::max(0.55f, (tr.w - s(10)) / tw);
        label(labels[i], tr.center(), fs, i == active ? Color::white() : theme_.textDim, Align::Center, FontFace::Bold,
              VAlign::Middle);
    }
    if (changedNow) {
        app_->resources().playSound("sounds/ui_move.wav", 0.6f);
        focus_ = 0;
    }
    return changedNow;
}

void UIContext::beginScroll(const std::string& name, const Rect& view, float contentHeight) {
    WidgetId wid = id(name);
    float& off = scrolls_[wid];
    float maxOff = std::max(0.0f, contentHeight - view.h);
    InputManager& in = input();
    if (inputEnabled && view.contains(in.mousePos()) && in.wheel() != 0.0f) off -= in.wheel() * s(60);
    off = std::clamp(off, 0.0f, maxOff);
    scrollStack_.push_back({wid, view, off, contentHeight});
    r().pushClip(view);
}

void UIContext::endScroll() {
    if (scrollStack_.empty()) return;
    Scroll sc = scrollStack_.back();
    scrollStack_.pop_back();
    r().popClip();
    // Keep the focused widget visible.
    if (scrollToFocus_ != 0) {
        for (const auto& f : focusables_) {
            if (f.id != scrollToFocus_) continue;
            float& off = scrolls_[sc.id];
            if (f.rect.y < sc.view.y) off -= sc.view.y - f.rect.y + s(8);
            if (f.rect.bottom() > sc.view.bottom()) off += f.rect.bottom() - sc.view.bottom() + s(8);
            off = std::clamp(off, 0.0f, std::max(0.0f, sc.content - sc.view.h));
        }
        scrollToFocus_ = 0;
    }
    // Scrollbar
    if (sc.content > sc.view.h) {
        float k = sc.view.h / sc.content;
        float barH = std::max(s(30), sc.view.h * k);
        float y = sc.view.y + (sc.view.h - barH) * (sc.offset / std::max(1.0f, sc.content - sc.view.h));
        r().fillRoundedRect({sc.view.right() - s(6), y, s(4), barH}, s(2), theme_.accent.withAlpha(120));
    }
}

}  // namespace cosmic
