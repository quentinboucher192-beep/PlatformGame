#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Text.h"

namespace cosmic {

class Application;
class InputManager;
class Renderer;

struct UITheme {
    Color background{8, 11, 24, 255};
    Color panel{14, 20, 40, 225};
    Color panelLight{24, 34, 62, 235};
    Color border{57, 240, 216, 70};
    Color text{226, 236, 245, 255};
    Color textDim{140, 160, 185, 255};
    Color accent{57, 240, 216, 255};
    Color accentSoft{57, 240, 216, 50};
    Color warm{255, 179, 71, 255};
    Color danger{255, 77, 109, 255};
    Color success{120, 230, 140, 255};
    Color violet{176, 76, 255, 255};
};

using WidgetId = uint32_t;

/// Immediate-mode UI used by every menu and editor. Widgets are declared each frame;
/// focus navigation works with keyboard arrows, D-pad / stick and mouse, so all screens
/// are fully playable with a gamepad.
class UIContext {
public:
    /// Starts a UI frame. Idempotent within one application frame, so a scene and the
    /// overlay above it share the same frame; only interactive scenes call end().
    void begin(Application& app, float dt);
    void end();

    // ------------------------------------------------------------ helpers
    float scale() const { return scale_; }
    float s(float v) const { return v * scale_; }
    float screenW() const;
    float screenH() const;
    const UITheme& theme() const { return theme_; }
    Renderer& r();
    TextRenderer& text();
    InputManager& input();
    Application& app() { return *app_; }

    WidgetId id(const std::string& name) const;
    void pushId(const std::string& name);
    void popId();

    // --------------------------------------------------------------- draw
    void panel(const Rect& r, bool highlight = false, float radius = 10.0f);
    /// Font size (unscaled) at which `t` fits in `room` pixels, never below minScale * size.
    float fitFont(const std::string& t, FontFace face, float size, float room, float minScale = 0.55f);
    Vec2 label(const std::string& t, Vec2 pos, float size = 24, Color c = {}, Align a = Align::Left,
               FontFace face = FontFace::Body, VAlign va = VAlign::Top);
    Vec2 labelWrapped(const std::string& t, Vec2 pos, float width, float size = 22, Color c = {});
    void separator(Vec2 a, float width);
    void icon(const std::string& texture, int index, int cell, const Rect& dst, Color tint = Color::white());

    // ------------------------------------------------------------ widgets
    struct ButtonStyle {
        bool primary = false;
        bool danger = false;
        bool small = false;
        bool enabled = true;
        bool selected = false;
        float fontSize = 0.0f;
        Align align = Align::Center;
        std::string icon;  // texture path
        int iconIndex = -1;
        int iconCell = 32;
    };
    bool button(const std::string& name, const Rect& r, const std::string& text, const ButtonStyle& st);
    bool button(const std::string& name, const Rect& r, const std::string& text) { return button(name, r, text, ButtonStyle{}); }
    bool toggle(const std::string& name, const Rect& r, const std::string& text, bool& value);
    bool slider(const std::string& name, const Rect& r, const std::string& text, float& value, float minV, float maxV,
                float step, const char* fmt = "%.2f");
    bool sliderInt(const std::string& name, const Rect& r, const std::string& text, int& value, int minV, int maxV);
    bool cycle(const std::string& name, const Rect& r, const std::string& text, int& index,
               const std::vector<std::string>& options);
    bool textField(const std::string& name, const Rect& r, std::string& value, const std::string& placeholder = "",
                   size_t maxLen = 120);
    /// Text field whose content is displayed as dots (passwords).
    bool passwordField(const std::string& name, const Rect& r, std::string& value, const std::string& placeholder = "",
                       size_t maxLen = 128);
    bool numberField(const std::string& name, const Rect& r, float& value, float step, float minV, float maxV,
                     const char* fmt = "%.1f");
    bool listItem(const std::string& name, const Rect& r, const std::string& text, bool selected,
                  const std::string& sub = "", Color accent = {});
    bool tabs(const std::string& name, const Rect& r, const std::vector<std::string>& labels, int& active);

    // Scroll regions
    void beginScroll(const std::string& name, const Rect& view, float contentHeight);
    void endScroll();
    float scrollOffset() const { return scrollStack_.empty() ? 0.0f : scrollStack_.back().offset; }
    /// Moves a scroll region (clamped by the next beginScroll; a huge value = the end).
    void setScroll(const std::string& name, float offset) { scrolls_[id(name)] = offset; }

    // --------------------------------------------------------------- state
    bool focused(const std::string& name) const { return focus_ == id(name); }
    void setFocus(const std::string& name) { focus_ = id(name); }
    /// Focuses a text field and starts editing it (e.g. the chat input after sending).
    void editText(const std::string& name);
    bool hasFocus() const { return focus_ != 0; }
    bool editingText() const { return editing_ != 0; }
    bool mouseOver(const Rect& r) const;
    bool mouseClicked(const Rect& r) const;
    bool backPressed() const;        // menu back (Esc / B)
    bool usingPointer() const { return pointerMode_; }
    float time() const { return time_; }
    /// When false, widgets ignore input (e.g. behind a modal dialog).
    bool inputEnabled = true;
    /// Last widget changed this frame (for undo snapshots in editors).
    bool anyChanged() const { return changed_; }

private:
    struct Focusable {
        WidgetId id;
        Rect rect;
    };
    struct Scroll {
        WidgetId id;
        Rect view;
        float offset;
        float content;
    };
    bool interact(WidgetId wid, const Rect& r, bool* hovered = nullptr);
    bool textFieldImpl(const std::string& name, const Rect& r, std::string& value, const std::string& placeholder, size_t maxLen,
                       bool mask);
    float anim(WidgetId wid, bool on, float speed = 12.0f);
    void registerFocusable(WidgetId wid, const Rect& r);
    bool clippedOut(const Rect& r) const;
    void drawFrame(const Rect& r, float hot, bool focusedNow, bool primary, bool danger, bool enabled, bool selected);

    Application* app_ = nullptr;
    UITheme theme_;
    float scale_ = 1.0f;
    float dt_ = 0.016f;
    float time_ = 0.0f;
    std::vector<std::string> idStack_;
    WidgetId focus_ = 0;
    WidgetId active_ = 0;   // mouse pressed on
    WidgetId editing_ = 0;  // text field being edited
    size_t caret_ = 0;
    bool focusSeen_ = false;
    bool confirm_ = false;
    bool navConsumedH_ = false;
    bool pointerMode_ = true;
    bool changed_ = false;
    std::vector<Focusable> focusables_;
    std::unordered_map<WidgetId, float> anims_;
    std::unordered_map<WidgetId, float> scrolls_;
    std::vector<Scroll> scrollStack_;
    WidgetId scrollToFocus_ = 0;
    uint64_t frame_ = UINT64_MAX;
    bool ended_ = false;
};

}  // namespace cosmic
