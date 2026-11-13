#include "engine/animation/Animation.h"

#include "engine/core/Log.h"

namespace cosmic {

float AnimationClip::totalDuration() const {
    float t = 0.0f;
    for (float d : durations) t += d;
    return t;
}

const AnimationClip* AnimationSet::clip(const std::string& name) const {
    auto it = clips.find(name);
    return it == clips.end() ? nullptr : &it->second;
}

Rect AnimationSet::frameRect(const AnimationClip& c, int frame) const {
    int col = c.start + std::clamp(frame, 0, std::max(0, c.frameCount() - 1));
    return {static_cast<float>(col * frameWidth), static_cast<float>(c.row * frameHeight),
            static_cast<float>(frameWidth), static_cast<float>(frameHeight)};
}

std::shared_ptr<AnimationSet> AnimationSet::fromJson(const Json& j, const std::string& source) {
    auto set = std::make_shared<AnimationSet>();
    set->sourcePath = source;
    set->texturePath = jstr(j, "texture");
    set->frameWidth = std::max(1, jget<int>(j, "frameWidth", 16));
    set->frameHeight = std::max(1, jget<int>(j, "frameHeight", 16));
    set->pivot = vec2FromJson(j.value("pivot", Json()), {set->frameWidth * 0.5f, static_cast<float>(set->frameHeight)});
    if (j.contains("clips") && j["clips"].is_object()) {
        for (auto& [name, cj] : j["clips"].items()) {
            AnimationClip c;
            c.name = name;
            c.row = jget<int>(cj, "row", 0);
            c.start = jget<int>(cj, "start", 0);
            int count = std::max(1, jget<int>(cj, "count", 1));
            float fps = std::max(0.1f, jget<float>(cj, "fps", 10.0f));
            c.durations.assign(count, 1.0f / fps);
            if (cj.contains("durations") && cj["durations"].is_array()) {
                const auto& dj = cj["durations"];
                for (int i = 0; i < count && i < static_cast<int>(dj.size()); ++i)
                    if (dj[i].is_number()) c.durations[i] = std::max(0.001f, dj[i].get<float>());
            }
            c.loop = jget<bool>(cj, "loop", true);
            c.next = jstr(cj, "next");
            if (cj.contains("events") && cj["events"].is_array()) {
                for (const auto& ej : cj["events"]) c.events.push_back({jget<int>(ej, "frame", 0), jstr(ej, "name")});
            }
            set->clips.emplace(name, std::move(c));
        }
    }
    if (set->clips.empty()) Log::warn("Animation {} has no clips", source);
    return set;
}

Json AnimationSet::toJson() const {
    Json j;
    j["texture"] = texturePath;
    j["frameWidth"] = frameWidth;
    j["frameHeight"] = frameHeight;
    j["pivot"] = cosmic::toJson(pivot);
    Json clipsJ = Json::object();
    for (const auto& [name, c] : clips) {
        Json cj;
        cj["row"] = c.row;
        if (c.start) cj["start"] = c.start;
        cj["count"] = c.frameCount();
        cj["durations"] = c.durations;
        cj["loop"] = c.loop;
        if (!c.next.empty()) cj["next"] = c.next;
        if (!c.events.empty()) {
            Json ev = Json::array();
            for (const auto& e : c.events) ev.push_back({{"frame", e.frame}, {"name", e.name}});
            cj["events"] = ev;
        }
        clipsJ[name] = cj;
    }
    j["clips"] = clipsJ;
    return j;
}

// ------------------------------------------------------------------- Animator

void Animator::setSet(AnimationSetPtr set) {
    set_ = std::move(set);
    clip_ = nullptr;
    currentName_.clear();
}

bool Animator::has(const std::string& name) const { return set_ && set_->clip(name); }

void Animator::play(const std::string& name, bool restart) {
    if (!set_) return;
    if (!restart && name == currentName_ && clip_) return;
    const AnimationClip* c = set_->clip(name);
    if (!c) return;
    clip_ = c;
    currentName_ = name;
    frame_ = 0;
    timer_ = 0.0f;
    finished_ = false;
    eventsFired_ = false;
}

void Animator::fireEvents(const EventHandler& onEvent) {
    if (eventsFired_ || !clip_) return;
    eventsFired_ = true;
    if (!onEvent) return;
    for (const auto& e : clip_->events)
        if (e.frame == frame_) onEvent(e.name);
}

void Animator::update(float dt, const EventHandler& onEvent) {
    if (!clip_ || clip_->durations.empty()) return;
    fireEvents(onEvent);
    if (finished_) return;
    timer_ += dt * speed;
    // Bounded loop so a huge dt can never spin forever.
    for (int guard = 0; guard < 64 && timer_ >= clip_->durations[frame_]; ++guard) {
        timer_ -= clip_->durations[frame_];
        if (frame_ + 1 < clip_->frameCount()) {
            ++frame_;
        } else if (clip_->loop) {
            frame_ = 0;
        } else {
            finished_ = true;
            timer_ = 0.0f;
            if (!clip_->next.empty() && set_->clip(clip_->next)) {
                std::string nxt = clip_->next;
                play(nxt, true);
            }
            break;
        }
        eventsFired_ = false;
        fireEvents(onEvent);
    }
}

Rect Animator::sourceRect() const {
    if (!set_ || !clip_) return {};
    return set_->frameRect(*clip_, frame_);
}

Rect Animator::destRect(Vec2 anchor, bool flipX, float scale) const {
    if (!set_) return {};
    float px = flipX ? (set_->frameWidth - set_->pivot.x) : set_->pivot.x;
    return {anchor.x - px * scale, anchor.y - set_->pivot.y * scale, set_->frameWidth * scale,
            set_->frameHeight * scale};
}

float Animator::clipProgress() const {
    if (!clip_) return 1.0f;
    if (finished_) return 1.0f;
    float total = clip_->totalDuration(), t = 0.0f;
    for (int i = 0; i < frame_; ++i) t += clip_->durations[i];
    return total > 0 ? std::clamp((t + timer_) / total, 0.0f, 1.0f) : 1.0f;
}

}  // namespace cosmic
