#pragma once

#include <memory>
#include <string>
#include <vector>

#include "editors/shared/EditorHistory.h"
#include "engine/animation/Animation.h"
#include "engine/core/Scene.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

class GameServices;

/// Visual enemy editor: create and tune enemies without code (appearance,
/// animations, stats, behaviours, attacks, powers, resistances, rewards, boss phases),
/// with an animated preview and a live test arena.
class EnemyEditorScene : public Scene {
public:
    explicit EnemyEditorScene(GameServices& g, std::string startId = {}, int startTab = 0)
        : g_(g), startId_(std::move(startId)), tab_(startTab) {}
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
    /// The selected enemy is a native (campaign) entity: read-only, saved as a copy.
    bool editingNative() const;

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
};

}  // namespace cosmic
