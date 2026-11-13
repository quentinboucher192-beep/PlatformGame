#pragma once

#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace cosmic {

enum class LogLevel { Debug, Info, Warning, Error };

/// Thread-safe logger writing to stderr, to a log file in the user folder and to an
/// in-memory ring buffer displayed by the debug overlay.
class Log {
public:
    static void init(const std::string& filePath);
    static void shutdown();
    static void write(LogLevel level, std::string_view message);
    static void setMinLevel(LogLevel level);

    struct Entry {
        LogLevel level;
        std::string text;
    };
    /// Copy of the most recent entries (newest last).
    static std::deque<Entry> recent();

    template <typename... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void warn(std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }
};

}  // namespace cosmic
