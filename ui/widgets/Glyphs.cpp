#include "ui/widgets/Glyphs.h"

#include "engine/core/Application.h"
#include "game/powers/Power.h"
#include "game/powers/PowerKeys.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::Glyphs {

const char* sheetFor(GamepadStyle style) {
    switch (style) {
        case GamepadStyle::Xbox: return "ui/glyphs/xbox.png";
        case GamepadStyle::PlayStation: return "ui/glyphs/playstation.png";
        case GamepadStyle::Generic: return "ui/glyphs/generic.png";
        case GamepadStyle::Switch: return "ui/glyphs/generic.png";  // places, not letters: its names say B / A
    }
    return "ui/glyphs/generic.png";
}

float drawKeycap(UIContext& ui, const std::string& label, Vec2 pos, float height) {
    TextRenderer& t = ui.text();
    float fs = height * 0.55f;
    Vec2 sz = t.measure(label, FontFace::Bold, fs);
    float w = std::max(height, sz.x + height * 0.6f);
    Rect r{pos.x, pos.y - height * 0.5f, w, height};
    Renderer& rr = ui.r();
    rr.fillRoundedRectGradient(r, height * 0.2f, {70, 84, 110, 255}, {38, 46, 66, 255});
    rr.fillRoundedRect({r.x + 1, r.bottom() - height * 0.14f, r.w - 2, height * 0.12f}, height * 0.1f, {20, 24, 36, 255});
    rr.drawRoundedRect(r, height * 0.2f, {150, 170, 200, 200}, std::max(1.0f, height * 0.05f));
    TextStyle st;
    st.face = FontFace::Bold;
    st.size = fs;
    st.color = {235, 242, 250, 255};
    st.align = Align::Center;
    st.valign = VAlign::Middle;
    t.draw(label, {r.center().x, r.center().y - height * 0.04f}, st);
    return w;
}

float drawPadButton(UIContext& ui, const PadBinding& b, Vec2 pos, float height) { return drawPadButton(ui, b, pos, height, ui.input().gamepadStyle()); }

float drawPadButton(UIContext& ui, const PadBinding& b, Vec2 pos, float height, GamepadStyle style) {
    int idx = glyphIndex(b);
    if (idx < 0) return drawKeycap(ui, padBindingName(b, style), pos, height);
    ui.icon(sheetFor(style), idx, 32, {pos.x, pos.y - height * 0.5f, height, height});
    return height;
}

float drawAction(UIContext& ui, Action a, Vec2 pos, float height) {
    const InputManager& in = ui.input();
    const InputBindings& b = in.bindings();
    int i = static_cast<int>(a);
    if (i < 0 || i >= kActionCount) return drawKeycap(ui, keyName(SDL_SCANCODE_UNKNOWN), pos, height);  // no key: « — »
    if (in.lastDevice() == InputDevice::Gamepad && in.hasGamepad()) {
        const PadBinding& pb = b.pad[i][0].kind != PadBinding::Kind::None ? b.pad[i][0] : b.pad[i][1];
        return drawPadButton(ui, pb, pos, height);
    }
    SDL_Scancode sc = b.keys[i][0] != SDL_SCANCODE_UNKNOWN ? b.keys[i][0] : b.keys[i][1];
    return drawKeycap(ui, keyName(sc), pos, height);
}

namespace {
/// The binding drawAction shows: the first one of the device in use.
struct Shown {
    bool pad = false;
    PadBinding pb;
    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
};
Shown shownBinding(UIContext& ui, Action a) {
    const InputManager& in = ui.input();
    const InputBindings& b = in.bindings();
    int i = static_cast<int>(a);
    Shown s;
    if (i < 0 || i >= kActionCount) return s;
    if (in.lastDevice() == InputDevice::Gamepad && in.hasGamepad()) {
        s.pad = true;
        s.pb = b.pad[i][0].kind != PadBinding::Kind::None ? b.pad[i][0] : b.pad[i][1];
    } else {
        s.sc = b.keys[i][0] != SDL_SCANCODE_UNKNOWN ? b.keys[i][0] : b.keys[i][1];
    }
    return s;
}

float keycapWidth(UIContext& ui, const std::string& label, float height) {
    Vec2 sz = ui.text().measure(label, FontFace::Bold, height * 0.55f);
    return std::max(height, sz.x + height * 0.6f);
}
}  // namespace

float measureAction(UIContext& ui, Action a, float height) {
    Shown s = shownBinding(ui, a);
    if (s.pad) {
        if (glyphIndex(s.pb) >= 0) return height;
        return keycapWidth(ui, padBindingName(s.pb, ui.input().gamepadStyle()), height);
    }
    return keycapWidth(ui, keyName(s.sc), height);
}

float drawKeys(UIContext& ui, const std::vector<Action>& keys, Vec2 pos, float height, bool draw) {
    const float plusW = ui.text().measure("+", FontFace::Bold, height * 0.6f).x;
    const float gap = height * 0.18f;
    float x = pos.x;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            if (draw) {
                TextStyle st;
                st.face = FontFace::Bold;
                st.size = height * 0.6f;
                st.color = ui.theme().text;
                st.valign = VAlign::Middle;
                ui.text().draw("+", {x + gap, pos.y}, st);
            }
            x += gap * 2.0f + plusW;
        }
        x += draw ? drawAction(ui, keys[i], {x, pos.y}, height) : measureAction(ui, keys[i], height);
    }
    return x - pos.x;
}

Color freePowerColor() { return {255, 196, 92, 255}; }

void drawFreePowerFrame(UIContext& ui, const Rect& r, float radius, float thickness, float alpha) {
    Renderer& rr = ui.r();
    Color c = freePowerColor().withAlphaF(alpha);
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    // Corners: quarter arcs; straight edges: dashes, spread evenly between the corners.
    const float pi = 3.14159265f;
    float half = thickness * 0.5f;
    rr.drawArc({r.right() - radius, r.y + radius}, radius - half, thickness, -pi * 0.5f, 0.0f, c);
    rr.drawArc({r.right() - radius, r.bottom() - radius}, radius - half, thickness, 0.0f, pi * 0.5f, c);
    rr.drawArc({r.x + radius, r.bottom() - radius}, radius - half, thickness, pi * 0.5f, pi, c);
    rr.drawArc({r.x + radius, r.y + radius}, radius - half, thickness, pi, pi * 1.5f, c);
    auto dashes = [&](Vec2 a, Vec2 b, bool horizontal) {
        float len = horizontal ? b.x - a.x : b.y - a.y;
        if (len <= 0.0f) return;
        float dash = std::max(3.0f, thickness * 3.0f), space = std::max(2.0f, thickness * 2.2f);
        int n = std::max(1, static_cast<int>((len + space) / (dash + space)));
        float step = (len + space) / static_cast<float>(n);
        float d = step - space;
        for (int k = 0; k < n; ++k) {
            float o = k * step;
            Rect seg = horizontal ? Rect{a.x + o, a.y - half, d, thickness} : Rect{a.x - half, a.y + o, thickness, d};
            rr.fillRect(seg, c);
        }
    };
    float gap = std::max(2.0f, thickness * 1.5f);  // between an arc and the first dash
    dashes({r.x + radius + gap, r.y + half}, {r.right() - radius - gap, r.y + half}, true);
    dashes({r.x + radius + gap, r.bottom() - half}, {r.right() - radius - gap, r.bottom() - half}, true);
    dashes({r.x + half, r.y + radius + gap}, {r.x + half, r.bottom() - radius - gap}, false);
    dashes({r.right() - half, r.y + radius + gap}, {r.right() - half, r.bottom() - radius - gap}, false);
}

float drawFreePowerUse(UIContext& ui, const std::vector<Action>& keys, const std::string& text, Vec2 pos, float height, float alpha,
                       bool draw) {
    const float pad = height * 0.4f, fs = height * 0.62f;
    float keysW = keys.empty() ? 0.0f : drawKeys(ui, keys, {0.0f, 0.0f}, height, false) + (text.empty() ? 0.0f : height * 0.3f);
    float textW = text.empty() ? 0.0f : ui.text().measure(text, FontFace::Body, fs).x;
    float w = pad * 2.0f + keysW + textW;
    if (!draw) return w;
    Rect badge{pos.x, pos.y - height * 0.7f, w, height * 1.4f};
    ui.r().fillRoundedRect(badge, height * 0.3f, Color{8, 14, 30, 210}.withAlphaF(alpha));
    drawFreePowerFrame(ui, badge, height * 0.3f, std::max(1.5f, height * 0.05f), alpha);
    float x = badge.x + pad;
    if (!keys.empty()) x += drawKeys(ui, keys, {x, pos.y}, height) + (text.empty() ? 0.0f : height * 0.3f);
    if (!text.empty()) {
        TextStyle st;
        st.size = fs;
        st.color = ui.theme().text.withAlphaF(alpha);
        st.valign = VAlign::Middle;
        st.shadow = true;
        ui.text().draw(text, {x, pos.y}, st);
    }
    return w;
}

std::string keyAndButton(const InputManager& in, Action a) {
    return actionKeyName(in.bindings(), a) + " / " + actionPadName(in.bindings(), a, in.gamepadStyle());
}

bool showsPad(UIContext& ui) {
    const InputManager& in = ui.input();
    return in.lastDevice() == InputDevice::Gamepad && in.hasGamepad();
}

float drawBinding(UIContext& ui, SDL_Scancode key, const PadBinding& padBinding, bool pad, Vec2 pos, float height, bool draw) {
    if (pad) {
        if (padBinding.kind != PadBinding::Kind::None && glyphIndex(padBinding) >= 0) {
            if (draw) drawPadButton(ui, padBinding, pos, height);
            return height;
        }
        const std::string name = padBinding.kind == PadBinding::Kind::None ? keyName(SDL_SCANCODE_UNKNOWN) : padBindingName(padBinding, ui.input().gamepadStyle());
        return draw ? drawKeycap(ui, name, pos, height) : keycapWidth(ui, name, height);
    }
    return draw ? drawKeycap(ui, keyName(key), pos, height) : keycapWidth(ui, keyName(key), height);
}

float drawPowerKey(UIContext& ui, const ResolvedPowerKey& key, Vec2 pos, float height, bool draw) {
    return drawBinding(ui, key.key, key.pad, showsPad(ui), pos, height, draw);
}

float drawPowerUse(UIContext& ui, const PowerDefinition& def, const PowerKeyMap& keys, Vec2 pos, float height, float alpha, bool draw,
                   const std::string& passiveText) {
    const PowerTriggerInfo use = powerTriggerInfo(def);
    if (use.kind == PowerTrigger::SlotKey) {
        ResolvedPowerKey none;
        const ResolvedPowerKey* k = keys.find(def.id);
        return drawPowerKey(ui, k ? *k : none, pos, height, draw);
    }
    const std::string t = use.kind == PowerTrigger::Passive ? passiveText : "";
    return drawFreePowerUse(ui, use.keys, t, pos, height, alpha, draw);
}

float drawHint(UIContext& ui, Action a, const std::string& text, Vec2 pos, float height) {
    float w = drawAction(ui, a, pos, height);
    TextStyle st;
    st.size = height * 0.62f;
    st.color = ui.theme().text;
    st.valign = VAlign::Middle;
    st.shadow = true;
    Vec2 sz = ui.text().draw(text, {pos.x + w + height * 0.3f, pos.y}, st);
    return w + height * 0.3f + sz.x;
}

}  // namespace cosmic::Glyphs
