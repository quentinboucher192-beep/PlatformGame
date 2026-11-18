#pragma once
// Asterion, le Dévoreur de mondes, drawn from its sprite in layers (CLANS › RAIDS, the sheet of a
// raid event): each branch of its body, its heart and its aura, in the state the server gives
// (game/online/AsterionBody.h), breathing, its heart beating, motes drifting around it. Every
// part answers the mouse (its own pixels, not a box) and the keyboard / gamepad (one focus stop
// per part): it lights up and its tooltip tells its name, its gigaboss, its state and what
// destroying it does to the titan.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "game/online/AsterionBody.h"

namespace cosmic {

class GameServices;

class AsterionFigure {
public:
    /// The titan fitted and centred in `r` (whole multiples of its pixels once enlarged), each
    /// part in its state (intact when `states` does not name it). `id`: the prefix of its focus
    /// stops. `openable`: the parts a click opens (the map of their branch; its tooltip says so).
    /// Returns the id of the openable part clicked or activated (empty when none).
    std::string draw(GameServices& g, const std::string& id, const Rect& r, const std::map<std::string, asterion::State>& states,
                     const std::vector<std::string>& openable = {});
    /// The tooltip of the part hovered or focused by the last draw(), over everything: called once
    /// the rest of the screen is drawn (nothing when draw() was not called since the last one).
    void drawTooltip(GameServices& g);
    /// The part whose tooltip shows (empty when none).
    std::string shownPart() const { return tip_; }
    /// 2.68: where the last draw() put the titan, and the point of a part (its focus stop): the
    /// titan's map places its badges from them. False when the part was not drawn.
    Rect bounds() const { return figure_; }
    bool anchorOf(const std::string& part, Vec2& out) const;
    /// 2.73: what the tooltip of an openable part says activating it does (the raid sheets open the
    /// sheet of its gigaboss; by default, the map of its branch).
    void setOpenHint(std::string hint) { openHint_ = std::move(hint); }

private:
    Rect figure_;
    std::map<std::string, Vec2> anchors_;
    std::string tip_;
    asterion::State tipState_ = asterion::State::Intact;
    bool tipOpens_ = false;
    Rect tipFigure_;
    Vec2 tipAnchor_;
    std::string openHint_ = "Clic ou validation : carte de la branche";
};

}  // namespace cosmic
