#pragma once

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/levels/DifficultyRating.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class ContentDatabase;

/// What the campaign steps did to one level (2.45).
struct CampaignPipelineReport {
    int gatesAdded = 0;      // seals: gates against the shortcuts found
    int stations = 0;        // power stations planned (each: a sign and its tablets)
    int tablets = 0;
    int stationsRemoved = 0; // the stations of before, replaced by the new plan
    DifficultyRating rating;
    std::vector<std::string> notes;  // what the admin should know (a checkpoint skipped…)
};

/// The steps of the campaign pipeline of tools/levelgen, on ONE level edited by an
/// administrator before it is published as a revision (ADMINISTRATION › Campagne), so that it
/// stays as consistent as the generated levels:
///   1. seals (--seal-levels): the shortcut detector (analyzeLevel) and its gates
///      (suggestGates), solid columns of the level's most common material, added like
///      levelkit.apply_seals (ids seal_<n>, props.seal);
///   2. power stations (--plan-stations): the stations of before are removed (the generator
///      builds a level without them), then planPowerStations places the tablets and their
///      sign like levelkit.apply_power_slots;
///   3. rating (--rate-levels): rateDifficulty, sent with the revision.
/// `revision`: metadata.revision of the new revision (part of its fingerprint).
LevelData prepareCampaignRevision(const LevelData& level, const ContentDatabase& content, int revision, CampaignPipelineReport* report = nullptr);

}  // namespace cosmic
