#pragma once

// 2.62: cooperation (docs/COOP.md). Where the inputs of a team's steps come from: this
// computer only (2 or 3 players on one PC), or a relay that orders everyone's inputs (online:
// the CoopRoom Durable Object; local network: one PC's CoopRelay). The game simulates a step
// only with its team frame (one InputFrame per player), the same on every computer.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/InputFrame.h"

namespace cosmic {

/// A player of a team, as the lobby set it up.
struct CoopSeat {
    std::string name;             // shown in the game (an account's name, else « Joueur N »)
    enum class Device { Keyboard, Pad, Remote } device = Device::Keyboard;
    uint32_t pad = 0;             // SDL_JoystickID of its gamepad (Device::Pad)
    int padNumber = 0;            // 2.65: its gamepad's stable number (DeviceRegistry: « n° 1 »)
    std::string accountId;        // its Cosmic Online account ("" : no account, never ranked)
    std::string accountToken;     // same PC: the access token of a player who signed in (memory only)
    Color color{62, 230, 214, 255};
};

/// What a team plays: the level, the players (index = player number - 1), the mode.
struct CoopSetup {
    enum class Mode { SamePc, Lan, Online };
    Mode mode = Mode::SamePc;
    std::string levelId;
    /// 2.63.5: a community level (EXTRÊME): its Cosmic Online id and published version (its
    /// Duo / Trio boards); "" for a level of the campaign.
    std::string communityId;
    int communityVersion = 0;
    std::vector<CoopSeat> seats;
    /// This computer's player online or on the local network (one per computer); -1 on one PC.
    int localSeat = -1;
    uint64_t seed = 0;
    std::string attemptId;
    std::string roomId;           // online: the room of the relay
    std::string address;          // local network: the PC that hosts (its relay)
    int inputDelay = 0;           // steps between an input and its step (network)
    bool local(int seat) const { return mode == Mode::SamePc || seat == localSeat; }
};

/// The colours of the players (1 cyan, 2 pink, 3 gold), as the lobby and the HUD show them.
Color coopColor(int seat);

class CoopLink {
public:
    virtual ~CoopLink() = default;
    /// The inputs this computer's players chose now, each for its seat, for step `tick`
    /// (the current step + the input delay).
    virtual void pushLocal(uint32_t tick, const std::vector<std::pair<int, InputFrame>>& frames) = 0;
    /// The team frame of step `tick` (one input per player) once the relay gave it.
    virtual bool frameFor(uint32_t tick, std::vector<InputFrame>& out) = 0;
    virtual int inputDelay() const { return 0; }
    /// Who the game waits for (« Lyra »), "" when it does not wait.
    virtual std::string waitingFor() const { return {}; }
    /// A player's link in ms (-1: unknown or on this computer).
    virtual int pingMs(int /*seat*/) const { return -1; }
    /// Network: receives what came, sends what waits (each frame).
    virtual void update(float /*dt*/) {}
    /// The session ended (the host left, the relay closed): a message, else "".
    virtual std::string endReason() const { return {}; }
    /// Online and local network: how many steps the relay has given so far.
    virtual uint32_t available() const { return 0; }
    /// This game came back to a game already started: it plays the steps given so far again
    /// (off the main thread), then catchupDone(); its inputs count again from rejoinAt().
    virtual bool joinedLate() const { return false; }
    virtual void catchupDone(uint32_t /*tick*/) {}
    virtual uint32_t rejoinAt() const { return 0; }
    /// The attempt ended after `ticks` steps (the online relay notes the inputs played).
    virtual void result(uint32_t /*ticks*/) {}
    /// 2.65 (raids): the branch's state the relay gave for step `tick` (objectives done).
    virtual uint32_t raidStateAt(uint32_t /*tick*/) const { return 0; }
};

/// Every player on this computer: a step's frame is the inputs just given.
class LocalCoopLink : public CoopLink {
public:
    explicit LocalCoopLink(int players) : players_(players) {}
    void pushLocal(uint32_t tick, const std::vector<std::pair<int, InputFrame>>& frames) override {
        tick_ = tick;
        frame_.assign(static_cast<size_t>(players_), InputFrame{});
        for (const auto& [seat, f] : frames)
            if (seat >= 0 && seat < players_) frame_[static_cast<size_t>(seat)] = f;
        ready_ = true;
    }
    bool frameFor(uint32_t tick, std::vector<InputFrame>& out) override {
        if (!ready_ || tick != tick_) return false;
        out = frame_;
        ready_ = false;
        return true;
    }

private:
    int players_;
    uint32_t tick_ = 0;
    bool ready_ = false;
    std::vector<InputFrame> frame_;
};

}  // namespace cosmic
