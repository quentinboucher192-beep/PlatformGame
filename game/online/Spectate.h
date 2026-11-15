#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/Replay.h"

namespace cosmic {

class OnlineService;
struct ApiResult;

/// Live spectating between friends (2.35, docs/online/API.md §8.7 bis).
///
/// A player who lets friends watch (« Mes amis peuvent me regarder jouer », on by default,
/// and a visible presence) streams the run they are playing: its header when it starts (the
/// level and its fingerprint, the seed, the keys of the powers, a challenge's rules) and the
/// inputs of the simulation steps about every half second, with the replay format's state
/// digests. A friend's game plays the run again from them exactly like an exact replay (same
/// level version, same seed, same inputs: ReplaySimulator), about a second behind live.
/// Nothing of it changes the simulation: the inputs are those the replay records anyway.
///
/// The player watched is never told anything about the viewers (the server tells nobody),
/// and the viewer never sends anything to the player's game: it only reads the server.

/// The run a player streams: what a friend needs to play it again and the level it is on.
struct SpectateHeader {
    std::string runId;        // UUID v4 of this run (a restart is another run)
    std::string stream;       // from the server: changes at every start it receives
    std::string kind;         // "campaign", "community" or "challenge" (from the server)
    std::string levelSource;  // "campaign" or "community"
    std::string levelId;      // campaign level id, or the community level's online id
    int levelVersion = 0;     // revision (campaign) or published version (community)
    std::string levelHash;    // ranked fingerprint (campaign), gameplay fingerprint (community)
    std::string levelTitle;   // from the server, never from the player's game
    std::string challengeId;  // a challenge run: its id, its kind and its rules (canonical
    std::string challengeKind;  // form, from the server's challenge)
    std::string challengeRules;
    uint64_t seed = 0;
    bool keysPerSlot = true;
    int simVersion = 0;
    std::string gameVersion;
    std::string startedAt;    // server time (ISO 8601)

    /// Body of POST /spectate/broadcast {action: "start"} (the server adds the rest).
    Json startBody() const;
    /// The `run` of GET /spectate/{playerId}; nullopt when something is missing or malformed.
    static std::optional<SpectateHeader> fromJson(const Json& j, std::string* error = nullptr);
};

namespace spectate {
/// Steps of a chunk as sent: runs of identical steps, 18 bytes each like the replay format
/// 3 (u16 repeat, u32 held, pressed, released, i8 moveX, moveY, u8 flags, choice), base64.
std::string encodeInputs(const InputFrame* frames, size_t count);
/// Back: exactly `count` steps appended to `out`, or false (`out` unchanged, `error`).
bool decodeInputs(const std::string& base64, uint32_t count, std::vector<InputFrame>& out, std::string* error = nullptr);
/// Body of POST /spectate/broadcast {action: "chunk"}: steps [from, from + count) and the
/// state digests taken among them.
Json chunkBody(const std::string& runId, int seq, uint32_t from, const InputFrame* frames, size_t count, const std::vector<ReplayCheck>& checks);
std::string base64Encode(const uint8_t* data, size_t n);
std::optional<std::vector<uint8_t>> base64Decode(const std::string& text);
std::string hex64(uint64_t v);
bool parseHex64(const std::string& s, uint64_t& out);
/// Steps of one chunk at most (the server's limit: 20 s), and of a run (30 minutes).
constexpr uint32_t kMaxChunkFrames = 120 * 20;
constexpr uint32_t kMaxRunFrames = 120 * 60 * 30;
}  // namespace spectate

/// The player's side: GameScene gives it the run (begin, every step's input, the digests,
/// end); it sends them in order, one request at a time, through OnlineService's request queue
/// (never blocking the game): the header at once, then a chunk about every half second, a
/// keep-alive while nothing is simulated (pause). Everything of the run stays here until the
/// server took it: a cut network, a lost answer or a room that forgot the run (the server
/// says « resync ») send the run again from its start. Nothing is shown to the player: the
/// only trace of the feature on this side is the setting.
class SpectateBroadcaster {
public:
    explicit SpectateBroadcaster(OnlineService& online) : online_(online) {}
    SpectateBroadcaster(const SpectateBroadcaster&) = delete;
    SpectateBroadcaster& operator=(const SpectateBroadcaster&) = delete;

    /// Whether this game may stream at all (false: automated runs without a server).
    void setAllowed(bool allowed) { allowed_ = allowed; }
    /// Logged in, Cosmic Online usable and the profile letting friends watch (spectate_allowed
    /// and presence_visible); Unknown while the profile is being read.
    enum class Permission { No, Unknown, Yes };
    Permission permission() const;
    bool enabled() const { return permission() == Permission::Yes; }
    /// A run starts (or restarts): the previous one is replaced. Returns its token (0: not
    /// streamed), which the calls below name, so that a scene leaving never touches the run
    /// of the next one.
    unsigned begin(SpectateHeader header);
    /// The input of the step about to be simulated (the one the replay records).
    void input(unsigned token, const InputFrame& f);
    /// A state digest of the run (the replay's, every 0.5 s).
    void check(unsigned token, const ReplayCheck& c);
    /// The run ends: "finished", "failed" or "quit" (what is left is sent first).
    void end(unsigned token, const std::string& reason);
    /// Every frame (main thread).
    void update(float dt);

    bool active() const { return run_ != nullptr && !run_->refused; }
    /// Steps and chunks the server has taken (tests).
    uint32_t framesAcked() const { return run_ ? run_->acked : 0; }
    int seqAcked() const { return run_ ? run_->seq : 0; }
    bool runEnded() const { return run_ && run_->endSent; }
    /// Starts sent for the current run (1, more after a resync) (tests).
    int starts() const { return run_ ? run_->starts : 0; }

    static constexpr float kInterval = 0.5f;   // between two chunks
    static constexpr float kKeepAlive = 5.0f;  // without any step

private:
    struct Run {
        SpectateHeader header;
        std::vector<InputFrame> inputs;
        std::vector<ReplayCheck> checks;
        bool started = false;  // the server took the header
        uint32_t acked = 0;    // steps the server took
        size_t checksAcked = 0;
        int seq = 0;
        int starts = 0;
        std::string endReason;
        bool endSent = false;
        bool refused = false;  // the server refused this run (setting, level, too long)
        float sinceSent = 0.0f;
        /// A chunk sent and not confirmed yet: sent again exactly as it was.
        struct Chunk {
            Json body;
            int seq = 0;
            uint32_t upTo = 0;
            size_t checksUpTo = 0;
        };
        std::optional<Chunk> pending;
    };
    void send(const Json& body, std::function<void(const ApiResult&)> done);
    void sendPending();
    void failed(const ApiResult& r);

    OnlineService& online_;
    bool allowed_ = true;
    std::unique_ptr<Run> run_;
    unsigned generation_ = 0;  // answers of a replaced run are ignored
    bool busy_ = false;
    float wait_ = 0.0f;
    float backoff_ = 1.0f;
};

/// The viewer's side: what has been received of a friend's run, as a growing exact replay
/// (ReplaySimulator plays its steps as they come). Fed with the answers of
/// GET /spectate/{playerId}; checks that every chunk follows the previous one.
class SpectateStream {
public:
    enum class Update {
        None,     // nothing new
        Started,  // another run (or the same one sent again from its start): play it from its start
        Added,    // more steps
        Invalid,  // a chunk that does not follow: read the run again from its start
    };
    Update absorb(const Json& data, std::string* error = nullptr);
    /// A run is kept by the server (ended runs too, for a minute).
    bool live() const { return live_; }
    const std::optional<SpectateHeader>& header() const { return header_; }
    /// Seed, keys of the powers, rules, and every step received with its digests.
    const ReplayData& replay() const { return replay_; }
    uint32_t frames() const { return static_cast<uint32_t>(replay_.inputs.size()); }
    int lastSeq() const { return lastSeq_; }
    /// "" while played; "finished", "failed", "quit" or "too_long".
    const std::string& ended() const { return ended_; }
    /// The player's game has not sent anything for 30 s (connection lost on their side).
    bool stale() const { return stale_; }
    /// More chunks wait on the server (read again at once).
    bool more() const { return more_; }
    /// Query of the next read ("?stream=…&after=…", or "" for everything).
    std::string query() const;
    void reset();

private:
    bool live_ = false;
    std::optional<SpectateHeader> header_;
    ReplayData replay_;
    int lastSeq_ = 0;
    std::string ended_;
    bool stale_ = false;
    bool more_ = false;
};

}  // namespace cosmic
