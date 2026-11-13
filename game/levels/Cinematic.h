#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class GameWorld;

/// Plays intro / outro sequences: scripted camera (travelling, zoom, focus), texts,
/// dialogues, player appearance, shakes, flashes, fades, music and sounds.
class CinematicPlayer {
public:
    void start(const CinematicDef& def, GameWorld& w);
    void fixedUpdate(GameWorld& w, float dt);
    void skip(GameWorld& w);
    bool active() const { return active_; }
    bool skippable() const { return def_.skippable; }

    // Presentation state read by the HUD
    float letterbox() const { return letterbox_; }
    float fade() const { return fade_; }
    const std::string& title() const { return title_; }
    const std::string& subtitle() const { return subtitle_; }
    float titleAlpha() const;
    const std::string& speaker() const { return speaker_; }
    const std::string& dialogue() const { return dialogue_; }
    float dialogueAlpha() const;
    bool titleIsCaption() const { return caption_; }

private:
    void beginStep(GameWorld& w, const CinematicStep& s);
    void finish(GameWorld& w);
    Vec2 resolveTarget(GameWorld& w, const Json& params) const;

    struct Running {
        CinematicStep step;
        float elapsed = 0.0f;
    };
    CinematicDef def_;
    size_t index_ = 0;
    std::vector<Running> running_;
    bool active_ = false;
    float letterbox_ = 0.0f, letterboxTarget_ = 0.0f;
    float fade_ = 0.0f, fadeFrom_ = 0.0f, fadeTo_ = 0.0f, fadeT_ = 0.0f, fadeDur_ = 0.0f;
    std::string title_, subtitle_;
    float titleT_ = 0.0f, titleDur_ = 0.0f;
    bool caption_ = false;
    std::string speaker_, dialogue_;
    float dialogueT_ = 0.0f, dialogueDur_ = 0.0f;
};

}  // namespace cosmic
