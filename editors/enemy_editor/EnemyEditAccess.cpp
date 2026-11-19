#include "editors/enemy_editor/EnemyEditAccess.h"

#include "game/content/ContentDatabase.h"

namespace cosmic {

EnemyEditAccess enemyEditAccess(const ContentDatabase& content, const std::string& id, bool admin) {
    EnemyEditAccess a;
    const EnemyDefinition* d = content.enemy(id);
    if (!d) return a;
    if (content.isNativeEnemy(id)) {
        if (!admin) return a;  // the campaign's entities: read-only for the player
        a.fields = true;
        a.publish = true;
        a.parts = true;  // written into data/enemies/<id>.json (EnemyEditorParts.cpp)
        return a;
    }
    if (admin) return a;  // the administration only publishes natives
    a.fields = a.id = a.animation = a.save = a.remove = true;
    return a;
}

EnemyDefinition duplicateForEditing(const ContentDatabase& content, const EnemyDefinition& source) {
    EnemyDefinition d = source;
    d.id = content.freeEnemyId(source.id + "_copie");
    d.name = source.name + " (copie)";
    d.builtin = false;
    d.sourcePath.clear();
    return d;
}

}  // namespace cosmic
