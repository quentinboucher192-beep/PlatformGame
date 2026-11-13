// Globe logic: geographic conversions, camera, projection / unprojection, marker layout,
// clustering, picking, sun position and country lookup. Pure maths, no rendering.

#include <chrono>
#include <cmath>

#include "engine/core/Paths.h"
#include "game/online/OnlineGlobe.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
GlobeMarker marker(const std::string& name, double lat, double lon, int rank) {
    GlobeMarker m;
    m.playerName = name;
    m.location = m.display = {lat, lon};
    m.rank = rank;
    return m;
}
}  // namespace

TEST_CASE("globe: latitude / longitude conversions round trip") {
    for (double lat : {-80.0, -45.5, 0.0, 12.3, 46.6, 89.0})
        for (double lon : {-179.0, -98.6, 0.0, 2.4, 138.3, 179.5}) {
            GeoPoint back = unitToGeo(geoToUnit({lat, lon}));
            CHECK_NEAR(back.lat, lat, 1e-9);
            CHECK_NEAR(back.lon, lon, 1e-9);
        }
    CHECK_NEAR(geoAngle({0, 0}, {0, 90}), 90.0, 1e-9);
    CHECK_NEAR(geoAngle({46.6, 2.4}, {46.6, 2.4}), 0.0, 1e-6);
    CHECK_NEAR(wrapLon(190.0), -170.0, 1e-9);
    CHECK_NEAR(wrapLon(-540.0), -180.0, 1e-9);
}

TEST_CASE("globe: the camera centre faces the viewer and projections invert") {
    GlobeCamera cam;
    cam.center = {46.6, 2.4};
    GlobeView view{{500, 400}, 200};
    Projected p = projectGeo(cam, view, {46.6, 2.4});
    CHECK_NEAR(p.screen.x, 500.0, 1e-3);
    CHECK_NEAR(p.screen.y, 400.0, 1e-3);
    CHECK_NEAR(p.depth, 1.0, 1e-9);
    // North is up, east is right.
    CHECK(projectGeo(cam, view, {56.6, 2.4}).screen.y < 400.0f);
    CHECK(projectGeo(cam, view, {46.6, 12.4}).screen.x > 500.0f);
    // The antipode is hidden.
    CHECK(!projectGeo(cam, view, {-46.6, -177.6}).visible());
    for (GeoPoint g : {GeoPoint{40.0, -3.0}, GeoPoint{60.0, 25.0}, GeoPoint{10.0, 20.0}}) {
        Projected q = projectGeo(cam, view, g);
        CHECK(q.visible());
        auto back = unprojectScreen(cam, view, q.screen);
        CHECK(back.has_value());
        CHECK_NEAR(back->lat, g.lat, 1e-3);
        CHECK_NEAR(back->lon, g.lon, 1e-3);
    }
    CHECK(!unprojectScreen(cam, view, {500 + 250, 400}).has_value());  // outside the disk
    // Zoom scales the radius.
    cam.zoom = 2.0;
    Projected z = projectGeo(cam, view, {46.6, 12.4});
    cam.zoom = 1.0;
    Projected o = projectGeo(cam, view, {46.6, 12.4});
    CHECK_NEAR(z.screen.x - 500.0, 2.0 * (o.screen.x - 500.0), 1e-3);
}

TEST_CASE("globe: rotation, zoom limits and flights") {
    GlobeCamera cam;
    cam.center = {0, 170};
    cam.rotate(-20, 0);  // dragging left turns the globe west... the centre moves east
    CHECK_NEAR(cam.center.lon, -170.0, 1e-9);
    cam.rotate(0, 200);
    CHECK(cam.center.lat <= 75.0);
    cam.zoomBy(100.0);
    CHECK_NEAR(cam.zoom, GlobeCamera::kMaxZoom, 1e-9);
    cam.zoomBy(0.0001);
    CHECK_NEAR(cam.zoom, GlobeCamera::kMinZoom, 1e-9);
    // Flight across the antimeridian takes the short way.
    cam.center = {10, 170};
    cam.flyTo({10, -170}, 2.0, 1.0f);
    cam.update(0.5f);
    CHECK(std::abs(cam.center.lon) > 170.0);  // stays near the date line, never via 0
    for (int i = 0; i < 20; ++i) cam.update(0.1f);
    CHECK(!cam.flying());
    CHECK_NEAR(cam.center.lon, -170.0, 1e-6);
    CHECK_NEAR(cam.zoom, 2.0, 1e-6);
}

TEST_CASE("globe: players of one country are fanned out, clustered, picked") {
    GlobeCamera cam;
    cam.center = {46.6, 2.4};
    GlobeView view{{500, 400}, 200};
    std::vector<GlobeMarker> ms = {marker("A", 46.6, 2.4, 2), marker("B", 46.6, 2.4, 7), marker("C", 46.6, 2.4, 1),
                                   marker("D", 31.8, -7.1, 3), marker("E", -46.6, -177.6, 4)};
    layoutMarkers(ms, view.radius(cam), 14.0f);
    CHECK_NEAR(ms[0].display.lat, 46.6, 1e-9);  // first one stays on the centre
    CHECK(geoAngle(ms[1].display, ms[0].display) > 0.5);
    CHECK(geoAngle(ms[2].display, ms[1].display) > 0.5);
    CHECK(geoAngle(ms[1].display, ms[0].display) < 6.0);  // still in the country area
    // Zoomed out: no spreading, the three French players form one cluster (aggregation per
    // country), best rank first; Morocco is a separate cluster; the antipode is not visible.
    layoutMarkers(ms, view.radius(cam), 14.0f, false);
    auto clusters = clusterMarkers(ms, cam, view, 18.0f);
    CHECK(clusters.size() == 2);
    CHECK(clusters[0].members.size() == 3);
    CHECK(clusters[0].bestRank == 1);
    CHECK(clusters[0].members.front() == 2);
    // Zoomed in: each French player has its own marker.
    cam.zoom = 4.0;
    layoutMarkers(ms, view.radius(cam), 14.0f);
    auto fine = clusterMarkers(ms, cam, view, 10.0f);
    int french = 0;
    for (const auto& c : fine) french += static_cast<int>(c.members.size() == 1 && ms[static_cast<size_t>(c.members[0])].location.lon == 2.4);
    CHECK(french == 3);
    // Picking the nearest marker under the pointer.
    int hit = pickCluster(fine, fine[0].screen + Vec2{3, -2}, 12.0f);
    CHECK(hit == 0);
    CHECK(pickCluster(fine, {5, 5}, 12.0f) == -1);
    // The number of clusters is bounded.
    std::vector<GlobeMarker> many;
    for (int i = 0; i < 2000; ++i) many.push_back(marker("p", -60.0 + (i % 120), -170.0 + (i / 120) * 20.0, i + 1));
    CHECK(clusterMarkers(many, cam, view, 4.0f, 150).size() <= 150);
    CHECK(markerTier(1) == MarkerTier::Gold && markerTier(2) == MarkerTier::Silver && markerTier(3) == MarkerTier::Bronze &&
          markerTier(4) == MarkerTier::Cyan);
}

TEST_CASE("globe: markers from the API are validated") {
    Json ok = {{"record_id", "r"}, {"player_name", "Nova"}, {"latitude", 46.6}, {"longitude", 362.4}, {"rank", 1}, {"time_ms", 5000}};
    auto m = GlobeMarker::fromJson(ok);
    CHECK(m.has_value());
    CHECK_NEAR(m->location.lon, 2.4, 1e-9);
    CHECK(!GlobeMarker::fromJson({{"player_name", "Nova"}, {"latitude", "46"}, {"longitude", 2}}).has_value());
    CHECK(!GlobeMarker::fromJson({{"latitude", 1}, {"longitude", 2}}).has_value());
    CHECK(!GlobeMarker::fromJson(Json::array()).has_value());
}

TEST_CASE("globe: the sun is where it should be") {
    using namespace std::chrono;
    // 2026-06-21 12:00 UTC: near the tropic of Cancer and the Greenwich meridian.
    auto june = system_clock::time_point(seconds(1782043200));
    GeoPoint s = subsolarPoint(june);
    CHECK_NEAR(s.lat, 23.4, 1.0);
    CHECK_NEAR(s.lon, 0.0, 2.5);
    // 2026-12-21 18:00 UTC: southern tropic, 90° west.
    auto dec = system_clock::time_point(seconds(1797876000));
    GeoPoint d = subsolarPoint(dec);
    CHECK_NEAR(d.lat, -23.4, 1.0);
    CHECK_NEAR(d.lon, -90.0, 2.5);
}

TEST_CASE("globe: country tables and the hit-test map") {
    CountryTable t;
    CHECK(t.load(Paths::dataDir() / "online" / "country_codes.json", Paths::dataDir() / "online" / "flags.json"));
    const CountryInfo* fr = t.find("FR");
    CHECK(fr && fr->nameFr == "France" && fr->center.has_value());
    CHECK_NEAR(fr->center->lat, 46.5, 1.5);
    CHECK(fr->flagCell > 0);
    CHECK(t.byIndex(fr->index) == fr);
    CountryIndexMap map;
    CHECK(map.load(Paths::assetsDir() / "online" / "earth_countries.png"));
    for (const char* code : {"FR", "US", "JP", "BR", "AU", "ZA", "IN", "CN", "CA", "DE", "GB", "RU"}) {
        const CountryInfo* c = t.find(code);
        CHECK(c && c->center);
        CHECK(map.indexAt(*c->center) == c->index);
    }
    CHECK(map.indexAt({0.0, -30.0}) == 0);  // Atlantic ocean
    CountryIndexMap tiny;
    tiny.setData(4, 2, {0, 1, 2, 3, 4, 5, 6, 7});
    CHECK(tiny.indexAt({45, -180}) == 0);
    CHECK(tiny.indexAt({-45, 179}) == 7);
}
