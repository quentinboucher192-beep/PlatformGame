#include "tools/tas/Memory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/core/FileUtil.h"
#include "engine/physics/PhysicsWorld.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"

namespace cosmic::tas {

namespace {

// A stable hash (the file outlives the program: never std::hash).
uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}

std::string hex(uint64_t k) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(k));
    return b;
}

Json maneuverJson(const Maneuver& m) {
    return Json{{"dir", m.dir},       {"momentum", m.momentum}, {"fastFall", m.fastFall}, {"drop", m.drop},   {"slideAt", m.slideAt},
                {"pressAt", m.pressAt}, {"hold", m.hold},       {"gap", m.gap},           {"hold2", m.hold2}, {"key", m.key},
                {"keyX", m.keyX},     {"keyY", m.keyY},         {"keyAt", m.keyAt},       {"keyHold", m.keyHold}};
}

std::optional<Maneuver> maneuverOf(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    Maneuver m;
    auto i16 = [&](const char* k, int16_t def) { return static_cast<int16_t>(std::clamp(jget<int>(j, k, def), -1, static_cast<int>(Maneuver::kHeld))); };
    auto i8 = [&](const char* k, int8_t def) { return static_cast<int8_t>(std::clamp(jget<int>(j, k, def), -64, 64)); };
    m.dir = jget<int>(j, "dir", 1) < 0 ? int8_t{-1} : int8_t{1};
    m.momentum = jget<bool>(j, "momentum", false);
    m.fastFall = jget<bool>(j, "fastFall", false);
    m.drop = jget<bool>(j, "drop", false);
    m.slideAt = i16("slideAt", -1);
    m.pressAt = i16("pressAt", -1);
    m.hold = i16("hold", Maneuver::kHeld);
    m.gap = i16("gap", -1);
    m.hold2 = i16("hold2", Maneuver::kHeld);
    m.key = i8("key", -1);
    m.keyX = i8("keyX", 0);
    m.keyY = i8("keyY", 0);
    m.keyAt = i16("keyAt", -1);
    m.keyHold = i16("keyHold", 1);
    if (m.hold < 1 || m.hold2 < 1 || m.keyHold < 1) return std::nullopt;
    return m;
}

}  // namespace

Configuration Configuration::of(const GameWorld& w, int8_t dir) {
    Configuration c;
    c.dir = dir < 0 ? int8_t{-1} : int8_t{1};
    const Player& p = w.player();
    const Vec2 f = p.feet();
    const MoveOptions o = p.moveOptions();
    uint64_t h = mix(0xc0f16u, static_cast<uint64_t>(c.dir + 2));
    // The solids of a grid in front of her: row kRows - 1 the ground under her feet, the others
    // up to 144 px above them; column 0 one cell behind her, then kCols - 1 cells ahead.
    bool grid[kRows][kCols] = {};
    for (int r = 0; r < kRows; ++r)
        for (int col = 0; col < kCols; ++col) {
            const float x0 = c.dir > 0 ? std::floor(f.x) + static_cast<float>((col - 1) * kCell) : std::floor(f.x) - static_cast<float>(col * kCell);
            const float y0 = std::floor(f.y) - static_cast<float>((kRows - 1 - r) * kCell);
            grid[r][col] = w.physics().overlapsSolid(Rect{x0 + 1.0f, y0 + 1.0f, kCell - 2.0f, kCell - 2.0f}, o);
            h = mix(h, grid[r][col] ? 1u : 0u);
        }
    c.speed = static_cast<int>(std::lround(p.vel.x * static_cast<float>(c.dir) / 25.0f)) * 25;
    c.grounded = p.grounded();
    c.airJumps = p.airJumpsLeft();
    c.energy = std::min(8, static_cast<int>(p.energy / 25.0f)) * 25;
    std::vector<std::string> ready;
    for (const auto& pw : p.powers())
        if (pw->trigger() == PowerTrigger::SlotKey && pw->ready(p)) ready.push_back(pw->id());
    std::sort(ready.begin(), ready.end());
    for (const std::string& id : ready) c.powers += (c.powers.empty() ? "" : ",") + id;
    h = mix(h, static_cast<uint64_t>(c.speed + 10000));
    h = mix(h, c.grounded ? 1u : 0u);
    h = mix(h, static_cast<uint64_t>(c.airJumps + 8));
    h = mix(h, static_cast<uint64_t>(c.energy));
    for (char ch : c.powers) h = mix(h, static_cast<uint8_t>(ch));
    c.key = h;
    // What a person reads of it: the highest solid in front of her, the widest hole.
    for (int col = 1; col <= 10; ++col)
        for (int r = 0; r < kRows - 1; ++r)
            if (grid[r][col]) {
                const int height = (kRows - 1 - r) * kCell;
                if (height > c.rise) c.rise = height, c.riseAt = (col - 1) * kCell;
                break;
            }
    int run = 0;
    for (int col = 1; col < kCols; ++col) {
        run = grid[kRows - 1][col] ? 0 : run + 1;
        c.gap = std::max(c.gap, run * kCell);
    }
    return c;
}

Json Remembered::toJson() const {
    return Json{{"key", hex(config.key)},
                {"dir", config.dir},
                {"speed", config.speed},
                {"grounded", config.grounded},
                {"airJumps", config.airJumps},
                {"energy", config.energy},
                {"powers", config.powers},
                {"rise", config.rise},
                {"riseAt", config.riseAt},
                {"gap", config.gap},
                {"maneuver", maneuverJson(maneuver)},
                {"leadFrames", leadFrames},
                {"uses", uses},
                {"gainMs", gainMs},
                {"level", level}};
}

std::optional<Remembered> Remembered::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    Remembered r;
    const std::string k = jstr(j, "key");
    if (k.size() != 16 || k.find_first_not_of("0123456789abcdef") != std::string::npos) return std::nullopt;
    r.config.key = std::stoull(k, nullptr, 16);
    r.config.dir = jget<int>(j, "dir", 1) < 0 ? int8_t{-1} : int8_t{1};
    r.config.speed = jget<int>(j, "speed", 0);
    r.config.grounded = jget<bool>(j, "grounded", true);
    r.config.airJumps = std::clamp(jget<int>(j, "airJumps", 0), 0, 8);
    r.config.energy = std::clamp(jget<int>(j, "energy", 0), 0, 1000);
    r.config.powers = jstr(j, "powers");
    r.config.rise = std::clamp(jget<int>(j, "rise", 0), 0, 1000);
    r.config.riseAt = std::clamp(jget<int>(j, "riseAt", 0), 0, 1000);
    r.config.gap = std::clamp(jget<int>(j, "gap", 0), 0, 1000);
    const auto m = maneuverOf(j.value("maneuver", Json()));
    if (!m) return std::nullopt;
    r.maneuver = *m;
    r.leadFrames = std::max(0, jget<int>(j, "leadFrames", 0));
    r.uses = std::max(0, jget<int>(j, "uses", 0));
    r.gainMs = std::max<int64_t>(0, jget<int64_t>(j, "gainMs", 0));
    r.level = jstr(j, "level");
    return r;
}

TrajectoryMemory::TrajectoryMemory(std::filesystem::path file) : file_(std::move(file)) {}

void TrajectoryMemory::load() {
    std::lock_guard<std::mutex> l(m_);
    entries_.clear();
    if (file_.empty()) return;
    const auto text = FileUtil::readText(file_);
    if (!text) return;
    const Json j = Json::parse(*text, nullptr, false);
    if (!j.is_object() || jstr(j, "format") != "cosmic.tas.memory" || !j.contains("entries") || !j["entries"].is_array()) return;
    for (const Json& e : j["entries"]) {
        if (entries_.size() >= kMax) break;
        if (auto r = Remembered::fromJson(e)) entries_.push_back(std::move(*r));
    }
}

std::optional<Remembered> TrajectoryMemory::find(uint64_t key) const {
    std::lock_guard<std::mutex> l(m_);
    for (const Remembered& r : entries_)
        if (r.config.key == key) return r;
    return std::nullopt;
}

void TrajectoryMemory::remember(const Configuration& c, const Maneuver& m, int leadFrames, const std::string& level) {
    {
        std::lock_guard<std::mutex> l(m_);
        Remembered* hit = nullptr;
        for (Remembered& r : entries_)
            if (r.config.key == c.key) hit = &r;
        if (hit) {
            if (leadFrames <= hit->leadFrames) return;
            hit->maneuver = m;
            hit->leadFrames = leadFrames;
        } else {
            if (entries_.size() >= kMax) {
                // The least useful one makes room.
                auto worst = std::min_element(entries_.begin(), entries_.end(), [](const Remembered& a, const Remembered& b) {
                    return std::tie(a.gainMs, a.uses, a.leadFrames) < std::tie(b.gainMs, b.uses, b.leadFrames);
                });
                entries_.erase(worst);
            }
            Remembered r;
            r.config = c;
            r.maneuver = m;
            r.leadFrames = leadFrames;
            r.level = level;
            entries_.push_back(std::move(r));
        }
    }
    save();
}

void TrajectoryMemory::used(uint64_t key, int64_t gainMs) {
    {
        std::lock_guard<std::mutex> l(m_);
        for (Remembered& r : entries_)
            if (r.config.key == key) {
                ++r.uses;
                r.gainMs += std::max<int64_t>(0, gainMs);
            }
    }
    save();
}

size_t TrajectoryMemory::size() const {
    std::lock_guard<std::mutex> l(m_);
    return entries_.size();
}

std::vector<Remembered> TrajectoryMemory::top(size_t n) const {
    std::lock_guard<std::mutex> l(m_);
    std::vector<Remembered> out = entries_;
    std::stable_sort(out.begin(), out.end(), [](const Remembered& a, const Remembered& b) {
        return std::tie(b.gainMs, b.uses, b.leadFrames) < std::tie(a.gainMs, a.uses, a.leadFrames);
    });
    if (out.size() > n) out.resize(n);
    return out;
}

void TrajectoryMemory::save() const {
    if (file_.empty()) return;
    Json arr = Json::array();
    {
        std::lock_guard<std::mutex> l(m_);
        for (const Remembered& r : entries_) arr.push_back(r.toJson());
    }
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    FileUtil::writeTextAtomic(file_, Json{{"format", "cosmic.tas.memory"}, {"version", 1}, {"entries", arr}}.dump(1));
}

}  // namespace cosmic::tas
