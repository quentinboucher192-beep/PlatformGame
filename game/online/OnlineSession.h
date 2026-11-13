#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

/// Server address of Cosmic Online. Resolution order: `--online-server` / environment
/// variable COSMIC_ONLINE_URL, the advanced setting, then data/online/config.json
/// (environment "development", "staging" or "production"). Only HTTPS addresses are
/// accepted, except the local development server (http://127.0.0.1).
struct OnlineConfig {
    std::string environment = "production";
    std::string serverUrl;       // without trailing slash, empty = not configured
    std::string tosVersion = "2026-11";
    std::string source;          // where the address came from (shown in the settings)

    static OnlineConfig load(const std::string& commandLineOverride, const std::string& settingsOverride);
    /// Validates and normalises an address; empty string if refused.
    static std::string sanitizeUrl(const std::string& url);
};

/// Tokens of the current session. The access token lives only in memory; the refresh
/// token is saved (if "connexion automatique" is on) protected by DPAPI on Windows and
/// in a file readable only by the user elsewhere.
struct SessionTokens {
    std::string accessToken;
    std::chrono::system_clock::time_point accessExpires{};
    std::string refreshToken;
    std::string refreshExpiresIso;
    std::string playerId;
    std::string username;

    bool hasAccess() const { return !accessToken.empty(); }
    /// The access token expires within `marginSeconds`.
    bool accessExpiring(int marginSeconds = 60) const;
    void clear() { *this = SessionTokens{}; }
    /// Fills the tokens from the "session" object of a login / register / refresh answer.
    bool fromApi(const Json& session);
};

class SessionStore {
public:
    explicit SessionStore(fs::path file) : file_(std::move(file)) {}
    bool save(const SessionTokens& t, const std::string& serverUrl) const;
    /// Restores the refresh token saved for this server (nothing for another server).
    std::optional<SessionTokens> load(const std::string& serverUrl) const;
    void erase() const;
    const fs::path& file() const { return file_; }

    /// Exposed for tests: platform protection of a secret (DPAPI on Windows).
    static std::string protect(const std::string& plain);
    static std::optional<std::string> unprotect(const std::string& stored);

private:
    fs::path file_;
};

/// Parses "2026-11-13T10:30:00.000Z" (UTC). Returns nullopt if malformed.
std::optional<std::chrono::system_clock::time_point> parseIsoUtc(const std::string& s);

}  // namespace cosmic
