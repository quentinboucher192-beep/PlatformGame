#include "game/cosmetics/Collection.h"

#include <algorithm>
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
    if (!item.achievement.empty() || item.challengeReward()) return -1;
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
    if (auto it = equipped_.find(category); it != equipped_.end())
        if (const CosmeticItem* i = catalog.find(it->second); i && i->category == category && cosmeticUnlocked(*i, p)) return i;
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
    menuMusic = jstr(j, "menuMusic");
    exported = std::max(0, jget<int>(j, "exported", 0));
    badges.clear();
    if (auto it = j.find("badges"); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_string() && v.get<std::string>().size() <= 64 && badges.size() < 10 &&
                std::find(badges.begin(), badges.end(), v.get<std::string>()) == badges.end())
                badges.push_back(v.get<std::string>());
}

}  // namespace cosmic
