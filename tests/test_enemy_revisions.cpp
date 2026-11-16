// Native entities edited by an administrator (2.46, docs/online/ENEMY_REVISIONS.md): read-only
// in the player's enemy editor, editable and published in ADMINISTRATION; a published revision
// is played only by the level revisions bound to it (metadata.enemyRevisions, part of the
// fingerprint), so an older level revision (replays, records, challenges) keeps its definition.

#include "editors/enemy_editor/EnemyEditAccess.h"
#include "game/content/ContentDatabase.h"
#include "game/online/EnemyPublication.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("Enemy revisions: a native is read-only for the player, editable for an administrator") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("aethra_01");
    CHECK(level.has_value());
    auto used = enemiesUsedBy(content, *level);
    CHECK(!used.empty());
    const std::string id = *used.begin();
    CHECK(content.isNativeEnemy(id));
    EnemyEditAccess player = enemyEditAccess(content, id, false);
    CHECK(player.readOnly());
    CHECK(!player.save && !player.remove && !player.animation && !player.publish);
    EnemyEditAccess admin = enemyEditAccess(content, id, true);
    CHECK(admin.fields && admin.publish);
    CHECK(!admin.save && !admin.remove && !admin.id && !admin.animation);
    EnemyDefinition copy = duplicateForEditing(content, *content.enemy(id));
    CHECK(copy.id != id);
    CHECK(!content.isNativeEnemy(copy.id));
}

TEST_CASE("Enemy revisions: only the level revisions bound to a revision play it") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("aethra_01");
    CHECK(level.has_value());
    const std::string id = *enemiesUsedBy(content, *level).begin();
    const EnemyDefinition* native = content.enemy(id);
    CHECK(native != nullptr);
    auto before = fingerprintRankedLevel(content, level->toJson());
    CHECK(before.has_value());

    EnemyDefinition changed = *native;
    changed.hp = native->hp + 7;
    CHECK(content.setEnemyRevision(1, changed));
    // Not bound: the game's own definition, the same fingerprint as before.
    CHECK(content.enemyFor(id, *level)->hp == native->hp);
    auto unbound = fingerprintRankedLevel(content, level->toJson());
    CHECK(unbound.has_value() && unbound->hash == before->hash);
    // Bound to revision 1: its definition, another fingerprint.
    LevelData bound = *level;
    setEnemyBinding(bound, enemyBindingFor(content, bound, id, 1, {}));
    CHECK(content.enemyBindingOf(bound).at(id) == 1);
    CHECK(content.enemyFor(id, bound)->hp == native->hp + 7);
    auto after = fingerprintRankedLevel(content, bound.toJson());
    CHECK(after.has_value() && after->hash != before->hash);
    // A revision this game does not have: no definition (the level is not played as the server's).
    std::map<std::string, int> missing{{id, 9}};
    CHECK(content.enemyFor(id, missing) == nullptr);
    // A non-native id is refused.
    EnemyDefinition other = changed;
    other.id = "not_a_native_enemy";
    CHECK(!content.setEnemyRevision(1, other));
}
