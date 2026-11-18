#include "game/raid/Reconnect.h"

#include <algorithm>
#include <cmath>

namespace cosmic::raid {

void Reconnect::lost(float now) {
    if (!active_) {
        active_ = true;
        since_ = now;
        attempts_ = 0;
        nextAt_ = now;  // the first attempt at once
    } else if (waiting_) {
        failed(now);    // the attempt under way died too
    }
    waiting_ = false;
}

void Reconnect::back() {
    active_ = false;
    waiting_ = false;
    attempts_ = 0;
}

bool Reconnect::due(float now) {
    if (!active_ || waiting_ || expired(now) || now < nextAt_) return false;
    waiting_ = true;
    ++attempts_;
    return true;
}

void Reconnect::failed(float now) {
    if (!active_) return;
    waiting_ = false;
    // 1, 2, 4, 8 s after the attempts that failed, then every 8 s.
    float wait = 1.0f;
    for (int i = 1; i < attempts_ && wait < kMaxBackoff; ++i) wait *= 2.0f;
    nextAt_ = now + std::min(kMaxBackoff, wait);
}

float Reconnect::remaining(float now) const { return active_ ? std::max(0.0f, kWindow - (now - since_)) : 0.0f; }

std::string Reconnect::message(float now) const {
    const int left = static_cast<int>(std::ceil(remaining(now)));
    std::string m = "Liaison avec le relais perdue : reconnexion automatique";
    if (attempts_ > 0) m += " (essai " + std::to_string(attempts_) + ")";
    m += ". Votre place est gardée encore " + std::to_string(left) + " s.";
    return m;
}

std::string Reconnect::gaveUp() {
    return "La liaison avec le relais n'a pas pu être rétablie en 60 s : votre place a été libérée. Revenez par la carte de la branche pour reprendre une place.";
}

}  // namespace cosmic::raid
