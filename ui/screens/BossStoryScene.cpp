#include "ui/screens/BossStoryScene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/enemies/BossDifficulty.h"
#include "game/enemies/EnemyDefinition.h"
#include "ui/online/RatingWidgets.h"
#include "ui/screens/CreatureArt.h"
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
};

/// The parts of « Espace · Ⓐ  Passer », measured.
SkipHintMetrics skipHintMetrics(UIContext& ui) {
    SkipHintMetrics m;
    m.h = std::max(18.0f, ui.s(30));
    m.fs = m.h * 0.6f;
    m.space = m.h * 0.28f;
    m.keyW = Glyphs::drawBinding(ui, SDL_SCANCODE_SPACE, PadBinding{}, false, {0, 0}, m.h, false);
    m.dotW = ui.text().measure("·", FontFace::Bold, m.fs).x;
    m.txtW = ui.text().measure("Passer", FontFace::Body, m.fs).x;
    m.total = m.keyW + m.space + m.dotW + m.space + m.h + m.space * 1.4f + m.txtW;
    return m;
}

class BossStoryScene : public Scene {
public:
    BossStoryScene(GameServices& g, const BossStory& story, int startAt) : g_(g), story_(story), startAt_(startAt) {}

    void onEnter() override {
        def_ = g_.content.enemy(story_.bossId);
        // The timeline, from the lines as they are shown (a shot lasts long enough to read its
        // line in the language in use).
        float t = 0.0f;
        for (const StoryShot& s : story_.shots) {
            Shot shot;
            shot.data = &s;
            shot.shown = i18n::tr(s.text);
            shot.words = narrationWords(shot.shown);
            shot.start = t;
            shot.dur = story::shotDuration(shot.shown);
            shot.tex = s.image.empty() ? nullptr : g_.app.resources().texture(s.image, false);
            t += shot.dur;
            shots_.push_back(std::move(shot));
        }
        titleStart_ = t;
        total_ = t + story::kTitleCard;
        if (startAt_ >= 0) {
            // Automated screenshots: that shot with its line typed, or the title card revealed.
            t_ = startAt_ < static_cast<int>(shots_.size()) ? shots_[static_cast<size_t>(startAt_)].start + 1.4f : titleStart_ + 1.8f;
            sinceEnter_ = 10.0f;
        }
        roarLen_ = def_ ? creatureClipDuration(g_, *def_, story_.clip) : 0.0f;
        g_.app.audio().setMusicDuck(0.3f);
    }

    void onExit() override { g_.app.audio().setMusicDuck(1.0f); }

    bool isOverlay() const override { return true; }
    bool holdsNotifications() const override { return true; }

    void update(float dt) override {
        sinceEnter_ += dt;
        if (closing_) {
            closeT_ += dt;
            if (closeT_ >= story::kFade) finish();
            return;
        }
        t_ += dt;
        if (!roared_ && t_ >= titleStart_ + 0.2f) {
            roared_ = true;
            if (def_ && !def_->alertSound.empty()) g_.app.resources().playSound(def_->alertSound, 0.5f);
        }
        if (t_ >= total_) {
            closing_ = true;
            return;
        }
        // Skipped (never by a press of the fight's first instant: the player may be jumping).
        InputManager& in = g_.app.input();
        bool skip = in.menuPressed(Action::MenuConfirm) || in.menuPressed(Action::MenuBack) || in.keyPressed(SDL_SCANCODE_SPACE) ||
                    in.mousePressed(SDL_BUTTON_LEFT);
        if (skip && sinceEnter_ > 0.6f) {
            closing_ = true;
            g_.uiSound("ui_confirm", 0.35f);
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Renderer& r = ui.r();
        const float in = startAt_ >= 0 ? 1.0f : clamp01(sinceEnter_ / story::kFade);
        const float out = closing_ ? 1.0f - clamp01(closeT_ / story::kFade) : 1.0f;
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
        drawStory();
        if (onTarget) {
            r.setTarget(nullptr);
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
    };

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
        const bool title = t_ >= titleStart_;
        if (!title) {
            const int cur = currentShot();
            const float a = cur > 0 ? smooth((t_ - shots_[static_cast<size_t>(cur)].start) / story::kCrossFade) : 1.0f;
            if (cur > 0 && a < 1.0f) drawShot(cur - 1, img, 1.0f);
            drawShot(cur, img, a);
        } else {
            const float a = smooth((t_ - titleStart_) / story::kCrossFade);
            if (a < 1.0f && !shots_.empty()) drawShot(static_cast<int>(shots_.size()) - 1, img, 1.0f);
            drawTitle(img, a);
        }
        // Soft edges against the bands.
        const float soft = std::round(img.h * 0.08f);
        r.fillGradientV({img.x, img.y, img.w, soft}, Color{0, 0, 0, 170}, Color{0, 0, 0, 0});
        r.fillGradientV({img.x, img.bottom() - soft, img.w, soft}, Color{0, 0, 0, 0}, Color{0, 0, 0, 170});
        r.popClip();
        r.fillRect({img.x, img.y - std::max(1.0f, ui.s(1.5f)), img.w, std::max(1.0f, ui.s(1.5f))}, story_.color.withAlpha(60));
        r.fillRect({img.x, img.bottom(), img.w, std::max(1.0f, ui.s(1.5f))}, story_.color.withAlpha(60));

        // The bottom band: progress bar on its top, the line of narration, « Passer » at the right
        // (the line keeps to the room left of the measured hint: layoutStoryNarration).
        const int cur = currentShot();
        const StoryNarrationLayout lay = layoutStoryNarration(ui, title ? std::string() : shots_[static_cast<size_t>(cur)].shown);
        drawProgress(lay.bar);
        if (!title) drawNarration(cur, lay, img);
        drawSkipHint({lay.hint.right(), lay.hint.center().y});
    }

    /// The painting of shot `i` covering `img`, zooming slowly towards its focus, its particles over it.
    void drawShot(int i, const Rect& img, float alpha) {
        if (i < 0 || i >= static_cast<int>(shots_.size()) || alpha <= 0.0f) return;
        Renderer& r = g_.ui.r();
        const Shot& s = shots_[static_cast<size_t>(i)];
        const float local = t_ - s.start;
        const float p = easeInOut(local / (s.dur + story::kCrossFade));
        if (s.tex && s.tex->width() > 0 && s.tex->height() > 0) {
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

    /// A few live particles over a painting (display only).
    void drawParticles(const std::string& kind, Color c, const Rect& area, float t, float alpha, uint32_t seed) {
        if (kind == "none" || alpha <= 0.0f) return;
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float u = std::max(0.5f, ui.s(1.0f));
        const int n = kind == "stars" ? 46 : kind == "dust" ? 34 : kind == "sparks" ? 26 : 32;
        for (int k = 0; k < n; ++k) {
            const uint32_t h = seed * 131U + static_cast<uint32_t>(k) * 2654435761U;
            const float a = hash01(h), b = hash01(h + 1), c2 = hash01(h + 2), d = hash01(h + 3), e = hash01(h + 4);
            Vec2 p;
            float size = u * (1.5f + 2.5f * d), glow = 0.0f, op = 0.0f;
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
            if (glow > 0.0f) r.drawGlow(p, glow, c.withAlphaF(0.45f * op));
            r.fillCircle(p, size, Color::lerp(c, Color::white(), 0.55f).withAlphaF(op), BlendMode::Additive);
        }
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
        const int n = static_cast<int>(shots_.size()) + 1;
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

    /// « Espace · Ⓐ  Passer », its right edge at `rightMid.x`, centred on `rightMid.y`.
    void drawSkipHint(Vec2 rightMid) {
        UIContext& ui = g_.ui;
        const SkipHintMetrics m = skipHintMetrics(ui);
        float x = rightMid.x - m.total;
        Glyphs::drawBinding(ui, SDL_SCANCODE_SPACE, PadBinding{}, false, {x, rightMid.y}, m.h, true);
        x += m.keyW + m.space;
        TextStyle st;
        st.face = FontFace::Bold;
        st.size = m.fs;
        st.color = Color{200, 208, 224, 255};
        st.valign = VAlign::Middle;
        ui.text().draw("·", {x, rightMid.y}, st);
        x += m.dotW + m.space;
        Glyphs::drawPadButton(ui, PadBinding::button(SDL_GAMEPAD_BUTTON_SOUTH), {x, rightMid.y}, m.h);
        x += m.h + m.space * 1.4f;
        st.face = FontFace::Body;
        st.color = Color{226, 232, 244, 255};
        st.shadow = true;
        ui.text().draw("Passer", {x, rightMid.y}, st);
    }

    GameServices& g_;
    const BossStory& story_;
    const EnemyDefinition* def_ = nullptr;
    int startAt_ = -1;
    std::vector<Shot> shots_;
    float titleStart_ = 0.0f, total_ = 0.0f;
    float t_ = 0.0f;            // time in the story
    float sinceEnter_ = 0.0f;   // time since the story was opened
    bool closing_ = false;
    float closeT_ = 0.0f;
    bool done_ = false;
    bool roared_ = false;
    float roarLen_ = 0.0f;
    TexturePtr target_;
};

}  // namespace

StoryNarrationLayout layoutStoryNarration(UIContext& ui, const std::string& shown) {
    StoryNarrationLayout lay;
    const float W = ui.screenW(), H = ui.screenH();
    const float band = std::round(H * 0.13f);
    const float margin = std::round(std::max(ui.s(56), 16.0f));
    const float bandTop = H - band;
    lay.bar = {margin, bandTop + std::max(6.0f, ui.s(14)), W - 2.0f * margin, std::max(2.0f, ui.s(4))};
    const float below = lay.bar.bottom();
    const float hintW = skipHintMetrics(ui).total;
    lay.hint = {W - margin - hintW, below, hintW, H - below};
    lay.area = {margin, below + std::max(4.0f, ui.s(8)), lay.hint.x - std::max(16.0f, ui.s(36)) - margin, H - below - std::max(8.0f, ui.s(16))};
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

std::unique_ptr<Scene> makeBossStory(GameServices& g, const std::string& bossId, int startAt) {
    const BossStory* s = g.stories.find(bossId);
    if (!s || s->shots.empty()) return nullptr;
    return std::make_unique<BossStoryScene>(g, *s, std::max(-1, startAt));
}

}  // namespace cosmic
