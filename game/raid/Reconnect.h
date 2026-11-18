#pragma once

// 2.74 (raids, phase 7, docs/RAIDS.md §15): what a raid instance's game does when its link to the
// relay dies (no END from the relay: a network cut, the relay's object restarted). The relay keeps
// the player's seat 60 s, so the game connects again by itself during those 60 s: attempts at
// once, then after 1, 2, 4 and 8 s, then every 8 s. Each attempt is a return like any other (the
// history replayed off the main thread, then REJOIN). After 60 s the seat is free: the game stops
// and says so. Display and network only: nothing of it touches the simulation.

#include <string>

namespace cosmic::raid {

class Reconnect {
public:
    /// The relay frees a seat absent this long.
    static constexpr float kWindow = 60.0f;
    /// The longest wait between two attempts.
    static constexpr float kMaxBackoff = 8.0f;

    /// The link died at `now` (seconds): the window opens (a second loss during it keeps it).
    void lost(float now);
    /// The game is back in the instance (the relay's GO came again): the window closes.
    void back();
    bool active() const { return active_; }
    /// True once when an attempt is due at `now` (the first one at once).
    bool due(float now);
    /// The attempt failed (refused before its GO, or no connection): the next one waits more.
    void failed(float now);
    /// 60 s have passed since the link died: the seat is free, the game stops.
    bool expired(float now) const { return active_ && now - since_ >= kWindow; }
    int attempts() const { return attempts_; }
    /// Seconds before the seat is freed (0 once expired).
    float remaining(float now) const;
    /// What the player reads while it connects again (French).
    std::string message(float now) const;
    /// What the player reads when it gave up (French).
    static std::string gaveUp();

private:
    bool active_ = false;
    float since_ = 0.0f;
    float nextAt_ = 0.0f;
    int attempts_ = 0;
    bool waiting_ = false;  // an attempt is under way (its GO, or its failure, not known yet)
};

}  // namespace cosmic::raid
