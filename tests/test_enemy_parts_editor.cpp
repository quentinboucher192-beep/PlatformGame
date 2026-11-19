// 2.76.2: the hit parts of a boss in the enemy editor (Boss › Parties, docs/HITBOX_PARTS.md): its rules written
// into data/enemies/<id>.json without touching anything else, the lock of the natives (read-only for the player,
// an administrator sets them), and the panel's layout (nothing overlaps, every text holds in its place).

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

#include "editors/enemy_editor/EnemyEditAccess.h"
#include "editors/enemy_editor/EnemyEditorScene.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/HitParts.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/HitPartsView.h"

#include <algorithm>

using namespace cosmic;

namespace {

std::string enemyFile(const std::string& id) {
    auto t = FileUtil::readText(Paths::dataDir() / "enemies" / (id + ".json"));
    return t ? *t : std::string();
}

std::vector<std::string> keysOf(const nlohmann::ordered_json& j) {
    std::vector<std::string> out;
    for (auto it = j.begin(); it != j.end(); ++it) out.push_back(it.key());
    return out;
}

bool overlaps(const Rect& a, const Rect& b) {
    const float e = 0.5f;
    return a.x < b.right() - e && b.x < a.right() - e && a.y < b.bottom() - e && b.y < a.bottom() - e;
}

}  // namespace

TEST_CASE("enemy editor parts (2.76.2): a boss's file written with its own rules comes back byte for byte") {
    ContentDatabase content;
    content.reload();
    int files = 0;
    for (const EnemyDefinition* d : content.enemies()) {
        if (!d->builtin || PartRules::of(d->id).rules.empty()) continue;
        const std::string text = enemyFile(d->id);
        CHECK(!text.empty());
        if (!Json::parse(text, nullptr, false).contains("parts")) continue;  // an écho with its original's parts (no block of its own)
        const std::string again = withPartRules(text, PartRules::of(d->id));
        if (again != text) std::printf("  %s : le fichier réécrit diffère\n", d->id.c_str());
        CHECK(again == text);
        ++files;
        // Written with the same rules: nothing is written, they are read again.
        std::string error;
        CHECK(savePartRules(d->id, PartRules::of(d->id), &error));
        CHECK(enemyFile(d->id) == text);
    }
    std::printf("  %d fichiers de boss avec des parties\n", files);
    CHECK(files >= 9);  // Pyrrhax (2.76.0) and the 8 Gigaboss of Asterion (2.76.2, step c)
    std::string error;
    CHECK(withPartRules("pas du json", PartRules{}, &error).empty());
    CHECK(!error.empty());
    CHECK(!savePartRules("../pyrrhax", PartRules{}, &error));
}

TEST_CASE("enemy editor parts (2.76.2): a change touches only the roles; a part that hurts takes no multiplier") {
    const std::string text = enemyFile("pyrrhax");
    PartRules rules = PartRules::of("pyrrhax");
    CHECK(rules.fromVersion == 16);
    for (PartRule& r : rules.rules) {
        if (r.name == "queue") {
            r.role = PartRole::Weak;
            r.damage = 1.5f;
        }
        if (r.name == "cou") r.hurtDuring = {"morsure"};
        if (r.name == "corps") {
            r.role = PartRole::Hurt;
            r.damage = 0.0f;
        }
    }
    PartRule fresh;
    fresh.name = "une partie nouvelle";
    fresh.role = PartRole::Armour;
    fresh.damage = 0.3f;
    rules.rules.push_back(fresh);
    const std::string out = withPartRules(text, rules);
    CHECK(!out.empty());
    using Ordered = nlohmann::ordered_json;
    const Ordered a = Ordered::parse(text), b = Ordered::parse(out);
    // Every other key, in its order, unchanged.
    CHECK(keysOf(a) == keysOf(b));
    for (auto it = a.begin(); it != a.end(); ++it)
        if (it.key() != "parts") CHECK(b[it.key()] == it.value());
    CHECK(b["parts"]["profile"] == a["parts"]["profile"]);
    // The roles in their order, the new one after them.
    std::vector<std::string> before = keysOf(a["parts"]["roles"]), after = keysOf(b["parts"]["roles"]);
    before.push_back("une partie nouvelle");
    CHECK(before == after);
    CHECK(b["parts"]["roles"]["queue"] == Ordered({{"role", "weak"}, {"damage", 1.5}}));
    CHECK(b["parts"]["roles"]["cou"] == Ordered({{"role", "normal"}, {"damage", 1}, {"hurtDuring", {"morsure"}}}));
    CHECK(b["parts"]["roles"]["corps"] == Ordered({{"role", "hurt"}}));
    CHECK(b["parts"]["roles"]["une partie nouvelle"] == Ordered({{"role", "armour"}, {"damage", 0.3}}));
    CHECK(out.find("\"damage\": 2,") != std::string::npos);  // an integer written as one (« tête »)
    // Read back: the rules the editor set.
    const PartRules back = PartRules::parse(Json::parse(out)["parts"], 16);
    CHECK(back.rule("queue") && back.rule("queue")->role == PartRole::Weak && back.rule("queue")->damage == 1.5f);
    CHECK(back.rule("corps") && back.rule("corps")->role == PartRole::Hurt && back.rule("corps")->damage == 0.0f);
    CHECK(back.rule("cou") && back.rule("cou")->hurtDuring == std::vector<std::string>{"morsure"});
    // The game's file itself is untouched (the editor writes only on « Enregistrer les parties »).
    CHECK(enemyFile("pyrrhax") == text);
}

TEST_CASE("enemy editor parts (2.76.2): no « blesse pendant » list of a sheet shows an attack's identifier; each name has its English") {
    ContentDatabase content;
    content.reload();
    const i18n::Dictionary& en = i18n::dictionary(Language::English);
    int lists = 0;
    for (const EnemyDefinition* d : content.enemies()) {
        if (!d->isBoss) continue;
        for (const PartRule& rule : PartRules::of(d->id).rules) {
            if (rule.role == PartRole::Hurt || rule.hurtDuring.empty()) continue;
            const std::vector<std::string> names = hurtDuringNames(rule, *d);
            const std::string label = hurtDuringLabel(rule, *d);
            // Each attack the boss has, by its name; none of the boss's identifiers anywhere in the list.
            int own = 0;
            for (const std::string& id : rule.hurtDuring)
                for (const AttackDef& a : d->attacks)
                    if (a.id == id) ++own;
            CHECK(static_cast<int>(names.size()) <= own);
            CHECK(names.empty() == label.empty());
            for (const std::string& n : names) {
                CHECK(!n.empty() && n.find('_') == std::string::npos);
                for (const AttackDef& a : d->attacks) CHECK(n != a.id);
                std::string out;
                if (!en.lookup(n, out)) std::printf("  %s, %s : « %s » sans anglais\n", d->id.c_str(), rule.name.c_str(), n.c_str());
                CHECK(en.lookup(n, out));
            }
            for (const AttackDef& a : d->attacks)
                if (std::find(rule.hurtDuring.begin(), rule.hurtDuring.end(), a.id) != rule.hurtDuring.end())
                    CHECK(std::find(names.begin(), names.end(), a.name) != names.end());
            if (!label.empty()) {
                std::string out;
                CHECK(en.lookup(label, out));
                CHECK(en.lookup(partNameLabel(rule.name) + " " + label, out));  // as the sheets of Asterion write it
                ++lists;
            }
        }
    }
    std::printf("  %d listes « blesse aussi pendant »\n", lists);
    CHECK(lists >= 10);
}

TEST_CASE("enemy editor parts (2.76.2): a native's parts are read-only for the player and set by an administrator only") {
    ContentDatabase content;
    content.reload();
    CHECK(content.isNativeEnemy("pyrrhax"));
    CHECK(!enemyEditAccess(content, "pyrrhax", false).parts);
    CHECK(enemyEditAccess(content, "pyrrhax", true).parts);
    // The player's own copy: everything else editable, its parts never (it fights with its box).
    EnemyDefinition copy = duplicateForEditing(content, *content.enemy("pyrrhax"));
    CHECK(content.saveUserEnemy(copy));
    const EnemyEditAccess mine = enemyEditAccess(content, copy.id, false);
    CHECK(mine.fields && mine.save && !mine.parts);
    CHECK(!enemyEditAccess(content, copy.id, true).parts);
    CHECK(PartRules::of(copy.id).fromVersion == 0 && PartRules::of(copy.id).rules.empty());
    content.deleteUserEnemy(copy.id);
}

TEST_CASE("enemy editor parts (2.76.2): Boss › Parties holds its texts, nothing overlaps (1280 x 720, 1920 x 1080, FR / EN)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    app.headlessDt = 1.0f / 30.0f;
    {
        GameServices g(app);
        g.loadAll();
        app.onSceneRender = [&g](bool covered) { g.ui.sceneInput = !covered; };
        g.progression.tutorial = "finished";
        g.settings.gameplay.attractTrailer = false;
        g.progression.asterionFilmSeen = true;
        app.pushScene(makeMainMenu(g));  // under the editor, as when it is opened from the menus
        app.runFrames(2);
        for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}})
            for (Language lang : {Language::French, Language::English})
                for (const char* id : {"pyrrhax", "voidworm", "titanus"})
                    for (bool admin : {false, true}) {
                        app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                        app.renderer().refreshOutputSize();
                        i18n::setLanguage(lang);
                        g.onlineOpen = "parts";
                        app.pushScene(admin ? makeEnemyEditorAdmin(g, id, 0) : makeEnemyEditor(g, id, 6));
                        app.runFrames(3);
                        auto* scene = dynamic_cast<EnemyEditorScene*>(app.topScene());
                        CHECK(scene != nullptr);
                        if (scene) {
                            const EnemyEditorScene::PartsLayout& L = scene->partsLayout();
                            CHECK(L.parts > 0);
                            CHECK(L.editable == admin);
                            CHECK(!L.texts.empty());
                            for (const auto& t : L.texts) {
                                const Vec2 m = g.ui.text().measureWrapped(t.text, t.face, t.px, t.r.w + 0.5f);
                                if (m.x > t.r.w + 0.5f || m.y > t.r.h + 0.5f)
                                    std::printf("  %s %gx%g : « %s » %.1f x %.1f dans %.1f x %.1f\n", id, size.x, size.y, t.text.c_str(), m.x, m.y, t.r.w,
                                                t.r.h);
                                CHECK(m.x <= t.r.w + 0.5f);
                                CHECK(m.y <= t.r.h + 0.5f);
                            }
                            for (size_t i = 0; i < L.boxes.size(); ++i)
                                for (size_t j = i + 1; j < L.boxes.size(); ++j) {
                                    if (overlaps(L.boxes[i], L.boxes[j])) std::printf("  %s %gx%g : éléments %zu et %zu se chevauchent\n", id, size.x, size.y, i, j);
                                    CHECK(!overlaps(L.boxes[i], L.boxes[j]));
                                }
                        }
                        app.popScene();
                        app.runFrames(1);
                    }
        i18n::setLanguage(Language::French);
        app.setWindowSize(1280, 720);
        app.renderer().refreshOutputSize();
        app.shutdown();
        app.onSceneRender = nullptr;
    }
}
