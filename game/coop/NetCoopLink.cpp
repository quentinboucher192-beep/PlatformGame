#include "game/coop/NetCoopLink.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace cosmic {

using namespace coopnet;

NetCoopLink::NetCoopLink(std::unique_ptr<CoopTransport> transport, std::string name, uint8_t wantedSeat, std::string account)
    : transport_(std::move(transport)), name_(std::move(name)), wantedSeat_(wantedSeat), account_(std::move(account)) {}

NetCoopLink::~NetCoopLink() { close(); }

void NetCoopLink::start() {
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

void NetCoopLink::close() {
    if (thread_.joinable()) {
        post(leave());
        stop_ = true;
        thread_.join();
    }
}

void NetCoopLink::post(std::vector<uint8_t> msg) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    outbox_.push_back(std::move(msg));
}

void NetCoopLink::run() {
    std::string err;
    if (!transport_->connect(err)) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        threadError_ = err;
        threadDone_ = true;
        return;
    }
    transport_->send(hello(wantedSeat_, name_, account_));
    std::deque<std::vector<uint8_t>> got, out;
    while (!stop_) {
        got.clear();
        const bool open = transport_->receive(got, 4);
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (auto& m : got) inbox_.push_back(std::move(m));
            out.swap(outbox_);
        }
        for (auto& m : out) transport_->send(m);
        out.clear();
        if (!open) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            threadError_ = "La connexion au relais est perdue.";
            break;
        }
    }
    // What waits still goes (LEAVE), then the socket closes.
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        out.swap(outbox_);
    }
    for (auto& m : out) transport_->send(m);
    transport_->close();
    threadDone_ = true;
}

void NetCoopLink::update(float /*dt*/) {
    std::deque<std::vector<uint8_t>> msgs;
    std::string threadError;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        msgs.swap(inbox_);
        threadError = threadError_;
    }
    for (const auto& m : msgs) handle(m);
    // 2.74: after what came (an END that came just before the socket closed is the reason, not
    // the closed link); a link lost while connected is told apart (the raids connect again).
    if (!threadError.empty() && error_.empty()) {
        if (connected_ && endReason_.empty()) {
            endReason_ = threadError;
            lost_ = true;
        } else if (!connected_) {
            error_ = threadError;
        }
    }
    // About twice a second: the link (round trip), and the lobby keeps the seat alive.
    const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
    if (connected_ && now - lastPing_ > 500) {
        lastPing_ = now;
        post(ping(now, rttMs_ < 0 ? 0xFFFF : static_cast<uint16_t>(std::min(rttMs_, 60000))));
    }
    sendInputs();
}

void NetCoopLink::handle(const std::vector<uint8_t>& msg) {
    if (msg.empty()) return;
    Reader r{msg.data() + 1, msg.data() + msg.size()};
    switch (static_cast<Msg>(msg[0])) {
        case Msg::Welcome: {
            const uint8_t seat = r.u8();
            r.u8();
            if (r.ok && seat == kWatcherSeat) {  // 2.75: a spectator's socket (no seat, no input)
                seat_ = -1;
                watcher_ = true;
                connected_ = true;
                break;
            }
            if (!r.ok || seat >= kMaxPlayers) return;
            seat_ = seat;
            connected_ = true;
            break;
        }
        case Msg::Lobby: {
            Json j = Json::parse(std::string(reinterpret_cast<const char*>(msg.data() + 1), msg.size() - 1), nullptr, false);
            if (j.is_object()) lobby_ = std::move(j);  // never trusted: shown as read, bounded by the UI
            sawLobby_ = true;
            break;
        }
        case Msg::Go: {
            GoInfo g;
            g.seed = r.u64();
            g.players = r.u8();
            g.delay = r.u8();
            g.level = r.str(64);
            g.hash = r.str(64);
            g.attempt = r.str(64);
            if (!r.ok || g.players < 2 || g.players > kMaxPlayers || g.delay > 30) return;
            if (g.seed == 0) g.seed = levelSeed(g.level);
            // A game that comes once the game started: it has frames to play again first.
            // (A game in the lobby always had its LOBBY first; one coming back gets GO at once.)
            joinedLate_ = !go_.has_value() && !sawLobby_;
            go_ = g;
            break;
        }
        case Msg::Frames: {
            if (!go_) return;
            const uint32_t first = r.u32();
            const uint16_t n = r.u16();
            if (!r.ok || n == 0 || n > kMaxBatch) return;
            const int players = go_->players;
            InputFrame seat[kMaxBatch];
            std::vector<InputFrame> block(static_cast<size_t>(n) * static_cast<size_t>(players));
            for (int s = 0; s < players; ++s) {
                if (!readFrames(r.p, r.end, seat, n)) {
                    endReason_ = "Le relais a envoyé des données illisibles.";
                    return;
                }
                for (uint16_t i = 0; i < n; ++i) block[static_cast<size_t>(i) * static_cast<size_t>(players) + static_cast<size_t>(s)] = seat[i];
            }
            std::lock_guard<std::mutex> lock(framesMutex_);
            if (first != ticks_) return;  // in order, never twice (a repeated history is skipped)
            frames_.insert(frames_.end(), block.begin(), block.end());
            ticks_ += n;
            break;
        }
        case Msg::RaidState: {
            // 2.65 (raids): the branch's state from a step on (never one already given).
            const uint32_t tick = r.u32();
            const uint32_t mask = r.u32();
            // 2.74 (phase 6): the relay of a heart's instance adds the heart's shared state.
            const uint32_t heart = r.ok && r.p != r.end ? r.u32() : 0u;
            if (!r.done() || !go_) return;
            std::lock_guard<std::mutex> lock(framesMutex_);
            if (tick < ticks_ || (!raidStates_.empty() && tick < raidStates_.back().first)) return;
            raidStates_.emplace_back(tick, mask);
            raidHearts_.push_back(heart);
            break;
        }
        case Msg::Pong: {
            const uint32_t clock = r.u32();
            if (r.ok) rttMs_ = static_cast<int>(static_cast<uint32_t>(SDL_GetTicks()) - clock);
            break;
        }
        case Msg::Rejoin: {
            const uint8_t seat = r.u8();
            const uint32_t at = r.u32();
            if (r.ok && seat == seat_) rejoinAt_ = at;
            break;
        }
        case Msg::PackagePart: {
            if (!askingPackage_) return;
            if (!package_.accept(r.p, static_cast<size_t>(r.end - r.p)) || package_.complete()) {
                askingPackage_ = false;  // refused, none on the host's PC, or every byte is here
                return;
            }
            post(packageAsk(package_.nextOffset()));
            break;
        }
        case Msg::End: {
            const auto reason = static_cast<EndReason>(r.u8());
            endReason_ = reason == EndReason::HostLeft  ? "L'hôte a quitté la partie."
                         : reason == EndReason::TooLong ? "La partie a atteint sa durée maximale (2 h)."
                         : reason == EndReason::Refused ? "Le relais refuse cette connexion (partie complète, ou plus de 60 s d'absence)."
                                                        : "La partie est terminée.";
            break;
        }
        default:
            break;
    }
}

void NetCoopLink::setReady(bool on) { post(coopnet::ready(on)); }
void NetCoopLink::startGame(int players) { post(coopnet::start(static_cast<uint8_t>(players))); }

void NetCoopLink::requestPackage() {
    if (askingPackage_ || go_) return;
    package_.reset();
    askingPackage_ = true;
    post(packageAsk(0));
}
void NetCoopLink::catchupDone(uint32_t tick) {
    if (!watcher_) post(coopnet::catchupDone(tick));  // 2.75: a spectator never comes back into play
}

void NetCoopLink::result(uint32_t ticks) {
    std::vector<uint8_t> m{8};  // RESULT: u32 steps of the attempt
    Writer{m}.u32(ticks);
    post(std::move(m));
}

void NetCoopLink::pushLocal(uint32_t tick, const std::vector<std::pair<int, InputFrame>>& frames) {
    if (seat_ < 0 || !go_) return;
    for (const auto& [s, f] : frames) {
        if (s != seat_) continue;
        if (rejoinAt_ != 0 && tick < rejoinAt_) return;  // before its return: nothing counts
        InputFrame clean = f;
        clean.flags &= InputFrame::SkipCinematic;  // never Standby: the relay alone decides it
        if (pending_.empty()) pendingFrom_ = tick;
        else if (tick != pendingFrom_ + pending_.size()) {
            sendInputs();  // not consecutive: what waits goes first
            pendingFrom_ = tick;
        }
        pending_.push_back(clean);
        if (pending_.size() >= 4) sendInputs();
    }
}

void NetCoopLink::sendInputs() {
    if (pending_.empty()) return;
    post(input(pendingFrom_, pending_.data(), pending_.size()));
    pendingFrom_ += static_cast<uint32_t>(pending_.size());
    pending_.clear();
}

bool NetCoopLink::frameFor(uint32_t tick, std::vector<InputFrame>& out) {
    std::lock_guard<std::mutex> lock(framesMutex_);
    if (!go_ || tick >= ticks_) return false;
    const size_t n = go_->players;
    out.assign(frames_.begin() + static_cast<std::ptrdiff_t>(tick * n), frames_.begin() + static_cast<std::ptrdiff_t>((tick + 1) * n));
    return true;
}

uint32_t NetCoopLink::available() const {
    std::lock_guard<std::mutex> lock(framesMutex_);
    return ticks_;
}

std::string NetCoopLink::waitingFor() const {
    // The relay knows who is late; the game only knows it waits.
    return {};
}

int NetCoopLink::pingMs(int seat) const {
    if (seat == seat_) return rttMs_;
    if (lobby_.is_object())
        for (const auto& s : lobby_.value("seats", Json::array()))
            if (jget<int>(s, "seat", -1) == seat) return jget<int>(s, "ping", -1);
    return -1;
}

uint32_t NetCoopLink::raidStateAt(uint32_t tick) const {
    std::lock_guard<std::mutex> lock(framesMutex_);
    uint32_t mask = 0;
    for (const auto& [t, m] : raidStates_) {
        if (t > tick) break;
        mask = m;
    }
    return mask;
}

uint32_t NetCoopLink::raidHeartAt(uint32_t tick) const {
    std::lock_guard<std::mutex> lock(framesMutex_);
    uint32_t heart = 0;
    for (size_t k = 0; k < raidStates_.size() && k < raidHearts_.size(); ++k) {
        if (raidStates_[k].first > tick) break;
        heart = raidHearts_[k];
    }
    return heart;
}

}  // namespace cosmic
