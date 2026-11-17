#include "ui/screens/PauseLayout.h"

#include <algorithm>

namespace cosmic {

PauseLayout pauseLayout(int buttons, int rows, bool note, float scale, float screenH, bool section) {
    buttons = std::max(0, buttons);
    rows = std::clamp(rows, 0, 5);
    PauseLayout roomy;  // the comfortable sizes (those of the struct)
    roomy.section = section;
    PauseLayout tight;
    tight.section = section;
    tight.top = 176.0f;
    tight.buttonH = 48.0f;
    tight.gap = 8.0f;
    tight.sectionGap = 4.0f;
    tight.headerH = 36.0f;
    tight.rowH = 36.0f;
    tight.emptyH = 28.0f;
    tight.noteH = 22.0f;
    tight.footer = 70.0f;
    auto total = [&](const PauseLayout& l) { return l.sectionTop(buttons) + l.sectionHeight(rows, note) + l.footer; };
    // Room on screen (unscaled), 20 px kept free above and below the panel.
    const float room = screenH / std::max(scale, 0.01f) - 40.0f;
    const float a = total(roomy), b = total(tight);
    const float t = a <= room ? 0.0f : std::clamp((a - room) / std::max(a - b, 0.001f), 0.0f, 1.0f);
    auto mix = [t](float x, float y) { return x + (y - x) * t; };
    PauseLayout l;
    l.section = section;
    l.top = mix(roomy.top, tight.top);
    l.buttonH = mix(roomy.buttonH, tight.buttonH);
    l.gap = mix(roomy.gap, tight.gap);
    l.sectionGap = mix(roomy.sectionGap, tight.sectionGap);
    l.headerH = mix(roomy.headerH, tight.headerH);
    l.rowH = mix(roomy.rowH, tight.rowH);
    l.emptyH = mix(roomy.emptyH, tight.emptyH);
    l.noteH = mix(roomy.noteH, tight.noteH);
    l.footer = mix(roomy.footer, tight.footer);
    l.height = total(l);
    return l;
}

float PauseSideTracked::height(const std::vector<int>& descLines, bool note) const {
    float h = headerH;
    const int rows = static_cast<int>(std::min<size_t>(descLines.size(), PauseLayout::kPlaces));
    for (int i = 0; i < rows; ++i) h += rowHeight(descLines[static_cast<size_t>(i)]);
    h += static_cast<float>(PauseLayout::kPlaces - rows) * (freeH + 8.0f);
    return h + (note ? noteH : 0.0f);
}

}  // namespace cosmic
