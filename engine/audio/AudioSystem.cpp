#include "engine/audio/AudioSystem.h"

#include <algorithm>

#include "engine/core/Log.h"

namespace cosmic {

namespace {
constexpr int kEffectTracks = 32;

void playTrack(MIX_Track* track, int loops, int fadeInMs) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
    if (fadeInMs > 0) SDL_SetNumberProperty(props, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, fadeInMs);
    MIX_PlayTrack(track, props);
    SDL_DestroyProperties(props);
}
}  // namespace

bool AudioSystem::init() {
    if (!MIX_Init()) {
        Log::warn("MIX_Init failed ({}); audio disabled", SDL_GetError());
        return false;
    }
    mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!mixer_) {
        Log::warn("No audio device ({}); audio disabled", SDL_GetError());
        return false;
    }
    for (int i = 0; i < kEffectTracks; ++i) {
        if (MIX_Track* t = MIX_CreateTrack(mixer_)) effectTracks_.push_back(t);
    }
    for (auto& m : music_) m.track = MIX_CreateTrack(mixer_);
    ambience_ = MIX_CreateTrack(mixer_);
    setVolumes(volumes_);
    Log::info("Audio ready ({} effect voices)", effectTracks_.size());
    return true;
}

void AudioSystem::shutdown() {
    if (mixer_) {
        MIX_StopAllTracks(mixer_, 0);
        for (auto* t : effectTracks_) MIX_DestroyTrack(t);
        effectTracks_.clear();
        for (auto& m : music_) {
            if (m.track) MIX_DestroyTrack(m.track);
            m.track = nullptr;
        }
        if (ambience_) MIX_DestroyTrack(ambience_);
        ambience_ = nullptr;
        for (auto& [p, a] : cache_) MIX_DestroyAudio(a);
        cache_.clear();
        MIX_DestroyMixer(mixer_);
        mixer_ = nullptr;
    }
    MIX_Quit();
}

MIX_Audio* AudioSystem::load(const std::string& path, bool predecode) {
    if (!mixer_ || path.empty()) return nullptr;
    auto it = cache_.find(path);
    if (it != cache_.end()) return it->second;
    MIX_Audio* a = MIX_LoadAudio(mixer_, path.c_str(), predecode);
    if (!a) Log::warn("Cannot load audio {}: {}", path, SDL_GetError());
    cache_[path] = a;
    return a;
}

float AudioSystem::busGain(AudioBus bus) const {
    switch (bus) {
        case AudioBus::Music: return volumes_.music;
        case AudioBus::Effects: return volumes_.effects;
        case AudioBus::Ambience: return volumes_.ambience;
        case AudioBus::Interface: return volumes_.effects;
    }
    return 1.0f;
}

MIX_Track* AudioSystem::acquireEffectTrack() {
    if (effectTracks_.empty()) return nullptr;
    for (size_t i = 0; i < effectTracks_.size(); ++i) {
        size_t idx = (nextEffect_ + i) % effectTracks_.size();
        if (!MIX_TrackPlaying(effectTracks_[idx])) {
            nextEffect_ = idx + 1;
            return effectTracks_[idx];
        }
    }
    // All voices busy: steal the oldest round-robin voice.
    MIX_Track* t = effectTracks_[nextEffect_ % effectTracks_.size()];
    nextEffect_ = (nextEffect_ + 1) % effectTracks_.size();
    MIX_StopTrack(t, 0);
    return t;
}

void AudioSystem::playSound(const std::string& path, float volume, float pitch, float pan, AudioBus bus) {
    if (!mixer_) return;
    MIX_Audio* a = load(path, true);
    if (!a) return;
    MIX_Track* t = acquireEffectTrack();
    if (!t) return;
    MIX_SetTrackAudio(t, a);
    MIX_SetTrackGain(t, std::clamp(volume, 0.0f, 2.0f) * busGain(bus));
    MIX_SetTrackFrequencyRatio(t, std::clamp(pitch, 0.25f, 4.0f));
    pan = std::clamp(pan, -1.0f, 1.0f);
    MIX_StereoGains g{std::min(1.0f, 1.0f - pan), std::min(1.0f, 1.0f + pan)};
    MIX_SetTrackStereo(t, &g);
    playTrack(t, 0, 0);
}

void AudioSystem::applyMusicGain() {
    if (!mixer_) return;
    for (auto& m : music_)
        if (m.track) MIX_SetTrackGain(m.track, volumes_.music * duck_ * m.baseGain);
    if (ambience_) MIX_SetTrackGain(ambience_, volumes_.ambience);
}

void AudioSystem::playMusic(const std::string& path, int fadeMs) {
    if (path == musicPath_) return;
    musicPath_ = path;
    if (!mixer_) return;
    MusicTrack& old = music_[activeMusic_];
    if (old.track && MIX_TrackPlaying(old.track))
        MIX_StopTrack(old.track, MIX_TrackMSToFrames(old.track, fadeMs));
    if (path.empty()) return;
    activeMusic_ = 1 - activeMusic_;
    MusicTrack& cur = music_[activeMusic_];
    MIX_Audio* a = load(path, false);
    if (!a || !cur.track) return;
    cur.path = path;
    MIX_SetTrackAudio(cur.track, a);
    applyMusicGain();
    playTrack(cur.track, -1, fadeMs);
}

void AudioSystem::stopMusic(int fadeMs) {
    musicPath_.clear();
    if (!mixer_) return;
    for (auto& m : music_)
        if (m.track && MIX_TrackPlaying(m.track)) MIX_StopTrack(m.track, MIX_TrackMSToFrames(m.track, fadeMs));
}

void AudioSystem::setMusicDuck(float factor) {
    duck_ = std::clamp(factor, 0.0f, 1.0f);
    applyMusicGain();
}

void AudioSystem::playAmbience(const std::string& path, int fadeMs) {
    if (path == ambiencePath_) return;
    ambiencePath_ = path;
    if (!mixer_ || !ambience_) return;
    if (MIX_TrackPlaying(ambience_)) MIX_StopTrack(ambience_, 0);
    if (path.empty()) return;
    MIX_Audio* a = load(path, false);
    if (!a) return;
    MIX_SetTrackAudio(ambience_, a);
    applyMusicGain();
    playTrack(ambience_, -1, fadeMs);
}

void AudioSystem::stopAmbience(int fadeMs) {
    ambiencePath_.clear();
    if (mixer_ && ambience_ && MIX_TrackPlaying(ambience_))
        MIX_StopTrack(ambience_, MIX_TrackMSToFrames(ambience_, fadeMs));
}

void AudioSystem::setVolumes(const AudioVolumes& v) {
    volumes_ = v;
    if (!mixer_) return;
    MIX_SetMixerGain(mixer_, std::clamp(v.master, 0.0f, 1.0f));
    applyMusicGain();
}

void AudioSystem::clearCache() {
    if (!mixer_) return;
    for (auto it = cache_.begin(); it != cache_.end();) {
        bool inUse = false;
        for (auto& m : music_)
            if (m.path == it->first) inUse = true;
        if (it->first == ambiencePath_) inUse = true;
        if (!inUse) {
            // MIX_DestroyAudio is safe while tracks play: SDL_mixer refcounts audio.
            if (it->second) MIX_DestroyAudio(it->second);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace cosmic
