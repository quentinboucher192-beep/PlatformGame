// libcurl transport (Linux / macOS builds).

#include "game/online/HttpTransport.h"

#if defined(COSMIC_HTTP_CURL)

#include <curl/curl.h>

#include <mutex>

namespace cosmic {

namespace {

struct Transfer {
    HttpResponse* response;
    const HttpRequest* request;
    std::string rawHeaders;
    bool tooLarge = false;
};

size_t onBody(char* data, size_t size, size_t n, void* user) {
    auto* t = static_cast<Transfer*>(user);
    size_t bytes = size * n;
    if (t->response->body.size() + bytes > t->request->maxResponseBytes) {
        t->tooLarge = true;
        return 0;  // aborts the transfer
    }
    t->response->body.append(data, bytes);
    return bytes;
}

size_t onHeader(char* data, size_t size, size_t n, void* user) {
    auto* t = static_cast<Transfer*>(user);
    size_t bytes = size * n;
    if (t->rawHeaders.size() + bytes < 64 * 1024) t->rawHeaders.append(data, bytes);
    return bytes;
}

int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* t = static_cast<Transfer*>(user);
    return t->request->cancel && t->request->cancel->load() ? 1 : 0;
}

class CurlTransport final : public IHttpTransport {
public:
    CurlTransport() {
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    const char* name() const override { return "libcurl"; }

    HttpResponse perform(const HttpRequest& rq) override {
        HttpResponse res;
        ParsedUrl url;
        if (!parseHttpUrl(rq.url, url) || (!url.https && !isLoopbackHost(url.host))) {
            res.error = NetError::Other;
            res.errorMessage = "URL refusée (HTTPS obligatoire hors machine locale)";
            return res;
        }
        CURL* h = curl_easy_init();
        if (!h) {
            res.error = NetError::Other;
            res.errorMessage = "initialisation de libcurl impossible";
            return res;
        }
        Transfer t{&res, &rq, {}, false};
        curl_slist* headers = nullptr;
        for (const auto& [k, v] : rq.headers) headers = curl_slist_append(headers, (k + ": " + v).c_str());
        // No "Expect: 100-continue" round trip for uploads.
        headers = curl_slist_append(headers, "Expect:");
        curl_easy_setopt(h, CURLOPT_URL, rq.url.c_str());
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(h, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1");
        curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(rq.timeoutMs));
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min(rq.timeoutMs, 6000)));
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(h, CURLOPT_USERAGENT, "CosmicPlatformer/" COSMIC_VERSION_STRING);
        curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, onBody);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, &t);
        curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, onHeader);
        curl_easy_setopt(h, CURLOPT_HEADERDATA, &t);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, onProgress);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, &t);
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        if (rq.method == "GET") {
            curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
        } else {
            curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, rq.method.c_str());
            if (!rq.body.empty() || rq.method != "DELETE") {
                curl_easy_setopt(h, CURLOPT_POSTFIELDS, rq.body.data());
                curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(rq.body.size()));
            }
        }
        CURLcode rc = curl_easy_perform(h);
        long status = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(h);
        if (rc != CURLE_OK) {
            res.status = 0;
            res.body.clear();
            switch (rc) {
                case CURLE_OPERATION_TIMEDOUT: res.error = NetError::Timeout; break;
                case CURLE_COULDNT_RESOLVE_HOST:
                case CURLE_COULDNT_RESOLVE_PROXY: res.error = NetError::Dns; break;
                case CURLE_COULDNT_CONNECT: res.error = NetError::Connect; break;
                case CURLE_SSL_CONNECT_ERROR:
                case CURLE_PEER_FAILED_VERIFICATION:
                case CURLE_SSL_CERTPROBLEM:
                case CURLE_SSL_CIPHER:
                case CURLE_SSL_CACERT_BADFILE: res.error = NetError::Tls; break;
                case CURLE_ABORTED_BY_CALLBACK: res.error = NetError::Cancelled; break;
                case CURLE_WRITE_ERROR: res.error = t.tooLarge ? NetError::TooLarge : NetError::Other; break;
                default: res.error = NetError::Other; break;
            }
            res.errorMessage = curl_easy_strerror(rc);
            return res;
        }
        res.status = static_cast<int>(status);
        // Keep only the headers of the final response block.
        size_t last = t.rawHeaders.rfind("HTTP/");
        parseHeaderLines(last == std::string::npos ? t.rawHeaders : t.rawHeaders.substr(last), res.headers);
        return res;
    }
};

}  // namespace

std::unique_ptr<IHttpTransport> makePlatformTransport() { return std::make_unique<CurlTransport>(); }

}  // namespace cosmic

#endif
