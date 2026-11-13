#include "ui/widgets/Glyphs.h"

#include "engine/core/Application.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::Glyphs {

const char* sheetFor(GamepadStyle style) {
    switch (style) {
        case GamepadStyle::Xbox: return "ui/glyphs/xbox.png";
        case GamepadStyle::PlayStation: return "ui/glyphs/playstation.png";
        case GamepadStyle::Generic: return "ui/glyphs/generic.png";
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

float drawPadButton(UIContext& ui, const PadBinding& b, Vec2 pos, float height) {
    int idx = glyphIndex(b);
    GamepadStyle style = ui.input().gamepadStyle();
    if (idx < 0) return drawKeycap(ui, padBindingName(b, style), pos, height);
    ui.icon(sheetFor(style), idx, 32, {pos.x, pos.y - height * 0.5f, height, height});
    return height;
}

float drawAction(UIContext& ui, Action a, Vec2 pos, float height) {
    const InputManager& in = ui.input();
    const InputBindings& b = in.bindings();
    int i = static_cast<int>(a);
    if (in.lastDevice() == InputDevice::Gamepad && in.hasGamepad()) {
        const PadBinding& pb = b.pad[i][0].kind != PadBinding::Kind::None ? b.pad[i][0] : b.pad[i][1];
        return drawPadButton(ui, pb, pos, height);
    }
    SDL_Scancode sc = b.keys[i][0] != SDL_SCANCODE_UNKNOWN ? b.keys[i][0] : b.keys[i][1];
    return drawKeycap(ui, keyName(sc), pos, height);
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
