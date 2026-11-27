#pragma once

// 2.95 (docs/RAIDS.md §39): the raid's chat on the screen, as in Minecraft, in its place of the raids' HUD
// (RaidHudElement::Chat: Paramètres › HUD › « Raids », with its width, its lines, how long a line stays and the
// opacity of its background).
//
// * Closed: the recent lines at the bottom of its box, each on its own translucent strip, fading away after the
//   delay chosen; under them a small « Chat · T » the mouse clicks (never a key nor the focus taken).
// * Open (Action::ChatOpen: T, Partage; the raid menu's « Groupe »; that small button): its title row (what the
//   keys do, or « TEST LOCAL — rien n'est envoyé »), the history scrolled (the wheel, its bar held, Page
//   précédente / suivante, the right stick), the quick messages (a gamepad without a keyboard: ◀ ▶ to choose, A to
//   send), and its row: the channel (« Groupe » / « Raid », clicked or Action::ChatChannel: Tab, RB, LB), the field
//   (Entrée sends, « /g » and « /r » choose the channel) and « Envoyer ». Échap, B or Start close it.
//
// Every line is measured into the box: wrapped word after word (a word wider than the box cut between its
// characters), the channel's tag, the author's clan and name and the text each in its colour; the players' words
// drawn as they wrote them (i18n::Verbatim), the rest translated. Display and this computer's choices only.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Text.h"
#include "game/raid/RaidChat.h"
#include "saves/RaidHudLayout.h"

namespace cosmic {

class InputManager;
class UIContext;

namespace raidchatui {

/// The colour of a channel's tag (« [Groupe] » cyan, « [Raid] » orange), of the AI's (violet).
Color channelColor(raidchat::Channel c);
Color aiColor();

/// What the drawing keeps between frames.
struct View {
    enum class Focus { None, Field, Quick };
    Focus focusPending = Focus::None;  // given at the next drawing (opened by a key)
    bool keepEditing = false;          // the channel changed with Tab while typing: the field goes on
    int quick = 0;                     // the quick message chosen
    float scrollBy = 0.0f;             // pixels asked by the keys and the stick (applied by the next drawing)
    bool follow = true;                // the history at its newest (it follows the new lines)
    uint64_t seen = 0;                 // the log's serial last drawn
    Rect pill;                         // where « Chat · T » was drawn closed (empty: not drawn)
    Rect drawn;                        // the box drawn this frame (empty: nothing)
    // The history wrapped (rebuilt when the log, the width, the size or the language change).
    struct Cache;
    std::shared_ptr<Cache> cache;
};

/// The keys of this frame (the screen's update, before the drawing).
struct Keys {
    bool opened = false;  // opened this frame
    bool closed = false;  // closed this frame
    bool back = false;    // closed by Échap, B or Start: the screen does not pause with that press
};
/// Closed: `allowed` and Action::ChatOpen (T, Partage) open it — the field being typed in at once, or from the
/// gamepad the quick messages first. Open: Échap, B or Start close it; the channel's key (Action::ChatChannel:
/// Tab, RB; LB too) changes channel, also while typing; T again (not typing) goes back to the field; Page
/// précédente / suivante and the right stick scroll the history. Not `allowed` any more (the game ended…): closed.
Keys handleKeys(UIContext& ui, const InputManager& in, raidchat::Chat& c, View& v, bool allowed, float dt);

/// How the screen names things: its keys, its title, the authors' colours.
struct Look {
    std::string openKey;  // « T », or the gamepad's button: the small button of the closed chat
    std::string hints;    // the title row of the open chat (« Entrée : envoyer · Tab : canal · Échap : fermer »)
    std::string header;   // « TEST LOCAL — rien n'est envoyé » (a local test), drawn first in gold; "" none
    std::function<Color(const raidchat::Line&)> nameColor;
};

/// Its places inside `area` (its box on this screen), each after the one before.
struct Layout {
    Rect area;
    Rect lines;                            // the rows of the lines (closed), header + history + quick (open)
    Rect row;                              // the field's row at the bottom
    Rect header, history, quick;           // open
    Rect quickPrev, quickText, quickNext;  // open: « ◀ », the message, « ▶ »
    Rect pill, field, send;                // open: the channel, the field, « Envoyer »; closed: pill = « Chat · T »
    float font = 15.0f;                    // the lines' size (unscaled)
    float lineH = 0.0f;
};
Layout layout(UIContext& ui, const Rect& area, float k, bool open, const Look& look, raidchat::Channel channel);

/// A piece of a line as drawn: its text (already translated), colour, face and place from the line's left.
struct Piece {
    std::string text;
    Color color = Color::white();
    FontFace face = FontFace::Body;
    float x = 0.0f, w = 0.0f;
};
using Visual = std::vector<Piece>;
/// One line of the chat wrapped to `width` pixels at `size` (unscaled): its tag, the author, the text; never
/// wider than `width` (a word too long is cut between its characters).
std::vector<Visual> wrapLine(UIContext& ui, const raidchat::Line& l, const Look& look, float size, float width);

/// What the player sent this frame (Entrée, « Envoyer », a quick message): the screen submits it.
struct Sent {
    bool any = false;
    bool quick = false;
    std::string text;
};
/// Draws the chat in `area` (its place) at scale `k` with its settings; `clock`: this computer's seconds (the
/// fading). `chatShown`: the element shown in the HUD (else only its open form is drawn).
/// `interactive` false: drawn only (the HUD editor's example: its small button never answers).
Sent draw(UIContext& ui, raidchat::Chat& c, View& v, const Rect& area, float k, const RaidChatSettings& st, const Look& look, float clock,
          bool chatShown = true, bool interactive = true);

}  // namespace raidchatui

}  // namespace cosmic
