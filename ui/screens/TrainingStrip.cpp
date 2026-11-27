#include "ui/screens/TrainingStrip.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "game/GameServices.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

std::map<std::string, TrainingStrip::Report>& reports() {
    static std::map<std::string, TrainingStrip::Report> r;
    return r;
}

}  // namespace

bool TrainingStrip::draw(GameServices& g, statsart::LevelArt* art, const std::string& levelId, const Rect& levelBounds, const Rect& r, const Rect& area) {
    UIContext& ui = g.ui;
    rect_ = r;
    bounds_ = levelBounds.w > 1.0f && levelBounds.h > 1.0f ? levelBounds : Rect{0.0f, 0.0f, 3200.0f, 1200.0f};
    info_ = art && !levelId.empty() ? art->info(levelId) : nullptr;
    mapped_ = info_ && info_->ok && info_->bounds.w > 1.0f;
    // Asked at the size it is shown (whole pixels, rounded to 8: a few sizes only; at most the
    // largest picture LevelArt makes, stretched beyond).
    const int pw = std::clamp(static_cast<int>(std::lround(r.w / 8.0f)) * 8, 16, 2048);
    const int ph = std::clamp(static_cast<int>(std::lround(r.h / 8.0f)) * 8, 16, 1024);
    if (mapped_) {
        band_ = statsart::levelBand(*info_, pw, ph);
        bounds_ = info_->bounds;
        picture_ = {static_cast<float>(pw), static_cast<float>(ph)};
    }
    const Texture* t = mapped_ ? art->band(levelId, pw, ph) : nullptr;
    Report& rep = reports()[name_];
    rep = Report{};
    rep.levelId = levelId;
    rep.strip = r;
    rep.area = area.w > 0.0f && area.h > 0.0f ? area : r;
    if (t) {
        ui.r().fillRect(r, Color{8, 11, 24, 255});
        // A little darker: what is drawn over it stays easy to read.
        g.app.renderer().drawTexture(*t, nullptr, r, Color{222, 222, 222, 255});
        rep.picture = true;
    } else {
        ui.r().fillRoundedRect(r, ui.s(8), Color{20, 28, 54, 230});  // the schematic bar
    }
    ui.r().drawRoundedRect(r, ui.s(t ? 3 : 8), ui.theme().accent.withAlpha(t ? 90 : 60), std::max(1.0f, ui.s(1.0f)));
    return t != nullptr;
}

Vec2 TrainingStrip::toScreen(Vec2 world) const {
    if (mapped_) {
        const Vec2 p = band_.toPicture(world);
        return {rect_.x + p.x * rect_.w / picture_.x, rect_.y + p.y * rect_.h / picture_.y};
    }
    const float f = std::clamp((world.x - bounds_.x) / bounds_.w, 0.0f, 1.0f);
    return {rect_.x + rect_.w * f, rect_.center().y};
}

Vec2 TrainingStrip::toWorld(Vec2 screen) const {
    if (mapped_) return band_.toWorld({(screen.x - rect_.x) * picture_.x / std::max(1.0f, rect_.w), (screen.y - rect_.y) * picture_.y / std::max(1.0f, rect_.h)});
    const float f = std::clamp((screen.x - rect_.x) / std::max(1.0f, rect_.w), 0.0f, 1.0f);
    return {bounds_.x + bounds_.w * f, bounds_.center().y};
}

float TrainingStrip::worldPerPixel() const { return bounds_.w / std::max(1.0f, rect_.w); }

void TrainingStrip::note(const Rect& overlay) { reports()[name_].overlays.push_back(overlay); }

const TrainingStrip::Report& TrainingStrip::last(const std::string& name) { return reports()[name]; }

}  // namespace cosmic
