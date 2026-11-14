#include "ui/screens/GhostRun.h"

#include <algorithm>
#include <cmath>

#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/LevelProgress.h"
#include "game/player/Player.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

bool isMine(const GhostMeta& m) { return m.source == "pb" || m.source == "prev" || m.source == "last"; }

/// Mine are named by what they are (best time, last run...), the others by their player.
std::string displayName(const GhostMeta& m) { return m.label.empty() || isMine(m) ? m.sourceLabel() : m.label; }

}  // namespace

void GhostRun::begin(std::vector<GhostEntry> choices, const LevelData& level, const ContentDatabase& content,
                     const std::string& levelHash, const GhostSettings& settings, bool training) {
    settings_ = settings;
    training_ = training;
    race_.clear();
    anims_.clear();
    labels_.clear();
    refused_.clear();
    seenSplits_ = 0;
    recordFlash_ = 0.0f;
    checkpoints_.clear();
    for (const LevelObject& o : level.objects)
        if (o.type == "checkpoint") checkpoints_.push_back({o.rect.bottomCenter(), o.propStr("label")});
    if (choices.size() > static_cast<size_t>(std::clamp(settings.maxGhosts, 1, GhostRace::kMaxGhosts)))
        choices.resize(static_cast<size_t>(std::clamp(settings.maxGhosts, 1, GhostRace::kMaxGhosts)));
    if (choices.empty()) return;
    // Reading, checking and mapping the replays on the route never happen on the main thread.
    loading_ = std::async(std::launch::async, [choices = std::move(choices), level, &content, levelHash]() {
        Prepared p;
        p.route = buildProgressTrack(level, content);
        for (const GhostEntry& e : choices) {
            Loaded l;
            l.entry = e;
            std::string who = displayName(e.meta);
            std::string err;
            auto r = GhostStore::loadReplay(e.file, &err);
            if (!r) {
                l.refused = who + " : " + err;
            } else if (GhostCheck c = checkGhostReplay(*r, "", level.bounds); !c.ok) {
                l.refused = who + " : " + c.reason;
            } else {
                l.sameVersion = r->levelHash == levelHash;
                // Another version: still this level? (the uploader's own copy has another id;
                // the description written at download time names ours).
                if (!l.sameVersion && r->levelId != level.id && e.meta.levelId != level.id) {
                    l.refused = who + " : replay d'un autre niveau.";
                } else {
                    l.track.load(*r);
                    l.track.mapProgress(p.route);
                    if (l.entry.meta.timeMs <= 0) l.entry.meta.timeMs = r->durationMs;
                }
            }
            p.ghosts.push_back(std::move(l));
        }
        return p;
    });
}

Color GhostRun::colorFor(const GhostMeta& m, int sameKindBefore) const {
    int idx = isMine(m) ? settings_.colorMine : m.source == "world" ? settings_.colorWorld
            : m.source == "friend"                               ? settings_.colorFriend
                                                                 : settings_.colorOther;
    Color c = ghostPaletteColor(idx);
    // Two ghosts of the same kind: the second one lighter, the third one darker...
    if (sameKindBefore > 0) c = Color::lerp(c, sameKindBefore % 2 ? Color{255, 255, 255, 255} : Color{40, 40, 60, 255}, 0.35f);
    return c;
}

void GhostRun::collect() {
    if (!loading_.valid() || loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    Prepared p = loading_.get();
    race_.setRoute(p.route);
    int mine = 0, world = 0, friends = 0, other = 0;
    for (Loaded& l : p.ghosts) {
        if (!l.refused.empty()) {
            refused_.push_back(l.refused);
            continue;
        }
        const GhostMeta& m = l.entry.meta;
        int& kind = isMine(m) ? mine : m.source == "world" ? world : m.source == "friend" ? friends : other;
        Color c = colorFor(m, kind++);
        if (race_.add(m, std::move(l.track), l.sameVersion, c) < 0) break;
        std::string label = displayName(m);
        if (m.timeMs > 0) label += " · " + formatGhostTime(static_cast<float>(m.timeMs) / 1000.0f);
        labels_.push_back(std::move(label));
    }
    anims_.resize(static_cast<size_t>(race_.size()));
    if (hasTrainingFrom_ && !race_.empty()) {
        // The reference ghost's split at this checkpoint (exact), else its nearest moment.
        const GhostTrack& t = race_.racer(race_.reference()).track;
        int best = -1;
        float bd = 320.0f * 320.0f;
        for (size_t j = 0; j < t.splits().size(); ++j) {
            float d = (t.poseAt(t.splits()[j]).feet - trainingFrom_).lengthSq();
            if (d < bd) {
                bd = d;
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) race_.setOffset(t.splits()[static_cast<size_t>(best)], best);
        else race_.setOffset(t.nearestTime(trainingFrom_), 0);
    }
}

void GhostRun::setSettings(const GhostSettings& s) {
    settings_ = s;
    int mine = 0, world = 0, friends = 0, other = 0;
    for (int i = 0; i < race_.size(); ++i) {
        GhostRace::Racer& r = race_.racer(i);
        int& kind = isMine(r.meta) ? mine : r.meta.source == "world" ? world : r.meta.source == "friend" ? friends : other;
        r.color = colorFor(r.meta, kind++);
    }
}

int GhostRun::otherVersions() const {
    int n = 0;
    for (int i = 0; i < race_.size(); ++i) n += race_.racer(i).sameVersion ? 0 : 1;
    return n;
}

void GhostRun::update(const GameWorld& w, float dt) {
    collect();
    if (race_.empty()) return;
    const std::vector<float>& sp = w.timer().splits();
    while (seenSplits_ < sp.size()) {
        // Training: the checkpoint the run starts on is not a split of the comparison.
        bool startPoint = training_ && seenSplits_ == 0 && sp[0] < 0.75f;
        if (!startPoint) race_.onSplit(static_cast<int>(seenSplits_), sp[seenSplits_], w.player().feet());
        ++seenSplits_;
    }
    race_.update(w.timer().elapsed(), w.player().feet(), dt);
    if (race_.finished() && !race_.splits().empty() && race_.splits().back().aheadOfRecord) recordFlash_ += dt;
    for (int i = 0; i < race_.size(); ++i) {
        const GhostRace::Racer& g = race_.racer(i);
        ReplayFrame f;
        f.state = g.pose.state;
        f.flags = g.pose.flags;
        Animator& a = anims_[static_cast<size_t>(i)];
        if (!a.set()) continue;
        a.play(ghostClipFor(f, g.pose.vel));
        a.update(dt);
    }
}

void GhostRun::drawWorld(Renderer& r, const WorldView& view, const AnimationSetPtr& set) {
    if (!visible_ || race_.empty() || !set || !set->texture) return;
    r.setView(view);
    const float alpha = ghostOpacity(settings_.opacity);
    const float t = race_.ghostTime();
    // The reference ghost last: on top of the others.
    for (int k = 0; k < race_.size(); ++k) {
        int i = (k + race_.reference() + 1) % race_.size();
        const GhostRace::Racer& g = race_.racer(i);
        Animator& a = anims_[static_cast<size_t>(i)];
        if (!a.set()) {
            a.setSet(set);
            a.play("idle");
        }
        if (!g.pose.visible || static_cast<PlayerState>(g.pose.state) == PlayerState::Hidden) continue;
        // A ghost that has finished waits a moment at the exit, then fades out.
        float fade = std::clamp(1.0f - (t - g.track.duration() - 1.5f) / 0.6f, 0.0f, 1.0f);
        if (fade <= 0.0f) continue;
        float al = alpha * fade;
        bool flip = (g.pose.flags & ReplayFrame::FacingLeft) != 0;
        Rect src = a.sourceRect();
        Rect dst = a.destRect(g.pose.feet, flip);
        Color c = g.color;
        r.drawWorldGlow({g.pose.feet.x, g.pose.feet.y - 14.0f}, 30.0f, c.withAlpha(static_cast<uint8_t>(70 * al)));
        // A discreet luminous outline: the silhouette drawn around in additive colour.
        float o = 1.6f / std::max(0.25f, view.scale);
        const Vec2 offs[4] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}};
        for (Vec2 d : offs) {
            Rect od = dst;
            od.x += d.x;
            od.y += d.y;
            r.drawWorldTexture(*set->texture, &src, od, c.withAlpha(static_cast<uint8_t>(120 * al)), BlendMode::Additive, flip);
        }
        Color body = Color::lerp(Color{255, 255, 255, 255}, c, 0.55f);
        r.drawWorldTexture(*set->texture, &src, dst, body.withAlpha(static_cast<uint8_t>(255 * al)), BlendMode::Alpha, flip);
        if (g.pose.flags & ReplayFrame::Shield)
            r.drawWorldCircle({g.pose.feet.x, g.pose.feet.y - 14.0f}, 24.0f, c.withAlpha(static_cast<uint8_t>(200 * al)), 2.0f);
    }
    r.setView(WorldView{});
}

void GhostRun::drawHud(UIContext& ui, const WorldView& view, const Rect& area, float k, bool boxShown, const Rect* keepOut,
                       int keepOutCount) {
    hudRect_ = {};
    if (!visible_) return;
    if (!boxShown || (!settings_.showDelta && !settings_.showPanel) || race_.empty()) {
        if (boxShown && !race_.empty()) {}  // nothing in the box: only the names
        else if (boxShown) drawStatus(ui, area, k);
        drawNames(ui, view, keepOut, keepOutCount);
        return;
    }
    auto S = [&](float v) { return ui.s(v) * k; };
    auto F = [&](float v) { return v * k; };
    const UITheme& th = ui.theme();
    const GhostRace::Racer& ref = race_.racer(race_.reference());
    // The comparison box fills its HUD place from the top (HUD editor: « Fantômes »).
    const float right = area.right(), width = area.w, left = area.x;
    const float y = area.y;
    float pad = S(10);
    float inner = width - 2 * pad;
    std::string refName = ref.meta.sourceLabel();
    if (!ref.meta.label.empty() && ref.meta.label != "Moi") refName += " · " + ref.meta.label;
    std::string who = race_.size() > 1 ? std::to_string(race_.size()) + " FANTÔMES · RÉF. " + refName : "FANTÔME · " + refName;
    if (!ref.sameVersion) who += " (autre version)";
    // Rows of the panel (passed checkpoints, then the ghost's next ones, then the exit).
    struct Row {
        std::string name;
        float seg = -1, segDelta = 0, cum = -1, cumDelta = 0;
        bool done = false, hasSegDelta = false, hasCumDelta = false, gold = false, fresh = false;
    };
    Row rows[10];
    int nRows = 0;
    const auto& res = race_.splits();
    const auto& gs = ref.track.splits();
    int lastGhost = -1;
    size_t firstShown = res.size() > 4 ? res.size() - 4 : 0;  // the last passed ones (at most 4)
    for (size_t i = firstShown; i < res.size() && nRows < 10; ++i) {
        const GhostRace::SplitResult& sr = res[i];
        Row& row = rows[nRows++];
        if (sr.index < 0) row.name = "Arrivée";
        else if (sr.comparable) row.name = checkpointName(ref.track.poseAt(sr.ghost).feet, sr.index);
        else row.name = "Point " + std::to_string(sr.index + 1);
        row.done = true;
        row.seg = sr.segment;
        row.cum = sr.player - race_.offset();  // training: from the segment's start
        row.hasCumDelta = sr.comparable;
        row.cumDelta = sr.delta;
        row.hasSegDelta = sr.ghostSegment >= 0.0f;
        row.segDelta = sr.segment - sr.ghostSegment;
        row.gold = sr.goldSegment;
        row.fresh = i + 1 == res.size() && race_.sinceSplit() < 3.0f;
        if (sr.ghostIndex >= 0) lastGhost = sr.ghostIndex;
    }
    if (!race_.finished()) {
        // Next targets: the reference ghost's splits still ahead, then the exit.
        int shownNext = 0;
        const float off = race_.offset();
        int first = lastGhost + 1;
        while (first < static_cast<int>(gs.size()) && gs[static_cast<size_t>(first)] <= off + 0.01f) ++first;  // training: before the start
        for (int j = first; j < static_cast<int>(gs.size()) && shownNext < 3 && nRows < 9; ++j, ++shownNext) {
            Row& row = rows[nRows++];
            row.name = checkpointName(ref.track.poseAt(gs[static_cast<size_t>(j)]).feet, j);
            row.seg = gs[static_cast<size_t>(j)] - std::max(off, j > 0 ? gs[static_cast<size_t>(j - 1)] : 0.0f);
            row.cum = gs[static_cast<size_t>(j)] - off;
        }
        Row& exitRow = rows[nRows++];
        exitRow.name = "Arrivée";
        exitRow.cum = ref.track.duration() - off;
        exitRow.seg = ref.track.duration() - std::max(off, gs.empty() ? 0.0f : gs.back());
    }
    // Sizes: what does not fit in the place goes (oldest rows, then the other ghosts).
    // Never below 11.5 pixels (small windows): below, the font loses its spaces.
    const float minFs = 11.5f / ui.s(1.0f);
    const float bigFs = F(32), smallFs = std::max(F(16), minFs), rowFs = std::max(F(15), minFs), headFs = std::max(F(13), minFs);
    const float lineSmall = S(23), lineRow = S(22);
    int others = race_.size() > 1 ? std::min(race_.size(), 4) : 0;
    auto heightFor = [&](int rowsShown, int othersShown) {
        float h = pad + lineSmall;
        if (settings_.showDelta) h += S(42) + lineSmall;
        h += lineSmall * static_cast<float>(othersShown);
        if (settings_.showPanel) h += S(8) + lineRow * static_cast<float>(rowsShown + 1) + lineSmall;
        if (recordFlash_ > 0.0f) h += S(38);
        return h + pad;
    };
    while (heightFor(nRows, others) > area.h && settings_.showPanel && nRows > 1) {
        for (int i = 0; i + 1 < nRows; ++i) rows[i] = rows[i + 1];
        --nRows;
    }
    while (heightFor(settings_.showPanel ? nRows : 0, others) > area.h && others > 0) --others;
    Rect box{left, y, width, std::min(area.h, heightFor(settings_.showPanel ? nRows : 0, others))};
    hudRect_ = box;
    ui.r().fillRoundedRect(box, S(8), Color{8, 12, 26, 170});
    ui.r().drawRoundedRect(box, S(8), ref.color.withAlpha(90), S(1.5f));
    ui.r().pushClip(box);
    float x = left + pad, cy = y + pad;
    float hf = ui.fitFont(who, FontFace::Bold, std::max(F(14), minFs), inner, 0.85f);
    ui.label(ui.elide(who, FontFace::Bold, hf, inner), {x, cy}, hf, ref.color, Align::Left, FontFace::Bold);
    cy += lineSmall;
    if (settings_.showDelta) {
        bool has = ref.hasLiveDelta;
        float d = ref.liveDelta;
        Color dc = !has ? th.textDim : d > 0.0005f ? th.danger : d < -0.0005f ? th.success : th.text;
        std::string gap = has ? formatGhostDelta(d) : "…";
        ui.label(gap, {right - pad, cy}, bigFs, dc, Align::Right, FontFace::Title);
        const char* word = !has ? "" : d > 0.0005f ? "retard" : d < -0.0005f ? "avance" : "égalité";
        ui.label(word, {x, cy + S(12)}, smallFs, dc, Align::Left, FontFace::Bold);
        cy += S(42);
        std::string info;
        if (ref.timeHere >= 0.0f) info = "Fantôme ici " + formatGhostTime(std::max(0.0f, ref.timeHere - race_.offset()));
        float fin = race_.estimatedFinish();
        if (!race_.finished() && fin > 0.0f && !training_) info += (info.empty() ? "" : "  ·  ") + std::string("Fin estimée ") + formatGhostTime(fin);
        if (race_.finished()) info = "Arrivée " + formatGhostTime(race_.finalTime()) + "  ·  fantôme " + formatGhostTime(ref.track.duration());
        if (!info.empty()) {
            float f = ui.fitFont(info, FontFace::Body, smallFs, inner, 0.75f);
            ui.label(ui.elide(info, FontFace::Body, f, inner), {right - pad, cy}, f, th.textDim, Align::Right);
        }
        cy += lineSmall;
    }
    for (int kk = 0; kk < others; ++kk) {
        // Every ghost's gap (the reference one first).
        int i = (race_.reference() + kk) % race_.size();
        const GhostRace::Racer& g = race_.racer(i);
        ui.r().fillCircle({x + S(6), cy + S(10)}, S(5), g.color);
        std::string d = g.hasLiveDelta ? formatGhostDelta(g.liveDelta) : "…";
        float dw = ui.text().measure(d, FontFace::Bold, ui.s(smallFs)).x;
        ui.label(ui.elide(displayName(g.meta), FontFace::Body, smallFs, inner - dw - S(30)), {x + S(18), cy}, smallFs, th.text);
        Color dc = !g.hasLiveDelta ? th.textDim : g.liveDelta > 0.0005f ? th.danger : g.liveDelta < -0.0005f ? th.success : th.text;
        ui.label(d, {right - pad, cy}, smallFs, dc, Align::Right, FontFace::Bold);
        cy += lineSmall;
    }
    if (settings_.showPanel) {
        cy += S(4);
        ui.r().fillRect({x, cy, inner, std::max(1.0f, S(1))}, ref.color.withAlpha(60));
        cy += S(4);
        // Columns, measured: name | duration | segment gap | time | gap.
        const float numW = std::max(ui.text().measure("+1:15,500", FontFace::Body, ui.s(rowFs)).x,
                                    ui.text().measure("Écart seg.", FontFace::Body, ui.s(headFs)).x) + S(10);
        float cCumD = x + inner, cCum = cCumD - numW, cSegD = cCum - numW, cSeg = cSegD - numW;
        float nameRoom = cSeg - numW - x - S(6);
        ui.label("Segment", {x, cy}, headFs, th.textDim);
        ui.label("Durée", {cSeg, cy}, headFs, th.textDim, Align::Right);
        ui.label("Écart seg.", {cSegD, cy}, headFs, th.textDim, Align::Right);
        ui.label("Temps", {cCum, cy}, headFs, th.textDim, Align::Right);
        ui.label("Écart", {cCumD, cy}, headFs, th.textDim, Align::Right);
        cy += lineRow;
        for (int i = 0; i < nRows; ++i) {
            const Row& row = rows[i];
            if (row.fresh) ui.r().fillRoundedRect({x - S(4), cy - S(2), inner + S(8), lineRow}, S(4), ref.color.withAlpha(40));
            Color base = row.done ? th.text : th.textDim;
            ui.label(ui.elide(row.name, FontFace::Body, rowFs, nameRoom), {x, cy}, rowFs, base);
            if (row.seg >= 0.0f) ui.label(formatGhostTime(row.seg), {cSeg, cy}, rowFs, row.gold ? th.warm : base, Align::Right);
            if (row.done && row.hasSegDelta) {
                Color c = row.gold ? th.warm : row.segDelta > 0.0005f ? th.danger : row.segDelta < -0.0005f ? th.success : th.text;
                ui.label(formatGhostDelta(row.segDelta, false), {cSegD, cy}, rowFs, c, Align::Right);
            }
            if (row.cum >= 0.0f) ui.label(formatGhostTime(row.cum), {cCum, cy}, rowFs, base, Align::Right);
            if (row.done && row.hasCumDelta) {
                Color c = row.cumDelta > 0.0005f ? th.danger : row.cumDelta < -0.0005f ? th.success : th.text;
                ui.label(formatGhostDelta(row.cumDelta, false), {cCumD, cy}, rowFs, c, Align::Right, FontFace::Bold);
            }
            cy += lineRow;
        }
        // The personal record and the sum of the best segments (the second one only if it fits).
        std::string rec, sobText;
        if (race_.personalTime() > 0.0f) rec = "Record perso " + formatGhostTime(race_.personalTime());
        float sob = 0.0f;
        bool sobOk = !race_.personalBestSegments().empty();
        for (float sg : race_.personalBestSegments()) {
            if (sg <= 0.0f) sobOk = false;
            sob += sg;
        }
        if (sobOk) sobText = "Meilleurs segments " + formatGhostTime(sob);
        std::string foot = rec;
        if (!sobText.empty()) {
            std::string both = rec.empty() ? sobText : rec + "  ·  " + sobText;
            foot = ui.text().measure(both, FontFace::Body, ui.s(headFs)).x <= inner ? both : (rec.empty() ? sobText : rec);
        }
        if (!foot.empty()) ui.label(ui.elide(foot, FontFace::Body, headFs, inner), {x, cy + S(2)}, headFs, th.textDim);
        cy += lineSmall;
    }
    if (recordFlash_ > 0.0f) {
        float pulse = 0.75f + 0.25f * std::sin(recordFlash_ * 8.0f);
        float fs = F(24) * std::min(1.0f, 0.6f + recordFlash_ * 2.0f);
        Color gold{255, 214, 90, static_cast<uint8_t>(255 * pulse)};
        ui.label("NOUVEAU RECORD !", {box.center().x, cy + S(17)}, fs, gold, Align::Center, FontFace::Title, VAlign::Middle);
    }
    ui.r().popClip();
    drawNames(ui, view, keepOut, keepOutCount);
}

void GhostRun::drawStatus(UIContext& ui, const Rect& area, float k) {
    const UITheme& th = ui.theme();
    float fs = 16.0f * k;
    if (loading()) {
        ui.label("Chargement des fantômes…", {area.right(), area.y}, fs, th.textDim, Align::Right);
    } else if (!refused_.empty() && race_.ghostTime() < 8.0f) {
        std::string t = refused_.size() > 1 ? std::to_string(refused_.size()) + " fantômes ignorés, dont " + refused_.front()
                                            : "Fantôme ignoré : " + refused_.front();
        ui.labelWrapped(t, {area.x, area.y}, area.w, fs, th.warm);
    }
}

void GhostRun::drawPreview(UIContext& ui, const Rect& area, float k) {
    // HUD editor: where the comparison box goes and how big it can be (no times shown).
    const UITheme& th = ui.theme();
    ui.r().fillRoundedRect(area, ui.s(8) * k, Color{8, 12, 26, 170});
    ui.r().drawRoundedRect(area, ui.s(8) * k, ghostPaletteColor(0).withAlpha(110), ui.s(1.5f) * k);
    float pad = ui.s(10) * k, inner = area.w - 2 * pad;
    std::string t = "FANTÔMES : ÉCART ET SEGMENTS";
    float f = ui.fitFont(t, FontFace::Bold, 14 * k, inner, 0.6f);
    ui.label(ui.elide(t, FontFace::Bold, f, inner), {area.x + pad, area.y + pad}, f, ghostPaletteColor(0), Align::Left, FontFace::Bold);
    ui.labelWrapped("Affiché pendant une course contre un fantôme : écart en direct, puis le temps de chaque point de sauvegarde.",
                    {area.x + pad, area.y + pad + ui.s(30) * k}, inner, 15 * k, th.textDim);
}

std::string GhostRun::checkpointName(Vec2 feet, int fallbackIndex) const {
    const Checkpoint* best = nullptr;
    float bestD = 260.0f * 260.0f;
    for (const Checkpoint& c : checkpoints_) {
        float d = (c.at - feet).lengthSq();
        if (d < bestD) {
            bestD = d;
            best = &c;
        }
    }
    if (best && !best->label.empty()) return best->label;
    return "Point " + std::to_string(fallbackIndex + 1);
}

void GhostRun::drawNames(UIContext& ui, const WorldView& view, const Rect* keepOut, int keepOutCount) {
    if (!settings_.showNames || race_.empty()) return;
    // Each name above its ghost, inside the screen, never on another name nor on the HUD
    // (moved up, then under the ghost; left out if there is no room).
    Rect placed[GhostRace::kMaxGhosts];
    int n = 0;
    const float fs = 16.0f;
    const float t = race_.ghostTime();
    const float W = ui.screenW(), H = ui.screenH(), m = ui.s(8);
    auto blocked = [&](const Rect& b) {
        if (b.x < m || b.right() > W - m || b.y < m || b.bottom() > H - m) return true;
        if (hudRect_.w > 0.0f && b.intersects(hudRect_)) return true;
        for (int j = 0; j < keepOutCount; ++j)
            if (keepOut[j].w > 0.0f && b.intersects(keepOut[j])) return true;
        for (int j = 0; j < n; ++j)
            if (b.intersects(placed[j])) return true;
        return false;
    };
    for (int i = 0; i < race_.size() && i < static_cast<int>(labels_.size()); ++i) {
        const GhostRace::Racer& g = race_.racer(i);
        if (!g.pose.visible || t > g.track.duration() + 2.1f) continue;
        const std::string& label = labels_[static_cast<size_t>(i)];
        Vec2 size = ui.text().measure(label, FontFace::Bold, ui.s(fs));
        Vec2 head = view.toScreen({g.pose.feet.x, g.pose.feet.y - 30.0f});  // just above the head
        Vec2 foot = view.toScreen({g.pose.feet.x, g.pose.feet.y + 6.0f});
        Rect box{head.x - size.x * 0.5f - ui.s(6), head.y - size.y - ui.s(4), size.x + ui.s(12), size.y + ui.s(4)};
        // Off screen: no name. Near an edge: kept inside.
        if (head.x < -box.w || head.x > W + box.w || foot.y < 0 || head.y > H) continue;
        box.x = std::clamp(box.x, m, W - m - box.w);
        bool ok = false;
        for (int step = 0; step < 4 && !ok; ++step) {
            if (!blocked(box)) ok = true;
            else box.y -= box.h + ui.s(2);
        }
        if (!ok) {
            box.y = foot.y + ui.s(2);  // under the ghost
            ok = !blocked(box);
        }
        if (!ok) continue;
        placed[n++] = box;
        float fade = std::clamp(1.0f - (t - g.track.duration() - 1.5f) / 0.6f, 0.0f, 1.0f);
        ui.r().fillRoundedRect(box, ui.s(5), Color{8, 12, 26, static_cast<uint8_t>(150 * fade)});
        ui.label(label, {box.center().x, box.center().y}, fs, g.color.withAlpha(static_cast<uint8_t>(235 * fade)), Align::Center,
                 FontFace::Bold, VAlign::Middle);
    }
}

}  // namespace cosmic
