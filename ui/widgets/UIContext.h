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
    /// `t` shortened with « … » (on a character boundary) so that it fits `room` at `size`.
    std::string elide(const std::string& t, FontFace face, float size, float room);
    Vec2 label(const std::string& t, Vec2 pos, float size = 24, Color c = {}, Align a = Align::Left,
               FontFace face = FontFace::Body, VAlign va = VAlign::Top);
    Vec2 labelWrapped(const std::string& t, Vec2 pos, float width, float size = 22, Color c = {});
    void separator(Vec2 a, float width);
    void icon(const std::string& texture, int index, int cell, const Rect& dst, Color tint = Color::white());
    /// The EXTRÊME badge (one life), `height` pixels high, its left edge centred on
    /// `leftMid`; returns its width (0 when nothing is drawn).
    float extremeBadge(Vec2 leftMid, float height, float alpha = 1.0f);

    // ------------------------------------------------------------ widgets
    struct ButtonStyle {
        bool primary = false;
        bool danger = false;
        bool small = false;
        bool enabled = true;
        bool selected = false;
        /// The key / button this cell shows is held right now (lists of bindings): lit with
        /// heldHighlight and a white label; same size and place.
        bool lit = false;
        float fontSize = 0.0f;
        Align align = Align::Center;
        std::string icon;  // texture path
        int iconIndex = -1;
        int iconCell = 32;
    };
    bool button(const std::string& name, const Rect& r, const std::string& text, const ButtonStyle& st);
    /// The mark of a key held right now in a list of bindings: an accent fill, glow and edge
    /// drawn inside `r` only (nothing moves, nothing grows). Draw it under the label.
    void heldHighlight(const Rect& r);
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
    /// A focusable, clickable area drawn by the caller (map nodes, web nodes...): takes part
    /// in keyboard / gamepad navigation like a button. Returns true when activated.
    bool hotspot(const std::string& name, const Rect& r, bool* hovered = nullptr);
    /// A mouse-only click zone (no keyboard / gamepad stop): a player's avatar anywhere opens
    /// their profile. Called after a larger button under it, it wins the click.
    bool mouseClick(const std::string& name, const Rect& r, bool* hovered = nullptr);
    bool listItem(const std::string& name, const Rect& r, const std::string& text, bool selected,
                  const std::string& sub = "", Color accent = {});
    /// `badges` (optional, one per tab): a counter shown in a small pill at the tab's right,
    /// the label being fitted to the room left (0: no pill). Positive: something waits for
    /// an answer (red pill); negative: a plain count, shown as its absolute value (accent).
    bool tabs(const std::string& name, const Rect& r, const std::vector<std::string>& labels, int& active, const std::vector<int>& badges = {});

    // Scroll regions. Their bar (at the right of the view, when the content is taller) is
    // held with the mouse: grabbed and dragged, or clicked in its lane to jump there (2.39).
    void beginScroll(const std::string& name, const Rect& view, float contentHeight);
    void endScroll();
    float scrollOffset() const { return scrollStack_.empty() ? 0.0f : scrollStack_.back().offset; }
    /// Moves a scroll region (clamped by the next beginScroll; a huge value = the end).
    void setScroll(const std::string& name, float offset) { scrolls_[id(name)] = offset; }
    /// 2.77.1: a widget that uses the wheel itself over `r` (a diagram's zoom, a legend scrolled in a scrolled
    /// page): from the next frame on, a larger scroll region under it does not scroll with the wheel there (the
    /// regions begin before the widgets inside them are drawn). Call each frame the pointer is over it.
    void claimWheel(const Rect& r) { wheelClaims_.push_back(r); }

    // --------------------------------------------------------------- state
    bool focused(const std::string& name) const { return focus_ == id(name); }
    void setFocus(const std::string& name) { focus_ = id(name); }
    /// Focuses a text field and starts editing it (e.g. the chat input after sending).
    void editText(const std::string& name);
    bool hasFocus() const { return focus_ != 0; }
    /// Whether a widget of that name (full id path) is one of the keyboard / gamepad stops of the
    /// frame drawn last (tests: a button that must be there).
    bool navigable(const std::string& name) const {
        const WidgetId w = id(name);
        for (const auto& f : focusables_)
            if (f.id == w) return true;
        return false;
    }
    bool editingText() const { return editing_ != 0; }
    bool mouseOver(const Rect& r) const;
    bool mouseClicked(const Rect& r) const;
    /// A widget drawn by the caller used this frame's arrows itself (a marker moved on a map):
    /// the focus does not move that way.
    void consumeNavigation(bool horizontal, bool vertical) {
        navConsumedH_ = navConsumedH_ || horizontal;
        navConsumedV_ = navConsumedV_ || vertical;
    }
    bool backPressed() const;        // menu back (Esc / B)
    bool usingPointer() const { return pointerMode_; }
    float time() const { return time_; }
    /// When false, widgets ignore input (e.g. behind a modal dialog).
    bool inputEnabled = true;
    /// False while a scene covered by another one is drawn (set by the application before
    /// each scene): nothing under an overlay reacts to clicks, keys or the gamepad.
    bool sceneInput = true;
    bool acceptsInput() const { return inputEnabled && sceneInput; }
    /// A read-only form (2.45: a native entity in the enemy editor): the widgets that edit a
    /// value (toggles, sliders, cycles, text and number fields, buttons) are drawn greyed and
    /// change nothing, but still take the focus (keyboard and gamepad reach every field, the
    /// scroll follows); list items and tabs work as usual (choosing what to look at).
    bool readOnly = false;
    /// Last widget changed this frame (for undo snapshots in editors).
    bool anyChanged() const { return changed_; }

    // ----------------------------------------------------------- « NOUVEAU »
    /// Pill « NOUVEAU » on the widget `name` (already drawn at `r`) while the novelty
    /// `feature` is new (data/news/new_features.json), on its top edge at the right. A novelty
    /// goes for good as soon as the widget gets the focus (keyboard, gamepad, mouse hover) or a
    /// click; the focus a screen gives on opening does not count: the player has to come to it.
    /// A parent (a menu entry, a tab, a category…) keeps its pill, focused or not, as long as
    /// anything under it is new. `small`: the pill of crowded places (rows, cards, nodes).
    /// Returns true while the pill is due (drawn, or to be drawn by the caller: `drawPill` false).
    bool newBadge(const std::string& name, const Rect& r, const std::string& feature, bool drawPill = true, bool small = false);
    /// Whether `feature` (a novelty, or a parent with something new under it) is new.
    bool isNew(const std::string& feature) const { return isNewFeature && !feature.empty() && isNewFeature(feature); }
    /// Draws a « NOUVEAU » pill whose right edge is at `right`, centred on `y` (lists, rows).
    /// `small`: smaller (crowded places), its text never under 11.5 pixels.
    Rect drawNewPill(float right, float y, bool small = false);
    /// 2.84: a small « NOUVEAU » pill centred on `c`, at most `maxW` wide (its word fitted to it).
    Rect drawNewPillIn(Vec2 c, float maxW);
    /// 2.84 (tests): the « NOUVEAU » pills drawn since the frame began.
    int newPillsDrawn() const { return pillsDrawn_; }
    float newPillWidth(bool small = false);
    float newPillHeight(bool small = false) const;
    std::function<bool(const std::string&)> isNewFeature;
    std::function<void(const std::string&)> featureSeen;

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
    // The edited field's caret and selection (byte offsets on UTF-8 boundaries): the
    // selection runs from anchor_ to caret_ (empty when equal).
    size_t caret_ = 0;
    size_t anchor_ = 0;
    bool dragSelect_ = false;
    bool focusSeen_ = false;
    bool confirm_ = false;
    bool navConsumedH_ = false;
    bool navConsumedV_ = false;
    bool pointerMode_ = true;
    bool changed_ = false;
    std::vector<Focusable> focusables_;
    std::unordered_map<WidgetId, float> anims_;
    // « NOUVEAU »: 1: may be seen on focus, 2: had the focus at first sight (or was just seen);
    // with the frame it was last drawn (a widget back on screen is at first sight again).
    struct BadgeState {
        uint8_t armed = 0;
        uint64_t frame = 0;
    };
    std::unordered_map<WidgetId, BadgeState> badgeArmed_;
    float newPillFont(bool small) const;
    std::unordered_map<WidgetId, float> scrolls_;
    std::vector<Scroll> scrollStack_;
    std::vector<Rect> wheelClaims_, wheelClaimed_;  // 2.77.1: this frame's, the previous frame's (claimWheel)
    WidgetId scrollToFocus_ = 0;
    // The scroll bar held by the mouse (2.39): its region, and where the thumb was grabbed.
    WidgetId scrollDrag_ = 0;
    float scrollGrab_ = 0.0f;
    // The focus its scroll region was last brought to (followed once per new focus).
    WidgetId followedFocus_ = 0;
    WidgetId lastFocus_ = 0;
    uint64_t frame_ = UINT64_MAX;
    int pillsDrawn_ = 0;  // 2.84: « NOUVEAU » pills drawn this frame
    bool ended_ = false;
};

}  // namespace cosmic
