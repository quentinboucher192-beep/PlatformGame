#pragma once

// 2.77.2 (docs/RAIDS.md §23, docs/COOP.md §1): what a player who no longer plays sees, in a raid and in
// cooperation. Display only: nothing here reaches the simulation (the camera neither).
//
// * DOWN, waiting for a revival (GameWorld::Presence::Down): a banner at the top, « À TERRE — en attente
//   de réanimation », what has to happen (« Un allié doit maintenir Interagir 2 s près de votre âme »), the
//   revival under way and its gauge (« Joueur 2 vous réanime… 60 % »), the team's revivals left
//   (cooperation), where the soul is (an arrow on the border when it is out of view) and, in a raid, what
//   the player can do: watch the previous / next player, come back to their soul (the keys of the device
//   in use, and the mouse on the buttons).
// * The GROUP WIPED (raids, RaidRun::wiped): « GROUPE ÉLIMINÉ — mode spectateur jusqu'à la fin de
//   l'événement », the same view keys and the free camera: another banner, another colour.
// * A light filter over the world (a soft vignette, the colours a little washed): this player no longer
//   plays; the HUD is drawn over it, as readable as ever.
// * The tremble of 2.77.1 (« en groupe éliminé, le jeu tremble »): a wiped group's world no longer steps
//   (RaidRun::step leaves it as it fell), but it was drawn with the app's interpolation alpha, which
//   changes at every frame: the camera (between its last two positions) and every body that moved at its
//   last step swung back and forth at every frame. drawAlpha(): a world that does not step is drawn as
//   it is. ViewBlend: a change of who is watched is a short smooth move, never a cut.
//
// Every text is measured and placed after the one before (layout), inside its panel; the panel goes
// under the HUD at the top of the screen and the raid's notifications go under it (place, below).

#include <algorithm>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/InputTypes.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class GameWorld;
class UIContext;

namespace downed {

/// The interpolation a world is drawn with: the app's `alpha` while the world steps; 1 (as it is) when
/// it does not (a group wiped, everyone waiting for a power's choice, a pause that stops it): drawn
/// with an alpha that changes at every frame, its last two states would alternate (the tremble).
inline float drawAlpha(bool stepping, float alpha) { return stepping ? std::clamp(alpha, 0.0f, 1.0f) : 1.0f; }

/// A change of view (another player watched, back to the soul, the free camera) eased over kSeconds:
/// from the view shown when it changed to the view wanted now (which may move itself: a camera that
/// follows a player), its centre and its zoom eased in and out; the view wanted itself once done.
class ViewBlend {
public:
    static constexpr float kSeconds = 0.6f;
    void start(const WorldView& from) {
        from_ = from;
        t_ = 0.0f;
        on_ = from.scale > 0.0f && from.screenW > 1 && from.screenH > 1;  // a view drawn already
    }
    void stop() { on_ = false; }
    bool active() const { return on_; }
    /// This frame's view (`dt` seconds since the last one).
    WorldView apply(const WorldView& want, float dt);

private:
    WorldView from_;
    float t_ = 0.0f;
    bool on_ = false;
};

/// The centre (world) a view shows.
Vec2 viewCenter(const WorldView& v);

enum class State { None, Down, Wiped };
/// This computer's player: their group wiped (spectators, `wiped`), their seat `local` down waiting for a
/// revival, or still playing (None). The group wiped wins.
State stateOf(const GameWorld& w, int local, bool wiped);

/// A button of the banner: its key (the device in use: keyboard or gamepad) then its label; the mouse
/// clicks it. Action::Count: no key.
struct Command {
    std::string label;
    Action key = Action::Count;
    bool enabled = true;
};

/// What a banner says.
struct Banner {
    State state = State::None;
    std::string title;
    Color color = Color::white();
    std::vector<std::string> lines;  // each one wrapped, 2 lines at most
    float revive = -1.0f;            // the revival under way (0..1), -1: none
    std::string reviveText;
    std::vector<Command> commands;
};

/// What a player down is told (from the simulation's state, read by the screen).
struct DownInfo {
    float revive = 0.0f;       // seconds an ally has held near the soul (GameWorld::kReviveSeconds to stand up)
    std::string reviver;       // who revives them ("" nobody now)
    bool canRevive = true;     // somebody in play can still revive them (cooperation: revivals left)
    int revivesLeft = -1;      // the team's (cooperation), -1: unlimited (raids)
    int revivesMax = 2;
    bool soulInView = true;    // their soul is on screen
    std::string watching;      // the player watched ("" : their soul)
    bool viewKeys = true;      // raids: watch the others, back to the soul (cooperation: the team's camera)
    bool others = true;        // another player in play to watch
    std::string who;           // several players on this computer: whose banner ("" : this player's)
};
Banner downBanner(const DownInfo& d);

/// The group wiped: the free camera or the player watched (`sub`), the players to follow.
Banner wipedBanner(const std::string& sub, bool freeCamera, bool targets);

/// Its places: each part measured and placed after the one before, inside the panel.
struct Layout {
    Rect panel, title;
    std::vector<Rect> lineBoxes;
    std::vector<std::vector<std::string>> lines;
    Rect reviveLabel, gauge;  // empty without a revival under way
    std::vector<Rect> buttons;
};
/// Its width on this screen (between the HUD's columns on the small screens).
float width(UIContext& ui);
Layout layout(UIContext& ui, const Banner& b, Vec2 topLeft, float width);
/// Its place: centred, at the top, under every rect of `taken` it would cover (the HUD); inside the screen.
Rect place(UIContext& ui, float width, float height, const std::vector<Rect>& taken);
/// The banner laid out at its place (`taken`: the HUD at the top).
Layout placed(UIContext& ui, const Banner& b, const std::vector<Rect>& taken);
/// An area (the raid's notifications) pushed under the banner when they meet: as tall as it was, but
/// never down to a rect of `taken` under it (shorter, even empty: what does not fit waits its turn).
Rect below(const Rect& area, const Rect& banner, float gap, const std::vector<Rect>& taken);
/// Draws it at its places; the command clicked with the mouse (-1: none). `id`: its widgets' name.
int draw(UIContext& ui, const Banner& b, const Layout& l, const std::string& id);
/// The light filter over the world (before the HUD): a soft vignette, the colours a little washed.
void drawFilter(Renderer& r, float w, float h);

}  // namespace downed

}  // namespace cosmic
