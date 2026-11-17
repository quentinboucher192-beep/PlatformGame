#include "ui/widgets/AssetPreview.h"

#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "engine/animation/Animation.h"
#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

// ============================================================================ image loader

struct AssetImageLoader::Shared {
    struct Job {
        std::string key, path;
        fs::path file, animJson;  // animJson: the first frame of this animation (its sheet is `file`)
        bool thumb = true;
        int px = 128;
    };
    struct Done {
        std::string key;
        SDL_Surface* surface = nullptr;
        bool thumb = true;
        bool pixelArt = true;
    };
    std::mutex m;
    std::condition_variable cv;
    std::deque<Job> jobs;                    // the most recent request first
    std::vector<Done> done;
    std::unordered_set<std::string> busy;    // queued or being decoded
    std::unordered_set<std::string> failed;  // paths that are not pictures
    bool stop = false;
    ~Shared() {
        for (Done& d : done) SDL_DestroySurface(d.surface);
    }
};

namespace {

/// The first frame of an animation: its « idle » clip, else the first clip of the sheet.
Rect firstFrame(const AnimationSet& set) {
    const AnimationClip* first = set.clip("idle");
    if (!first)
        for (const auto& [name, c] : set.clips)
            if (!first || std::make_pair(c.row, c.start) < std::make_pair(first->row, first->start)) first = &c;
    if (first && first->frameCount() > 0) return set.frameRect(*first, 0);
    return {0, 0, static_cast<float>(set.frameWidth), static_cast<float>(set.frameHeight)};
}

/// On the worker: a picture decoded, cropped to an animation's first frame and scaled down
/// to a thumbnail when asked.
SDL_Surface* decodePicture(const fs::path& file, const fs::path& animJson, bool thumb, int px) {
    SDL_Rect crop{0, 0, 0, 0};
    if (!animJson.empty()) {
        auto j = FileUtil::loadJson(animJson);
        if (!j) return nullptr;
        const Rect fr = firstFrame(*AnimationSet::fromJson(*j, Paths::toUtf8(animJson)));
        crop = {static_cast<int>(fr.x), static_cast<int>(fr.y), static_cast<int>(fr.w), static_cast<int>(fr.h)};
    }
    SDL_Surface* src = IMG_Load(Paths::toUtf8(file).c_str());
    if (!src) return nullptr;
    if (crop.w <= 0 || crop.h <= 0) crop = {0, 0, src->w, src->h};
    crop.x = std::clamp(crop.x, 0, std::max(0, src->w - 1));
    crop.y = std::clamp(crop.y, 0, std::max(0, src->h - 1));
    crop.w = std::max(1, std::min(crop.w, src->w - crop.x));
    crop.h = std::max(1, std::min(crop.h, src->h - crop.y));
    const float scale = thumb ? std::min(1.0f, static_cast<float>(px) / static_cast<float>(std::max(crop.w, crop.h))) : 1.0f;
    const bool whole = crop.x == 0 && crop.y == 0 && crop.w == src->w && crop.h == src->h;
    if (scale >= 1.0f && whole) return src;
    // Only the piece kept is copied (an animation's first frame out of a large sheet).
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
    SDL_Surface* piece = SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32);
    if (piece && !SDL_BlitSurface(src, &crop, piece, nullptr)) {
        SDL_DestroySurface(piece);
        piece = nullptr;
    }
    SDL_DestroySurface(src);
    if (!piece || scale >= 1.0f) return piece;
    const int w = std::max(1, static_cast<int>(std::lround(crop.w * scale))), h = std::max(1, static_cast<int>(std::lround(crop.h * scale)));
    SDL_Surface* out = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    SDL_SetSurfaceBlendMode(piece, SDL_BLENDMODE_NONE);
    if (out && !SDL_BlitSurfaceScaled(piece, nullptr, out, nullptr, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroySurface(out);
        out = nullptr;
    }
    SDL_DestroySurface(piece);
    return out;
}

/// Paintings and photographs are smoothed when scaled; the pixel art stays sharp.
bool pixelArtOf(const std::string& path) {
    return path.rfind("stories/", 0) != 0 && path.rfind("online/", 0) != 0 && path.rfind("ui/planets/", 0) != 0;
}

}  // namespace

void AssetImageLoader::work(std::shared_ptr<Shared> s) {
    for (;;) {
        Shared::Job job;
        {
            std::unique_lock<std::mutex> lock(s->m);
            s->cv.wait(lock, [&] { return s->stop || !s->jobs.empty(); });
            if (s->stop) return;
            job = std::move(s->jobs.front());
            s->jobs.pop_front();
        }
        SDL_Surface* out = decodePicture(job.file, job.animJson, job.thumb, job.px);
        std::lock_guard<std::mutex> lock(s->m);
        if (s->stop) {
            if (out) SDL_DestroySurface(out);
            return;
        }
        s->busy.erase(job.key);
        if (out) s->done.push_back({job.key, out, job.thumb, pixelArtOf(job.path)});
        else s->failed.insert(job.path);
    }
}

AssetImageLoader::AssetImageLoader(size_t thumbnails, size_t pictures, int thumbPx)
    : shared_(std::make_shared<Shared>()), thumbs_(thumbnails), pictures_(pictures), thumbPx_(std::max(16, thumbPx)) {
    std::thread(work, shared_).detach();
}

AssetImageLoader::~AssetImageLoader() {
    std::lock_guard<std::mutex> lock(shared_->m);
    shared_->stop = true;
    shared_->jobs.clear();
    shared_->cv.notify_all();
}

void AssetImageLoader::request(const std::string& key, const fs::path& file, const std::string& animJson, bool thumb) {
    asked_[key] = frame_;
    const std::string path = key.substr(2);
    std::lock_guard<std::mutex> lock(shared_->m);
    if (shared_->busy.count(key) || shared_->failed.count(path)) return;
    shared_->busy.insert(key);
    shared_->jobs.push_front({key, path, file, animJson.empty() ? fs::path() : Paths::fromUtf8(animJson), thumb, thumbPx_});
    shared_->cv.notify_one();
}

TexturePtr AssetImageLoader::thumbnail(const AssetCatalog& cat, const AssetEntry& e) {
    if (e.type != AssetType::Image && e.type != AssetType::Animation) return nullptr;
    const std::string key = "t:" + e.path;
    if (TexturePtr* t = thumbs_.get(key)) return *t;
    if (e.type == AssetType::Image) request(key, cat.absolutePath(e), {}, true);
    else if (!e.sheet.empty()) request(key, cat.root() / Paths::fromUtf8(e.sheet), Paths::toUtf8(cat.absolutePath(e)), true);
    return nullptr;
}

TexturePtr AssetImageLoader::picture(const AssetCatalog& cat, const std::string& imagePath) {
    if (imagePath.empty()) return nullptr;
    const std::string key = "p:" + imagePath;
    if (TexturePtr* t = pictures_.get(key)) return *t;
    request(key, cat.root() / Paths::fromUtf8(imagePath), {}, false);
    return nullptr;
}

TexturePtr AssetImageLoader::fontSample(Renderer& r, const AssetCatalog& cat, const AssetEntry& e, const std::string& text, int px, int wrap) {
    if (e.type != AssetType::Font || text.empty() || px <= 0) return nullptr;
    const std::string key = "f:" + e.path + "|" + std::to_string(px) + "|" + std::to_string(wrap) + "|" + text;
    if (TexturePtr* t = thumbs_.get(key)) return *t;
    {
        std::lock_guard<std::mutex> lock(shared_->m);
        if (shared_->failed.count(e.path)) return nullptr;
    }
    TexturePtr tex;
    if (TTF_Font* f = TTF_OpenFont(Paths::toUtf8(cat.absolutePath(e)).c_str(), static_cast<float>(px))) {
        const SDL_Color white{255, 255, 255, 255};
        SDL_Surface* s = wrap > 0 ? TTF_RenderText_Blended_Wrapped(f, text.c_str(), 0, white, wrap) : TTF_RenderText_Blended(f, text.c_str(), 0, white);
        if (s) {
            tex = r.textureFromSurface(s, key, false);
            SDL_DestroySurface(s);
        }
        TTF_CloseFont(f);
    }
    if (tex) thumbs_.put(key, tex);
    else {
        std::lock_guard<std::mutex> lock(shared_->m);
        shared_->failed.insert(e.path);
    }
    return tex;
}

void AssetImageLoader::pump(Renderer& r, int budget) {
    ++frame_;
    std::vector<Shared::Done> ready;
    {
        std::lock_guard<std::mutex> lock(shared_->m);
        // Requests not repeated during the previous frame: gone from the screen.
        for (auto it = shared_->jobs.begin(); it != shared_->jobs.end();) {
            auto a = asked_.find(it->key);
            if (a == asked_.end() || a->second + 1 < frame_) {
                shared_->busy.erase(it->key);
                it = shared_->jobs.erase(it);
            } else {
                ++it;
            }
        }
        const size_t n = std::min(shared_->done.size(), static_cast<size_t>(std::max(1, budget)));
        ready.assign(shared_->done.begin(), shared_->done.begin() + static_cast<std::ptrdiff_t>(n));
        shared_->done.erase(shared_->done.begin(), shared_->done.begin() + static_cast<std::ptrdiff_t>(n));
    }
    for (Shared::Done& d : ready) {
        TexturePtr t = r.textureFromSurface(d.surface, d.key, d.pixelArt);
        SDL_DestroySurface(d.surface);
        if (t) (d.thumb ? thumbs_ : pictures_).put(d.key, std::move(t));
    }
    if (asked_.size() > 4 * thumbs_.capacity())
        for (auto it = asked_.begin(); it != asked_.end();) it = it->second + 600 < frame_ ? asked_.erase(it) : std::next(it);
}

bool AssetImageLoader::failed(const std::string& path) const {
    std::lock_guard<std::mutex> lock(shared_->m);
    return shared_->failed.count(path) > 0;
}

size_t AssetImageLoader::pending() const {
    std::lock_guard<std::mutex> lock(shared_->m);
    return shared_->jobs.size() + shared_->done.size();
}

// ============================================================================ preview

/// A piece of work done on a thread of its own (never waited for: abandoned, its result is
/// dropped by that thread when it ends).
struct AssetPreview::Job {
    std::mutex m;
    bool done = false;
    bool abandoned = false;
    std::shared_ptr<AnimationSet> anim;
    std::vector<std::string> lines;
    bool cut = false;
    MIX_Audio* audio = nullptr;
};

namespace {

using PreviewJob = std::shared_ptr<AssetPreview::Job>;

template <typename Work>
PreviewJob launch(Work work) {
    auto job = std::make_shared<AssetPreview::Job>();
    std::thread([job, work = std::move(work)]() mutable {
        work(*job);
        std::lock_guard<std::mutex> lock(job->m);
        job->done = true;
        if (job->abandoned && job->audio) {
            MIX_DestroyAudio(job->audio);
            job->audio = nullptr;
        }
    }).detach();
    return job;
}

bool finished(const PreviewJob& j) {
    if (!j) return false;
    std::lock_guard<std::mutex> lock(j->m);
    return j->done;
}

void abandon(PreviewJob& j) {
    if (!j) return;
    {
        std::lock_guard<std::mutex> lock(j->m);
        j->abandoned = true;
        if (j->done && j->audio) {
            MIX_DestroyAudio(j->audio);
            j->audio = nullptr;
        }
    }
    j.reset();
}

/// Frames per second a clip plays at in the game (its frames over its length).
int clipFps(const AnimationClip* c) {
    if (!c || c->frameCount() <= 0) return 8;
    const float t = c->totalDuration();
    return t > 0.0f ? std::clamp(static_cast<int>(std::lround(static_cast<float>(c->frameCount()) / t)), 1, 60) : 8;
}

/// The checkerboard behind a picture: never more than about 200 squares.
void checkerboard(Renderer& r, const Rect& area, float minCell) {
    r.fillRect(area, Color{52, 56, 68, 255});
    const float cell = std::max(minCell, std::sqrt(std::max(1.0f, area.w * area.h) / 200.0f));
    const int cols = static_cast<int>(std::ceil(area.w / cell)), rows = static_cast<int>(std::ceil(area.h / cell));
    for (int y = 0; y < rows; ++y)
        for (int x = y & 1; x < cols; x += 2) {
            Rect c{area.x + static_cast<float>(x) * cell, area.y + static_cast<float>(y) * cell, cell, cell};
            c.w = std::min(c.w, area.right() - c.x);
            c.h = std::min(c.h, area.bottom() - c.y);
            if (c.w > 0.0f && c.h > 0.0f) r.fillRect(c, Color{78, 84, 98, 255});
        }
}

/// `w` × `h` fitted in `room`: whole scales for what is smaller (sharp pixels), centred.
Rect fitted(float w, float h, const Rect& room, float* scaleOut) {
    if (w <= 0.0f || h <= 0.0f) return {room.center().x, room.center().y, 0, 0};
    float k = std::min(room.w / w, room.h / h);
    if (k >= 1.0f) k = std::min(16.0f, std::floor(k));
    if (scaleOut) *scaleOut = k;
    return Rect::fromCenter(room.center(), {w * k, h * k});
}

/// A font specimen (drawn by TrueType itself: never translated), wrapped to the stage.
constexpr const char* kSpecimen = "COSMIC PLATFORMER · AaBbCcDdEeFfGg 0123456789 · àâçéèêëîïôùûœ ÀÉÈÇŒ !?";

}  // namespace

AssetPreview::AssetPreview(GameServices& g) : g_(g) {}

AssetPreview::~AssetPreview() {
    stopAudio();
    abandon(animJob_);
    abandon(textJob_);
}

void AssetPreview::show(const std::shared_ptr<const AssetCatalog>& cat, const AssetEntry* e) {
    if (cat == cat_ && e == entry_) return;
    stopAudio();
    abandon(animJob_);
    abandon(textJob_);
    anim_.reset();
    clipNames_.clear();
    lines_.clear();
    textCut_ = false;
    audioError_.clear();
    clip_ = 0;
    animT_ = 0.0f;
    animPaused_ = false;
    cat_ = cat;
    entry_ = cat ? e : nullptr;
    if (!entry_) return;
    const fs::path file = cat_->absolutePath(*entry_);
    if (entry_->type == AssetType::Animation) {
        const std::string rel = entry_->path;
        animJob_ = launch([file, rel](Job& j) {
            if (auto json = FileUtil::loadJson(file)) j.anim = AnimationSet::fromJson(*json, rel);
        });
    } else if (entry_->type == AssetType::Data) {
        textJob_ = launch([file](Job& j) {
            std::ifstream f(file, std::ios::binary);
            std::string line;
            size_t bytes = 0;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                for (char& c : line)
                    if (c == '\t') c = ' ';
                bytes += line.size();
                if (j.lines.size() >= 400 || bytes > 48 * 1024) {
                    j.cut = true;
                    break;
                }
                j.lines.push_back(std::move(line));
            }
        });
    }
}

bool AssetPreview::playing() const { return audioStarted_ || audioJob_ != nullptr; }

float AssetPreview::controlsHeight() const {
    if (!entry_) return 0.0f;
    const AssetType t = entry_->type;
    return t == AssetType::Animation || t == AssetType::Sound || t == AssetType::Music ? g_.ui.s(46) : 0.0f;
}

void AssetPreview::playAudio() {
    if (!entry_ || !cat_) return;
    AudioSystem& audio = g_.app.audio();
    stopAudio();
    if (!audio.available()) {
        audioError_ = "Aucun périphérique audio : rien ne peut être écouté.";
        return;
    }
    audioError_.clear();
    MIX_Mixer* mixer = audio.mixer();
    const std::string file = Paths::toUtf8(cat_->absolutePath(*entry_));
    const bool predecode = entry_->type == AssetType::Sound;  // a short effect; a music is decoded as it plays
    audioJob_ = launch([mixer, file, predecode](Job& j) { j.audio = MIX_LoadAudio(mixer, file.c_str(), predecode); });
}

void AssetPreview::restoreMusic() {
    AudioSystem& audio = g_.app.audio();
    if (pausedMusic_) audio.resumeMusic();
    if (ducked_) audio.setMusicDuck(duckBefore_);
    pausedMusic_ = ducked_ = false;
}

void AssetPreview::stopAudio() {
    abandon(audioJob_);
    if (audioStarted_) g_.app.audio().stopPreview(60);
    audioStarted_ = false;
    restoreMusic();
}

void AssetPreview::updateAudio() {
    AudioSystem& audio = g_.app.audio();
    if (finished(audioJob_)) {
        MIX_Audio* a = nullptr;
        {
            std::lock_guard<std::mutex> lock(audioJob_->m);
            a = audioJob_->audio;
            audioJob_->audio = nullptr;
        }
        audioJob_.reset();
        if (!a) {
            audioError_ = "Ce fichier ne se lit pas.";
        } else if (entry_) {
            const bool music = entry_->type == AssetType::Music;
            if (music) {
                // Two musics at once would mix: the menu's one waits, where it was.
                if (!audio.currentMusic().empty() && !audio.musicPaused()) {
                    audio.pauseMusic();
                    pausedMusic_ = true;
                }
            } else {
                duckBefore_ = audio.musicDuck();
                audio.setMusicDuck(std::min(duckBefore_, 0.35f));  // only lowered for a sound
                ducked_ = true;
            }
            audio.playPreview(a, music ? AudioBus::Music : AudioBus::Effects);
            audioStarted_ = true;
        } else {
            MIX_DestroyAudio(a);
        }
    }
    if (audioStarted_ && !audio.previewPlaying()) {  // heard to its end
        audioStarted_ = false;
        restoreMusic();
    }
}

void AssetPreview::draw(const Rect& r, float dt) {
    UIContext& ui = g_.ui;
    if (finished(animJob_)) {
        anim_ = animJob_->anim;
        animJob_.reset();
        clipNames_.clear();
        if (anim_) {
            for (const auto& [name, c] : anim_->clips) clipNames_.push_back(name);
            std::sort(clipNames_.begin(), clipNames_.end(), [&](const std::string& a, const std::string& b) {
                const AnimationClip& x = anim_->clips.at(a);
                const AnimationClip& y = anim_->clips.at(b);
                if (x.row != y.row) return x.row < y.row;
                if (x.start != y.start) return x.start < y.start;
                return a < b;
            });
            auto idle = std::find(clipNames_.begin(), clipNames_.end(), "idle");
            clip_ = idle == clipNames_.end() ? 0 : static_cast<int>(idle - clipNames_.begin());
            fps_ = clipNames_.empty() ? 8 : clipFps(anim_->clip(clipNames_[static_cast<size_t>(clip_)]));
        }
    }
    if (finished(textJob_)) {
        lines_ = std::move(textJob_->lines);
        textCut_ = textJob_->cut;
        textJob_.reset();
    }
    updateAudio();
    if (!animPaused_) animT_ += dt;
    const float ch = controlsHeight();
    const float gap = ch > 0.0f ? ui.s(10) : 0.0f;
    drawStage({r.x, r.y, r.w, std::max(ui.s(40), r.h - ch - gap)});
    if (ch > 0.0f) drawControls({r.x, r.bottom() - ch, r.w, ch});
}

void AssetPreview::drawStage(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    rr.fillRoundedRect(r, ui.s(10), Color{8, 12, 26, 240});
    rr.drawRoundedRect(r, ui.s(10), ui.theme().border, ui.s(1));
    const Rect in{r.x + ui.s(10), r.y + ui.s(10), r.w - ui.s(20), r.h - ui.s(20)};
    if (!entry_ || !cat_ || in.w <= 0.0f || in.h <= 0.0f) return;
    auto waiting = [&](const std::string& t) {
        const float fs = ui.fitFont(t, FontFace::Body, 17, in.w);
        ui.label(ui.elide(t, FontFace::Body, fs, in.w), in.center(), fs, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
    };
    const AssetEntry& e = *entry_;
    switch (e.type) {
        case AssetType::Image: {
            TexturePtr tex = images_.picture(*cat_, e.path);
            if (!tex) tex = images_.thumbnail(*cat_, e);  // the thumbnail meanwhile
            if (!tex) return waiting(images_.failed(e.path) ? "Cette image ne se lit pas." : "Chargement…");
            const float w = e.width > 0 ? static_cast<float>(e.width) : static_cast<float>(tex->width());
            const float h = e.height > 0 ? static_cast<float>(e.height) : static_cast<float>(tex->height());
            const Rect dst = fitted(w, h, in, nullptr);
            checkerboard(rr, dst, ui.s(8));
            rr.drawTexture(*tex, nullptr, dst);
            return;
        }
        case AssetType::Animation: {
            if (!anim_) return waiting(animJob_ ? "Chargement…" : "Cette animation ne se lit pas.");
            TexturePtr sheet = images_.picture(*cat_, anim_->texturePath);
            if (!sheet) return waiting(images_.failed(anim_->texturePath) ? "Sa planche ne se lit pas." : "Chargement…");
            const AnimationClip* clip = clipNames_.empty() ? nullptr : anim_->clip(clipNames_[static_cast<size_t>(std::clamp(clip_, 0, static_cast<int>(clipNames_.size()) - 1))]);
            if (!clip || clip->frameCount() <= 0) return waiting("Aucun clip.");
            const int frame = static_cast<int>(animT_ * static_cast<float>(fps_)) % clip->frameCount();
            const Rect src = anim_->frameRect(*clip, frame);
            const Rect dst = fitted(src.w, src.h, in, nullptr);
            checkerboard(rr, dst, ui.s(8));
            rr.drawTexture(*sheet, &src, dst);
            return;
        }
        case AssetType::Font: {
            const int px = static_cast<int>(std::clamp(in.h / 6.0f, 14.0f, 44.0f));
            TexturePtr tex = images_.fontSample(rr, *cat_, e, kSpecimen, px, static_cast<int>(in.w));
            if (!tex) return waiting("Cette police ne se lit pas.");
            const float k = std::min({1.0f, in.w / static_cast<float>(tex->width()), in.h / static_cast<float>(tex->height())});
            rr.drawTexture(*tex, nullptr, Rect::fromCenter(in.center(), {tex->width() * k, tex->height() * k}), ui.theme().text);
            return;
        }
        case AssetType::Sound:
        case AssetType::Music: {
            // A speaker (sound) or a note (music), the time, the progress.
            AudioSystem& audio = g_.app.audio();
            const float size = std::min(in.w, in.h) * 0.32f;
            const Vec2 c{in.center().x, in.y + in.h * 0.42f};
            const Color col = audioStarted_ ? ui.theme().accent : ui.theme().textDim;
            if (e.type == AssetType::Music) {
                rr.fillCircle({c.x - size * 0.22f, c.y + size * 0.32f}, size * 0.18f, col);
                rr.fillRect({c.x - size * 0.22f + size * 0.14f, c.y - size * 0.42f, size * 0.06f, size * 0.74f}, col);
                rr.fillRect({c.x - size * 0.22f + size * 0.14f, c.y - size * 0.42f, size * 0.36f, size * 0.07f}, col);
            } else {
                rr.fillRect({c.x - size * 0.42f, c.y - size * 0.14f, size * 0.2f, size * 0.28f}, col);
                rr.fillTriangle({c.x - size * 0.24f, c.y - size * 0.14f}, {c.x + size * 0.02f, c.y - size * 0.38f}, {c.x + size * 0.02f, c.y + size * 0.38f}, col);
                rr.fillTriangle({c.x - size * 0.24f, c.y - size * 0.14f}, {c.x + size * 0.02f, c.y + size * 0.38f}, {c.x - size * 0.24f, c.y + size * 0.14f}, col);
                for (int k = 1; k <= 2; ++k)
                    rr.drawArc({c.x + size * 0.02f, c.y}, size * (0.18f + 0.16f * static_cast<float>(k)), ui.s(3), -0.7f, 0.7f, col);
            }
            const int64_t total = audioStarted_ ? std::max<int64_t>(audio.previewDurationMs(), e.durationMs) : e.durationMs;
            const int64_t at = audioStarted_ ? audio.previewPositionMs() : 0;
            const float barY = in.y + in.h * 0.78f;
            const Rect bar{in.x + in.w * 0.1f, barY, in.w * 0.8f, ui.s(6)};
            rr.fillRoundedRect(bar, ui.s(3), Color{40, 50, 70, 255});
            if (total > 0 && audioStarted_)
                rr.fillRoundedRect({bar.x, bar.y, bar.w * std::clamp(static_cast<float>(at) / static_cast<float>(total), 0.0f, 1.0f), bar.h}, ui.s(3), ui.theme().accent);
            // While it plays: « 0:03 / 0:12 » (a clock), else its length.
            auto clock = [](int64_t ms) {
                char b[24];
                const int64_t s = std::max<int64_t>(0, ms) / 1000;
                std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
                return std::string(b);
            };
            const std::string t = audioStarted_ ? clock(at) + " / " + clock(total) : formatDuration(total);
            const float fs = ui.fitFont(t, FontFace::Bold, 18, in.w);
            ui.label(t, {in.center().x, barY + ui.s(24)}, fs, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
            if (!audioError_.empty()) {
                const float ey = barY + ui.s(44);
                if (ey + ui.s(18) <= in.bottom()) {
                    const float es = ui.fitFont(audioError_, FontFace::Body, 15, in.w, 0.7f);
                    ui.label(ui.elide(audioError_, FontFace::Body, es, in.w), {in.center().x, ey}, es, ui.theme().danger, Align::Center, FontFace::Body,
                             VAlign::Middle);
                }
            }
            return;
        }
        case AssetType::Data: {
            if (textJob_) return waiting("Chargement…");
            if (lines_.empty()) return waiting("Fichier vide.");
            const float lh = ui.s(20);
            const size_t n = lines_.size() + (textCut_ ? 1 : 0);
            ui.pushId("apreview");
            ui.beginScroll("text", in, static_cast<float>(n) * lh);
            const float y0 = in.y - ui.scrollOffset();
            const size_t first = static_cast<size_t>(std::max(0.0f, (in.y - y0) / lh));
            i18n::Verbatim raw;  // a file's own words: never translated
            const float room = in.w - ui.s(16);
            for (size_t i = first; i < n; ++i) {
                const float y = y0 + static_cast<float>(i) * lh;
                if (y > in.bottom()) break;
                static const std::string kMore = "…";
                const std::string& line = i < lines_.size() ? lines_[i] : kMore;
                if (line.empty()) continue;
                ui.label(ui.elide(line, FontFace::Body, 15, room), {in.x, y + lh * 0.5f}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            }
            ui.endScroll();
            ui.popId();
            return;
        }
        case AssetType::Count: break;
    }
}

void AssetPreview::drawControls(const Rect& r) {
    UIContext& ui = g_.ui;
    if (!entry_) return;
    ui.pushId("apreview");
    UIContext::ButtonStyle bs;
    bs.small = true;
    bs.fontSize = 17;
    const float gap = ui.s(10);
    if (entry_->type == AssetType::Animation) {
        // [clip ◂▸] [− 12 images/s +] [Pause]
        const float playW = std::max(ui.text().measure("Pause", FontFace::Bold, ui.s(17)).x, ui.text().measure("Lecture", FontFace::Bold, ui.s(17)).x) + ui.s(36);
        const float stepW = ui.s(44);
        const std::string fpsText = std::to_string(fps_) + " images/s";
        const float fpsRoomWanted = ui.text().measure("60 images/s", FontFace::Bold, ui.s(17)).x + ui.s(12);
        float clipW = r.w - playW - 2 * stepW - fpsRoomWanted - 3 * gap;
        float fpsRoom = fpsRoomWanted;
        if (clipW < ui.s(140)) {  // narrow: the label of the speed gives way first
            fpsRoom = std::max(ui.s(40), fpsRoom - (ui.s(140) - clipW));
            clipW = r.w - playW - 2 * stepW - fpsRoom - 3 * gap;
        }
        clipW = std::max(ui.s(60), clipW);
        float x = r.x;
        if (!clipNames_.empty()) {
            // Clip names are the sheet's ids: shown as they are, shortened to their room.
            i18n::Verbatim raw;
            std::vector<std::string> shown;
            const float optRoom = clipW - ui.s(6) - ui.s(36) - ui.s(44) - ui.s(8);
            for (const std::string& n : clipNames_) shown.push_back(ui.elide(n, FontFace::Bold, 11, std::max(ui.s(10), optRoom)));
            int c = clip_;
            if (ui.cycle("clip", {x, r.y, clipW, r.h}, "", c, shown) && c != clip_) {
                clip_ = c;
                animT_ = 0.0f;
                fps_ = clipFps(anim_ ? anim_->clip(clipNames_[static_cast<size_t>(clip_)]) : nullptr);
            }
        }
        x += clipW + gap;
        UIContext::ButtonStyle ss = bs;
        ss.enabled = fps_ > 1;
        if (ui.button("slower", {x, r.y, stepW, r.h}, "−", ss)) fps_ = std::max(1, fps_ - 1);
        x += stepW;
        const float fs = ui.fitFont(fpsText, FontFace::Bold, 17, fpsRoom - ui.s(8), 0.6f);
        ui.label(ui.elide(fpsText, FontFace::Bold, fs, fpsRoom - ui.s(8)), {x + fpsRoom * 0.5f, r.center().y}, fs, ui.theme().text, Align::Center,
                 FontFace::Bold, VAlign::Middle);
        x += fpsRoom;
        ss.enabled = fps_ < 60;
        if (ui.button("faster", {x, r.y, stepW, r.h}, "+", ss)) fps_ = std::min(60, fps_ + 1);
        x += stepW + gap;
        if (ui.button("pause", {r.right() - playW, r.y, playW, r.h}, animPaused_ ? "Lecture" : "Pause", bs)) animPaused_ = !animPaused_;
    } else {
        // [Écouter / Arrêter]   0:12
        const bool on = playing();
        const float bw = std::max(ui.text().measure("Écouter", FontFace::Bold, ui.s(17)).x, ui.text().measure("Arrêter", FontFace::Bold, ui.s(17)).x) + ui.s(40);
        UIContext::ButtonStyle ps = bs;
        ps.primary = true;
        ps.enabled = g_.app.audio().available();
        if (ui.button("listen", {r.x, r.y, std::min(bw, r.w), r.h}, on ? "Arrêter" : "Écouter", ps)) {
            if (on) stopAudio();
            else playAudio();
        }
        const float room = r.w - bw - gap;
        if (room > ui.s(30)) {
            const std::string t = g_.app.audio().available()
                                      ? (entry_->type == AssetType::Music ? std::string("La musique du menu attend pendant l'écoute.")
                                                                          : std::string("La musique du menu baisse pendant l'écoute."))
                                      : std::string("Aucun périphérique audio.");
            const float fs = ui.fitFont(t, FontFace::Body, 15, room, 0.7f);
            ui.label(ui.elide(t, FontFace::Body, fs, room), {r.x + bw + gap, r.center().y}, fs, ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
        }
    }
    ui.popId();
}

}  // namespace cosmic
