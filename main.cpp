#include "engine/core/Application.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

int main(int argc, char** argv) {
    cosmic::Paths::init();
    cosmic::Log::init(cosmic::Paths::toUtf8(cosmic::Paths::logFile()));
    cosmic::Application app;
    cosmic::AppConfig cfg;
    if (!app.init(cfg)) return 1;
    app.run();
    app.shutdown();
    return 0;
}
