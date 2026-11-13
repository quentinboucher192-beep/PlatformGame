#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "engine/core/Json.h"
#include "game/online/HttpTransport.h"

namespace cosmic {

/// Connection state shown in the interface.
enum class OnlineState {
    Disabled,       // online features turned off in the settings
    NotConfigured,  // no server address for this build / environment
    Unsupported,    // compiled without an HTTP client
    Idle,           // never contacted yet (nothing is sent before the player asks)
    Connecting,
    Online,
    Offline,        // last request failed at the network level
};
const char* onlineStateLabel(OnlineState s);

/// Leaderboard categories (see docs/online/RULES.md).
enum class RunCategory { AnyPercent, HundredPercent, NoDeath, NoDamage, Speedrun };
constexpr std::array<RunCategory, 5> kAllCategories = {RunCategory::AnyPercent, RunCategory::HundredPercent, RunCategory::NoDeath,
                                                       RunCategory::NoDamage, RunCategory::Speedrun};
const char* categoryId(RunCategory c);     // "ANY_PERCENT"
const char* categoryLabel(RunCategory c);  // "ANY%"
const char* categoryRule(RunCategory c);   // French explanation
bool categoryFromId(const std::string& id, RunCategory& out);

/// Status of a performance, from the player's point of view.
enum class RecordStatus { Local, Pending, Verified, Rejected };
const char* recordStatusId(RecordStatus s);     // "LOCAL"
const char* recordStatusLabel(RecordStatus s);  // "LOCAL" / "EN ATTENTE" ...
RecordStatus recordStatusFromServer(const std::string& s);

/// Result of an API call, delivered on the main thread.
struct ApiResult {
    bool ok = false;
    int status = 0;          // HTTP status, 0 on network failure
    NetError net = NetError::None;
    std::string code;        // API error code ("VALIDATION_ERROR", ...) or "NETWORK"
    std::string message;     // human readable (French)
    Json data;               // "data" of the envelope
    Json meta;               // "meta" of the envelope (pagination)
    Json details;            // "error.details"
    bool fromCache = false;  // served from the local cache because the network failed
    double cacheAgeSeconds = 0.0;

    bool networkFailure() const { return net != NetError::None; }
};
using ApiCallback = std::function<void(const ApiResult&)>;

/// Parses the homogeneous JSON envelope of the API ({ok, data, meta} or {ok:false, error}).
/// A body that is not such an envelope is reported as MALFORMED_RESPONSE, never as success.
ApiResult parseApiResponse(const HttpResponse& r);

/// "1:02.345" for a time in milliseconds.
std::string formatRunTime(int64_t ms);

namespace online {
/// Percent-encoding of a query string value.
std::string encodeQueryValue(const std::string& v);
}  // namespace online

}  // namespace cosmic
