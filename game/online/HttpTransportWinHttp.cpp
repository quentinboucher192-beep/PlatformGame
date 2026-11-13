// WinHTTP transport (Windows builds): part of the operating system, no extra DLL to ship.

#include "game/online/HttpTransport.h"

#if defined(COSMIC_HTTP_WINHTTP)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>

namespace cosmic {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w, size_t len) {
    if (len == 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), s.data(), n, nullptr, nullptr);
    return s;
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

NetError mapError(DWORD e) {
    switch (e) {
        case ERROR_WINHTTP_TIMEOUT: return NetError::Timeout;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return NetError::Dns;
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR: return NetError::Connect;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA: return NetError::Tls;
        case ERROR_WINHTTP_OPERATION_CANCELLED: return NetError::Cancelled;
        default: return NetError::Other;
    }
}

class WinHttpTransport final : public IHttpTransport {
public:
    WinHttpTransport() {
        session_ = WinHttpOpen(L"CosmicPlatformer/" COSMIC_VERSION_STRING, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        // Windows < 8.1 does not know AUTOMATIC_PROXY.
        if (!session_)
            session_ = WinHttpOpen(L"CosmicPlatformer/" COSMIC_VERSION_STRING, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_) {
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
                protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
            }
        }
    }
    ~WinHttpTransport() override {
        if (session_) WinHttpCloseHandle(session_);
    }

    const char* name() const override { return "WinHTTP"; }

    HttpResponse perform(const HttpRequest& rq) override {
        HttpResponse res;
        auto fail = [&](NetError e, const std::string& msg) {
            res.status = 0;
            res.body.clear();
            res.error = rq.cancel && rq.cancel->load() ? NetError::Cancelled : e;
            res.errorMessage = msg;
            return res;
        };
        if (!session_) return fail(NetError::Other, "WinHTTP indisponible");
        ParsedUrl url;
        if (!parseHttpUrl(rq.url, url) || (!url.https && !isLoopbackHost(url.host)))
            return fail(NetError::Other, "URL refusée (HTTPS obligatoire hors machine locale)");
        std::string host = url.host;
        if (host.size() > 2 && host.front() == '[') host = host.substr(1, host.size() - 2);
        Handle connect(WinHttpConnect(session_, widen(host).c_str(), static_cast<INTERNET_PORT>(url.port), 0));
        if (!connect.h) return fail(mapError(GetLastError()), "connexion impossible");
        std::wstring method = widen(rq.method);
        std::wstring path = widen(url.pathAndQuery);
        Handle request(WinHttpOpenRequest(connect.h, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, url.https ? WINHTTP_FLAG_SECURE : 0));
        if (!request.h) return fail(mapError(GetLastError()), "requête impossible");
        int connectTimeout = std::min(rq.timeoutMs, 6000);
        WinHttpSetTimeouts(request.h, connectTimeout, connectTimeout, rq.timeoutMs, rq.timeoutMs);
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        if (isLoopbackHost(url.host)) {
            // The local development server is never reached through a proxy.
            WINHTTP_PROXY_INFO direct{WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr};
            WinHttpSetOption(request.h, WINHTTP_OPTION_PROXY, &direct, sizeof(direct));
        }
        std::wstring headers;
        for (const auto& [k, v] : rq.headers) headers += widen(k + ": " + v + "\r\n");
        if (rq.cancel && rq.cancel->load()) return fail(NetError::Cancelled, "annulée");
        BOOL sent = WinHttpSendRequest(request.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                       headers.empty() ? 0 : static_cast<DWORD>(-1L),
                                       rq.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(rq.body.data()),
                                       static_cast<DWORD>(rq.body.size()), static_cast<DWORD>(rq.body.size()), 0);
        if (!sent || !WinHttpReceiveResponse(request.h, nullptr)) return fail(mapError(GetLastError()), "échange impossible");
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                            &size, WINHTTP_NO_HEADER_INDEX);
        DWORD hdrBytes = 0;
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &hdrBytes,
                            WINHTTP_NO_HEADER_INDEX);
        if (hdrBytes > 0 && hdrBytes < 128 * 1024) {
            std::wstring raw(hdrBytes / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &hdrBytes,
                                    WINHTTP_NO_HEADER_INDEX))
                parseHeaderLines(narrow(raw.data(), hdrBytes / sizeof(wchar_t)), res.headers);
        }
        for (;;) {
            if (rq.cancel && rq.cancel->load()) return fail(NetError::Cancelled, "annulée");
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request.h, &avail)) return fail(mapError(GetLastError()), "lecture impossible");
            if (avail == 0) break;
            if (res.body.size() + avail > rq.maxResponseBytes) return fail(NetError::TooLarge, "réponse trop volumineuse");
            size_t at = res.body.size();
            res.body.resize(at + avail);
            DWORD read = 0;
            if (!WinHttpReadData(request.h, res.body.data() + at, avail, &read)) return fail(mapError(GetLastError()), "lecture impossible");
            res.body.resize(at + read);
        }
        res.status = static_cast<int>(status);
        return res;
    }

private:
    HINTERNET session_ = nullptr;
};

}  // namespace

std::unique_ptr<IHttpTransport> makePlatformTransport() { return std::make_unique<WinHttpTransport>(); }

}  // namespace cosmic

#endif
