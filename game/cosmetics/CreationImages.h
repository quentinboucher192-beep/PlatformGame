#pragma once

// 2.61 Studio de Créations: a creation of Cosmic Online may have an image (a PNG sheet) besides
// its parameters (SuitRecolor.h, CreationLooks.h):
//   - a suit: Nova's whole sheet, cut as hers (320 × 864, frames of 32 × 32);
//   - a trail or a victory effect: an animated particle (its frames side by side, the particle's
//     life played from the first to the last) and its emitter (ImageEmitter);
//   - an avatar frame: a ring drawn around the avatar (square frames side by side, looped).
// The server keeps the same rules (server/src/creationAssets.ts): anything else read from it, or
// from the files kept on this computer, is left out, never guessed. Display only: nothing here
// touches a run, a replay or a record. When the image is missing, the category's simple look
// (the creation's parameters) is drawn.

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "engine/rendering/Texture.h"

namespace cosmic {

class Renderer;

/// Nova's sheet (sprites/player/nova.png, animations/nova.json): a suit's image has the size of
/// its first 27 rows (2.69: the 3 rows of the strikes upwards and downwards come after them; a
/// suit created online plays the forward strike instead: Player::attackClip).
constexpr int kSuitSheetW = 320, kSuitSheetH = 864, kSuitFrame = 32;
/// A particle (trail, victory effect): frames of 4..64 pixels, 1..16 of them side by side.
constexpr int kParticleFrameMin = 4, kParticleFrameMax = 64, kImageFramesMax = 16;
/// An avatar frame: square frames of 32..128 pixels.
constexpr int kRingFrameMin = 32, kRingFrameMax = 128;
/// The frames per second of an animated image (a particle's are played over its life instead).
constexpr int kImageFpsMin = 1, kImageFpsMax = 30;
/// At most this many bytes of PNG (the server refuses more).
constexpr size_t kCreationImageMaxBytes = 512 * 1024;

/// How a trail or a victory effect throws its particles (server: EMITTER_BOUNDS).
struct ImageEmitter {
    int rate = 40;      // particles per second (1..120)
    int lifeMs = 650;   // a particle's life (100..1000 ms)
    int size = 100;     // percent of the frame's size in pixels of the game (25..400)
    int speed = 30;     // pixels per second (0..400)
    int spread = 25;    // degrees around its direction (0..360)
    int gravity = -10;  // pixels per second² (-600..600; negative: it rises)
    int spin = 90;      // degrees per second (-720..720)
    int fade = 1;       // 0: none, 1: at the end, 2: all along its life
    bool sparkles = true;

    bool operator==(const ImageEmitter&) const = default;
    Json toJson() const;
    /// nullopt when a field is missing, of the wrong type or out of its bounds.
    static std::optional<ImageEmitter> fromJson(const Json& j);
    /// The same values clamped to their bounds (the editor's sliders).
    ImageEmitter clamped() const;
};

/// Bounds of each setting of the emitter, by its JSON name (the editor's sliders, the tests).
struct EmitterBound {
    const char* key;
    int min, max;
};
const std::vector<EmitterBound>& emitterBounds();

/// Why the layout of an image of `kind` is refused ("" = accepted): the size of the whole
/// image, of a frame, the frame count and the frames per second (ignored for a suit).
std::string imageLayoutProblem(const std::string& kind, int width, int height, int frameW, int frameH, int frames, int fps);

/// What the header of a PNG says (its signature, then the IHDR chunk, its CRC checked).
struct PngHeader {
    int width = 0, height = 0;
    int bitDepth = 0, colorType = 0;
    bool alpha = false;  // an alpha channel or a tRNS chunk
};
/// false (and `why`, in French) when `data` is not a PNG this game reads.
bool readPngHeader(const uint8_t* data, size_t size, PngHeader& out, std::string* why = nullptr);
/// Decodes a PNG (or any image SDL_image reads) into RGBA8 rows.
bool decodeImage(const uint8_t* data, size_t size, int& w, int& h, std::vector<uint8_t>& rgba);
/// Encodes RGBA8 rows as a PNG (empty on failure).
std::vector<uint8_t> encodePng(int w, int h, const std::vector<uint8_t>& rgba);

/// An image of a creation, as GET /creations/images (or /admin/creations/images) lists it.
struct CreationImageInfo {
    std::string ref;   // "srvsuit:<n>", "srvtrail:<n>", "srvframe:<n>", "srvvictory:<n>"
    std::string kind;  // "suit", "trail", "frame", "victory"
    std::string hash;  // SHA-256 of the PNG's bytes (64 hexadecimal digits)
    int width = 0, height = 0, frameW = 0, frameH = 0, frames = 1, fps = 12;
    std::optional<ImageEmitter> emitter;  // a trail, a victory effect
    std::string updatedAt;

    int creationId() const;
    /// "/api/v1/creations/<id>/image/<hash>": where the server gives it (cached by its hash).
    std::string path() const;
    static std::optional<CreationImageInfo> fromJson(const Json& j);
    Json toJson() const;
};

/// What the drawing code needs of an image: its texture (ready) and its layout.
struct CreationImageDraw {
    TexturePtr texture;
    CreationImageInfo info;
};
/// Where the drawing code finds the image of a reference (GameServices: the images known and
/// downloaded; nullopt while it is not ready: the simple look is drawn meanwhile).
using CreationImageResolver = std::function<std::optional<CreationImageDraw>(const std::string& ref)>;
void setCreationImageResolver(CreationImageResolver resolver);
std::optional<CreationImageDraw> resolveCreationImage(const std::string& ref);

/// The images of the creations this game knows (the list kept on this computer, user folder
/// online/creation_images/list.json) and their files (online/creation_images/<hash>.png): drawn
/// offline after one download. A file is read and decoded off the main thread; a download is
/// checked (its SHA-256, its PNG header, its size) before it is kept.
class CreationImageBook {
public:
    /// Downloads `path` (a GET of the server, with the session); `done(ok, bytes)`.
    using Fetch = std::function<void(const std::string& path, std::function<void(bool, std::vector<uint8_t>)> done)>;

    ~CreationImageBook();
    void setDir(fs::path dir) { dir_ = std::move(dir); }
    void setFetch(Fetch f) { fetch_ = std::move(f); }
    void load();
    bool save() const;
    /// `images` of GET /creations/images replaces what is known; true when something changed.
    bool applyList(const Json& images);
    const CreationImageInfo* find(const std::string& ref) const;
    const std::vector<CreationImageInfo>& all() const { return list_; }
    /// The texture of the image of `ref` when it is ready; otherwise what is missing starts
    /// (a file read, a download: never waited for) and nullptr comes back.
    TexturePtr texture(const std::string& ref, Renderer& renderer);
    /// Files decoded meanwhile become textures (main thread); called by texture() too.
    void update(Renderer& renderer);
    /// Requests under way (file reads and downloads).
    size_t busy() const;
    /// The file of an image (whether or not it is on disk).
    fs::path fileOf(const std::string& hash) const { return dir_ / (hash + ".png"); }
    /// Checks downloaded bytes against `info` ("" = good).
    static std::string checkBytes(const CreationImageInfo& info, const std::vector<uint8_t>& bytes);

private:
    struct Decoded {
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
    };
    struct Slot {
        std::string hash;
        TexturePtr texture;
        std::future<std::optional<Decoded>> decoding;
        bool downloading = false;
        bool failed = false;
        uint64_t retryAtMs = 0;
    };
    void startDecode(Slot& slot, const CreationImageInfo& info);

    fs::path dir_;
    Fetch fetch_;
    std::vector<CreationImageInfo> list_;
    std::unordered_map<std::string, Slot> slots_;  // by reference
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
