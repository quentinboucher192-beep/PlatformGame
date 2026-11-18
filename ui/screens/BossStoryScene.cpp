#include "ui/screens/BossStoryScene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/core/I18n.h"
#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/enemies/BossDifficulty.h"
#include "game/tutorial/Tutorial.h"
#include "game/enemies/EnemyDefinition.h"
#include "ui/online/RatingWidgets.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/MenuBackground.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float smooth(float v) {
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}
float easeInOut(float v) { return 0.5f - 0.5f * std::cos(kPi * clamp01(v)); }

/// A number in [0, 1) from an integer (particles: the same ones every frame).
float hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFU) / 16777216.0f;
}

/// The first `n` characters (code points) of a UTF-8 text.
std::string prefix(const std::string& s, size_t n) {
    size_t i = 0, count = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (count == n) break;
            ++count;
        }
        ++i;
    }
    return s.substr(0, i);
}

/// The words of a line of narration; a word of punctuation stays with its neighbour (« : »,
/// « ? », « » », and « « » with the next word): a line never starts with one of them.
std::vector<std::string> narrationWords(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t sp = text.find(' ', start);
        std::string w = text.substr(start, sp == std::string::npos ? std::string::npos : sp - start);
        start = sp == std::string::npos ? text.size() + 1 : sp + 1;
        if (w.empty()) continue;
        const bool closing = w[0] == ':' || w[0] == ';' || w[0] == '!' || w[0] == '?' || w.rfind("\xC2\xBB", 0) == 0;  // »
        if (closing && !out.empty()) {
            out.back() += " " + w;
            continue;
        }
        if (!out.empty() && out.back() == "\xC2\xAB") {  // «
            out.back() += " " + w;
            continue;
        }
        out.push_back(w);
    }
    return out;
}

/// The words wrapped greedily into lines no wider than `width` at `px` (text as displayed).
std::vector<std::string> wrapWords(TextRenderer& tr, const std::vector<std::string>& words, float px, float width) {
    std::vector<std::string> lines;
    std::string cur;
    for (const auto& w : words) {
        std::string cand = cur.empty() ? w : cur + " " + w;
        if (!cur.empty() && tr.measure(cand, FontFace::Body, px).x > width) {
            lines.push_back(cur);
            cur = w;
        } else {
            cur = std::move(cand);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

struct SkipHintMetrics {
    float h = 0, fs = 0, space = 0, keyW = 0, dotW = 0, txtW = 0, total = 0;
    // 2.63: « Échap · Ⓑ Quitter » after it (« Passer » skips the shot, « Quitter » the story)
    float quitKeyW = 0, quitTxtW = 0, quitW = 0, gap = 0;
};

/// The parts of « Espace · Ⓐ  Passer   Échap · Ⓑ  Quitter », measured.
SkipHintMetrics skipHintMetrics(UIContext& ui) {
    SkipHintMetrics m;
    m.h = std::max(18.0f, ui.s(30));
    m.fs = m.h * 0.6f;
    m.space = m.h * 0.28f;
    m.keyW = Glyphs::drawBinding(ui, SDL_SCANCODE_SPACE, PadBinding{}, false, {0, 0}, m.h, false);
    m.dotW = ui.text().measure("·", FontFace::Bold, m.fs).x;
    m.txtW = ui.text().measure("Passer", FontFace::Body, m.fs).x;
    m.quitKeyW = Glyphs::drawBinding(ui, SDL_SCANCODE_ESCAPE, PadBinding{}, false, {0, 0}, m.h, false);
    m.quitTxtW = ui.text().measure("Quitter", FontFace::Body, m.fs).x;
    m.quitW = m.quitKeyW + m.space + m.dotW + m.space + m.h + m.space * 1.4f + m.quitTxtW;
    m.gap = m.h * 1.1f;
    m.total = m.keyW + m.space + m.dotW + m.space + m.h + m.space * 1.4f + m.txtW + m.gap + m.quitW;
    return m;
}

class BossStoryScene : public Scene {
public:
    BossStoryScene(GameServices& g, const BossStory& story, int startAt, float atSeconds, bool attract = false)
        : g_(g), story_(story), startAt_(atSeconds >= 0.0f ? -1 : startAt), atSeconds_(atSeconds), trailer_(story.isTrailer()),
          attract_(attract) {}
    /// 2.68: a film, owned by the scene (`locked`: its first viewing, nothing skips it).
    BossStoryScene(GameServices& g, std::shared_ptr<const BossStory> film, bool locked, std::function<void()> onEnd, float atSeconds)
        : keep_(std::move(film)), g_(g), story_(*keep_), atSeconds_(atSeconds), film_(true), locked_(locked), onEnd_(std::move(onEnd)) {}

    bool locked() const { return locked_; }

    /// 2.63: the shot playing (the number of shots: the title card).
    int shotIndex() const { return t_ >= titleStart_ ? static_cast<int>(shots_.size()) : currentShot(); }
    /// 2.63: the trailer's passes (attract mode: it starts again at its end).
    int passes() const { return passes_; }
    bool attract() const { return attract_; }

    void onEnter() override {
        def_ = trailer_ || film_ ? nullptr : g_.content.enemy(story_.bossId);
        // The timeline, from the lines as they are shown (a shot lasts long enough to read its
        // line in the language in use).
        // Every texture of the story is loaded here, at its start (never while it plays): the
        // paintings, the scenes' layers and sprite sheets, the boss's own sheet (title card).
        float t = 0.0f;
        for (const StoryShot& s : story_.shots) {
            Shot shot;
            shot.data = &s;
            shot.shown = i18n::tr(s.text);
            shot.words = narrationWords(shot.shown);
            shot.start = t;
            shot.dur = story::shotDuration(shot.shown);
            // A scene is written in seconds of its French line: stretched to the line shown.
            shot.stageScale = story::shotDuration(s.text) / std::max(0.01f, shot.dur);
            if (s.duration > 0.0f) {  // 2.63 (the trailer): its own length, in every language
                shot.dur = s.duration;
                // 2.68 (a film): a scene written for another length is stretched to the shot's.
                shot.stageScale = s.stage && s.stage->length > 0.0f ? s.stage->length / s.duration : 1.0f;
            }
            if (s.stage) shot.stageReady = loadStage(shot, *s.stage);
            if (!shot.stageReady) shot.tex = s.image.empty() ? nullptr : g_.app.resources().texture(s.image, false);
            t += shot.dur;
            shots_.push_back(std::move(shot));
        }
        if (def_ && !def_->animation.empty()) g_.app.resources().animation(def_->animation);
        titleStart_ = t;
        total_ = t + story_.titleCard;
        if (startAt_ >= 0) {
            // Automated screenshots: that shot with its line typed, or the title card revealed.
            t_ = startAt_ < static_cast<int>(shots_.size()) ? shots_[static_cast<size_t>(startAt_)].start + 1.4f : titleStart_ + 1.8f;
            sinceEnter_ = 10.0f;
        }
        if (atSeconds_ >= 0.0f) {  // automated screenshots: that moment of the story, as it plays
            t_ = std::min(atSeconds_, total_ - 0.01f);
            sinceEnter_ = 10.0f;
        }
        roarLen_ = def_ ? creatureClipDuration(g_, *def_, story_.clip) : 0.0f;
        activity_ = g_.app.input().activityCount();
        if (trailer_ && !story_.music.empty()) {
            g_.playMusic(story_.music);  // the trailer's own music
            baseDuck_ = 1.0f;
        }
        if (film_) {  // 2.68: a film's own music, once; the one playing before comes back after it
            prevMusic_ = g_.app.audio().currentMusic();
            if (const std::string p = g_.app.resources().soundPath(story_.music); !story_.music.empty() && !p.empty())
                g_.app.audio().playMusic(p, 600, false);
            baseDuck_ = 1.0f;
        }
        setDuck(baseDuck_);
        // 2.63: its sounds are decoded here too (never while it plays).
        ResourceManager& res = g_.app.resources();
        for (const StoryShot& s : story_.shots)
            if (s.stage)
                for (const StorySfx& fx : s.stage->sounds)
                    if (const std::string p = res.soundPath(fx.sound); !p.empty()) g_.app.audio().load(p, true);
    }

    void onExit() override {
        g_.app.audio().setMusicDuck(1.0f);
        if (film_) {  // 2.68: the music of the screen below again
            if (!prevMusic_.empty()) g_.app.audio().playMusic(prevMusic_, 1200);
            else g_.app.audio().stopMusic(800);
        }
    }

    bool isOverlay() const override { return true; }
    bool holdsNotifications() const override { return true; }

    void update(float dt) override {
        sinceEnter_ += dt;
        if (closing_) {
            closeT_ += dt;
            if (closeT_ >= fadeOut()) finish();
            return;
        }
        // 2.63, attract mode: any input (a key, a button, a click, the mouse or a stick moving)
        // closes it at once; it is shown over the menu until it is gone, so that the menu never
        // acts on that input.
        if (attract_) {
            const uint64_t act = g_.app.input().activityCount();
            if (act != activity_ && sinceEnter_ > 0.05f) {
                closing_ = true;
                quickClose_ = true;
                return;
            }
            activity_ = act;
        }
        t_ += dt;
        if (attract_ && t_ >= total_) {  // attract mode: it starts again, through black
            t_ = 0.0f;
            roared_ = false;
            ++passes_;
            sfxShot_ = -1;
            return;
        }
        playSounds();
        if (!roared_ && t_ >= titleStart_ + 0.2f) {
            roared_ = true;
            if (def_ && !def_->alertSound.empty()) g_.app.resources().playSound(def_->alertSound, 0.5f);
        }
        if (t_ >= total_) {
            closing_ = true;
            if (film_ && !ended_) {  // 2.68: played to its end (its first viewing is now seen)
                ended_ = true;
                if (onEnd_) onEnd_();
            }
            return;
        }
        // 2.68: a film's first viewing: nothing skips it or leaves it (no key, button or click).
        if (locked_) return;
        // 2.63: « Passer » (Space, Enter, Ⓐ, a click) skips the shot playing — a quick cross-fade
        // to the next one; on the last shot or the title card, the story ends. « Quitter »
        // (Échap, Ⓑ, a click on its hint) leaves the whole story. Never by a press of the
        // fight's first instant (the player may be jumping).
        InputManager& in = g_.app.input();
        const bool click = in.mousePressed(SDL_BUTTON_LEFT);
        const bool quit = in.menuPressed(Action::MenuBack) || (click && quitRect_.contains(in.mousePos()));
        const bool skip = !quit && (in.menuPressed(Action::MenuConfirm) || in.keyPressed(SDL_SCANCODE_SPACE) || click);
        if (sinceEnter_ <= 0.6f) return;
        if (quit) {
            closing_ = true;
            g_.uiSound("ui_back", 0.35f);
        } else if (skip) {
            skipShot();
            g_.uiSound("ui_confirm", 0.35f);
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Renderer& r = ui.r();
        const float in = startAt_ >= 0 || atSeconds_ >= 0.0f ? 1.0f : clamp01(sinceEnter_ / story::kFade);
        const float out = closing_ ? 1.0f - clamp01(closeT_ / fadeOut()) : 1.0f;
        const float alpha = std::min(in, out);
        // Faded in and out over the screen below: drawn on a target, then shown with its alpha.
        bool onTarget = false;
        if (alpha < 0.999f) {
            if (!target_ || target_->width() != r.width() || target_->height() != r.height()) target_ = r.createTarget(r.width(), r.height(), true);
            if (target_) {
                r.setTarget(target_);
                SDL_SetRenderDrawColor(r.sdl(), 0, 0, 0, 255);
                SDL_RenderClear(r.sdl());
                onTarget = true;
            }
        }
        drawTarget_ = onTarget ? target_ : nullptr;
        drawStory();
        if (onTarget) {
            r.setTarget(nullptr);
            // 2.68: a film rises from black (the screen below shows only as it fades out at its end).
            if (film_ && !closing_) r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{0, 0, 0, 255});
            r.drawTexture(*target_, nullptr, {0, 0, ui.screenW(), ui.screenH()}, Color::white().withAlphaF(alpha));
        }
    }

private:
    struct Shot {
        const StoryShot* data = nullptr;
        std::string shown;  // the line as displayed (translated)
        std::vector<std::string> words;
        float start = 0.0f, dur = 0.0f;
        TexturePtr tex;
        // Version 2: the scene's files, per element (loaded at the start), whether all are there.
        std::vector<TexturePtr> layers;
        std::vector<AnimationSetPtr> sheets;
        std::vector<std::vector<const AnimationClip*>> clips;  // per sprite, the clip each key starts
        bool stageReady = false;
        float stageScale = 1.0f;  // seconds of the scene per second of the shot
    };

    /// Loads the files of a scene; false (its painting instead) when one is missing.
    bool loadStage(Shot& shot, const StoryStage& st) {
        ResourceManager& res = g_.app.resources();
        shot.layers.assign(st.elements.size(), nullptr);
        shot.sheets.assign(st.elements.size(), nullptr);
        shot.clips.assign(st.elements.size(), {});
        bool ok = true;
        for (size_t k = 0; k < st.elements.size(); ++k) {
            const StoryElement& e = st.elements[k];
            if (e.kind == StoryElementKind::Layer) {
                if (res.exists(e.source)) shot.layers[k] = res.texture(e.source, true);
                else ok = false;
            } else if (e.kind == StoryElementKind::Sprite) {
                AnimationSetPtr set = res.exists(e.source) ? res.animation(e.source) : nullptr;
                if (set && set->texture && res.exists(set->texturePath)) {
                    shot.sheets[k] = set;
                    // found once, here: drawing a frame never looks a clip up by its name
                    for (const StoryKey& key : e.keys) shot.clips[k].push_back(key.clip.empty() ? nullptr : set->clip(key.clip));
                } else {
                    ok = false;
                }
            }
            if (!ok) {
                Log::warn("Histoire de {} : « {} » introuvable, le plan garde sa peinture", story_.bossId, e.source);
                return false;
            }
        }
        return true;
    }

    /// The fade at its end: quick when left by an input in attract mode, slow at a film's end.
    float fadeOut() const { return quickClose_ ? kQuickFade : film_ && ended_ ? kFilmFade : story::kFade; }

    void finish() {
        if (done_) return;
        done_ = true;
        g_.app.popScene();
    }

    int currentShot() const {
        int cur = 0;
        for (size_t i = 0; i < shots_.size(); ++i)
            if (t_ >= shots_[i].start) cur = static_cast<int>(i);
        return cur;
    }

    void drawStory() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float W = ui.screenW(), H = ui.screenH();
        r.fillRect({0, 0, W, H}, Color{0, 0, 0, 255});
        const float band = std::round(H * 0.13f);
        const Rect img{0, band, W, H - 2.0f * band};
        r.pushClip(img);
        const bool title = !film_ && t_ >= titleStart_;  // a film has no title card: its last shot stays
        if (!title) {
            const int cur = currentShot();
            const float a = cur > 0 ? smooth((t_ - shots_[static_cast<size_t>(cur)].start) / crossFade()) : 1.0f;
            if (cur > 0 && a < 1.0f) drawShot(cur - 1, img, 1.0f);
            drawShot(cur, img, a);
        } else {
            const float a = smooth((t_ - titleStart_) / crossFade());
            if (a < 1.0f && !shots_.empty()) drawShot(static_cast<int>(shots_.size()) - 1, img, 1.0f);
            if (trailer_) drawTrailerCard(img, a);
            else drawTitle(img, a);
        }
        if (trailer_ && !title) drawTrailerTitle(currentShot(), img);
        if (film_) drawFilmTitle(currentShot(), img);
        if (attract_) {  // between two passes: through black
            const float through = std::min(clamp01(t_ / story::kFade), clamp01((total_ - t_) / story::kFade));
            if (passes_ > 0 || t_ > story::kFade) r.fillRect(img, Color{0, 0, 0, 255}.withAlphaF(1.0f - through));
        }
        // Soft edges against the bands.
        const float soft = std::round(img.h * 0.08f);
        r.fillGradientV({img.x, img.y, img.w, soft}, Color{0, 0, 0, 170}, Color{0, 0, 0, 0});
        r.fillGradientV({img.x, img.bottom() - soft, img.w, soft}, Color{0, 0, 0, 0}, Color{0, 0, 0, 170});
        r.popClip();
        r.fillRect({img.x, img.y - std::max(1.0f, ui.s(1.5f)), img.w, std::max(1.0f, ui.s(1.5f))}, story_.color.withAlpha(60));
        r.fillRect({img.x, img.bottom(), img.w, std::max(1.0f, ui.s(1.5f))}, story_.color.withAlpha(60));

        if (trailer_) {  // the trailer: its titles are in the picture; below, how to leave it
            drawTrailerHint({0, img.bottom(), W, H - img.bottom()});
            return;
        }
        // The bottom band: progress bar on its top, the line of narration, « Passer » at the right
        // (the line keeps to the room left of the measured hint: layoutStoryNarration).
        const int cur = currentShot();
        const StoryNarrationLayout lay = layoutStoryNarration(ui, title ? std::string() : shots_[static_cast<size_t>(cur)].shown, !locked_);
        drawProgress(lay.bar);
        if (!title) drawNarration(cur, lay, img);
        if (!locked_) drawSkipHint({lay.hint.right(), lay.hint.center().y});  // a first viewing: no hint at all
    }

    /// The painting of shot `i` covering `img`, zooming slowly towards its focus, its particles over it.
    void drawShot(int i, const Rect& img, float alpha) {
        if (i < 0 || i >= static_cast<int>(shots_.size()) || alpha <= 0.0f) return;
        Renderer& r = g_.ui.r();
        const Shot& s = shots_[static_cast<size_t>(i)];
        const float local = t_ - s.start;
        const float p = easeInOut(local / (s.dur + crossFade()));
        if (s.stageReady) {
            Renderer& rr = g_.ui.r();
            if (alpha < 0.999f) {
                // Cross-fading: the scene drawn whole on its own target, then shown with its alpha
                // (its layers never show through one another).
                if (!shotTarget_ || shotTarget_->width() != rr.width() || shotTarget_->height() != rr.height())
                    shotTarget_ = rr.createTarget(rr.width(), rr.height(), true);
            }
            if (alpha < 0.999f && shotTarget_) {
                rr.setTarget(shotTarget_);
                SDL_SetRenderDrawColor(rr.sdl(), 0, 0, 0, 0);
                SDL_RenderClear(rr.sdl());
                drawStage(i, img, 1.0f, local, p);
                rr.setTarget(drawTarget_);
                rr.drawTexture(*shotTarget_, &img, img, Color::white().withAlphaF(alpha));
            } else {
                drawStage(i, img, alpha, local, p);
            }
        } else if (s.tex && s.tex->width() > 0 && s.tex->height() > 0) {
            const float iw = static_cast<float>(s.tex->width()), ih = static_cast<float>(s.tex->height());
            const float cover = std::max(img.w / iw, img.h / ih);
            const Vec2 size0{iw * cover, ih * cover};
            const Vec2 tl0{img.x + (img.w - size0.x) * 0.5f, img.y + (img.h - size0.y) * 0.5f};
            const Vec2 f = s.data->focus;
            const Vec2 anchor0{tl0.x + f.x * size0.x, tl0.y + f.y * size0.y};
            // The focus stays where it is while the painting grows around it, gliding a little
            // towards the middle of the frame; the painting always covers the frame.
            const Vec2 anchor = lerpv(anchor0, img.center(), 0.25f * p);
            const float z = 1.0f + (s.data->zoom - 1.0f) * p;
            const Vec2 size{size0.x * z, size0.y * z};
            Vec2 tl{anchor.x - f.x * size.x, anchor.y - f.y * size.y};
            tl.x = std::clamp(tl.x, img.right() - size.x, img.x);
            tl.y = std::clamp(tl.y, img.bottom() - size.y, img.y);
            r.drawTexture(*s.tex, nullptr, {tl.x, tl.y, size.x, size.y}, Color::white().withAlphaF(alpha));
        } else {
            // Without its painting: the shot's colours.
            r.fillGradientV(img, s.data->glow.scaled(0.25f).withAlphaF(alpha), Color{4, 4, 10, 255}.withAlphaF(alpha));
            r.drawGlow(img.center(), img.h * 0.4f, s.data->glow.withAlphaF(0.5f * alpha));
        }
        drawParticles(s.data->particles, s.data->glow, img, t_, alpha, static_cast<uint32_t>(i) * 7919U + 17U);
    }

    /// Shot `i` played as its scene, `local` seconds in, covering `img`: the camera (with the slow
    /// zoom of every shot, `p` of the way), each element in its order at its depth, the shakes, the
    /// flashes.
    void drawStage(int i, const Rect& img, float alpha, float local, float p) {
        Renderer& r = g_.ui.r();
        const Shot& s = shots_[static_cast<size_t>(i)];
        const StoryStage& st = *s.data->stage;
        const float tau = local * s.stageScale;
        const float base = std::max(img.w / st.size.x, img.h / st.size.y);
        auto [cam, zoom] = st.cameraAt(tau);
        zoom *= 1.0f + (s.data->zoom - 1.0f) * p;
        cam = st.clampCamera(cam, {img.w / (base * zoom), img.h / (base * zoom)});
        const Vec2 shake = st.shakeAt(tau) * base;
        const Vec2 c0 = st.size * 0.5f;
        r.fillRect(img, st.backdrop.withAlphaF(alpha));
        for (size_t k = 0; k < st.elements.size(); ++k) {
            const StoryElement& e = st.elements[k];
            const StoryElementState es = e.at(tau);
            const float a = es.alpha * alpha * e.pulseAt(tau);
            if (a <= 0.003f) continue;
            const float kd = base * (1.0f + (zoom - 1.0f) * e.depth);  // far layers zoom less
            const Vec2 cd = c0 + (cam - c0) * e.depth;                   // and move less
            auto scr = [&](Vec2 q) { return img.center() + (q - cd) * kd + shake; };
            const Color col = e.tint.withAlphaF(a * static_cast<float>(e.tint.a) / 255.0f);
            const BlendMode blend = e.additive ? BlendMode::Additive : BlendMode::Alpha;
            switch (e.kind) {
                case StoryElementKind::Layer: {
                    const TexturePtr& tex = s.layers[k];
                    if (!tex) break;
                    const float sc = kStoryPixel * es.scale * kd;
                    const Vec2 size{static_cast<float>(tex->width()) * sc, static_cast<float>(tex->height()) * sc};
                    const Vec2 tl = scr(es.pos + e.drift * tau);
                    if (e.repeat && size.x >= 1.0f) {
                        float x = img.x + std::fmod(tl.x - img.x, size.x);
                        if (x > img.x) x -= size.x;
                        for (; x < img.right(); x += size.x) r.drawTexture(*tex, nullptr, {x, tl.y, size.x, size.y}, col, blend);
                    } else {
                        r.drawTexture(*tex, nullptr, {tl.x, tl.y, size.x, size.y}, col, blend);
                    }
                    break;
                }
                case StoryElementKind::Sprite: {
                    const AnimationSetPtr& set = s.sheets[k];
                    if (!set || !set->texture) break;
                    const AnimationClip* clip = es.clipKey >= 0 ? s.clips[k][static_cast<size_t>(es.clipKey)] : nullptr;
                    const Rect src = storySpriteFrame(*set, clip, es.clipTime);
                    if (src.w <= 0.0f) break;
                    const float sc = kStoryPixel * es.scale * kd;
                    Vec2 pv = set->pivot;
                    if (es.flip) pv.x = static_cast<float>(set->frameWidth) - pv.x;
                    const Vec2 at = scr(es.pos + e.drift * tau);
                    const Vec2 pivot = pv * sc;
                    if (e.shadow.on && r.glowTexture()) {
                        // its shadow on the ground, pushed away from its light
                        float off = 0.0f;
                        if (e.shadow.light) off = std::clamp((es.pos.x - e.shadow.light->x) * 0.12f, -40.0f, 40.0f);
                        const Vec2 c = scr({es.pos.x + off, es.pos.y});
                        const Vec2 size = e.shadow.size * (es.scale * kd);
                        r.drawTexture(*r.glowTexture(), nullptr, Rect::fromCenter(c, size), Color{0, 0, 0, 255}.withAlphaF(a * e.shadow.alpha));
                    }
                    if (e.repeat && src.w * sc >= 1.0f) {  // scenery: the sheet repeated along x
                        const float step = src.w * sc;
                        float x = img.x + std::fmod(at.x - pivot.x - img.x, step);
                        if (x > img.x) x -= step;
                        for (; x < img.right(); x += step) r.drawTexture(*set->texture, &src, {x, at.y - pivot.y, step, src.h * sc}, col, blend, es.flip);
                    } else {
                        r.drawTexture(*set->texture, &src, {at.x - pivot.x, at.y - pivot.y, src.w * sc, src.h * sc}, col, blend, es.flip,
                                      false, es.rot, &pivot);
                    }
                    break;
                }
                case StoryElementKind::Glow:
                    r.drawGlow(scr(es.pos), e.radius * es.scale * kd, e.color.withAlphaF(a));
                    break;
                case StoryElementKind::Particles: {
                    const Vec2 tl = scr({e.area.x, e.area.y});
                    drawParticles(e.source, e.color, {tl.x, tl.y, e.area.w * kd, e.area.h * kd}, tau + static_cast<float>(k) * 1.7f, a,
                                  static_cast<uint32_t>(i) * 977U + static_cast<uint32_t>(k) * 131U + 7U);
                    break;
                }
            }
        }
        Color fc = Color::white();
        if (const float f = st.flashAt(tau, &fc) * alpha; f > 0.003f) r.fillRect(img, fc.withAlphaF(f), BlendMode::Additive);
    }

    /// A few live particles over a painting (display only).
    void drawParticles(const std::string& kind, Color c, const Rect& area, float t, float alpha, uint32_t seed) {
        if (kind == "none" || alpha <= 0.0f) return;
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float u = std::max(0.5f, ui.s(1.0f));
        const int n = kind == "stars" ? 46 : kind == "dust" ? 34 : kind == "sparks" ? 26 : kind == "ember_rain" ? 40 : kind == "ash" ? 44 : 32;
        if (kind == "ember_rain") {
            // 2.60: burning embers falling down and to the left, each with its trail.
            for (int k = 0; k < n; ++k) {
                const uint32_t h = seed * 131U + static_cast<uint32_t>(k) * 2654435761U;
                const float a = hash01(h), b = hash01(h + 1), c2 = hash01(h + 2), d = hash01(h + 3);
                const float span = area.h + u * 80.0f;
                const float speed = u * (120.0f + 140.0f * b);
                const float y = std::fmod(t * speed + c2 * span, span);
                const Vec2 head{area.x + a * (area.w + span * 0.5f) - y * 0.5f, area.y - u * 40.0f + y};
                const Vec2 dir = Vec2{-0.447f, 0.894f};
                const float len = u * (14.0f + 22.0f * d);
                const float op = alpha * (0.5f + 0.5f * std::sin(t * (7.0f + 4.0f * d) + a * 30.0f)) * 0.8f + alpha * 0.2f;
                if (op <= 0.01f || head.x < area.x - len || head.x > area.right() + len) continue;
                r.drawLine(head - dir * len, head, c.withAlphaF(0.5f * op), std::max(1.0f, u * 1.5f), BlendMode::Additive);
                r.drawGlow(head, u * (6.0f + 6.0f * d), c.withAlphaF(0.5f * op));
                r.fillCircle(head, u * (1.2f + 1.2f * d), Color::lerp(c, Color::white(), 0.6f).withAlphaF(op), BlendMode::Additive);
            }
            return;
        }
        for (int k = 0; k < n; ++k) {
            const uint32_t h = seed * 131U + static_cast<uint32_t>(k) * 2654435761U;
            const float a = hash01(h), b = hash01(h + 1), c2 = hash01(h + 2), d = hash01(h + 3), e = hash01(h + 4);
            Vec2 p;
            float size = u * (1.5f + 2.5f * d), glow = 0.0f, op = 0.0f;
            Color pc = c;
            if (kind == "embers") {
                // Rising, swaying, flickering.
                const float span = area.h + u * 60.0f;
                const float rise = u * (28.0f + 46.0f * b);
                const float y = std::fmod(t * rise + c2 * span, span);
                p = {area.x + a * area.w + std::sin(t * (0.7f + e) + c2 * 6.28f) * u * 16.0f, area.bottom() + u * 30.0f - y};
                op = (0.55f + 0.45f * std::sin(t * (6.0f + 5.0f * e) + a * 20.0f)) * smooth(std::min(y / (span * 0.2f), (span - y) / (span * 0.3f)));
                glow = size * 5.0f;
            } else if (kind == "sparks") {
                // Twinkling in place.
                p = {area.x + a * area.w, area.y + (0.08f + 0.84f * b) * area.h};
                const float tw = std::sin(t * (1.2f + 2.2f * c2) + d * 6.28f);
                op = tw > 0.0f ? tw * tw * tw : 0.0f;
                size *= 0.9f;
                glow = u * (10.0f + 14.0f * e);
            } else if (kind == "stars") {
                p = {area.x + a * area.w, area.y + b * area.h};
                op = 0.35f + 0.65f * std::fabs(std::sin(t * (0.6f + 1.6f * c2) + d * 6.28f));
                size = u * (0.8f + 1.4f * e);
                glow = size * 3.0f;
            } else if (kind == "snow") {
                const float span = area.h + u * 40.0f;
                const float y = std::fmod(t * u * (18.0f + 26.0f * b) + c2 * span, span);
                p = {area.x + a * area.w + std::sin(t * (0.5f + e) + c2 * 6.28f) * u * 22.0f, area.y - u * 20.0f + y};
                op = 0.7f;
                glow = size * 2.5f;
            } else if (kind == "ash") {
                // 2.60: ash lifted by the wind: blown along in gusts, rising and sinking.
                const float span = area.w + u * 60.0f;
                const float gust = 1.0f + 0.6f * std::sin(t * 0.9f + c2 * 3.0f);
                const float x = std::fmod(a * span + t * u * (30.0f + 40.0f * b) * gust, span);
                p = {area.x - u * 30.0f + x, area.bottom() - (0.1f + 0.85f * c2) * area.h + std::sin(t * (1.1f + e) + d * 6.28f) * u * 14.0f};
                op = 0.35f + 0.4f * std::fabs(std::sin(t * 1.3f + e * 6.28f));
                size *= 0.6f;
                glow = 0.0f;
                pc = Color::lerp(Color{150, 140, 150, 255}, c, 0.35f);
            } else if (kind == "dustfall") {
                // 2.60: dust falling slowly from the spires.
                const float span = area.h + u * 30.0f;
                const float y = std::fmod(t * u * (10.0f + 14.0f * b) + c2 * span, span);
                p = {area.x + a * area.w + std::sin(t * (0.6f + e) + c2 * 6.28f) * u * 6.0f, area.y - u * 15.0f + y};
                op = 0.45f * smooth(std::min(y / (span * 0.15f), (span - y) / (span * 0.25f)));
                size *= 0.55f;
                glow = size * 2.0f;
            } else {  // dust: drifting motes
                const float span = area.w + u * 40.0f;
                const float x = std::fmod(a * span + t * u * (6.0f + 12.0f * b), span);
                p = {area.x - u * 20.0f + x, area.y + (0.1f + 0.8f * c2) * area.h + std::sin(t * (0.4f + 0.5f * e) + d * 6.28f) * u * 12.0f};
                op = 0.25f + 0.35f * std::fabs(std::sin(t * 0.7f + e * 6.28f));
                size *= 0.8f;
                glow = size * 3.5f;
            }
            op *= alpha;
            if (op <= 0.01f) continue;
            if (glow > 0.0f) r.drawGlow(p, glow, pc.withAlphaF(0.45f * op));
            r.fillCircle(p, size, Color::lerp(pc, Color::white(), kind == "ash" ? 0.15f : 0.55f).withAlphaF(op), BlendMode::Additive);
        }
    }

    /// 2.63: « Passer »: on to the next shot (its scene from its start, cross-faded from this
    /// one), or the end of the story from its last shot or its title card.
    void skipShot() {
        const int cur = currentShot();
        if (t_ >= titleStart_ || cur + 1 >= static_cast<int>(shots_.size())) {
            closing_ = true;
            return;
        }
        t_ = shots_[static_cast<size_t>(cur + 1)].start;
    }

    void setDuck(float d) {
        if (std::fabs(d - duck_) < 1e-4f) return;
        duck_ = d;
        g_.app.audio().setMusicDuck(d);
    }

    /// 2.63: the sounds of the shot playing: a one-shot when the scene passes its time, a loop
    /// (an ambience) kept up while the scene is within it, fading in and out; a big sound lowers
    /// the music a little while it lasts. All on the « effets » volume (muted with it). Not
    /// refreshed any more (the next shot, the title card, skipped), a loop fades out by itself.
    void playSounds() {
        if (t_ >= titleStart_ || shots_.empty()) {
            setDuck(baseDuck_);
            return;
        }
        const int cur = currentShot();
        const Shot& s = shots_[static_cast<size_t>(cur)];
        if (!s.stageReady || !s.data->stage) return;
        const StoryStage& st = *s.data->stage;
        const float tau = (t_ - s.start) * s.stageScale;
        if (cur != sfxShot_) {
            sfxShot_ = cur;
            sfxTau_ = -1e-3f;
        }
        ResourceManager& res = g_.app.resources();
        const float end = s.dur * s.stageScale;
        const auto [cam, zoom] = st.cameraAt(tau);
        for (size_t k = 0; k < st.sounds.size(); ++k) {
            const StorySfx& fx = st.sounds[k];
            float pan = fx.pan.value_or(0.0f);
            if (!fx.pan && fx.at) pan = std::clamp((fx.at->x - cam.x) / (st.size.x * 0.5f / zoom), -1.0f, 1.0f) * 0.8f;
            if (fx.loop) {
                const float to = fx.to > 0.0f ? fx.to : end;
                if (tau < fx.t || tau > to) continue;
                const float f = fx.fade > 0.0f ? std::min(clamp01((tau - fx.t) / fx.fade), clamp01((to - tau) / fx.fade)) : 1.0f;
                res.sustainLoop("story/" + std::to_string(cur) + "/" + std::to_string(k), fx.sound, fx.volume * f);
            } else if (fx.t > sfxTau_ && fx.t <= tau) {
                res.playSound(fx.sound, fx.volume, fx.pitch, pan);
                if (fx.duck) {
                    const int64_t ms = g_.app.audio().fileDurationMs(res.soundPath(fx.sound));
                    duckUntil_ = std::max(duckUntil_, t_ + (ms > 0 ? static_cast<float>(ms) / 1000.0f : 1.2f));
                }
            }
        }
        sfxTau_ = tau;
        setDuck(baseDuck_ * (t_ < duckUntil_ ? 0.7f : 1.0f));
    }

    /// 2.63, the trailer: cuts are quicker than a story's cross-fades.
    float crossFade() const { return trailer_ ? 0.18f : story::kCrossFade; }

    /// 2.63, the trailer: the title of shot `i` in large letters where its focus says (a pop as it
    /// comes, a fade as the shot ends), its subtitle under it; both fitted to the picture's width.
    void drawTrailerTitle(int i, const Rect& img) {
        if (i < 0 || i >= static_cast<int>(shots_.size())) return;
        UIContext& ui = g_.ui;
        const Shot& s = shots_[static_cast<size_t>(i)];
        const float local = t_ - s.start;
        const float in = smooth((local - 0.12f) / 0.28f);
        const float out = clamp01((s.dur - local) / 0.22f);
        const float a = in * out;
        if (a <= 0.003f) return;
        const std::string sub = s.data->subtitle.empty() ? std::string() : i18n::tr(s.data->subtitle);
        i18n::Verbatim asShown;  // both already translated
        const float maxW = img.w * 0.86f;
        const float pop = 1.0f + 0.14f * (1.0f - in);
        const float titlePx = ui.s(ui.fitFont(s.shown, FontFace::Title, 70, maxW / pop, 0.25f)) * pop;
        const float subPx = sub.empty() ? 0.0f : ui.s(ui.fitFont(sub, FontFace::Body, 30, maxW, 0.4f));
        const float gap = sub.empty() ? 0.0f : ui.s(14);
        const float blockH = titlePx + gap + subPx;
        float y = img.y + img.h * s.data->focus.y - blockH * 0.5f;
        y = std::clamp(y, img.y + ui.s(12), img.bottom() - blockH - ui.s(12));
        const Color c = s.data->glow;
        TextStyle st;
        st.face = FontFace::Title;
        st.size = titlePx;
        st.color = Color::white().withAlphaF(a);
        st.align = Align::Center;
        st.shadow = true;
        st.shadowOffset = ui.s(3);
        st.shadowColor = Color{0, 0, 0, static_cast<uint8_t>(200.0f * a)};
        st.glow = ui.s(18);
        st.glowColor = c.withAlphaF(0.55f * a);
        const float cx = img.x + img.w * s.data->focus.x;
        ui.text().draw(s.shown, {cx, y}, st);
        if (sub.empty()) return;
        TextStyle ss;
        ss.face = FontFace::Body;
        ss.size = subPx;
        ss.color = Color::lerp(c, Color::white(), 0.55f).withAlphaF(a * smooth((local - 0.3f) / 0.3f));
        ss.align = Align::Center;
        ss.shadow = true;
        ss.shadowColor = Color{0, 0, 0, static_cast<uint8_t>(190.0f * a)};
        ui.text().draw(sub, {cx, y + titlePx + gap}, ss);
    }

    /// 2.68, a film: the title of shot `i` showing now (the latest begun of those whose time it is,
    /// its times stretched with the scene), large in the picture where it says, fitted to its width,
    /// its subtitle under it; it pops in and fades out. One at a time: two never overlap.
    void drawFilmTitle(int i, const Rect& img) {
        if (i < 0 || i >= static_cast<int>(shots_.size())) return;
        const Shot& s = shots_[static_cast<size_t>(i)];
        const float tau = (t_ - s.start) * s.stageScale;  // seconds of the shot as written
        const StoryTitle* ti = nullptr;
        for (const StoryTitle& x : s.data->titles)
            if (tau >= x.t && tau <= x.to && (!ti || x.t >= ti->t)) ti = &x;
        if (!ti) return;
        UIContext& ui = g_.ui;
        const float in = smooth((tau - ti->t) / 0.35f);
        const float a = in * clamp01((ti->to - tau) / 0.35f) * clamp01((s.start + s.dur - t_) / 0.2f);
        if (a <= 0.003f) return;
        const std::string text = i18n::tr(ti->text);
        const std::string sub = ti->subtitle.empty() ? std::string() : i18n::tr(ti->subtitle);
        i18n::Verbatim asShown;  // both already translated
        const float maxW = img.w * ti->width;
        const float pop = 1.0f + 0.1f * (1.0f - in);
        const float titlePx = ui.s(ui.fitFont(text, FontFace::Title, ti->size, maxW / pop, 0.2f)) * pop;
        const float subPx = sub.empty() ? 0.0f : ui.s(ui.fitFont(sub, FontFace::Body, std::max(18.0f, ti->size * 0.4f), maxW, 0.35f));
        const float gap = sub.empty() ? 0.0f : ui.s(12);
        const float blockH = titlePx + gap + subPx * 1.2f;
        float y = img.y + img.h * ti->pos.y - blockH * 0.5f;
        y = std::clamp(y, img.y + ui.s(10), std::max(img.y + ui.s(10), img.bottom() - blockH - ui.s(10)));
        float cx = img.x + img.w * ti->pos.x;  // kept inside the picture, its whole width
        cx = std::clamp(cx, img.x + maxW * 0.5f, std::max(img.x + maxW * 0.5f, img.right() - maxW * 0.5f));
        TextStyle st;
        st.face = FontFace::Title;
        st.size = titlePx;
        st.color = Color::white().withAlphaF(a);
        st.align = Align::Center;
        st.shadow = true;
        st.shadowOffset = ui.s(3);
        st.shadowColor = Color{0, 0, 0, static_cast<uint8_t>(210.0f * a)};
        st.glow = ui.s(20);
        st.glowColor = ti->color.withAlphaF(0.6f * a);
        ui.text().draw(text, {cx, y}, st);
        if (sub.empty()) return;
        TextStyle ss;
        ss.face = FontFace::Body;
        ss.size = subPx;
        ss.color = Color::lerp(ti->color, Color::white(), 0.6f).withAlphaF(a * smooth((tau - ti->t - 0.25f) / 0.35f));
        ss.align = Align::Center;
        ss.shadow = true;
        ss.shadowColor = Color{0, 0, 0, static_cast<uint8_t>(200.0f * a)};
        ui.text().draw(sub, {cx, y + titlePx + gap}, ss);
    }

    /// 2.63, the trailer's last card: the game's name, its line under it, stars and embers.
    void drawTrailerCard(const Rect& img, float alpha) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float u = t_ - titleStart_;
        const Color c = story_.color;
        r.fillGradientV(img, Color::lerp(Color{6, 6, 14, 255}, c, 0.18f).withAlphaF(alpha), Color{3, 3, 8, 255}.withAlphaF(alpha));
        r.drawGlow(img.center(), img.h * 0.55f, c.withAlphaF(0.25f * alpha));
        drawParticles("stars", Color::lerp(c, Color::white(), 0.5f), img, t_, alpha, 991U);
        drawParticles("embers", story_.accent, img, t_, alpha * 0.7f, 4441U);
        // The name (drawGameTitle: « COSMIC » over « PLATFORMER »), as large as the width allows.
        float size = 104.0f;
        size = std::min(size, ui.fitFont("COSMIC", FontFace::Title, size, img.w * 0.8f, 0.2f));
        size = std::min(size, ui.fitFont("PLATFORMER", FontFace::Title, size * 0.62f, img.w * 0.8f, 0.2f) / 0.62f);
        size = std::min(size, img.h * 0.32f / std::max(0.01f, ui.s(1.0f)));
        const float appear = smooth(u / 0.5f);
        const Vec2 at{img.center().x, img.y + img.h * 0.44f};
        drawGameTitle(g_, at, size * (1.0f + 0.08f * (1.0f - appear)), alpha * appear);
        const std::string line = i18n::tr(story_.title);
        i18n::Verbatim asShown;
        const float px = ui.s(ui.fitFont(line, FontFace::Body, 30, img.w * 0.8f, 0.4f));
        TextStyle st;
        st.face = FontFace::Body;
        st.size = px;
        st.color = Color{232, 236, 246, 255}.withAlphaF(alpha * smooth((u - 0.5f) / 0.5f));
        st.align = Align::Center;
        st.shadow = true;
        ui.text().draw(line, {at.x, at.y + ui.s(size * 0.95f)}, st);
    }

    /// 2.63, the trailer's bottom band: in attract mode, that any input goes back to the menu
    /// (blinking softly); else « Espace · Ⓐ Passer », as a story.
    void drawTrailerHint(const Rect& band) {
        UIContext& ui = g_.ui;
        if (!attract_) {
            const float margin = std::round(std::max(ui.s(56), 16.0f));
            drawSkipHint({band.right() - margin, band.center().y});
            return;
        }
        const std::string hint = i18n::tr("Appuyez sur une touche pour revenir au menu");
        i18n::Verbatim asShown;
        const float px = ui.s(ui.fitFont(hint, FontFace::Body, 22, band.w * 0.9f, 0.5f));
        TextStyle st;
        st.face = FontFace::Body;
        st.size = px;
        st.color = Color{200, 208, 224, 255}.withAlphaF(0.55f + 0.35f * std::sin(sinceEnter_ * 2.4f));
        st.align = Align::Center;
        st.valign = VAlign::Middle;
        ui.text().draw(hint, band.center(), st);
    }

    /// The title card: the boss (its sprite roaring), « GIGA-BOSS · 8 PHASES », its name, its
    /// subtitle and its difficulty.
    void drawTitle(const Rect& img, float alpha) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float u = t_ - titleStart_;
        const Color c = story_.color, acc = story_.accent;
        r.fillGradientV(img, Color::lerp(Color{6, 6, 14, 255}, c, 0.16f).withAlphaF(alpha), Color{3, 3, 8, 255}.withAlphaF(alpha));
        const bool sprite = def_ != nullptr;
        Rect sbox{img.x + img.w * 0.05f, img.y + img.h * 0.08f, img.w * 0.42f, img.h * 0.84f};
        if (sprite) {
            r.drawGlow(sbox.center(), sbox.h * 0.62f, c.withAlphaF(0.32f * alpha));
            r.drawGlow(sbox.center(), sbox.h * 0.3f, acc.withAlphaF(0.22f * alpha));
        }
        drawParticles("stars", Color::lerp(c, Color::white(), 0.5f), img, t_, alpha, 991U);
        drawParticles("embers", c, img, t_, alpha * 0.6f, 4441U);
        if (sprite) {
            // Its roar once, then its idle clip.
            const std::string clip = u < std::max(0.6f, roarLen_) ? story_.clip : std::string("idle");
            drawCreature(g_, *def_, sbox, clip == "idle" ? u - roarLen_ : u, Color::white().withAlphaF(alpha * smooth(u / 0.4f)), clip);
        }
        // The words, in a column at the right of the boss (centred when there is no sprite).
        const float tx = sprite ? sbox.right() + img.w * 0.04f : img.x + img.w * 0.12f;
        const float tw = (sprite ? img.right() - img.w * 0.05f : img.right() - img.w * 0.12f) - tx;
        const int phases = def_ ? static_cast<int>(def_->phases.size()) : story::kGigaPhases;
        const std::string label = "GIGA-BOSS · " + std::to_string(phases) + " PHASES";
        std::string name = def_ ? def_->name : story_.bossId;
        if (def_ && !def_->bossTitle.empty()) name = def_->bossTitle.substr(0, def_->bossTitle.find(','));  // « PYRRHAX »
        const float labelPx = ui.s(ui.fitFont(label, FontFace::Bold, 26, tw, 0.6f));
        const float namePx = ui.s(ui.fitFont(name, FontFace::Title, 104, tw, 0.35f));
        const float subPx = ui.s(ui.fitFont(story_.title, FontFace::Body, 36, tw, 0.5f));
        const float starR = std::min(ui.s(17), tw / 10.0f * 0.42f);
        const float starStep = starR / 0.42f;
        const float gap1 = ui.s(14), gap2 = ui.s(8), gap3 = ui.s(30);
        const float blockH = labelPx + gap1 + namePx + gap2 + subPx + gap3 + starR * 2.0f;
        float y = img.center().y - blockH * 0.5f;
        auto style = [&](FontFace face, float px, Color col, float a) {
            TextStyle st;
            st.face = face;
            st.size = px;
            st.color = col.withAlphaF(a * alpha);
            st.align = sprite ? Align::Left : Align::Center;
            st.shadow = true;
            st.shadowColor = Color{0, 0, 0, static_cast<uint8_t>(170.0f * a * alpha)};
            return st;
        };
        const float ax = sprite ? tx : tx + tw * 0.5f;
        const float la = smooth(u / 0.4f);
        ui.text().draw(label, {ax, y}, style(FontFace::Bold, labelPx, acc, la));
        y += labelPx + gap1;
        const float na = smooth((u - 0.2f) / 0.5f);
        TextStyle ns = style(FontFace::Title, namePx, c, na);
        ns.glow = ui.s(16);
        ns.glowColor = c.withAlphaF(0.45f * na * alpha);
        ui.text().draw(name, {ax, y + (1.0f - na) * ui.s(18)}, ns);
        y += namePx + gap2;
        const float sa = smooth((u - 0.55f) / 0.5f);
        ui.text().draw(story_.title, {ax, y}, style(FontFace::Body, subPx, Color{232, 236, 246, 255}, sa));
        y += subPx + gap3;
        // Its difficulty (10 stars, from its definition): the stars light up one by one.
        const int stars = def_ ? bossDifficulty(*def_).stars : 10;
        const float rowW = starStep * 10.0f;
        const float sx = sprite ? tx : tx + (tw - rowW) * 0.5f;
        const Color gold{255, 217, 138, 255};
        for (int k = 0; k < 10; ++k) {
            const float on = smooth((u - 0.9f - 0.07f * static_cast<float>(k)) / 0.2f);
            const bool lit = k < stars && on > 0.0f;
            Color col = lit ? Color::lerp(Color{90, 104, 130, 255}, gold, on) : Color{90, 104, 130, 255};
            drawRatingStar(r, {sx + starStep * (static_cast<float>(k) + 0.5f), y + starR}, starR * (lit ? 0.85f + 0.15f * on : 0.85f),
                           col.withAlphaF(alpha * std::max(sa, 0.0f)), lit);
        }
    }

    /// The line of shot `i`, typed in, then whole until the shot ends, as `lay` placed it.
    void drawNarration(int i, const StoryNarrationLayout& lay, const Rect& img) {
        if (i < 0 || i >= static_cast<int>(shots_.size()) || lay.lines.empty()) return;
        UIContext& ui = g_.ui;
        TextRenderer& tr = ui.text();
        const Shot& s = shots_[static_cast<size_t>(i)];
        const float local = t_ - s.start;
        const float typed = clamp01((local - 0.2f) / story::kTypeTime);
        const float fadeOut = clamp01((s.start + s.dur - t_) / 0.3f);
        if (typed <= 0.0f || fadeOut <= 0.0f) return;
        i18n::Verbatim asShown;  // the line is already translated (its prefix is drawn as it is)
        if (lay.top < lay.area.y)  // a very small window: the block rises over the painting, on a dark veil
            ui.r().fillRect({0, lay.top - ui.s(6), ui.screenW(), img.bottom() - lay.top + ui.s(6)}, Color{0, 0, 0, 170});
        size_t total = 0;
        for (const auto& l : lay.lines) total += story::characters(l);
        size_t shown = static_cast<size_t>(std::ceil(typed * static_cast<float>(total)));
        TextStyle st;
        st.face = FontFace::Body;
        st.size = lay.px;
        st.color = Color{238, 240, 248, 255}.withAlphaF(fadeOut);
        st.shadow = true;
        st.shadowColor = Color{0, 0, 0, static_cast<uint8_t>(200.0f * fadeOut)};
        for (size_t k = 0; k < lay.lines.size() && shown > 0; ++k) {
            const size_t n = story::characters(lay.lines[k]);
            const float lw = tr.measure(lay.lines[k], FontFace::Body, lay.px).x;
            const float x = lay.area.x + (lay.area.w - lw) * 0.5f;  // centred; typing never moves the line
            tr.draw(prefix(lay.lines[k], std::min(n, shown)), {x, lay.top + static_cast<float>(k) * lay.lineH + (lay.lineH - lay.px) * 0.5f}, st);
            shown -= std::min(n, shown);
        }
    }

    /// The story's progress: one segment per shot, one for the title card.
    void drawProgress(const Rect& bar) {
        Renderer& r = g_.ui.r();
        const int n = static_cast<int>(shots_.size()) + (film_ ? 0 : 1);  // a film has no title card
        if (n <= 0) return;
        const float gap = std::max(3.0f, g_.ui.s(6));
        const float w = (bar.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (int k = 0; k < n; ++k) {
            const float start = k < static_cast<int>(shots_.size()) ? shots_[static_cast<size_t>(k)].start : titleStart_;
            const float dur = k < static_cast<int>(shots_.size()) ? shots_[static_cast<size_t>(k)].dur : story::kTitleCard;
            const Rect seg{bar.x + static_cast<float>(k) * (w + gap), bar.y, w, bar.h};
            r.fillRoundedRect(seg, bar.h * 0.5f, Color{255, 255, 255, 40});
            const float f = clamp01((t_ - start) / dur);
            if (f > 0.0f) r.fillRoundedRect({seg.x, seg.y, std::max(bar.h, seg.w * f), seg.h}, bar.h * 0.5f, story_.color.withAlpha(230));
        }
    }

    /// « Espace · Ⓐ  Passer   Échap · Ⓑ  Quitter », its right edge at `rightMid.x`, centred on
    /// `rightMid.y` (the room of « Quitter » is kept: a click on it leaves the story).
    void drawSkipHint(Vec2 rightMid) {
        UIContext& ui = g_.ui;
        const SkipHintMetrics m = skipHintMetrics(ui);
        float x = rightMid.x - m.total;
        auto group = [&](SDL_Scancode key, SDL_GamepadButton pad, float keyW, const char* label, Color c) {
            Glyphs::drawBinding(ui, key, PadBinding{}, false, {x, rightMid.y}, m.h, true);
            x += keyW + m.space;
            TextStyle st;
            st.face = FontFace::Bold;
            st.size = m.fs;
            st.color = Color{200, 208, 224, 255};
            st.valign = VAlign::Middle;
            ui.text().draw("·", {x, rightMid.y}, st);
            x += m.dotW + m.space;
            Glyphs::drawPadButton(ui, PadBinding::button(pad), {x, rightMid.y}, m.h);
            x += m.h + m.space * 1.4f;
            st.face = FontFace::Body;
            st.color = c;
            st.shadow = true;
            ui.text().draw(label, {x, rightMid.y}, st);
        };
        group(SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_SOUTH, m.keyW, "Passer", Color{226, 232, 244, 255});
        x += m.txtW + m.gap;
        quitRect_ = {x - m.space, rightMid.y - m.h * 0.7f, m.quitW + 2.0f * m.space, m.h * 1.4f};
        const bool hover = quitRect_.contains(g_.app.input().mousePos());
        if (hover) ui.r().fillRoundedRect(quitRect_, m.h * 0.3f, Color{255, 255, 255, 28});
        group(SDL_SCANCODE_ESCAPE, SDL_GAMEPAD_BUTTON_EAST, m.quitKeyW, "Quitter", hover ? Color{255, 255, 255, 255} : Color{200, 208, 224, 255});
    }

    std::shared_ptr<const BossStory> keep_;  // 2.68: a film's story, owned by its scene
    GameServices& g_;
    const BossStory& story_;
    const EnemyDefinition* def_ = nullptr;
    int startAt_ = -1;
    float atSeconds_ = -1.0f;
    std::vector<Shot> shots_;
    float titleStart_ = 0.0f, total_ = 0.0f;
    float t_ = 0.0f;            // time in the story
    float sinceEnter_ = 0.0f;   // time since the story was opened
    bool closing_ = false;
    float closeT_ = 0.0f;
    bool done_ = false;
    bool roared_ = false;
    float roarLen_ = 0.0f;
    int sfxShot_ = -1;          // 2.63: the shot whose sounds are being played,
    float sfxTau_ = 0.0f;       // up to this second of its scene
    float duckUntil_ = 0.0f;    // a big sound: the music a little lower until then
    float baseDuck_ = 0.3f;     // the music under a story (the trailer's own: full)
    float duck_ = -1.0f;
    bool trailer_ = false;      // 2.63: the game's trailer (its titles, its last card)
    bool attract_ = false;      // 2.63: the main menu's attract mode (loops, any input leaves)
    bool quickClose_ = false;   // left by an input in attract mode
    bool film_ = false;         // 2.68: a mini-film (its titles, no title card, its music, from black)
    bool locked_ = false;       // 2.68: a film's first viewing: nothing skips it
    bool ended_ = false;        // 2.68: played to its end
    std::function<void()> onEnd_;
    std::string prevMusic_;     // 2.68: the music before the film (played again after it)
    static constexpr float kFilmFade = 0.9f;  // 2.68: a film's end fades slowly to the screen below
    int passes_ = 0;
    uint64_t activity_ = 0;
    static constexpr float kQuickFade = 0.2f;
    Rect quitRect_;             // 2.63: « Échap · Ⓑ Quitter », as drawn last (a click there leaves)
    TexturePtr target_;
    TexturePtr shotTarget_;  // a scene cross-fading in
    TexturePtr drawTarget_;  // where the story is drawn this frame (nullptr: the window)
};

}  // namespace

Rect storySpriteFrame(const AnimationSet& set, const std::string& clip, float t) {
    return storySpriteFrame(set, clip.empty() ? nullptr : set.clip(clip), t);
}

Rect storySpriteFrame(const AnimationSet& set, const AnimationClip* clip, float t) {
    const AnimationClip* c = clip;
    if (!c) c = set.clip("idle");
    if (!c)  // the sheet's first clip (its top-left frame)
        for (const auto& [name, cl] : set.clips)
            if (!c || cl.row < c->row || (cl.row == c->row && cl.start < c->start)) c = &cl;
    t = std::max(0.0f, t);
    for (int hop = 0; c && hop < 8; ++hop) {
        const float total = c->totalDuration();
        if (c->frameCount() == 0 || total <= 0.0f) return {};
        if (c->loop) {
            t = std::fmod(t, total);
        } else if (t >= total) {
            if (const AnimationClip* n = c->next.empty() ? nullptr : set.clip(c->next)) {
                t -= total;
                c = n;
                continue;
            }
            return set.frameRect(*c, c->frameCount() - 1);
        }
        float acc = 0.0f;
        for (int f = 0; f < c->frameCount(); ++f) {
            acc += c->durations[static_cast<size_t>(f)];
            if (t < acc) return set.frameRect(*c, f);
        }
        return set.frameRect(*c, c->frameCount() - 1);
    }
    return c ? set.frameRect(*c, c->frameCount() - 1) : Rect{};
}

StoryNarrationLayout layoutStoryNarration(UIContext& ui, const std::string& shown, bool skipHint) {
    StoryNarrationLayout lay;
    const float W = ui.screenW(), H = ui.screenH();
    const float band = std::round(H * 0.13f);
    const float margin = std::round(std::max(ui.s(56), 16.0f));
    const float bandTop = H - band;
    lay.bar = {margin, bandTop + std::max(6.0f, ui.s(14)), W - 2.0f * margin, std::max(2.0f, ui.s(4))};
    const float below = lay.bar.bottom();
    const float hintW = skipHint ? skipHintMetrics(ui).total : 0.0f;
    lay.hint = {W - margin - hintW, below, hintW, H - below};
    const float room = skipHint ? lay.hint.x - std::max(16.0f, ui.s(36)) : W - margin;  // without a hint: the band's whole width
    lay.area = {margin, below + std::max(4.0f, ui.s(8)), room - margin, H - below - std::max(8.0f, ui.s(16))};
    if (shown.empty() || lay.area.w <= 0.0f) return lay;
    i18n::Verbatim asShown;  // `shown` is the line as displayed (already translated)
    TextRenderer& tr = ui.text();
    const std::vector<std::string> words = narrationWords(shown);
    // The largest size at which the line fits the room in two lines at most.
    const float maxPx = ui.s(30), minPx = std::max(12.0f, ui.s(17)), step = std::max(1.0f, ui.s(1));
    bool fits = false;
    for (float px = maxPx; px >= minPx - 0.01f && !fits; px -= step) {
        lay.px = px;
        lay.lines = wrapWords(tr, words, px, lay.area.w);
        fits = lay.lines.size() <= kStoryNarrationLines && static_cast<float>(lay.lines.size()) * px * 1.32f <= lay.area.h;
        for (const auto& l : lay.lines) fits = fits && tr.measure(l, FontFace::Body, px).x <= lay.area.w;
    }
    if (!fits) {
        lay.px = minPx;
        lay.lines = wrapWords(tr, words, minPx, lay.area.w);
        for (auto& l : lay.lines)
            if (tr.measure(l, FontFace::Body, minPx).x > lay.area.w) l = ui.elide(l, FontFace::Body, minPx / ui.s(1.0f), lay.area.w);
    }
    lay.lineH = lay.px * 1.32f;
    const float blockH = static_cast<float>(lay.lines.size()) * lay.lineH;
    lay.top = blockH <= lay.area.h ? lay.area.y + (lay.area.h - blockH) * 0.5f : lay.area.bottom() - blockH;
    return lay;
}

std::unique_ptr<Scene> makeTrailer(GameServices& g, bool attract, float atSeconds) {
    if (!g.trailer || g.trailer->shots.empty()) return nullptr;
    return std::make_unique<BossStoryScene>(g, *g.trailer, -1, std::isfinite(atSeconds) ? atSeconds : -1.0f, attract);
}

std::unique_ptr<Scene> makeFilm(GameServices& g, std::shared_ptr<const BossStory> film, bool locked, std::function<void()> onEnd, float atSeconds) {
    if (!film || film->shots.empty()) return nullptr;
    return std::make_unique<BossStoryScene>(g, std::move(film), locked, std::move(onEnd), std::isfinite(atSeconds) ? atSeconds : -1.0f);
}

bool filmLocked(const Scene* s) {
    const auto* t = dynamic_cast<const BossStoryScene*>(s);
    return t && t->locked();
}

int storyShot(const Scene* s) {
    const auto* t = dynamic_cast<const BossStoryScene*>(s);
    return t ? t->shotIndex() : -1;
}

int trailerPasses(const Scene* s) {
    const auto* t = dynamic_cast<const BossStoryScene*>(s);
    return t && t->attract() ? t->passes() : -1;
}

bool attractAllowed(GameServices& g) {
    const InputManager& in = g.app.input();
    if (!g.settings.gameplay.attractTrailer || !g.trailer) return false;
    if (in.textInputActive()) return false;  // never while something is being typed
    if (tutorial::due(g.progression)) return false;  // nor before the tutorial is done
    if (const NewsCenter::Item* it = g.updates.offer()) {  // nor while an update downloads
        const UpdateInstaller::State st = g.updates.state(it->target);
        if (st == UpdateInstaller::State::Downloading || st == UpdateInstaller::State::Checking) return false;
    }
    return true;
}

std::unique_ptr<Scene> makeBossStory(GameServices& g, const std::string& bossId, int startAt, float atSeconds) {
    const BossStory* s = g.stories.find(bossId);
    if (!s || s->shots.empty()) return nullptr;
    return std::make_unique<BossStoryScene>(g, *s, std::max(-1, startAt), std::isfinite(atSeconds) ? atSeconds : -1.0f);
}

}  // namespace cosmic
