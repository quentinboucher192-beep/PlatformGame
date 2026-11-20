#pragma once
// Gifts (« Cadeaux »): what the screens draw of a gift. The box of each wrap is pixel art
// (assets/ui/gifts/wraps.png, tools/assetgen/environment/gift_wraps.py); what a gift held is drawn with the
// game's own renderers, as the Collection draws it: a suit with its real sheet (its victory clip at the
// reveal), a trail and a victory effect with CosmeticFx, an avatar frame with its real frame, a title as a
// gold plaque, a badge as the emblem of the profile badges. Display only.

#include <string>

#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"
#include "game/online/Gifts.h"

namespace cosmic {

class GameServices;
class OnlineUi;

namespace giftart {

/// The star of the sparks: eight points, four long and four short, turned by `rot` (radians).
void star(Renderer& r, Vec2 c, float radius, float rot, Color col, BlendMode blend = BlendMode::Alpha);

/// How the box stands: turned (radians, around the middle of its bottom edge), squashed, its lid raised by
/// `lidDy` sheet pixels (negative: up), or open (no lid).
struct BoxPose {
    float rot = 0.0f;
    float sx = 1.0f, sy = 1.0f;
    float lidDy = 0.0f;
    bool open = false;
    Color tint = Color::white();
};
/// The box of `wrap`, the middle of its cell's bottom edge on `base`, `px` screen pixels per pixel of the sheet.
void drawBox(GameServices& g, const std::string& wrap, Vec2 base, float px, const BoxPose& pose = {});
/// The lid alone (flying away): where it sits on a closed box standing on `base`, moved by `offset`, turned by `rot`.
void drawLid(GameServices& g, const std::string& wrap, Vec2 base, float px, Vec2 offset, float rot, float alpha);

/// The name of a reward as this game knows it (its catalogue, its achievements), else the server's.
std::string rewardName(GameServices& g, const gifts::Reward& r);
/// What a reward looks like, centred on `c`, about `size` pixels (`reveal`: the opening: a suit's victory clip).
void drawReward(GameServices& g, OnlineUi* online, const gifts::Reward& r, Vec2 c, float size, float t, bool reveal);
/// The icon of a gift in a list: wrapped, a glow of its light and the box gently bobbing; opened, what it held.
void drawGiftIcon(GameServices& g, OnlineUi* online, const gifts::Gift& gift, const Rect& box, float t);
/// A wrap alone, closed, bobbing in `box` (the choice of wraps of ADMINISTRATION › Cadeaux).
void drawWrapIcon(GameServices& g, const std::string& wrap, const Rect& box, float t, float phase);

}  // namespace giftart
}  // namespace cosmic
