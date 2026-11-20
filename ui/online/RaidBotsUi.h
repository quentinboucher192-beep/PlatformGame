#pragma once

// 2.79 (docs/RAIDS.md §28): what a raid test shows of its simulated players and how its administrator talks to
// them. Display and this computer's inputs only: the orders reach the simulated players through the
// administrator's frame (raid::bots::withOrder, raid::TestSession), never from here.
//
// * « Ce qu'ils pensent »: a short bubble over each simulated player in view — its role, its intention (« Réanimateur
//   → réanime Joueur simulé 3 »), and under it how it moves (the navigation's intention). Each one measured (its
//   font fitted, its text shortened with « … » when it must), placed over its player's head and moved up or to
//   the side until it overlaps no other bubble, no name over a player, no character and no panel of the HUD,
//   inside the screen; one that finds no room is not drawn. Shown or hidden with its key (Action::BotThoughts:
//   V), the order wheel, or Paramètres › HUD › « Raids ».
// * The order wheel (Action::BotOrders: C, R3; the mouse): « Suivez-moi », « Attendez ici », « Tous sur
//   l'objectif », « Réanimez-le », « Libres », « Marquer un endroit » (a cursor moved with the stick, the arrows
//   or the mouse, placed with Valider or a click), and « Pensées : afficher / masquer ». The game goes on under
//   it; this player's inputs are neutral while it is open (as under the raid's menu).

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"
#include "game/raid/bots/BotTypes.h"

namespace cosmic {

class UIContext;
class InputManager;

namespace botsui {

/// One simulated player in view, as the bubbles read it.
struct ThoughtView {
    int seat = 0;
    Rect body;          // its character on the screen
    Color color = Color::white();
    std::string role;   // « Réanimateur »
    std::string intent; // « réanime Joueur simulé 3 »
    std::string move;   // « saute vers la plateforme » ("" none)
    bool order = false; // it obeys an order
};

struct Bubble {
    int seat = 0;
    Rect box;
    Color color = Color::white();
    std::string role, intent, move;  // as drawn (shortened when needed)
    float size1 = 13.0f, size2 = 12.0f;
    bool order = false;
};

/// Places the bubbles of `in` on a screen of `screen` (its pixels), out of `avoid` (the names over the players, the
/// HUD's panels) and of every character in `bodies`: never over one another, inside the screen.
std::vector<Bubble> layoutThoughts(UIContext& ui, const std::vector<ThoughtView>& in, const std::vector<Rect>& avoid, const std::vector<Rect>& bodies,
                                   const Rect& screen);
void drawThoughts(UIContext& ui, const std::vector<Bubble>& bubbles);
/// The arrow between a bubble's role and its intention.
inline constexpr const char* kArrow = " → ";

// ------------------------------------------------------------------------------- the order wheel

enum class WheelItem : int { Follow, Wait, Objective, Revive, Free, Mark, Thoughts, Count };
constexpr int kWheelItems = static_cast<int>(WheelItem::Count);
/// Its text (« Suivez-moi »…; the last one says what the press will do: show or hide).
std::string wheelLabel(WheelItem i, bool thoughtsShown);
/// What it does, under the wheel.
const char* wheelHelp(WheelItem i);

struct WheelLayout {
    Rect panel;               // around everything
    Rect title, help, keys;   // its title, the chosen item's help, the keys
    std::vector<Rect> items;  // one per WheelItem, around the centre
    std::vector<std::string> helpLines, keyLines;
    float itemFont = 16.0f, titleFont = 20.0f;
};
WheelLayout layoutWheel(UIContext& ui, bool thoughtsShown, int selected, const std::string& keysText);

struct OrderWheel {
    bool open = false;
    int selected = 0;
    bool marking = false;  // choosing the place of « Marquer un endroit »
    Vec2 cursor;           // the place being marked (world)
};

/// What the wheel asks this frame.
struct WheelResult {
    bool order = false;
    raid::bots::OrderKind kind = raid::bots::OrderKind::None;
    bool markAt = false;   // `kind` Mark at `at` (else at the administrator's feet, given by the scene)
    Vec2 at;
    bool toggleThoughts = false;
    bool close = false;    // closed without an order
};

/// One frame of the wheel: its key opens and closes it (when `allowed`); open, the stick, the arrows or the mouse
/// choose, Valider / a click acts, Retour closes. Marking: the cursor (world, `view`) moves with the stick, the arrows
/// or the mouse, Valider / a click places it, Retour cancels. `dt`: this frame's seconds.
WheelResult handleWheel(UIContext& ui, const InputManager& in, OrderWheel& w, bool allowed, bool thoughtsShown, const WorldView& view, Vec2 start,
                        float dt);
void drawWheel(UIContext& ui, const OrderWheel& w, bool thoughtsShown, const WorldView& view, const std::string& keysText);

}  // namespace botsui

}  // namespace cosmic
