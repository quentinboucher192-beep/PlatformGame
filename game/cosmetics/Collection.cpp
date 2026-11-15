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
        if (i.isDefault || !i.achievement.empty()) continue;
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
    if (!item.achievement.empty()) return -1;
    if (item.isDefault) return 0;
    if (item.allStars) return std::max(1, totalStars);
    return item.stars;
}

bool cosmeticUnlocked(const CosmeticItem& item, const CosmeticProgress& p) {
    if (item.isDefault) return true;
    if (!item.achievement.empty()) return p.book && p.book->unlocked(item.achievement);
    return p.stars >= cosmeticThreshold(item, p.totalStars);
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
}

}  // namespace cosmic
