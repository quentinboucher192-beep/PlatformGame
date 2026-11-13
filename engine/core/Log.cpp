#include "engine/core/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace cosmic {

namespace {
std::mutex g_mutex;
std::ofstream g_file;
std::deque<Log::Entry> g_recent;
LogLevel g_minLevel = LogLevel::Debug;
constexpr size_t kMaxRecent = 64;

const char* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
    return buf;
}
}  // namespace

void Log::init(const std::string& filePath) {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
    g_file.open(filePath, std::ios::out | std::ios::trunc);
}

void Log::shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
}

void Log::setMinLevel(LogLevel level) {
    std::lock_guard lock(g_mutex);
    g_minLevel = level;
}

void Log::write(LogLevel level, std::string_view message) {
    std::lock_guard lock(g_mutex);
    if (level < g_minLevel) return;
    std::string line = std::format("[{}] {} {}", timestamp(), levelName(level), message);
    std::fprintf(stderr, "%s\n", line.c_str());
    if (g_file.is_open()) {
        g_file << line << '\n';
        if (level >= LogLevel::Warning) g_file.flush();
    }
    g_recent.push_back({level, std::string(message)});
    while (g_recent.size() > kMaxRecent) g_recent.pop_front();
}

std::deque<Log::Entry> Log::recent() {
    std::lock_guard lock(g_mutex);
    return g_recent;
}

}  // namespace cosmic
