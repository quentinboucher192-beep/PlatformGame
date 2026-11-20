#pragma once
// 2.77.4: the players' reviews of a raid event (server/src/raidReviews.ts, docs/RAIDS.md §24), as
// the game shows them, without drawing anything (ui/online/RaidReviewsPanel draws them).
//
// After playing an event, a player gives it 1 to 5 stars and, if they wish, a comment of 500
// characters at most: one review per player and event, changed or deleted by its author. Who may
// review is the SERVER's decision, from its own data (a seat in an instance of the event and a
// contribution its consensus accepted): the game only offers « Noter ce raid » when the server says
// `can_review`, never from what it knows itself, and the server checks again on every write.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic::raidreviews {

constexpr int kMinStars = 1;
constexpr int kMaxStars = 5;
/// The longest comment, as the server counts it (UTF-16 units, as JavaScript's `length`).
constexpr int kMaxText = 500;

/// The average and the number of reviews of an event (`reviews` of an event, `summary` of a list).
struct Summary {
    double average = 0.0;  // one decimal, as the server rounds it
    int count = 0;
    bool known = false;    // the server sent it (an older server: nothing shown)
};
Summary parseSummary(const Json& j);
/// « 4,2 » (one decimal, the French comma; the English display writes « 4.2 »).
std::string averageText(double average);
/// « 1 avis », « 12 avis ».
std::string countText(int count);
/// « 4,2 · 12 avis », « Aucun avis » (count 0); "" when unknown.
std::string summaryText(const Summary& s);

/// One review as the server sends it.
struct Review {
    std::string id, authorId, author;
    int stars = 0;
    std::string body;        // "" when there is none, or when it is masked for this player
    bool hasBody = false;
    bool hidden = false;     // its comment is masked (reports, or an administrator)
    std::string hiddenReason;  // "reports" or "moderation"
    bool mine = false, reported = false, canReport = false;
    int64_t createdAt = 0, editedAt = 0;  // UTC seconds (0: never edited)
    // The administrators' list only.
    int reportCount = 0;
    std::vector<std::string> reasons;
    bool hiddenByReports = false;
    bool valid = false;      // an id and 1 to 5 stars
};
Review parseReview(const Json& j);

/// GET /raids/events/{id}/reviews (the player's own review apart) or GET /admin/raids/{id}/reviews.
struct Page {
    bool valid = false;
    Summary summary;
    bool signedIn = false;   // `me` was sent: the request had a session
    bool played = false;     // the server's verdict: a seat and a validated contribution in this event
    bool canReview = false;
    bool hasMine = false;
    Review mine;
    std::vector<Review> reviews;  // the others', newest first (all of them for an administrator)
    int total = 0;
    int nextOffset = -1;     // -1: no more
};
Page parsePage(const Json& data, const Json& meta);

/// What the game offers the player: nothing (not signed in, did not play, unknown), « Noter ce
/// raid » (played, not reviewed yet) or « Modifier mon avis » (already reviewed).
enum class Offer { None, Rate, Edit };
Offer offerOf(const Page& p);
/// « Noter ce raid », « Modifier mon avis » ("" for None).
std::string offerLabel(Offer o);
/// Why there is no offer, to say under the reviews' header ("" when there is one).
std::string noOfferNote(const Page& p, bool loggedIn);

/// A rating's word: « Décevant », « Moyen », « Bien », « Très bien », « Excellent » ("" out of 1..5).
std::string starsWord(int stars);
/// The characters of `utf8` as the server counts them (UTF-16 units: one per letter, accented ones
/// included, two for a character beyond the basic plane such as an emoji).
int textLength(const std::string& utf8);
/// `utf8` without its spaces and line breaks at both ends (what the server keeps).
std::string trimmed(const std::string& utf8);
/// « 123 / 500 ».
std::string counterText(const std::string& text);
/// Why the editor cannot send its review ("" when it can): stars out of 1..5, a comment too long.
std::string problemOf(int stars, const std::string& text);
/// The body of PUT /raids/events/{id}/review.
Json requestBody(int stars, const std::string& text);
/// The line under a masked comment, for its author (`mine`) or another player.
std::string hiddenNote(const Review& r);

}  // namespace cosmic::raidreviews
