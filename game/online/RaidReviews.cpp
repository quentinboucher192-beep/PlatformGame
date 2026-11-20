#include "game/online/RaidReviews.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/online/Raids.h"

namespace cosmic::raidreviews {

Summary parseSummary(const Json& j) {
    Summary s;
    if (!j.is_object()) return s;
    const Json count = jfield(j, "count");
    if (!count.is_number_integer() || count.get<int64_t>() < 0) return s;
    s.count = static_cast<int>(count.get<int64_t>());
    s.known = true;
    const Json avg = jfield(j, "average");
    if (s.count > 0 && avg.is_number()) s.average = std::clamp(avg.get<double>(), static_cast<double>(kMinStars), static_cast<double>(kMaxStars));
    else s.count = 0;  // no average: nothing to show as a rating
    return s;
}

std::string averageText(double average) {
    char b[16];
    std::snprintf(b, sizeof b, "%.1f", std::round(average * 10.0) / 10.0);
    std::string s = b;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string countText(int count) { return std::to_string(count) + " avis"; }

std::string summaryText(const Summary& s) {
    if (!s.known) return "";
    if (s.count <= 0) return "Aucun avis";
    return averageText(s.average) + " · " + countText(s.count);
}

Review parseReview(const Json& j) {
    Review r;
    if (!j.is_object()) return r;
    r.id = jstr(j, "id");
    const Json author = jfield(j, "author");
    if (author.is_object()) {
        r.authorId = jstr(author, "id");
        r.author = jstr(author, "username");
    }
    r.stars = jget<int>(j, "stars", 0);
    r.body = jstr(j, "body");
    r.hasBody = jget<bool>(j, "has_body", !r.body.empty());
    r.hidden = jget<bool>(j, "hidden", false);
    r.hiddenReason = r.hidden ? jstr(j, "hidden_reason", "moderation") : std::string();
    r.mine = jget<bool>(j, "mine", false);
    r.reported = jget<bool>(j, "reported", false);
    r.canReport = jget<bool>(j, "can_report", false) && !r.mine && !r.hidden;
    if (auto t = raids::parseUtc(jstr(j, "created_at"))) r.createdAt = *t;
    if (auto t = raids::parseUtc(jstr(j, "edited_at"))) r.editedAt = *t;
    r.reportCount = std::max(0, jget<int>(j, "report_count", 0));
    const Json reasons = jfield(j, "reasons", Json::array());
    if (reasons.is_array())
        for (const Json& x : reasons)
            if (x.is_string()) r.reasons.push_back(x.get<std::string>());
    r.hiddenByReports = jget<bool>(j, "hidden_by_reports", false);
    r.valid = !r.id.empty() && r.stars >= kMinStars && r.stars <= kMaxStars;
    return r;
}

Page parsePage(const Json& data, const Json& meta) {
    Page p;
    if (!data.is_object()) return p;
    p.summary = parseSummary(jfield(data, "summary"));
    const Json me = jfield(data, "me");
    if (me.is_object()) {
        p.signedIn = true;
        p.played = jget<bool>(me, "played", false);
        p.canReview = jget<bool>(me, "can_review", false);
        const Review mine = parseReview(jfield(me, "review"));
        if (mine.valid) {
            p.hasMine = true;
            p.mine = mine;
            p.mine.mine = true;
        }
    }
    const Json list = jfield(data, "reviews", Json::array());
    if (list.is_array())
        for (const Json& j : list) {
            Review r = parseReview(j);
            if (r.valid) p.reviews.push_back(std::move(r));
        }
    p.total = std::max(0, jget<int>(data, "total", static_cast<int>(p.reviews.size())));
    const Json next = jfield(meta, "next_offset");
    p.nextOffset = next.is_number_integer() && next.get<int64_t>() > 0 ? static_cast<int>(next.get<int64_t>()) : -1;
    p.valid = p.summary.known;
    return p;
}

Offer offerOf(const Page& p) {
    if (!p.valid || !p.signedIn) return Offer::None;
    if (p.hasMine) return Offer::Edit;  // their own review: always theirs to change or delete
    return p.played && p.canReview ? Offer::Rate : Offer::None;
}

std::string offerLabel(Offer o) {
    switch (o) {
        case Offer::Rate: return "Noter ce raid";
        case Offer::Edit: return "Modifier mon avis";
        case Offer::None: break;
    }
    return "";
}

std::string noOfferNote(const Page& p, bool loggedIn) {
    if (offerOf(p) != Offer::None) return "";
    if (!loggedIn) return "Connectez-vous pour noter un raid que vous avez joué.";
    if (!p.valid) return "";
    return "Seuls les joueurs qui ont joué cet événement peuvent le noter.";
}

std::string starsWord(int stars) {
    switch (stars) {
        case 1: return "Décevant";
        case 2: return "Moyen";
        case 3: return "Bien";
        case 4: return "Très bien";
        case 5: return "Excellent";
        default: return "";
    }
}

int textLength(const std::string& utf8) {
    int n = 0;
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n += len == 4 ? 2 : 1;  // beyond the basic plane: a surrogate pair in UTF-16
        i += len;
    }
    return n;
}

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string counterText(const std::string& text) { return std::to_string(textLength(trimmed(text))) + " / " + std::to_string(kMaxText); }

std::string problemOf(int stars, const std::string& text) {
    if (stars < kMinStars || stars > kMaxStars) return "Choisissez une note de 1 à 5 étoiles.";
    const int n = textLength(trimmed(text));
    if (n > kMaxText) return "Commentaire trop long : " + std::to_string(n) + " caractères pour " + std::to_string(kMaxText) + " au plus.";
    return "";
}

Json requestBody(int stars, const std::string& text) {
    Json body = {{"stars", stars}};
    const std::string t = trimmed(text);
    body["body"] = t;
    return body;
}

std::string hiddenNote(const Review& r) {
    if (!r.hidden) return "";
    if (r.mine)
        return r.hiddenReason == "reports" ? "Votre commentaire est masqué après des signalements, en attendant la décision des modérateurs (vous seul le voyez). Votre note compte toujours."
                                           : "Votre commentaire a été masqué par la modération (vous seul le voyez). Votre note compte toujours.";
    return r.hiddenReason == "reports" ? "Commentaire masqué après des signalements, en attendant la décision des modérateurs."
                                       : "Commentaire masqué par la modération.";
}

}  // namespace cosmic::raidreviews
