#pragma once
// 2.52: the pause menu's column, measured: its buttons, then « Succès suivis » (one row per
// achievement followed, or a line inviting to follow some), then the hints. Everything is in
// unscaled units (1920 x 1080); when the window is too short for the comfortable sizes, the
// gaps, the buttons and the rows shrink together (never under their compact sizes) so that the
// whole column stays on screen and nothing overlaps.

namespace cosmic {

struct PauseLayout {
    float top = 200.0f;        // the first button, from the panel's top (title, level, run's stats above)
    float buttonH = 64.0f;
    float gap = 16.0f;         // under each button
    float sectionGap = 10.0f;  // between the last button and « Succès suivis »
    float headerH = 44.0f;     // the section's title line (with its « Modifier » button)
    float rowH = 46.0f;        // one achievement followed: name and numbers, then its bar
    float emptyH = 34.0f;      // the line shown when none is followed
    float noteH = 26.0f;       // « * compté après la partie » (rows that do not move in a run)
    float footer = 84.0f;      // under the section: the hints (Valider, Reprendre)
    float height = 0.0f;       // the panel
    /// Where the section starts, from the panel's top.
    float sectionTop(int buttons) const { return top + static_cast<float>(buttons) * (buttonH + gap) + sectionGap; }
    /// The section's height (title, rows or the empty line, the note).
    float sectionHeight(int rows, bool note) const { return headerH + (rows > 0 ? static_cast<float>(rows) * rowH : emptyH) + (note ? noteH : 0.0f); }
};

/// The layout of a pause menu of `buttons` buttons and `rows` achievements followed (`note`: the
/// footnote line is shown), in a window `screenH` pixels high at UI scale `scale`.
PauseLayout pauseLayout(int buttons, int rows, bool note, float scale, float screenH);

}  // namespace cosmic
