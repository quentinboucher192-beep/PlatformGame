// The rising liquids (the bosses' floods: lava, star plasma, the abyss's dark water, Voidworm's
// acid) are drawn as a body filling their span wall to wall, from their surface down into the
// ground under their floor, see-through near the surface and darker deeper (display only).

#include "game/enemies/BossHazards.h"
#include "game/gameplay/LiquidBody.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("liquids: a flood's body spans its whole width and reaches under its floor at every age") {
    BossHazards::Flood f;
    f.left = 320.0f;
    f.right = 2240.0f;
    f.floorY = 800.0f;
    f.height = 150.0f;
    f.rise = 2.5f;
    f.hold = 6.0f;
    f.fall = 2.0f;
    for (float age : {0.3f, 1.25f, 2.5f, 5.0f, 9.0f, 10.2f}) {
        f.age = age;
        const Rect b = f.bodyRect();
        CHECK(b.x == f.left);
        CHECK(b.right() == f.right);
        CHECK(b.y == f.surfaceY());
        // Filled down to its floor and under it (no floating ribbon), whatever its level.
        CHECK(b.bottom() >= f.floorY + BossHazards::Flood::kSolidBelow);
        CHECK(b.h >= f.level() + BossHazards::Flood::kSolidBelow);
    }
}

TEST_CASE("liquids: each look is see-through at its surface, darker and denser deeper") {
    for (const char* look : {"", "lava", "plasma", "tide", "acid"}) {
        const LiquidLook l = liquidLook(look, Color{157, 255, 58, 255});
        CHECK(l.shallow.a < 255);
        CHECK(l.shallow.a >= 150);  // a body, not a veil
        CHECK(l.deep.a >= l.shallow.a);
        CHECK(l.deep.r + l.deep.g + l.deep.b < l.shallow.r + l.shallow.g + l.shallow.b);
    }
    // The acid takes the boss's light at its crest; the dark water does not glow from within.
    CHECK(liquidLook("acid", Color{157, 255, 58, 255}).crest.g == 255);
    CHECK(liquidLook("acid", Color{157, 255, 58, 255}).glowsInside);
    CHECK(!liquidLook("tide", Color{157, 255, 58, 255}).glowsInside);
}
