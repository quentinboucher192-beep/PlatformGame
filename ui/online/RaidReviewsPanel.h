#pragma once
// 2.77.4: the players' reviews of a raid event (game/online/RaidReviews, server/src/raidReviews.ts,
// docs/RAIDS.md §24), drawn: the stars, the « ★ 4,2 · 12 avis » of the cards, the panel « AVIS DES
// JOUEURS » of an event's sheet (the player's own review first with « Modifier », the others with
// their author, stars, date and text, scrolled), the « Noter ce raid » editor (never opened by
// itself: always from a button, after the game), the report of a comment, and the administrators'
// list (ADMINISTRATION › Raids › Avis: every review, its reports, « Masquer » / « Rétablir »).
//
// Placement: every piece is measured and placed after the one before it (layoutItem,
// layoutEditor); the tests check that no text overlaps another nor leaves its room, at 1280×720
// and 1920×1080, in French and in English. Keyboard, gamepad and mouse: the stars are focus stops
// (← → or the stick choose the rating, Entrée / A or a click too), the comment is a text field
// (Ctrl+C / Ctrl+V as everywhere), each action is a button; Échap / B closes a window.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/RaidReviews.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

class GameServices;

namespace raidreviewsui {

/// A five-pointed star centred on `c`, outer radius `r`, filled from its left up to `fill` (0 to 1).
void drawStar(UIContext& ui, Vec2 c, float r, float fill, Color on, Color off);
/// The width of five stars of `size` (unscaled, as a font size), with their gaps.
float starsWidth(UIContext& ui, float size);
/// Five stars from `x`, centred on `cy`, filled up to `value` (a half star for 3,5); their width.
float drawStars(UIContext& ui, float x, float cy, float size, double value, Color on, Color off);

/// « ★ 4,2 · 12 avis » on one line: one star, then its text (smaller, then shortened, within `maxW`).
struct SummaryLayout {
    Rect star, text;       // empty when nothing is shown
    float size = 0.0f;     // the text's size (unscaled)
    std::string shown;     // as drawn
    bool filled = false;   // some reviews: a full star (else an empty one, « Aucun avis »)
    float width() const { return text.right() - star.x; }
};
SummaryLayout layoutSummary(UIContext& ui, float x, float cy, const raidreviews::Summary& s, float size, float maxW, bool alignRight);
void drawSummary(UIContext& ui, const SummaryLayout& L, Color starColor, Color textColor);

/// One review's card: each piece in its own room, one after the other (stars and author, the text,
/// the masked note, the reports of the administrators' list, the action and the date).
struct ItemLayout {
    Rect card, stars, name, action, date;
    std::string nameText, dateText, actionLabel, actionKind;  // kind: "edit", "report", "reported", "hide", "restore", ""
    std::vector<std::string> body, note, admin;  // wrapped lines
    float bodyY = 0.0f, noteY = 0.0f, adminY = 0.0f;
    float bodyLineH = 0.0f, noteLineH = 0.0f;
    float h = 0.0f;
};
ItemLayout layoutItem(UIContext& ui, const raidreviews::Review& r, float x, float y, float w, bool admin, bool loggedIn);

/// The « Noter ce raid » window, measured from the screen's size.
struct EditorLayout {
    Rect box, title, caption, word, field, counter, captionText, preview, problem, remove, cancel, ok;
    std::vector<Rect> stars;
    std::vector<std::string> intro, previewLines;
    float introY = 0.0f, introLineH = 0.0f, previewLineH = 0.0f;
    std::string titleText, okLabel;
};
EditorLayout layoutEditor(UIContext& ui, bool editing, int stars, const std::string& text);

}  // namespace raidreviewsui

/// The reviews of one event: read from the server, drawn as a panel (the event's sheet) or as the
/// administrators' list, with the editor and the report window. Never opens a window by itself.
class RaidReviewsPanel {
public:
    enum class Mode { Player, Admin };
    RaidReviewsPanel(GameServices& g, Mode mode = Mode::Player);

    /// The event whose reviews are shown (read now when it changes, or with `force`).
    void open(const std::string& eventId, bool force = false);
    void reload();
    const std::string& eventId() const { return eventId_; }
    bool loaded() const { return loaded_; }
    const raidreviews::Page& page() const { return page_; }
    /// What the player may do (« Noter ce raid », « Modifier mon avis », nothing): the server's verdict.
    raidreviews::Offer offer() const;
    /// Opens the editor (« Noter ce raid » or « Modifier mon avis ») when the offer allows it.
    void openEditor();

    /// The panel in `r`: its header with the average and the count, the offer (or why there is none),
    /// the player's own review first, then the others (scrolled), « Plus d'avis ».
    void draw(const Rect& r);
    /// A window (editor, deletion, report) is open: the screen under it must not react.
    bool modal() const { return modal_ != Modal::None; }
    void drawModal();

    /// Messages for the screen's status line (sent, deleted, an error of the server).
    std::function<void(const std::string&, bool)> say;
    /// After a write: the screen may read its own figures again (the sheet's summary).
    std::function<void()> changed;

private:
    enum class Modal { None, Editor, Delete, Report };
    void load(bool more);
    void submit();
    void remove();
    void report();
    void moderate(const std::string& id, bool hide);
    float itemsHeight(float w, bool loggedIn);
    void drawItem(const raidreviews::Review& r, const raidreviewsui::ItemLayout& L);

    GameServices& g_;
    Mode mode_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string eventId_, error_;
    raidreviews::Page page_;
    bool loaded_ = false, loading_ = false, busy_ = false;
    int gen_ = 0;

    Modal modal_ = Modal::None;
    int stars_ = 0;
    std::string text_, modalError_, reportId_;
    int reportReason_ = 0;
    int prevStarFocus_ = -1;   // the star focused on the previous frame (-1: none)
    bool focusStars_ = false;  // the editor just opened: its stars take the focus
};

}  // namespace cosmic
