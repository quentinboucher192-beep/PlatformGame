#include "game/online/Replay.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>

#include "game/gameplay/GameWorld.h"
#include "game/online/LevelFingerprint.h"

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

/// Inputs as runs of identical frames (18 bytes each: u16 repeat, u32 held, pressed, released,
/// i8 moveX, moveY, u8 flags, choice); returns the number of runs.
uint32_t putRuns(std::vector<uint8_t>& o, const std::vector<InputFrame>& inputs) {
    uint32_t runs = 0;
    for (size_t i = 0; i < inputs.size();) {
        size_t j = i + 1;
        while (j < inputs.size() && j - i < 0xFFFF && inputs[j] == inputs[i]) ++j;
        const InputFrame& f = inputs[i];
        putU16(o, static_cast<uint16_t>(j - i));
        putU32(o, f.held);
        putU32(o, f.pressed);
        putU32(o, f.released);
        o.push_back(static_cast<uint8_t>(f.moveX));
        o.push_back(static_cast<uint8_t>(f.moveY));
        o.push_back(f.flags);
        o.push_back(f.choice);
        ++runs;
        i = j;
    }
    return runs;
}

void putFrame(std::vector<uint8_t>& o, const ReplayFrame& f) {
    putF32(o, f.x);
    putF32(o, f.y);
    o.push_back(f.state);
    o.push_back(f.flags);
    o.push_back(f.health);
    o.push_back(0);
}

void putEvent(std::vector<uint8_t>& o, const ReplayEvent& e) {
    putU32(o, e.frame);
    o.push_back(static_cast<uint8_t>(e.type));
    o.push_back(e.arg);
    putU16(o, e.value);
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
    // Without recorded inputs (trace only) the file stays in version 1; a challenge run is
    // version 4 (its rules after the digests), every other run version 3.
    const bool teamRun = !inputs.empty() && !team.empty();  // 2.62: format 5
    const bool challenge = !inputs.empty() && !challengeRules.empty() && !teamRun;
    putU16(o, inputs.empty() ? 1 : teamRun ? kFormatTeam : challenge ? kFormatChallenge : kFormatVersion);
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
    for (const auto& f : frames) putFrame(o, f);
    for (const auto& e : events) putEvent(o, e);
    if (inputs.empty()) return o;
    // Version 3: seed, run-length encoded inputs, state digests.
    putU32(o, static_cast<uint32_t>(seed & 0xFFFFFFFFu));
    putU32(o, static_cast<uint32_t>(seed >> 32));
    putU16(o, tickHz);
    // Input flags (reserved, 0, in version 2).
    putU16(o, static_cast<uint16_t>((keysPerSlot ? kInputKeysPerSlot : 0) | (challenge ? kInputChallengeRules : 0)));
    putU32(o, static_cast<uint32_t>(inputs.size()));
    size_t runCountAt = o.size();
    putU32(o, 0);
    const uint32_t runs = putRuns(o, inputs);  // the power choice in the last byte (was reserved)
    for (int k = 0; k < 4; ++k) o[runCountAt + k] = static_cast<uint8_t>((runs >> (8 * k)) & 0xFF);
    putU32(o, static_cast<uint32_t>(checks.size()));
    for (const auto& c : checks) {
        putU32(o, c.tick);
        putU32(o, static_cast<uint32_t>(c.hash & 0xFFFFFFFFu));
        putU32(o, static_cast<uint32_t>(c.hash >> 32));
    }
    if (challenge) {
        // Version 4: the canonical form of the challenge rules (1 .. 1024 bytes).
        size_t n = std::min(challengeRules.size(), ChallengeRules::kMaxCanonicalBytes);
        putU16(o, static_cast<uint16_t>(n));
        o.insert(o.end(), challengeRules.begin(), challengeRules.begin() + static_cast<std::ptrdiff_t>(n));
    }
    if (teamRun) {
        // Version 5: the team section (docs/COOP.md § 5).
        o.push_back(static_cast<uint8_t>(teamSize()));
        putU16(o, coopVersion);
        o.push_back(teamIncomplete ? 1 : 0);
        o.push_back(revives);
        for (const TeamPlayer& t : team) {
            putU32(o, static_cast<uint32_t>(t.frames.size()));
            putU32(o, static_cast<uint32_t>(t.events.size()));
            for (const auto& f : t.frames) putFrame(o, f);
            for (const auto& e : t.events) putEvent(o, e);
            size_t at = o.size();
            putU32(o, 0);
            const uint32_t r = putRuns(o, t.inputs);
            for (int k = 0; k < 4; ++k) o[at + k] = static_cast<uint8_t>((r >> (8 * k)) & 0xFF);
        }
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
    d.formatVersion = getU16(p + 4);
    if (d.formatVersion < 1 || d.formatVersion > kFormatTeam) {
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
    if (d.formatVersion == 1 ? at + need != raw.size() : at + need > raw.size()) {
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
    if (d.formatVersion == 1) return d;
    // ---- version 2, 3 and 4 trailer
    auto left = [&]() { return raw.size() - at; };
    if (left() < 20) {
        fail(error, "section des entrées absente");
        return std::nullopt;
    }
    d.seed = static_cast<uint64_t>(getU32(p + at)) | (static_cast<uint64_t>(getU32(p + at + 4)) << 32);
    d.tickHz = getU16(p + at + 8);
    // Version 3: input flags (version 2: reserved, one button per power).
    const uint16_t inputFlags = d.formatVersion >= 3 ? getU16(p + at + 10) : 0;
    d.keysPerSlot = (inputFlags & kInputKeysPerSlot) != 0;
    uint32_t tickCount = getU32(p + at + 12);
    uint32_t runCount = getU32(p + at + 16);
    at += 20;
    // A run: 12 bytes in version 2 (actions on 16 bits), 18 in version 3 and 4 (32 bits).
    const size_t runBytes = d.formatVersion >= 3 ? 18 : 12;
    // Version 4 is exactly a challenge run (flag 2 and its section); no other bit exists.
    const bool challenge = d.formatVersion == kFormatChallenge;
    const bool teamRun = d.formatVersion == kFormatTeam;  // 2.62
    const uint16_t knownFlags = challenge ? (kInputKeysPerSlot | kInputChallengeRules) : kInputKeysPerSlot;
    if (d.tickHz != kSimHz || tickCount == 0 || tickCount > kMaxTicks || runCount == 0 || runCount > tickCount ||
        (inputFlags & ~knownFlags) != 0 || challenge != ((inputFlags & kInputChallengeRules) != 0) ||
        static_cast<uint64_t>(runCount) * runBytes + 4 > left()) {
        fail(error, "section des entrées incohérente");
        return std::nullopt;
    }
    d.inputs.reserve(tickCount);
    for (uint32_t r = 0; r < runCount; ++r) {
        uint16_t repeat = getU16(p + at);
        InputFrame f;
        size_t k = at + 2;
        if (runBytes == 18) {
            f.held = getU32(p + k);
            f.pressed = getU32(p + k + 4);
            f.released = getU32(p + k + 8);
            k += 12;
        } else {
            f.held = getU16(p + k);
            f.pressed = getU16(p + k + 2);
            f.released = getU16(p + k + 4);
            k += 6;
        }
        // Only the actions recorded by that version (version 2: up to Special).
        constexpr uint32_t kMask3 = InputFrame::kActions >= 32 ? 0xFFFFFFFFu : (1u << InputFrame::kActions) - 1u;
        constexpr uint32_t kMask2 = (1u << (static_cast<int>(Action::Special) + 1)) - 1u;
        const uint32_t mask = runBytes == 18 ? kMask3 : kMask2;
        f.held &= mask;
        f.pressed &= mask;
        f.released &= mask;
        f.moveX = static_cast<int8_t>(p[k]);
        f.moveY = static_cast<int8_t>(p[k + 1]);
        f.flags = p[k + 2];
        f.choice = p[k + 3];
        at += runBytes;
        if (repeat == 0 || d.inputs.size() + repeat > tickCount) {
            fail(error, "plages d'entrées incohérentes");
            return std::nullopt;
        }
        d.inputs.insert(d.inputs.end(), repeat, f);
    }
    if (d.inputs.size() != tickCount || left() < 4) {
        fail(error, "nombre d'entrées incohérent");
        return std::nullopt;
    }
    uint32_t checkCount = getU32(p + at);
    at += 4;
    const uint64_t checkBytes = static_cast<uint64_t>(checkCount) * 12;
    if (checkCount > tickCount / kCheckTicks + 2 || (challenge ? checkBytes + 2 > left() : teamRun ? checkBytes + 5 > left() : checkBytes != left())) {
        fail(error, "sommes de contrôle incohérentes");
        return std::nullopt;
    }
    d.checks.resize(checkCount);
    uint32_t prevTick = 0;
    for (auto& c : d.checks) {
        c.tick = getU32(p + at);
        c.hash = static_cast<uint64_t>(getU32(p + at + 4)) | (static_cast<uint64_t>(getU32(p + at + 8)) << 32);
        at += 12;
        if (c.tick <= prevTick || c.tick > tickCount) {
            fail(error, "sommes de contrôle incohérentes");
            return std::nullopt;
        }
        prevTick = c.tick;
    }
    if (challenge) {
        // Version 4: u16 length, then the canonical form of the rules, and nothing after.
        uint16_t n = getU16(p + at);
        at += 2;
        if (n == 0 || n > ChallengeRules::kMaxCanonicalBytes || left() != n) {
            fail(error, "section des règles du défi incohérente");
            return std::nullopt;
        }
        d.challengeRules.assign(reinterpret_cast<const char*>(p + at), n);
        std::string why;
        if (!ChallengeRules::fromCanonical(d.challengeRules, &why)) {
            fail(error, ("règles du défi : " + why).c_str());
            return std::nullopt;
        }
    }
    if (teamRun) {
        // Version 5: the team section; the file ends right after it.
        const uint8_t size = p[at];
        d.coopVersion = getU16(p + at + 1);
        const uint8_t flags = p[at + 3];
        d.revives = p[at + 4];
        at += 5;
        if (size < 2 || size > 3 || (flags & ~1u) != 0 || d.revives > 2) {
            fail(error, "section d'équipe incohérente");
            return std::nullopt;
        }
        d.teamIncomplete = (flags & 1) != 0;
        for (int i = 1; i < size; ++i) {
            ReplayData::TeamPlayer t;
            if (left() < 8) {
                fail(error, "section d'équipe tronquée");
                return std::nullopt;
            }
            const uint32_t fc = getU32(p + at), ec = getU32(p + at + 4);
            at += 8;
            if (fc != frameCount || static_cast<uint64_t>(fc) * 12 + static_cast<uint64_t>(ec) * 8 + 4 > left()) {
                fail(error, "section d'équipe incohérente");
                return std::nullopt;
            }
            t.frames.resize(fc);
            for (auto& f : t.frames) {
                f.x = getF32(p + at);
                f.y = getF32(p + at + 4);
                f.state = p[at + 8];
                f.flags = p[at + 9];
                f.health = p[at + 10];
                at += 12;
            }
            t.events.resize(ec);
            for (auto& e : t.events) {
                e.frame = getU32(p + at);
                e.type = static_cast<ReplayEventType>(p[at + 4]);
                e.arg = p[at + 5];
                e.value = getU16(p + at + 6);
                at += 8;
            }
            const uint32_t rc = getU32(p + at);
            at += 4;
            if (rc == 0 || rc > tickCount || static_cast<uint64_t>(rc) * 18 > left()) {
                fail(error, "entrées d'équipe incohérentes");
                return std::nullopt;
            }
            t.inputs.reserve(tickCount);
            constexpr uint32_t kMask = InputFrame::kActions >= 32 ? 0xFFFFFFFFu : (1u << InputFrame::kActions) - 1u;
            for (uint32_t r = 0; r < rc; ++r) {
                const uint16_t repeat = getU16(p + at);
                InputFrame f;
                f.held = getU32(p + at + 2) & kMask;
                f.pressed = getU32(p + at + 6) & kMask;
                f.released = getU32(p + at + 10) & kMask;
                f.moveX = static_cast<int8_t>(p[at + 14]);
                f.moveY = static_cast<int8_t>(p[at + 15]);
                f.flags = p[at + 16];
                f.choice = p[at + 17];
                at += 18;
                if (repeat == 0 || t.inputs.size() + repeat > tickCount) {
                    fail(error, "entrées d'équipe incohérentes");
                    return std::nullopt;
                }
                t.inputs.insert(t.inputs.end(), repeat, f);
            }
            if (t.inputs.size() != tickCount) {
                fail(error, "entrées d'équipe incohérentes");
                return std::nullopt;
            }
            d.team.push_back(std::move(t));
        }
        if (left() != 0) {
            fail(error, "section d'équipe incohérente");
            return std::nullopt;
        }
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

int64_t ReplayData::splitTimeMs(const ReplayEvent& e) const {
    // The 16 low bits are exact; the frame (sampled at sampleHz) is within a sample of the
    // time: the nearest value with those low bits is the split.
    int64_t approx = static_cast<int64_t>(e.frame) * 1000 / std::max<int64_t>(1, sampleHz);
    int64_t base = approx - (approx & 0xFFFF) + e.value;
    int64_t best = base;
    for (int64_t c : {base - 65536, base + 65536})
        if (c >= 0 && std::llabs(c - approx) < std::llabs(best - approx)) best = c;
    return std::max<int64_t>(0, best);
}

// ---------------------------------------------------------------- recorder

void ReplayRecorder::begin(const std::string& levelId, const std::string& levelHash, const std::string& attemptId, uint64_t seed) {
    data_ = ReplayData{};
    data_.levelId = levelId;
    data_.levelHash = levelHash;
    data_.attemptId = attemptId;
    data_.seed = seed;
    active_ = true;
    started_ = false;
    lastBoss_.clear();
    lastWaves_.clear();
    lastSplits_ = 0;
}

void ReplayRecorder::beginTeam(int players) {
    data_.team.assign(static_cast<size_t>(std::clamp(players, 2, 3) - 1), ReplayData::TeamPlayer{});
    data_.coopVersion = static_cast<uint16_t>(kCoopSimulationVersion);
    track_.assign(static_cast<size_t>(std::clamp(players, 2, 3)), TeamTrack{});
}

void ReplayRecorder::recordTeamInputs(const InputFrame* frames, int count) {
    if (!active_ || data_.inputs.size() >= ReplayData::kMaxTicks || count < 1) return;
    data_.inputs.push_back(frames[0]);
    for (size_t i = 0; i < data_.team.size(); ++i)
        data_.team[i].inputs.push_back(static_cast<int>(i) + 1 < count ? frames[i + 1] : InputFrame{});
}

void ReplayRecorder::pushTo(int player, ReplayEventType t, uint8_t arg, uint16_t value) {
    if (player <= 0 || data_.team.empty()) {
        push(t, arg, value);
        return;
    }
    auto& tp = data_.team[static_cast<size_t>(std::min<int>(player, static_cast<int>(data_.team.size())) - 1)];
    tp.events.push_back({static_cast<uint32_t>(tp.frames.size()), t, arg, value});
}

namespace {
ReplayFrame frameOf(const Player& p) {
    ReplayFrame f;
    Vec2 feet = p.feet();
    f.x = feet.x;
    f.y = feet.y;
    f.state = static_cast<uint8_t>(p.state());
    f.flags = static_cast<uint8_t>((p.facing < 0 ? ReplayFrame::FacingLeft : 0) | (p.grounded() ? ReplayFrame::Grounded : 0) |
                                   (p.invulnerable() ? ReplayFrame::Invulnerable : 0) | (p.gliding() ? ReplayFrame::Gliding : 0) |
                                   (p.shieldActive() ? ReplayFrame::Shield : 0));
    f.health = static_cast<uint8_t>(std::clamp(p.health, 0, 255));
    return f;
}
}  // namespace

void ReplayRecorder::sampleTeam(const GameWorld& w) {
    for (size_t i = 0; i < data_.team.size(); ++i)
        if (static_cast<int>(i) + 1 < w.teamSize()) data_.team[i].frames.push_back(frameOf(w.playerAt(static_cast<int>(i) + 1)));
}

void ReplayRecorder::teamTick(const GameWorld& w) {
    // Each player's own events: its fall, its revival, its return to the team, its standby.
    for (int i = 0; i < w.teamSize() && static_cast<size_t>(i) < track_.size(); ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        TeamTrack& t = track_[static_cast<size_t>(i)];
        const auto state = static_cast<uint8_t>(w.playerAt(i).state());
        if (state == static_cast<uint8_t>(PlayerState::Dead) && t.state != state) pushTo(i, ReplayEventType::Death);
        t.state = state;
        for (; t.revived < m.revived; ++t.revived) pushTo(i, ReplayEventType::Revive);
        for (; t.regrouped < m.regrouped; ++t.regrouped) pushTo(i, ReplayEventType::Regroup);
        for (; t.returned < m.returned; ++t.returned) pushTo(i, ReplayEventType::Return);
        const int presence = static_cast<int>(m.presence);
        if (presence != t.presence) {
            if (m.presence == GameWorld::Presence::Standby) pushTo(i, ReplayEventType::Standby);
            if (m.presence == GameWorld::Presence::Gone) pushTo(i, ReplayEventType::Gone);
            t.presence = presence;
        }
    }
}

void ReplayRecorder::onWorldEvent(const std::string& source, const std::string& trigger, int player) {
    if (!active_ || !started_) return;
    if (source == "player" && trigger == "jump") {
        pushTo(player, ReplayEventType::Jump);
    } else if (trigger == "powerUsed") {
        pushTo(player, ReplayEventType::Power, replayPowerIndex(source));
        if (source == "teleport") pushTo(player, ReplayEventType::Teleport);
    } else if (trigger == "checkpoint") {
        push(ReplayEventType::Checkpoint);
    }
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

void ReplayRecorder::recordInput(const InputFrame& f) {
    if (!active_ || data_.inputs.size() >= ReplayData::kMaxTicks) return;
    data_.inputs.push_back(f);
}

void ReplayRecorder::check(const GameWorld& w) {
    auto tick = static_cast<uint32_t>(data_.inputs.size());
    if (tick == 0 || (!data_.checks.empty() && data_.checks.back().tick == tick)) return;
    data_.checks.push_back({tick, simulationStateHash(w)});
}

void ReplayRecorder::tick(const GameWorld& w) {
    if (!active_) return;
    data_.keysPerSlot = w.keysPerSlot();
    if (data_.challengeRules.empty())
        if (const ChallengeRules* c = w.challenge()) data_.challengeRules = c->canonical();
    if (!data_.inputs.empty() && data_.inputs.size() % ReplayData::kCheckTicks == 0) check(w);
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
        lastSplits_ = timer.splits().size();
    }
    const RunStats& s = w.stats();
    if (!data_.team.empty()) {
        lastDeaths_ = s.deaths;  // 2.62: a team's falls are in each player's own trace
        teamTick(w);
    }
    for (; lastDeaths_ < s.deaths; ++lastDeaths_) push(ReplayEventType::Death);
    for (; lastShards_ < s.shards; ++lastShards_) push(ReplayEventType::Pickup, static_cast<uint8_t>(ReplayPickup::Shard));
    for (; lastRelics_ < s.relics; ++lastRelics_) push(ReplayEventType::Pickup, static_cast<uint8_t>(ReplayPickup::Relic));
    for (; lastSecrets_ < s.secrets; ++lastSecrets_) push(ReplayEventType::Pickup, static_cast<uint8_t>(ReplayPickup::Secret));
    for (; lastKills_ < s.kills; ++lastKills_) push(ReplayEventType::Kill);
    if (s.damageTaken > lastDamage_) {
        push(ReplayEventType::Damage, 0, static_cast<uint16_t>(std::min(s.damageTaken - lastDamage_, 65535)));
        lastDamage_ = s.damageTaken;
    }
    // Checkpoint splits to the millisecond: the low 16 bits of the time, the event's frame
    // (within a sample of it) gives the rest (see splitTimeMs).
    for (; lastSplits_ < timer.splits().size(); ++lastSplits_) {
        long ms = std::lround(static_cast<double>(timer.splits()[lastSplits_]) * 1000.0);
        push(ReplayEventType::Split, static_cast<uint8_t>(std::min<size_t>(lastSplits_, 255)), static_cast<uint16_t>(static_cast<unsigned long>(std::max(0L, ms)) & 0xFFFFu));
    }
    uint8_t state = static_cast<uint8_t>(w.player().state());
    if (state != lastState_ && w.player().state() == PlayerState::Respawn && data_.team.empty()) push(ReplayEventType::Respawn);
    lastState_ = state;
    // One sample every 1/sampleHz second of level time (the timer, not wall clock).
    float next = static_cast<float>(data_.frames.size()) / static_cast<float>(data_.sampleHz);
    if (timer.elapsed() + 1e-4f >= next) {
        sample(w);
        sampleTeam(w);  // a team: every player at the same instants
    }
}

void ReplayRecorder::raceProgress(const std::vector<float>& bossRemoved, const std::vector<int>& wavesDone) {
    if (!active_ || !started_) return;
    lastBoss_.resize(bossRemoved.size(), 0);
    lastWaves_.resize(wavesDone.size(), 0);
    for (size_t i = 0; i < bossRemoved.size() && i < 255; ++i) {
        auto pm = static_cast<uint16_t>(std::clamp(std::lround(bossRemoved[i] * 1000.0f), 0L, 1000L));
        // Every percent (and the end): enough to recompute the score, few events.
        if (pm >= lastBoss_[i] + 10 || (pm == 1000 && lastBoss_[i] != 1000)) {
            push(ReplayEventType::BossHit, static_cast<uint8_t>(i), pm);
            lastBoss_[i] = pm;
        }
    }
    for (size_t i = 0; i < wavesDone.size() && i < 255; ++i)
        if (wavesDone[i] > lastWaves_[i]) {
            push(ReplayEventType::WaveCleared, static_cast<uint8_t>(i), static_cast<uint16_t>(wavesDone[i]));
            lastWaves_[i] = wavesDone[i];
        }
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

void ReplayRecorder::finishAttempt(const GameWorld& w, float elapsed) {
    if (!active_) return;
    tick(w);
    check(w);
    sample(w);
    sampleTeam(w);
    data_.durationMs = static_cast<uint32_t>(std::lround(elapsed * 1000.0f));
    for (auto& e : data_.events) e.frame = std::min<uint32_t>(e.frame, static_cast<uint32_t>(data_.frames.size() - 1));
    for (auto& t : data_.team)
        for (auto& e : t.events) e.frame = std::min<uint32_t>(e.frame, static_cast<uint32_t>(std::max<size_t>(1, t.frames.size()) - 1));
    if (!data_.team.empty()) {
        data_.teamIncomplete = w.teamIncomplete();
        data_.revives = static_cast<uint8_t>(w.revivesUsed());
    }
    active_ = false;
}

void ReplayRecorder::finish(const GameWorld& w, float elapsed) {
    if (!active_) return;
    tick(w);
    check(w);  // the final state always has its digest
    sample(w);
    data_.durationMs = static_cast<uint32_t>(std::lround(elapsed * 1000.0f));
    data_.events.push_back({static_cast<uint32_t>(data_.frames.size() - 1), ReplayEventType::Finish, 0, 0});
    // Events recorded after the last sample point at it.
    for (auto& e : data_.events) e.frame = std::min<uint32_t>(e.frame, static_cast<uint32_t>(data_.frames.size() - 1));
    sampleTeam(w);
    for (auto& t : data_.team)
        for (auto& e : t.events) e.frame = std::min<uint32_t>(e.frame, static_cast<uint32_t>(std::max<size_t>(1, t.frames.size()) - 1));
    if (!data_.team.empty()) {
        data_.teamIncomplete = w.teamIncomplete();
        data_.revives = static_cast<uint8_t>(w.revivesUsed());
    }
    active_ = false;
}

ReplaySimulator::ReplaySimulator(const ReplayData& replay, GameWorld& world) : replay_(replay), world_(world) {
    world_.setKeysPerSlot(replay_.keysPerSlot);
    world_.seedGameplay(replay_.seed);
    // A challenge run (version 4): its rules, as the run was set up (after the level's load).
    if (!replay_.challengeRules.empty())
        if (auto rules = ChallengeRules::fromCanonical(replay_.challengeRules)) world_.setChallenge(*rules);
}

bool ReplaySimulator::step() {
    if (finished() || desynced()) return false;
    if (!replay_.team.empty()) {
        // 2.62: a team's step (the world was given the team's size before its load).
        InputFrame frames[GameWorld::kMaxTeam];
        frames[0] = replay_.inputs[tick_];
        int n = 1;
        for (const auto& t : replay_.team) frames[n++] = tick_ < t.inputs.size() ? t.inputs[tick_] : InputFrame{};
        stepTeam(world_, frames, n, kSimDt);
    } else {
        stepWorld(world_, replay_.inputs[tick_], kSimDt);
    }
    ++tick_;
    while (nextCheck_ < replay_.checks.size() && replay_.checks[nextCheck_].tick <= tick_) {
        const ReplayCheck& c = replay_.checks[nextCheck_++];
        if (c.tick != tick_) continue;  // malformed list: ignored
        if (simulationStateHash(world_) != c.hash) {
            desyncTick_ = tick_;
            return false;
        }
        ++checksPassed_;
    }
    return true;
}

ReplayData ReplayRecorder::take() {
    active_ = false;
    return std::move(data_);
}

}  // namespace cosmic
