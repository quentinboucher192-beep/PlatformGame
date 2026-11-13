#include "ui/online/OnlineToasts.h"

#include <algorithm>

#include "engine/core/Easing.h"
#include "game/GameServices.h"

namespace cosmic {

namespace {
constexpr float kLife = 5.5f;

Color kindColor(const UITheme& th, OnlineService::Notification::Kind k) {
    switch (k) {
        case OnlineService::Notification::Kind::Success: return th.success;
        case OnlineService::Notification::Kind::Warning: return th.warm;
        case OnlineService::Notification::Kind::Error: return th.danger;
        case OnlineService::Notification::Kind::Record: return Color{255, 206, 84, 255};
        default: return th.accent;
    }
}
}  // namespace

void OnlineToasts::update(GameServices& g, float dt) {
    for (auto& n : g.online.takeNotifications()) {
        toasts_.push_back({n, 0.0f});
        g.uiSound(n.kind == OnlineService::Notification::Kind::Record ? "ui_confirm" : "ui_move", 0.5f);
    }
    for (auto& t : toasts_) t.t += dt;
    while (!toasts_.empty() && toasts_.front().t > kLife) toasts_.pop_front();
    while (toasts_.size() > 4) toasts_.pop_front();
}

void OnlineToasts::render(GameServices& g) {
    if (toasts_.empty()) return;
    UIContext& ui = g.ui;
    ui.begin(g.app, g.app.frameTime() / 1000.0f);
    float w = ui.s(380), h = ui.s(70);
    float y = ui.s(78);
    for (const auto& t : toasts_) {
        float in = applyEase(Ease::OutCubic, std::clamp(t.t / 0.35f, 0.0f, 1.0f));
        float out = std::clamp((kLife - t.t) / 0.5f, 0.0f, 1.0f);
        float a = std::min(in, out);
        Rect r{ui.screenW() - ui.s(20) - w + (1.0f - in) * ui.s(60), y, w, h};
        Color c = kindColor(ui.theme(), t.n.kind);
        ui.r().fillRoundedRect(r, ui.s(10), Color{10, 15, 34, static_cast<uint8_t>(235 * a)});
        ui.r().fillRoundedRect({r.x, r.y, ui.s(5), r.h}, ui.s(2), c.withAlphaF(a));
        ui.r().drawRoundedRect(r, ui.s(10), c.withAlphaF(0.5f * a), ui.s(1.2f));
        ui.label(t.n.title, {r.x + ui.s(18), r.y + ui.s(22)}, 18, c.withAlphaF(a), Align::Left, FontFace::Bold, VAlign::Middle);
        ui.r().pushClip({r.x, r.y, r.w - ui.s(10), r.h});
        ui.label(t.n.text, {r.x + ui.s(18), r.y + ui.s(48)}, 15, ui.theme().text.withAlphaF(a), Align::Left, FontFace::Body, VAlign::Middle);
        ui.r().popClip();
        y += (h + ui.s(10)) * std::max(a, 0.2f);
    }
}

}  // namespace cosmic
