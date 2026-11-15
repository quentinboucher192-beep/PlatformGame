#pragma once

namespace cosmic {

class Enemy;
class GameWorld;
struct RenderContext;

/// Display of the effects of the 2.19 powers that live in the world rather than with Nova
/// (drawn by the GameRenderer; never part of the simulation).
/// Ice on a frozen enemy (Nova de givre): a translucent block whose top can be stood on; a
/// boss only slowed gets a frosty aura.
void renderFrozenEnemy(RenderContext& ctx, const Enemy& e);
/// The hologram of Nova left by the Leurre holographique, while it lasts.
void renderDecoy(RenderContext& ctx, const GameWorld& w);

}  // namespace cosmic
