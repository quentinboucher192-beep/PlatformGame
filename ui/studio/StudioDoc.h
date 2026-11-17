#pragma once

// 2.61 Studio de Créations: a creation being drawn — its kind, its name, its parameters (those
// the server keeps: a suit's three colours, a trail's style...), its image and how it is cut in
// frames, its palette, and where it comes from (a draft of this computer, a creation of the
// server). A draft is kept in the user folder, studio/brouillons/<uid>/ (doc.json, image.png).

#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/cosmetics/CreationImages.h"
#include "game/cosmetics/CreationLooks.h"
#include "game/cosmetics/SuitRecolor.h"
#include "ui/studio/StudioImage.h"

namespace cosmic::studio {

enum class DocKind { Suit, Trail, Frame, Victory };
constexpr int kDocKinds = 4;
/// "suit", "trail", "frame", "victory" (the server's names).
const char* kindId(DocKind k);
std::optional<DocKind> kindFromId(const std::string& id);
/// « Combinaison », « Traînée », « Cadre », « Effet de victoire ».
const char* kindName(DocKind k);
/// « combinaison », « traînée », « cadre », « effet de victoire » (inside a sentence).
const char* kindWord(DocKind k);
/// An animation that throws particles (a trail, a victory effect).
inline bool particleKind(DocKind k) { return k == DocKind::Trail || k == DocKind::Victory; }

/// An animation of the game a creation's frames belong to: for a suit, one of Nova's (a row of
/// her sheet, as animations/nova.json describes it); otherwise the creation's own strip.
struct DocAnim {
    std::string id;    // "run"... ("particle", "ring")
    std::string name;  // « Course »...
    int row = 0;
    int count = 1;
    std::vector<float> durations;  // seconds per frame (a suit: the game's own rhythm)
};
/// Nova's animations, from animations/nova.json (in the order of her sheet's rows).
std::vector<DocAnim> suitAnims(const Json& novaJson);
/// « Attente », « Course »... (the French name of one of Nova's animations; the id otherwise).
std::string animName(const std::string& clipId);

struct StudioDoc {
    std::string uid;  // its draft's folder
    DocKind kind = DocKind::Suit;
    std::string name;
    // On the server (0: never sent).
    int serverId = 0;
    std::string serverUpdatedAt;
    std::string serverImageHash;
    bool archived = false;
    // Its parameters (the simple look drawn while its image is not there).
    SuitColors colors{{220, 230, 240}, {80, 220, 255}, {230, 60, 60}};
    CreationLook look;
    // Its image: a suit's whole sheet; a strip of frames side by side otherwise.
    PixelImage image;
    int frameW = kSuitFrame, frameH = kSuitFrame;
    int fps = 12;
    ImageEmitter emitter;
    std::vector<Px> palette;
    std::string savedAt;  // the draft's last save on this computer (ISO, UTC)

    /// The animations shown in the frise (`novaAnims`: suitAnims() for a suit).
    std::vector<DocAnim> anims(const std::vector<DocAnim>& novaAnims) const;
    /// The frames of a strip (not a suit).
    int stripFrames() const { return frameW > 0 ? image.w() / frameW : 0; }
    /// Where frame `index` of animation `anim` is in the image.
    IRect frameRect(const DocAnim& anim, int index) const;
    /// How many frames of `anim` hold something.
    int drawnFrames(const DocAnim& anim) const;
    /// A frame added to the strip after `at` (a copy of frame `at` when `copy`), or removed.
    bool insertFrame(int at, bool copy);
    bool removeFrame(int at);
    /// The settings undo / redo keeps with the image (frame size, rhythm, emitter, palette).
    Json metaJson() const;
    void applyMeta(const Json& m);
    /// Why it cannot be sent to the server ("" = it can).
    std::string sendProblem(const std::vector<DocAnim>& novaAnims) const;
    /// The palette from its image's most used colours (at most `max`).
    void paletteFromImage(size_t max = 15);

    Json toJson() const;
    static std::optional<StudioDoc> fromJson(const Json& j, std::string* why = nullptr);
    bool saveDraft(const fs::path& dir, std::string* why = nullptr);
    static std::optional<StudioDoc> loadDraft(const fs::path& dir, std::string* why = nullptr);

    /// A new creation of `kind`: a suit from Nova's sheet (`novaSheet`), a trail or victory
    /// effect from a particle (`particle`, its frames side by side; 4 empty frames of 16 × 16
    /// when empty), a frame from `ring` (an empty one of 64 × 64 when empty).
    static StudioDoc make(DocKind kind, const std::string& uid, const std::string& name, const PixelImage& start, int frames = 1);
};

/// The first `n` characters (code points) of a UTF-8 text.
std::string firstChars(const std::string& s, size_t n);
/// A new draft folder's name: "b" + date + time (UTC) + 4 random hexadecimal digits.
std::string newDraftUid();
/// "2026-11-17T14:32:05Z" (now, UTC).
std::string nowUtcIso();

}  // namespace cosmic::studio
