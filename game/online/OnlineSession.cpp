#include "game/online/OnlineSession.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/online/HttpTransport.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cosmic {

// --------------------------------------------------------------------- config

std::string OnlineConfig::sanitizeUrl(const std::string& raw) {
    std::string url = raw;
    while (!url.empty() && (url.back() == '/' || url.back() == ' ')) url.pop_back();
    while (!url.empty() && url.front() == ' ') url.erase(url.begin());
    ParsedUrl p;
    if (!parseHttpUrl(url, p)) return {};
    if (!p.https && !isLoopbackHost(p.host)) return {};
    if (p.pathAndQuery != "/" && !p.pathAndQuery.empty()) return {};  // the API lives at the root
    return url;
}

OnlineConfig OnlineConfig::load(const std::string& commandLineOverride, const std::string& settingsOverride) {
    OnlineConfig c;
    Json j = Json::object();
    fs::path file = Paths::dataDir() / "online" / "config.json";
    if (auto text = FileUtil::readText(file)) {
        Json parsed = Json::parse(*text, nullptr, false);
        if (parsed.is_object()) j = parsed;
    }
    c.environment = jstr(j, "environment", "production");
    c.tosVersion = jstr(j, "tos_version", c.tosVersion);
    const Json servers = j.contains("servers") && j["servers"].is_object() ? j["servers"] : Json::object();
    const char* env = std::getenv("COSMIC_ONLINE_URL");
    if (!commandLineOverride.empty()) {
        c.serverUrl = sanitizeUrl(commandLineOverride);
        c.source = "ligne de commande";
    } else if (env && *env) {
        c.serverUrl = sanitizeUrl(env);
        c.source = "variable COSMIC_ONLINE_URL";
    } else if (!settingsOverride.empty()) {
        c.serverUrl = sanitizeUrl(settingsOverride);
        c.source = "paramètres";
    } else {
        if (const char* e = std::getenv("COSMIC_ONLINE_ENV"); e && *e) c.environment = e;
        c.serverUrl = sanitizeUrl(jstr(servers, c.environment.c_str()));
        c.source = "config.json (" + c.environment + ")";
    }
    return c;
}

// ------------------------------------------------------------------- tokens

std::optional<std::chrono::system_clock::time_point> parseIsoUtc(const std::string& s) {
    int y, mo, d, h, mi, sec;
    if (s.size() < 19 || std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6) return std::nullopt;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return std::nullopt;
    // days_from_civil (Howard Hinnant), UTC without time zone libraries.
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;
    long long secs = days * 86400 + h * 3600 + mi * 60 + sec;
    return std::chrono::system_clock::time_point(std::chrono::seconds(secs));
}

bool SessionTokens::accessExpiring(int marginSeconds) const {
    return accessToken.empty() || std::chrono::system_clock::now() + std::chrono::seconds(marginSeconds) >= accessExpires;
}

bool SessionTokens::fromApi(const Json& s) {
    std::string access = jstr(s, "access_token");
    std::string refresh = jstr(s, "refresh_token");
    auto exp = parseIsoUtc(jstr(s, "access_expires_at"));
    if (access.empty() || refresh.empty() || !exp) return false;
    accessToken = access;
    refreshToken = refresh;
    accessExpires = *exp;
    refreshExpiresIso = jstr(s, "refresh_expires_at");
    return true;
}

// -------------------------------------------------------------------- store

namespace {
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::string& in) {
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(kB64[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(kB64[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::optional<std::string> unbase64(const std::string& in) {
    std::string out;
    int val = 0, bits = -8;
    for (char c : in) {
        if (c == '=') break;
        const char* p = std::strchr(kB64, c);
        if (!p || c == 0) return std::nullopt;
        val = (val << 6) + static_cast<int>(p - kB64);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}
}  // namespace

std::string SessionStore::protect(const std::string& plain) {
#if defined(_WIN32)
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (CryptProtectData(&in, L"Cosmic Online", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        std::string blob(reinterpret_cast<char*>(out.pbData), out.cbData);
        LocalFree(out.pbData);
        return "dpapi:" + base64(blob);
    }
    return {};  // never store the secret unprotected on Windows
#else
    return "plain:" + base64(plain);
#endif
}

std::optional<std::string> SessionStore::unprotect(const std::string& stored) {
#if defined(_WIN32)
    if (stored.rfind("dpapi:", 0) != 0) return std::nullopt;
    auto blob = unbase64(stored.substr(6));
    if (!blob) return std::nullopt;
    DATA_BLOB in{static_cast<DWORD>(blob->size()), reinterpret_cast<BYTE*>(blob->data())};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string plain(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
#else
    if (stored.rfind("plain:", 0) != 0) return std::nullopt;
    return unbase64(stored.substr(6));
#endif
}

bool SessionStore::save(const SessionTokens& t, const std::string& serverUrl) const {
    std::string secret = protect(t.refreshToken);
    if (secret.empty()) return false;
    Json j = {{"format", "cosmic.online_session"}, {"version", 1},      {"server", serverUrl},
              {"player_id", t.playerId},        {"username", t.username}, {"refresh_expires_at", t.refreshExpiresIso},
              {"refresh_token", secret}};
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
#if !defined(_WIN32)
    // The file is created with owner-only permissions before the secret is written.
    fs::path tmp = file_;
    tmp += ".tmp";
    std::string content = j.dump(2);
    int fd = ::open(Paths::toUtf8(tmp).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = ::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size());
    ::close(fd);
    if (!ok) return false;
    fs::rename(tmp, file_, ec);
    return !ec;
#else
    return FileUtil::writeTextAtomic(file_, j.dump(2));
#endif
}

std::optional<SessionTokens> SessionStore::load(const std::string& serverUrl) const {
    auto text = FileUtil::readText(file_);
    if (!text) return std::nullopt;
    Json j = Json::parse(*text, nullptr, false);
    if (!j.is_object() || jstr(j, "format") != "cosmic.online_session" || jstr(j, "server") != serverUrl) return std::nullopt;
    auto refresh = unprotect(jstr(j, "refresh_token"));
    if (!refresh || refresh->empty()) return std::nullopt;
    if (auto exp = parseIsoUtc(jstr(j, "refresh_expires_at")); exp && *exp <= std::chrono::system_clock::now()) return std::nullopt;
    SessionTokens t;
    t.refreshToken = *refresh;
    t.refreshExpiresIso = jstr(j, "refresh_expires_at");
    t.playerId = jstr(j, "player_id");
    t.username = jstr(j, "username");
    return t;
}

void SessionStore::erase() const {
    std::error_code ec;
    fs::remove(file_, ec);
}

}  // namespace cosmic
