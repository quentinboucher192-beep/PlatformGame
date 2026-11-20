#include "game/raid/Aftermath.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace cosmic::aftermath {

std::optional<int64_t> parseIsoMs(const std::string& s) {
    int y, mo, d, h, mi, sec;
    if (s.size() < 19 || std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6) return std::nullopt;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return std::nullopt;
    int ms = 0;
    if (s.size() > 20 && s[19] == '.') {  // the fraction of a second: its first three digits
        int scale = 100;
        for (size_t i = 20; i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) && scale > 0; ++i, scale /= 10) ms += (s[i] - '0') * scale;
    }
    // days_from_civil (Howard Hinnant), UTC without time zone libraries.
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
    return (days * 86400 + h * 3600 + mi * 60 + sec) * 1000 + ms;
}

std::optional<Party> partyFromJson(const Json& j, const std::string& eventId) {
    if (!j.is_object()) return std::nullopt;
    Party p;
    p.eventId = eventId;
    const auto film = parseIsoMs(jstr(j, "film_at"));
    if (!film) return std::nullopt;  // never a time of our own: the server's or nothing
    p.filmAtMs = *film;
    p.fallenAtMs = parseIsoMs(jstr(j, "fallen_at")).value_or(*film - kDelayMs);
    if (auto it = j.find("clans"); it != j.end() && it->is_array())
        for (const auto& c : *it) {
            if (!c.is_object()) continue;
            Clan k{jstr(c, "id"), jstr(c, "tag"), jstr(c, "name")};
            if (k.tag.empty() && k.name.empty()) continue;
            p.clans.push_back(std::move(k));
            if (p.clans.size() >= 200) break;
        }
    p.myClanId = jstr(j, "my_clan");
    p.tookPart = jget<bool>(j, "took_part", false);
    p.official = jget<bool>(j, "official", false);
    p.unlocked = jget<bool>(j, "unlocked", p.tookPart || p.official);
    return p;
}

int64_t skewMs(const Json& answer, int64_t localNowMs) {
    const auto t = parseIsoMs(jstr(answer, "server_time"));
    return t ? *t - localNowMs : 0;
}

double positionAt(int64_t serverNowMs, int64_t filmAtMs) { return static_cast<double>(serverNowMs - filmAtMs) / 1000.0; }

bool liveAt(int64_t serverNowMs, int64_t filmAtMs, double lengthSeconds, double aheadSeconds) {
    const double at = positionAt(serverNowMs, filmAtMs);
    return std::isfinite(at) && at >= -aheadSeconds && at < lengthSeconds;
}

std::vector<std::string> rosterNames(const Party& p, int* own) {
    std::vector<std::string> out;
    int mine = -1;
    for (const Clan& c : p.clans) {
        if (!p.myClanId.empty() && c.id == p.myClanId) mine = static_cast<int>(out.size());
        out.push_back(c.tag.empty() ? c.name : c.name.empty() ? "[" + c.tag + "]" : "[" + c.tag + "] " + c.name);
    }
    if (own) *own = mine;
    return out;
}

}  // namespace cosmic::aftermath
