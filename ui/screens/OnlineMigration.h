#pragma once
// 2.50 « Migrer vers un nouveau compte » (docs/online/DEVICES.md): an account stays private to
// its computer; its data go to a NEW account, on another computer, with a one-time code made on
// the old one (valid 24 h) and the old account's password. Then the new computer's game adds the
// old progression (its last copy) to its own: « Restaurer la progression transférée ».

#include <memory>

#include "engine/core/Json.h"

namespace cosmic {

class GameServices;
class Scene;

/// On the old computer: a copy of the progression is sent, then the code is made and shown.
std::unique_ptr<Scene> makeMigrationPrepare(GameServices& g);
/// On the new computer, logged in with the new account: the old pseudonym, its password, the code.
std::unique_ptr<Scene> makeMigrationRecover(GameServices& g);
/// The progression of the old account waiting on the server (`restore` of
/// GET /players/me/progress-restore): « Plus tard » or « Restaurer ».
std::unique_ptr<Scene> makeProgressRestore(GameServices& g, const Json& restore);

}  // namespace cosmic
