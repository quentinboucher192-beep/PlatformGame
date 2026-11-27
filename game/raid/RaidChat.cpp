#include "game/raid/RaidChat.h"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace cosmic::raidchat {

const char* channelLabel(Channel c) { return c == Channel::Raid ? "Raid" : "Groupe"; }

namespace {

/// The next code point of `s` from `i` (advanced past it); -1 for a byte that starts no valid character
/// (skipped alone).
int32_t nextCodePoint(const std::string& s, size_t& i) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    if (c < 0x80) {
        ++i;
        return c;
    }
    int n = 0;
    int32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
        n = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3;
        cp = c & 0x07;
    } else {
        ++i;
        return -1;
    }
    for (int k = 1; k <= n; ++k) {
        if (i + static_cast<size_t>(k) >= s.size() || (b(i + static_cast<size_t>(k)) & 0xC0) != 0x80) {
            ++i;
            return -1;
        }
        cp = (cp << 6) | (b(i + static_cast<size_t>(k)) & 0x3F);
    }
    // Overlong forms, surrogates and code points past Unicode are not characters.
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return -1;
    }
    i += static_cast<size_t>(n) + 1;
    return cp;
}

void appendUtf8(std::string& out, int32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// Removed from a message, as the discussions' messages (server/src/social.ts cleanMessage): the control
/// characters (C0, DEL, C1) and the invisible marks of direction (zero widths, embeddings, isolates).
bool dropped(int32_t cp) {
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
           (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

size_t codePoints(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();)
        if (nextCodePoint(s, i) >= 0) ++n;
    return n;
}

std::string clean(const std::string& raw) {
    std::string out;
    size_t chars = 0;
    bool space = false;  // a space waits (written before the next character: none at the end)
    for (size_t i = 0; i < raw.size() && chars < kMaxChars;) {
        const int32_t cp = nextCodePoint(raw, i);
        if (cp < 0) continue;
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
            space = !out.empty();
            continue;
        }
        if (dropped(cp)) continue;
        if (space) {
            out.push_back(' ');
            ++chars;
            space = false;
            if (chars >= kMaxChars) break;
        }
        appendUtf8(out, cp);
        ++chars;
    }
    return out;
}

Draft parseDraft(const std::string& field, Channel current) {
    Draft d;
    d.channel = current;
    size_t at = field.find_first_not_of(" \t");
    if (at == std::string::npos) return d;
    if (field[at] != '/') {
        d.text = clean(field);
        return d;
    }
    const size_t end = field.find_first_of(" \t", at);
    const std::string word = lowerAscii(field.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1));
    const std::string rest = end == std::string::npos ? std::string() : field.substr(end);
    if (word == "g" || word == "groupe" || word == "group") d.channel = Channel::Group;
    else if (word == "r" || word == "raid") d.channel = Channel::Raid;
    else {
        d.unknownCommand = true;
        return d;
    }
    d.text = clean(rest);
    d.switchOnly = d.text.empty();
    return d;
}

float RateLimit::tokensAt(float now) const {
    if (!started_) return static_cast<float>(burst_);
    return std::min(static_cast<float>(burst_), tokens_ + std::max(0.0f, now - last_) / refill_);
}

bool RateLimit::take(float now) {
    const float t = tokensAt(now);
    if (t < 1.0f) return false;
    tokens_ = t - 1.0f;
    last_ = now;
    started_ = true;
    return true;
}

float RateLimit::waitFor(float now) const {
    const float t = tokensAt(now);
    return t >= 1.0f ? 0.0f : (1.0f - t) * refill_;
}

void Log::push(Line l) {
    lines_.push_back(std::move(l));
    while (lines_.size() > kLogMax) lines_.pop_front();
    ++serial_;
}

bool Log::pushHistory(Line l) {
    for (const Line& o : lines_)
        if (o.channel == l.channel && o.kind == l.kind && o.at == l.at && o.playerId == l.playerId && o.text == l.text) return false;
    l.clock = -1e6f;  // in the history only: never shown as new
    // In its order: before the lines that came later (a line of the server written after it).
    auto it = lines_.end();
    while (it != lines_.begin() && std::prev(it)->at > l.at && std::prev(it)->at != 0) --it;
    lines_.insert(it, std::move(l));
    while (lines_.size() > kLogMax) lines_.pop_front();
    ++serial_;
    return true;
}

float lineAlpha(const Line& l, float now, float fadeSeconds) {
    const float age = now - l.clock;
    if (age < 0.0f) return 1.0f;
    if (age >= fadeSeconds) return 0.0f;
    const float fadeLen = std::min(1.0f, fadeSeconds * 0.5f);
    return age <= fadeSeconds - fadeLen ? 1.0f : (fadeSeconds - age) / fadeLen;
}

Access access(const Where& w, Channel c) {
    Access a;
    if (w.local) {
        a.can = true;  // shown here only: « TEST LOCAL — rien n'est envoyé »
        return a;
    }
    if (!w.relayed) {
        a.why = "Le chat n'est ouvert que pendant un raid.";
        return a;
    }
    if (!w.connected) {
        a.why = "Pas de liaison avec le relais pour l'instant : rien n'est envoyé.";
        return a;
    }
    if (!w.serverChat) {
        a.why = "Ce serveur ne transporte pas encore le chat des raids : rien n'est envoyé.";
        return a;
    }
    if (c == Channel::Group && w.watchingOther) {
        a.why = "Vous regardez un autre groupe : écrivez sur le canal Raid.";
        return a;
    }
    // Down, eliminated, watching their own group: they still read and write.
    a.can = true;
    a.sends = true;
    return a;
}

std::vector<Thought> AiFeed::update(float now, const std::vector<Thought>& current) {
    std::vector<int> present;
    for (const Thought& t : current) {
        Seat& s = seats_[t.seat];
        const std::string key = t.intent.empty() ? std::string() : t.role + "\x1f" + t.intent;
        if (key != s.key) {
            s.key = key;
            s.since = now;
        }
        s.pending = t;
        present.push_back(t.seat);
    }
    if (now - lastAny_ < kGlobalGap) return {};
    int best = -1;
    for (int seat : present) {
        const Seat& s = seats_[seat];
        if (s.key.empty() || s.key == s.posted || now - s.since < kStable || now - s.lastPost < kSeatGap) continue;
        if (best < 0 || s.lastPost < seats_[best].lastPost || (s.lastPost == seats_[best].lastPost && seat < best)) best = seat;
    }
    if (best < 0) return {};
    Seat& s = seats_[best];
    s.posted = s.key;
    s.lastPost = now;
    lastAny_ = now;
    return {s.pending};
}

Chat::Result Chat::submit(const std::string& field, const Where& w, float now, const std::function<void(Channel, const std::string&)>& send,
                          const std::string& myName, const std::string& myId) {
    const Draft d = parseDraft(field, channel);
    if (d.unknownCommand) {
        notice(channel, "Commandes du chat : /g (canal Groupe) et /r (canal Raid), suivies du message.", now);
        return Result::Unknown;
    }
    if (d.switchOnly) {
        channel = d.channel;
        return Result::Switched;
    }
    if (d.text.empty()) return Result::Empty;
    const Access a = access(w, d.channel);
    if (!a.can) {
        notice(d.channel, a.why, now);
        return Result::Refused;
    }
    if (!rate.take(now)) {
        notice(d.channel, "Trop de messages à la suite : attendez une seconde.", now);
        return Result::Wait;
    }
    if (!a.sends) {
        Line l;
        l.channel = d.channel;
        l.kind = Kind::Player;
        l.playerId = myId;
        l.name = myName;
        l.text = d.text;
        l.mine = true;
        l.seat = -1;
        l.clock = now;
        log.push(std::move(l));
        return Result::Shown;
    }
    if (send) send(d.channel, d.text);
    return Result::Sent;
}

void Chat::notice(Channel c, const std::string& text, float now) {
    Line l;
    l.channel = c;
    l.kind = Kind::Local;
    l.text = text;
    l.clock = now;
    log.push(std::move(l));
}

void Chat::aiLine(const Thought& t, float now) {
    Line l;
    l.channel = Channel::Group;
    l.kind = Kind::Ai;
    l.name = t.name;
    l.role = t.role;
    l.text = t.intent;
    l.seat = t.seat;
    l.clock = now;
    log.push(std::move(l));
}

const std::vector<const char*>& quickMessages() {
    static const std::vector<const char*> k = {"Prêt !",       "On y va !", "Attendez-moi !",   "Besoin d'aide ici !",
                                               "Réanimez-moi !", "Bien joué !", "Regroupons-nous !", "Merci !"};
    return k;
}

}  // namespace cosmic::raidchat
