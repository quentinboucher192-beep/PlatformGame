#include "game/online/Replay.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <random>

#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {

constexpr std::array<const char*, 10> kPowerIds = {"dash", "wave", "shield", "impulse", "teleport",
                                                  "glide", "ground_pound", "charge_shot", "grapple", "triple_jump"};

void putU16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(static_cast<uint8_t>(v & 0xFF));
    o.push_back(static_cast<uint8_t>(v >> 8));
}
void putU32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
void putF32(std::vector<uint8_t>& o, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    putU32(o, v);
}
uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
float getF32(const uint8_t* p) {
    uint32_t v = getU32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

bool hexToBytes(const std::string& hex, uint8_t* out, size_t n) {
    if (hex.size() != n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = hex[i * 2 + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        out[i] = static_cast<uint8_t>(v);
    }
    return true;
}

std::string bytesToHex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s.push_back(d[p[i] >> 4]);
        s.push_back(d[p[i] & 15]);
    }
    return s;
}

bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
}

}  // namespace

uint8_t replayPowerIndex(const std::string& id) {
    for (size_t i = 0; i < kPowerIds.size(); ++i)
        if (id == kPowerIds[i]) return static_cast<uint8_t>(i);
    return 255;
}

const char* replayPowerId(uint8_t index) { return index < kPowerIds.size() ? kPowerIds[index] : ""; }

std::string makeUuidV4() {
    std::random_device rd;
    std::array<uint8_t, 16> b{};
    for (size_t i = 0; i < b.size(); i += 4) {
        uint32_t v = rd();
        std::memcpy(&b[i], &v, 4);
    }
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);
    std::string h = bytesToHex(b.data(), b.size());
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20);
}

// ------------------------------------------------------------------ format

std::vector<uint8_t> ReplayData::serialize() const {
    std::vector<uint8_t> o;
    o.reserve(80 + frames.size() * 12 + events.size() * 8);
    o.insert(o.end(), {'C', 'P', 'R', 'P'});
    putU16(o, kFormatVersion);
    putU16(o, sampleHz);
    putU32(o, static_cast<uint32_t>(frames.size()));
    putU32(o, durationMs);
    putU32(o, static_cast<uint32_t>(events.size()));
    std::array<uint8_t, 32> hash{};
    hexToBytes(levelHash, hash.data(), hash.size());
    o.insert(o.end(), hash.begin(), hash.end());
    std::array<uint8_t, 16> uuid{};
    std::string compact;
    for (char c : attemptId)
        if (c != '-') compact.push_back(c);
    hexToBytes(compact, uuid.data(), uuid.size());
    o.insert(o.end(), uuid.begin(), uuid.end());
    auto putStr = [&](const std::string& s, size_t max) {
        size_t n = std::min(s.size(), max);
        o.push_back(static_cast<uint8_t>(n));
        o.insert(o.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    };
    putStr(levelId, 64);
    putStr(gameVersion, 16);
    for (const auto& f : frames) {
        putF32(o, f.x);
        putF32(o, f.y);
        o.push_back(f.state);
        o.push_back(f.flags);
        o.push_back(f.health);
        o.push_back(0);
    }
    for (const auto& e : events) {
        putU32(o, e.frame);
        o.push_back(static_cast<uint8_t>(e.type));
        o.push_back(e.arg);
        putU16(o, e.value);
    }
    return o;
}

std::optional<ReplayData> ReplayData::parse(const std::vector<uint8_t>& raw, std::string* error) {
    if (raw.size() < 70 || std::memcmp(raw.data(), "CPRP", 4) != 0) {
        fail(error, "en-tête CPRP absent");
        return std::nullopt;
    }
    const uint8_t* p = raw.data();
    ReplayData d;
    if (getU16(p + 4) != kFormatVersion) {
        fail(error, "version de format non prise en charge");
        return std::nullopt;
    }
    d.sampleHz = getU16(p + 6);
    uint32_t frameCount = getU32(p + 8);
    d.durationMs = getU32(p + 12);
    uint32_t eventCount = getU32(p + 16);
    if (d.sampleHz < 10 || d.sampleHz > 120 || frameCount == 0) {
        fail(error, "en-tête incohérent");
        return std::nullopt;
    }
    d.levelHash = bytesToHex(p + 20, 32);
    std::string u = bytesToHex(p + 52, 16);
    d.attemptId = u.substr(0, 8) + "-" + u.substr(8, 4) + "-" + u.substr(12, 4) + "-" + u.substr(16, 4) + "-" + u.substr(20);
    size_t at = 68;
    auto getStr = [&](size_t max, std::string& out) {
        if (at >= raw.size()) return false;
        size_t n = raw[at++];
        if (n > max || at + n > raw.size()) return false;
        out.assign(reinterpret_cast<const char*>(p + at), n);
        at += n;
        return true;
    };
    if (!getStr(64, d.levelId) || !getStr(16, d.gameVersion)) {
        fail(error, "fichier tronqué");
        return std::nullopt;
    }
    uint64_t need = static_cast<uint64_t>(frameCount) * 12 + static_cast<uint64_t>(eventCount) * 8;
    if (at + need != raw.size()) {
        fail(error, "taille incohérente");
        return std::nullopt;
    }
    d.frames.resize(frameCount);
    for (auto& f : d.frames) {
        f.x = getF32(p + at);
        f.y = getF32(p + at + 4);
        f.state = p[at + 8];
        f.flags = p[at + 9];
        f.health = p[at + 10];
        at += 12;
    }
    d.events.resize(eventCount);
    for (auto& e : d.events) {
        e.frame = getU32(p + at);
        e.type = static_cast<ReplayEventType>(p[at + 4]);
        e.arg = p[at + 5];
        e.value = getU16(p + at + 6);
        at += 8;
    }
    return d;
}

std::vector<uint8_t> ReplayData::encode() const {
    std::vector<uint8_t> raw = serialize();
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<uint8_t> out(8 + bound);
    std::memcpy(out.data(), "CPRZ", 4);
    uint32_t n = static_cast<uint32_t>(raw.size());
    for (int i = 0; i < 4; ++i) out[4 + i] = static_cast<uint8_t>((n >> (8 * i)) & 0xFF);
    mz_ulong len = bound;
    // zlib stream (RFC 1950), what the server's DecompressionStream("deflate") expects.
    if (mz_compress2(out.data() + 8, &len, raw.data(), static_cast<mz_ulong>(raw.size()), MZ_BEST_COMPRESSION) != MZ_OK) return {};
    out.resize(8 + len);
    return out;
}

std::optional<ReplayData> ReplayData::decode(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 10 || std::memcmp(data.data(), "CPRZ", 4) != 0) {
        fail(error, "en-tête CPRZ absent");
        return std::nullopt;
    }
    uint32_t rawSize = getU32(data.data() + 4);
    if (rawSize < 70 || rawSize > kMaxRawBytes) {
        fail(error, "taille décompressée invalide");
        return std::nullopt;
    }
    std::vector<uint8_t> raw(rawSize);
    mz_ulong len = rawSize;
    if (mz_uncompress(raw.data(), &len, data.data() + 8, static_cast<mz_ulong>(data.size() - 8)) != MZ_OK || len != rawSize) {
        fail(error, "flux compressé corrompu");
        return std::nullopt;
    }
    return parse(raw, error);
}

size_t ReplayData::countEvents(ReplayEventType t) const {
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [t](const ReplayEvent& e) { return e.type == t; }));
}

ReplayFrame ReplayData::sampleAt(float seconds) const {
    if (frames.empty()) return {};
    float f = std::max(0.0f, seconds) * static_cast<float>(sampleHz);
    size_t i = static_cast<size_t>(f);
    if (i + 1 >= frames.size()) return frames.back();
    ReplayFrame a = frames[i];
    const ReplayFrame& b = frames[i + 1];
    float t = f - static_cast<float>(i);
    // Do not interpolate across teleports / respawns.
    if (std::hypot(b.x - a.x, b.y - a.y) < 120.0f) {
        a.x += (b.x - a.x) * t;
        a.y += (b.y - a.y) * t;
    }
    return a;
}

// ---------------------------------------------------------------- recorder

void ReplayRecorder::begin(const std::string& levelId, const std::string& levelHash, const std::string& attemptId) {
    data_ = ReplayData{};
    data_.levelId = levelId;
    data_.levelHash = levelHash;
    data_.attemptId = attemptId;
    active_ = true;
    started_ = false;
}

void ReplayRecorder::push(ReplayEventType t, uint8_t arg, uint16_t value) {
    // Events between two samples belong to the next sample (finish() adds the last one).
    data_.events.push_back({static_cast<uint32_t>(data_.frames.size()), t, arg, value});
}

void ReplayRecorder::sample(const GameWorld& w) {
    const Player& p = w.player();
    ReplayFrame f;
    Vec2 feet = p.feet();
    f.x = feet.x;
    f.y = feet.y;
    f.state = static_cast<uint8_t>(p.state());
    f.flags = static_cast<uint8_t>((p.facing < 0 ? ReplayFrame::FacingLeft : 0) | (p.grounded() ? ReplayFrame::Grounded : 0) |
                                   (p.invulnerable() ? ReplayFrame::Invulnerable : 0) | (p.gliding() ? ReplayFrame::Gliding : 0) |
                                   (p.shieldActive() ? ReplayFrame::Shield : 0));
    f.health = static_cast<uint8_t>(std::clamp(p.health, 0, 255));
    data_.frames.push_back(f);
}

void ReplayRecorder::tick(const GameWorld& w) {
    if (!active_) return;
    const LevelTimer& timer = w.timer();
    if (!started_) {
        if (!timer.running()) return;
        started_ = true;
        const RunStats& s = w.stats();
        lastDeaths_ = s.deaths;
        lastShards_ = s.shards;
        lastRelics_ = s.relics;
        lastSecrets_ = s.secrets;
        lastKills_ = s.kills;
        lastDamage_ = s.damageTaken;
        lastState_ = static_cast<uint8_t>(w.player().state());
    }
    const RunStats& s = w.stats();
    for (; lastDeaths_ < s.deaths; ++lastDeaths_) push(ReplayEventType::Death);
    for (; lastShards_ < s.shards; ++lastShards_) push(ReplayEventType::Pickup, static_cast<uint8_t>(ReplayPickup::Shard));
    for (; lastRelics_ < s.relics; ++lastRelics_) push(ReplayEventType::Pickup, static_cast<uint8_t>(ReplayPickup::Relic));
    for (; lastSecrets_ < s.secrets; ++lastSecrets_) push(ReplayEventType::Pickup, static_cast<uint8_t>(ReplayPickup::Secret));
    for (; lastKills_ < s.kills; ++lastKills_) push(ReplayEventType::Kill);
    if (s.damageTaken > lastDamage_) {
        push(ReplayEventType::Damage, 0, static_cast<uint16_t>(std::min(s.damageTaken - lastDamage_, 65535)));
        lastDamage_ = s.damageTaken;
    }
    uint8_t state = static_cast<uint8_t>(w.player().state());
    if (state != lastState_ && w.player().state() == PlayerState::Respawn) push(ReplayEventType::Respawn);
    lastState_ = state;
    // One sample every 1/sampleHz second of level time (the timer, not wall clock).
    float next = static_cast<float>(data_.frames.size()) / static_cast<float>(data_.sampleHz);
    if (timer.elapsed() + 1e-4f >= next) sample(w);
}

void ReplayRecorder::onWorldEvent(const std::string& source, const std::string& trigger) {
    if (!active_ || !started_) return;
    if (source == "player" && trigger == "jump") {
        push(ReplayEventType::Jump);
    } else if (trigger == "powerUsed") {
        push(ReplayEventType::Power, replayPowerIndex(source));
        if (source == "teleport") push(ReplayEventType::Teleport);
    } else if (trigger == "checkpoint") {
        push(ReplayEventType::Checkpoint);
    }
}

void ReplayRecorder::finish(const GameWorld& w, float elapsed) {
    if (!active_) return;
    tick(w);
    sample(w);
    data_.durationMs = static_cast<uint32_t>(std::lround(elapsed * 1000.0f));
    data_.events.push_back({static_cast<uint32_t>(data_.frames.size() - 1), ReplayEventType::Finish, 0, 0});
    // Events recorded after the last sample point at it.
    for (auto& e : data_.events) e.frame = std::min<uint32_t>(e.frame, static_cast<uint32_t>(data_.frames.size() - 1));
    active_ = false;
}

ReplayData ReplayRecorder::take() {
    active_ = false;
    return std::move(data_);
}

}  // namespace cosmic
