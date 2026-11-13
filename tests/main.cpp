#include <SDL3/SDL.h>

#include <filesystem>

#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "tests/TestFramework.h"

int main(int argc, char** argv) {
    // Isolated user folder so tests never touch real saves.
    auto tmp = std::filesystem::temp_directory_path() / "cosmic_tests_user";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    cosmic::Paths::init(cosmic::Paths::toUtf8(tmp));
    cosmic::Log::setMinLevel(cosmic::LogLevel::Error);

    std::string filter = argc > 1 ? argv[1] : "";
    int passed = 0, failed = 0;
    for (auto& t : cosmic::test::registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        try {
            t.fn();
            ++passed;
            std::cout << "[ OK ] " << t.name << "\n";
        } catch (const cosmic::test::Failure& f) {
            ++failed;
            std::cout << "[FAIL] " << t.name << "\n       " << f.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << t.name << " (exception: " << e.what() << ")\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, " << cosmic::test::checks() << " checks\n";
    std::filesystem::remove_all(tmp, ec);
    SDL_Quit();
    return failed == 0 ? 0 : 1;
}
