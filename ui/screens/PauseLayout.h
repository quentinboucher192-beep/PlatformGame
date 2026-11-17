#pragma once

#include <vector>
// 2.52: the pause menu's column, measured: its buttons, then « Succès suivis » (one row per
// achievement followed, or a line inviting to follow some), then the hints. Everything is in
// unscaled units (1920 x 1080); when the window is too short for the comfortable sizes, the
// gaps, the buttons and the rows shrink together (never under their compact sizes) so that the
// whole column stays on screen and nothing overlaps.
// 2.60: when the map is shown on the right, « Succès suivis » moves under it (PauseSideTracked):
// the five places, each achievement with its description; the column then holds the buttons.

namespace cosmic {

struct PauseLayout {
    float top = 200.0f;        // the first button, from the panel's top (title, level, run's stats above)
    float buttonH = 64.0f;
    float gap = 16.0f;         // under each button
    float sectionGap = 10.0f;  // between the last button and « Succès suivis »
    float headerH = 44.0f;     // the section's title line (with its « Modifier » button)
    float rowH = 66.0f;        // one achievement followed: name and numbers, its description, its bar
    float emptyH = 34.0f;      // the line shown when none is followed (or how many places are free)
    float noteH = 26.0f;       // « * compté après la partie » (rows that do not move in a run)
    float footer = 84.0f;      // under the section: the hints (Valider, Reprendre)
    float height = 0.0f;       // the panel
    bool section = true;       // « Succès suivis » in the column (false: under the map, on the right)
    /// Where the section starts, from the panel's top.
    float sectionTop(int buttons) const { return top + static_cast<float>(buttons) * (buttonH + gap) + sectionGap; }
    /// The section's height (title, rows, the line of the free places or the empty line, the note).
    float sectionHeight(int rows, bool note) const {
        if (!section) return 0.0f;
        return headerH + static_cast<float>(rows) * rowH + (rows < kPlaces ? emptyH : 0.0f) + (note ? noteH : 0.0f);
    }
    /// Room for the description under each name (else the name and the bar only).
    bool rowDescription() const { return rowH >= 54.0f; }
    static constexpr int kPlaces = 5;  // achievements followed at most (AchievementBook::kMaxTracked)
};

/// « Succès suivis » under the map (2.60), in unscaled units: the title line, then the five
/// places: each achievement followed (its name and numbers, its description on `descLines`
/// lines, its bar), each free place (a dashed slot); the note under them.
struct PauseSideTracked {
    float headerH = 44.0f;
    float nameH = 26.0f;
    float lineH = 20.0f;   // a line of description
    float barH = 7.0f;
    float rowGap = 12.0f;  // under each place
    float freeH = 34.0f;   // a free place
    float noteH = 24.0f;
    float rowHeight(int descLines) const { return nameH + static_cast<float>(descLines) * lineH + 6.0f + barH + rowGap; }
    /// The section's height: `descLines` holds the lines of each row followed.
    float height(const std::vector<int>& descLines, bool note) const;
};

/// The layout of a pause menu of `buttons` buttons and `rows` achievements followed (`note`: the
/// footnote line is shown), in a window `screenH` pixels high at UI scale `scale`.
/// `section` false: « Succès suivis » is drawn elsewhere (under the map).
PauseLayout pauseLayout(int buttons, int rows, bool note, float scale, float screenH, bool section = true);

}  // namespace cosmic
