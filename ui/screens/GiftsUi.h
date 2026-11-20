#pragma once
// Gifts (« Cadeaux »): where each text and button of the gift screens goes, measured before anything is drawn
// (the screens draw these rectangles, tests/test_gifts.cpp checks them at 1280 x 720 and 1920 x 1080, in French
// and in English): COSMIC ONLINE › CADEAUX (the list and the detail), the opening's last texts and buttons, and
// the three columns of ADMINISTRATION › Cadeaux. Every text is fitted (smaller, then shortened) to its room.

#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class UIContext;

namespace giftsui {

/// A text as it is drawn: what to give ui.label (fitted, maybe shortened), its size (unscaled) and its box.
struct Text {
    std::string shown;
    float size = 0.0f;
    Rect box;
};

// ------------------------------------------------------------ COSMIC ONLINE › CADEAUX

struct ScreenLayout {
    Rect header, nav, list, detail, hint;
};
/// The columns of the online screens: the navigation, the list of gifts, the detail of the one chosen.
ScreenLayout screenLayout(UIContext& ui);

struct ListHead {
    Text title, count;
    Rect tabs, rows;
};
/// « MES CADEAUX », its counts at the right (« 2 à ouvrir · 5 reçus »), the filters, the room of the rows.
ListHead listHead(UIContext& ui, const Rect& list, const std::string& count);

constexpr float kRowH = 92.0f;
constexpr float kRowGap = 10.0f;
struct RowLayout {
    Rect icon, status;
    Text title, sub, statusText;
};
/// One gift of the list: its icon, « Cadeau de X » (or what it held), « Contenu : surprise · date », its state.
RowLayout rowLayout(UIContext& ui, const Rect& row, const std::string& title, const std::string& sub, const std::string& status, bool verbatimTitle);

struct DetailTexts {
    std::string kind, name, from, message, note;
    std::vector<std::string> actions;  // one or two buttons
    bool verbatimName = false;
};
struct DetailLayout {
    Text kind, name, from, note;
    Rect art, message;
    std::vector<std::string> messageLines;
    float messageSize = 0.0f, messageLineH = 0.0f;
    std::vector<Rect> actions;
    float actionSize = 0.0f;
};
/// The detail panel: the kind, the big box (or what it held), the name, who sent it and when, the message, the buttons.
DetailLayout detailLayout(UIContext& ui, const Rect& panel, const DetailTexts& t);

// ------------------------------------------------------------ the opening

struct RevealTexts {
    std::string kind, name, from, message;
    std::vector<std::string> buttons;  // « Équiper maintenant », « Cadeau suivant (n) », « Plus tard »
};
struct RevealLayout {
    Text kind, name, from;
    std::vector<std::string> messageLines;
    float messageSize = 0.0f, messageLineH = 0.0f, messageY = 0.0f;
    std::vector<Rect> buttons;
    float buttonSize = 0.0f;
    Rect hint;  // the line of the keys, at the bottom
    float itemY = 0.0f, boxBase = 0.0f, scale = 1.0f;  // the scene's 1280 x 720 frame mapped on the screen
    Vec2 origin;
};
/// The texts under the item once revealed, and the buttons; the item rises above them.
RevealLayout revealLayout(UIContext& ui, const RevealTexts& t);

// ------------------------------------------------------------ ADMINISTRATION › Cadeaux

struct AdminLayout {
    Rect rewards, players, send;  // the three columns
    int itemColumns = 3, wrapColumns = 6;
    float itemH = 0.0f, wrapH = 0.0f, playerH = 0.0f, historyH = 0.0f;
};
/// The tab's three columns in the body the administration screen gives it.
AdminLayout adminLayout(UIContext& ui, const Rect& inner);

}  // namespace giftsui
}  // namespace cosmic
