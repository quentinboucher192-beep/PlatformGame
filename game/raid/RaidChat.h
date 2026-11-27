#pragma once

// 2.95 (docs/RAIDS.md §39): the chat of a raid, as in Minecraft — the lines at the bottom left over the game,
// which fade away after a few seconds; opened (Action::ChatOpen: T), the history and a field. Two channels:
// « Groupe » (the players of this instance: the instance's relay carries it) and « Raid » (every player of the
// event, every group: the event's room on the server carries it). The server decides who wrote a line (its
// session), cleans it and limits the pace; this computer only shows what came back.
//
// This file is the model, without any drawing: the lines kept, the channels, what the field says (« /g »,
// « /r »), the pace a player may write at (the server's own rule, so that nothing is sent that it would refuse),
// who may write where (a player down or eliminated still talks), and, « pour le moment », the thoughts of a
// test's simulated players turned into lines of the « Groupe » channel (only when they change, at a pace that
// stays readable). Display and this computer's choices only: nothing here takes part in the simulation (the
// frames of a player typing are neutral, as under the raid's menu: raidmenu::Hold::chat).

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cosmic::raidchat {

enum class Channel : uint8_t { Group = 0, Raid = 1 };
constexpr int kChannels = 2;
/// « Groupe », « Raid » (French: translated when drawn).
const char* channelLabel(Channel c);
inline Channel otherChannel(Channel c) { return c == Channel::Group ? Channel::Raid : Channel::Group; }

/// The limits shared with the server (server/src/raidChat.ts): a message of kMaxChars characters at most
/// (kMaxBytes on the wire), kBurst at once then one every kRefillSeconds, kHistory lines per channel given again
/// to a game that (re)connects.
constexpr size_t kMaxChars = 200;
constexpr size_t kMaxBytes = 1024;
constexpr int kBurst = 3;
constexpr float kRefillSeconds = 1.0f;
constexpr size_t kHistory = 50;
/// The lines this screen keeps (both channels together).
constexpr size_t kLogMax = 200;

/// Who a line is from: a player's words (shown as written, never translated), a simulated player's thought
/// (« IA »), a notice of the server (its French text, translated), or what this screen says itself.
enum class Kind : uint8_t { Player, Ai, Server, Local };

struct Line {
    Channel channel = Channel::Group;
    Kind kind = Kind::Player;
    std::string playerId;  // the author's account (a player's line), "" otherwise
    std::string name;      // « Nova », « Joueur simulé 3 » ("" for the notices)
    std::string label;     // « [AUBE]·2 » on the « Raid » channel (the author's clan and group), "" none
    std::string role;      // a simulated player's role (« Réanimateur »), its thought in `text`
    std::string text;
    int seat = -1;         // the author's seat in this group (its colour), -1 unknown
    bool mine = false;     // written on this computer
    uint32_t at = 0;       // the server's time (Unix seconds), 0 for a line made here
    float clock = 0.0f;    // this computer's clock when it came (its fading)
};

/// A message as it may be sent: one line (line breaks and tabs become spaces), no control or direction
/// character, spaces gathered, trimmed, kMaxChars characters at most (valid UTF-8, whole characters).
std::string clean(const std::string& raw);
/// Characters (code points) of valid UTF-8.
size_t codePoints(const std::string& s);

/// What the field says. « /g texte » or « /groupe texte » send `texte` on « Groupe », « /r » or « /raid » on
/// « Raid »; the prefix alone changes the channel (nothing sent). Another « /… » is never sent.
struct Draft {
    Channel channel = Channel::Group;
    std::string text;            // cleaned; "" when there is nothing to send
    bool switchOnly = false;     // « /g » or « /r » alone: the channel changes
    bool unknownCommand = false; // « /x … »: refused (the screen says which commands exist)
};
Draft parseDraft(const std::string& field, Channel current);

/// The server's pace (kBurst at once, then one every kRefillSeconds), mirrored here: a message the server
/// would refuse is not sent (the screen says to wait).
class RateLimit {
public:
    explicit RateLimit(int burst = kBurst, float refillSeconds = kRefillSeconds) : burst_(burst), refill_(refillSeconds) {}
    /// One message at `now` (seconds): false when it must wait.
    bool take(float now);
    /// Seconds before the next one is allowed (0: now).
    float waitFor(float now) const;

private:
    float tokensAt(float now) const;
    int burst_;
    float refill_;
    float tokens_ = 0.0f;
    float last_ = 0.0f;
    bool started_ = false;
};

/// The lines of the screen, both channels in their order of arrival (the newest kLogMax). The lines of the players
/// this one blocked (or who blocked them) never come: the server leaves them out (as in the discussions).
class Log {
public:
    void push(Line l);
    /// A line the server gives again (its history, on a (re)connection): kept once. Never on screen as new: it
    /// is only in the history (its clock far in the past).
    bool pushHistory(Line l);
    const std::deque<Line>& lines() const { return lines_; }
    /// Grows with each line kept (the open history follows the newest).
    uint64_t serial() const { return serial_; }
    void clear() {
        lines_.clear();
        ++serial_;
    }

private:
    std::deque<Line> lines_;
    uint64_t serial_ = 0;
};

/// The opacity of a line of the closed chat at `now`: 1 while new, fading over its last second, 0 after
/// `fadeSeconds` (the HUD editor's « Disparition »).
float lineAlpha(const Line& l, float now, float fadeSeconds);

/// Where this screen is, for who may write where.
struct Where {
    bool local = false;          // a test or a simulation on this computer only (« TEST LOCAL — rien n'est envoyé »)
    bool relayed = false;        // an instance of a raid, or a test with invited players: a relay carries the chat
    bool connected = false;      // its link is up (not reconnecting)
    bool serverChat = false;     // the relay said it carries the chat (a server before 2.95: no)
    bool watchingOther = false;  // a spectator watching another group than their own: its « Groupe » is not theirs
    bool down = false;           // this player down, waiting for a revival
    bool eliminated = false;     // their group wiped (a spectator)
};
struct Access {
    bool can = false;
    bool sends = false;  // the message leaves this computer (false: a local test, shown here only)
    std::string why;     // when it cannot (French, translated when drawn)
};
/// Whether this player may write on `c` now. Being down or eliminated is never a reason: they still talk.
Access access(const Where& w, Channel c);

/// « pour le moment »: a simulated player's thought as the chat shows it (its seat, name, role and intention).
struct Thought {
    int seat = 0;
    std::string name, role, intent;
};

/// The thoughts of a test's simulated players as lines of the « Groupe » channel: one only when it CHANGED, and
/// once it held for kStable seconds (no flicker); each simulated player at most once every kSeatGap seconds, all of
/// them at most once every kGlobalGap seconds (the chat stays readable: the others wait their turn, the
/// latest thought of each one is what is written).
class AiFeed {
public:
    static constexpr float kStable = 1.5f;
    static constexpr float kSeatGap = 10.0f;
    static constexpr float kGlobalGap = 2.5f;
    /// This frame's thoughts of the simulated players in view of the chat (every one, each frame); the lines to
    /// post now (at most one).
    std::vector<Thought> update(float now, const std::vector<Thought>& current);
    /// Another group shown (the full raid's simulation): its simulated players start afresh.
    void reset() {
        seats_.clear();
        lastAny_ = -1e9f;
    }

private:
    struct Seat {
        std::string posted;   // the last thought written
        Thought pending;      // the thought now
        std::string key;      // its role and intention (what counts as a change)
        float since = 0.0f;   // since when it holds
        float lastPost = -1e9f;
    };
    std::map<int, Seat> seats_;
    float lastAny_ = -1e9f;
};

/// The quick messages (a gamepad without a keyboard): French, sent in the language shown.
const std::vector<const char*>& quickMessages();

/// A raid's chat on a screen: its lines, the channel chosen, the field, the pace, the simulated players' lines.
struct Chat {
    Log log;
    Channel channel = Channel::Group;
    RateLimit rate;
    AiFeed ai;
    std::string draft;
    bool open = false;

    enum class Result {
        Sent,      // given to the relay (it comes back to everyone concerned, this computer included)
        Shown,     // a local test: shown here only, nothing sent
        Switched,  // « /g » or « /r » alone: the channel changed
        Empty,     // nothing to send
        Refused,   // this player may not write there now (said in the chat)
        Wait,      // too fast (the server's pace): said in the chat
        Unknown,   // « /x »: the commands are said in the chat
    };
    /// The field (or a quick message) at `now`: « /g » and « /r » choose the channel, who may write where
    /// (access()), the pace (RateLimit). `send` carries a message to the relay; a local test shows it here as
    /// this player's (`myName`, `myId`). Nothing is ever shown as sent before the relay gives it back.
    Result submit(const std::string& field, const Where& w, float now, const std::function<void(Channel, const std::string&)>& send,
                  const std::string& myName, const std::string& myId = {});
    /// A line of this screen itself on `c` (a refusal, the local test's word): French, translated when drawn.
    void notice(Channel c, const std::string& text, float now);
    /// A simulated player's thought as a line of « Groupe » (never sent: every computer of a test computes it).
    void aiLine(const Thought& t, float now);
};

}  // namespace cosmic::raidchat
