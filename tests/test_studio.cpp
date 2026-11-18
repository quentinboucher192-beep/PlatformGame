// 2.61 Studio de Créations: the drawing tools (pencil, line, rectangle, ellipse, fill, mirrors,
// symmetry, recolour), undo / redo by difference, the cutting of an imported sheet (zones found
// automatically, a grid, rows, frames anchored), the documents and their drafts, and the images
// of the creations (their layout rules, the PNG header, the emitter, the list kept on disk).

#include <cstring>

#include "engine/core/FileUtil.h"
#include "engine/rendering/Renderer.h"
#include "game/cosmetics/CreationImages.h"
#include "packaging/Sha256.h"
#include "tests/TestFramework.h"
#include "ui/studio/StudioDoc.h"
#include "ui/studio/StudioImage.h"
#include "ui/studio/StudioSlicer.h"

using namespace cosmic;
using namespace cosmic::studio;

namespace {

const Px kRed = Px::rgb(220, 40, 60), kBlue = Px::rgb(60, 120, 240), kNone{};

int countColor(const PixelImage& img, Px c) {
    int n = 0;
    for (int y = 0; y < img.h(); ++y)
        for (int x = 0; x < img.w(); ++x) n += img.get(x, y) == c ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("studio: pencil, line, rectangle and ellipse stay in their frame") {
    PixelImage img(64, 32);
    Canvas cv{img, {32, 0, 32, 32}};
    // Outside the frame being edited: nothing changes.
    CHECK(plot(cv, 3, 3, kRed).empty());
    CHECK(img.get(3, 3) == kNone);
    CHECK(plot(cv, 40, 5, kRed) == (IRect{40, 5, 1, 1}));
    CHECK(plot(cv, 40, 5, kRed).empty());  // the same colour again: nothing to change
    // A line: both ends, as many pixels as its longer side.
    PixelImage line(16, 16);
    Canvas lc{line, line.bounds()};
    CHECK(drawLine(lc, 1, 1, 10, 4, kBlue) == (IRect{1, 1, 10, 4}));
    CHECK(countColor(line, kBlue) == 10);
    CHECK(line.get(1, 1) == kBlue && line.get(10, 4) == kBlue);
    CHECK(linePoints(0, 0, 0, 5).size() == 6);
    // A rectangle: its outline, or filled.
    PixelImage rect(16, 16);
    Canvas rc{rect, rect.bounds()};
    drawRect(rc, 8, 6, 2, 2, kRed, false);  // corners in any order
    CHECK(countColor(rect, kRed) == 2 * 7 + 2 * 3);
    CHECK(rect.get(5, 4) == kNone);
    drawRect(rc, 2, 2, 8, 6, kRed, true);
    CHECK(countColor(rect, kRed) == 7 * 5);
    // An ellipse inscribed in its rectangle: symmetric, touching its four sides, empty inside.
    PixelImage ell(20, 20);
    Canvas ec{ell, ell.bounds()};
    const IRect d = drawEllipse(ec, 2, 3, 15, 12, kBlue, false);
    CHECK(d == (IRect{2, 3, 14, 10}));
    CHECK(ell.get(8, 7) == kNone);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) CHECK(ell.get(x, y) == ell.get(17 - x, y) && ell.get(x, y) == ell.get(x, 15 - y));
    PixelImage full(20, 20);
    Canvas fc{full, full.bounds()};
    drawEllipse(fc, 2, 3, 15, 12, kBlue, true);
    CHECK(countColor(full, kBlue) > countColor(ell, kBlue));
    CHECK(full.get(8, 7) == kBlue);
}

TEST_CASE("studio: symmetry mirrors what is drawn inside the frame") {
    PixelImage img(64, 32);
    Canvas cv{img, {32, 0, 32, 32}, Symmetry::Vertical};
    plot(cv, 34, 4, kRed);
    CHECK(img.get(34, 4) == kRed && img.get(32 + 31 - 2, 4) == kRed);
    CHECK(countColor(img, kRed) == 2);
    cv.sym = Symmetry::Both;
    const IRect d = plot(cv, 33, 1, kBlue);
    CHECK(d == (IRect{33, 1, 30, 30}));
    CHECK(img.get(33, 1) == kBlue && img.get(62, 1) == kBlue && img.get(33, 30) == kBlue && img.get(62, 30) == kBlue);
    // Mirrors of a selection.
    PixelImage m(4, 2);
    m.set(0, 0, kRed);
    m.flipH(m.bounds());
    CHECK(m.get(3, 0) == kRed && m.get(0, 0) == kNone);
    m.flipV(m.bounds());
    CHECK(m.get(3, 1) == kRed && m.get(3, 0) == kNone);
}

TEST_CASE("studio: the fill takes the area of one colour, inside the frame only") {
    PixelImage img(20, 10);
    Canvas wall{img, img.bounds()};
    drawLine(wall, 5, 0, 5, 9, kRed);  // a wall in the middle of the first frame
    Canvas cv{img, {0, 0, 10, 10}};
    const IRect d = floodFill(cv, 1, 1, kBlue);
    CHECK(d == (IRect{0, 0, 5, 10}));
    CHECK(countColor(img, kBlue) == 50);
    CHECK(img.get(7, 5) == kNone);   // behind the wall
    CHECK(img.get(15, 5) == kNone);  // the next frame
    CHECK(floodFill(cv, 1, 1, kBlue).empty());
    // The recolour of every frame at once.
    CHECK(replaceColor(img, img.bounds(), kBlue, kRed) == 50);
    CHECK(countColor(img, kRed) == 60);
    const auto counts = colorCounts(img, img.bounds());
    CHECK(counts.size() == 1 && counts[0].first == kRed && counts[0].second == 60);
    Px p;
    CHECK(parseHex("#3EE6D6", p) && p == Px::rgb(0x3e, 0xe6, 0xd6) && hexOf(p) == "#3ee6d6");
    CHECK(!parseHex("#3ee6d", p) && !parseHex("zz0000", p));
}

TEST_CASE("studio: undo and redo keep only the rectangle that changed") {
    PixelImage img(320, 864);
    ImageHistory h;
    CHECK(!h.canUndo() && !h.canRedo());
    PixelImage before = img;
    Canvas cv{img, {32, 64, 32, 32}};
    drawRect(cv, 33, 65, 40, 70, kRed, true);
    CHECK(h.record("Rectangle", before, img));
    CHECK(h.bytes() < 4096);  // 8 × 6 pixels kept twice, never the whole sheet
    before = img;
    CHECK(!h.record("Rien", before, img));  // nothing changed: nothing kept
    plot(cv, 50, 80, kBlue);
    CHECK(h.record("Crayon", before, img));
    CHECK(h.undoLabel() == "Crayon");
    const PixelImage drawn = img;
    CHECK(h.undo(img)->label == "Crayon");
    CHECK(img.get(50, 80) == kNone && img.get(33, 65) == kRed);
    CHECK(h.undo(img)->label == "Rectangle");
    CHECK(img.transparent(img.bounds()));
    CHECK(h.undo(img) == nullptr);
    h.redo(img);
    h.redo(img);
    CHECK(img == drawn);
    // A new change drops what could have been redone.
    h.undo(img);
    before = img;
    plot(cv, 34, 90, kBlue);
    h.record("Crayon 2", before, img);
    CHECK(!h.canRedo() && h.size() == 2);
    // A change of size (a frame added) keeps the whole image, and the settings with it.
    PixelImage strip(16, 16), longer(32, 16);
    ImageHistory s;
    CHECK(s.record("+ Image", strip, longer, Json{{"frames", 1}}, Json{{"frames", 2}}));
    PixelImage cur = longer;
    const ImageEdit* e = s.undo(cur);
    CHECK(e && e->resized && cur.w() == 16 && e->metaBefore["frames"] == 1);
    // A change kept by its region only (a stroke in one frame): only the pixels that differ.
    PixelImage sheet(320, 864);
    ImageHistory rh;
    const IRect frame{64, 96, 32, 32};
    const PixelImage frameBefore = sheet.crop(frame);
    Canvas fc{sheet, frame};
    drawLine(fc, 66, 100, 70, 100, kRed);
    CHECK(rh.recordRegion("Crayon", frame, frameBefore, sheet));
    CHECK(rh.bytes() < 512);
    CHECK(!rh.recordRegion("Rien", frame, sheet.crop(frame), sheet));
    const ImageEdit* re = rh.undo(sheet);
    CHECK(re && re->region == (IRect{66, 100, 5, 1}) && sheet.transparent(sheet.bounds()));
    rh.redo(sheet);
    CHECK(sheet.get(68, 100) == kRed);
    // Settings alone (the emitter...): kept without pixels.
    CHECK(rh.recordRegion("Émetteur", {}, PixelImage(), sheet, Json{{"rate", 1}}, Json{{"rate", 2}}));
    CHECK(rh.undo(sheet)->metaBefore["rate"] == 1);
    // Old changes go when the history is full.
    ImageHistory small(10000);
    PixelImage a(64, 64), b(64, 64);
    for (int i = 0; i < 20; ++i) {
        b.fill(b.bounds(), Px::rgb(static_cast<uint8_t>(i), 0, 0));
        small.record("x", a, b);
        a = b;
    }
    CHECK(small.bytes() <= 10000 + 64 * 64 * 8 && small.size() < 20);
}

TEST_CASE("studio: zones found automatically, grouped in rows") {
    // Three rows: 3, 2 and 1 sprites; a speck of dust; a sword one pixel away from its hand.
    PixelImage img(120, 90);
    auto blob = [&](int x, int y, int w, int h) { img.fill({x, y, w, h}, kRed); };
    blob(2, 2, 10, 20);
    blob(20, 4, 10, 18);
    blob(40, 2, 10, 20);
    blob(5, 35, 12, 15);
    blob(30, 33, 8, 17);
    blob(39, 40, 2, 2);  // one pixel from the previous one: the same zone
    blob(10, 70, 16, 16);
    img.set(100, 80, kRed);  // dust
    const auto zones = detectZones(img);
    CHECK(zones.size() == 6);
    const auto rows = groupRows(zones);
    CHECK(rows.size() == 3);
    CHECK(rows[0].size() == 3 && rows[1].size() == 2 && rows[2].size() == 1);
    CHECK(rows[0][0].x == 2 && rows[0][1].x == 20 && rows[0][2].x == 40);
    CHECK(rows[1][1] == (IRect{30, 33, 11, 17}));
    // Semi-transparent pixels under the threshold do not count.
    PixelImage faint(10, 10);
    faint.fill({2, 2, 3, 3}, Px{255, 255, 255, 10});
    CHECK(detectZones(faint).empty());
    SliceOptions any;
    any.alphaThreshold = 0;
    CHECK(detectZones(faint, any).size() == 1);
    // A sheet on a plain background: the background keyed out first.
    PixelImage bg(30, 10);
    bg.fill(bg.bounds(), Px::rgb(255, 0, 255));
    bg.fill({3, 3, 4, 4}, kBlue);
    bg.fill({20, 3, 4, 4}, kBlue);
    CHECK(detectZones(bg).size() == 1);
    Px key;
    CHECK(detectZones(keyOutBackground(bg, &key)).size() == 2 && key == Px::rgb(255, 0, 255));
}

TEST_CASE("studio: a regular grid, and the zones placed in the frames") {
    PixelImage sheet(96, 64);
    sheet.fill({4, 10, 8, 20}, kRed);    // cell (0, 0)
    sheet.fill({70, 2, 10, 10}, kBlue);  // cell (2, 0)
    sheet.fill({40, 40, 5, 5}, kRed);    // cell (1, 1)
    GridSpec g;
    auto all = gridRows(sheet, g, false);
    CHECK(all.size() == 2 && all[0].size() == 3 && all[1].size() == 3);
    auto used = gridRows(sheet, g, true);
    CHECK(used.size() == 2 && used[0].size() == 2 && used[1].size() == 1 && used[1][0] == (IRect{32, 32, 32, 32}));
    GridSpec gaps{16, 16, 4, 4, 4, 4};
    auto withGaps = gridRows(sheet, gaps, false);
    CHECK(withGaps.size() == 3 && withGaps[0].size() == 4 && withGaps[0][1].x == 24);
    // A cell of the frame's size: copied as it is.
    PixelImage exact = fitZone(sheet, {0, 0, 32, 32}, 32, 32, Anchor::BottomCenter);
    CHECK(exact.get(4, 10) == kRed && exact.get(3, 10) == kNone);
    // A zone: trimmed, anchored under the feet in the middle.
    bool shrunk = true;
    PixelImage feet = fitZone(sheet, {4, 10, 8, 20}, 32, 32, Anchor::BottomCenter, 100, 20, &shrunk);
    CHECK(!shrunk);
    CHECK(feet.opaqueBounds(feet.bounds()) == (IRect{12, 12, 8, 20}));
    PixelImage mid = fitZone(sheet, {4, 10, 8, 20}, 32, 32, Anchor::Center);
    CHECK(mid.opaqueBounds(mid.bounds()) == (IRect{12, 6, 8, 20}));
    // Larger than the frame: shrunk to fit, its proportions kept.
    PixelImage big(64, 40);
    big.fill({0, 0, 64, 40}, kBlue);
    PixelImage small = fitZone(big, big.bounds(), 32, 32, Anchor::BottomCenter, 100, 20, &shrunk);
    CHECK(shrunk);
    CHECK(small.opaqueBounds(small.bounds()) == (IRect{0, 12, 32, 20}));
    PixelImage twice = fitZone(sheet, {40, 40, 5, 5}, 32, 32, Anchor::Center, 200);
    CHECK(twice.opaqueBounds(twice.bounds()) == (IRect{11, 11, 10, 10}));
    // What cannot be imported, said plainly.
    CHECK(importProblem(PixelImage(4000, 10)).find("trop grande") != std::string::npos);
    CHECK(importProblem(PixelImage(10, 10)).find("transparente") != std::string::npos);
    CHECK(importProblem(sheet).empty());
}

TEST_CASE("studio: documents, frames of a strip and drafts on disk") {
    // Nova's animations from her own description.
    auto nova = FileUtil::loadJson(Paths::assetsDir() / "animations" / "nova.json");
    CHECK(nova.has_value());
    const auto anims = suitAnims(*nova);
    CHECK(anims.size() == 27 && anims[0].id == "idle" && anims[0].name == "Attente" && anims[2].name == "Course" && anims[2].count == 8);
    CHECK(static_cast<int>(anims[2].durations.size()) == 8);
    // A suit's image is Nova's sheet without the rows added in 2.70 (the strikes upwards and
    // downwards, rows 27-29: a suit created online plays the forward strike instead).
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    CHECK(loadRgbaImage(Paths::assetsDir() / "sprites" / "player" / "nova.png", px, w, h));
    CHECK(w == kSuitSheetW && h == kSuitSheetH + 3 * kSuitFrame);
    PixelImage sheet = PixelImage::fromRgba(w, h, px);
    StudioDoc suit = StudioDoc::make(DocKind::Suit, newDraftUid(), "Aube de Sylvanox", sheet);
    CHECK(suit.sendProblem(anims).empty());
    CHECK(!suit.palette.empty() && suit.palette.size() <= 15);
    // An empty frame: refused, with the animation named.
    suit.image.fill(suit.frameRect(anims[17], 3), Px{});
    CHECK(suit.drawnFrames(anims[17]) == anims[17].count - 1);
    CHECK(suit.sendProblem(anims).find("Victoire") != std::string::npos);
    // A particle: frames added, copied, removed.
    StudioDoc trail = StudioDoc::make(DocKind::Trail, "t1", "Comète d'aurore", PixelImage(), 4);
    CHECK(trail.stripFrames() == 4 && trail.frameW == 16 && trail.image.h() == 16);
    CHECK(!trail.sendProblem(anims).empty());  // empty
    Canvas cv{trail.image, trail.frameRect(trail.anims(anims)[0], 1)};
    plot(cv, 20, 3, kRed);
    CHECK(trail.insertFrame(1, true));
    CHECK(trail.stripFrames() == 5 && trail.image.get(36, 3) == kRed && trail.image.get(20, 3) == kRed);
    CHECK(trail.removeFrame(1) && trail.stripFrames() == 4 && trail.image.get(20, 3) == kRed);
    CHECK(trail.sendProblem(anims).empty());
    CHECK(!suit.insertFrame(0, false));
    // The draft: written, read back the same.
    trail.emitter.rate = 77;
    trail.palette = {kRed, kBlue};
    const fs::path dir = Paths::userDir() / "studio" / "brouillons" / trail.uid;
    std::string why;
    CHECK(trail.saveDraft(dir, &why));
    CHECK(!trail.savedAt.empty());
    auto back = StudioDoc::loadDraft(dir, &why);
    CHECK(back.has_value());
    CHECK(back->name == "Comète d'aurore" && back->kind == DocKind::Trail && back->emitter.rate == 77 && back->palette.size() == 2);
    CHECK(back->image == trail.image);
    // A damaged draft is refused.
    FileUtil::writeTextAtomic(dir / "doc.json", "{\"format\":\"autre\"}");
    CHECK(!StudioDoc::loadDraft(dir, &why).has_value());
}

TEST_CASE("creation images: layouts, PNG header, emitter and the list kept") {
    CHECK(imageLayoutProblem("suit", 320, 864, 32, 32, 1, 1).empty());
    CHECK(!imageLayoutProblem("suit", 320, 832, 32, 32, 1, 1).empty());
    CHECK(imageLayoutProblem("trail", 64, 16, 16, 16, 4, 12).empty());
    CHECK(!imageLayoutProblem("trail", 64, 16, 16, 16, 3, 12).empty());  // not side by side
    CHECK(!imageLayoutProblem("trail", 17 * 16, 16, 16, 16, 17, 12).empty());
    CHECK(!imageLayoutProblem("victory", 2, 2, 2, 2, 1, 12).empty());
    CHECK(imageLayoutProblem("frame", 128, 64, 64, 64, 2, 8).empty());
    CHECK(!imageLayoutProblem("frame", 64, 32, 64, 32, 1, 8).empty());  // not square
    CHECK(!imageLayoutProblem("frame", 64, 64, 64, 64, 1, 0).empty());
    CHECK(!imageLayoutProblem("title", 64, 64, 64, 64, 1, 8).empty());
    // A PNG made here: its header read back; anything else refused with a reason.
    PixelImage img(48, 16);
    img.fill({2, 2, 4, 4}, kRed);
    std::vector<uint8_t> png = encodePng(img.w(), img.h(), img.data());
    CHECK(!png.empty());
    PngHeader hd;
    std::string why;
    CHECK(readPngHeader(png.data(), png.size(), hd, &why));
    CHECK(hd.width == 48 && hd.height == 16 && hd.alpha);
    std::vector<uint8_t> bad = png;
    bad[20] ^= 0x01;  // the width, its CRC no longer matching
    CHECK(!readPngHeader(bad.data(), bad.size(), hd, &why) && why.find("abîmé") != std::string::npos);
    const char* jpeg = "\xFF\xD8\xFF\xE0 not a png";
    CHECK(!readPngHeader(reinterpret_cast<const uint8_t*>(jpeg), std::strlen(jpeg), hd, &why) && why == "Pas un fichier PNG.");
    std::vector<uint8_t> cut(png.begin(), png.begin() + static_cast<std::ptrdiff_t>(png.size() / 2));
    CHECK(!readPngHeader(cut.data(), cut.size(), hd, &why));
    int dw = 0, dh = 0;
    std::vector<uint8_t> rgba;
    CHECK(decodeImage(png.data(), png.size(), dw, dh, rgba) && PixelImage::fromRgba(dw, dh, rgba) == img);
    // The emitter: every field in its bounds, else refused.
    ImageEmitter e;
    auto ej = e.toJson();
    CHECK(ImageEmitter::fromJson(ej) == e);
    ej["rate"] = 500;
    CHECK(!ImageEmitter::fromJson(ej));
    ej["rate"] = 20;
    ej.erase("sparkles");
    CHECK(!ImageEmitter::fromJson(ej));
    ImageEmitter wild;
    wild.spin = 5000;
    wild.lifeMs = 1;
    CHECK(wild.clamped().spin == 720 && wild.clamped().lifeMs == 100);
    // The list of the server: entries checked one by one.
    const std::string hash = Sha256::hex(png);
    Json list = Json::array();
    list.push_back(Json{{"ref", "srvtrail:7"}, {"kind", "trail"}, {"hash", hash}, {"width", 48}, {"height", 16}, {"frame_w", 16},
                        {"frame_h", 16}, {"frames", 3}, {"fps", 12}, {"emitter", e.toJson()}, {"updated_at", "2026-11-17T10:00:00Z"}});
    list.push_back(Json{{"ref", "srvframe:8"}, {"kind", "trail"}, {"hash", hash}, {"width", 48}, {"height", 16}});        // kind ≠ ref
    list.push_back(Json{{"ref", "srvsuit:9"}, {"kind", "suit"}, {"hash", "abc"}, {"width", 320}, {"height", 864}});      // bad hash
    list.push_back(Json{{"ref", "srvtrail:10"}, {"kind", "trail"}, {"hash", hash}, {"width", 48}, {"height", 16}, {"frame_w", 16},
                        {"frame_h", 16}, {"frames", 3}, {"fps", 12}});  // no emitter
    CreationImageBook book;
    book.setDir(Paths::userDir() / "online" / "creation_images");
    CHECK(book.applyList(list));
    CHECK(book.all().size() == 1 && book.find("srvtrail:7") && !book.find("srvframe:8"));
    CHECK(book.find("srvtrail:7")->path() == "/api/v1/creations/7/image/" + hash);
    CHECK(!book.applyList(list));
    CHECK(CreationImageBook::checkBytes(*book.find("srvtrail:7"), png).empty());
    CHECK(!CreationImageBook::checkBytes(*book.find("srvtrail:7"), bad).empty());
    // Kept on disk: read back the same; a file no longer listed goes.
    FileUtil::writeBinary(book.fileOf(hash), png);
    FileUtil::writeBinary(book.fileOf(std::string(64, 'e')), png);
    CHECK(book.save());
    CHECK(fs::exists(book.fileOf(hash)) && !fs::exists(book.fileOf(std::string(64, 'e'))));
    CreationImageBook again;
    again.setDir(Paths::userDir() / "online" / "creation_images");
    again.load();
    CHECK(again.all().size() == 1 && again.find("srvtrail:7")->emitter == e);
}

TEST_CASE("creation images: downloaded once, checked, kept on disk, never waited for") {
    PixelImage img(32, 16);
    img.fill({1, 1, 6, 6}, kBlue);
    const std::vector<uint8_t> png = encodePng(img.w(), img.h(), img.data());
    const std::string hash = Sha256::hex(png);
    ImageEmitter e;
    Json list = Json::array();
    list.push_back(Json{{"ref", "srvvictory:3"}, {"kind", "victory"}, {"hash", hash}, {"width", 32}, {"height", 16}, {"frame_w", 16},
                        {"frame_h", 16}, {"frames", 2}, {"fps", 12}, {"emitter", e.toJson()}});
    const fs::path dir = Paths::userDir() / "online" / "creation_images_dl";
    std::error_code ec;
    fs::remove_all(dir, ec);
    CreationImageBook book;
    book.setDir(dir);
    book.applyList(list);
    // The download is asked for, its answer comes later (the drawing never waits).
    std::vector<std::string> asked;
    std::function<void(bool, std::vector<uint8_t>)> answer;
    book.setFetch([&](const std::string& path, std::function<void(bool, std::vector<uint8_t>)> done) {
        asked.push_back(path);
        answer = std::move(done);
    });
    Renderer none;  // no window: the texture itself cannot be made here
    CHECK(book.texture("srvvictory:3", none) == nullptr);
    CHECK(asked.size() == 1 && asked[0] == "/api/v1/creations/3/image/" + hash);
    CHECK(book.texture("srvvictory:3", none) == nullptr);
    CHECK(asked.size() == 1);  // not asked twice while it comes
    CHECK(book.busy() == 1);
    // Other bytes than the server announced: refused, nothing kept.
    std::vector<uint8_t> other = png;
    other.back() ^= 0x5A;
    answer(true, other);
    CHECK(!fs::exists(book.fileOf(hash)));
    CHECK(book.busy() == 0);
    // The good file: kept on disk for the next times (offline too), read off the main thread.
    CreationImageBook fresh;
    fresh.setDir(dir);
    fresh.applyList(list);
    fresh.setFetch([&](const std::string& path, std::function<void(bool, std::vector<uint8_t>)> done) {
        asked.push_back(path);
        done(true, png);
    });
    CHECK(fresh.texture("srvvictory:3", none) == nullptr);
    CHECK(fs::exists(fresh.fileOf(hash)));
    CHECK(*FileUtil::readBinary(fresh.fileOf(hash)) == png);
    CHECK(!fresh.texture("srvtrail:404", none));  // not listed: nothing asked
    CHECK(asked.size() == 2);
}
