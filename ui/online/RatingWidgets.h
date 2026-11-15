#pragma once

#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
class Renderer;
class UIContext;
struct LevelRatingInfo;

/// A five-pointed star (the game's fonts have no star glyph): filled, or its outline.
void drawRatingStar(Renderer& r, Vec2 center, float radius, Color color, bool filled);

/// "4,3 ★ · 128 notes" (or "aucune note pour l'instant") from `leftMid` (vertically centred),
/// at font size `size` (unscaled), shrunk to fit `maxWidth`. Returns the width drawn.
float drawRatingSummary(UIContext& ui, Vec2 leftMid, double average, int count, float size, Color text, float maxWidth);
float measureRatingSummary(UIContext& ui, double average, int count, float size);

/// The players' rating of an official level as the campaign shows it (cache of
/// OnlineService, nothing fetched here): "Note des joueurs : 4,3 ★ · 128 notes", or
/// "en ligne (Cosmic Online)" when nothing was ever received, and a second line saying when
/// an old value was received. Returns the height used.
float drawCampaignRating(GameServices& g, const std::string& levelId, Vec2 topLeft, float width, float size);

/// The players' rating of one official level, read from and sent to the server
/// (GET / PUT / DELETE /levels/{id}/rating): the average, the player's stars (only once the
/// server knows a verified run of theirs), changing or withdrawing them. Every answer also
/// updates the campaign's cache of averages.
class LevelRatingPanel {
public:
    explicit LevelRatingPanel(GameServices& g);
    void open(const std::string& levelId);
    void refresh();
    /// The run just finished: when its time becomes verified, the rating opens (results).
    void watchAttempt(const std::string& attemptId) { attemptId_ = attemptId; }
    const std::string& levelId() const { return levelId_; }
    bool loaded() const { return view_.is_object(); }
    bool busy() const { return busy_; }
    /// Last rating view received ({rating, my_stars, can_rate, reason, comments}).
    const Json& view() const { return view_; }
    bool fromCache() const { return fromCache_; }
    /// Why the last read failed ("" when it did not), and the server's error code.
    const std::string& error() const { return error_; }
    const std::string& errorCode() const { return errorCode_; }
    int comments() const;
    /// Results screen: one status line and five stars, in `area` (about 270 × 64).
    void drawCompact(const Rect& area);
    /// Thread page: the average, the explanation, the player's stars and "Retirer ma
    /// note". Returns the height used.
    float drawFull(const Rect& area);

private:
    void send(int stars);
    void say(const std::string& m, bool error);
    /// Five star buttons from `x` (each `size` wide); returns the stars clicked (0: none).
    int starRow(const std::string& id, float x, float y, float size, int mine, bool enabled);

    GameServices& g_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string levelId_;
    Json view_;
    bool busy_ = false, sending_ = false, fromCache_ = false;
    double cacheAge_ = 0.0;
    std::string error_, errorCode_;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
    std::string attemptId_;
    bool attemptVerifiedSeen_ = false;
    int generation_ = 0;
};

}  // namespace cosmic
