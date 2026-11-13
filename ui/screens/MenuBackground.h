#pragma once

#include <vector>

#include "engine/core/Random.h"
#include "engine/rendering/Parallax.h"

namespace cosmic {

class GameServices;

/// Animated Sylvanox vista used behind the menus (slow travelling over the parallax
/// planes with drifting bioluminescent spores).
class MenuBackground {
public:
    void init(GameServices& g);
    void update(float dt);
    void render(GameServices& g, float dim = 0.0f);

private:
    struct Mote {
        Vec2 pos;
        Vec2 vel;
        float size;
        float phase;
        Color color;
    };
    ParallaxBackground parallax_;
    std::vector<Mote> motes_;
    Random rng_{1234};
    float time_ = 0.0f;
    bool ready_ = false;
};

/// Draws the game logo (title text with glow) centred at `center`.
void drawGameTitle(GameServices& g, Vec2 center, float size, float alpha = 1.0f);

}  // namespace cosmic
