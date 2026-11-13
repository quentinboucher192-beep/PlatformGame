#pragma once

#include <SDL3_mixer/SDL_mixer.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cosmic {

enum class AudioBus { Music, Effects, Ambience, Interface };

struct AudioVolumes {
    float master = 0.9f;
    float music = 0.7f;
    float effects = 0.8f;
    float ambience = 0.6f;
};

/// Sound effects, music with cross-fades and ambience loops on top of SDL3_mixer.
/// If no audio device is available the system stays silent but fully functional.
class AudioSystem {
public:
    bool init();
    void shutdown();
    bool available() const { return mixer_ != nullptr; }

    /// Loads (and caches) a decoded sound. `path` is an absolute file path.
    MIX_Audio* load(const std::string& path, bool predecode);

    /// Plays a one-shot effect. `pan` in [-1, 1], `pitch` as frequency ratio.
    void playSound(const std::string& path, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f,
                   AudioBus bus = AudioBus::Effects);

    /// Starts a looping music with a cross-fade from the current one.
    void playMusic(const std::string& path, int fadeMs = 1200);
    void stopMusic(int fadeMs = 800);
    const std::string& currentMusic() const { return musicPath_; }
    /// Temporarily lowers music (e.g. pause menu); 1 = normal.
    void setMusicDuck(float factor);

    void playAmbience(const std::string& path, int fadeMs = 1500);
    void stopAmbience(int fadeMs = 800);

    /// Sustained looping effect (charging, gliding wind, updraft): starts it if needed and
    /// sets its volume. It keeps playing only while it is refreshed: updateLoops() fades out
    /// loops that were not refreshed recently (released button, paused game, closed level).
    void sustainLoop(const std::string& key, const std::string& path, float volume);
    void stopLoop(const std::string& key, int fadeMs = 150);
    /// Called once per frame by the application.
    void updateLoops();

    void setVolumes(const AudioVolumes& v);
    const AudioVolumes& volumes() const { return volumes_; }

    /// Releases cached sounds that are no longer playing.
    void clearCache();

private:
    struct MusicTrack {
        MIX_Track* track = nullptr;
        std::string path;
        float baseGain = 1.0f;
    };
    MIX_Track* acquireEffectTrack();
    void applyMusicGain();
    float busGain(AudioBus bus) const;

    MIX_Mixer* mixer_ = nullptr;
    std::unordered_map<std::string, MIX_Audio*> cache_;
    std::vector<MIX_Track*> effectTracks_;
    size_t nextEffect_ = 0;
    MusicTrack music_[2];
    int activeMusic_ = 0;
    MIX_Track* ambience_ = nullptr;
    struct Loop {
        MIX_Track* track = nullptr;
        std::string path;
        uint64_t lastRefresh = 0;
        bool playing = false;
    };
    std::unordered_map<std::string, Loop> loops_;
    std::string musicPath_;
    std::string ambiencePath_;
    AudioVolumes volumes_;
    float duck_ = 1.0f;
};

}  // namespace cosmic
