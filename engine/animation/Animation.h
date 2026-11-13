#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"

namespace cosmic {

struct AnimationEvent {
    int frame = 0;
    std::string name;
};

/// One row of a sprite sheet played as an animation.
struct AnimationClip {
    std::string name;
    int row = 0;
    int start = 0;
    std::vector<float> durations;  // seconds per frame
    bool loop = true;
    std::string next;              // clip to chain to when a non-looping clip ends
    std::vector<AnimationEvent> events;

    int frameCount() const { return static_cast<int>(durations.size()); }
    float totalDuration() const;
};

/// A sprite sheet with its clips (loaded from assets/animations/<name>.json).
struct AnimationSet {
    std::string sourcePath;   // relative path of the json
    std::string texturePath;  // relative path of the texture
    TexturePtr texture;
    int frameWidth = 16;
    int frameHeight = 16;
    Vec2 pivot{8, 16};
    std::unordered_map<std::string, AnimationClip> clips;

    const AnimationClip* clip(const std::string& name) const;
    Rect frameRect(const AnimationClip& c, int frame) const;
    /// Parses the JSON description. The texture must be assigned by the caller.
    static std::shared_ptr<AnimationSet> fromJson(const Json& j, const std::string& source);
    Json toJson() const;
};

using AnimationSetPtr = std::shared_ptr<AnimationSet>;

/// Plays clips of an AnimationSet and reports frame events.
class Animator {
public:
    using EventHandler = std::function<void(const std::string& event)>;

    void setSet(AnimationSetPtr set);
    const AnimationSetPtr& set() const { return set_; }

    /// Starts `name`; does nothing if already playing unless `restart`.
    void play(const std::string& name, bool restart = false);
    bool has(const std::string& name) const;
    void update(float dt, const EventHandler& onEvent = {});

    const std::string& current() const { return currentName_; }
    int frame() const { return frame_; }
    bool finished() const { return finished_; }
    float speed = 1.0f;

    /// Source rectangle of the current frame in the texture.
    Rect sourceRect() const;
    /// World rectangle for drawing at `anchor` (bottom-centre of the collision box).
    Rect destRect(Vec2 anchor, bool flipX, float scale = 1.0f) const;
    float clipProgress() const;

private:
    void fireEvents(const EventHandler& onEvent);

    AnimationSetPtr set_;
    const AnimationClip* clip_ = nullptr;
    std::string currentName_;
    int frame_ = 0;
    float timer_ = 0.0f;
    bool finished_ = false;
    bool eventsFired_ = false;
};

}  // namespace cosmic
