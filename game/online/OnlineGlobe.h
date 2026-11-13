#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"

namespace cosmic {

/// Globe logic, independent from rendering: geographic conversions, camera (rotation,
/// zoom, animated flights), orthographic projection to the screen, marker layout,
/// clustering and picking, sun position, country lookup. Coordinates of players are
/// always approximate representation points (country centres) received from the server.

struct GeoPoint {
    double lat = 0.0;  // degrees, north positive
    double lon = 0.0;  // degrees, east positive
};

struct Vec3d {
    double x = 0.0, y = 0.0, z = 0.0;
    double dot(const Vec3d& o) const { return x * o.x + y * o.y + z * o.z; }
};

/// Unit vector: lon 0 / lat 0 points to +z, east to +x, north to +y.
Vec3d geoToUnit(GeoPoint p);
GeoPoint unitToGeo(const Vec3d& v);
/// Great-circle angle between two points (degrees).
double geoAngle(GeoPoint a, GeoPoint b);
/// Wraps a longitude into [-180, 180).
double wrapLon(double lon);

class GlobeCamera {
public:
    static constexpr double kMinZoom = 0.85;
    static constexpr double kMaxZoom = 4.5;

    GeoPoint center{25.0, 10.0};  // point of the globe facing the viewer
    double zoom = 1.0;            // radius multiplier

    /// Rotates by screen-space angles (degrees): dragging right turns the globe east.
    void rotate(double dLonDeg, double dLatDeg);
    void zoomBy(double factor);
    /// Smooth flight to a point (shortest way around), optionally changing the zoom.
    void flyTo(GeoPoint target, double targetZoom, float seconds = 1.2f);
    bool flying() const { return flightT_ < flightDur_; }
    void cancelFlight() { flightT_ = flightDur_; }
    void update(float dt);

    /// World unit vector -> view space (+z towards the viewer).
    Vec3d toView(const Vec3d& world) const;
    Vec3d fromView(const Vec3d& view) const;

private:
    GeoPoint from_{}, to_{};
    double zoomFrom_ = 1.0, zoomTo_ = 1.0;
    float flightT_ = 1.0f, flightDur_ = 1.0f;
};

/// Screen placement of the globe (orthographic projection).
struct GlobeView {
    Vec2 center;
    float baseRadius = 200.0f;  // radius at zoom 1
    float radius(const GlobeCamera& c) const { return baseRadius * static_cast<float>(c.zoom); }
};

struct Projected {
    Vec2 screen;
    double depth = 0.0;  // view-space z: > 0 facing the viewer
    bool visible() const { return depth > 0.0; }
};

Projected projectGeo(const GlobeCamera& cam, const GlobeView& view, GeoPoint p);
/// Screen point -> point of the globe under it (nullopt outside the disk).
std::optional<GeoPoint> unprojectScreen(const GlobeCamera& cam, const GlobeView& view, Vec2 screen);

/// A record placed on the globe (from GET /globe/records).
struct GlobeMarker {
    std::string recordId;
    std::string playerId;
    std::string playerName;
    std::string avatar;
    std::string countryCode;
    std::string levelId;
    std::string achievedAt;
    GeoPoint location;  // approximate (country centre)
    int rank = 0;       // world rank on the board
    int64_t timeMs = 0;
    bool isMe = false;
    /// Display position: players of one country are fanned out around its centre so
    /// that each can be selected when zoomed in (purely visual).
    GeoPoint display;

    static std::optional<GlobeMarker> fromJson(const Json& j);
};

enum class MarkerTier { Gold, Silver, Bronze, Cyan };
MarkerTier markerTier(int rank);

/// Spreads markers sharing a location on a small spiral whose angular size keeps a
/// constant on-screen spacing (`spacingPx`) at the current zoom. With `spread` false
/// (zoomed out) every marker stays on its country centre, so clustering aggregates them
/// per country.
void layoutMarkers(std::vector<GlobeMarker>& markers, float radiusPx, float spacingPx = 14.0f, bool spread = true);

struct GlobeCluster {
    Vec2 screen;
    double depth = 0.0;
    std::vector<int> members;  // indices into the marker list, best rank first
    int bestRank = 0;
    bool single() const { return members.size() == 1; }
};

/// Groups the visible markers closer than `pixelRadius` on screen. At most `maxClusters`
/// clusters are returned (best ranks first) to bound the rendering cost.
std::vector<GlobeCluster> clusterMarkers(const std::vector<GlobeMarker>& markers, const GlobeCamera& cam, const GlobeView& view,
                                         float pixelRadius, size_t maxClusters = 400);
/// Cluster under `screen` (index), -1 if none within `radius` pixels.
int pickCluster(const std::vector<GlobeCluster>& clusters, Vec2 screen, float radius);

/// Sub-solar point at a given time (approximate astronomical formula, ~1°).
GeoPoint subsolarPoint(std::chrono::system_clock::time_point t);

/// Country table (data/online/country_codes.json): names, representation points, index
/// in the hit-test map and flag cell.
struct CountryInfo {
    std::string code;
    std::string nameFr;
    std::string nameEn;
    std::optional<GeoPoint> center;
    int index = 0;
    int flagCell = 0;
};

class CountryTable {
public:
    bool load(const fs::path& codesFile, const fs::path& flagsFile);
    const CountryInfo* find(const std::string& code) const;
    const CountryInfo* byIndex(int index) const;
    const std::vector<CountryInfo>& all() const { return list_; }
    int flagColumns() const { return flagColumns_; }
    int flagCellW() const { return flagW_; }
    int flagCellH() const { return flagH_; }

private:
    std::vector<CountryInfo> list_;
    std::map<std::string, size_t> byCode_;
    std::map<int, size_t> byIndex_;
    int flagColumns_ = 32, flagW_ = 48, flagH_ = 36;
};

/// Hit-test map (assets/online/earth_countries.png): pixel = R + 256 G = country index.
class CountryIndexMap {
public:
    bool load(const fs::path& png);
    bool loaded() const { return width_ > 0; }
    /// 0 = ocean / no country.
    int indexAt(GeoPoint p) const;
    /// Tests only: builds the map from raw indices.
    void setData(int width, int height, std::vector<uint16_t> data);

private:
    int width_ = 0, height_ = 0;
    std::vector<uint16_t> data_;
};

}  // namespace cosmic
