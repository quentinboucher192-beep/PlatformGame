#pragma once

// 2.79 (docs/RAIDS.md, « Joueurs simulés : terrain et anticipation »): what a simulated player thinks of
// its moves — « ce qu'il pense » côté déplacement —, readable on the screen next to the rest of its
// thoughts: « Saut vers la plateforme mobile », « Double saut au-dessus du vide », « Esquive du rayon »,
// « Remonte au-dessus du magma »…
//
// The text is French (the source language); the screen translates it when it draws it
// (data/i18n/en.json, docs/I18N.md). Display only: never read by the simulation.

#include <cstdint>

namespace cosmic::raid::nav {

enum class Intent : uint8_t {
    None,           // nothing to do (no goal)
    Observe,        // a zone just entered: it looks at it (its graph is being built)
    Walk,           // walks toward its goal
    Wait,           // waits at its goal
    Climb,          // up or down a ladder
    Drop,           // lets itself drop to a lower floor
    DropThrough,    // through the one-way platform under it
    Jump,           // a jump to another floor
    JumpMover,      // a jump onto a moving platform (where it WILL be)
    WaitMover,      // waits for the moving platform to come
    Ride,           // carried by the moving platform
    DoubleJump,     // a double jump
    DoubleJumpVoid, // a double jump over the void
    TripleJump,     // a triple jump
    Dash,           // a dash in the air
    DashVoid,       // a dash over the void
    LongJump,       // a dash on the ground, then a jump
    Impulse,        // the impulse, upward
    WallJump,       // a jump off a wall
    Recover,        // falling: catches itself (an air jump, a dash, the impulse)
    DodgeShot,      // out of the path of a shot
    DodgeBeam,      // out of a beam (a laser)
    DodgeBlow,      // out of a blow announced (a fist, a jet, a mark)
    Duck,           // crouched under a shot or a blow
    FleeLiquid,     // up above the rising magma
    Shelter,        // up above another rising danger (plasma, souls, embers…)
    AvoidHazard,    // keeps out of a danger (spikes, a deadly liquid)
    Stuck,          // no way found: it looks for another one
    Count
};

/// The thought in French (translated when drawn), e.g. « Double saut au-dessus du vide ».
const char* intentText(Intent i);

/// A seat's thought now and since when (the step it began).
struct Thought {
    Intent intent = Intent::None;
    uint32_t since = 0;
};

}  // namespace cosmic::raid::nav
