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
        case OnlineService::Notification::Kind::Friend: return Color{150, 170, 255, 255};
        case OnlineService::Notification::Kind::Message: return Color{120, 220, 255, 255};
        case OnlineService::Notification::Kind::Workshop: return Color{255, 170, 120, 255};
        default: return th.accent;
    }
}
}  // namespace

void OnlineToasts::update(GameServices& g, float dt) {
    // During a run the notifications stay queued in the service and appear afterwards.
    Scene* top = g.app.topScene();
    bool hold = top && top->holdsNotifications();
    for (auto& n : hold ? std::vector<OnlineService::Notification>{} : g.online.takeNotifications()) {
        toasts_.push_back({n, 0.0f});
        bool loud = n.kind == OnlineService::Notification::Kind::Record || n.kind == OnlineService::Notification::Kind::Friend ||
                    n.kind == OnlineService::Notification::Kind::Message || n.kind == OnlineService::Notification::Kind::Workshop;
        g.uiSound(loud ? "ui_confirm" : "ui_move", 0.5f);
    }
    for (auto& t : toasts_) t.t += dt;
    while (!toasts_.empty() && toasts_.front().t > kLife) toasts_.pop_front();
    while (toasts_.size() > 4) toasts_.pop_front();
}

void OnlineToasts::render(GameServices& g) {
    if (toasts_.empty()) return;
    UIContext& ui = g.ui;
    ui.begin(g.app, g.app.frameTime() / 1000.0f);
    float w = ui.s(420);
    float y = ui.s(78);
    for (const auto& t : toasts_) {
        // The text wraps: the toast grows instead of cutting the message.
        float textW = w - ui.s(36);
        float textH = ui.text().measureWrapped(t.n.text, FontFace::Body, ui.s(15), textW).y;
        float h = ui.s(44) + std::max(textH, ui.s(18)) + ui.s(10);
        float in = applyEase(Ease::OutCubic, std::clamp(t.t / 0.35f, 0.0f, 1.0f));
        float out = std::clamp((kLife - t.t) / 0.5f, 0.0f, 1.0f);
        float a = std::min(in, out);
        Rect r{ui.screenW() - ui.s(20) - w + (1.0f - in) * ui.s(60), y, w, h};
        Color c = kindColor(ui.theme(), t.n.kind);
        ui.r().fillRoundedRect(r, ui.s(10), Color{10, 15, 34, static_cast<uint8_t>(252 * a)});
        ui.r().fillRoundedRect({r.x, r.y, ui.s(5), r.h}, ui.s(2), c.withAlphaF(a));
        ui.r().drawRoundedRect(r, ui.s(10), c.withAlphaF(0.5f * a), ui.s(1.2f));
        ui.label(t.n.title, {r.x + ui.s(18), r.y + ui.s(22)}, 18, c.withAlphaF(a), Align::Left, FontFace::Bold, VAlign::Middle);
        ui.labelWrapped(t.n.text, {r.x + ui.s(18), r.y + ui.s(38)}, textW, 15, ui.theme().text.withAlphaF(a));
        y += (h + ui.s(10)) * std::max(a, 0.2f);
    }
}

}  // namespace cosmic
