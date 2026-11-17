#pragma once

// 2.61 Studio de Créations: the colours of the validated mock-up, shared by its panels.

#include "engine/core/Math.h"

namespace cosmic::studio::style {

constexpr Color kPanel{15, 23, 48, 255};       // the panels
constexpr Color kBorder{34, 48, 90, 255};      // their edges
constexpr Color kField{10, 17, 36, 255};       // fields, buttons at rest
constexpr Color kSel{18, 48, 74, 255};         // a chosen chip, a pressed tool
constexpr Color kSelRow{29, 74, 94, 255};      // the selected row of the explorer
constexpr Color kAccent{62, 230, 214, 255};
constexpr Color kText{232, 238, 252, 255};
constexpr Color kSub{201, 211, 238, 255};
constexpr Color kDim{143, 155, 189, 255};
constexpr Color kWarm{255, 180, 84, 255};
constexpr Color kViolet{166, 107, 255, 255};
constexpr Color kVioletBg{28, 20, 54, 255};
constexpr Color kDangerBg{29, 14, 23, 255};
constexpr Color kDangerEdge{90, 36, 51, 255};
constexpr Color kDangerText{255, 179, 193, 255};
constexpr Color kCheckA{24, 33, 63, 255};      // the checkerboard under transparent pixels
constexpr Color kCheckB{18, 26, 51, 255};
constexpr Color kTip{5, 9, 20, 250};
/// The colours of the rows of an imported sheet (zones numbered row by row).
constexpr Color kRowColors[6] = {{62, 230, 214, 255}, {166, 107, 255, 255}, {255, 180, 84, 255},
                                 {120, 230, 140, 255}, {255, 110, 150, 255}, {110, 170, 255, 255}};

}  // namespace cosmic::studio::style
