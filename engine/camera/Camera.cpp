#include "engine/camera/Camera.h"

#include <cmath>

namespace cosmic {

namespace {
// Smooth pseudo-noise for shake (sum of sines, cheap and continuous).
float noise1(float t, float seed) {
    return std::sin(t * 37.0f + seed) * 0.6f + std::sin(t * 61.0f + seed * 2.3f) * 0.4f;
}
}  // namespace

void Camera::reset(Vec2 center) {
    pos_ = prevPos_ = target_ = center;
    focusY_ = center.y;
    lookAhead_ = 0.0f;
    trauma_ = 0.0f;
    scripted_ = false;
    zoomDur_ = 0.0f;
}

void Camera::setTarget(Vec2 pos, Vec2 velocity, float facing, bool grounded) {
    target_ = pos;
    targetVel_ = velocity;
    facing_ = facing;
    grounded_ = grounded;
}

void Camera::setZone(const CameraZone* zone) {
    if (zone)
        zone_ = *zone;
    else
        zone_.reset();
}

void Camera::moveTo(Vec2 center, float duration, Ease ease) {
    scripted_ = true;
    scriptFrom_ = pos_;
    scriptTo_ = center;
    scriptT_ = 0.0f;
    scriptDur_ = std::max(0.0001f, duration);
    scriptEase_ = ease;
}

void Camera::zoomTo(float zoom, float duration, Ease ease) {
    zoomFrom_ = zoom_;
    zoomTo_ = std::clamp(zoom, 0.25f, 4.0f);
    zoomT_ = 0.0f;
    zoomDur_ = std::max(0.0001f, duration);
    zoomEase_ = ease;
    baseZoom_ = zoomTo_;
}

bool Camera::scriptBusy() const { return (scripted_ && scriptT_ < scriptDur_) || zoomT_ < zoomDur_; }

void Camera::addTrauma(float amount) { trauma_ = std::clamp(trauma_ + amount, 0.0f, 1.0f); }

Vec2 Camera::desiredCenter() const {
    Vec2 d{target_.x + lookAhead_, focusY_};
    if (zone_ && zone_->lockCenter) d = zone_->center;
    return d;
}

Vec2 Camera::clampCenter(Vec2 c, float zoom, int screenW, int screenH) const {
    float scale = (screenH / kViewHeight) * zoom;
    float halfW = screenW / scale * 0.5f;
    float halfH = screenH / scale * 0.5f;
    auto clampTo = [&](const Rect& b, Vec2 p) {
        Vec2 out = p;
        if (b.w <= halfW * 2) out.x = b.center().x;
        else out.x = std::clamp(p.x, b.x + halfW, b.right() - halfW);
        if (b.h <= halfH * 2) out.y = b.center().y;
        else out.y = std::clamp(p.y, b.y + halfH, b.bottom() - halfH);
        return out;
    };
    Vec2 out = c;
    if (zone_ && zone_->clampToArea && !zone_->lockCenter) out = clampTo(zone_->area, out);
    if (hasBounds_) out = clampTo(bounds_, out);
    return out;
}

void Camera::fixedUpdate(float dt) {
    prevPos_ = pos_;

    if (zoomT_ < zoomDur_) {
        zoomT_ += dt;
        zoom_ = lerpf(zoomFrom_, zoomTo_, applyEase(zoomEase_, std::min(1.0f, zoomT_ / zoomDur_)));
    } else if (zone_ && zone_->zoom > 0.0f && !scripted_) {
        zoom_ = lerpf(zoom_, zone_->zoom, damp(3.0f, dt));
    } else if (!scripted_) {
        zoom_ = lerpf(zoom_, baseZoom_, damp(3.0f, dt));
    }

    if (scripted_) {
        scriptT_ = std::min(scriptT_ + dt, scriptDur_);
        pos_ = lerpv(scriptFrom_, scriptTo_, applyEase(scriptEase_, scriptT_ / scriptDur_));
    } else {
        // Look-ahead in the facing / moving direction, eased so turning is not jarring.
        float speedFactor = std::clamp(std::abs(targetVel_.x) / 150.0f, 0.0f, 1.0f);
        float wantAhead = facing_ * (28.0f + 30.0f * speedFactor);
        lookAhead_ = lerpf(lookAhead_, wantAhead, damp(2.2f, dt));

        // Vertical: platform-snap when grounded, follow quickly when falling far.
        float dy = target_.y - focusY_;
        if (grounded_) {
            focusY_ = lerpf(focusY_, target_.y - 10.0f, damp(4.0f, dt));
        } else if (dy > 40.0f) {
            focusY_ = lerpf(focusY_, target_.y - 30.0f, damp(6.0f, dt));
        } else if (dy < -70.0f) {
            focusY_ = lerpf(focusY_, target_.y + 50.0f, damp(4.0f, dt));
        }

        Vec2 want = clampCenter(desiredCenter(), zoom_, lastScreenW_, lastScreenH_);
        float rate = (zone_ && zone_->lockCenter) ? 3.0f : 7.0f;
        pos_ = lerpv(pos_, want, damp(rate, dt));
    }

    // Trauma shake (offset proportional to trauma^2).
    shakeTime_ += dt;
    trauma_ = std::max(0.0f, trauma_ - dt * 1.4f);
    float amount = trauma_ * trauma_ * shakeScale_ * 9.0f;
    shakeOffset_ = {noise1(shakeTime_, 1.3f) * amount, noise1(shakeTime_, 7.9f) * amount};
}

void Camera::update(float /*dt*/) {}

WorldView Camera::view(int screenW, int screenH, float alpha) const {
    lastScreenW_ = screenW;
    lastScreenH_ = screenH;
    Vec2 c = lerpv(prevPos_, pos_, std::clamp(alpha, 0.0f, 1.0f));
    if (!scripted_) c = clampCenter(c, zoom_, screenW, screenH);
    c += shakeOffset_;
    WorldView v;
    v.scale = (screenH / kViewHeight) * zoom_;
    v.screenW = screenW;
    v.screenH = screenH;
    v.topLeft = {c.x - screenW / v.scale * 0.5f, c.y - screenH / v.scale * 0.5f};
    return v;
}

Rect Camera::visibleArea(int screenW, int screenH) const { return view(screenW, screenH, 1.0f).visibleWorld(); }

}  // namespace cosmic
