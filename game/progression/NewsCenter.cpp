#include "game/progression/NewsCenter.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelTimer.h"
#include "game/online/LevelFingerprint.h"
#include "game/records/Records.h"

namespace cosmic {

namespace {
fs::path newsPath() { return Paths::savesDir() / "news.json"; }
constexpr size_t kMaxItems = 80;

const char* medalLabel(Medal m) {
    switch (m) {
        case Medal::Bronze: return "bronze";
        case Medal::Silver: return "argent";
        case Medal::Gold: return "or";
        case Medal::Platinum: return "platine";
        default: return "";
    }
}
}  // namespace

bool versionNewer(const std::string& a, const std::string& b) {
    int pa[3] = {0, 0, 0}, pb[3] = {0, 0, 0};
    std::sscanf(a.c_str(), "%d.%d.%d", &pa[0], &pa[1], &pa[2]);
    std::sscanf(b.c_str(), "%d.%d.%d", &pb[0], &pb[1], &pb[2]);
    for (int i = 0; i < 3; ++i)
        if (pa[i] != pb[i]) return pa[i] > pb[i];
    return false;
}

Json NewsCenter::Item::toJson() const {
    Json j = {{"id", id}, {"kind", kind}, {"title", title}, {"text", text}, {"date", date}, {"target", target}, {"read", read}};
    if (kind == "update") {
        j["size"] = size;
        j["sha256"] = sha256;
        j["download"] = download;
    }
    return j;
}

NewsCenter::Item NewsCenter::Item::fromJson(const Json& j) {
    Item it;
    it.id = jstr(j, "id");
    it.kind = jstr(j, "kind");
    it.title = jstr(j, "title");
    it.text = jstr(j, "text");
    it.date = jstr(j, "date");
    it.target = jstr(j, "target");
    it.read = jget<bool>(j, "read", false);
    it.size = jget<int64_t>(j, "size", 0);
    it.sha256 = jstr(j, "sha256");
    it.download = jstr(j, "download");
    return it;
}

NewsCenter::Snapshot NewsCenter::snapshot(const ContentDatabase& content) {
    Snapshot s;
    for (const PlanetData* p : content.planets())
        for (const LevelEntry* e : content.levelsForPlanet(p->id)) {
            if (e->source != ContentSource::Builtin) continue;
            if (auto l = content.loadLevel(e->id)) {
                s.levels[e->id] = fingerprintGameplay(content, *l);
                s.levelNames[e->id] = l->name + " (" + p->name + ")";
            }
        }
    for (const PowerDefinition* p : content.powers()) s.powers.push_back({p->id, p->name});
    for (const EnemyDefinition* e : content.enemies()) {
        if (!content.isNativeEnemy(e->id) || !e->variantOf.empty()) continue;  // echoes: their original is announced
        (e->isBoss ? s.bosses : s.enemies).push_back({e->id, e->name});
    }
    return s;
}

void NewsCenter::load() {
    auto j = FileUtil::loadJson(newsPath());
    if (!j || !j->is_object()) return;
    known_ = j->value("known", Json::object());
    lastVersion_ = jstr(*j, "lastVersion");
    items_.clear();
    if (j->contains("items") && (*j)["items"].is_array())
        for (const auto& it : (*j)["items"]) items_.push_back(Item::fromJson(it));
}

bool NewsCenter::save() const {
    Json list = Json::array();
    for (const auto& it : items_) list.push_back(it.toJson());
    Json j = {{"format", "cosmic.news"}, {"version", 1}, {"lastVersion", lastVersion_}, {"known", known_}, {"items", list}};
    return FileUtil::saveJson(newsPath(), j);
}

void NewsCenter::startScan(const ContentDatabase& content) {
    pending_ = std::async(std::launch::async, [&content] { return snapshot(content); });
}

void NewsCenter::poll(const ContentDatabase& content, RecordsManager& records, const std::string& gameVersion) {
    (void)content;
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    Snapshot s = pending_.get();
    auto changelog = FileUtil::loadJson(Paths::dataDir() / "news" / "changelog.json");
    apply(s, records, gameVersion, changelog ? *changelog : Json::object());
    records.save();
    save();
}

void NewsCenter::add(Item it) {
    for (const auto& e : items_)
        if (e.id == it.id) return;
    if (it.date.empty()) it.date = FileUtil::nowString();
    items_.insert(items_.begin(), std::move(it));
    if (items_.size() > kMaxItems) items_.resize(kMaxItems);
}

void NewsCenter::apply(const Snapshot& now, RecordsManager& records, const std::string& gameVersion, const Json& changelog) {
    bool first = known_.empty() || !known_.contains("levels");
    // The version just installed, with its changes.
    if (lastVersion_ != gameVersion) {
        Item v;
        v.id = "version:" + gameVersion;
        v.kind = "version";
        v.target = gameVersion;
        v.title = "Version " + gameVersion + " installée";
        if (changelog.contains(gameVersion) && changelog[gameVersion].is_array()) {
            for (const auto& line : changelog[gameVersion])
                if (line.is_string()) v.text += "• " + line.get<std::string>() + "\n";
            if (!v.text.empty()) v.text.pop_back();
        }
        add(v);
    }
    auto knownList = [&](const char* key) {
        std::set<std::string> out;
        if (known_.contains(key) && known_[key].is_array())
            for (const auto& x : known_[key])
                if (x.is_string()) out.insert(x.get<std::string>());
        return out;
    };
    if (!first) {
        std::set<std::string> powers = knownList("powers"), enemies = knownList("enemies"), bosses = knownList("bosses");
        for (const auto& [id, name] : now.powers)
            if (!powers.count(id)) add({"power:" + id, "power", "Nouveau pouvoir : " + name,
                                        "Il apparaît dans les niveaux et l'éditeur (Bibliothèque › Pouvoirs).", "", id});
        for (const auto& [id, name] : now.enemies)
            if (!enemies.count(id)) add({"enemy:" + id, "enemy", "Nouveau monstre : " + name, "Il rejoint la bibliothèque de l'éditeur.", "", id});
        for (const auto& [id, name] : now.bosses)
            if (!bosses.count(id)) add({"boss:" + id, "boss", "Nouveau boss : " + name, "Un nouvel adversaire attend dans la campagne.", "", id});
        const Json& lv = known_["levels"];
        for (const auto& [id, hash] : now.levels) {
            std::string name = now.levelNames.count(id) ? now.levelNames.at(id) : id;
            if (!lv.contains(id)) add({"level:" + id, "level", "Nouveau niveau dans la campagne : " + name, "À jouer depuis la carte des planètes.", "", id});
            else if (lv[id].is_string() && lv[id].get<std::string>() != hash)
                add({"level_changed:" + id + ":" + hash.substr(0, 12), "level_changed", "Niveau modifié : " + name,
                     "Sa nouvelle version change le parcours : les records sont comparés sur la nouvelle version.", "", id});
        }
    }
    // Best times set on another version of a campaign level no longer count.
    for (const auto& [id, hash] : now.levels) {
        const LevelRecord* r = records.get(id);
        if (!r || r->bestTime <= 0.0f || r->levelHash == hash) continue;
        auto old = records.invalidateBest(id);
        if (!old) continue;
        std::string name = now.levelNames.count(id) ? now.levelNames.at(id) : id;
        std::string text = "Le niveau a changé depuis votre record de " + LevelTimer::format(old->bestTime);
        if (old->bestMedal != Medal::None) text += std::string(" (médaille ") + medalLabel(old->bestMedal) + ")";
        text += " : il ne compte plus. À vous d'en établir un nouveau !";
        add({"record:" + id + ":" + hash.substr(0, 12), "record", "Record perdu : " + name, text, "", id});
    }
    // Remember what was seen.
    Json levels = Json::object();
    for (const auto& [id, hash] : now.levels) levels[id] = hash;
    Json pw = Json::array(), en = Json::array(), bo = Json::array();
    for (const auto& [id, n] : now.powers) pw.push_back(id);
    for (const auto& [id, n] : now.enemies) en.push_back(id);
    for (const auto& [id, n] : now.bosses) bo.push_back(id);
    known_ = {{"levels", levels}, {"powers", pw}, {"enemies", en}, {"bosses", bo}};
    lastVersion_ = gameVersion;
    scanned_ = true;
}

void NewsCenter::offerRelease(const std::string& version, int64_t size, const std::string& sha256, const std::string& download,
                              const std::string& notes) {
    Item it;
    it.id = "update:" + version;
    it.kind = "update";
    it.target = version;
    it.title = "Nouvelle version disponible : " + version;
    it.text = notes.empty() ? "L'installateur est prêt à être téléchargé." : notes;
    it.size = size;
    it.sha256 = sha256;
    it.download = download;
    add(it);
    save();
}

int NewsCenter::unread() const {
    int n = 0;
    for (const auto& it : items_) n += it.read ? 0 : 1;
    return n;
}

void NewsCenter::markAllRead() {
    for (auto& it : items_) it.read = true;
    save();
}

}  // namespace cosmic
