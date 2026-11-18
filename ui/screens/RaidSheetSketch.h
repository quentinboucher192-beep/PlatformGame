#pragma once
// 2.73: the animated example of a gigaboss's mechanism on its raid sheet (ui/screens/
// RaidSheetsScene): its engine attacks are played by the game itself (ui/screens/BossAttackPreview);
// what its routine does around them (marks under every player, gazes, waves, plates held together,
// sectors turning over, the acid, the eye-light) is drawn here, with the gigaboss's own sprite and
// the clips of its effects sheet (the ones the combat draws), timed by its rules (the announce, the
// hold…; a long one shortened so the loop stays short, its real value in the caption). Display only.

#include <string>

#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
struct EnemyDefinition;
namespace raid {
struct SheetSketch;
}

/// Draws `k` in `frame` at time `t` (seconds since it is shown): `fx` the gigaboss's effects sheet
/// (animations/…), `boss` its definition (its sprite), `tint` its colour.
void drawRaidSketch(GameServices& g, const Rect& frame, const raid::SheetSketch& k, const std::string& fx, const EnemyDefinition* boss, Color tint,
                    float t);
/// The caption under it: what it shows, with the real numbers of the rules.
std::string raidSketchCaption(const raid::SheetSketch& k);

}  // namespace cosmic
