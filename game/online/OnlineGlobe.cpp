#include "game/online/OnlineGlobe.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>

#include "engine/core/FileUtil.h"

namespace cosmic {

namespace {
constexpr double kPiD = 3.14159265358979323846;
constexpr double kDegD = kPiD / 180.0;

double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }
}  // namespace

Vec3d geoToUnit(GeoPoint p) {
    double la = p.lat * kDegD, lo = p.lon * kDegD;
    return {std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)};
}

GeoPoint unitToGeo(const Vec3d& v) {
    double len = std::sqrt(v.dot(v));
    if (len <= 0.0) return {};
    return {std::asin(std::clamp(v.y / len, -1.0, 1.0)) / kDegD, std::atan2(v.x, v.z) / kDegD};
}

double geoAngle(GeoPoint a, GeoPoint b) { return std::acos(std::clamp(geoToUnit(a).dot(geoToUnit(b)), -1.0, 1.0)) / kDegD; }

double wrapLon(double lon) {
    lon = std::fmod(lon + 180.0, 360.0);
    if (lon < 0) lon += 360.0;
    return lon - 180.0;
}

// --------------------------------------------------------------------- camera

void GlobeCamera::rotate(double dLonDeg, double dLatDeg) {
    cancelFlight();
    center.lon = wrapLon(center.lon - dLonDeg);
    center.lat = std::clamp(center.lat + dLatDeg, -75.0, 75.0);
}

void GlobeCamera::zoomBy(double factor) {
    zoom = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
    zoomTo_ = zoom;
}

void GlobeCamera::flyTo(GeoPoint target, double targetZoom, float seconds) {
    from_ = center;
    to_ = {std::clamp(target.lat, -75.0, 75.0), wrapLon(target.lon)};
    // Shortest way around the globe.
    double d = wrapLon(to_.lon - from_.lon);
    to_.lon = from_.lon + d;
    zoomFrom_ = zoom;
    zoomTo_ = std::clamp(targetZoom, kMinZoom, kMaxZoom);
    flightT_ = 0.0f;
    flightDur_ = std::max(0.05f, seconds);
}

void GlobeCamera::update(float dt) {
    if (!flying()) return;
    flightT_ = std::min(flightDur_, flightT_ + dt);
    double t = smoothstep(flightT_ / flightDur_);
    center.lat = from_.lat + (to_.lat - from_.lat) * t;
    center.lon = wrapLon(from_.lon + (to_.lon - from_.lon) * t);
    // Zoom out a little in the middle of long flights (cinematic arc).
    double arc = std::sin(t * kPiD) * std::min(0.35, geoAngle(from_, to_) / 180.0);
    zoom = std::clamp(zoomFrom_ + (zoomTo_ - zoomFrom_) * t - arc, kMinZoom, kMaxZoom);
}

Vec3d GlobeCamera::toView(const Vec3d& p) const {
    double th = -center.lon * kDegD;
    double x1 = p.x * std::cos(th) + p.z * std::sin(th);
    double z1 = -p.x * std::sin(th) + p.z * std::cos(th);
    double ph = center.lat * kDegD;
    double y2 = p.y * std::cos(ph) - z1 * std::sin(ph);
    double z2 = p.y * std::sin(ph) + z1 * std::cos(ph);
    return {x1, y2, z2};
}

Vec3d GlobeCamera::fromView(const Vec3d& v) const {
    double ph = -center.lat * kDegD;
    double y1 = v.y * std::cos(ph) - v.z * std::sin(ph);
    double z1 = v.y * std::sin(ph) + v.z * std::cos(ph);
    double th = center.lon * kDegD;
    double x2 = v.x * std::cos(th) + z1 * std::sin(th);
    double z2 = -v.x * std::sin(th) + z1 * std::cos(th);
    return {x2, y1, z2};
}

Projected projectGeo(const GlobeCamera& cam, const GlobeView& view, GeoPoint p) {
    Vec3d v = cam.toView(geoToUnit(p));
    float r = view.radius(cam);
    return {{view.center.x + r * static_cast<float>(v.x), view.center.y - r * static_cast<float>(v.y)}, v.z};
}

std::optional<GeoPoint> unprojectScreen(const GlobeCamera& cam, const GlobeView& view, Vec2 screen) {
    float r = view.radius(cam);
    double x = (screen.x - view.center.x) / r;
    double y = -(screen.y - view.center.y) / r;
    double d2 = x * x + y * y;
    if (d2 > 1.0) return std::nullopt;
    return unitToGeo(cam.fromView({x, y, std::sqrt(1.0 - d2)}));
}

// -------------------------------------------------------------------- markers

std::optional<GlobeMarker> GlobeMarker::fromJson(const Json& j) {
    if (!j.is_object() || !j.contains("latitude") || !j["latitude"].is_number() || !j.contains("longitude") || !j["longitude"].is_number())
        return std::nullopt;
    GlobeMarker m;
    m.recordId = jstr(j, "record_id");
    m.playerId = jstr(j, "player_id");
    m.playerName = jstr(j, "player_name");
    m.avatar = jstr(j, "avatar", "avatar_01");
    m.countryCode = jstr(j, "country_code");
    m.levelId = jstr(j, "level_id");
    m.achievedAt = jstr(j, "achieved_at");
    m.location = {std::clamp(j["latitude"].get<double>(), -90.0, 90.0), wrapLon(j["longitude"].get<double>())};
    m.display = m.location;
    m.rank = jget<int>(j, "rank", 0);
    m.timeMs = jget<int64_t>(j, "time_ms", 0);
    m.isMe = jget<bool>(j, "is_me", false);
    if (m.playerName.empty()) return std::nullopt;
    return m;
}

MarkerTier markerTier(int rank) {
    switch (rank) {
        case 1: return MarkerTier::Gold;
        case 2: return MarkerTier::Silver;
        case 3: return MarkerTier::Bronze;
        default: return MarkerTier::Cyan;
    }
}

void layoutMarkers(std::vector<GlobeMarker>& markers, float radiusPx, float spacingPx) {
    // Angular size of `spacingPx` pixels at the centre of the disk.
    double step = std::clamp(static_cast<double>(spacingPx) / std::max(1.0f, radiusPx) / kDegD, 0.05, 6.0);
    std::map<std::pair<long long, long long>, int> seen;
    for (auto& m : markers) {
        auto key = std::make_pair(std::llround(m.location.lat * 10.0), std::llround(m.location.lon * 10.0));
        int k = seen[key]++;
        if (k == 0) {
            m.display = m.location;
            continue;
        }
        // Golden-angle spiral: even spacing whatever the number of players.
        double a = k * 2.399963229728653;
        double r = step * std::sqrt(static_cast<double>(k));
        double lat = std::clamp(m.location.lat + r * std::sin(a), -89.0, 89.0);
        double lon = m.location.lon + r * std::cos(a) / std::max(0.2, std::cos(m.location.lat * kDegD));
        m.display = {lat, wrapLon(lon)};
    }
}

std::vector<GlobeCluster> clusterMarkers(const std::vector<GlobeMarker>& markers, const GlobeCamera& cam, const GlobeView& view,
                                         float pixelRadius, size_t maxClusters) {
    std::vector<int> order(markers.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        int ra = markers[a].rank > 0 ? markers[a].rank : 1 << 30;
        int rb = markers[b].rank > 0 ? markers[b].rank : 1 << 30;
        return ra < rb;
    });
    std::vector<GlobeCluster> out;
    float r2 = pixelRadius * pixelRadius;
    for (int i : order) {
        Projected p = projectGeo(cam, view, markers[i].display);
        if (p.depth < 0.06) continue;  // hidden behind the globe (or on the very edge)
        GlobeCluster* hit = nullptr;
        for (auto& c : out) {
            float dx = c.screen.x - p.screen.x, dy = c.screen.y - p.screen.y;
            if (dx * dx + dy * dy <= r2) {
                hit = &c;
                break;
            }
        }
        if (hit) {
            hit->members.push_back(i);
            continue;
        }
        if (out.size() >= maxClusters) continue;
        GlobeCluster c;
        c.screen = p.screen;
        c.depth = p.depth;
        c.members.push_back(i);
        c.bestRank = markers[i].rank;
        out.push_back(c);
    }
    return out;
}

int pickCluster(const std::vector<GlobeCluster>& clusters, Vec2 screen, float radius) {
    int best = -1;
    float bestD = radius * radius;
    for (size_t i = 0; i < clusters.size(); ++i) {
        float dx = clusters[i].screen.x - screen.x, dy = clusters[i].screen.y - screen.y;
        float d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

GeoPoint subsolarPoint(std::chrono::system_clock::time_point t) {
    // Days since J2000.0 (2000-01-01 12:00 UTC).
    double days = std::chrono::duration<double>(t.time_since_epoch()).count() / 86400.0 - 10957.5;
    double g = (357.529 + 0.98560028 * days) * kDegD;              // mean anomaly
    double q = 280.459 + 0.98564736 * days;                       // mean longitude
    double L = (q + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g)) * kDegD;  // ecliptic longitude
    double e = (23.439 - 0.00000036 * days) * kDegD;               // obliquity
    double decl = std::asin(std::sin(e) * std::sin(L));
    double ra = std::atan2(std::cos(e) * std::sin(L), std::cos(L)) / kDegD;
    double gmst = std::fmod(280.46061837 + 360.98564736629 * days, 360.0);
    return {decl / kDegD, wrapLon(ra - gmst)};
}

// ------------------------------------------------------------------ countries

bool CountryTable::load(const fs::path& codesFile, const fs::path& flagsFile) {
    list_.clear();
    byCode_.clear();
    byIndex_.clear();
    auto codes = FileUtil::loadJson(codesFile);
    if (!codes || !codes->contains("codes") || !(*codes)["codes"].is_array()) return false;
    Json flags = FileUtil::loadJson(flagsFile).value_or(Json::object());
    flagColumns_ = jget<int>(flags, "columns", 32);
    flagW_ = jget<int>(flags, "cell_w", 48);
    flagH_ = jget<int>(flags, "cell_h", 36);
    const Json cells = flags.contains("cells") && flags["cells"].is_object() ? flags["cells"] : Json::object();
    for (const auto& c : (*codes)["codes"]) {
        CountryInfo info;
        info.code = jstr(c, "code");
        if (info.code.size() != 2) continue;
        info.nameFr = jstr(c, "name_fr", info.code);
        info.nameEn = jstr(c, "name_en", info.code);
        info.index = jget<int>(c, "index", 0);
        if (c.contains("lat") && c["lat"].is_number() && c.contains("lon") && c["lon"].is_number())
            info.center = GeoPoint{c["lat"].get<double>(), c["lon"].get<double>()};
        info.flagCell = jget<int>(cells, info.code.c_str(), 0);
        byCode_[info.code] = list_.size();
        if (info.index > 0) byIndex_[info.index] = list_.size();
        list_.push_back(info);
    }
    return !list_.empty();
}

const CountryInfo* CountryTable::find(const std::string& code) const {
    auto it = byCode_.find(code);
    return it == byCode_.end() ? nullptr : &list_[it->second];
}

const CountryInfo* CountryTable::byIndex(int index) const {
    auto it = byIndex_.find(index);
    return it == byIndex_.end() ? nullptr : &list_[it->second];
}

bool CountryIndexMap::load(const fs::path& png) {
    SDL_Surface* s = IMG_Load(Paths::toUtf8(png).c_str());
    if (!s) return false;
    SDL_Surface* rgb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!rgb) return false;
    width_ = rgb->w;
    height_ = rgb->h;
    data_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_));
    for (int y = 0; y < height_; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(rgb->pixels) + static_cast<size_t>(y) * static_cast<size_t>(rgb->pitch);
        for (int x = 0; x < width_; ++x)
            data_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)] =
                static_cast<uint16_t>(row[x * 4] | (row[x * 4 + 1] << 8));
    }
    SDL_DestroySurface(rgb);
    return true;
}

void CountryIndexMap::setData(int width, int height, std::vector<uint16_t> data) {
    width_ = width;
    height_ = height;
    data_ = std::move(data);
}

int CountryIndexMap::indexAt(GeoPoint p) const {
    if (width_ <= 0) return 0;
    int x = static_cast<int>((wrapLon(p.lon) + 180.0) / 360.0 * width_);
    int y = static_cast<int>((90.0 - std::clamp(p.lat, -90.0, 90.0)) / 180.0 * height_);
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);
    return data_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)];
}

}  // namespace cosmic
