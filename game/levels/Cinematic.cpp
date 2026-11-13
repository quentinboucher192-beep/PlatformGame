#include "game/levels/Cinematic.h"

#include "engine/audio/AudioSystem.h"
#include "engine/core/Easing.h"
#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

void CinematicPlayer::start(const CinematicDef& def, GameWorld& w) {
    def_ = def;
    index_ = 0;
    running_.clear();
    active_ = !def.steps.empty();
    letterboxTarget_ = def.letterbox ? 1.0f : 0.0f;
    title_.clear();
    subtitle_.clear();
    dialogue_.clear();
    speaker_.clear();
    titleDur_ = dialogueDur_ = 0.0f;
    w.player().controlEnabled = false;
    if (!active_) finish(w);
}

Vec2 CinematicPlayer::resolveTarget(GameWorld& w, const Json& p) const {
    if (jget<bool>(p, "player", false)) return w.player().center() + Vec2{0, -20};
    if (p.contains("target")) {
        if (LevelEntity* e = w.entity(jstr(p, "target"))) return e->box.center();
    }
    if (p.contains("start") && jget<bool>(p, "start", false)) return w.respawnPoint() - Vec2{0, 30};
    return {jget<float>(p, "x", w.camera().center().x), jget<float>(p, "y", w.camera().center().y)};
}

void CinematicPlayer::beginStep(GameWorld& w, const CinematicStep& s) {
    const Json& p = s.params;
    const std::string& t = s.type;
    if (t == "camera" || t == "focus") {
        w.camera().moveTo(resolveTarget(w, p), s.duration, easeFromString(jstr(p, "ease", "inOutCubic")));
    } else if (t == "cut") {
        Vec2 c = resolveTarget(w, p);
        w.camera().moveTo(c, 0.0001f);
    } else if (t == "zoom") {
        w.camera().zoomTo(jget<float>(p, "zoom", 1.0f), s.duration, easeFromString(jstr(p, "ease", "inOutCubic")));
    } else if (t == "text") {
        title_ = jstr(p, "text");
        subtitle_ = jstr(p, "subtitle");
        caption_ = jstr(p, "style", "title") == "caption";
        titleT_ = 0.0f;
        titleDur_ = s.duration;
    } else if (t == "dialogue") {
        speaker_ = jstr(p, "speaker");
        dialogue_ = jstr(p, "text");
        dialogueT_ = 0.0f;
        dialogueDur_ = s.duration;
    } else if (t == "spawnPlayer") {
        w.player().spawn(w, w.respawnPoint(), false, true);
        w.player().controlEnabled = false;
        w.playSound("sounds/respawn.wav", 0.9f);
        w.particles().emit("teleport_in", w.player().center(), 1.5f);
    } else if (t == "hidePlayer") {
        w.player().setHidden(true);
    } else if (t == "playerAnim") {
        w.player().forceAnimation(jstr(p, "anim", "victory"), s.duration);
    } else if (t == "playerWalk") {
        w.player().setCinematicWalk(jget<float>(p, "speed", 60.0f));
    } else if (t == "shake") {
        w.shake(jget<float>(p, "amount", 0.5f));
    } else if (t == "flash") {
        w.flash(colorFromJson(p.value("color", Json()), Color::white()), s.duration);
    } else if (t == "fade") {
        fadeFrom_ = fade_;
        fadeTo_ = jget<float>(p, "to", 1.0f);
        fadeT_ = 0.0f;
        fadeDur_ = std::max(0.01f, s.duration);
    } else if (t == "sound") {
        w.playSound(jstr(p, "sound"), jget<float>(p, "volume", 1.0f));
    } else if (t == "music") {
        w.setMusic(jstr(p, "music"));
    } else if (t == "letterbox") {
        letterboxTarget_ = jget<bool>(p, "on", true) ? 1.0f : 0.0f;
    } else if (t == "effect") {
        w.particles().emit(jstr(p, "effect", "explosion"), resolveTarget(w, p), jget<float>(p, "scale", 1.0f));
    } else if (t == "action") {
        EventAction a;
        a.type = jstr(p, "action");
        a.target = jstr(p, "target");
        a.params = p;
        w.events().runAction(w, a);
    } else if (t != "wait") {
        Log::warn("Unknown cinematic step '{}'", t);
    }
}

void CinematicPlayer::fixedUpdate(GameWorld& w, float dt) {
    letterbox_ = approach(letterbox_, active_ ? letterboxTarget_ : 0.0f, dt * 2.5f);
    if (fadeT_ < fadeDur_) {
        fadeT_ += dt;
        fade_ = lerpf(fadeFrom_, fadeTo_, std::min(1.0f, fadeT_ / fadeDur_));
    }
    if (titleDur_ > 0.0f) titleT_ += dt;
    if (dialogueDur_ > 0.0f) dialogueT_ += dt;
    if (!active_) return;

    for (auto& r : running_) r.elapsed += dt;
    running_.erase(std::remove_if(running_.begin(), running_.end(),
                                  [&](const Running& r) {
                                      bool done = r.elapsed >= r.step.duration;
                                      if (done && r.step.type == "playerWalk") w.player().setCinematicWalk(0.0f);
                                      return done;
                                  }),
                   running_.end());

    // Advance while the current blocking step is finished.
    while (index_ < def_.steps.size()) {
        bool blocked = false;
        for (const auto& r : running_)
            if (r.step.wait) blocked = true;
        if (blocked) break;
        const CinematicStep& s = def_.steps[index_++];
        beginStep(w, s);
        running_.push_back({s, 0.0f});
    }
    if (index_ >= def_.steps.size() && running_.empty()) finish(w);
}

void CinematicPlayer::skip(GameWorld& w) {
    if (!active_ || !def_.skippable) return;
    // Apply the lasting effects of the remaining steps (player appearance, music...).
    for (size_t i = index_; i < def_.steps.size(); ++i) {
        const auto& s = def_.steps[i];
        if (s.type == "spawnPlayer" || s.type == "music" || s.type == "action") beginStep(w, s);
    }
    running_.clear();
    index_ = def_.steps.size();
    titleDur_ = dialogueDur_ = 0.0f;
    fade_ = fadeTo_ < 0.5f ? 0.0f : fade_;
    finish(w);
}

void CinematicPlayer::finish(GameWorld& w) {
    active_ = false;
    w.player().setCinematicWalk(0.0f);
    w.camera().releaseScript();
    if (w.state() == WorldState::Playing) w.player().controlEnabled = true;
    if (w.state() == WorldState::Intro) w.beginPlay();
}

float CinematicPlayer::titleAlpha() const {
    if (titleDur_ <= 0.0f || titleT_ >= titleDur_) return 0.0f;
    float in = std::min(1.0f, titleT_ / 0.5f);
    float out = std::min(1.0f, (titleDur_ - titleT_) / 0.6f);
    return std::min(in, out);
}

float CinematicPlayer::dialogueAlpha() const {
    if (dialogueDur_ <= 0.0f || dialogueT_ >= dialogueDur_) return 0.0f;
    return std::min({1.0f, dialogueT_ / 0.25f, (dialogueDur_ - dialogueT_) / 0.3f});
}

}  // namespace cosmic
