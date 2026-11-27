#pragma once

// 2.61 ADMINISTRATION › Créations › « Studio »: the editor of the creations' images, for the
// administrators (the screen only exists for them, and the server checks the role of the session
// on every request). As the validated mock-up lays it out:
//   - at the left, the explorer: the kinds (Combinaisons, Traînées, Cadres, Effets de victoire)
//     with the creations of the server and the drafts of this computer, a search, « + Nouvelle »,
//     « Importer… », « Dupliquer », « Renommer », « Archiver »; the library (the game's sprites as
//     models, the palettes, the folder « Images à importer » and its new files);
//   - at the top, the tabs of the creations open (« ● » not saved; closing one asks first);
//   - in the middle, the rail of tools (pencil, eraser, fill, picker, line, rectangle, ellipse,
//     selection, move, mirrors and symmetry: tooltips with their keys), the canvas (zoom, grid,
//     onion skin, coordinates), and the frise of the frames of each animation of the game;
//   - at the right, the palette (the creation's, Nova's, the planets'), the HSV picker, the code
//     #rrggbb, « Recolorer toutes les images… », and the live preview (in a level, on a board,
//     on a profile); a trail or a victory effect has its emitter beside its particle.
// « Enregistrer » keeps a draft on this computer (user folder, studio/brouillons); « Envoyer au
// serveur » creates or updates the creation and sends its image (creationAssets.ts on the server).
// Keyboard, gamepad (a cursor on the canvas, the tools on the shoulders) and mouse. Display only.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"
#include "game/online/OnlineTypes.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/CreationImageFx.h"
#include "ui/studio/StudioDoc.h"
#include "ui/studio/StudioImage.h"
#include "ui/studio/StudioSlicer.h"

namespace cosmic {

class GameServices;
class UIContext;

namespace studio {

enum class Tool { Pencil, Eraser, Fill, Picker, Line, Rect, Ellipse, Select, Move, Mirror, Count };

/// A palette of the library: the creation's own, Nova's, a planet's, the game's base colours.
struct LibraryPalette {
    std::string name;
    std::vector<Px> colors;
};

/// A model of the library: a sprite of the game a creation may start from or look at.
struct LibraryModel {
    std::string name;    // « Nova », « Givre »…
    std::string detail;  // « planche de Nova · 320 × 864 »
    DocKind kind;        // what it starts
    std::string asset;   // the file of the assets ("" when drawn by the game: `look`)
    std::string look;    // a frame of the catalogue drawn by the game (ring models)
    TexturePtr thumb;
};

/// A file of the folder « Images à importer » (or dropped, or chosen).
struct ImportFile {
    fs::path path;
    std::string name;
    uintmax_t bytes = 0;
    int w = 0, h = 0;
    std::string problem;  // why it cannot be imported ("" = it can)
    bool fresh = false;   // not looked at yet
    TexturePtr thumb;
};

class Studio {
public:
    struct Hooks {
        std::function<void(const std::string&, bool)> say;  // the status line of ADMINISTRATION
        std::function<bool(const ApiResult&)> lostRole;     // the role went: the screen closes
        std::function<void()> reloadList;                   // the list of the server, read again
        std::function<void(const std::string& ref, const std::string& name)> giveReward;  // « Donner en récompense… »
    };
    Studio(GameServices& g, Hooks hooks);
    ~Studio();  // the open creations not saved are kept as drafts

    /// The creations of GET /admin/creations (`loaded`: read at least once); `creations` must
    /// outlive the Studio (AdminCreations' list, which owns it).
    void setServerList(const Json& creations, bool loaded);
    /// Automated screenshots: "new:<kind>", "import:<file>", "draw", "onion", "<kind>"...
    void open(const std::string& arg);
    /// The top bar (status, « Enregistrer », « Envoyer au serveur », « Donner en récompense… »)
    /// in `bar`, the Studio below in `body`.
    void draw(const Rect& bar, const Rect& body);
    /// A window over everything (import, confirmation, recolour, library): drawn by drawModal().
    bool modal() const;
    void drawModal();
    /// Esc / B has something to cancel here (a selection, a shape, a window): the screen stays.
    bool capturesBack() const;
    /// A file dropped on the window (SDL_EVENT_DROP_FILE).
    void dropFile(const std::string& path);
    bool hasUnsaved() const;

private:
    struct OpenDoc {
        StudioDoc doc;
        ImageHistory history;
        bool dirty = false;    // changed since the draft was last saved
        bool unsent = false;   // changed since it was last sent to the server
        std::string sentAt;    // the server's answer to the last send (ISO)
        int anim = 0, frame = 0;
        float zoom = 0.0f;     // 0: as large as the canvas allows
        Vec2 pan{};
        bool grid = true, onion = true;
        Symmetry sym = Symmetry::None;
        // Selection (in the frame): its rectangle, and its pixels lifted while they move.
        bool hasSel = false;
        IRect sel;
        bool lifted = false;
        PixelImage liftedPx;
        int liftX = 0, liftY = 0;
        // The GPU copy of the image: all of it to send again, or only this rectangle.
        TexturePtr tex;
        bool texDirty = true;
        IRect texRect;
        bool playing = false;
        float playClock = 0.0f;
        // Downloading the server's image.
        bool loadingImage = false;
        // The live preview's chip.
        int previewMode = 0;
        // Its animations (currentAnim() points in it: the same count for a document's life).
        mutable std::vector<DocAnim> animCache;
        mutable int animFps = 0;
    };

    // ----------------------------------------------------------------- documents
    OpenDoc* active();
    const OpenDoc* active() const;
    void openDoc(StudioDoc doc, bool dirty);
    void openServer(const Json& creation);
    void openDraft(const std::string& uid);
    void newDoc(DocKind kind);
    void newFromModel(const LibraryModel& m, DocKind kind);
    void duplicateSelected();
    void archiveSelected();
    void startRename();
    bool saveDraft(OpenDoc& d, bool quiet = false);
    void closeTab(int index, bool force);
    void send();
    void sendImage(const std::string& uid, const std::vector<uint8_t>& png);
    void loadAdminImages();
    const CreationImageInfo* imageInfoOf(int serverId) const;
    void loadDrafts();
    void saveSession() const;
    void loadSession();
    std::vector<DocAnim> animsOf(const OpenDoc& d) const;
    const DocAnim* currentAnim(const OpenDoc& d) const;
    IRect currentFrame(const OpenDoc& d) const;
    /// The image before a change, inside `region` (empty: the whole image), kept until commit().
    void beginEdit(OpenDoc& d, IRect region = {});
    /// Those pixels of `r` back as they were at beginEdit() (a shape being dragged).
    void restoreEdit(OpenDoc& d, const IRect& r);
    void cancelEdit(OpenDoc& d);
    /// These pixels changed: sent to the texture before it is drawn (only them).
    void markDirty(OpenDoc& d, const IRect& r);
    void commit(OpenDoc& d, const std::string& label);
    void commitMeta(OpenDoc& d, const std::string& label, const Json& before);
    void undo(OpenDoc& d);
    void redo(OpenDoc& d);
    void touch(OpenDoc& d);
    void uploadTexture(OpenDoc& d);
    const PixelImage& novaSheet();
    PixelImage suitFromColors(const SuitColors& c);

    // ----------------------------------------------------------------- panels
    void drawTopBar(const Rect& r);
    void drawExplorer(const Rect& r);
    void drawTabs(const Rect& r);
    void drawRail(const Rect& r);
    void drawCanvasPanel(const Rect& r);
    void drawCanvas(const Rect& view);
    void handleCanvasInput(OpenDoc& d, const Rect& view, const Rect& frameOnScreen, float zoom);
    void applyTool(OpenDoc& d, int x, int y, bool pressed, bool held, bool released, bool secondary, bool shift);
    void drawFrise(const Rect& r);
    void drawRight(const Rect& r);
    float drawPalette(const Rect& r, bool measure);
    void drawPicker(const Rect& r);
    void drawEmitter(const Rect& r);
    void drawPreview(const Rect& r);
    void drawEmpty(const Rect& r);
    void handleShortcuts();
    void mirror(OpenDoc& d, bool vertical);
    void dropSelection(OpenDoc& d);
    void setColor(int which, Px c);

    // ----------------------------------------------------------------- import
    void openImport(const fs::path& file);
    void scanImportFolder(bool force);
    void selectImportFile(size_t i);
    void recut();
    void doImport();
    std::vector<DocAnim> importAnims() const;
    DocKind importKind() const;
    int importCount() const;
    std::vector<PixelImage> rowFrames(size_t row) const;
    void drawImport();
    void drawImportView(const Rect& view);
    void drawRecolor();
    void drawConfirmClose();
    void drawLibrary();

    // ----------------------------------------------------------------- library
    void loadLibrary();
    const std::vector<LibraryPalette>& palettes();
    PixelImage modelImage(const LibraryModel& m);
    /// A frame drawn by the game (a frame of the catalogue `catalogLook`, or a created `look`)
    /// into an image of `side` × `side` (render target, read back).
    PixelImage renderFrameImage(const std::string& catalogLook, const CreationLook* look, int side);
    PixelImage renderRing(const CreationLook& look, int side);

    // ----------------------------------------------------------------- helpers
    /// `accent` (alpha 0: none): the colour of an unselected chip that needs attention.
    bool chip(const std::string& name, const Rect& r, const std::string& text, bool selected, bool enabled = true,
              Color accent = Color::transparent());
    bool smallButton(const std::string& name, const Rect& r, const std::string& text, bool enabled = true, bool primary = false, bool danger = false);
    bool check(const std::string& name, const Rect& r, const std::string& text, bool& value);
    bool slider(const std::string& name, const Rect& r, const std::string& label, int& value, int minV, int maxV, int step,
                const std::string& valueText);
    /// A tooltip for the widget `name` at `r` (shown while hovered a moment, or focused with the
    /// keyboard / gamepad), drawn last.
    void tip(const std::string& name, const Rect& r, const std::string& title, const std::string& detail = {});
    void drawTip();
    void drawSection(const Rect& r);
    float sectionTitle(const std::string& t, float x, float y, float w);
    void drawChecker(const Rect& r, float cell);
    void drawSwatch(const Rect& r, Px c, bool selected = false);
    TexturePtr textureOf(const PixelImage& img, const std::string& name);
    void drawNovaFrame(const Texture& sheet, const DocAnim& anim, int frame, const Rect& dst, Color tint = Color::white(), bool flip = false);
    int frameAt(const DocAnim& a, float t, bool loop = true) const;

    GameServices& g_;
    UIContext& ui_;
    Hooks hooks_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    OnlineUi online_;

    const Json* serverList_ = nullptr;  // GET /admin/creations, kept by AdminCreations
    const Json& serverList() const {
        static const Json empty = Json::array();
        return serverList_ ? *serverList_ : empty;
    }
    bool serverLoaded_ = false;
    std::map<int, CreationImageInfo> adminImages_;  // GET /admin/creations/images, by creation
    std::vector<StudioDoc> drafts_;        // the drafts of this computer
    std::vector<StudioDoc> archived_;      // the drafts put away (studio/archives)
    bool draftsLoaded_ = false;
    std::vector<std::unique_ptr<OpenDoc>> docs_;
    int active_ = -1;
    std::vector<DocAnim> novaAnims_;
    PixelImage novaSheet_;
    TexturePtr novaTex_;
    bool novaTried_ = false;

    // Explorer.
    std::string search_;
    int selKind_ = 0;          // the kind of the selected group (« + Nouvelle »)
    std::string selKey_;       // the selected entry: "d:<uid>" (draft) or "s:<id>" (server)
    bool collapsed_[5] = {false, true, true, true, false};
    bool showReserve_[4] = {false, false, false, false};
    bool showArchives_ = false;
    bool renaming_ = false;
    std::string renameText_;
    bool focusRename_ = false;

    // Tools and colours.
    Tool tool_ = Tool::Pencil;
    Px color1_ = Px::rgb(62, 230, 214), color2_ = Px::rgb(234, 242, 255);
    int editColor_ = 0;  // the colour the picker edits (0, 1)
    float hue_ = 174.0f, sat_ = 0.73f, val_ = 0.9f;
    std::string hex_;
    Px hexShown_;
    int dragPicker_ = 0;  // 1: the square, 2: the hue bar
    bool mirrorMenu_ = false;
    Rect mirrorButton_, mirrorMenuRect_;
    int paletteSource_ = 0;
    // A stroke or a shape being drawn.
    bool stroking_ = false;
    bool strokeSecondary_ = false;
    int strokeX0_ = 0, strokeY0_ = 0, lastX_ = -1, lastY_ = -1;
    PixelImage strokeBefore_;
    IRect editRegion_;
    IRect shapeRect_;  // what the shape being dragged covered on the last frame
    bool editWhole_ = true;
    bool editOpen_ = false;
    Json metaBefore_;
    bool panning_ = false;
    int cursorX_ = 0, cursorY_ = 0;  // the gamepad's / keyboard's cursor on the canvas (frame pixels)
    bool padHeld_ = false;
    float triggerL_ = 0.0f, triggerR_ = 0.0f;
    int hoverX_ = -1, hoverY_ = -1;
    Rect canvasView_;
    // Emitter sliders: a change committed when the slider is let go.
    int dragSlider_ = -1;
    Json sliderBefore_;
    std::string sliderLabel_;

    // Windows.
    int confirmClose_ = -1;  // the tab whose closing waits for an answer
    bool recolorOpen_ = false;
    int recolorScope_ = 0;   // 0 every frame, 1 this animation, 2 this frame
    int recolorMode_ = 0;    // 0 one colour by another, 1 a suit's three colours
    Px recolorFrom_, recolorTo_;
    SuitColors recolorSuit_;
    bool libraryOpen_ = false;
    int libraryTab_ = 0;
    std::vector<LibraryModel> models_;
    std::vector<LibraryPalette> palettes_;
    bool libraryLoaded_ = false;

    // Import.
    bool importOpen_ = false;
    std::vector<ImportFile> importFiles_;
    size_t importSel_ = 0;
    float importScanT_ = -100.0f;
    std::vector<std::string> seenFiles_;
    PixelImage importImg_;
    TexturePtr importTex_;
    std::string importProblem_;
    bool importKeyed_ = false, importHasAlpha_ = true, importTexKeyed_ = false;
    bool skipEmpty_ = true;
    int importFrame_ = 16;  // a particle's or a ring's frames (pixels)
    std::vector<IRect> excluded_;
    bool importPreviewDirty_ = true;
    std::vector<PixelImage> importPreviewFrames_;
    TexturePtr importPreviewTex_;
    int cutMode_ = 0;  // 0 automatic, 1 grid, 2 by hand
    SliceOptions slice_;
    GridSpec grid_;
    Anchor anchor_ = Anchor::BottomCenter;
    int scale_ = 100;  // the zones' scale (percent; shrunk anyway when larger than a frame)
    std::vector<std::vector<IRect>> rows_;
    std::vector<IRect> manual_;
    std::vector<int> rowTarget_;  // per row: an animation's index (-1: left out)
    int importTarget_ = -1;       // the open creation that receives (-1: a new one)
    int importNewKind_ = 0;
    int previewRow_ = 0;
    bool dragZone_ = false;
    Vec2 dragFrom_;
    // The keyboard's / gamepad's cursor on the imported image, and a first corner set with it.
    int zoneCursorX_ = 0, zoneCursorY_ = 0;
    bool padCorner_ = false;
    int cornerX_ = 0, cornerY_ = 0;
    std::string importMsg_;
    int dialog_ = -1;

    // Tooltip of this frame.
    std::string tipKey_, tipTitle_, tipDetail_;
    Rect tipRect_;
    std::string tipHover_;
    float tipSince_ = 0.0f;
    bool tipSeen_ = false;

    // The frise and the live preview.
    float friseScroll_ = 0.0f;
    float previewClock_ = 0.0f, previewLastP_ = 0.0f;
    std::vector<TrailSample> previewSamples_;
    uint32_t previewSeed_ = 0;
    TrailFx previewTrail_;

    std::string status_;
    bool statusError_ = false;
    bool sending_ = false;
    float clock_ = -1.0f, dt_ = 0.0f;
    std::string wantOpen_;
    bool sessionLoaded_ = false;
};

}  // namespace studio
}  // namespace cosmic
