#pragma once
// Thumbnails and previews of the assets of the catalogue (2.63, game/content/AssetCatalog.h):
// ADMINISTRATION › Assets, and the Studio de Créations as its library of models.
//
//  · AssetImageLoader: the thumbnails (an image fitted to a square, the first frame of an
//    animation) and the whole pictures (an image, an animation's sheet). Files are read and
//    decoded on its own worker thread, the most recently asked first, and only those asked
//    during the last frame (the visible ones: a list that scrolls away drops its requests);
//    the main thread only turns decoded pictures into textures, a few per frame (pump()). The
//    textures are kept in LRU caches.
//        loader.pump(g.app.renderer());                      // once per frame
//        if (TexturePtr t = loader.thumbnail(*cat, entry)) …  // nullptr while it is read
//  · AssetPreview: the preview of one asset in a rectangle: an image fitted on a checkerboard,
//    an animation that plays (its clip, its frames per second, pause), a sound or a music heard
//    (play / stop, on the audio system's preview track: the menu's music is paused for a music
//    and only lowered for a sound, then given back), a font's specimen, the beginning of a text
//    file. Its controls are keyboard, gamepad and mouse widgets of the UIContext.
//        preview.show(cat, &entry);          // when the selection changes
//        preview.draw(rect, dt);             // each frame
// Read only: nothing is ever written to the assets.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"
#include "game/content/AssetCatalog.h"

namespace cosmic {

class GameServices;
class Renderer;
struct AnimationSet;

class AssetImageLoader {
public:
    /// `thumbnails` and `pictures`: textures kept (LRU); `thumbPx`: the side of a thumbnail.
    explicit AssetImageLoader(size_t thumbnails = 192, size_t pictures = 4, int thumbPx = 128);
    /// Never waits for the worker: it finishes the picture it decodes alone, then stops.
    ~AssetImageLoader();
    AssetImageLoader(const AssetImageLoader&) = delete;
    AssetImageLoader& operator=(const AssetImageLoader&) = delete;

    /// The thumbnail of an image or an animation (nullptr for the other types). Missing, it is
    /// asked to the worker and nullptr is returned until it is ready: ask each frame for the
    /// visible ones only.
    TexturePtr thumbnail(const AssetCatalog& cat, const AssetEntry& e);
    /// The whole picture of an image file (an animation: ask for its `sheet`).
    TexturePtr picture(const AssetCatalog& cat, const std::string& imagePath);
    /// A font's specimen (rendered on the main thread: a font file is small and TrueType is
    /// used from this thread only), `px` pixels high, wrapped at `wrap` pixels (0: one line).
    TexturePtr fontSample(Renderer& r, const AssetCatalog& cat, const AssetEntry& e, const std::string& text, int px, int wrap = 0);
    /// Once per frame, main thread: decoded pictures become textures (at most `budget`), the
    /// requests not repeated since the previous frame are dropped.
    void pump(Renderer& r, int budget = 6);
    /// The file could not be read as a picture.
    bool failed(const std::string& path) const;
    size_t thumbnailsCached() const { return thumbs_.size(); }
    size_t pending() const;
    int thumbPx() const { return thumbPx_; }

    /// Internal: what the worker and the main thread share (requests, decoded pictures).
    struct Shared;

private:
    static void work(std::shared_ptr<Shared> s);
    void request(const std::string& key, const fs::path& file, const std::string& animJson, bool thumb);

    std::shared_ptr<Shared> shared_;
    LruCache<std::string, TexturePtr> thumbs_;
    LruCache<std::string, TexturePtr> pictures_;
    std::unordered_map<std::string, uint64_t> asked_;  // key → the frame it was last asked
    uint64_t frame_ = 1;
    int thumbPx_;
};

class AssetPreview {
public:
    explicit AssetPreview(GameServices& g);
    /// Its sound stops, the music is given back.
    ~AssetPreview();
    AssetPreview(const AssetPreview&) = delete;
    AssetPreview& operator=(const AssetPreview&) = delete;

    /// The asset shown (nullptr: none). Another one stops the sound of the previous one.
    void show(const std::shared_ptr<const AssetCatalog>& cat, const AssetEntry* e);
    const AssetEntry* shown() const { return entry_; }
    /// The stage (checkerboard, picture…) and, under it, its controls, inside `r`.
    void draw(const Rect& r, float dt);
    /// Height of the row of controls under the stage (0 when the asset has none).
    float controlsHeight() const;
    /// Stops the sound or the music being heard (and gives the menu's music back).
    void stopAudio();
    bool playing() const;
    AssetImageLoader& images() { return images_; }

    /// Internal: a piece of work done off the main thread (an animation or a text read, a
    /// sound loaded), never waited for.
    struct Job;

private:
    void drawStage(const Rect& r);
    void drawControls(const Rect& r);
    void startAudio();
    void updateAudio();
    void restoreMusic();

    GameServices& g_;
    AssetImageLoader images_;
    std::shared_ptr<const AssetCatalog> cat_;
    const AssetEntry* entry_ = nullptr;
    // Animation
    std::shared_ptr<Job> animJob_;
    std::shared_ptr<AnimationSet> anim_;
    int clip_ = 0;
    int fps_ = 8;
    float animT_ = 0.0f;
    bool animPaused_ = false;
    std::vector<std::string> clipNames_;
    // Text
    std::shared_ptr<Job> textJob_;
    std::vector<std::string> lines_;
    bool textCut_ = false;
    // Audio
    std::shared_ptr<Job> audioJob_;
    bool audioStarted_ = false;   // the preview track was started for this asset
    bool pausedMusic_ = false;    // the menu's music was paused for it
    bool ducked_ = false;         // the music was lowered for it
    float duckBefore_ = 1.0f;
    std::string audioError_;
};

}  // namespace cosmic
