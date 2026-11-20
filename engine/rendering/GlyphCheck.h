#pragma once

// 2.79: the characters a text needs that the font of its face does not have. Orbitron, the titles' face,
// has no « · », no « « » nor « » » and no « ▼ »: drawn, each would be an empty box (« RAID COMPLET ▯ TEST
// LOCAL » in the raid's panel). TextRenderer measures and draws such a text in the bold body face instead
// (never a box) and remembers the miss here: the tests draw the raid's panels and the contributions table,
// texts composed while the game runs included, and require none (tests/test_font_glyphs.cpp).

#include <cstdint>
#include <string>
#include <vector>

namespace cosmic::glyphs {

struct Missing {
    int face = 0;            // FontFace
    uint32_t codepoint = 0;
    std::string text;        // the text (as displayed, in the language in use), its first 120 bytes
};

/// The misses since the last clear() (each text and character once, at most 256).
const std::vector<Missing>& missing();
void clear();

}  // namespace cosmic::glyphs
