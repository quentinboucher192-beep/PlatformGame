#pragma once

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

/// Snapshot-based undo / redo: each committed edit stores the whole document. Simple,
/// robust (no inverse operations to get wrong) and cheap enough for level-sized JSON.
class EditorHistory {
public:
    explicit EditorHistory(size_t limit = 150) : limit_(limit) {}

    void reset(const Json& doc, const std::string& label = "Ouverture") {
        states_.clear();
        labels_.clear();
        states_.push_back(doc);
        labels_.push_back(label);
        index_ = 0;
    }
    /// Records `doc` as a new state if it differs from the current one.
    bool commit(const Json& doc, const std::string& label) {
        if (!states_.empty() && states_[index_] == doc) return false;
        states_.resize(index_ + 1);
        labels_.resize(index_ + 1);
        states_.push_back(doc);
        labels_.push_back(label);
        if (states_.size() > limit_) {
            states_.erase(states_.begin());
            labels_.erase(labels_.begin());
        }
        index_ = states_.size() - 1;
        return true;
    }
    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ + 1 < states_.size(); }
    const Json& undo() {
        if (canUndo()) --index_;
        return states_[index_];
    }
    const Json& redo() {
        if (canRedo()) ++index_;
        return states_[index_];
    }
    const std::string& undoLabel() const { return labels_[index_]; }
    const std::string& redoLabel() const { return labels_[std::min(index_ + 1, labels_.size() - 1)]; }
    size_t size() const { return states_.size(); }

private:
    size_t limit_;
    std::vector<Json> states_;
    std::vector<std::string> labels_;
    size_t index_ = 0;
};

/// Pan / zoom camera of the editors' viewports.
struct EditorCamera {
    Vec2 center{240, 135};
    float zoom = 1.0f;

    WorldView view(const Rect& viewport) const {
        WorldView v;
        v.scale = (viewport.h / 270.0f) * zoom;
        v.screenW = static_cast<int>(viewport.w);
        v.screenH = static_cast<int>(viewport.h);
        v.topLeft = {center.x - viewport.w / v.scale * 0.5f - viewport.x / v.scale,
                     center.y - viewport.h / v.scale * 0.5f - viewport.y / v.scale};
        return v;
    }
    void zoomAt(const Rect& viewport, Vec2 screen, float factor) {
        WorldView before = view(viewport);
        Vec2 w = before.toWorld(screen);
        zoom = std::clamp(zoom * factor, 0.15f, 6.0f);
        WorldView after = view(viewport);
        Vec2 w2 = after.toWorld(screen);
        center += w - w2;
    }
};

}  // namespace cosmic
