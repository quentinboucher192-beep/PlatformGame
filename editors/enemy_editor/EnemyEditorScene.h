#pragma once

#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "editors/enemy_editor/EnemyEditAccess.h"
#include "editors/shared/EditorHistory.h"
#include "engine/animation/Animation.h"
#include "engine/core/Scene.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/EnemyPublication.h"
#include "game/powers/PowerKeys.h"

namespace cosmic {

class GameServices;

/// Visual enemy editor: create and tune enemies without code (appearance,
/// animations, stats, behaviours, attacks, powers, resistances, rewards, boss phases),
/// with an animated preview and a live test arena. The native entities (the campaign's) are
/// read-only there (2.45, EnemyEditAccess): « Dupliquer pour modifier » makes the player's copy.
/// In ADMINISTRATION › Ennemis (`admin`), one native's definition is edited and published online
/// with the campaign levels that use it (EnemyEditorPublish.cpp).
class EnemyEditorScene : public Scene {
public:
    explicit EnemyEditorScene(GameServices& g, std::string startId = {}, int startTab = 0, bool admin = false, int adminBase = 0)
        : g_(g), startId_(std::move(startId)), tab_(startTab), admin_(admin), adminBase_(adminBase) {}
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void fixedUpdate(float dt) override;
    void render(float alpha) override;

private:
    void select(const std::string& id);
    void newEnemy(bool duplicate);
    void saveEnemy();
    void deleteEnemy();
    void resetArena();
    void changed();
    void loadAnimation();
    /// Writes the edited clip timings to the user folder; a native animation is never
    /// replaced: the enemy gets its own copy ("animations/user/<id>.json").
    bool saveAnimation();
    /// Unsaved timing edits live in the shared animation cache: reload it from disk.
    void discardAnimationEdits();
    /// The selected enemy is a native (campaign) entity.
    bool editingNative() const;
    /// What can be changed on the selected entity (read-only native, admin publication…).
    EnemyEditAccess access() const;
    /// « Dupliquer pour modifier »: the player's own copy of the selected entity, selected.
    void duplicateToEdit();
    void quit();

    // ADMINISTRATION › Ennemis (EnemyEditorPublish.cpp): the native's definition published
    // online with the campaign levels that use it, each with its choice about the records.
    struct Publish {
        int step = 0;  // 0 asking the server, 1 preparing the levels, 2 the levels and their choice,
                       // 3 confirmation, 4 sending, 5 published (the server's answer), 6 error
        int revision = 0;        // the new revision (the server's next number)
        std::string definition;  // the definition file exactly as it is sent
        std::future<EnemyPublicationPlan> preparing;
        std::vector<EnemyPublicationLevel> levels;
        std::vector<char> reset;  // per level: 1 « Réinitialiser les records » (new boards), 0 keep them
        bool pending = false;     // the revision is in the content while it is prepared
        std::string error;
        Json result;  // the server's answer
    };
    void openPublish();
    void updatePublish();
    void sendPublish();
    void closePublish();
    void drawPublish();
    void drawAdminPanel(const Rect& r);

    void drawList(const Rect& r);
    void drawPreview(const Rect& r);
    void drawArena(const Rect& r, float alpha);
    void drawProperties(const Rect& r);
    void tabGeneral(float x, float& y, float w);
    void tabBehaviour(float x, float& y, float w);
    void tabAttacks(float x, float& y, float w);
    void tabPowers(float x, float& y, float w);
    void tabResistances(float x, float& y, float w);
    void tabRewards(float x, float& y, float w);
    void tabBoss(float x, float& y, float w);
    std::vector<std::string> clipNames() const;
    void flash(const std::string& m, bool error = false);

    GameServices& g_;
    EnemyDefinition work_;
    std::string selectedId_;
    bool dirty_ = false;
    EditorHistory history_;
    AnimationSetPtr anim_;
    Animator previewAnim_;
    int previewClip_ = 0;
    bool previewPlaying_ = true;
    float previewSpeed_ = 1.0f;
    bool showRanges_ = true;
    bool animDirty_ = false;
    std::string animPath_;
    fs::path animMount_;

    std::unique_ptr<GameWorld> arena_;
    PowerKeyMap arenaKeys_;  // the arena's powers on their suggested keys (2.31)
    GameRenderer arenaRenderer_;
    bool arenaDirty_ = true;
    bool controlPlayer_ = false;
    bool godMode_ = true;
    Rect arenaRect_;
    std::string startId_;  // enemy shown first (command line, links)
    float scrollTo_ = -1.0f;  // properties scroll asked from the command line (unscaled px)
    int tab_ = 0;
    int selectedAttack_ = 0;
    int selectedPhase_ = 0;
    float time_ = 0.0f;
    std::string message_;
    bool messageError_ = false;
    float messageTimer_ = 0.0f;
    bool confirmDelete_ = false;
    float respawnTimer_ = 0.0f;
    // ADMINISTRATION › Ennemis
    bool admin_ = false;
    int adminBase_ = 0;           // the revision online the edit starts from (0: the game's own definition)
    bool adminMissing_ = false;   // that revision is not in this game: nothing can be published
    bool confirmQuit_ = false;    // « Retour » with changes not published: asked again
    std::unique_ptr<Publish> publish_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
