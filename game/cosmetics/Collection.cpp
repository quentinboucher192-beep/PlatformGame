#include "game/cosmetics/Collection.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "engine/core/FileUtil.h"
#include "game/achievements/Achievements.h"

namespace cosmic {

bool CosmeticCatalog::load(const fs::path& file, std::string* error) {
    categories_.clear();
    items_.clear();
    auto j = FileUtil::loadJson(file);
    if (!j || !j->is_object()) {
        if (error) *error = "catalogue des récompenses illisible : " + Paths::toUtf8(file);
        return false;
    }
    if (auto it = j->find("categories"); it != j->end() && it->is_array())
        for (const auto& c : *it) {
            if (!c.is_object()) continue;
            CosmeticCategory cat{jstr(c, "id"), jstr(c, "name")};
            if (!cat.id.empty()) categories_.push_back(std::move(cat));
        }
    if (auto it = j->find("items"); it != j->end() && it->is_array())
        for (const auto& e : *it) {
            if (!e.is_object()) continue;
            CosmeticItem item;
            item.id = jstr(e, "id");
            item.category = jstr(e, "category");
            item.name = jstr(e, "name");
            item.description = jstr(e, "description");
            item.look = jstr(e, "look");
            item.achievement = jstr(e, "achievement");
            item.isDefault = jget<bool>(e, "default", false);
            item.souvenir = jstr(e, "souvenir");
            item.raid = jstr(e, "raid");  // 2.67
            if (auto s = e.find("stars"); s != e.end()) {
                if (s->is_string() && s->get<std::string>() == "all") item.allStars = true;
                else if (s->is_number_integer()) item.stars = std::max(0, s->get<int>());
            }
            // 2.35: rewards of the challenges. A malformed condition never unlocks anything.
            auto kindOk = [](const std::string& k, bool any) { return k == "daily" || k == "weekly" || k == "monthly" || (any && k == "any"); };
            if (const Json& c = jfield(e, "challenge", Json()); c.is_object()) {
                const std::string tier = jstr(c, "tier");
                item.challenge.kind = jstr(c, "kind");
                item.challenge.tier = tier == "gold" ? 3 : tier == "silver" ? 2 : tier == "bronze" ? 1 : 0;
                item.challenge.count = jget<int>(c, "count", 0);
                if (!kindOk(item.challenge.kind, true) || item.challenge.tier == 0 || item.challenge.count < 1) continue;
            } else if (const Json& c2 = jfield(e, "challengeStreak", Json()); c2.is_object()) {
                item.challenge.streak = true;
                item.challenge.kind = jstr(c2, "kind");
                item.challenge.tier = 1;
                item.challenge.count = jget<int>(c2, "days", 0);
                if (!kindOk(item.challenge.kind, false) || item.challenge.count < 1) continue;
            }
            if (item.id.empty() || !category(item.category)) continue;
            if (find(item.id)) continue;  // duplicate id: the first wins
            items_.push_back(std::move(item));
        }
    // Every category needs a default (what a new player wears).
    for (const auto& c : categories_)
        if (!defaultItem(c.id)) {
            if (error) *error = "catégorie sans élément par défaut : " + c.id;
            return false;
        }
    return !items_.empty();
}

std::vector<const CosmeticItem*> CosmeticCatalog::items(const std::string& category) const {
    std::vector<const CosmeticItem*> out;
    for (const auto& i : items_)
        if (i.category == category) out.push_back(&i);
    return out;
}

const CosmeticItem* CosmeticCatalog::find(const std::string& id) const {
    for (const auto& i : items_)
        if (i.id == id) return &i;
    return nullptr;
}

const CosmeticItem* CosmeticCatalog::defaultItem(const std::string& category) const {
    for (const auto& i : items_)
        if (i.category == category && i.isDefault) return &i;
    return nullptr;
}

const CosmeticCategory* CosmeticCatalog::category(const std::string& id) const {
    for (const auto& c : categories_)
        if (c.id == id) return &c;
    return nullptr;
}

std::string CosmeticCatalog::lookOr(const std::string& category, const std::string& look) const {
    if (category == "suit" && isServerSuitRef(look)) return look;
    if (std::string kind; isServerLookRef(look, &kind) && kind == category) return look;
    for (const auto& i : items_)
        if (i.category == category && i.look == look && !look.empty()) return look;
    const CosmeticItem* d = defaultItem(category);
    return d ? d->look : std::string();
}

std::vector<CosmeticCatalog::Milestone> CosmeticCatalog::milestones(int totalStars) const {
    std::map<int, Milestone> m;
    for (const auto& i : items_) {
        if (i.isDefault || !i.achievement.empty() || i.challengeReward()) continue;
        int t = cosmeticThreshold(i, totalStars);
        if (t <= 0) continue;
        m[t].stars = t;
        m[t].items.push_back(&i);
    }
    std::vector<Milestone> out;
    for (auto& [s, ms] : m) out.push_back(std::move(ms));
    return out;
}

int cosmeticThreshold(const CosmeticItem& item, int totalStars) {
    if (!item.achievement.empty() || item.challengeReward() || !item.souvenir.empty() || !item.raid.empty()) return -1;
    if (item.isDefault) return 0;
    if (item.allStars) return std::max(1, totalStars);
    return item.stars;
}

namespace {

int kindIndex(const std::string& kind) { return kind == "daily" ? 0 : kind == "weekly" ? 1 : kind == "monthly" ? 2 : -1; }

}  // namespace

int ChallengeRewardProgress::results(const std::string& kind, int tier) const {
    if (tier < 1 || tier > 3) return 0;
    if (kind == "any") return atLeast[0][tier - 1] + atLeast[1][tier - 1] + atLeast[2][tier - 1];
    int k = kindIndex(kind);
    return k < 0 ? 0 : atLeast[k][tier - 1];
}

int ChallengeRewardProgress::streak(const std::string& kind) const {
    int k = kindIndex(kind);
    return k < 0 ? 0 : bestStreak[k];
}

bool ChallengeRewardProgress::operator==(const ChallengeRewardProgress& o) const {
    if (known != o.known) return false;
    for (int k = 0; k < 3; ++k) {
        if (bestStreak[k] != o.bestStreak[k]) return false;
        for (int t = 0; t < 3; ++t)
            if (atLeast[k][t] != o.atLeast[k][t]) return false;
    }
    return true;
}

std::string cosmeticCategorySingular(const std::string& category) {
    if (category == "suit") return "Combinaison";
    if (category == "trail") return "Traînée";
    if (category == "frame") return "Cadre d'avatar";
    if (category == "title") return "Titre";
    if (category == "victory") return "Effet de victoire";
    return category;
}

std::pair<int, int> cosmeticChallengeProgress(const CosmeticItem& item, const CosmeticProgress& p) {
    if (!item.challengeReward()) return {0, 0};
    const int need = std::max(1, item.challenge.count);
    if (!p.challenges || !p.challenges->known) return {0, need};
    int have = item.challenge.streak ? p.challenges->streak(item.challenge.kind) : p.challenges->results(item.challenge.kind, item.challenge.tier);
    return {std::clamp(have, 0, need), need};
}

bool cosmeticUnlocked(const CosmeticItem& item, const CosmeticProgress& p) {
    if (item.isDefault) return true;
    // 2.43: won at a challenge (never taken back); what was created on the server only so.
    if (p.won && p.won->has(item.id)) return true;
    if (item.serverMade()) return false;
    if (!item.souvenir.empty()) return item.souvenir == "tutorial" && p.tutorialFinished;  // 2.64
    if (!item.raid.empty()) return false;  // 2.67: a raid's reward, only once claimed (won above)
    if (!item.achievement.empty()) return p.book && p.book->unlocked(item.achievement);
    if (item.challengeReward()) {
        auto [have, need] = cosmeticChallengeProgress(item, p);
        return p.challenges && p.challenges->known && have >= need;
    }
    return p.stars >= cosmeticThreshold(item, p.totalStars);
}

std::vector<const CosmeticItem*> challengeRewardsFor(const CosmeticCatalog& catalog, const std::string& kind, bool late) {
    std::vector<const CosmeticItem*> out;
    for (const auto& i : catalog.items()) {
        if (!i.challengeReward()) continue;
        if (i.challenge.streak ? (!late && i.challenge.kind == kind) : (i.challenge.kind == kind || i.challenge.kind == "any")) out.push_back(&i);
    }
    // Those of this kind first, then those of any kind, the nearest (fewest results) first.
    std::stable_sort(out.begin(), out.end(), [](const CosmeticItem* a, const CosmeticItem* b) {
        bool anyA = a->challenge.kind == "any", anyB = b->challenge.kind == "any";
        if (anyA != anyB) return !anyA;
        return a->challenge.count < b->challenge.count;
    });
    return out;
}

void CollectionBook::load() {
    if (file_.empty()) file_ = Paths::savesDir() / "collection.json";
    std::error_code ec;
    existed_ = fs::exists(file_, ec);
    if (!existed_) return;
    if (auto j = FileUtil::loadJson(file_)) fromJson(*j);
}

bool CollectionBook::save() const {
    if (file_.empty()) return false;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    return FileUtil::writeTextAtomic(file_, toJson().dump(1));
}

const CosmeticItem* CollectionBook::equipped(const CosmeticCatalog& catalog, const std::string& category,
                                             const CosmeticProgress& p) const {
    if (auto it = equipped_.find(category); it != equipped_.end()) {
        if (const CosmeticItem* i = catalog.find(it->second); i && i->category == category && cosmeticUnlocked(*i, p)) return i;
        if (const CosmeticItem* t = p.won ? p.won->item(it->second) : nullptr; t && t->category == category) return t;
    }
    return catalog.defaultItem(category);
}

std::string CollectionBook::equippedId(const std::string& category) const {
    auto it = equipped_.find(category);
    return it == equipped_.end() ? std::string() : it->second;
}

void CollectionBook::equip(const std::string& category, const std::string& id) { equipped_[category] = id; }

std::string CollectionBook::look(const CosmeticCatalog& catalog, const std::string& category, const CosmeticProgress& p) const {
    const CosmeticItem* i = equipped(catalog, category, p);
    return i ? i->look : std::string();
}

std::map<std::string, std::string> CollectionBook::equippedIds(const CosmeticCatalog& catalog, const CosmeticProgress& p) const {
    std::map<std::string, std::string> out;
    for (const auto& c : catalog.categories())
        if (const CosmeticItem* i = equipped(catalog, c.id, p)) out[c.id] = i->id;
    return out;
}

std::vector<const CosmeticItem*> CollectionBook::fresh(const CosmeticCatalog& catalog, const CosmeticProgress& p) const {
    std::vector<const CosmeticItem*> out;
    for (const auto& i : catalog.items())
        if (!i.isDefault && !announced(i.id) && cosmeticUnlocked(i, p)) out.push_back(&i);
    return out;
}

// ------------------------------------------------------- rewards won (2.43)

bool WonRewards::has(const std::string& ref) const {
    for (const auto& w : list)
        if (w.ref == ref) return true;
    return false;
}

bool WonRewards::wonAt(const std::string& challengeId, const std::string& ref) const {
    for (const auto& w : list)
        if (w.challengeId == challengeId && w.ref == ref) return true;
    return false;
}

const CosmeticItem* WonRewards::title(const std::string& ref) const {
    for (const auto& t : titles)
        if (t.id == ref) return &t;
    return nullptr;
}

const CosmeticItem* WonRewards::suit(const std::string& ref) const {
    for (const auto& s : suits)
        if (s.id == ref) return &s;
    return nullptr;
}

const CosmeticItem* WonRewards::serverLook(const std::string& ref) const {
    for (const auto& l : looks)
        if (l.id == ref) return &l;
    return nullptr;
}

const CosmeticItem* WonRewards::item(const std::string& ref) const {
    if (const CosmeticItem* t = title(ref)) return t;
    if (const CosmeticItem* s = suit(ref)) return s;
    return serverLook(ref);
}

const WonReward* WonRewards::origin(const std::string& ref) const {
    const WonReward* first = nullptr;
    for (const auto& w : list)
        if (w.ref == ref && (!first || w.grantedAt < first->grantedAt || (w.grantedAt == first->grantedAt && w.date < first->date))) first = &w;
    return first;
}

namespace {

bool knownTitleColor(const std::string& c) { return c == "gold" || c == "silver" || c == "cyan" || c == "violet"; }

/// At most `maxCodepoints` characters of UTF-8, control characters as spaces.
std::string cleanText(const std::string& s, size_t maxCodepoints) {
    std::string out;
    size_t n = 0;
    for (size_t i = 0; i < s.size() && n < maxCodepoints;) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + len > s.size()) break;
        if (c < 0x20 || c == 0x7f) out += ' ';
        else out.append(s, i, len);
        i += len;
        ++n;
    }
    return out;
}

bool safeRef(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char ch : s) {
        unsigned char u = static_cast<unsigned char>(ch);
        if (!(std::islower(u) || std::isdigit(u) || ch == '_' || ch == ':' || ch == '-' || ch == 'W')) return false;
    }
    return true;
}

}  // namespace

std::optional<WonRewards> WonRewards::fromJson(const Json& rewards) {
    if (!rewards.is_array()) return std::nullopt;
    WonRewards w;
    w.known = true;
    for (const Json& r : rewards) {
        if (!r.is_object() || w.list.size() >= 1000) continue;
        WonReward x;
        const Json& c = jfield(r, "challenge", Json());
        x.challengeId = jstr(c, "id");
        x.challengeKind = jstr(c, "kind");
        x.period = jstr(c, "period");
        x.date = jstr(c, "date");
        x.kind = jstr(r, "kind");
        x.ref = jstr(r, "ref");
        x.grantedAt = jstr(r, "granted_at");
        // 2.60: what it was won by (a condition this game knows, its value in bounds; else nothing said).
        if (std::string cond = jstr(r, "condition"); cond == "tier" || cond == "top" || cond == "finished") {
            const int v = jget<int>(r, "value", 0);
            if (cond == "finished" || (cond == "tier" && v >= 1 && v <= 3) || (cond == "top" && v >= 1 && v <= 100)) {
                x.condition = cond;
                x.value = cond == "finished" ? 0 : v;
            }
        }
        if (!safeRef(x.challengeId) || !safeRef(x.ref) || (x.kind != "item" && x.kind != "title" && x.kind != "suit" && !isCreationLookKind(x.kind)))
            continue;
        if (isCreationLookKind(x.kind)) {
            // 2.59: a trail, a frame or a victory effect created on the server: its name and look,
            // as an item of its category.
            const Json& c = jfield(r, "creation", Json());
            std::string name = cleanText(jstr(c, "name"), 32);
            std::string refKind;
            auto look = creationLookFromJson(x.kind, c);
            if (!isServerLookRef(x.ref, &refKind) || refKind != x.kind || jstr(c, "kind") != x.kind || name.empty() || !look) continue;
            if (!w.serverLook(x.ref)) {
                CosmeticItem item;
                item.id = x.ref;
                item.category = x.kind;
                item.name = name;
                item.look = x.ref;
                item.creationLook = *look;
                item.description = x.kind == "trail"   ? "Une traînée créée sur Cosmic Online, gagnée à un défi."
                                   : x.kind == "frame" ? "Un cadre d'avatar créé sur Cosmic Online, gagné à un défi."
                                                       : "Un effet de victoire créé sur Cosmic Online, gagné à un défi.";
                w.looks.push_back(std::move(item));
            }
        } else if (x.kind == "suit") {
            // A suit created on the server: its name and colours, as an item of the category « suit ».
            const Json& s = jfield(r, "suit", Json());
            std::string name = cleanText(jstr(s, "name"), 32);
            auto colors = suitColorsFromJson(jfield(s, "colors", Json()));
            if (!isServerSuitRef(x.ref) || name.empty() || !colors) continue;
            if (!w.suit(x.ref)) {
                CosmeticItem item;
                item.id = x.ref;
                item.category = "suit";
                item.name = name;
                item.look = x.ref;
                item.suitColors = *colors;
                item.description = "Une combinaison créée sur Cosmic Online, gagnée à un défi.";
                w.suits.push_back(std::move(item));
            }
        } else if (x.kind == "title") {
            // A title of the server: its text and colour, as an item of the category « title ».
            const Json& t = jfield(r, "title", Json());
            std::string text = cleanText(jstr(t, "text"), 32);
            if (x.ref.rfind("srv:", 0) != 0 || text.empty()) continue;
            if (!w.title(x.ref)) {
                CosmeticItem item;
                item.id = x.ref;
                item.category = "title";
                item.name = text;
                item.look = text;
                std::string col = jstr(t, "color");
                item.color = knownTitleColor(col) ? col : "gold";
                item.description = "Un titre créé sur Cosmic Online, gagné à un défi.";
                w.titles.push_back(std::move(item));
            }
        } else if (x.ref.rfind("srv", 0) == 0) {
            continue;  // an item of the catalogue is never named like what the server creates
        }
        w.list.push_back(std::move(x));
    }
    return w;
}

Json WonRewards::toJson() const {
    Json out = Json::array();
    for (const auto& x : list) {
        Json r = {{"challenge", {{"id", x.challengeId}, {"kind", x.challengeKind}, {"period", x.period}, {"date", x.date}}},
                  {"kind", x.kind},
                  {"ref", x.ref},
                  {"granted_at", x.grantedAt}};
        if (!x.condition.empty()) {
            r["condition"] = x.condition;
            r["value"] = x.value;
        }
        if (const CosmeticItem* t = x.kind == "title" ? title(x.ref) : nullptr) r["title"] = {{"id", t->id}, {"text", t->look}, {"color", t->color}};
        if (const CosmeticItem* s = x.kind == "suit" ? suit(x.ref) : nullptr; s && s->suitColors)
            r["suit"] = {{"id", s->id}, {"name", s->name}, {"colors", suitColorsToJson(*s->suitColors)}};
        if (const CosmeticItem* l = isCreationLookKind(x.kind) ? serverLook(x.ref) : nullptr; l && l->creationLook) {
            Json c = creationLookToJson(*l->creationLook);
            c["id"] = l->id;
            c["kind"] = x.kind;
            c["name"] = l->name;
            r["creation"] = std::move(c);
        }
        out.push_back(std::move(r));
    }
    return out;
}

Json CollectionBook::toJson() const {
    Json j = Json::object();
    j["version"] = kVersion;
    j["equipped"] = equipped_;
    j["announced"] = announced_;
    j["menuMusic"] = menuMusic;
    j["heard"] = heard;
    j["met"] = met;
    j["exported"] = exported;
    j["badges"] = badges;
    j["badges_chosen"] = badgesChosen;
    j["raid_rewards"] = raidRewards;
    return j;
}

void CollectionBook::fromJson(const Json& j) {
    if (!j.is_object()) return;
    auto strings = [&](const char* key, std::set<std::string>& out) {
        if (auto it = j.find(key); it != j.end() && it->is_array())
            for (const auto& v : *it)
                if (v.is_string() && v.get<std::string>().size() <= 200) out.insert(v.get<std::string>());
    };
    if (auto it = j.find("equipped"); it != j.end() && it->is_object())
        for (auto e = it->begin(); e != it->end(); ++e)
            if (e->is_string()) equipped_[e.key()] = e->get<std::string>();
    strings("announced", announced_);
    strings("heard", heard);
    strings("met", met);
    strings("raid_rewards", raidRewards);  // 2.67
    menuMusic = jstr(j, "menuMusic");
    exported = std::max(0, jget<int>(j, "exported", 0));
    badges.clear();
    if (auto it = j.find("badges"); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_string() && v.get<std::string>().size() <= 64 && badges.size() < 10 &&
                std::find(badges.begin(), badges.end(), v.get<std::string>()) == badges.end())
                badges.push_back(v.get<std::string>());
    // Saves of 2.41 betas without the flag: a non-empty choice was a choice.
    badgesChosen = jget<bool>(j, "badges_chosen", !badges.empty());
}

}  // namespace cosmic
