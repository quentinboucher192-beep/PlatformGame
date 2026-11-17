// 2.61 Studio de Créations (see Studio.h): the windows over the Studio — « Importer une image »
// (the folder « Images à importer », a file dropped or chosen, its cut in zones, the animation
// each row goes to, an animated preview), « Recolorer toutes les images… », the closing of a
// creation not saved, and the library (the game's sprites as models, the palettes).

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "ui/screens/ChallengeUi.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/studio/Studio.h"
#include "ui/studio/StudioStyle.h"
#include "ui/widgets/FileDialogs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::studio {

using challenge_ui::fitLabel;
using challenge_ui::fz;
using namespace style;

namespace {

constexpr uintmax_t kImportMaxBytes = 8u << 20;  // a file to import: 8 Mo at most
constexpr int kImportMaxSide = 2048;

fs::path importDir() { return Paths::userDir() / "Images à importer"; }

std::string sizeText(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " o";
    if (bytes < 1024 * 1024) return std::to_string((bytes + 512) / 1024) + " Ko";
    const uintmax_t tenths = (bytes * 10 + 512 * 1024) / (1024 * 1024);
    return std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " Mo";
}

/// The frame sizes offered for a particle and for a ring.
const int kParticleSizes[] = {8, 12, 16, 24, 32, 48, 64};
const int kRingSizes[] = {32, 48, 64, 96, 128};

/// The quick colours of « Couleurs de base » (as ADMINISTRATION › Créations offers them).
const Px kBase[] = {Px::rgb(240, 244, 250), Px::rgb(180, 190, 210), Px::rgb(80, 90, 120),  Px::rgb(35, 35, 45),   Px::rgb(220, 40, 60),
                    Px::rgb(255, 150, 50),  Px::rgb(255, 215, 90),  Px::rgb(150, 230, 80), Px::rgb(60, 180, 100), Px::rgb(50, 200, 180),
                    Px::rgb(90, 230, 255),  Px::rgb(60, 120, 240),  Px::rgb(150, 80, 255), Px::rgb(240, 120, 200), Px::rgb(140, 90, 70),
                    Px::rgb(140, 20, 48)};

/// The most present colours of an image, close shades counted together (16 at most).
std::vector<Px> dominantColors(const PixelImage& img, size_t max) {
    std::map<uint32_t, std::array<uint64_t, 4>> buckets;  // r, g, b sums and the count
    for (int y = 0; y < img.h(); ++y)
        for (int x = 0; x < img.w(); ++x) {
            const Px c = img.get(x, y);
            if (c.a < 128) continue;
            const uint32_t k = (static_cast<uint32_t>(c.r >> 4) << 8) | (static_cast<uint32_t>(c.g >> 4) << 4) | (c.b >> 4);
            auto& b = buckets[k];
            b[0] += c.r, b[1] += c.g, b[2] += c.b, b[3] += 1;
        }
    std::vector<std::array<uint64_t, 4>> list;
    for (auto& [k, b] : buckets) list.push_back(b);
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a[3] > b[3]; });
    std::vector<Px> out;
    for (const auto& b : list) {
        if (out.size() >= max) break;
        out.push_back(Px::rgb(static_cast<uint8_t>(b[0] / b[3]), static_cast<uint8_t>(b[1] / b[3]), static_cast<uint8_t>(b[2] / b[3])));
    }
    return out;
}

PixelImage loadAsset(GameServices& g, const std::string& rel) {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (auto p = g.app.resources().resolve(rel); p && loadRgbaImage(*p, px, w, h)) return PixelImage::fromRgba(w, h, std::move(px));
    return {};
}

}  // namespace

// ------------------------------------------------------------------ the library

PixelImage Studio::renderRing(const CreationLook& look, int side) { return renderFrameImage("", &look, side); }

PixelImage Studio::renderFrameImage(const std::string& catalogLook, const CreationLook* look, int side) {
    Renderer& r = g_.app.renderer();
    PixelImage out(side, side);
    TexturePtr target = r.createTarget(side, side, false);
    if (!target) return out;
    SDL_Rect saved{};
    const bool clipped = SDL_RenderClipEnabled(r.sdl());
    if (clipped) SDL_GetRenderClipRect(r.sdl(), &saved);
    r.setTarget(target);
    SDL_SetRenderClipRect(r.sdl(), nullptr);
    SDL_SetRenderDrawColor(r.sdl(), 0, 0, 0, 0);
    SDL_RenderClear(r.sdl());
    // The game's own drawing of the frame, as large as the image holds it.
    const float rad = side * 0.5f / (1.0f + kAvatarFrameMargin / 40.0f);
    drawAvatarFrame(r, {side * 0.5f, side * 0.5f}, rad, catalogLook, rad / 40.0f, 0.0f, look);
    SDL_Surface* s = SDL_RenderReadPixels(r.sdl(), nullptr);
    r.setTarget(nullptr);
    if (clipped) SDL_SetRenderClipRect(r.sdl(), &saved);
    if (!s) return out;
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!conv) return out;
    if (SDL_LockSurface(conv)) {
        for (int y = 0; y < std::min(side, conv->h); ++y)
            for (int x = 0; x < std::min(side, conv->w); ++x) {
                const uint8_t* p = static_cast<const uint8_t*>(conv->pixels) + y * conv->pitch + x * 4;
                // Back from the blending onto a transparent target (straight alpha again).
                Px c{p[0], p[1], p[2], p[3]};
                if (c.a > 0 && c.a < 255) {
                    c.r = static_cast<uint8_t>(std::min(255, c.r * 255 / c.a));
                    c.g = static_cast<uint8_t>(std::min(255, c.g * 255 / c.a));
                    c.b = static_cast<uint8_t>(std::min(255, c.b * 255 / c.a));
                }
                out.set(x, y, c);
            }
        SDL_UnlockSurface(conv);
    }
    SDL_DestroySurface(conv);
    return out;
}

void Studio::loadLibrary() {
    libraryLoaded_ = true;
    models_.clear();
    models_.push_back({"Nova", "planche de Nova · 320 × 864", DocKind::Suit, "sprites/player/nova.png", ""});
    for (const CosmeticItem* i : g_.cosmetics.items("suit"))
        if (!i->look.empty() && i->look != "standard" && !i->serverSuit())
            models_.push_back({i->name, "combinaison du jeu", DocKind::Suit, "sprites/player/suits/nova_" + i->look + ".png", ""});
    struct Fx {
        const char* name;
        const char* file;
    };
    const Fx fx[] = {{"Étincelle", "spark.png"}, {"Étoile", "star.png"},      {"Particule", "particle.png"}, {"Poussière", "dust.png"},
                     {"Fumée", "smoke.png"},     {"Impact", "hit.png"},       {"Explosion", "explosion.png"}, {"Téléportation", "teleport.png"}};
    for (const Fx& f : fx) models_.push_back({f.name, "particule du jeu", DocKind::Trail, std::string("textures/fx/") + f.file, ""});
    models_.push_back({"Anneau", "anneau du jeu · 64 × 64", DocKind::Frame, "textures/fx/ring.png", ""});
    for (const CosmeticItem* i : g_.cosmetics.items("frame"))
        if (!i->look.empty() && !i->serverLook()) models_.push_back({i->name, "cadre du jeu", DocKind::Frame, "", i->look});
    // The palettes: Nova's, each planet's (its landscapes), the base colours.
    palettes_.clear();
    palettes_.push_back({"Nova", dominantColors(novaSheet(), 16)});
    for (const PlanetData* p : g_.content.planets()) {
        PixelImage img;
        for (const std::string& rel : {"backgrounds/" + p->id + "/mid.png", "backgrounds/" + p->id + "/far.png", p->illustration, p->banner})
            if (!rel.empty() && img.empty()) img = loadAsset(g_, rel);
        if (img.empty()) continue;
        palettes_.push_back({p->name, dominantColors(img, 16)});
    }
    palettes_.push_back({"Couleurs de base", std::vector<Px>(std::begin(kBase), std::end(kBase))});
}

const std::vector<LibraryPalette>& Studio::palettes() { return palettes_; }

PixelImage Studio::modelImage(const LibraryModel& m) {
    if (!m.look.empty()) return renderFrameImage(m.look, nullptr, 64);
    return loadAsset(g_, m.asset);
}

void Studio::newFromModel(const LibraryModel& m, DocKind kind) {
    PixelImage img = modelImage(m);
    if (img.empty()) return hooks_.say("Modèle illisible : " + m.name + ".", true);
    StudioDoc d;
    if (kind == DocKind::Suit) {
        d = StudioDoc::make(DocKind::Suit, newDraftUid(), firstChars(m.name, 20) + " (modèle)", img);
    } else if (kind == DocKind::Frame) {
        // A ring: square frames side by side, resized to a size the game accepts.
        PixelImage ring(64, 64);
        ring.paste(fitZone(img, img.bounds(), 64, 64, Anchor::Center, 100), 0, 0);
        d = StudioDoc::make(DocKind::Frame, newDraftUid(), firstChars(m.name, 20) + " (modèle)", ring);
    } else {
        // A particle: its frames (square, side by side) in frames of 4 to 64 pixels.
        const int side = std::clamp(img.h(), kParticleFrameMin, kParticleFrameMax);
        const int n = std::clamp(img.w() / std::max(1, img.h()), 1, kImageFramesMax);
        PixelImage strip(side * n, side);
        for (int i = 0; i < n; ++i) strip.paste(fitZone(img, {i * img.h(), 0, img.h(), img.h()}, side, side, Anchor::Center, 100), i * side, 0);
        d = StudioDoc::make(kind, newDraftUid(), firstChars(m.name, 20) + " (modèle)", strip);
    }
    d.look = defaultCreationLook(kindId(kind == DocKind::Suit ? DocKind::Trail : kind));
    openDoc(std::move(d), true);
    libraryOpen_ = false;
    hooks_.say("Nouvelle création à partir de « " + m.name + " » : pas encore enregistrée.", false);
}

void Studio::drawLibrary() {
    Renderer& rr = ui_.r();
    const float W = std::min(ui_.screenW() - ui_.s(120), ui_.s(1100)), H = std::min(ui_.screenH() - ui_.s(100), ui_.s(700));
    const Rect box{(ui_.screenW() - W) * 0.5f, (ui_.screenH() - H) * 0.5f, W, H};
    rr.fillRect({0, 0, ui_.screenW(), ui_.screenH()}, Color{2, 4, 12, 170});
    rr.fillRoundedRect(box, ui_.s(16), kPanel);
    rr.drawRoundedRect(box, ui_.s(16), kAccent, std::max(1.5f, ui_.s(2)));
    ui_.pushId("library");
    const float pad = ui_.s(22);
    float y = box.y + pad;
    const float x = box.x + pad, w = box.w - 2 * pad;
    fitLabel(ui_, "BIBLIOTHÈQUE", {x, y + ui_.s(16)}, 26, w * 0.4f, kText, Align::Left, FontFace::Title);
    const std::vector<std::string> tabs = {"Sprites du jeu (modèles)", "Palettes", "Images importées"};
    float tx = x + w * 0.4f;
    for (int i = 0; i < 3; ++i) {
        const float cw = ui_.text().measure(tabs[static_cast<size_t>(i)], FontFace::Bold, ui_.s(fz(ui_, 14))).x + ui_.s(26);
        if (chip("t" + std::to_string(i), {tx, y + ui_.s(4), cw, ui_.s(28)}, tabs[static_cast<size_t>(i)], libraryTab_ == i)) libraryTab_ = i;
        tx += cw + ui_.s(8);
    }
    y += ui_.s(48);
    const float bh = ui_.s(40);
    const Rect list{x, y, w, box.bottom() - pad - bh - ui_.s(12) - y};
    OpenDoc* d = active();
    if (libraryTab_ == 0) {
        // Cards: the model, its name, what it starts.
        const float cardW = ui_.s(240), cardH = ui_.s(150), gap = ui_.s(12);
        const int cols = std::max(1, static_cast<int>((list.w + gap) / (cardW + gap)));
        const float cw = (list.w - gap * (cols - 1) - ui_.s(14)) / cols;
        const int rows = (static_cast<int>(models_.size()) + cols - 1) / cols;
        const LibraryModel* pick = nullptr;
        DocKind pickKind = DocKind::Suit;
        ui_.beginScroll("models", list, rows * (cardH + gap));
        const float so = ui_.scrollOffset();
        for (size_t i = 0; i < models_.size(); ++i) {
            LibraryModel& m = models_[i];
            const Rect card{list.x + (i % cols) * (cw + gap), list.y - so + (i / cols) * (cardH + gap), cw, cardH};
            if (card.bottom() < list.y || card.y > list.bottom()) {
                ui_.hotspot("m" + std::to_string(i), card);
                continue;
            }
            rr.fillRoundedRect(card, ui_.s(10), kField);
            rr.drawRoundedRect(card, ui_.s(10), ui_.focused("m" + std::to_string(i)) ? kAccent : kBorder, std::max(1.0f, ui_.s(1)));
            const Rect thumb{card.x + ui_.s(10), card.y + ui_.s(10), ui_.s(84), ui_.s(84)};
            drawChecker(thumb, ui_.s(8));
            if (!m.thumb) {
                PixelImage img = modelImage(m);
                // A suit's thumbnail: Nova standing (the first frame of « Attente »).
                if (m.kind == DocKind::Suit && !img.empty()) img = img.crop({0, 0, 32, 32});
                m.thumb = textureOf(img, "<modèle " + m.name + ">");
            }
            if (m.thumb) {
                const float k = std::min(thumb.w / m.thumb->width(), thumb.h / m.thumb->height());
                rr.drawTexture(*m.thumb, nullptr, Rect::fromCenter(thumb.center(), {m.thumb->width() * k, m.thumb->height() * k}));
            }
            const float tx2 = thumb.right() + ui_.s(10);
            fitLabel(ui_, m.name, {tx2, card.y + ui_.s(22)}, 15, card.right() - ui_.s(8) - tx2, kText, Align::Left, FontFace::Bold);
            fitLabel(ui_, m.detail, {tx2, card.y + ui_.s(44)}, 12.5f, card.right() - ui_.s(8) - tx2, kDim);
            const float by = card.bottom() - ui_.s(42);
            if (m.kind == DocKind::Trail) {
                const float half = (cw - ui_.s(26)) * 0.5f;
                if (smallButton("mt" + std::to_string(i), {card.x + ui_.s(10), by, half, ui_.s(32)}, "Nouvelle traînée")) pick = &m, pickKind = DocKind::Trail;
                if (smallButton("mv" + std::to_string(i), {card.x + ui_.s(16) + half, by, half, ui_.s(32)}, "Nouvel effet")) pick = &m, pickKind = DocKind::Victory;
            } else {
                if (smallButton("m" + std::to_string(i), {card.x + ui_.s(10), by, cw - ui_.s(20), ui_.s(32)},
                                m.kind == DocKind::Suit ? "Nouvelle combinaison" : "Nouveau cadre"))
                    pick = &m, pickKind = m.kind;
            }
        }
        ui_.endScroll();
        if (pick) {
            const LibraryModel chosen = *pick;
            newFromModel(chosen, pickKind);
            ui_.popId();
            return;
        }
    } else if (libraryTab_ == 1) {
        const float rowH = ui_.s(54), gap = ui_.s(8);
        ui_.beginScroll("pals", list, palettes_.size() * (rowH + gap));
        const float so = ui_.scrollOffset();
        for (size_t i = 0; i < palettes_.size(); ++i) {
            const LibraryPalette& p = palettes_[i];
            const Rect row{list.x, list.y - so + i * (rowH + gap), list.w - ui_.s(14), rowH};
            rr.fillRoundedRect(row, ui_.s(10), kField);
            fitLabel(ui_, p.name, {row.x + ui_.s(12), row.center().y}, 15, ui_.s(150), kText, Align::Left, FontFace::Bold);
            const float bw = ui_.s(200), bw2 = ui_.s(150);
            const float sx = row.x + ui_.s(170), sw = std::clamp((row.w - ui_.s(190) - bw - bw2 - ui_.s(20)) / 16.0f - ui_.s(3), ui_.s(6), ui_.s(26));
            for (size_t k = 0; k < p.colors.size(); ++k) drawSwatch({sx + k * (sw + ui_.s(3)), row.center().y - ui_.s(12), sw, ui_.s(24)}, p.colors[k]);
            if (smallButton("use_" + std::to_string(i), {row.right() - bw - bw2 - ui_.s(16), row.y + ui_.s(10), bw2, rowH - ui_.s(20)}, "Afficher")) {
                paletteSource_ = static_cast<int>(i) + 1;
                libraryOpen_ = false;
            }
            if (smallButton("add" + std::to_string(i), {row.right() - bw - ui_.s(8), row.y + ui_.s(10), bw, rowH - ui_.s(20)}, "Ajouter à la création", d != nullptr) && d) {
                const Json before = d->doc.metaJson();
                for (const Px& c : p.colors)
                    if (d->doc.palette.size() < 31 && std::find(d->doc.palette.begin(), d->doc.palette.end(), c) == d->doc.palette.end()) d->doc.palette.push_back(c);
                commitMeta(*d, "Palette « " + p.name + " »", before);
                paletteSource_ = 0;
                libraryOpen_ = false;
            }
        }
        ui_.endScroll();
    } else {
        // The images of the folder (and those dropped or chosen in this session).
        scanImportFolder(false);
        const float rowH = ui_.s(64), gap = ui_.s(8);
        ui_.beginScroll("imgs", list, std::max<size_t>(1, importFiles_.size()) * (rowH + gap));
        const float so = ui_.scrollOffset();
        if (importFiles_.empty())
            ui_.labelWrapped("Aucune image : déposez des PNG dans le dossier « Images à importer » (" + Paths::toUtf8(importDir()) + "), ou glissez-en un dans la fenêtre.",
                             {list.x, list.y}, list.w, fz(ui_, 15), kDim);
        for (size_t i = 0; i < importFiles_.size(); ++i) {
            const ImportFile& f = importFiles_[i];
            const Rect row{list.x, list.y - so + i * (rowH + gap), list.w - ui_.s(14), rowH};
            rr.fillRoundedRect(row, ui_.s(10), f.problem.empty() ? kField : kDangerBg);
            const Rect th{row.x + ui_.s(8), row.y + ui_.s(8), ui_.s(70), rowH - ui_.s(16)};
            drawChecker(th, ui_.s(6));
            if (f.thumb) {
                const float k = std::min(th.w / f.thumb->width(), th.h / f.thumb->height());
                rr.drawTexture(*f.thumb, nullptr, Rect::fromCenter(th.center(), {f.thumb->width() * k, f.thumb->height() * k}));
            }
            const float bw = ui_.s(190);
            {
                i18n::Verbatim file;
                fitLabel(ui_, f.name, {th.right() + ui_.s(12), row.y + ui_.s(20)}, 14.5f, row.w - bw - th.w - ui_.s(40), kText, Align::Left, FontFace::Bold);
            }
            fitLabel(ui_, f.problem.empty() ? std::to_string(f.w) + " × " + std::to_string(f.h) + " px · " + sizeText(f.bytes) : f.problem,
                     {th.right() + ui_.s(12), row.y + ui_.s(44)}, 13, row.w - bw - th.w - ui_.s(40), f.problem.empty() ? kSub : kDangerText);
            if (smallButton("imp" + std::to_string(i), {row.right() - bw - ui_.s(8), row.y + ui_.s(14), bw, rowH - ui_.s(28)}, "Découper et importer", f.problem.empty())) {
                libraryOpen_ = false;
                openImport(f.path);
                ui_.endScroll();
                ui_.popId();
                return;
            }
        }
        ui_.endScroll();
    }
    if (smallButton("close", {box.right() - pad - ui_.s(160), box.bottom() - pad - bh, ui_.s(160), bh}, "Fermer") || ui_.backPressed()) libraryOpen_ = false;
    ui_.popId();
}

// ------------------------------------------------------------------ « Images à importer »

void Studio::scanImportFolder(bool force) {
    if (!force && clock_ - importScanT_ < 3.0f) return;
    importScanT_ = clock_;
    std::error_code ec;
    fs::create_directories(importDir(), ec);
    std::vector<ImportFile> next;
    // Dropped or chosen files of this session stay first.
    for (const ImportFile& f : importFiles_)
        if (f.path.parent_path() != importDir()) next.push_back(f);
    for (const auto& e : fs::directory_iterator(importDir(), ec)) {
        if (!e.is_regular_file(ec)) continue;
        ImportFile f;
        f.path = e.path();
        f.name = Paths::toUtf8(e.path().filename());
        f.bytes = e.file_size(ec);
        // Already known (same name and size): kept as it was read.
        bool known = false;
        for (const ImportFile& o : importFiles_)
            if (o.path == f.path && o.bytes == f.bytes) {
                next.push_back(o);
                known = true;
            }
        if (known) continue;
        const std::string ext = FileUtil::extensionLower(f.path);
        if (ext != ".png") {
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif" || ext == ".webp") f.problem = "Pas un PNG transparent : ignorée";
            else continue;  // not an image: not listed
        } else if (f.bytes > kImportMaxBytes) {
            f.problem = "Fichier trop lourd (" + sizeText(f.bytes) + ", 8 Mo au plus)";
        } else if (auto bytes = FileUtil::readBinary(f.path)) {
            PngHeader h;
            std::string why;
            if (!readPngHeader(bytes->data(), bytes->size(), h, &why)) f.problem = why;
            else {
                f.w = h.width;
                f.h = h.height;
                if (h.width > kImportMaxSide || h.height > kImportMaxSide)
                    f.problem = "Image trop grande : " + std::to_string(h.width) + " × " + std::to_string(h.height) + " px (2048 au plus)";
                else {
                    int w = 0, hh = 0;
                    std::vector<uint8_t> rgba;
                    if (decodeImage(bytes->data(), bytes->size(), w, hh, rgba)) f.thumb = textureOf(PixelImage::fromRgba(w, hh, std::move(rgba)), "<import>");
                    else f.problem = "PNG illisible";
                }
            }
        } else {
            f.problem = "Fichier illisible";
        }
        next.push_back(std::move(f));
    }
    for (ImportFile& f : next) f.fresh = std::find(seenFiles_.begin(), seenFiles_.end(), f.name + "|" + std::to_string(f.bytes)) == seenFiles_.end();
    importFiles_ = std::move(next);
}

void Studio::openImport(const fs::path& file) {
    importOpen_ = true;
    importMsg_.clear();
    scanImportFolder(true);
    // The target: the creation open (its kind), else a new suit.
    importTarget_ = active_;
    if (OpenDoc* d = active()) importNewKind_ = static_cast<int>(d->doc.kind);
    if (!file.empty()) {
        std::error_code ec;
        size_t at = importFiles_.size();
        for (size_t i = 0; i < importFiles_.size(); ++i)
            if (importFiles_[i].path == file) at = i;
        if (at == importFiles_.size()) {
            // A file dropped or chosen outside the folder: read and listed first.
            ImportFile f;
            f.path = file;
            f.name = Paths::toUtf8(file.filename());
            f.bytes = fs::file_size(file, ec);
            const std::string ext = FileUtil::extensionLower(file);
            if (ec) f.problem = "Fichier illisible";
            else if (ext != ".png") f.problem = "Pas un PNG transparent : ignorée";
            else if (f.bytes > kImportMaxBytes) f.problem = "Fichier trop lourd (" + sizeText(f.bytes) + ", 8 Mo au plus)";
            importFiles_.insert(importFiles_.begin(), f);
            at = 0;
        }
        selectImportFile(at);
    } else {
        // The newest file of the folder that can be imported.
        for (size_t i = 0; i < importFiles_.size(); ++i)
            if (importFiles_[i].problem.empty()) {
                selectImportFile(i);
                break;
            }
    }
}

void Studio::selectImportFile(size_t i) {
    if (i >= importFiles_.size()) return;
    importSel_ = i;
    ImportFile& f = importFiles_[i];
    importImg_ = PixelImage();
    importTex_.reset();
    importProblem_ = f.problem;
    manual_.clear();
    excluded_.clear();
    if (f.problem.empty()) {
        auto bytes = FileUtil::readBinary(f.path);
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        PngHeader hd;
        std::string why;
        if (!bytes || !readPngHeader(bytes->data(), bytes->size(), hd, &why)) importProblem_ = why.empty() ? "Fichier illisible" : why;
        else if (!decodeImage(bytes->data(), bytes->size(), w, h, rgba)) importProblem_ = "PNG illisible";
        else {
            importImg_ = PixelImage::fromRgba(w, h, std::move(rgba));
            importProblem_ = importProblem(importImg_, kImportMaxSide);
            importHasAlpha_ = hd.alpha;
            importKeyed_ = !hd.alpha;  // no transparency: its plain background is keyed out
            f.w = w;
            f.h = h;
        }
        f.problem = importProblem_;
    }
    // Looked at: no longer « nouvelle ».
    const std::string key = f.name + "|" + std::to_string(f.bytes);
    if (std::find(seenFiles_.begin(), seenFiles_.end(), key) == seenFiles_.end()) {
        seenFiles_.push_back(key);
        if (seenFiles_.size() > 400) seenFiles_.erase(seenFiles_.begin());
        saveSession();
    }
    f.fresh = false;
    recut();
}

std::vector<DocAnim> Studio::importAnims() const {
    const int k = importTarget_ >= 0 && importTarget_ < static_cast<int>(docs_.size()) ? static_cast<int>(docs_[static_cast<size_t>(importTarget_)]->doc.kind)
                                                                                       : importNewKind_;
    if (k == static_cast<int>(DocKind::Suit)) return novaAnims_;
    DocAnim a;
    a.id = k == static_cast<int>(DocKind::Frame) ? "ring" : "particle";
    a.name = k == static_cast<int>(DocKind::Frame) ? "Cadre" : "Particule";
    a.count = kImageFramesMax;
    return {a};
}

DocKind Studio::importKind() const {
    return importTarget_ >= 0 && importTarget_ < static_cast<int>(docs_.size()) ? docs_[static_cast<size_t>(importTarget_)]->doc.kind
                                                                               : static_cast<DocKind>(importNewKind_);
}

void Studio::recut() {
    rows_.clear();
    importPreviewDirty_ = true;
    if (importImg_.empty() || !importProblem_.empty()) return;
    const PixelImage src = importKeyed_ ? keyOutBackground(importImg_) : importImg_;
    if (cutMode_ == 0) {
        std::vector<IRect> zones;
        for (const IRect& z : detectZones(src, slice_))
            if (std::find(excluded_.begin(), excluded_.end(), z) == excluded_.end()) zones.push_back(z);
        rows_ = groupRows(zones);
    } else if (cutMode_ == 1) {
        rows_ = gridRows(src, grid_, skipEmpty_, slice_.alphaThreshold);
    } else {
        rows_ = groupRows(manual_);
    }
    // Each row to an animation: row n to the n-th (a sheet laid out like Nova's falls in place);
    // a particle or a ring: every row, one after the other.
    const auto anims = importAnims();
    rowTarget_.assign(rows_.size(), -1);
    for (size_t i = 0; i < rows_.size(); ++i) rowTarget_[i] = importKind() == DocKind::Suit ? (i < anims.size() ? static_cast<int>(i) : -1) : 0;
    // The size of a particle's or a ring's frames: what holds the largest zone.
    if (importKind() != DocKind::Suit) {
        int side = 0;
        for (const auto& row : rows_)
            for (const IRect& z : row) side = std::max({side, z.w, z.h});
        const bool ring = importKind() == DocKind::Frame;
        importFrame_ = ring ? kRingSizes[std::size(kRingSizes) - 1] : kParticleSizes[std::size(kParticleSizes) - 1];
        if (ring) {
            for (int s : kRingSizes)
                if (s >= side) {
                    importFrame_ = s;
                    break;
                }
        } else {
            for (int s : kParticleSizes)
                if (s >= side) {
                    importFrame_ = s;
                    break;
                }
        }
    }
    previewRow_ = std::clamp(previewRow_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
}

int Studio::importCount() const {
    const auto anims = importAnims();
    if (importKind() != DocKind::Suit) {
        int n = 0;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rowTarget_[i] >= 0) n += static_cast<int>(rows_[i].size());
        return std::min(n, kImageFramesMax);
    }
    int n = 0;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rowTarget_[i] >= 0 && rowTarget_[i] < static_cast<int>(anims.size()))
            n += std::min(static_cast<int>(rows_[i].size()), anims[static_cast<size_t>(rowTarget_[i])].count);
    return n;
}

std::vector<PixelImage> Studio::rowFrames(size_t row) const {
    std::vector<PixelImage> out;
    if (row >= rows_.size()) return out;
    const PixelImage src = importKeyed_ ? keyOutBackground(importImg_) : importImg_;
    const bool suit = importKind() == DocKind::Suit;
    const int fw = suit ? kSuitFrame : importFrame_, fh = fw;
    for (const IRect& z : rows_[row]) out.push_back(fitZone(src, z, fw, fh, suit ? anchor_ : Anchor::Center, scale_ == 0 ? 100 : scale_, slice_.alphaThreshold));
    return out;
}

void Studio::doImport() {
    if (rows_.empty() || importCount() == 0) return;
    if (importTarget_ < 0 || importTarget_ >= static_cast<int>(docs_.size())) {
        newDoc(static_cast<DocKind>(importNewKind_));
        importTarget_ = active_;
    }
    OpenDoc& d = *docs_[static_cast<size_t>(importTarget_)];
    active_ = importTarget_;
    dropSelection(d);
    beginEdit(d);
    const auto anims = importAnims();
    int done = 0, shrunk = 0;
    const PixelImage src = importKeyed_ ? keyOutBackground(importImg_) : importImg_;
    if (d.doc.kind == DocKind::Suit) {
        for (size_t r = 0; r < rows_.size(); ++r) {
            const int t = rowTarget_[r];
            if (t < 0 || t >= static_cast<int>(anims.size())) continue;
            const DocAnim& a = anims[static_cast<size_t>(t)];
            for (int i = 0; i < std::min(a.count, static_cast<int>(rows_[r].size())); ++i) {
                bool s = false;
                const PixelImage f = fitZone(src, rows_[r][static_cast<size_t>(i)], kSuitFrame, kSuitFrame, anchor_, scale_ == 0 ? 100 : scale_, slice_.alphaThreshold, &s);
                const IRect fr = d.doc.frameRect(a, i);
                d.doc.image.paste(f, fr.x, fr.y);
                ++done;
                shrunk += s ? 1 : 0;
            }
        }
    } else {
        std::vector<PixelImage> frames;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rowTarget_[r] >= 0)
                for (const IRect& z : rows_[r]) {
                    if (static_cast<int>(frames.size()) >= kImageFramesMax) break;
                    bool s = false;
                    frames.push_back(fitZone(src, z, importFrame_, importFrame_, Anchor::Center, scale_ == 0 ? 100 : scale_, slice_.alphaThreshold, &s));
                    shrunk += s ? 1 : 0;
                }
        if (frames.empty()) {
            editOpen_ = false;
            return;
        }
        PixelImage strip(importFrame_ * static_cast<int>(frames.size()), importFrame_);
        for (size_t i = 0; i < frames.size(); ++i) strip.paste(frames[i], static_cast<int>(i) * importFrame_, 0);
        d.doc.frameW = d.doc.frameH = importFrame_;
        d.doc.image = std::move(strip);
        d.frame = 0;
        done = static_cast<int>(frames.size());
    }
    if (d.doc.palette.empty()) d.doc.paletteFromImage();
    const std::string file = importSel_ < importFiles_.size() ? importFiles_[importSel_].name : std::string("image");
    commit(d, "Importer « " + file + " »");
    importOpen_ = false;
    hooks_.say(std::to_string(done) + " images importées dans « " + d.doc.name + " »" +
                   (shrunk > 0 ? " (" + std::to_string(shrunk) + " réduites pour tenir)" : std::string()) + " : Ctrl+Z annule l'import en entier.",
               false);
}

void Studio::drawImport() {
    Renderer& rr = ui_.r();
    InputManager& in = ui_.input();
    // The file dialog's answer (SDL3, asynchronous).
    if (dialog_ >= 0)
        if (auto res = FileDialogs::result(dialog_)) {
            dialog_ = -1;
            if (!res->empty()) openImport(Paths::fromUtf8(*res));
        }
    rr.fillRect({0, 0, ui_.screenW(), ui_.screenH()}, Color{2, 4, 12, 190});
    const float mx = std::min(ui_.s(60), ui_.screenW() * 0.03f), my = std::min(ui_.s(40), ui_.screenH() * 0.04f);
    const Rect box{mx, my, ui_.screenW() - 2 * mx, ui_.screenH() - 2 * my};
    rr.fillRoundedRect(box, ui_.s(16), kPanel);
    rr.drawRoundedRect(box, ui_.s(16), kAccent, std::max(1.5f, ui_.s(2)));
    ui_.pushId("import");
    const float pad = ui_.s(24);
    const float x = box.x + pad, w = box.w - 2 * pad;
    float y = box.y + ui_.s(20);
    // Title, and where the images go (a click: the next creation open, or a new one).
    const float titleW = ui_.text().measure("IMPORTER UNE IMAGE", FontFace::Title, ui_.s(26)).x;
    ui_.label("IMPORTER UNE IMAGE", {x, y + ui_.s(16)}, 26, kText, Align::Left, FontFace::Title, VAlign::Middle);
    {
        std::string into;
        if (importTarget_ >= 0 && importTarget_ < static_cast<int>(docs_.size())) {
            const StudioDoc& t = docs_[static_cast<size_t>(importTarget_)]->doc;
            into = "dans « " + t.name + " » · " + kindWord(t.kind);
        } else {
            into = std::string("dans une nouvelle création · ") + kindWord(static_cast<DocKind>(importNewKind_));
        }
        const float iw = std::min(w - titleW - ui_.s(20), ui_.text().measure(into, FontFace::Body, ui_.s(fz(ui_, 15))).x + ui_.s(30));
        const Rect ir{x + titleW + ui_.s(16), y + ui_.s(4), iw, ui_.s(26)};
        bool hovered = false;
        if (ui_.hotspot("into", ir, &hovered)) {
            // The open creations, then a new one of each kind.
            const int n = static_cast<int>(docs_.size());
            int pos = importTarget_ >= 0 ? importTarget_ : n + importNewKind_;
            pos = (pos + 1) % (n + kDocKinds);
            if (pos < n) importTarget_ = pos;
            else {
                importTarget_ = -1;
                importNewKind_ = pos - n;
            }
            recut();
        }
        {
            i18n::Verbatim name;
            fitLabel(ui_, into, {ir.x + ui_.s(4), ir.center().y}, 15, ir.w - ui_.s(22), hovered || ui_.focused("into") ? kAccent : kDim);
        }
        rr.fillTriangle({ir.right() - ui_.s(12), ir.center().y - ui_.s(2)}, {ir.right() - ui_.s(4), ir.center().y - ui_.s(2)}, {ir.right() - ui_.s(8), ir.center().y + ui_.s(3)}, kDim);
        tip("into", ir, "Où vont les images", "Clic : une autre création ouverte, ou une nouvelle (combinaison, traînée, cadre, effet de victoire).");
    }
    y += ui_.s(44);
    const float bh = ui_.s(44);
    const float bottomY = box.bottom() - ui_.s(20) - bh;
    const float colGap = ui_.s(16);
    const float leftW = std::clamp(ui_.s(280), 200.0f, w * 0.22f), rightW = std::clamp(ui_.s(320), 230.0f, w * 0.26f);
    const Rect left{x, y, leftW, bottomY - ui_.s(14) - y};
    const Rect mid{x + leftW + colGap, y, w - leftW - rightW - 2 * colGap, left.h};
    const Rect right{mid.right() + colGap, y, rightW, left.h};

    // ---- left: the images found.
    {
        float ly = left.y;
        ly += sectionTitle("IMAGES DÉTECTÉES", left.x, ly, left.w);
        const std::string help = "Dossier « Images à importer » du jeu, surveillé : déposez-y vos PNG, ou glissez un fichier dans la fenêtre.";
        const float hh = ui_.text().measureWrapped(help, FontFace::Body, ui_.s(fz(ui_, 13)), left.w).y;
        ui_.labelWrapped(help, {left.x, ly}, left.w, fz(ui_, 13), kDim);
        ly += hh + ui_.s(8);
        const float chooseH = ui_.s(40);
        const Rect list{left.x, ly, left.w, left.bottom() - chooseH - ui_.s(10) - ly};
        const float rowH = ui_.s(60), gap = ui_.s(8);
        ui_.beginScroll("files", list, std::max<size_t>(1, importFiles_.size()) * (rowH + gap));
        const float so = ui_.scrollOffset();
        if (importFiles_.empty())
            ui_.labelWrapped("Le dossier est vide pour l'instant :\n" + Paths::toUtf8(importDir()), {list.x, list.y}, list.w, fz(ui_, 13), kSub);
        const bool scrolls = importFiles_.size() * (rowH + gap) > list.h;
        for (size_t i = 0; i < importFiles_.size(); ++i) {
            const ImportFile& f = importFiles_[i];
            const Rect row{list.x, list.y - so + i * (rowH + gap), list.w - (scrolls ? ui_.s(14) : 0.0f), rowH};
            const std::string id = "file_" + std::to_string(i);
            bool hovered = false;
            if (ui_.hotspot(id, row, &hovered)) selectImportFile(i);
            const bool sel = i == importSel_;
            const bool bad = !f.problem.empty();
            rr.fillRoundedRect(row, ui_.s(10), bad ? kDangerBg : sel ? kSel : Color{10, 17, 36, 0});
            rr.drawRoundedRect(row, ui_.s(10), bad ? kDangerEdge : sel ? kAccent : (hovered || ui_.focused(id)) ? kAccent.withAlpha(150) : kBorder,
                               std::max(1.0f, ui_.s(sel ? 2.0f : 1.0f)));
            const Rect th{row.x + ui_.s(8), row.y + ui_.s(8), ui_.s(64), rowH - ui_.s(16)};
            drawChecker(th, ui_.s(8));
            if (f.thumb) {
                const float k = std::min(th.w / f.thumb->width(), th.h / f.thumb->height());
                rr.drawTexture(*f.thumb, nullptr, Rect::fromCenter(th.center(), {f.thumb->width() * k, f.thumb->height() * k}));
            }
            const float tx = th.right() + ui_.s(10), tw = row.right() - ui_.s(8) - tx;
            {
                i18n::Verbatim file;
                fitLabel(ui_, f.name, {tx, row.y + ui_.s(14)}, 14, tw, kText, Align::Left, FontFace::Bold);
            }
            if (bad) {
                fitLabel(ui_, f.problem, {tx, row.y + ui_.s(36)}, 12.5f, tw, kDangerText);
            } else {
                fitLabel(ui_, std::to_string(f.w) + " × " + std::to_string(f.h) + " px · " + sizeText(f.bytes), {tx, row.y + ui_.s(32)}, 12.5f, tw, kSub);
                if (f.fresh) fitLabel(ui_, "nouvelle", {tx, row.y + ui_.s(48)}, 12.5f, tw, kWarm);
            }
            if (bad) tip(id, row, f.name, f.problem);
        }
        ui_.endScroll();
        if (smallButton("choose", {left.x, left.bottom() - chooseH, left.w, chooseH}, "Choisir un autre fichier…") && dialog_ < 0)
            dialog_ = FileDialogs::openFile(g_.app.window(), "Images PNG", "png", Paths::toUtf8(importDir()));
        tip("choose", {left.x, left.bottom() - chooseH, left.w, chooseH}, "Choisir un autre fichier…", "La fenêtre des fichiers du système : un PNG n'importe où sur ce PC.");
    }

    // ---- middle: the cut.
    {
        float cy = mid.y;
        const float chipH = ui_.s(28);
        const float tw = ui_.text().measure("DÉCOUPE", FontFace::Bold, ui_.s(fz(ui_, 14))).x + ui_.s(16);
        fitLabel(ui_, "DÉCOUPE", {mid.x, cy + chipH * 0.5f}, 14, tw, kDim, Align::Left, FontFace::Bold);
        const std::vector<std::string> modes = {"Détection automatique", "Grille régulière", "À la main"};
        float cx = mid.x + tw;
        for (int i = 0; i < 3; ++i) {
            const float cw = ui_.text().measure(modes[static_cast<size_t>(i)], FontFace::Bold, ui_.s(fz(ui_, 14))).x + ui_.s(26);
            if (chip("mode_" + std::to_string(i), {cx, cy, cw, chipH}, modes[static_cast<size_t>(i)], cutMode_ == i)) {
                cutMode_ = i;
                recut();
            }
            cx += cw + ui_.s(8);
        }
        int zones = 0;
        for (const auto& row : rows_) zones += static_cast<int>(row.size());
        const std::string found = !importProblem_.empty() ? std::string() : std::to_string(zones) + " images trouvées en " + std::to_string(rows_.size()) +
                                                                                (rows_.size() > 1 ? " rangées" : " rangée");
        if (!found.empty() && mid.right() - cx - ui_.s(8) > ui_.s(80))
            fitLabel(ui_, found, {mid.right(), cy + chipH * 0.5f}, 14, mid.right() - cx - ui_.s(8), kAccent, Align::Right);
        cy += chipH + ui_.s(10);
        // The settings of the cut (measured, on as many lines as they need), under the image.
        struct Item {
            std::string id, text;
            int kind;  // 0 a value with − +, 1 a chip, 2 a check, 3 plain text
            int* value;
            int minV, maxV, step;
        };
        std::vector<Item> items;
        const int pct = static_cast<int>(std::lround(slice_.alphaThreshold * 100.0 / 255.0));
        if (cutMode_ == 0) {
            items.push_back({"thr", "Seuil de transparence : " + std::to_string(pct) + " %", 0, &slice_.alphaThreshold, 0, 250, 5});
            items.push_back({"margin", "Marge autour des zones : " + std::to_string(slice_.margin) + " px", 0, &slice_.margin, 0, 16, 1});
        } else if (cutMode_ == 1) {
            items.push_back({"cw", "Largeur : " + std::to_string(grid_.cellW) + " px", 0, &grid_.cellW, 4, 512, 1});
            items.push_back({"ch", "Hauteur : " + std::to_string(grid_.cellH) + " px", 0, &grid_.cellH, 4, 512, 1});
            items.push_back({"ox", "Décalage : " + std::to_string(grid_.offsetX) + " px", 0, &grid_.offsetX, 0, 256, 1});
            items.push_back({"gx", "Espacement : " + std::to_string(grid_.gapX) + " px", 0, &grid_.gapX, 0, 64, 1});
        } else {
            items.push_back({"hand", "Glisser pour tracer une zone · clic sur une zone : la retirer", 3, nullptr, 0, 0, 0});
        }
        if (importKind() == DocKind::Suit)
            items.push_back({"anchor", anchor_ == Anchor::BottomCenter ? "Point d'ancrage : sous les pieds, au centre" : "Point d'ancrage : au centre", 1, nullptr, 0, 0, 0});
        else
            items.push_back({"fsize", "Taille d'une image : " + std::to_string(importFrame_) + " × " + std::to_string(importFrame_) + " px", 1, nullptr, 0, 0, 0});
        items.push_back({"scale", "Taille : " + std::to_string(scale_) + " %", 1, nullptr, 0, 0, 0});
        if (!importHasAlpha_ && !importImg_.empty()) items.push_back({"key", "Fond uni → transparent", 2, nullptr, 0, 0, 0});
        if (cutMode_ == 1) items.push_back({"skip", "Ignorer les cases vides", 2, nullptr, 0, 0, 0});
        if (cutMode_ == 0) items.push_back({"zone_click", "Clic sur une zone : la retirer ou la reprendre", 3, nullptr, 0, 0, 0});
        // Their lines, measured from the bottom of the middle column.
        const float itemH = ui_.s(28), igap = ui_.s(10);
        std::vector<float> widths;
        for (const Item& it : items) {
            const float tw2 = ui_.text().measure(it.text, FontFace::Body, ui_.s(fz(ui_, 13))).x;
            widths.push_back(it.kind == 0 ? tw2 + 2 * ui_.s(26) + ui_.s(16) : it.kind == 2 ? tw2 + ui_.s(34) : it.kind == 1 ? tw2 + ui_.s(24) : tw2);
        }
        int lines = 1;
        float lx = 0;
        for (float wv : widths) {
            if (lx > 0 && lx + wv > mid.w) {
                ++lines;
                lx = 0;
            }
            lx += std::min(wv, mid.w) + igap;
        }
        const float settingsY = mid.bottom() - lines * (itemH + ui_.s(6));
        const Rect view{mid.x, cy, mid.w, settingsY - ui_.s(10) - cy};
        drawImportView(view);
        float sx = mid.x, sy = settingsY;
        bool changed = false;
        for (size_t i = 0; i < items.size(); ++i) {
            Item& it = items[i];
            const float iw = std::min(widths[i], mid.w);
            if (sx > mid.x && sx + iw > mid.right()) {
                sx = mid.x;
                sy += itemH + ui_.s(6);
            }
            const Rect ir{sx, sy, iw, itemH};
            if (it.kind == 0) {
                const float b = ui_.s(26);
                if (smallButton(it.id + "-", {ir.x, ir.y + (itemH - b) * 0.5f, b, b}, "−", *it.value > it.minV)) *it.value = std::max(it.minV, *it.value - it.step), changed = true;
                fitLabel(ui_, it.text, {ir.x + b + ui_.s(8), ir.center().y}, 13, iw - 2 * b - ui_.s(16), kSub);
                if (smallButton(it.id + "+", {ir.right() - b, ir.y + (itemH - b) * 0.5f, b, b}, "+", *it.value < it.maxV)) *it.value = std::min(it.maxV, *it.value + it.step), changed = true;
                if (it.id == "cw" || it.id == "ox" || it.id == "gx") {
                    // The grid is square by default: height follows width until it is set, offsets and gaps both ways.
                    if (it.id == "ox") grid_.offsetY = grid_.offsetX;
                    if (it.id == "gx") grid_.gapY = grid_.gapX;
                }
            } else if (it.kind == 1) {
                if (chip(it.id, ir, it.text, false)) {
                    changed = true;
                    if (it.id == "anchor") anchor_ = anchor_ == Anchor::BottomCenter ? Anchor::Center : Anchor::BottomCenter;
                    if (it.id == "scale") scale_ = scale_ == 100 ? 50 : scale_ == 50 ? 200 : 100;
                    if (it.id == "fsize") {
                        const bool ring = importKind() == DocKind::Frame;
                        const int* sizes = ring ? kRingSizes : kParticleSizes;
                        const size_t n = ring ? std::size(kRingSizes) : std::size(kParticleSizes);
                        size_t at = 0;
                        for (size_t k = 0; k < n; ++k)
                            if (sizes[k] == importFrame_) at = k;
                        importFrame_ = sizes[(at + 1) % n];
                        importPreviewDirty_ = true;
                        changed = false;
                    }
                }
                tip(it.id, ir, it.text, it.id == "anchor" ? "Où chaque zone se pose dans son image de 32 × 32 : sous les pieds (comme Nova) ou au centre."
                                     : it.id == "scale"   ? "Clic : 100 %, 50 %, 200 % (au pixel près, sans flou) ; une zone plus grande qu'une image est réduite pour tenir."
                                                          : "Clic : la taille suivante.");
            } else if (it.kind == 2) {
                bool& v = it.id == "key" ? importKeyed_ : skipEmpty_;
                if (check(it.id, ir, it.text, v)) changed = true;
            } else {
                fitLabel(ui_, it.text, {ir.x, ir.center().y}, 13, iw, kDim);
            }
            sx += iw + igap;
        }
        if (changed) recut();
    }

    // ---- right: where each row goes, and the preview.
    {
        float ry = right.y;
        ry += sectionTitle("CHAQUE RANGÉE VA DANS…", right.x, ry, right.w);
        const auto anims = importAnims();
        const bool suit = importKind() == DocKind::Suit;
        std::vector<std::string> options;
        for (const DocAnim& a : anims) options.push_back(a.name);
        options.push_back("Ignorée");
        const float listH = std::max(ui_.s(120), right.h * 0.45f);
        const Rect list{right.x, ry, right.w, listH};
        // Each row: its count, its animation (a click: the next one), a warning when needed.
        std::vector<std::string> warns(rows_.size());
        for (size_t i = 0; i < rows_.size(); ++i) {
            const int t = rowTarget_[i];
            if (t < 0 || t >= static_cast<int>(anims.size())) continue;
            const int n = static_cast<int>(rows_[i].size());
            if (suit && n > anims[static_cast<size_t>(t)].count)
                warns[i] = "« " + anims[static_cast<size_t>(t)].name + " » du jeu a " + std::to_string(anims[static_cast<size_t>(t)].count) + " images : seules les " +
                           std::to_string(anims[static_cast<size_t>(t)].count) + " premières seront prises.";
            else if (suit && n < anims[static_cast<size_t>(t)].count)
                warns[i] = "« " + anims[static_cast<size_t>(t)].name + " » a " + std::to_string(anims[static_cast<size_t>(t)].count) + " images : les " +
                           std::to_string(anims[static_cast<size_t>(t)].count - n) + " dernières gardent leur dessin.";
            for (size_t j = 0; j < i; ++j)
                if (rowTarget_[j] == t && suit) warns[i] = "« " + anims[static_cast<size_t>(t)].name + " » reçoit déjà la rangée " + std::to_string(j + 1) + " : celle-ci la remplace.";
        }
        if (!suit) {
            int total = 0;
            for (size_t i = 0; i < rows_.size(); ++i)
                if (rowTarget_[i] >= 0) total += static_cast<int>(rows_[i].size());
            if (total > kImageFramesMax && !rows_.empty()) warns.back() = "16 images au plus : seules les 16 premières seront prises.";
        }
        float content = 0.0f;
        const float warnW = right.w - ui_.s(36);
        for (size_t i = 0; i < rows_.size(); ++i)
            content += ui_.s(38) + (warns[i].empty() ? 0.0f : ui_.text().measureWrapped(warns[i], FontFace::Body, ui_.s(fz(ui_, 12.5f)), warnW).y + ui_.s(6)) + ui_.s(8);
        ui_.beginScroll("rows", list, content);
        float yy = list.y - ui_.scrollOffset();
        const bool scrolls = content > list.h;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const float wh = warns[i].empty() ? 0.0f : ui_.text().measureWrapped(warns[i], FontFace::Body, ui_.s(fz(ui_, 12.5f)), warnW).y + ui_.s(6);
            const Rect row{list.x, yy, list.w - (scrolls ? ui_.s(14) : 0.0f), ui_.s(38) + wh};
            yy += row.h + ui_.s(8);
            const Color rc = kRowColors[i % 6];
            rr.drawRoundedRect(row, ui_.s(8), warns[i].empty() ? (static_cast<int>(i) == previewRow_ ? rc : kBorder) : kWarm, std::max(1.0f, ui_.s(static_cast<int>(i) == previewRow_ ? 2.0f : 1.0f)));
            const std::string label = "Rangée " + std::to_string(i + 1) + " · " + std::to_string(rows_[i].size()) + " images";
            const std::string id = "row" + std::to_string(i);
            const Rect head{row.x, row.y, row.w, ui_.s(38)};
            if (ui_.mouseClick("rowsel" + std::to_string(i), {head.x, head.y, head.w * 0.55f, head.h})) {
                previewRow_ = static_cast<int>(i);
                importPreviewDirty_ = true;
            }
            fitLabel(ui_, label, {row.x + ui_.s(10), head.center().y}, 14, row.w * 0.55f - ui_.s(14), kText);
            int idx = rowTarget_[i] < 0 ? static_cast<int>(options.size()) - 1 : rowTarget_[i];
            const std::string target = options[static_cast<size_t>(idx)];
            const Rect tr{row.x + row.w * 0.55f, head.y + ui_.s(5), row.w * 0.45f - ui_.s(8), head.h - ui_.s(10)};
            bool hovered = false;
            if (ui_.hotspot(id, tr, &hovered)) {
                idx = (idx + (in.shift() ? static_cast<int>(options.size()) - 1 : 1)) % static_cast<int>(options.size());
                rowTarget_[i] = idx == static_cast<int>(options.size()) - 1 ? -1 : idx;
                previewRow_ = static_cast<int>(i);
                importPreviewDirty_ = true;
            }
            if (hovered || ui_.focused(id)) rr.fillRoundedRect(tr, ui_.s(6), kSel);
            fitLabel(ui_, target, {tr.right() - ui_.s(16), tr.center().y}, 14, tr.w - ui_.s(22), rowTarget_[i] < 0 ? kDim : kAccent, Align::Right, FontFace::Bold);
            rr.fillTriangle({tr.right() - ui_.s(12), tr.center().y - ui_.s(2)}, {tr.right() - ui_.s(4), tr.center().y - ui_.s(2)}, {tr.right() - ui_.s(8), tr.center().y + ui_.s(3)},
                            kAccent);
            tip(id, tr, "Animation de la rangée " + std::to_string(i + 1), "Clic : la suivante (Maj : la précédente) · « Ignorée » : pas importée.");
            rr.fillCircle({row.x + ui_.s(4), head.center().y}, ui_.s(3), rc);
            if (!warns[i].empty()) ui_.labelWrapped(warns[i], {row.x + ui_.s(10), head.bottom() - ui_.s(4)}, warnW, fz(ui_, 12.5f), kWarm);
        }
        ui_.endScroll();
        ry += listH + ui_.s(12);
        ry += sectionTitle("APERÇU AVANT D'IMPORTER", right.x, ry, right.w);
        const float capH = ui_.s(20);
        const Rect pv{right.x, ry, right.w, right.bottom() - capH - ui_.s(8) - ry};
        rr.fillRoundedRectGradient(pv, ui_.s(10), Color{26, 18, 64, 255}, Color{16, 22, 46, 255});
        std::string caption;
        if (!rows_.empty() && pv.h > ui_.s(40)) {
            const int row = std::clamp(previewRow_, 0, static_cast<int>(rows_.size()) - 1);
            if (importPreviewDirty_) {
                importPreviewDirty_ = false;
                importPreviewFrames_ = rowFrames(static_cast<size_t>(row));
                const int fw = suit ? kSuitFrame : importFrame_;
                PixelImage strip(fw * std::max<int>(1, static_cast<int>(importPreviewFrames_.size())), fw);
                for (size_t k = 0; k < importPreviewFrames_.size(); ++k) strip.paste(importPreviewFrames_[k], static_cast<int>(k) * fw, 0);
                importPreviewTex_ = textureOf(strip, "<aperçu import>");
            }
            const int t = rowTarget_[static_cast<size_t>(row)];
            const int n = static_cast<int>(importPreviewFrames_.size());
            DocAnim a;
            a.count = n;
            float fps = 12.0f;
            if (suit && t >= 0 && t < static_cast<int>(anims.size())) {
                a = anims[static_cast<size_t>(t)];
                a.count = std::min(a.count, n);
                a.durations.resize(static_cast<size_t>(a.count));
                float total = 0.0f;
                for (float dd : a.durations) total += dd;
                fps = total > 0 ? a.count / total : 12.0f;
            } else {
                a.durations.assign(static_cast<size_t>(n), 1.0f / 12.0f);
            }
            if (importPreviewTex_ && a.count > 0) {
                const int f = frameAt(a, clock_);
                const int fw = suit ? kSuitFrame : importFrame_;
                const float k = std::max(1.0f, std::floor(std::min((pv.w - ui_.s(20)) / fw, (pv.h - ui_.s(20)) / fw)));
                const Rect src{static_cast<float>(f * fw), 0, static_cast<float>(fw), static_cast<float>(fw)};
                const Rect dst{pv.center().x - fw * k * 0.5f, pv.bottom() - ui_.s(14) - fw * k, fw * k, fw * k};
                rr.fillRect({pv.x, dst.bottom(), pv.w, std::max(1.0f, ui_.s(2))}, kAccent.withAlpha(90));
                rr.drawTexture(*importPreviewTex_, &src, dst);
            }
            caption = "Rangée " + std::to_string(row + 1) + " → " + (t < 0 ? std::string("ignorée") : options[static_cast<size_t>(t)]) + " · " +
                      std::to_string(static_cast<int>(std::lround(fps))) + " images / s, en boucle";
        } else {
            caption = importProblem_.empty() ? "Choisissez une image à gauche." : importProblem_;
        }
        fitLabel(ui_, caption, {right.x, right.bottom() - capH * 0.5f}, 13, right.w, importProblem_.empty() ? kDim : kDangerText);
    }

    // ---- bottom: what « Importer » does, « Annuler », « Importer N images dans … ».
    {
        const int n = importCount();
        std::string target;
        if (importTarget_ >= 0 && importTarget_ < static_cast<int>(docs_.size())) target = "« " + docs_[static_cast<size_t>(importTarget_)]->doc.name + " »";
        else target = std::string("une nouvelle ") + kindWord(static_cast<DocKind>(importNewKind_));
        const std::string go = "Importer " + std::to_string(n) + (n > 1 ? " images" : " image") + " dans " + target;
        const float gw = std::min(w * 0.5f, ui_.text().measure(go, FontFace::Bold, ui_.s(fz(ui_, 15))).x + ui_.s(40));
        const float cw = ui_.text().measure("Annuler", FontFace::Bold, ui_.s(fz(ui_, 15))).x + ui_.s(40);
        const Rect goR{x + w - gw, bottomY, gw, bh}, cancelR{goR.x - ui_.s(10) - cw, bottomY, cw, bh};
        fitLabel(ui_, "Rien n'est remplacé avant « Importer » ; ensuite, Ctrl+Z annule l'import en entier.", {x, bottomY + bh * 0.5f}, 13, cancelR.x - x - ui_.s(12), kDim);
        if (smallButton("cancel", cancelR, "Annuler") || (ui_.backPressed() && !ui_.editingText())) importOpen_ = false;
        if (smallButton("go", goR, go, n > 0, true)) doImport();
    }
    ui_.popId();
}

void Studio::drawImportView(const Rect& view) {
    Renderer& rr = ui_.r();
    InputManager& in = ui_.input();
    drawChecker(view, ui_.s(12));
    rr.drawRoundedRect(view, ui_.s(10), kBorder, std::max(1.0f, ui_.s(1)));
    if (importImg_.empty()) {
        const std::string t = importProblem_.empty() ? std::string("Aucune image choisie.") : importProblem_;
        const float tw = std::min(view.w - ui_.s(40), ui_.s(520));
        ui_.labelWrapped(t, {view.center().x - tw * 0.5f, view.center().y - ui_.s(20)}, tw, fz(ui_, 16), importProblem_.empty() ? kSub : kDangerText);
        return;
    }
    if (!importTex_) importTex_ = textureOf(importKeyed_ ? keyOutBackground(importImg_) : importImg_, "<import>");
    if (importTexKeyed_ != importKeyed_) {
        importTex_ = textureOf(importKeyed_ ? keyOutBackground(importImg_) : importImg_, "<import>");
        importTexKeyed_ = importKeyed_;
    }
    const float k = std::min((view.w - ui_.s(24)) / importImg_.w(), (view.h - ui_.s(24)) / importImg_.h());
    const float z = k >= 1.0f ? std::floor(k) : k;
    const Rect img{std::round(view.center().x - importImg_.w() * z * 0.5f), std::round(view.center().y - importImg_.h() * z * 0.5f), importImg_.w() * z,
                   importImg_.h() * z};
    if (importTex_) rr.drawTexture(*importTex_, nullptr, img);
    auto toScreen = [&](const IRect& r) { return Rect{img.x + r.x * z, img.y + r.y * z, r.w * z, r.h * z}; };
    const Vec2 m = in.mousePos();
    const int mx = static_cast<int>(std::floor((m.x - img.x) / z)), my = static_cast<int>(std::floor((m.y - img.y) / z));
    // The zones, numbered row by row in the row's colour.
    rr.pushClip(view);
    for (size_t r = 0; r < rows_.size(); ++r) {
        const Color c = kRowColors[r % 6];
        for (size_t i = 0; i < rows_[r].size(); ++i) {
            const Rect zr = toScreen(rows_[r][i]);
            rr.drawRect(zr, rowTarget_[r] < 0 ? c.withAlpha(90) : c, std::max(1.5f, ui_.s(2)));
            const std::string num = std::to_string(i + 1);
            if (zr.w >= ui_.s(12) && zr.h >= ui_.s(12)) ui_.label(num, {zr.x + ui_.s(3), zr.y + ui_.s(1)}, fz(ui_, 11.5f), c, Align::Left, FontFace::Bold);
        }
    }
    // Excluded zones (automatic): dashed.
    for (const IRect& e : excluded_) {
        const Rect zr = toScreen(e);
        for (float t = 0; t < zr.w; t += ui_.s(8)) rr.fillRect({zr.x + t, zr.y, std::min(ui_.s(4), zr.w - t), 1.5f}, kDim);
        for (float t = 0; t < zr.w; t += ui_.s(8)) rr.fillRect({zr.x + t, zr.bottom() - 1.5f, std::min(ui_.s(4), zr.w - t), 1.5f}, kDim);
    }
    // Drawing a zone by hand.
    if (dragZone_) {
        const int x0 = static_cast<int>(std::floor((dragFrom_.x - img.x) / z)), y0 = static_cast<int>(std::floor((dragFrom_.y - img.y) / z));
        rr.drawRect(toScreen(IRect::between(x0, y0, mx, my).intersect(importImg_.bounds())), Color::white(), std::max(1.5f, ui_.s(2)));
    }
    rr.popClip();
    auto zoneAt = [&](int x, int y) -> std::optional<IRect> {
        for (const auto& row : rows_)
            for (const IRect& r : row)
                if (r.contains(x, y)) return r;
        for (const IRect& r : excluded_)
            if (r.contains(x, y)) return r;
        return std::nullopt;
    };
    // The keyboard and the gamepad: a cursor on the image (arrows, D-pad; Maj: 8 pixels; at the
    // edge the focus leaves the image); A / Entrée on a zone: left out or taken back (automatic),
    // removed (by hand); by hand elsewhere: a corner, then the opposite one (a new zone).
    const bool confirmPad = ui_.hotspot("zones", view) && !ui_.usingPointer();
    tip("zones", view, "Zones de l'image",
        "Clic, A ou Entrée sur une zone : la retirer ou la reprendre. À la main : glisser, ou A sur deux coins opposés ; flèches : le curseur.");
    if (ui_.focused("zones") && !ui_.usingPointer()) {
        const int step = in.shift() ? 8 : 1;
        const int dx = (in.menuPressed(Action::MenuRight) ? step : 0) - (in.menuPressed(Action::MenuLeft) ? step : 0);
        const int dy = (in.menuPressed(Action::MenuDown) ? step : 0) - (in.menuPressed(Action::MenuUp) ? step : 0);
        if (dx != 0 && zoneCursorX_ + dx >= 0 && zoneCursorX_ + dx < importImg_.w()) {
            zoneCursorX_ += dx;
            ui_.consumeNavigation(true, false);
        }
        if (dy != 0 && zoneCursorY_ + dy >= 0 && zoneCursorY_ + dy < importImg_.h()) {
            zoneCursorY_ += dy;
            ui_.consumeNavigation(false, true);
        }
        zoneCursorX_ = std::clamp(zoneCursorX_, 0, importImg_.w() - 1);
        zoneCursorY_ = std::clamp(zoneCursorY_, 0, importImg_.h() - 1);
        rr.pushClip(view);
        if (padCorner_)
            rr.drawRect(toScreen(IRect::between(cornerX_, cornerY_, zoneCursorX_, zoneCursorY_)), Color::white(), std::max(1.5f, ui_.s(2)));
        rr.drawRect(toScreen({zoneCursorX_, zoneCursorY_, 1, 1}).expanded(std::max(2.0f, z * 0.5f)), kWarm, std::max(2.0f, ui_.s(2.5f)));
        rr.popClip();
        if (confirmPad) {
            auto hit = zoneAt(zoneCursorX_, zoneCursorY_);
            if (cutMode_ == 0 && hit) {
                auto it = std::find(excluded_.begin(), excluded_.end(), *hit);
                if (it == excluded_.end()) excluded_.push_back(*hit);
                else excluded_.erase(it);
                recut();
            } else if (cutMode_ == 2) {
                if (padCorner_) {
                    padCorner_ = false;
                    const IRect r = IRect::between(cornerX_, cornerY_, zoneCursorX_, zoneCursorY_);
                    if (r.w >= 2 && r.h >= 2) {
                        manual_.push_back(r);
                        recut();
                    }
                } else if (hit) {
                    manual_.erase(std::remove(manual_.begin(), manual_.end(), *hit), manual_.end());
                    recut();
                } else {
                    padCorner_ = true;
                    cornerX_ = zoneCursorX_;
                    cornerY_ = zoneCursorY_;
                }
            }
        }
    }
    if (!ui_.acceptsInput() || !view.contains(m) || !ui_.usingPointer()) {
        if (!in.mouseDown(SDL_BUTTON_LEFT)) dragZone_ = false;
        return;
    }
    // A click on a zone: (automatic) left out or taken back; (by hand) removed. A drag (by hand): a new zone.
    if (in.mousePressed(SDL_BUTTON_LEFT)) {
        auto hit = zoneAt(mx, my);
        if (cutMode_ == 0 && hit) {
            auto it = std::find(excluded_.begin(), excluded_.end(), *hit);
            if (it == excluded_.end()) excluded_.push_back(*hit);
            else excluded_.erase(it);
            recut();
        } else if (cutMode_ == 2) {
            if (hit) {
                manual_.erase(std::remove(manual_.begin(), manual_.end(), *hit), manual_.end());
                recut();
            } else {
                dragZone_ = true;
                dragFrom_ = m;
            }
        }
    }
    if (dragZone_ && in.mouseReleased(SDL_BUTTON_LEFT)) {
        dragZone_ = false;
        const int x0 = static_cast<int>(std::floor((dragFrom_.x - img.x) / z)), y0 = static_cast<int>(std::floor((dragFrom_.y - img.y) / z));
        const IRect r = IRect::between(x0, y0, mx, my).intersect(importImg_.bounds());
        if (r.w >= 2 && r.h >= 2) {
            manual_.push_back(r);
            recut();
        }
    }
}

// ------------------------------------------------------------------ recolour, closing

void Studio::drawRecolor() {
    OpenDoc* dp = active();
    if (!dp) {
        recolorOpen_ = false;
        return;
    }
    OpenDoc& d = *dp;
    Renderer& rr = ui_.r();
    rr.fillRect({0, 0, ui_.screenW(), ui_.screenH()}, Color{2, 4, 12, 170});
    const float W = std::min(ui_.screenW() - ui_.s(80), ui_.s(640)), H = std::min(ui_.screenH() - ui_.s(80), ui_.s(470));
    const Rect box{(ui_.screenW() - W) * 0.5f, (ui_.screenH() - H) * 0.5f, W, H};
    rr.fillRoundedRect(box, ui_.s(16), kPanel);
    rr.drawRoundedRect(box, ui_.s(16), kViolet, std::max(1.5f, ui_.s(2)));
    ui_.pushId("recolor");
    const float pad = ui_.s(22), x = box.x + pad, w = box.w - 2 * pad;
    float y = box.y + pad;
    fitLabel(ui_, "RECOLORER TOUTES LES IMAGES", {x, y + ui_.s(12)}, 20, w, kText, Align::Left, FontFace::Bold);
    y += ui_.s(36);
    const bool suit = d.doc.kind == DocKind::Suit;
    std::vector<std::string> modes = {"Une couleur par une autre"};
    if (suit) modes.push_back("Les trois couleurs d'une combinaison");
    float cx = x;
    for (size_t i = 0; i < modes.size(); ++i) {
        const float cw = ui_.text().measure(modes[i], FontFace::Bold, ui_.s(fz(ui_, 14))).x + ui_.s(26);
        if (chip("mode_" + std::to_string(i), {cx, y, cw, ui_.s(28)}, modes[i], recolorMode_ == static_cast<int>(i))) recolorMode_ = static_cast<int>(i);
        cx += cw + ui_.s(8);
    }
    if (!suit) recolorMode_ = 0;
    y += ui_.s(40);
    // Where: every frame, this animation, this frame.
    const std::vector<std::string> scopes = {"Toutes les images", "Cette animation", "Cette image"};
    cx = x;
    for (size_t i = 0; i < scopes.size(); ++i) {
        const float cw = ui_.text().measure(scopes[i], FontFace::Bold, ui_.s(fz(ui_, 13))).x + ui_.s(24);
        if (chip("scope" + std::to_string(i), {cx, y, cw, ui_.s(26)}, scopes[i], recolorScope_ == static_cast<int>(i))) recolorScope_ = static_cast<int>(i);
        cx += cw + ui_.s(8);
    }
    y += ui_.s(40);
    // The rectangles changed.
    std::vector<IRect> areas;
    const auto anims = animsOf(d);
    if (recolorScope_ == 2) areas.push_back(currentFrame(d));
    else if (recolorScope_ == 1 && d.doc.kind == DocKind::Suit && !anims.empty()) {
        const DocAnim& a = anims[static_cast<size_t>(std::clamp(d.anim, 0, static_cast<int>(anims.size()) - 1))];
        areas.push_back({0, a.row * kSuitFrame, a.count * kSuitFrame, kSuitFrame});
    } else {
        areas.push_back(d.doc.image.bounds());
    }
    std::string summary;
    if (recolorMode_ == 0) {
        // The colours of the creation (the most used first): the one to replace.
        const auto counts = colorCounts(d.doc.image, areas.front());
        fitLabel(ui_, "Remplacer (les couleurs de l'image, la plus présente d'abord) :", {x, y + ui_.s(8)}, 13.5f, w, kSub);
        y += ui_.s(22);
        const float sw = ui_.s(28), sg = ui_.s(6);
        const int perRow = std::max(1, static_cast<int>((w + sg) / (sw + sg)));
        for (size_t i = 0; i < std::min<size_t>(counts.size(), static_cast<size_t>(perRow) * 2); ++i) {
            const Rect s{x + (i % perRow) * (sw + sg), y + (i / perRow) * (sw + sg), sw, sw};
            const std::string id = "from" + std::to_string(i);
            if (ui_.hotspot(id, s)) recolorFrom_ = counts[i].first;
            drawSwatch(s, counts[i].first, counts[i].first == recolorFrom_ || ui_.focused(id));
            tip(id, s, hexOf(counts[i].first), std::to_string(counts[i].second) + " pixels");
        }
        y += (counts.size() > static_cast<size_t>(perRow) ? 2 : 1) * (sw + sg) + ui_.s(8);
        const float lw = ui_.text().measure("par la couleur 1 :", FontFace::Body, ui_.s(fz(ui_, 13.5f))).x + ui_.s(10);
        fitLabel(ui_, "par la couleur 1 :", {x, y + ui_.s(14)}, 13.5f, lw, kSub);
        recolorTo_ = color1_;
        drawSwatch({x + lw, y, ui_.s(60), ui_.s(28)}, recolorTo_);
        fitLabel(ui_, hexOf(recolorTo_) + " (choisie avec le sélecteur ou la palette)", {x + lw + ui_.s(70), y + ui_.s(14)}, 13, w - lw - ui_.s(70), kDim);
        y += ui_.s(40);
        int n = 0;
        for (const IRect& a : areas)
            for (int py = a.y; py < a.bottom(); ++py)
                for (int px = a.x; px < a.right(); ++px) n += d.doc.image.get(px, py) == recolorFrom_ && recolorFrom_.a > 0 ? 1 : 0;
        summary = n == 0 ? "Choisissez la couleur à remplacer." : std::to_string(n) + " pixels changeront.";
    } else {
        fitLabel(ui_, "Comme les combinaisons du jeu : chaque pixel garde sa lumière, prend la teinte de la combinaison, de l'accent ou de l'écharpe (clic : la couleur 1).",
                 {x, y + ui_.s(8)}, 13, w, kSub);
        y += ui_.s(26);
        const char* names[3] = {"Combinaison", "Accent", "Écharpe"};
        Rgb8* cols[3] = {&recolorSuit_.suit, &recolorSuit_.accent, &recolorSuit_.scarf};
        const float cw = (w - 2 * ui_.s(10)) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const Rect s{x + i * (cw + ui_.s(10)), y, cw, ui_.s(56)};
            const std::string id = std::string("suit_") + std::to_string(i);
            bool hovered = false;
            if (ui_.hotspot(id, s, &hovered)) *cols[i] = {color1_.r, color1_.g, color1_.b};
            rr.fillRoundedRect(s, ui_.s(8), kField);
            rr.drawRoundedRect(s, ui_.s(8), hovered || ui_.focused(id) ? kAccent : kBorder, std::max(1.0f, ui_.s(1)));
            drawSwatch({s.x + ui_.s(8), s.y + ui_.s(8), ui_.s(40), s.h - ui_.s(16)}, Px::rgb(cols[i]->r, cols[i]->g, cols[i]->b));
            fitLabel(ui_, names[i], {s.x + ui_.s(56), s.y + ui_.s(18)}, 13.5f, s.w - ui_.s(60), kText, Align::Left, FontFace::Bold);
            fitLabel(ui_, hexOf(Px::rgb(cols[i]->r, cols[i]->g, cols[i]->b)), {s.x + ui_.s(56), s.y + ui_.s(38)}, 12.5f, s.w - ui_.s(60), kDim);
            tip(id, s, names[i], "Clic : prendre la couleur 1 (" + hexOf(color1_) + ")");
        }
        y += ui_.s(68);
        summary = "Les paramètres de la combinaison prennent aussi ces couleurs (son apparence simple, sans image).";
    }
    ui_.labelWrapped(summary, {x, y}, w, fz(ui_, 13.5f), kDim);
    const float bh = ui_.s(42);
    const float by = box.bottom() - pad - bh;
    const float cw2 = ui_.s(150), gw = ui_.s(190);
    if (smallButton("cancel", {box.right() - pad - gw - ui_.s(10) - cw2, by, cw2, bh}, "Annuler") || ui_.backPressed()) recolorOpen_ = false;
    if (smallButton("go", {box.right() - pad - gw, by, gw, bh}, "Recolorer", true, true)) {
        dropSelection(d);
        beginEdit(d);
        for (const IRect& a : areas) {
            if (recolorMode_ == 0) replaceColor(d.doc.image, a, recolorFrom_, Px{recolorTo_.r, recolorTo_.g, recolorTo_.b, recolorFrom_.a});
            else {
                PixelImage part = d.doc.image.crop(a);
                recolorSuitPixels(part.data().data(), part.data().data(), part.data().size() / 4, recolorSuit_);
                d.doc.image.paste(part, a.x, a.y);
            }
        }
        if (recolorMode_ == 1) d.doc.colors = recolorSuit_;
        commit(d, "Recolorer");
        recolorOpen_ = false;
    }
    ui_.popId();
}

void Studio::drawConfirmClose() {
    if (confirmClose_ < 0 || confirmClose_ >= static_cast<int>(docs_.size())) {
        confirmClose_ = -1;
        return;
    }
    OpenDoc& d = *docs_[static_cast<size_t>(confirmClose_)];
    Renderer& rr = ui_.r();
    rr.fillRect({0, 0, ui_.screenW(), ui_.screenH()}, Color{2, 4, 12, 170});
    const float W = std::min(ui_.screenW() - ui_.s(80), ui_.s(620));
    const std::string text = "Ses modifications ne sont pas enregistrées. « Enregistrer et fermer » garde le brouillon sur ce PC ; « Fermer sans enregistrer » les perd.";
    const float pad = ui_.s(24);
    const float th = ui_.text().measureWrapped(text, FontFace::Body, ui_.s(fz(ui_, 15)), W - 2 * pad).y;
    const float H = pad + ui_.s(34) + th + ui_.s(20) + ui_.s(44) + pad;
    const Rect box{(ui_.screenW() - W) * 0.5f, (ui_.screenH() - H) * 0.5f, W, H};
    rr.fillRoundedRect(box, ui_.s(16), kPanel);
    rr.drawRoundedRect(box, ui_.s(16), kWarm, std::max(1.5f, ui_.s(2)));
    ui_.pushId("confirm");
    {
        i18n::Verbatim name;
        fitLabel(ui_, "Fermer « " + d.doc.name + " » ?", {box.x + pad, box.y + pad + ui_.s(12)}, 20, W - 2 * pad, kText, Align::Left, FontFace::Bold);
    }
    ui_.labelWrapped(text, {box.x + pad, box.y + pad + ui_.s(34)}, W - 2 * pad, fz(ui_, 15), kSub);
    const std::vector<std::string> labels = {"Enregistrer et fermer", "Fermer sans enregistrer", "Annuler"};
    const float gap = ui_.s(10), bw = (W - 2 * pad - 2 * gap) / 3.0f, by = box.bottom() - pad - ui_.s(44);
    if (smallButton("save", {box.x + pad, by, bw, ui_.s(44)}, labels[0], true, true)) {
        const int i = confirmClose_;
        confirmClose_ = -1;
        if (saveDraft(d)) closeTab(i, true);
    } else if (smallButton("drop", {box.x + pad + bw + gap, by, bw, ui_.s(44)}, labels[1], true, false, true)) {
        const int i = confirmClose_;
        confirmClose_ = -1;
        closeTab(i, true);
    } else if (smallButton("cancel", {box.x + pad + 2 * (bw + gap), by, bw, ui_.s(44)}, labels[2]) || ui_.backPressed()) {
        confirmClose_ = -1;
    }
    ui_.popId();
}

void Studio::drawModal() {
    tipKey_.clear();
    tipSeen_ = false;
    ui_.pushId("studio");
    if (importOpen_) drawImport();
    else if (recolorOpen_) drawRecolor();
    else if (confirmClose_ >= 0) drawConfirmClose();
    else if (libraryOpen_) drawLibrary();
    ui_.popId();
    drawTip();
}

}  // namespace cosmic::studio
