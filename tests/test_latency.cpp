// 2.72 « En ligne - 25ms »: the round trip to Cosmic Online shown next to the connection's dot.
// The transport times each exchange on the network thread (HttpResponse::roundTripMs); only the
// light requests (health check, presence heartbeat) are samples; the figure is the median of the
// last five; nothing is shown before a first sample, going offline forgets them, and the other
// states (« Hors ligne », « Connexion… ») keep their label. The text is measured into its slot.

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/LatencyMeter.h"
#include "game/online/OnlineService.h"
#include "tests/TestFramework.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;
namespace fs = std::filesystem;

TEST_CASE("latency: the median of the last five round trips, never a guess") {
    LatencyMeter m;
    CHECK(m.empty());
    CHECK(m.ms() == -1);  // no sample: no figure
    m.add(25.4);
    CHECK(m.ms() == 25);
    m.add(31.0);
    CHECK(m.ms() == 28);  // two samples: their middle
    for (double v : {400.0, 27.0, 29.0}) m.add(v);
    CHECK(m.size() == 5);
    CHECK(m.ms() == 29);  // one slow exchange (a new connection) does not make it jump
    m.add(24.0);          // the oldest (25.4) goes
    CHECK(m.size() == LatencyMeter::kSamples);
    CHECK(m.ms() == 29);  // 24, 27, 29, 31, 400
    m.add(23.0);
    m.add(22.0);
    CHECK(m.ms() == 24);  // 22, 23, 24, 27, 29 (31 and 400 gone)
    // Refused: not a measure.
    m.clear();
    for (double v : {-1.0, 60001.0, std::nan(""), std::numeric_limits<double>::infinity()}) m.add(v);
    CHECK(m.empty());
    m.add(0.4);
    CHECK(m.ms() == 0);
    // The samples: the light requests only.
    CHECK(LatencyMeter::probe("/api/v1/health"));
    CHECK(LatencyMeter::probe("/api/v1/presence"));
    CHECK(!LatencyMeter::probe("/api/v1/clans/me"));
    CHECK(!LatencyMeter::probe("/api/v1/health?x=1"));
    CHECK(!LatencyMeter::probe("/api/v1/records"));

    // The label: the figure only when online with a sample; the other states unchanged.
    CHECK(connectionLabel(OnlineState::Online, 25) == "En ligne - 25ms");
    CHECK(connectionLabel(OnlineState::Online, 0) == "En ligne - 0ms");
    CHECK(connectionLabel(OnlineState::Online, -1) == "En ligne");
    CHECK(connectionLabel(OnlineState::Offline, 25) == "Hors ligne");
    CHECK(connectionLabel(OnlineState::Connecting, 25) == "Connexion…");
    CHECK(connectionLabel(OnlineState::Disabled, 25) == "Désactivé");
}

namespace {

/// A server answering on the network thread, each answer with the round trip it is given.
struct TimedServer {
    std::mutex m;
    bool down = false;
    double next = 25.0;
    HttpResponse handle(const HttpRequest& rq) {
        std::lock_guard<std::mutex> lock(m);
        HttpResponse r;
        if (down) {
            r.error = NetError::Connect;
            r.errorMessage = "connexion impossible";
            return r;
        }
        r.status = 200;
        r.headers["content-type"] = "application/json";
        r.body = Json{{"ok", true}, {"data", Json{{"status", "ok"}}}, {"request_id", "t"}}.dump();
        // Any other request (a list, heavy for the server) answers slowly: never a sample.
        r.roundTripMs = rq.url.find("/api/v1/health") != std::string::npos ? next : 900.0;
        return r;
    }
};

class TimedTransport final : public IHttpTransport {
public:
    explicit TimedTransport(std::shared_ptr<TimedServer> s) : s_(std::move(s)) {}
    HttpResponse perform(const HttpRequest& rq) override { return s_->handle(rq); }
    const char* name() const override { return "timed"; }

private:
    std::shared_ptr<TimedServer> s_;
};

bool waitFor(OnlineService& svc, const std::atomic<bool>& done) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < end) {
        svc.update(0.016f);
        if (done.load()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("latency: OnlineService keeps the round trips of its light requests, forgets them offline") {
    auto server = std::make_shared<TimedServer>();
    const fs::path dir = fs::temp_directory_path() / ("cosmic_latency_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    {
        OnlineService svc;
        svc.init(OnlineSettings{}, "http://127.0.0.1:9", std::make_unique<TimedTransport>(server), dir);
        CHECK(svc.latencyMs() == -1);  // nothing measured yet: « En ligne » alone
        auto health = [&] {
            std::atomic<bool> done{false};
            svc.checkConnection([&](const ApiResult&) { done = true; });
            CHECK(waitFor(svc, done));
        };
        health();
        CHECK(svc.state() == OnlineState::Online);
        CHECK(svc.latencyMs() == 25);
        // A heavy request is not a sample.
        std::atomic<bool> listed{false};
        svc.get("/api/v1/levels/ratings", [&](const ApiResult&) { listed = true; }, false);
        CHECK(waitFor(svc, listed));
        CHECK(svc.latencyMs() == 25);
        // Smoothed: the median of the last samples.
        for (double v : {31.0, 400.0, 27.0, 29.0}) {
            {
                std::lock_guard<std::mutex> lock(server->m);
                server->next = v;
            }
            health();
        }
        CHECK(svc.latencyMs() == 29);
        // Offline: « Hors ligne », the samples forgotten; back online, a new measure.
        {
            std::lock_guard<std::mutex> lock(server->m);
            server->down = true;
        }
        health();
        CHECK(svc.state() == OnlineState::Offline);
        CHECK(svc.latencyMs() == -1);
        CHECK(connectionLabel(svc.state(), svc.latencyMs()) == "Hors ligne");
        {
            std::lock_guard<std::mutex> lock(server->m);
            server->down = false;
            server->next = 41.0;
        }
        health();
        CHECK(svc.state() == OnlineState::Online);
        CHECK(svc.latencyMs() == 41);
        CHECK(connectionLabel(svc.state(), svc.latencyMs()) == "En ligne - 41ms");
        svc.shutdown();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("latency: « En ligne - 25ms » measured into its slot at the top right (1280×720, French and English)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        app.setWindowSize(1280, 720);
        app.renderer().refreshOutputSize();
        UIContext& ui = g.ui;
        ui.begin(app, 1.0f / 60.0f);
        const float room = OnlineUi::defaultConnectionTextRoom(ui);
        // The 240-pixel slot the screens keep (the dot and its gap, then 12 pixels before the edge).
        CHECK(room > 0.0f && room <= ui.s(240) - ui.s(24) + 0.01f);
        for (Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            if (lang == Language::English) CHECK(i18n::tr("En ligne - 25ms") == "Online - 25ms");
            // The usual figures keep the full size.
            for (const std::string label : {std::string("En ligne"), connectionLabel(OnlineState::Online, 25), connectionLabel(OnlineState::Online, 999),
                                            std::string("Hors ligne")}) {
                const OnlineUi::ConnectionText t = OnlineUi::fitConnectionText(ui, label, room);
                CHECK(t.size == 17.0f);
                CHECK(t.text == label);
                CHECK(ui.text().measure(t.text, FontFace::Body, ui.s(t.size)).x <= room + 0.5f);
            }
            // The longest (syncing, a slow link): smaller or shortened, never past the slot.
            for (float r : {room, room * 0.6f, ui.s(90)}) {
                const std::string label = connectionLabel(OnlineState::Online, 12345) + " · synchronisation…";
                const OnlineUi::ConnectionText t = OnlineUi::fitConnectionText(ui, label, r);
                CHECK(!t.text.empty());
                CHECK(t.size >= 17.0f * 0.75f - 0.01f);
                CHECK(ui.text().measure(t.text, FontFace::Body, ui.s(t.size)).x <= r + 0.5f);
            }
        }
        i18n::setLanguage(Language::French);
    }
    app.shutdown();
}
