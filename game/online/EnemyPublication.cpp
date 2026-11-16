// Publication of an enemy revision (2.45, ADMINISTRATION › Ennemis): the campaign levels that use
// the enemy, each prepared as its next revision (same content, a new revision number, a binding
// to the new definition), with its fingerprint, manifest entry and rating computed by this game.
// The server checks everything again (server/src/enemyRevisions.ts).

#include "game/online/EnemyPublication.h"

#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/DifficultyRating.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/Package.h"

namespace cosmic {

namespace {
Json bindingJson(const std::map<std::string, int>& b) {
    Json j = Json::object();
    for (const auto& [id, rev] : b) j[id] = rev;
    return j;
}

std::string enemyName(const ContentDatabase& content, const std::string& id) {
    const EnemyDefinition* d = content.enemy(id);
    return d ? d->name : id;
}
}  // namespace

void setEnemyBinding(LevelData& level, const std::map<std::string, int>& binding) {
    if (!level.metadata.is_object()) level.metadata = Json::object();
    if (binding.empty()) level.metadata.erase("enemyRevisions");
    else level.metadata["enemyRevisions"] = bindingJson(binding);
}

std::set<std::string> enemiesUsedBy(const ContentDatabase& content, const LevelData& level) {
    ResourceManager res;
    res.init(nullptr, nullptr);
    DependencySet deps = DependencyCollector(content, res).collect(level);
    std::set<std::string> out;
    for (const auto& id : deps.enemies)
        if (content.isNativeEnemy(id)) out.insert(id);
    return out;
}

std::map<std::string, int> enemyBindingFor(const ContentDatabase& content, const LevelData& level, const std::string& enemyId, int revision,
                                           const std::map<std::string, int>& current) {
    LevelData l = level;
    std::map<std::string, int> binding = content.enemyBindingOf(level);
    if (!enemyId.empty()) binding[enemyId] = revision;
    // What it uses depends on the definitions it plays (a summon changed…): until it settles.
    for (int pass = 0; pass < 8; ++pass) {
        l.metadata["enemyRevisions"] = bindingJson(binding);
        std::map<std::string, int> next;
        for (const auto& id : enemiesUsedBy(content, l)) {
            auto c = current.find(id);
            if (c != current.end() && c->second > 0) next[id] = c->second;
        }
        if (!enemyId.empty()) next[enemyId] = revision;
        if (next == binding) break;
        binding = std::move(next);
    }
    return binding;
}

EnemyPublicationPlan planEnemyPublication(const ContentDatabase& content, const std::string& enemyId, int revision,
                                          const std::map<std::string, ServerCampaignLevel>& server, const std::map<std::string, int>& current) {
    EnemyPublicationPlan plan;
    auto fail = [&](const std::string& why) {
        plan.levels.clear();
        plan.error = why;
        return plan;
    };
    if (!content.isNativeEnemy(enemyId) || !content.enemyRevision(enemyId, revision)) return fail("La définition à publier est introuvable.");
    // Every definition online must be here: a level revision plays the current ones.
    for (const auto& [id, rev] : current)
        if (id != enemyId && rev > 0 && content.isNativeEnemy(id) && !content.enemyRevision(id, rev))
            return fail("La révision " + std::to_string(rev) + " de « " + enemyName(content, id) +
                        " » n'est pas dans ce jeu : actualisez la liste, ou mettez le jeu à jour.");
    for (const PlanetData* p : content.planets()) {
        for (const std::string& levelId : p->levels) {
            const LevelEntry* e = content.level(levelId);
            if (!e || e->source != ContentSource::Builtin) continue;
            std::string loadError;
            auto level = content.loadLevel(levelId, &loadError);
            if (!level) return fail("« " + e->name + " » : " + loadError);
            if (!enemiesUsedBy(content, *level).count(enemyId)) continue;
            auto s = server.find(levelId);
            if (s == server.end()) return fail("« " + e->name + " » est inconnu du serveur : ses classements ne peuvent pas suivre.");
            // The revision this game plays must be the one online (the new one is built on it).
            auto text = FileUtil::readText(e->path);
            Json now = text ? Json::parse(*text, nullptr, false) : Json();
            auto fp = now.is_discarded() || !now.is_object() ? std::nullopt : fingerprintRankedLevel(content, now);
            if (!fp || fp->hash != s->second.contentHash)
                return fail("« " + e->name + " » : ce jeu ne joue pas sa révision en ligne (révision " + std::to_string(s->second.version) +
                            ") : actualisez, ou mettez le jeu à jour.");
            EnemyPublicationLevel out;
            out.levelId = levelId;
            out.planetId = p->id;
            out.baseVersion = s->second.version;
            out.version = s->second.nextVersion;
            out.bests = s->second.bests;
            for (const auto& o : level->objects)
                if (o.type == "enemy" && o.propStr("enemy") == enemyId) ++out.instances;
            LevelData next = *level;
            next.metadata["revision"] = out.version;
            setEnemyBinding(next, enemyBindingFor(content, *level, enemyId, revision, current));
            // The file exactly as it is stored; its fingerprint computed from those bytes.
            out.text = next.toJson().dump(1) + "\n";
            Json parsed = Json::parse(out.text, nullptr, false);
            auto nfp = parsed.is_discarded() ? std::nullopt : fingerprintRankedLevel(content, parsed);
            auto lvl = parsed.is_discarded() ? std::nullopt : LevelData::fromJson(parsed);
            if (!nfp || !lvl) return fail("« " + e->name + " » : empreinte impossible à calculer.");
            out.hash = nfp->hash;
            out.manifest = onlineManifestEntry(content, *lvl, *nfp);
            out.rating = rateDifficulty(*lvl, content).toJson();
            plan.levels.push_back(std::move(out));
        }
    }
    return plan;
}

}  // namespace cosmic
