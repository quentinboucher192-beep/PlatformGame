#pragma once

// 2.92 — the level drawn in the strips of the training screens (ENTRAÎNEMENT › « Segments et
// enchaînements » and « Startpos et mesures », Pause › « Startpos et mesures »): the level's own
// picture — its terrain, tiles and decor along its whole width, made off the frame's path by the
// pictures of the « Statistiques » screen (statsart::LevelArt::band) — and the schematic bar
// until it is ready (or for a level that cannot be drawn), never an empty box. It tells where a
// place of the level lies in the strip (and back), the same with or without the picture, so that
// what is drawn over it (segments, names, the range chosen, StartPos, measure gates) stays where
// it is in the level.

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "ui/screens/StatisticsArt.h"

namespace cosmic {

class GameServices;

class TrainingStrip {
public:
    /// `name`: the strip's name in the tests' reports (last()).
    explicit TrainingStrip(std::string name) : name_(std::move(name)) {}

    /// Draws the level in `r`: its picture once made (`art`, the level `levelId`), else the
    /// schematic bar. `area` (empty: `r`) is all the strip takes, the names under it included
    /// (the tests check that what is drawn stays inside). True when the picture was drawn.
    bool draw(GameServices& g, statsart::LevelArt* art, const std::string& levelId, const Rect& levelBounds, const Rect& r, const Rect& area = {});
    /// Where a place of the level is in the strip: x from side to side; y in the band of the
    /// picture (its middle while the level is being read).
    Vec2 toScreen(Vec2 world) const;
    float xOf(float worldX) const { return toScreen({worldX, 0.0f}).x; }
    /// The place of the level at a point of the strip.
    Vec2 toWorld(Vec2 screen) const;
    /// The level is read: toScreen / toWorld follow its band (else x only).
    bool mapped() const { return mapped_; }
    const statsart::LevelInfo* info() const { return info_; }
    const Rect& rect() const { return rect_; }
    /// World pixels per strip pixel across.
    float worldPerPixel() const;

    /// What a frame drew there (tests): the strip, whether it was the picture, what was drawn
    /// over it (note()).
    struct Report {
        std::string levelId;
        Rect strip, area;
        bool picture = false;
        std::vector<Rect> overlays;
    };
    /// Something drawn over the strip or around it (a name, a segment, a mark), for the tests.
    void note(const Rect& overlay);
    /// The last frame's report of the strip called `name` (empty when none was drawn).
    static const Report& last(const std::string& name);

private:
    std::string name_;
    Rect rect_, bounds_;
    Vec2 picture_{1.0f, 1.0f};  // the picture's size (the band's pixels)
    statsart::LevelBand band_;
    bool mapped_ = false;
    const statsart::LevelInfo* info_ = nullptr;
};

}  // namespace cosmic
