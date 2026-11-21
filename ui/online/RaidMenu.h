#pragma once

// 2.75.5 (docs/RAIDS.md §21): what a raid tells its players without ever taking the controls from
// them, and the menu each player opens to read more.
//
// * Notifications (Notices): a zone finished, the group eliminated, a zone taken back by a group of
//   reinforcements, a gigaboss (a part of the titan) fallen, a link of the titan on… Each one a short
//   box (its title, a line, « Tableau : Tab ») under the menu's button (RaidHudElement::Notices, moved
//   and resized in Paramètres › HUD › « Raids »), shown kShow seconds then fading away; at most
//   kMaxShown at once, one under the other, the others waiting their turn. Never a widget: no focus,
//   no key, no click taken from the game (the button answers the mouse only). Each one stays in the
//   journal with its time in the raid.
// * The raid's menu (Menu): opened and closed by the player (Action::RaidMenu: Tab, L3; the HUD's
//   button; Échap or B close it too), its sections « Ce qu'il reste à faire », « Zones terminées »
//   (each zone's « Tableau des contributions »), « Journal », « Groupe » and « Titan » (Gauche /
//   Droite, Q / E, LB / RB, the mouse on its tabs), scrolled (Haut / Bas, the stick, the wheel, its
//   bar). The game goes on under it as under the pause: this player's inputs are neutral while it is
//   open (Overlay::frame), so the keys that move in the menu never move the character.
//
// Display and this computer's inputs only: nothing here takes part in the simulation.

#include <cstddef>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/InputManager.h"
#include "game/gameplay/InputFrame.h"
#include "ui/online/RaidHud.h"

namespace cosmic {

class UIContext;

namespace raidmenu {

enum class Section : int { Remaining, Zones, Journal, Group, Titan, Count };
constexpr int kSections = static_cast<int>(Section::Count);
/// Its tab's name.
const char* sectionLabel(Section s);

struct Notice {
    std::string title, text;
    Color color = Color::white();
    Section section = Section::Journal;  // where the menu tells more (it opens there while it is shown)
    bool table = false;                  // a zone finished here: its table (« Tableau : Tab »)
    float raidSeconds = 0.0f;            // when, in the raid's time (its simulation's steps)
    float shownAt = -1.0f;               // this computer's clock at its first frame on screen (-1: waiting)
};

/// The notifications of a raid: each one on screen kShow seconds (fading out over its last kFade), at
/// most kMaxShown at once in their order of arrival, the next ones waiting for room; all of them kept
/// in the journal (the newest kJournalMax).
class Notices {
public:
    static constexpr float kShow = 3.5f;
    static constexpr float kFade = 0.5f;
    static constexpr int kMaxShown = 3;
    static constexpr size_t kMaxWaiting = 8;
    static constexpr size_t kJournalMax = 400;

    void push(Notice n);
    /// Drops those shown long enough (`clock`: this computer's seconds).
    void update(float clock);
    /// The first ones in line (kMaxShown at most): on screen when the HUD has room for them.
    std::vector<const Notice*> front() const;
    /// The first `count` of front() are drawn this frame: their time starts at their first frame.
    void drawn(size_t count, float clock);
    /// Its opacity now (a short fade in, the fade out at the end).
    static float alpha(const Notice& n, float clock);
    const std::vector<Notice>& journal() const { return journal_; }
    bool any() const { return !queue_.empty(); }
    /// The section of the newest notification on screen (the menu opens there), Count when none.
    Section latestSection() const;

private:
    std::vector<Notice> journal_, queue_;
};

/// « 12:34 » (« 1:02:03 » past an hour): a time in the raid.
std::string raidTime(float raidSeconds);

struct NoticeBox {
    Rect rect;
    std::vector<std::string> lines;  // its text, wrapped to its width
};
struct NoticeLayout {
    Rect button;                   // the menu's button, at the top of the area (empty when not shown)
    std::vector<NoticeBox> boxes;  // the notifications placed, in order: a prefix of the list
};
/// The button (`buttonText`, its width measured) and the notifications under it, one after the
/// other, each measured to the area's width and inside the area; what does not fit is left out
/// (it waits). `button`: whether the button is placed.
NoticeLayout layoutNotices(UIContext& ui, const Rect& area, float k, const std::vector<const Notice*>& list, const std::string& buttonText, bool button);
/// The text of the button and of a notification's reminder with the menu's key (« Tableau : Tab »).
std::string buttonText(const std::string& key);
std::string reminder(const Notice& n, const std::string& key);
/// Draws the button and `list` at their places (`alpha` one per notification). No widget: nothing
/// takes the focus or a key; the button answers a mouse click (true when clicked). `drawnCount`:
/// how many of `list` were drawn; `buttonRect` (2.77.4): where the button was drawn (empty if not).
bool drawNotices(UIContext& ui, const Rect& area, float k, const std::vector<const Notice*>& list, const std::vector<float>& alpha,
                 const std::string& key, bool button, size_t* drawnCount = nullptr, Rect* buttonRect = nullptr);

/// The raid's menu, opened and closed by the player.
class Menu {
public:
    bool isOpen() const { return open_; }
    Section section() const { return section_; }
    void open(Section s);
    void close() { open_ = false; }
    void select(Section s) { section_ = s; }
    /// This frame's keys. Closed: its key (Action::RaidMenu) opens it on `openAt` (Count: where it
    /// was) when `allowed`. Open: its key, Échap or B close it, Gauche / Droite and Q / E (LB / RB)
    /// change section, Haut / Bas scroll, Entrée / A acts in the section (« Résultats du raid »).
    /// Returns true when it used this frame's « Retour » (Échap also pauses: not with that press).
    bool handleKeys(const InputManager& in, bool allowed, Section openAt);
    /// Pixels to scroll asked by the keys (applied by the next drawMenu), Entrée / A this frame.
    float scrollBy = 0.0f;
    bool confirm = false;

private:
    bool open_ = false;
    Section section_ = Section::Remaining;
    FreshKeys keys_;  // titan 13: its sections on a fresh press only
};

/// Whether this computer's player is held (neutral inputs) at a step: in the pause or the raid's menu
/// (their own choice), under a screen over the combat (a film, the results, a power's choice), or out
/// of the game (their group eliminated: spectators). A notification is never one of these.
struct Hold {
    bool paused = false, menu = false, covered = false, wiped = false;
    bool any() const { return paused || menu || covered || wiped; }
};
/// The frame this computer sends for its player: the input as read, neutral while held.
InputFrame playerFrame(const PlayerInput& in, const Hold& h);

/// The notifications and the menu of a raid's screen.
struct Overlay {
    Notices notices;
    Menu menu;
    Hold hold(bool paused, bool covered, bool wiped) const { return {paused, menu.isOpen(), covered, wiped}; }
    InputFrame frame(const PlayerInput& in, bool paused, bool covered, bool wiped) const { return playerFrame(in, hold(paused, covered, wiped)); }
    /// The pointer is shown while the menu is open (its tabs, its bar, its buttons).
    bool wantsPointer() const { return menu.isOpen(); }
};

// ------------------------------------------------------------------------------- the menu's pages

struct Member {
    std::string name, state, hearts;  // « 3 / 5 »
    std::string thought;  // 2.79: a test's simulated player: its role, what it does, how it moves (« Réanimateur → … »)
    Color color = Color::white(), stateColor = Color::white();
    float health = 0.0f;  // 0..1
    bool vacant = false, me = false;
};

/// What the menu shows, from the screen's own data (the simulation's, the server's): never invented.
struct MenuData {
    std::string title;    // « MENU DU RAID · BRAS DROIT »
    std::string keyName;  // the menu's key (« Tab »)
    std::string remainingTitle;
    std::vector<raidhud::Line> remaining;
    /// 2.78: one line per zone finished (« Veine de magma · terminée à 12:34 · meilleur au boss : Lyra (31 %) »),
    /// the newest first: the full « Tableau des contributions » opens over the menu (Entrée / A, its button).
    std::vector<std::string> tableLines;
    bool tableAvailable = false;
    std::vector<std::string> otherZones;     // the branch's zones finished elsewhere (their names)
    const std::vector<Notice>* journal = nullptr;
    raidhud::RaidClock clock;  // titan 13: the raid's countdown (the title row, the journal's times)
    std::string groupTitle, groupFooter;
    std::vector<Member> group;
    std::vector<raidhud::Line> groupStates;  // 2.77.2: « À TERRE » and « GROUPE ÉLIMINÉ » (mark: the state's name), explained
    std::vector<raidhud::Line> titan;  // its gigaboss, its parts, its links
    bool results = false;              // « Résultats du raid »: a gigaboss fell (never in a test)
};

struct MenuAction {
    bool close = false, results = false;
    bool table = false;  // 2.78: « Tableau des contributions » asked (its button, Entrée / A in « Zones terminées »)
};

/// Its places on this screen, each measured and after the one before: the panel, the title row and
/// its « Fermer » button, the rule (« le jeu continue »), the tabs, the section's view, the keys.
struct MenuLayout {
    Rect panel, title, close, tabsRow, view, keys;
    std::vector<Rect> tabs;
    std::vector<std::string> rule, keyLines;
    float ruleY = 0.0f, tabFont = 16.0f;
};
MenuLayout layoutMenu(UIContext& ui, const std::string& keyName);
/// The height of a section's content at `width` (what drawMenu scrolls through).
float sectionHeight(UIContext& ui, const MenuData& d, Section s, float width);
/// Draws the menu over the combat; what the player asked with the mouse (« Fermer », « Résultats »).
MenuAction drawMenu(UIContext& ui, Menu& m, const MenuData& d);

}  // namespace raidmenu

}  // namespace cosmic
