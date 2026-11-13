#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cosmic {

/// Network failure (no HTTP status could be obtained).
enum class NetError { None, Timeout, Dns, Connect, Tls, Cancelled, TooLarge, Unsupported, Other };
const char* netErrorLabel(NetError e);

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeoutMs = 10000;
    size_t maxResponseBytes = 8 * 1024 * 1024;
    /// Set to true from any thread to abort the transfer as soon as possible.
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
};

struct HttpResponse {
    int status = 0;  // 0 when `error` is set
    std::map<std::string, std::string> headers;  // lower-case names
    std::string body;
    NetError error = NetError::None;
    std::string errorMessage;
    bool networkFailure() const { return error != NetError::None; }
    std::string header(const std::string& lowerName) const {
        auto it = headers.find(lowerName);
        return it == headers.end() ? std::string() : it->second;
    }
};

/// Blocking HTTP client used only by the network worker thread (never by the render loop).
/// Implementations: WinHTTP (Windows), libcurl (Linux / macOS). Builds without an HTTP
/// library get a transport that always reports NetError::Unsupported, so the game keeps
/// working offline.
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse perform(const HttpRequest& request) = 0;
    virtual const char* name() const = 0;
};

std::unique_ptr<IHttpTransport> makePlatformTransport();

/// Splits an absolute http(s) URL. Returns false for anything else.
struct ParsedUrl {
    bool https = false;
    std::string host;
    int port = 0;
    std::string pathAndQuery = "/";
};
bool parseHttpUrl(const std::string& url, ParsedUrl& out);

/// Plain HTTP is only allowed towards the local machine (development server).
bool isLoopbackHost(const std::string& host);

/// Parses raw "Name: value\r\n" header lines into lower-case names.
void parseHeaderLines(const std::string& raw, std::map<std::string, std::string>& out);

}  // namespace cosmic
