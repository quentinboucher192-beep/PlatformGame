#pragma once
// ADMINISTRATION › Créations: what an administrator makes on Cosmic Online that every game
// draws without an update. Four kinds, chosen above the list (« Combinaisons », « Traînées »,
// « Cadres », « Effets de victoire »):
//   - a suit: a name and three colours (the suit, its accent, its scarf), applied to Nova's
//     sheet at run time the way the catalogue's suits are made (game/cosmetics/SuitRecolor.h);
//   - (2.59) a trail: a name, a shape the game draws, two colours (by Nova, at its tail), its
//     width, how long it lasts and its sparkles; a frame: a name, a style and two colours (its
//     border, its glow); a victory effect: a name, an effect and two colours
//     (game/cosmetics/CreationLooks.h, drawn by ui/screens/CosmeticFx).
// Each one with an animated live preview (Nova in the suit; Nova running with the trail; the
// frame on a profile and a board row; Nova at the exit with the effect). The list (those in
// service, the archives on demand), « Nouvelle… », the editor (name, style, colour pickers and
// settings usable with the keyboard, the gamepad and the mouse), « Enregistrer » and
// « Archiver » / « Restaurer »: each change is sent to the server, which checks everything again
// and answers (shown as it is: nothing is ever taken for done before its answer). A creation is
// then given as the reward of a challenge in « Calendrier des défis › Récompenses ».

#include <functional>
#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/cosmetics/CreationLooks.h"
#include "game/cosmetics/SuitRecolor.h"
#include "game/online/OnlineTypes.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/CosmeticFx.h"

namespace cosmic {

class GameServices;

class AdminCreations {
public:
    AdminCreations(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// Automated screenshots, once the list is read: "new" (a new suit), 2.59 "<kind>" (that
    /// kind's list: suit, trail, frame, victory), "<kind>:new" (a new one of it), "<kind>:<id>"
    /// (that creation in the editor).
    void open(const std::string& arg);
    /// The tab is shown: the list is read once (again with `force`).
    void enter(bool force = false);
    void draw(const Rect& inner);

private:
    struct Draft {
        int id = 0;  // 0: a new creation
        std::string kind = "suit";
        std::string name;
        SuitColors colors;  // a suit
        CreationLook look;  // a trail, a frame, a victory effect
        std::string updatedAt;
        bool archived = false;
    };

    void load();
    void startNew();
    void select(const Json& creation);
    /// A row chosen (`id`, -1: « Nouvelle… »): at once, or (changes not saved) on a second choice.
    void choose(int id);
    /// The kind shown (0 suits ... 3 victory effects): at once, or (changes not saved) on a second choice.
    void chooseKind(int kind);
    void save();
    void setArchived(bool archived);
    bool dirty() const;
    /// Why « Enregistrer » cannot be used ("" = it can).
    std::string problem() const;
    const Json* byId(int id) const;
    /// The kind shown, as the server names it ("suit"...).
    const std::string& kindId() const;
    bool suitKind() const { return kind_ == 0; }
    /// How many colours the kind edits (3 for a suit, 2 for the others) and the one edited.
    int colorCount() const { return suitKind() ? 3 : 2; }
    Rgb8& current();
    Rgb8 colorAt(int i) const;

    void drawKinds(const Rect& r);
    void drawList(const Rect& r);
    void drawRow(const Json& c, const Rect& row);
    void drawRowPreview(const Json& c, const Rect& pv, bool selected, bool archived);
    void drawEditor(const Rect& r);
    /// The style buttons in `w` (as many per row as fit); `measure`: only their height.
    float drawStyles(float x, float y, float w, bool measure);
    float drawColorCards(float x, float y, float w);
    float drawChannels(float x, float y, float w);
    /// One channel (0 red, 1 green, 2 blue) of the colour being edited: a bar of its gradient,
    /// focusable (← → by 5, Maj by 1), held with the mouse. Returns true when it changed.
    bool channelBar(int channel, const Rect& r);
    float drawPalette(float x, float y, float w);
    /// A trail's width, length in time and sparkles.
    float drawTrailSettings(float x, float y, float w, bool measure);
    /// A setting from `minV` to `maxV`: a bar, focusable (← → by `step`, Maj by `fine`), held with
    /// the mouse (`bar`: its number while held). Returns true when it changed.
    bool valueBar(const std::string& name, int bar, const Rect& r, int& value, int minV, int maxV, int step, int fine);
    void drawPreview(const Rect& r);
    void drawSuitPreview(const Rect& r, float top, float bottom);
    void drawTrailPreview(const Rect& r, float top, float bottom);
    void drawFramePreview(const Rect& r, float top, float bottom);
    void drawVictoryPreview(const Rect& r, float top, float bottom);
    /// A frame of `clip` of Nova at `dst`, in the suit `c` (nullptr: her own).
    void drawNova(const SuitColors* c, const std::string& clip, float t, const Rect& dst, Color tint = Color::white(), bool flip = false);

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    OnlineUi online_;

    Json list_ = Json::array();
    bool loaded_ = false, loading_ = false, saving_ = false;
    std::string loadError_;
    int loadGen_ = 0;
    bool showArchived_ = false;
    int kind_ = 0;          // the kind shown: 0 suits, 1 trails, 2 frames, 3 victory effects
    int pendingKind_ = -1;  // a kind chosen while the draft had changes
    bool editing_ = false;  // the editor shows a draft
    Draft draft_, base_;    // base_: as the server last answered (or the defaults of a new one)
    int colorTab_ = 0;      // the colour edited (0..colorCount() - 1)
    std::string hex_;       // the « #rrggbb » field
    Rgb8 hexShown_;         // the colour the field was last written from
    int hexTab_ = -1;
    int dragBar_ = -1;      // the bar held with the mouse (0..2 the channels, 10 width, 11 length)
    int pendingChoice_ = 0; // a row chosen while the draft had changes (0: none; -1: a new one)
    std::string msg_;
    bool msgError_ = false, msgWarn_ = false;
    std::string wantOpen_;
    bool focusName_ = false;  // a new creation: the name field gets the focus
    // The trail of the preview: Nova runs across it (screen pixels, the clock of the interface).
    TrailFx previewTrail_;
    Rect previewTrailRect_;
    float previewClock_ = -1.0f;
};

}  // namespace cosmic
