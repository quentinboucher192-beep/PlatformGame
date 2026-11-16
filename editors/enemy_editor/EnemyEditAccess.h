#pragma once

#include <string>

#include "game/enemies/EnemyDefinition.h"

namespace cosmic {

class ContentDatabase;

/// What the enemy editor lets the player change on an entity (2.45).
///   - a native entity (data/enemies, the campaign's) is read-only in the player's editor:
///     every field shown, nothing changed, saved or deleted, not even its clip timings;
///     « Dupliquer pour modifier » makes the player's own copy;
///   - the player's own entities (and imported ones) are fully editable;
///   - ADMINISTRATION › Ennemis (`admin`): the native's definition is edited and published
///     online (never saved in the user folder); its identifier, its sheet and the timings of its
///     clips stay the game's (the definition file only).
struct EnemyEditAccess {
    bool fields = false;     // the definition's fields can be changed
    bool id = false;         // its identifier
    bool animation = false;  // its sheet and the timings of its clips
    bool save = false;       // « Enregistrer » (user folder)
    bool remove = false;     // « Supprimer »
    bool publish = false;    // « Publier… » (online, administrators)
    bool readOnly() const { return !fields; }
};

EnemyEditAccess enemyEditAccess(const ContentDatabase& content, const std::string& id, bool admin);

/// The player's own copy of an entity, to edit it: a free identifier derived from it, « (copie) »
/// after its name, never native.
EnemyDefinition duplicateForEditing(const ContentDatabase& content, const EnemyDefinition& source);

}  // namespace cosmic
