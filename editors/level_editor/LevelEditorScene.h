#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "editors/shared/EditorHistory.h"
#include "editors/shared/PropertyEditor.h"
#include "engine/core/Scene.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class GameServices;

/// Integrated level editor: layered object placement with snapping, selection,
/// move / resize / rotate, copy-paste, undo / redo, asset library, property panel,
/// layers, events, cinematics, level settings, instant test play and autosave.
class LevelEditorScene : public Scene {
public:
    LevelEditorScene(GameServices& g, std::string levelId);
    bool openHelpOnEnter = false;  // automated screenshots (--editor-help)
    ~LevelEditorScene() override;

    void onEnter() override;
    void onExit() override;
    void onResume() override;
    void update(float dt) override;
    void render(float alpha) override;

private:
    enum class Tool { Select, Place, Pan };
    enum class Drag { None, Move, Resize, BoxSelect, Pan, Create };
    enum class Modal { None, Open, New, Settings, Events, Cinematics, Help, ConfirmQuit };
    enum class LeftTab { Explorer, History, Check };
    enum class Menu { None, File, Test };
    struct Issue {
        std::string text;
        std::string objectId;  // to select / frame ("" = level-wide)
        bool error = false;
    };

    // ---- document
    void openLevel(const std::string& id);
    void newLevel(const std::string& id, const std::string& name);
    void loadDocument(const LevelData& level, bool builtinSource);
    bool save();
    void autosave();
    void markChanged(const std::string& label);
    void applyJson(const Json& j);
    void rebuildPreview();
    void testPlay(bool fromCamera);
    void testFrom(Vec2 feet);
    /// Campaign levels are read-only: a copy (new id) becomes an editable player level.
    void duplicateForEditing();
    void captureThumbnail();

    // ---- editing operations
    std::string createObject(const PaletteEntry& entry, const Rect& rect);
    void deleteSelection();
    void copySelection();
    void paste(Vec2 at);
    void duplicateSelection();
    void rotateSelection(float degrees);
    void moveSelection(Vec2 delta);
    /// 0 left, 1 centre H, 2 right, 3 top, 4 centre V, 5 bottom, 6 spread H, 7 spread V.
    void alignSelection(int mode);
    void groupSelection(bool group);
    void expandToGroups();
    LevelObject* primary();
    std::string objectAt(Vec2 world) const;
    bool layerEditable(const std::string& layerId) const;
    float snapFor(const LevelObject& o) const;
    Vec2 snapPoint(Vec2 p, float grid) const;

    // ---- frame parts
    void handleViewport(const Rect& viewport);
    void handleShortcuts();
    void drawViewport(const Rect& viewport, float alpha);
    void drawOverlays(const Rect& viewport);
    void drawToolbar(const Rect& r);
    void drawMenus();
    void drawLeftPanel(const Rect& r);
    /// Layers as a tree with their objects (search, select, frame), and the active layer's settings.
    void drawExplorer(const Rect& r);
    void drawLayerSettings(const Rect& r);
    /// Right column: BIBLIOTHÈQUE and ENTITÉ, each folding.
    void drawRightPanel(const Rect& r);
    void drawLibrary(const Rect& r);
    /// Above the view: where the selection is (Niveau › calque › objet) and the overlays.
    void drawCanvasHeader(const Rect& r);
    /// Under the view, with 2 objects or more selected: one row each, and group actions.
    void drawSelectionTable(const Rect& r);
    void drawLinks(const Rect& viewport);
    void loadFavorites();
    void saveFavorites() const;
    void drawHistory(const Rect& r);
    void drawValidation(const Rect& r);
    void runChecks();
    void drawMinimap(const Rect& viewport);
    void drawReadOnlyBanner(const Rect& viewport);
    void drawProperties(const Rect& r);
    void drawStatus(const Rect& r);
    void drawModal();
    void modalOpen(const Rect& r);
    void modalNew(const Rect& r);
    void modalSettings(const Rect& r);
    void modalEvents(const Rect& r);
    void modalCinematics(const Rect& r);
    void modalHelp(const Rect& r);
    void flash(const std::string& msg, bool error = false);

    GameServices& g_;
    std::string requestedId_;
    LevelData level_;
    bool builtinSource_ = false;
    bool readOnly_ = false;
    std::string readOnlySource_;  // package id of an imported level being viewed
    std::string librarySearch_, outlinerSearch_;
    std::vector<Issue> issues_;
    bool checksDirty_ = true;
    bool minimapOn_ = true;
    Rect minimapRect_;
    Rect bannerRect_;
    int groupCounter_ = 0;
    std::string assetRoot_;
    bool mounted_ = false;
    bool dirty_ = false;
    float autosaveTimer_ = 0.0f;
    std::string lastSaved_;
    EditorHistory history_;

    std::unique_ptr<GameWorld> world_;
    GameRenderer renderer_;
    bool previewDirty_ = true;
    float previewDelay_ = 0.0f;

    EditorCamera cam_;
    Rect viewport_;
    Tool tool_ = Tool::Select;
    int paletteIndex_ = -1;
    std::string placeEnemy_;
    std::string placeDecor_;
    Drag drag_ = Drag::None;
    Vec2 dragStartWorld_, dragStartScreen_;
    Vec2 dragAccum_;
    int resizeHandle_ = -1;
    Rect resizeStart_;
    std::set<std::string> selection_;
    std::string hovered_;
    std::string activeLayer_ = "gameplay";
    bool gridOn_ = true;   // grid drawn
    bool snapOn_ = true;   // "Aimanter": positions on the grid
    int gridIndex_ = 1;    // 8, 16, 32
    bool overlayLinks_ = true, overlayZones_ = true, overlayCollisions_ = false;
    Menu menu_ = Menu::None;
    Rect menuAnchor_;
    bool libraryOpen_ = true, entityOpen_ = true;
    std::set<std::string> explorerOpen_;  // expanded layers ("type:<type>" when grouped by type)
    int explorerGroup_ = 0;               // 0 by layer, 1 by type
    std::set<std::string> favorites_;     // library items marked as favorites (editor/favorites.json)
    std::vector<LevelObject> clipboard_;
    LeftTab leftTab_ = LeftTab::Explorer;
    int libraryCategory_ = 0;
    std::string pickTarget_;  // property key waiting for an object pick
    PropertyEditor props_;
    Modal modal_ = Modal::None;
    Rect modalRect_;  // last dialog drawn (editor messages are placed outside it)
    int selectedEvent_ = 0;
    int cinematicTab_ = 0;
    int selectedStep_ = 0;
    std::string newId_, newName_;
    std::string message_;
    bool messageError_ = false;
    float messageTimer_ = 0.0f;
    float time_ = 0.0f;
    bool autosaveAvailable_ = false;
};

}  // namespace cosmic
