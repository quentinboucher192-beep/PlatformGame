#include "engine/core/I18n.h"

#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

namespace cosmic::i18n {

namespace {

// ------------------------------------------------------------------ UTF-8 helpers

bool isDigit(char c) { return c >= '0' && c <= '9'; }

/// Letter at s[i] (ASCII or Latin-1 / Latin Extended-A in UTF-8): its byte length, else 0.
size_t letterAt(std::string_view s, size_t i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
    if (i + 1 >= s.size()) return 0;
    unsigned char d = static_cast<unsigned char>(s[i + 1]);
    if (c == 0xC3 && d >= 0x80 && d <= 0xBF && d != 0x97 && d != 0xB7) return 2;  // À..ÿ but × ÷
    if ((c == 0xC4 || c == 0xC5) && d >= 0x80 && d <= 0xBF) return 2;              // Œ œ Ÿ…
    return 0;
}

bool hasLetter(std::string_view s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (letterAt(s, i)) return true;
    return false;
}

int letterCount(std::string_view s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if (size_t l = letterAt(s, i)) {
            ++n;
            i += l - 1;
        }
    return n;
}

bool isLowerAt(std::string_view s, size_t i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c >= 'a' && c <= 'z') return true;
    if (c == 0xC3 && i + 1 < s.size()) {
        unsigned char d = static_cast<unsigned char>(s[i + 1]);
        return d >= 0xA0 && d <= 0xBF && d != 0xB7;
    }
    if (c == 0xC5 && i + 1 < s.size()) return static_cast<unsigned char>(s[i + 1]) == 0x93;
    return false;
}

bool hasLower(std::string_view s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (isLowerAt(s, i)) return true;
    return false;
}

/// Changes the case of the letter at s[i] (in place; same byte length).
void setCaseAt(std::string& s, size_t i, bool upper) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        if (upper && c >= 'a' && c <= 'z') s[i] = static_cast<char>(c - 32);
        if (!upper && c >= 'A' && c <= 'Z') s[i] = static_cast<char>(c + 32);
        return;
    }
    if (i + 1 >= s.size()) return;
    unsigned char d = static_cast<unsigned char>(s[i + 1]);
    if (c == 0xC3) {
        if (upper && d >= 0xA0 && d <= 0xBE && d != 0xB7) s[i + 1] = static_cast<char>(d - 0x20);
        if (!upper && d >= 0x80 && d <= 0x9E && d != 0x97) s[i + 1] = static_cast<char>(d + 0x20);
    } else if (c == 0xC5) {
        if (upper && d == 0x93) s[i + 1] = static_cast<char>(0x92);
        if (!upper && d == 0x92) s[i + 1] = static_cast<char>(0x93);
    }
}

std::string toUpper(std::string_view s) {
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i) setCaseAt(out, i, true);
    return out;
}

/// Index of the first letter (npos when none).
size_t firstLetter(std::string_view s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (letterAt(s, i)) return i;
    return std::string_view::npos;
}

bool isSpaceAt(std::string_view s, size_t i, size_t& len) {
    if (s[i] == ' ') {
        len = 1;
        return true;
    }
    if (i + 1 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2 && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
        len = 2;  // no-break space
        return true;
    }
    if (i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xE2 && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
        static_cast<unsigned char>(s[i + 2]) == 0xAF) {
        len = 3;  // narrow no-break space
        return true;
    }
    return false;
}

/// French typography of the pieces kept around a translated text (« : », « ! », « « » »).
std::string englishTypography(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        size_t sp = 0;
        if (isSpaceAt(s, i, sp)) {
            size_t j = i + sp;
            if (j < s.size() && (s[j] == ':' || s[j] == '!' || s[j] == '?' || s[j] == ';')) {
                i = j;  // no space before these signs in English
                continue;
            }
            if (j + 1 < s.size() && static_cast<unsigned char>(s[j]) == 0xC2 && static_cast<unsigned char>(s[j + 1]) == 0xBB) {
                i = j;  // « … »: the space inside the closing quote goes too
                continue;
            }
        }
        if (i + 1 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2 &&
            (static_cast<unsigned char>(s[i + 1]) == 0xAB || static_cast<unsigned char>(s[i + 1]) == 0xBB)) {
            out.push_back('"');
            i += 2;
            if (static_cast<unsigned char>(s[i - 1]) == 0xAB) {
                size_t sp2 = 0;
                if (i < s.size() && isSpaceAt(s, i, sp2)) i += sp2;
            }
            continue;
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

/// A model key « … {0} … {1} … » cut into its pieces; false when it has no slot.
bool parseModel(std::string_view key, std::vector<std::string>& literals, std::vector<int>& slots) {
    literals.assign(1, std::string());
    slots.clear();
    for (size_t i = 0; i < key.size();) {
        if (key[i] == '{') {
            size_t j = i + 1;
            while (j < key.size() && isDigit(key[j])) ++j;
            if (j > i + 1 && j < key.size() && key[j] == '}') {
                slots.push_back(std::atoi(std::string(key.substr(i + 1, j - i - 1)).c_str()));
                literals.emplace_back();
                i = j + 1;
                continue;
            }
        }
        literals.back().push_back(key[i]);
        ++i;
    }
    return !slots.empty();
}

/// Coverage of a translation: Full (every word known), Partial (some), None.
enum Cover { None = 0, Partial = 1, Full = 2 };

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

/// Number in a text (« 1 », « 3,5 »…), for the choice of a singular or plural word.
bool isOne(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && !isDigit(s[a])) ++a;
    if (a == s.size()) return false;
    size_t b = a;
    while (b < s.size() && isDigit(s[b])) ++b;
    if (b + 1 < s.size() && (s[b] == ',' || s[b] == '.') && isDigit(s[b + 1])) return false;  // 1,5: plural
    std::string_view n = s.substr(a, b - a);
    while (n.size() > 1 && n.front() == '0') n.remove_prefix(1);
    return n == "1";
}

const char* const kSeparators[] = {"  ·  ", " · ", " — ", " – ", " | ", " • ", " : ", " ; ", ", "};

struct SvHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

}  // namespace

// ================================================================== Dictionary

void Dictionary::clear() {
    exact_.clear();
    models_.clear();
    upper_.clear();
}

void Dictionary::add(std::string_view french, std::string_view translated) {
    if (french.empty()) return;
    Model m;
    if (parseModel(french, m.literals, m.slots)) {
        // Two slots side by side cannot be told apart: such a model is never used.
        for (size_t i = 1; i + 1 < m.literals.size(); ++i)
            if (m.literals[i].empty()) return;
        int letters = 0;
        for (const auto& l : m.literals) {
            m.literalLength += l.size();
            letters += letterCount(l);
        }
        // « {0} : {1} », « {0} à {1} », « {0} s »: used only when what fills the slots is
        // known too (never to cut an unknown sentence).
        m.generic = letters < 3;
        m.value.assign(translated);
        for (auto& old : models_)
            if (old.literals == m.literals && old.slots == m.slots) {
                old = std::move(m);
                return;
            }
        models_.push_back(std::move(m));
        // Most specific models first (longest text around the slots).
        std::stable_sort(models_.begin(), models_.end(),
                         [](const Model& a, const Model& b) { return a.literalLength > b.literalLength; });
        return;
    }
    exact_[std::string(french)] = std::string(translated);
    if (hasLetter(french)) upper_.try_emplace(toUpper(french), std::string(french));
}

size_t Dictionary::load(const Json& j) {
    const Json& strings = j.is_object() && j.contains("strings") ? j["strings"] : j;
    if (!strings.is_object()) return 0;
    size_t n = 0;
    // Exact texts first, models sorted once at the end (faster than one sort per model).
    std::vector<std::pair<std::string, std::string>> models;
    for (auto it = strings.begin(); it != strings.end(); ++it) {
        if (!it.value().is_string()) continue;
        std::string v = it.value().get<std::string>();
        if (v.empty()) continue;  // not translated yet: shown in French
        std::vector<std::string> lits;
        std::vector<int> slots;
        if (parseModel(it.key(), lits, slots)) models.emplace_back(it.key(), std::move(v));
        else add(it.key(), v);
        ++n;
    }
    for (auto& [k, v] : models) add(k, v);
    return n;
}

const std::string* Dictionary::exact(std::string_view french) const {
    auto it = exact_.find(french);
    return it == exact_.end() ? nullptr : &it->second;
}

std::string Dictionary::localizeNumbers(std::string_view in) {
    std::string s(in);
    // Decimal comma between digits: 2,45 → 2.45.
    for (size_t i = 1; i + 1 < s.size(); ++i)
        if (s[i] == ',' && isDigit(s[i - 1]) && isDigit(s[i + 1])) s[i] = '.';
    // Groups of thousands: 12 345 → 12,345; and 45 % → 45%.
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        size_t sp = 0;
        if (i > 0 && isDigit(s[i - 1]) && isSpaceAt(s, i, sp)) {
            size_t j = i + sp;
            if (j < s.size() && s[j] == '%') {
                i = j;
                continue;
            }
            // Start of this number: 1 to 3 digits, not after a decimal point.
            size_t a = i;
            while (a > 0 && isDigit(s[a - 1])) --a;
            bool lead = i - a >= 1 && i - a <= 3 && (a == 0 || (s[a - 1] != '.' && s[a - 1] != ',' && !isDigit(s[a - 1])));
            // Previous group already converted: « 1,000 000 ».
            if (!lead && i - a == 3 && a > 0 && !out.empty() && out[out.size() - 4] == ',') lead = true;
            bool group = j + 3 <= s.size() && isDigit(s[j]) && isDigit(s[j + 1]) && isDigit(s[j + 2]) &&
                         (j + 3 == s.size() || !isDigit(s[j + 3]));
            if (lead && group) {
                out.push_back(',');
                i = j;
                continue;
            }
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

namespace {
/// While the i18n audit judges a text: the parts put in a model's slots that stayed untranslated.
thread_local std::vector<std::string>* t_auditParts = nullptr;
}  // namespace

std::string Dictionary::piece(std::string_view s, int depth) const {
    std::string out;
    const bool full = translateInto(s, out, depth + 1);
    if (!full && t_auditParts && letterCount(s) >= 2) t_auditParts->emplace_back(s);
    return out.empty() ? std::string(s) : out;
}

std::string Dictionary::fill(const Model& m, const std::vector<std::string_view>& caps, int depth) const {
    auto capture = [&](int slot) -> std::string_view {
        for (size_t i = 0; i < m.slots.size(); ++i)
            if (m.slots[i] == slot) return caps[i];
        return {};
    };
    std::string out;
    const std::string& v = m.value;
    for (size_t i = 0; i < v.size();) {
        if (v[i] == '{') {
            size_t j = i + 1;
            while (j < v.size() && isDigit(v[j])) ++j;
            if (j > i + 1 && j < v.size() && (v[j] == '}' || v[j] == '|')) {
                int slot = std::atoi(v.substr(i + 1, j - i - 1).c_str());
                if (v[j] == '}') {
                    out += piece(capture(slot), depth);
                    i = j + 1;
                    continue;
                }
                // {0|one|many}
                size_t bar = v.find('|', j + 1), end = v.find('}', j + 1);
                if (bar != std::string::npos && end != std::string::npos && bar < end) {
                    out += isOne(capture(slot)) ? v.substr(j + 1, bar - j - 1) : v.substr(bar + 1, end - bar - 1);
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push_back(v[i]);
        ++i;
    }
    return out;
}

bool Dictionary::byModel(std::string_view s, std::string& out, int depth, bool generic) const {
    std::vector<std::string_view> caps;
    for (const Model& m : models_) {
        if (m.generic != generic || s.size() <= m.literalLength) continue;
        const std::string& first = m.literals.front();
        const std::string& last = m.literals.back();
        if (!startsWith(s, first) || !endsWith(s, last)) continue;
        size_t pos = first.size(), end = s.size() - last.size();
        if (end <= pos) continue;
        caps.clear();
        bool ok = true;
        for (size_t k = 1; k + 1 < m.literals.size(); ++k) {
            size_t f = s.find(m.literals[k], pos + 1);
            if (f == std::string_view::npos || f + m.literals[k].size() > end) {
                ok = false;
                break;
            }
            caps.push_back(s.substr(pos, f - pos));
            pos = f + m.literals[k].size();
        }
        if (!ok || end <= pos) continue;
        caps.push_back(s.substr(pos, end - pos));
        // A generic model only applies when every inserted part is known (a number, a known
        // name…); any model, when an inserted part runs over a separator (« Réglages : Vie
        // et » in « {0} énergie »): the text is rather made of pieces.
        bool known = true;
        for (auto c : caps) {
            bool spans = false;
            for (const char* sep : kSeparators)
                if (std::string_view(sep) != ", " && c.find(sep) != std::string_view::npos) spans = true;
            std::string tmp;
            if ((m.generic || spans) && hasLetter(c) && !translateInto(c, tmp, depth + 1)) {
                known = false;
                break;
            }
        }
        if (!known) continue;
        out = fill(m, caps, depth);
        return true;  // models are sorted: the first that fits is the most specific
    }
    return false;
}

bool Dictionary::byCase(std::string_view s, std::string& out, int depth) const {
    (void)depth;
    if (!hasLower(s)) {
        // TITRE EN CAPITALES: the same words as a known text.
        auto it = upper_.find(s);
        if (it == upper_.end()) return false;
        out = toUpper(exact_.at(it->second));
        return true;
    }
    // A known text with the case of its first letter changed.
    size_t f = firstLetter(s);
    if (f != 0) return false;
    std::string other(s);
    bool lower = isLowerAt(s, 0);
    setCaseAt(other, 0, lower);
    if (const std::string* v = exact(other)) {
        out = *v;
        if (size_t g = firstLetter(out); g != std::string::npos) setCaseAt(out, g, !lower);
        return true;
    }
    return false;
}

bool Dictionary::byParts(std::string_view s, std::string& out, int depth) const {
    // 2.75.4: pieces joined by the interface (« Fantômes · Comparer deux replays dans le lecteur de
    // replays. »), each one a known text: first, before the signs around the whole are cut (its last
    // piece lost its final period, and a model such as « {0} replays » took it).
    for (const char* sepC : {"  ·  ", " · ", " | ", " • "}) {
        std::string_view sep(sepC);
        if (s.find(sep) == std::string_view::npos) continue;
        std::string res;
        bool all = true;
        for (size_t start = 0; all;) {
            const size_t f = s.find(sep, start);
            const std::string_view part = s.substr(start, f == std::string_view::npos ? std::string_view::npos : f - start);
            std::string t;
            if (part.empty()) res.append(part);
            else if (translateInto(part, t, depth + 1)) res += t;  // no letter: the number written the English way
            else all = false;
            if (f == std::string_view::npos) break;
            res.append(sep);
            start = f + sep.size();
        }
        if (all) {
            out = res;
            return true;
        }
        break;
    }
    // 2.75.4: a counter after a known text: « Détruire le soufflet ouest (un groupe) (0 / 2) ».
    if (s.size() > 3 && s.back() == ')') {
        const size_t open = s.rfind(" (");
        if (open != std::string_view::npos && open > 0 && !hasLetter(s.substr(open)) && hasLetter(s.substr(0, open))) {
            std::string core;
            if (translateInto(s.substr(0, open), core, depth + 1)) {
                out = core + localizeNumbers(s.substr(open));
                return true;
            }
        }
    }
    // Signs around a known text: « • Texte », « Texte : », « (Texte) », « Texte… ».
    std::string decorated;
    size_t a = 0, b = s.size();
    while (a < b) {
        if (letterAt(s, a) || isDigit(s[a])) break;
        ++a;
    }
    while (b > a) {
        // Step back over one UTF-8 character.
        size_t p = b - 1;
        while (p > a && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) --p;
        if (letterAt(s, p) || isDigit(s[p])) break;
        b = p;
    }
    // 2.75.4: the sentence keeps its own final sign inside quotes (« « Son dernier regard : survivez ! » »).
    size_t bs = b;
    for (bool more = true; more;) {
        more = false;
        size_t k = bs, sp = 0;
        if (k < s.size() && isSpaceAt(s, k, sp)) k += sp;
        if (k < s.size() && (s[k] == '.' || s[k] == '!' || s[k] == '?')) bs = k + 1, more = true;
        else if (k + 3 <= s.size() && s.compare(k, 3, "\xE2\x80\xA6") == 0) bs = k + 3, more = true;  // …
    }
    if ((a != 0 || b != s.size()) && a < b) {
        // Signs before only (« • Phrase complète. »), after only, then both; with the sentence's own sign first.
        std::vector<std::pair<size_t, size_t>> cuts;
        if (bs != b && bs != s.size()) cuts.emplace_back(a, bs);
        cuts.insert(cuts.end(), {{a, s.size()}, {0, b}, {a, b}});
        for (auto [ca, cb] : cuts) {
            if (ca == 0 && cb == s.size()) continue;
            // 2.75.4: never a bracket cut off from its pair (« Détruire le soufflet (un groupe » would
            // then be read by a model « {0} groupe »).
            const std::string_view kept = s.substr(ca, cb - ca);
            if (std::count(kept.begin(), kept.end(), '(') != std::count(kept.begin(), kept.end(), ')')) continue;
            std::string core;
            bool full = translateInto(s.substr(ca, cb - ca), core, depth + 1);
            if (core.empty()) continue;
            if (decorated.empty() || full) decorated = englishTypography(s.substr(0, ca)) + core + englishTypography(s.substr(cb));
            if (full) {
                out = decorated;
                return true;
            }
        }
    }
    // Pieces between separators (« A · B », « A — B »).
    for (const char* sepC : kSeparators) {
        std::string_view sep(sepC);
        if (s.find(sep) == std::string_view::npos) continue;
        std::string res;
        bool any = false, all = true;
        size_t start = 0;
        while (true) {
            size_t f = s.find(sep, start);
            std::string_view part = s.substr(start, f == std::string_view::npos ? std::string_view::npos : f - start);
            std::string t;
            bool full = !part.empty() && translateInto(part, t, depth + 1);
            if (!t.empty()) {
                res += t;
                any |= hasLetter(part);
            } else {
                res.append(part);
            }
            all &= full || letterCount(part) < 2;  // « o17 », « x »: an id, a unit
            if (f == std::string_view::npos) break;
            res += sep == " : " ? std::string(": ") : sep == " ; " ? std::string("; ") : std::string(sep);
            start = f + sep.size();
        }
        if (!any) break;
        out = res;
        return all;
    }
    if (decorated.empty()) return false;
    out = decorated;
    return false;
}

bool Dictionary::translateInto(std::string_view s, std::string& out, int depth) const {
    if (depth > 6 || s.empty()) return false;
    if (!hasLetter(s)) {
        out = localizeNumbers(s);
        return true;
    }
    if (const std::string* v = exact(s)) {
        out = *v;
        return true;
    }
    // Spaces around: the text inside, the spaces kept.
    size_t a = s.find_first_not_of(" \t\n\r"), b = s.find_last_not_of(" \t\n\r");
    if (a != 0 || b + 1 != s.size()) {
        std::string core;
        bool full = translateInto(s.substr(a, b - a + 1), core, depth + 1);
        if (!core.empty()) out = std::string(s.substr(0, a)) + core + std::string(s.substr(b + 1));
        return full;
    }
    // Several lines: each one.
    if (s.find('\n') != std::string_view::npos) {
        std::string res;
        bool all = true, any = false;
        size_t start = 0;
        while (true) {
            size_t f = s.find('\n', start);
            std::string_view line = s.substr(start, f == std::string_view::npos ? std::string_view::npos : f - start);
            std::string t;
            bool full = !line.empty() && translateInto(line, t, depth + 1);
            if (!t.empty()) {
                res += t;
                any = true;
            } else {
                res.append(line);
            }
            all &= full || !hasLetter(line);
            if (f == std::string_view::npos) break;
            res.push_back('\n');
            start = f + 1;
        }
        if (any) out = res;
        return any && all;
    }
    if (byModel(s, out, depth, false)) return true;
    if (byCase(s, out, depth)) return true;
    std::string parts;
    if (byParts(s, parts, depth)) {
        out = parts;
        return true;
    }
    if (byModel(s, out, depth, true)) return true;
    if (!parts.empty()) {
        out = parts;  // some pieces known: better than nothing
        return false;
    }
    return byWords(s, out);
}

bool Dictionary::byWords(std::string_view s, std::string& out) const {
    // Last resort: runs of words between signs and numbers (« Platine < 3:20 »,
    // « Vies 3/5 »), each one replaced when it is a known text.
    auto known = [&](std::string_view w, std::string& t) {
        if (const std::string* v = exact(w)) {
            t = *v;
            return true;
        }
        return byCase(w, t, 0);
    };
    // A group of words made of known texts (« Poser la balise gratuit », « Noyau et Comète »):
    // the longest known start first. Only short groups (no unknown sentence is cut up).
    std::function<bool(std::string_view, std::string&, int)> segment = [&](std::string_view w, std::string& t, int words) {
        if (known(w, t)) return true;
        if (words <= 1) return false;
        for (size_t sp = w.rfind(' '); sp != std::string_view::npos && sp > 0; sp = sp > 1 ? w.rfind(' ', sp - 1) : std::string_view::npos) {
            std::string a, b;
            int left = static_cast<int>(std::count(w.begin(), w.begin() + static_cast<std::ptrdiff_t>(sp), ' ')) + 1;
            if (known(w.substr(0, sp), a) && segment(w.substr(sp + 1), b, words - left)) {
                t = a + " " + b;
                return true;
            }
        }
        return false;
    };
    std::string res;
    bool any = false, all = true;
    size_t i = 0;
    while (i < s.size()) {
        size_t l = letterAt(s, i);
        if (!l) {
            res.push_back(s[i]);
            ++i;
            continue;
        }
        size_t j = i;
        while (j < s.size()) {
            if (size_t k = letterAt(s, j)) j += k;
            else if ((s[j] == ' ' || s[j] == '\'' || s[j] == '-') && j + 1 < s.size() && letterAt(s, j + 1)) ++j;
            else break;
        }
        std::string_view run = s.substr(i, j - i);
        std::string t;
        if (letterCount(run) < 2) {  // a lone letter (« s », « x », « D ») is no word to translate
            res.append(run);
        } else if (const std::string* v = exact(run)) {
            res += *v;
            any = true;
        } else if (byCase(run, t, 0)) {
            res += t;
            any = true;
        } else if (int words = static_cast<int>(std::count(run.begin(), run.end(), ' ')) + 1; words <= 8 && segment(run, t, words)) {
            res += t;
            any = true;
        } else {
            res.append(run);
            all = false;
        }
        i = j;
    }
    if (!any) return false;
    out = localizeNumbers(res);
    return all;
}

bool Dictionary::lookup(std::string_view french, std::string& out) const {
    out.clear();
    return translateInto(french, out, 0);
}

std::string Dictionary::translate(std::string_view french) const {
    std::string out;
    translateInto(french, out, 0);
    return out.empty() ? std::string(french) : out;
}

// ================================================================== language in use

namespace {

struct State {
    std::mutex mutex;
    Language language = Language::French;
    std::string folder;
    Dictionary english;
    bool englishLoaded = false;
    // Texts already shown: this generation and the previous one (texts made every frame,
    // such as counters, never make the cache grow without end).
    using Cache = std::unordered_map<std::string, std::string, SvHash, std::equal_to<>>;
    Cache cache, previous;
    void clearCache() {
        cache.clear();
        previous.clear();
    }
    std::set<std::string> missing;
    std::string missingFile;
    bool missingInit = false;
    // 2.75.4: the i18n audit (startAudit).
    struct Audit {
        std::string file;
        std::vector<AuditFinding> found;
        std::set<std::string, std::less<>> judged;     // French: texts already judged
        std::set<std::string, std::less<>> produced;   // English: translations handed out
        std::set<std::string, std::less<>> englishWords, frenchWords, englishOnlyTexts;
    } audit;
};

/// The i18n audit is on (read on the slow path only: a text not in the cache).
bool g_audit = false;

State& state() {
    static State s;
    return s;
}

thread_local int g_verbatim = 0;

void ensureLoaded(State& st, Language l) {
    if (l != Language::English || st.englishLoaded) return;
    st.englishLoaded = true;
    fs::path dir = st.folder.empty() ? Paths::dataDir() / "i18n" : Paths::fromUtf8(st.folder);
    fs::path file = dir / "en.json";
    if (auto j = FileUtil::loadJson(file)) {
        size_t n = st.english.load(*j);
        Log::info("Langue : {} textes anglais ({})", n, Paths::toUtf8(file));
    } else {
        Log::warn("Langue : dictionnaire anglais introuvable ({}), textes en français", Paths::toUtf8(file));
    }
}

/// Untranslated texts seen while playing, written to $COSMIC_I18N_MISSING (to complete
/// the dictionary: run the game in English with this variable set).
void noteMissing(State& st, std::string_view s) {
    if (!st.missingInit) {
        st.missingInit = true;
        if (const char* f = std::getenv("COSMIC_I18N_MISSING")) st.missingFile = f;
    }
    if (st.missingFile.empty() || !st.missing.insert(std::string(s)).second) return;
    if (FILE* f = std::fopen(st.missingFile.c_str(), "ab")) {
        std::string line = Json(std::string(s)).dump() + "\n";
        std::fwrite(line.data(), 1, line.size(), f);
        std::fclose(f);
    }
}

// ------------------------------------------------------------------ i18n audit (2.75.4)

/// The words of a text (runs of letters, lowercase; apostrophes and hyphens cut them).
template <class F>
void eachWord(std::string_view s, F&& f) {
    for (size_t i = 0; i < s.size();) {
        if (!letterAt(s, i)) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < s.size())
            if (size_t k = letterAt(s, j)) j += k;
            else break;
        std::string w(s.substr(i, j - i));
        for (size_t k = 0; k < w.size(); ++k) setCaseAt(w, k, false);
        f(w, letterCount(w));
        i = j;
    }
}

bool hasAccent(std::string_view w) {
    for (char c : w)
        if (static_cast<unsigned char>(c) >= 0x80) return true;
    return false;
}

/// Every word of `s` reads as English (a word of the dictionary's translations), so the text
/// looks right in English even unchanged (« Options », « Asterion · Phase 2 », « Level 3 »).
bool readsEnglish(const State::Audit& a, std::string_view s) {
    bool ok = true;
    eachWord(s, [&](const std::string& w, int letters) {
        if (!ok) return;
        if (letters < 2 && !hasAccent(w)) return;  // « s », « x », « D »: units, letters
        if (!a.englishWords.count(w)) ok = false;
    });
    return ok;
}

/// Every word of `s` is English and not French (« Level », « Settings »): an English text.
bool readsOnlyEnglish(const State::Audit& a, std::string_view s) {
    int words = 0;
    bool ok = true;
    eachWord(s, [&](const std::string& w, int letters) {
        if (!ok || letters < 2) return;
        ++words;
        if (hasAccent(w) || !a.englishWords.count(w) || a.frenchWords.count(w)) ok = false;
    });
    return ok && words > 0;
}

void auditNote(State& st, AuditFinding f) {
    if (!st.audit.file.empty()) {
        if (FILE* out = std::fopen(st.audit.file.c_str(), "ab")) {
            Json j = {{"reason", f.reason}, {"text", f.text}, {"shown", f.shown}};
            if (!f.part.empty()) j["part"] = f.part;
            std::string line = j.dump() + "\n";
            std::fwrite(line.data(), 1, line.size(), out);
            std::fclose(out);
        }
    }
    st.audit.found.push_back(std::move(f));
}

/// English: `source` (drawn) became `out`; `full` when every word was known; `parts`: the parts
/// put in slots that stayed untranslated.
void auditEnglish(State& st, std::string_view source, std::string_view french, const std::string& out, bool full,
                  const std::vector<std::string>& parts) {
    State::Audit& a = st.audit;
    const bool again = a.produced.count(french) > 0;  // a translation handed back to the renderer
    if (out != french) a.produced.emplace(out);
    if (letterCount(french) < 2) return;
    AuditFinding f;
    if (again) {
        // Translated twice (a translation changed again by a model or its parts): give the French text.
        // A text that is also a French text of the dictionary (« Explorer » is both) is not told apart.
        if (full && out != french && !st.english.exact(french)) f.reason = "retraduit";
    } else if (!full) {
        if (!readsEnglish(a, french)) f.reason = "absent";
    } else if (out == french) {
        if (!st.english.exact(french) && !readsEnglish(a, french)) f.reason = "identique";
    } else if (!parts.empty() && !readsEnglish(a, out)) {
        // A model applied but what it holds stayed French: the words of the translation that are not English.
        f.reason = "inséré";
        for (size_t i = 0; i < out.size();) {
            if (!letterAt(out, i)) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < out.size())
                if (size_t k = letterAt(out, j)) j += k;
                else break;
            const std::string_view w = std::string_view(out).substr(i, j - i);
            if ((letterCount(w) >= 2 || hasAccent(w)) && !readsEnglish(a, w)) f.part += (f.part.empty() ? "" : " ") + std::string(w);
            i = j;
        }
    }
    if (f.reason.empty()) return;
    f.text = std::string(source);
    f.shown = out;
    auditNote(st, std::move(f));
}

/// French: an English text shown as it is (a translation of the dictionary, or only English words).
void auditFrench(State& st, std::string_view french) {
    State::Audit& a = st.audit;
    if (letterCount(french) < 2 || a.judged.count(french)) return;
    a.judged.emplace(french);
    if (st.english.exact(french)) return;  // a French text of the dictionary
    if (a.englishOnlyTexts.count(french) || readsOnlyEnglish(a, french)) auditNote(st, {"anglais", std::string(french), std::string(french), {}});
}

}  // namespace

void startAudit(const std::string& fileUtf8) {
    State& st = state();
    std::lock_guard lock(st.mutex);
    ensureLoaded(st, Language::English);
    State::Audit& a = st.audit;
    a = State::Audit{};
    a.file = fileUtf8;
    if (!a.file.empty())
        if (FILE* f = std::fopen(a.file.c_str(), "wb")) std::fclose(f);
    st.english.forEach([&](std::string_view fr, std::string_view en) {
        eachWord(en, [&](const std::string& w, int) { a.englishWords.insert(w); });
        if (!fr.empty()) {
            eachWord(fr, [&](const std::string& w, int) { a.frenchWords.insert(w); });
            if (fr != en) a.englishOnlyTexts.emplace(en);
        }
    });
    st.english.forEachModelPiece([&](std::string_view piece) { eachWord(piece, [&](const std::string& w, int) { a.frenchWords.insert(w); }); });
    // French little words kept by names in the translations (« Les Confins », « Cœur de … »): never English.
    // (One literal cut at « _ »: a list of words, not texts to translate.)
    const std::string_view little = "le_la_les_de_du_des_un_une_et_est_en_au_aux_pour_sans_sur_dans_avec_par_ou_que_qui_ne_pas_ce_cette_ces_"
                                    "votre_vos_son_sa_ses_leur_leurs";
    for (size_t i = 0; i < little.size();) {
        const size_t j = std::min(little.find('_', i), little.size());
        a.englishWords.erase(std::string(little.substr(i, j - i)));
        i = j + 1;
    }
    // An English text that is also a French text of the dictionary is no leak.
    for (auto it = a.englishOnlyTexts.begin(); it != a.englishOnlyTexts.end();)
        it = st.english.exact(*it) ? a.englishOnlyTexts.erase(it) : std::next(it);
    st.clearCache();  // every text judged again
    g_audit = true;
}

void stopAudit() {
    State& st = state();
    std::lock_guard lock(st.mutex);
    g_audit = false;
}

bool auditing() {
    State& st = state();
    std::lock_guard lock(st.mutex);
    return g_audit;
}

std::vector<AuditFinding> auditFindings() {
    State& st = state();
    std::lock_guard lock(st.mutex);
    return st.audit.found;
}

const char* code(Language l) { return l == Language::English ? "en" : "fr"; }
const char* nativeName(Language l) { return l == Language::English ? "English" : "Français"; }

std::optional<Language> parse(std::string_view s) {
    std::string l;
    for (char c : s)
        if (c != ' ' && c != '\r' && c != '\n' && c != '\t') l.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
    if (l == "fr" || l == "french" || l == "fra" || l == "français" || l == "francais") return Language::French;
    if (l == "en" || l == "english" || l == "eng" || l == "anglais") return Language::English;
    return std::nullopt;
}

void setLanguage(Language l) {
    State& st = state();
    std::lock_guard lock(st.mutex);
    ensureLoaded(st, l);
    if (st.language == l) return;
    st.language = l;
    st.clearCache();
}

Language language() {
    State& st = state();
    std::lock_guard lock(st.mutex);
    return st.language;
}

void setDictionaryFolder(const std::string& folderUtf8) {
    State& st = state();
    std::lock_guard lock(st.mutex);
    st.folder = folderUtf8;
    st.englishLoaded = false;
    st.english.clear();
    st.clearCache();
}

const Dictionary& dictionary(Language l) {
    static const Dictionary kNone;
    State& st = state();
    std::lock_guard lock(st.mutex);
    ensureLoaded(st, l);
    return l == Language::English ? st.english : kNone;
}

/// Nothing to translate: no letter, and no number written the French way (« 2,5 », « 12 345 »,
/// « 45 % »). Most timers and counters (« 1:23.45 », « 3 / 4 ») are like this.
bool unchangedNumbers(std::string_view s) {
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (letterAt(s, i)) return false;
        if ((c == ',' || c == ' ' || c == 0xC2 || c == 0xE2 || c == '%') && i > 0 && isDigit(s[i - 1])) return false;
    }
    return true;
}

std::string_view display(std::string_view source) {
    // « \x1F contexte|texte »: a word translated according to where it is shown (« Annuler »
    // is Cancel in a dialog, Undo in the editor). The context is never shown.
    std::string_view french = source, context;
    if (!source.empty() && source[0] == '\x1F') {
        size_t bar = source.find('|');
        context = source.substr(1);
        french = bar == std::string_view::npos ? source.substr(1) : source.substr(bar + 1);
    }
    if (french.empty() || g_verbatim > 0) return french;
    State& st = state();
    std::lock_guard lock(st.mutex);
    if (st.language == Language::French) {
        if (g_audit) auditFrench(st, french);
        return french;
    }
    if (context.empty() && unchangedNumbers(french)) return french;
    auto it = st.cache.find(source);
    if (it != st.cache.end()) return it->second;
    std::string key(source), out;
    if (auto old = st.previous.find(source); old != st.previous.end()) {
        out = old->second;  // seen in the previous generation: kept
    } else if (const std::string* v = context.empty() ? nullptr : st.english.exact(context)) {
        out = *v;
        if (g_audit) st.audit.produced.emplace(out);
    } else {
        std::vector<std::string> parts;
        if (g_audit) t_auditParts = &parts;
        bool full = st.english.lookup(french, out);
        t_auditParts = nullptr;
        if (!full) {
            if (out.empty()) out = std::string(french);
            if (letterCount(french) >= 2) noteMissing(st, french);
        }
        if (g_audit) auditEnglish(st, source, french, out, full, parts);
    }
    auto [ins, ok] = st.cache.emplace(std::move(key), std::move(out));
    return ins->second;
}

std::string tr(std::string_view french) { return std::string(display(french)); }

void endFrame() {
    State& st = state();
    std::lock_guard lock(st.mutex);
    // Between two frames (no text handed out is in use any more): when this generation is
    // full, it becomes the previous one, and the texts still shown move back to the new one.
    if (st.cache.size() > 4096) st.previous = std::move(st.cache), st.cache = State::Cache{};
}

Verbatim::Verbatim(bool on) : on_(on) {
    if (on_) ++g_verbatim;
}
Verbatim::~Verbatim() {
    if (on_) --g_verbatim;
}

std::string keep(std::string shown) {
    State& st = state();
    std::lock_guard lock(st.mutex);
    if (st.language != Language::French && !shown.empty()) st.cache.try_emplace(shown, shown);
    return shown;
}
bool verbatim() { return g_verbatim > 0; }

}  // namespace cosmic::i18n
