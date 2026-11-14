#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
class OnlineUi;

/// Comments of a community level (ONLINE LEVELS): one level of replies, five reactions
/// (one per player, changeable), pinned comments of the creator first, edit / delete of
/// one's own comments, reports. Everything comes from and goes to the server; the panel
/// only shows what the server allows (can_edit, can_pin...), and the server checks again.
class CommentsPanel {
public:
    CommentsPanel(GameServices& g, OnlineUi& ui);

    void open(const std::string& levelId, const std::string& title);
    void close() { levelId_.clear(); }
    bool isOpen() const { return !levelId_.empty(); }
    const std::string& levelId() const { return levelId_; }
    /// Draws the panel in `area`. Returns false when the player goes back to the list.
    bool draw(const Rect& area);
    /// A confirmation (delete, report) is open: the rest of the screen must not react.
    bool modal() const { return !confirmDelete_.empty() || !confirmReport_.empty(); }
    void drawModal();
    /// Opens every thread of replies once the comments are loaded (automated screenshots).
    void expandAll() { expandAll_ = true; }
    /// Reaction labels, in the server's order (like, love, funny, impressive, not_useful).
    static const char* reactionLabel(const std::string& kind);

private:
    void load(bool more);
    void loadReplies(const std::string& id);
    void submit();
    void react(const std::string& id, const std::string& kind, const std::string& mine);
    void pin(const std::string& id, bool on);
    void say(const std::string& m, bool error);
    Json* find(const std::string& id);
    /// Lays out one comment at (x, y), width w. Draws it when `draw`; returns its height.
    float item(const Json& c, float x, float y, float w, bool reply, bool draw);
    float listHeight(float w);

    GameServices& g_;
    OnlineUi& ui_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string levelId_, title_;
    int sort_ = 0;
    Json comments_ = Json::array();
    std::map<std::string, Json> replies_;
    std::set<std::string> expanded_;
    int nextOffset_ = -1, total_ = 0, generation_ = 0;
    bool busy_ = false, loaded_ = false, sending_ = false, expandAll_ = false;
    std::string draft_, replyTo_, replyName_, editing_;
    std::string confirmDelete_, confirmReport_;
    int reportReason_ = 0;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
};

}  // namespace cosmic
