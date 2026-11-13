#include "game/online/HttpTransport.h"

#include <algorithm>
#include <cctype>

namespace cosmic {

const char* netErrorLabel(NetError e) {
    switch (e) {
        case NetError::None: return "aucune";
        case NetError::Timeout: return "délai dépassé";
        case NetError::Dns: return "serveur introuvable (DNS)";
        case NetError::Connect: return "connexion impossible";
        case NetError::Tls: return "connexion sécurisée refusée (TLS)";
        case NetError::Cancelled: return "annulée";
        case NetError::TooLarge: return "réponse trop volumineuse";
        case NetError::Unsupported: return "réseau non disponible dans cette version";
        case NetError::Other: return "erreur réseau";
    }
    return "erreur réseau";
}

bool isLoopbackHost(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]";
}

bool parseHttpUrl(const std::string& url, ParsedUrl& out) {
    std::string rest;
    if (url.rfind("https://", 0) == 0) {
        out.https = true;
        rest = url.substr(8);
    } else if (url.rfind("http://", 0) == 0) {
        out.https = false;
        rest = url.substr(7);
    } else {
        return false;
    }
    size_t slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    out.pathAndQuery = slash == std::string::npos ? "/" : rest.substr(slash);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;  // no credentials in URLs
    out.port = out.https ? 443 : 80;
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        std::string p = authority.substr(colon + 1);
        if (p.empty() || p.size() > 5 || !std::all_of(p.begin(), p.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            return false;
        out.port = std::stoi(p);
        if (out.port <= 0 || out.port > 65535) return false;
        authority = authority.substr(0, colon);
    }
    out.host = authority;
    for (char c : out.host)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']')) return false;
    return !out.host.empty();
}

void parseHeaderLines(const std::string& raw, std::map<std::string, std::string>& out) {
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t end = raw.find('\n', pos);
        if (end == std::string::npos) end = raw.size();
        std::string line = raw.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        size_t v = colon + 1;
        while (v < line.size() && (line[v] == ' ' || line[v] == '\t')) ++v;
        out[name] = line.substr(v);
    }
}

}  // namespace cosmic
