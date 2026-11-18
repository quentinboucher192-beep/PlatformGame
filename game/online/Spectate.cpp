#include "game/online/Spectate.h"

#include <algorithm>
#include <cstring>

#include "engine/core/Log.h"
#include "game/gameplay/ChallengeRules.h"
#include "game/online/OnlineService.h"

namespace cosmic {

namespace {

constexpr size_t kRunBytes = 18;
constexpr uint32_t kActionMask = InputFrame::kActions >= 32 ? 0xFFFFFFFFu : (1u << InputFrame::kActions) - 1u;

void putU16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(static_cast<uint8_t>(v & 0xFF));
    o.push_back(static_cast<uint8_t>(v >> 8));
}
void putU32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool isHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

}  // namespace

// ------------------------------------------------------------------- codec

namespace spectate {

std::string base64Encode(const uint8_t* data, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < n) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < n) v |= data[i + 2];
        out.push_back(t[(v >> 18) & 63]);
        out.push_back(t[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? t[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < n ? t[v & 63] : '=');
    }
    return out;
}

std::optional<std::vector<uint8_t>> base64Decode(const std::string& text) {
    if (text.size() % 4 != 0) return std::nullopt;
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int pad = 0;
        uint32_t v = 0;
        for (size_t k = 0; k < 4; ++k) {
            char c = text[i + k];
            if (c == '=') {
                // Only at the end, as the last one or two characters.
                if (!last || k < 2 || (k == 2 && text[i + 3] != '=')) return std::nullopt;
                ++pad;
                v <<= 6;
                continue;
            }
            int d = val(c);
            if (d < 0 || pad > 0) return std::nullopt;
            v = (v << 6) | static_cast<uint32_t>(d);
        }
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        if (pad < 2) out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        if (pad < 1) out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    return out;
}

std::string hex64(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[static_cast<size_t>(i)] = d[v & 15];
    return s;
}

bool parseHex64(const std::string& s, uint64_t& out) {
    if (!isHex(s, 16)) return false;
    uint64_t v = 0;
    for (char c : s) v = (v << 4) | static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    out = v;
    return true;
}

std::string encodeInputs(const InputFrame* frames, size_t count) {
    std::vector<uint8_t> o;
    for (size_t i = 0; i < count;) {
        size_t j = i + 1;
        while (j < count && j - i < 0xFFFF && frames[j] == frames[i]) ++j;
        const InputFrame& f = frames[i];
        putU16(o, static_cast<uint16_t>(j - i));
        putU32(o, f.held);
        putU32(o, f.pressed);
        putU32(o, f.released);
        o.push_back(static_cast<uint8_t>(f.moveX));
        o.push_back(static_cast<uint8_t>(f.moveY));
        o.push_back(f.flags);
        o.push_back(f.choice);
        i = j;
    }
    return base64Encode(o.data(), o.size());
}

bool decodeInputs(const std::string& text, uint32_t count, std::vector<InputFrame>& out, std::string* error) {
    if (count == 0) return text.empty() ? true : fail(error, "aucune entrée attendue");
    auto bytes = base64Decode(text);
    if (!bytes || bytes->empty() || bytes->size() % kRunBytes != 0) return fail(error, "entrées illisibles");
    std::vector<InputFrame> got;
    got.reserve(count);
    for (size_t at = 0; at < bytes->size(); at += kRunBytes) {
        const uint8_t* p = bytes->data() + at;
        uint16_t repeat = getU16(p);
        InputFrame f;
        f.held = getU32(p + 2);
        f.pressed = getU32(p + 6);
        f.released = getU32(p + 10);
        f.moveX = static_cast<int8_t>(p[14]);
        f.moveY = static_cast<int8_t>(p[15]);
        f.flags = p[16];
        f.choice = p[17];
        if (repeat == 0 || ((f.held | f.pressed | f.released) & ~kActionMask) != 0 || f.moveX == -128 || f.moveY == -128 ||
            (f.flags & ~InputFrame::SkipCinematic) != 0 || (f.choice > 8 && f.choice != InputFrame::kChoiceLeave))
            return fail(error, "entrée invalide");
        if (got.size() + repeat > count) return fail(error, "plus d'entrées qu'annoncé");
        got.insert(got.end(), repeat, f);
    }
    if (got.size() != count) return fail(error, "moins d'entrées qu'annoncé");
    out.insert(out.end(), got.begin(), got.end());
    return true;
}

Json chunkBody(const std::string& runId, int seq, uint32_t from, const InputFrame* frames, size_t count, const std::vector<ReplayCheck>& checks) {
    Json c = Json::array();
    for (const auto& k : checks) c.push_back(Json::array({k.tick, hex64(k.hash)}));
    Json body = {{"action", "chunk"}, {"run_id", runId}, {"seq", seq}, {"from_frame", from}, {"count", count}, {"runs", encodeInputs(frames, count)}};
    if (!c.empty()) body["checks"] = c;
    return body;
}

}  // namespace spectate

// ------------------------------------------------------------------ header

Json SpectateHeader::startBody() const {
    Json b = {{"action", "start"},
              {"run_id", runId},
              {"level", {{"source", levelSource}, {"id", levelId}, {"version", levelVersion}, {"hash", levelHash}}},
              {"seed", spectate::hex64(seed)},
              {"keys_per_slot", keysPerSlot},
              {"sim_version", simVersion},
              {"game_version", gameVersion}};
    if (!challengeId.empty()) b["challenge_id"] = challengeId;
    return b;
}

std::optional<SpectateHeader> SpectateHeader::fromJson(const Json& j, std::string* error) {
    auto refuse = [&](const std::string& why) -> std::optional<SpectateHeader> {
        fail(error, why);
        return std::nullopt;
    };
    SpectateHeader h;
    if (!j.is_object()) return refuse("en-tête absent");
    h.runId = jstr(j, "run_id");
    h.stream = jstr(j, "stream");
    h.kind = jstr(j, "kind");
    const Json& lv = j.value("level", Json::object());
    h.levelSource = jstr(lv, "source");
    h.levelId = jstr(lv, "id");
    h.levelVersion = jget<int>(lv, "version", 0);
    h.levelHash = jstr(lv, "hash");
    h.levelTitle = jstr(lv, "title");
    if (j.contains("challenge") && j["challenge"].is_object()) {
        h.challengeId = jstr(j["challenge"], "id");
        h.challengeKind = jstr(j["challenge"], "kind");
        h.challengeRules = jstr(j["challenge"], "rules");
    }
    h.keysPerSlot = jget<bool>(j, "keys_per_slot", true);
    h.simVersion = jget<int>(j, "sim_version", 0);
    h.gameVersion = jstr(j, "game_version");
    h.startedAt = jstr(j, "started_at");
    if (h.runId.size() != 36 || h.stream.empty() || h.levelId.empty() || h.levelVersion <= 0 || !isHex(h.levelHash, 64) ||
        (h.levelSource != "campaign" && h.levelSource != "community") || !spectate::parseHex64(jstr(j, "seed"), h.seed))
        return refuse("en-tête incomplet");
    if (!h.challengeId.empty()) {
        std::string why;
        if (!ChallengeRules::fromCanonical(h.challengeRules, &why)) return refuse("règles du défi : " + why);
    }
    return h;
}

// ------------------------------------------------------------- broadcaster

SpectateBroadcaster::Permission SpectateBroadcaster::permission() const {
    if (!allowed_ || !online_.usable() || !online_.loggedIn()) return Permission::No;
    const Json& p = online_.profile();
    if (!p.is_object() || jstr(p, "id").empty()) return Permission::Unknown;  // the profile is being read
    return jget<bool>(p, "spectate_allowed", true) && jget<bool>(p, "presence_visible", true) ? Permission::Yes : Permission::No;
}

unsigned SpectateBroadcaster::begin(SpectateHeader header) {
    ++generation_;  // answers about the previous run are ignored
    busy_ = false;
    run_.reset();
    // Kept from its start while the profile is being read (a level started right after the
    // game): sent once the setting is known to allow it.
    if (permission() == Permission::No) return 0;
    run_ = std::make_unique<Run>();
    run_->header = std::move(header);
    wait_ = 0.0f;
    backoff_ = 1.0f;
    return generation_;
}

void SpectateBroadcaster::input(unsigned token, const InputFrame& f) {
    if (!run_ || token != generation_ || run_->refused || !run_->endReason.empty()) return;
    // Past the server's limit (30 minutes), one chunk more is kept: the server refuses it and
    // tells the friends watching that the run is too long; nothing more is recorded here.
    if (run_->inputs.size() >= spectate::kMaxRunFrames + spectate::kMaxChunkFrames) return;
    run_->inputs.push_back(f);
}

void SpectateBroadcaster::check(unsigned token, const ReplayCheck& c) {
    if (!run_ || token != generation_ || run_->refused || !run_->endReason.empty()) return;
    if (c.tick == 0 || c.tick > run_->inputs.size() || (!run_->checks.empty() && c.tick <= run_->checks.back().tick)) return;
    run_->checks.push_back(c);
}

void SpectateBroadcaster::end(unsigned token, const std::string& reason) {
    if (!run_ || token != generation_ || !run_->endReason.empty()) return;
    run_->endReason = reason;
    wait_ = std::min(wait_, 0.1f);  // sent soon (after what is left)
}

void SpectateBroadcaster::send(const Json& body, std::function<void(const ApiResult&)> done) {
    busy_ = true;
    run_->sinceSent = 0.0f;
    const unsigned gen = generation_;
    online_.send("POST", "/api/v1/spectate/broadcast", body, [this, gen, done = std::move(done)](const ApiResult& r) {
        if (gen != generation_ || !run_) return;  // another run since
        busy_ = false;
        if (!r.ok) return failed(r);
        backoff_ = 1.0f;
        done(r);
    });
}

void SpectateBroadcaster::failed(const ApiResult& r) {
    if (r.status == 409 && r.code == "SPECTATE_RESYNC") {
        // The room does not have this run (any more), or not where this game stands: the
        // whole run again from its start.
        run_->started = false;
        run_->pending.reset();
        wait_ = 0.2f;
        return;
    }
    if (r.status == 429) {
        int retry = jget<int>(r.details, "retry_after_seconds", 1);
        wait_ = static_cast<float>(std::clamp(retry, 1, 30));
        return;
    }
    if (r.networkFailure() || r.status >= 500 || r.status == 401) {
        // Offline, the server busy, the session being renewed: later, the run kept here.
        wait_ = backoff_;
        backoff_ = std::min(backoff_ * 2.0f, 15.0f);
        return;
    }
    // Refused for good (the setting off, a level the server does not know, too long a run):
    // nothing more of this run.
    run_->refused = true;
    Log::info("Direct : partie non diffusée ({} {})", r.status, r.code);
}

void SpectateBroadcaster::update(float dt) {
    if (!run_) return;
    run_->sinceSent += dt;
    wait_ -= dt;
    if (busy_ || run_->refused || wait_ > 0.0f) return;
    switch (permission()) {
        case Permission::No:
            // Logged out, the setting turned off: the run is dropped (the server forgets it too).
            ++generation_;
            run_.reset();
            return;
        case Permission::Unknown:
            wait_ = 0.25f;  // the profile is on its way
            return;
        case Permission::Yes:
            break;
    }
    Run& r = *run_;
    if (r.endSent) return;
    if (!r.started) {
        // A run ended before its header could be sent has nothing to show.
        if (!r.endReason.empty() && r.inputs.empty()) {
            run_.reset();
            return;
        }
        ++r.starts;
        send(r.header.startBody(), [this](const ApiResult&) {
            run_->started = true;
            run_->acked = 0;
            run_->checksAcked = 0;
            run_->seq = 0;
            run_->pending.reset();
            wait_ = 0.0f;
        });
        return;
    }
    // A chunk whose answer did not come: exactly the same again (the room takes it once).
    if (r.pending) return sendPending();
    const uint32_t total = static_cast<uint32_t>(r.inputs.size());
    if (r.acked < total) {
        const uint32_t from = r.acked;
        const uint32_t count = std::min(total - from, spectate::kMaxChunkFrames);
        std::vector<ReplayCheck> checks;
        size_t upTo = r.checksAcked;
        while (upTo < r.checks.size() && r.checks[upTo].tick <= from + count) checks.push_back(r.checks[upTo++]);
        const int seq = r.seq + 1;
        r.pending = Run::Chunk{spectate::chunkBody(r.header.runId, seq, from, r.inputs.data() + from, count, checks), seq, from + count, upTo};
        return sendPending();
    }
    if (!r.endReason.empty()) {
        send({{"action", "end"}, {"run_id", r.header.runId}, {"reason", r.endReason}}, [this](const ApiResult&) {
            run_->endSent = true;
            run_->inputs = {};  // nothing more to send: the run's inputs are released
            run_->checks = {};
        });
        return;
    }
    if (r.sinceSent >= kKeepAlive) {
        // Nothing simulated (pause, a power choice): the friends still see the run live.
        const int seq = r.seq + 1;
        r.pending = Run::Chunk{spectate::chunkBody(r.header.runId, seq, r.acked, nullptr, 0, {}), seq, r.acked, r.checksAcked};
        return sendPending();
    }
    wait_ = 0.05f;
}

void SpectateBroadcaster::sendPending() {
    send(run_->pending->body, [this](const ApiResult&) {
        Run& r = *run_;
        r.seq = r.pending->seq;
        r.acked = r.pending->upTo;
        r.checksAcked = r.pending->checksUpTo;
        r.pending.reset();
        // More waiting (a catch-up after a cut): at once; else in half a second.
        wait_ = r.inputs.size() - r.acked >= spectate::kMaxChunkFrames ? 0.0f : kInterval;
    });
}

// ------------------------------------------------------------------ stream

void SpectateStream::reset() {
    live_ = false;
    header_.reset();
    replay_ = ReplayData{};
    lastSeq_ = 0;
    ended_.clear();
    stale_ = false;
    more_ = false;
}

std::string SpectateStream::query() const {
    if (!header_ || header_->stream.empty()) return {};
    return "?stream=" + header_->stream + "&after=" + std::to_string(lastSeq_);
}

SpectateStream::Update SpectateStream::absorb(const Json& data, std::string* error) {
    if (!data.is_object() || !jget<bool>(data, "live", false)) {
        live_ = false;
        more_ = false;
        return Update::None;
    }
    std::string why;
    auto h = SpectateHeader::fromJson(data.value("run", Json::object()), &why);
    if (!h) {
        if (error) *error = why;
        reset();
        return Update::Invalid;
    }
    Update result = Update::None;
    if (!header_ || header_->stream != h->stream) {
        // Another run, or the same one sent again from its start: everything from the start.
        replay_ = ReplayData{};
        replay_.levelId = h->levelId;
        replay_.levelHash = h->levelHash;
        replay_.attemptId = h->runId;
        replay_.seed = h->seed;
        replay_.keysPerSlot = h->keysPerSlot;
        replay_.simVersion = static_cast<uint16_t>(std::clamp(h->simVersion, 0, 0xFFFF));  // 2.70: its rules
        replay_.challengeRules = h->challengeRules;
        lastSeq_ = 0;
        result = Update::Started;
    }
    header_ = std::move(*h);
    live_ = true;
    for (const auto& c : data.value("chunks", Json::array())) {
        const int seq = jget<int>(c, "seq", 0);
        if (seq <= lastSeq_) continue;  // already here
        const auto from = jget<int64_t>(c, "from_frame", -1);
        const auto count = jget<int64_t>(c, "count", -1);
        std::string err;
        if (seq != lastSeq_ + 1 || from != static_cast<int64_t>(replay_.inputs.size()) || count < 0 || count > spectate::kMaxChunkFrames ||
            !spectate::decodeInputs(jstr(c, "runs"), static_cast<uint32_t>(count), replay_.inputs, &err)) {
            if (error) *error = err.empty() ? "morceau hors de son ordre" : err;
            reset();
            return Update::Invalid;
        }
        for (const auto& k : c.value("checks", Json::array())) {
            uint64_t hash = 0;
            if (!k.is_array() || k.size() != 2 || !k[0].is_number_unsigned() || !k[1].is_string() || !spectate::parseHex64(k[1].get<std::string>(), hash))
                continue;
            const auto tick = k[0].get<uint32_t>();
            if (tick == 0 || tick > replay_.inputs.size() || (!replay_.checks.empty() && tick <= replay_.checks.back().tick)) continue;
            replay_.checks.push_back({tick, hash});
        }
        lastSeq_ = seq;
        if (result == Update::None) result = Update::Added;
    }
    ended_ = data.contains("ended") && data["ended"].is_string() ? data["ended"].get<std::string>() : std::string();
    stale_ = jget<bool>(data, "stale", false);
    more_ = jget<bool>(data, "more", false);
    return result;
}

}  // namespace cosmic
