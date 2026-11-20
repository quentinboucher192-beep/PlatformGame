#include "game/online/RaidRecap.h"

#include <algorithm>
#include <cmath>

#include "game/online/Raids.h"

namespace cosmic::raids {

namespace {

int64_t timeOf(const Json& j, const char* key) {
    auto t = parseUtc(jstr(j, key));
    return t ? *t : 0;
}

const Json& field(const Json& j, const char* key) {
    static const Json null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}

std::string percent(double share) { return std::to_string(static_cast<int>(std::lround(std::clamp(share, 0.0, 1.0) * 100.0))) + " %"; }

RecapGroup parseGroup(const Json& j) {
    RecapGroup g;
    if (!j.is_object()) return g;
    g.valid = true;
    g.clanTag = jstr(j, "clan_tag");
    g.group = jget<int>(j, "group", 0);
    g.reinforcement = jget<bool>(j, "reinforcement", false);
    g.mine = jget<bool>(j, "mine", false);
    g.me = jget<bool>(j, "me", false);
    return g;
}

void parsePart(const Json& j, RecapPart& p) {
    p.part = jstr(j, "part");
    p.name = jstr(j, "name");
    p.boss = jstr(j, "boss");
    p.fallen = jget<bool>(j, "fallen", false);
    p.at = timeOf(j, "at");
    p.by = parseGroup(field(j, "by"));
    p.relic = jget<bool>(j, "relic", false);
    p.phase = std::clamp(jget<int>(j, "phase", 0), 0, 4);
}

}  // namespace

std::string durationText(int64_t s) {
    if (s < 60) return "moins d'une minute";
    const int64_t h = s / 3600, m = (s % 3600) / 60;
    if (h == 0) return std::to_string(m) + " min";
    return std::to_string(h) + " h " + std::to_string(m) + " min";
}

std::vector<RecapEntry> parseRecapList(const Json& data) {
    std::vector<RecapEntry> out;
    const Json& list = field(data, "raids");
    if (!list.is_array()) return out;
    for (const Json& j : list) {
        RecapEntry e;
        e.id = jstr(j, "id");
        e.kind = jstr(j, "kind", "clan");
        e.format = jstr(j, "format", "standard");
        e.catchup = jget<bool>(j, "catchup", false);
        e.heartFallen = jget<bool>(j, "heart_fallen", false);
        e.final = jget<bool>(j, "final", false);
        e.startsAt = timeOf(j, "starts_at");
        e.endsAt = timeOf(j, "ends_at");
        e.endAt = timeOf(j, "end_at");
        e.durationS = std::max(0, jget<int>(j, "duration_s", 0));
        e.partsDown = std::max(0, jget<int>(j, "parts_down", 0));
        e.partsTotal = std::max(1, jget<int>(j, "parts_total", 7));
        e.valid = !e.id.empty() && e.startsAt > 0;
        if (e.valid) out.push_back(std::move(e));
    }
    return out;
}

std::string recapEntryTitle(const RecapEntry& e) {
    const Civil c = toSystemLocal(e.startsAt);
    return "Asterion · " + std::to_string(c.day) + " " + monthShort(c.month);
}

std::string recapEntryLine(const RecapEntry& e) {
    if (e.heartFallen) return "Asterion vaincu · " + durationText(e.durationS);
    return std::to_string(e.partsDown) + " / " + std::to_string(e.partsTotal) + " parties · temps écoulé";
}

Recap parseRecap(const Json& d) {
    Recap r;
    const Json& clan = field(d, "clan");
    const Json& ev = field(d, "event");
    if (!clan.is_object() || !ev.is_object()) return r;
    r.clanId = jstr(clan, "id");
    r.clanTag = jstr(clan, "tag");
    r.clanName = jstr(clan, "name");
    const Json& em = field(clan, "emblem");
    r.emblemShape = jstr(em, "shape", "orb");
    r.emblemPrimary = jstr(em, "primary", "#ffd27a");
    r.emblemSecondary = jstr(em, "secondary", "#a66bff");
    r.eventId = jstr(ev, "id");
    r.kind = jstr(ev, "kind", "clan");
    r.format = jstr(ev, "format", "standard");
    r.catchup = jget<bool>(ev, "catchup", false);
    r.heartFallen = jget<bool>(ev, "heart_fallen", false);
    r.final = jget<bool>(ev, "final", false);
    r.startsAt = timeOf(ev, "starts_at");
    r.endsAt = timeOf(ev, "ends_at");
    r.endAt = timeOf(ev, "end_at");
    if (r.endAt <= 0) r.endAt = r.endsAt;
    r.durationS = std::max(0, jget<int>(ev, "duration_s", 0));
    r.member = jget<bool>(field(d, "viewer"), "member", true);
    r.rating = raidreviews::parseSummary(field(d, "reviews"));
    const Json& pending = field(d, "pending");
    r.pendingTables = std::max(0, jget<int>(pending, "tables", 0));
    r.pendingObjectives = std::max(0, jget<int>(pending, "objectives", 0));
    const Json& world = field(d, "world");
    r.worldRank = std::max(0, jget<int>(world, "my_clan_rank", 0));
    r.worldClans = std::max(0, jget<int>(world, "clans", 0));
    const Json& people = field(d, "people");
    r.registered = std::max(0, jget<int>(people, "registered", 0));
    r.players = std::max(0, jget<int>(people, "players", 0));
    r.groups = std::max(0, jget<int>(people, "groups", 0));
    r.reinforcements = std::max(0, jget<int>(people, "reinforcements", 0));
    const Json& f = field(d, "figures");
    r.partsDown = std::max(0, jget<int>(f, "parts_down", 0));
    r.partsTotal = std::max(1, jget<int>(f, "parts_total", 7));
    r.zonesDone = std::max(0, jget<int>(f, "zones_done", 0));
    r.zonesTotal = std::max(1, jget<int>(f, "zones_total", 56));
    r.zonesByClan = std::max(0, jget<int>(f, "zones_by_clan", 0));
    r.relics = std::max(0, jget<int>(f, "relics", 0));
    r.relicsTotal = std::max(1, jget<int>(f, "relics_total", 7));
    r.revives = std::max(0, jget<int>(f, "revives", 0));
    r.deaths = jget<int>(f, "deaths", -1);
    r.wiped = std::max(0, jget<int>(f, "wiped", 0));
    r.retaken = std::max(0, jget<int>(f, "retaken", 0));
    const Json& parts = field(d, "parts");
    if (parts.is_array())
        for (const Json& j : parts) {
            RecapPart p;
            parsePart(j, p);
            if (!p.part.empty()) r.parts.push_back(std::move(p));
        }
    const Json& h = field(d, "heart");
    parsePart(h, r.heart);
    if (r.heart.part.empty()) r.heart.part = "coeur";
    r.heart.open = jget<bool>(h, "open", false);
    r.heart.taken = std::clamp(jget<int>(h, "taken", 0), 0, 10000);
    r.heart.groups = std::max(0, jget<int>(h, "groups", 0));
    r.heart.clanGroups = std::max(0, jget<int>(h, "clan_groups", 0));
    r.heart.clanShare = std::clamp(jget<double>(h, "clan_share", 0.0), 0.0, 1.0);
    const Json& best = field(d, "best");
    for (const RecapCategory& c : recapCategories()) {
        RecapBest b;
        b.category = c.id;
        const Json& j = field(best, c.id.c_str());
        b.followed = j.is_object();
        const Json& p = field(j, "player");
        if (p.is_object()) {
            b.has = true;
            b.playerId = jstr(p, "id");
            b.username = jstr(p, "username");
            b.avatar = jstr(p, "avatar", "avatar_01");
            b.me = jget<bool>(p, "me", false);
            b.value = std::max(0, jget<int>(j, "value", 0));
            b.share = std::clamp(jget<double>(j, "share", 0.0), 0.0, 1.0);
        }
        r.best.push_back(std::move(b));
    }
    const Json& rewards = field(d, "rewards");
    if (rewards.is_array())
        for (const Json& j : rewards) {
            RecapReward w;
            w.reward = jstr(j, "reward");
            w.name = jstr(j, "name");
            w.kind = jstr(j, "kind", "item");
            w.ref = jstr(j, "ref");
            w.players = std::max(0, jget<int>(j, "players", 0));
            w.mine = jget<bool>(j, "mine", false);
            w.gigaboss = jget<bool>(j, "gigaboss", false);
            if (!w.reward.empty()) r.rewards.push_back(std::move(w));
        }
    const Json& titles = field(d, "titles");
    if (titles.is_array())
        for (const Json& j : titles) {
            // The titles' names (the clan's world rank is in the header's line).
            const std::string t = jstr(j, "title");
            if (!t.empty()) r.titles.push_back(t);
        }
    const Json& me = field(d, "me");
    r.me.played = jget<bool>(me, "played", false);
    const Json& groups = field(me, "groups");
    if (groups.is_array())
        for (const Json& j : groups) {
            RecapMyGroup g;
            g.clanTag = jstr(j, "clan_tag");
            g.part = jstr(j, "part");
            g.group = jget<int>(j, "group", 0);
            g.reinforcement = jget<bool>(j, "reinforcement", false);
            g.wiped = jget<bool>(j, "wiped", false);
            g.mine = jget<bool>(j, "mine", false);
            r.me.groups.push_back(std::move(g));
        }
    const Json& myParts = field(me, "parts");
    if (myParts.is_array())
        for (const Json& j : myParts)
            if (j.is_string()) r.me.parts.push_back(j.get<std::string>());
    r.me.zones = std::max(0, jget<int>(me, "zones", 0));
    r.me.revives = std::max(0, jget<int>(me, "revives", 0));
    r.me.deaths = jget<int>(me, "deaths", -1);
    r.me.heartShare = std::clamp(jget<double>(me, "heart_share", 0.0), 0.0, 1.0);
    r.me.heartRank = std::max(0, jget<int>(me, "heart_rank", 0));
    r.valid = !r.clanId.empty() && !r.eventId.empty() && r.startsAt > 0;
    return r;
}

bool recapPending(const Recap& r) { return !r.final || r.pendingTables > 0 || r.pendingObjectives > 0; }

std::string recapTitle(const Recap& r) { return r.heartFallen ? "ASTERION EST TOMBÉ" : "RAID TERMINÉ"; }

std::string recapEventLine(const Recap& r) {
    const Civil s = toSystemLocal(r.startsAt), e = toSystemLocal(r.endAt);
    std::string line = (r.kind == "world" ? (r.catchup ? "Session de rattrapage du " : "Événement mondial du ") : std::string("Raid du clan du ")) + dateLong(s);
    line += " · " + formatLabel(r.format) + " · " + hhmm(s) + " → " + hhmm(e);
    line += r.heartFallen ? " · durée " + durationText(r.durationS) : " · temps écoulé (" + durationText(r.durationS) + ")";
    return line;
}

std::string joined(const Pieces& p) {
    std::string out;
    for (const Piece& x : p) out += x.text;
    return out;
}

Pieces recapClanPieces(const Recap& r) {
    const Piece dot{" · ", true};
    Pieces p = {{"Clan", false}, {" [" + r.clanTag + "]", true}, dot, {std::to_string(r.players) + (r.players > 1 ? " joueurs" : " joueur"), false}, dot,
                {std::to_string(r.groups) + (r.groups > 1 ? " groupes" : " groupe"), false}, dot,
                {std::to_string(r.reinforcements) + (r.reinforcements > 1 ? " renforts" : " renfort"), false}};
    if (r.worldRank == 1 && r.worldClans > 0) p.insert(p.end(), {dot, {"1er sur " + std::to_string(r.worldClans) + " clans", false}});
    else if (r.worldRank > 1 && r.worldClans > 0) p.insert(p.end(), {dot, {std::to_string(r.worldRank) + "e sur " + std::to_string(r.worldClans) + " clans", false}});
    return p;
}

std::string recapClanLine(const Recap& r) { return joined(recapClanPieces(r)); }

std::string recapPendingText(const Recap& r) {
    if (!recapPending(r)) return "";
    const int n = r.pendingTables + r.pendingObjectives;
    if (n > 0) return std::to_string(n) + (n > 1 ? " résultats en attente de validation" : " résultat en attente de validation");
    return "Fiche provisoire : en attente de validation";
}

std::vector<RecapFigure> recapFigures(const Recap& r) {
    std::vector<RecapFigure> out;
    out.push_back({"parts", std::to_string(r.partsDown) + " / " + std::to_string(r.partsTotal), "Parties du titan", r.partsDown >= r.partsTotal ? 0 : 3});
    std::string heart = r.heartFallen ? "Vaincu" : !r.heart.open ? "Scellé" : "Phase " + std::to_string(std::max(1, r.heart.phase));
    out.push_back({"heart", heart, "Cœur d'Asterion", r.heartFallen ? 1 : 4});
    out.push_back({"zones", std::to_string(r.zonesDone) + " / " + std::to_string(r.zonesTotal), "Zones terminées", 2});
    out.push_back({"relics", std::to_string(r.relics) + " / " + std::to_string(r.relicsTotal), "Reliques", 3});
    out.push_back({"deaths", r.deaths >= 0 ? groupDigits(r.deaths) : "—", "Morts", 4});
    out.push_back({"revives", groupDigits(r.revives), "Réanimations", 0});
    std::string wiped = std::to_string(r.wiped);
    if (r.wiped > 0) wiped += " · " + std::to_string(r.retaken) + " repris";
    out.push_back({"wiped", wiped, "Groupes éliminés", r.wiped > 0 ? 1 : 0});
    return out;
}

const std::vector<RecapCategory>& recapCategories() {
    static const std::vector<RecapCategory> k = {
        {"gigaboss_damage", "Dégâts aux Gigaboss", 1}, {"heart_damage", "Dégâts au Cœur", 1}, {"support", "Soutien", 0},
        {"revives", "Réanimations faites", 0},          {"mechanisms", "Mécanismes", 3},      {"objectives", "Objectifs", 2},
        {"fewest_deaths", "Le moins de morts", 4},
    };
    return k;
}

std::string recapBestValue(const RecapBest& b) {
    if (!b.followed || !b.has) return "—";
    if (b.category == "gigaboss_damage" || b.category == "heart_damage") return percent(b.share);
    return groupDigits(b.value);
}

std::pair<std::string, std::string> recapPartLabel(const RecapPart& p, bool heart, int heartGroups) {
    if (!p.fallen) {
        if (heart) return p.phase > 0 ? std::pair<std::string, std::string>{"debout", "phase " + std::to_string(p.phase) + " / 4"} : std::pair<std::string, std::string>{"scellé", ""};
        return p.phase > 0 ? std::pair<std::string, std::string>{"debout", "phase " + std::to_string(std::min(3, p.phase)) + " / 3"}
                           : std::pair<std::string, std::string>{"jamais affronté", ""};
    }
    const std::string when = p.at > 0 ? hhmm(toSystemLocal(p.at)) : std::string("—");
    return {when, joined(recapWhoPieces(p, heart, heartGroups))};
}

Pieces recapWhoPieces(const RecapPart& p, bool heart, int heartGroups) {
    if (!p.fallen) return {{recapPartLabel(p, heart, heartGroups).second, false}};
    if (heart) return {{std::to_string(heartGroups) + (heartGroups > 1 ? " groupes" : " groupe"), false}};
    if (!p.by.valid) return {};
    Pieces who;
    if (p.by.mine) who.push_back({"Groupe " + std::to_string(p.by.group), false});
    else who.insert(who.end(), {{"[" + p.by.clanTag + "] ", true}, {"groupe " + std::to_string(p.by.group), false}});
    if (p.by.reinforcement) who.insert(who.end(), {{" ", true}, {"(renfort)", false}});
    return who;
}

Pieces recapMyGroupPieces(const Recap& r) {
    Pieces out;
    std::vector<std::string> seen;
    for (const RecapMyGroup& g : r.me.groups) {
        const std::string key = g.clanTag + ":" + std::to_string(g.group);
        if (g.part == "coeur" || std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        if (!out.empty()) out.push_back({", ", true});
        if (!g.mine) out.push_back({"[" + g.clanTag + "] ", true});
        out.push_back({std::to_string(g.group), true});
        if (g.reinforcement) out.insert(out.end(), {{" ", true}, {"(renfort)", false}});
    }
    if (out.empty()) out.push_back({"—", true});
    return out;
}

std::vector<std::pair<std::string, std::string>> recapMyLines(const Recap& r, const std::vector<std::pair<std::string, std::string>>& partNames) {
    auto nameOf = [&](const std::string& part) {
        for (const auto& [id, name] : partNames)
            if (id == part) return name;
        return part;
    };
    std::vector<std::pair<std::string, std::string>> out;
    if (!r.me.played) return out;
    // My groups (the clan's first), and the parts they downed.
    const std::string groups = joined(recapMyGroupPieces(r));
    std::string parts;
    for (const std::string& p : r.me.parts) parts += (parts.empty() ? "" : ", ") + nameOf(p);
    out.push_back({"Groupe", groups});
    out.push_back({"Parties abattues", parts.empty() ? std::string("aucune") : parts});
    out.push_back({"Zones terminées", std::to_string(r.me.zones)});
    out.push_back({"Morts / réanimations", (r.me.deaths >= 0 ? std::to_string(r.me.deaths) : std::string("—")) + " / " + std::to_string(r.me.revives)});
    std::string heart = "—";
    if (r.me.heartRank == 1) heart = percent(r.me.heartShare) + " (1er du clan)";
    else if (r.me.heartRank > 1) heart = percent(r.me.heartShare) + " (" + std::to_string(r.me.heartRank) + "e du clan)";
    out.push_back({"Dégâts au Cœur", heart});
    return out;
}

}  // namespace cosmic::raids
