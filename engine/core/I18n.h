#pragma once

// Languages of the game: French (the source of every text) or English.
//
// Nothing in the code is written for a language: texts stay in French in the code and in
// the data, and the text renderer (TextRenderer::draw / measure…) shows each one through
// i18n::display(), which looks it up in the dictionary of the language in use
// (data/i18n/en.json, French → English). The measure and the drawing of a text therefore
// always use the same translated words. Texts built from pieces are matched against models
// with slots (« {0} vaincu sans dégât ! » → « {0} defeated without taking damage! »), the
// inserted parts being translated themselves. A text without translation is shown in French
// (never empty). Display only: the language never touches the simulation or the replays.
//
// The language is a setting (Paramètres › Jeu › « Langue / Language »), applied at once;
// the Windows installer passes the language chosen during the installation (language.ini
// next to the executable, read at start: see applyInstallerLanguage in GameServices).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic {

enum class Language { French, English };

namespace i18n {

/// "fr" / "en" (settings, files).
const char* code(Language l);
/// Name of the language in that language ("Français", "English"): never translated.
const char* nativeName(Language l);
/// "fr", "en", "french", "english", "français", "anglais"… (case-insensitive).
std::optional<Language> parse(std::string_view s);

/// French → other language. Exact texts first, then models with slots ({0}, {1}…; a value
/// may choose a word by the number in a slot: « {0|level|levels} »), then the parts of a
/// text (lines, pieces between « · », « — »…, the words around a known text).
class Dictionary {
public:
    void clear();
    /// One entry. A key containing {0}, {1}… is a model; its value uses the same slots.
    void add(std::string_view french, std::string_view translated);
    /// { "strings": { "<français>": "<translation>", … } }. Returns the number of entries.
    size_t load(const Json& j);
    size_t size() const { return exact_.size() + models_.size(); }
    bool empty() const { return exact_.empty() && models_.empty(); }
    /// The exact entry (no model, no part), or nullptr.
    const std::string* exact(std::string_view french) const;
    /// `french` as shown in the translated language; unchanged when nothing applies.
    std::string translate(std::string_view french) const;
    /// True when every word of `french` is known. `out`: the translation, partial when only
    /// some pieces are known, empty when none is.
    bool lookup(std::string_view french, std::string& out) const;
    /// English number formats inside a text without words: « 2,45 » → « 2.45 »,
    /// « 12 345 » → « 12,345 ».
    static std::string localizeNumbers(std::string_view s);
    /// Every entry, exact texts and models (`f(french, translated)`): checks (the i18n audit).
    template <class F>
    void forEach(F&& f) const {
        for (const auto& [k, v] : exact_) f(std::string_view(k), std::string_view(v));
        for (const auto& m : models_) f(std::string_view{}, std::string_view(m.value));
    }
    /// The model keys' literal pieces (`f(piece)`): checks.
    template <class F>
    void forEachModelPiece(F&& f) const {
        for (const auto& m : models_)
            for (const auto& l : m.literals) f(std::string_view(l));
    }

private:
    struct Model {
        std::vector<std::string> literals;  // slots + 1 pieces of text around the slots
        std::vector<int> slots;             // slot number of each capture, in order
        std::string value;
        size_t literalLength = 0;
        bool generic = false;               // no letter outside the slots (« {0} : {1} »)
    };
    struct Hash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    using Map = std::unordered_map<std::string, std::string, Hash, std::equal_to<>>;

    bool translateInto(std::string_view s, std::string& out, int depth) const;
    bool byModel(std::string_view s, std::string& out, int depth, bool generic) const;
    bool byParts(std::string_view s, std::string& out, int depth) const;
    bool byCase(std::string_view s, std::string& out, int depth) const;
    bool byWords(std::string_view s, std::string& out) const;
    std::string fill(const Model& m, const std::vector<std::string_view>& caps, int depth) const;
    std::string piece(std::string_view s, int depth) const;

    Map exact_;
    std::vector<Model> models_;
    mutable Map upper_;  // UPPERCASE key → key (built on first use)
    mutable bool upperBuilt_ = false;
};

/// The language in use (French by default). Loads its dictionary the first time.
void setLanguage(Language l);
Language language();
/// Folder of the dictionaries (default: <data>/i18n).
void setDictionaryFolder(const std::string& folderUtf8);
/// The dictionary of `l` (loaded if needed; empty for French).
const Dictionary& dictionary(Language l);

/// `french` as displayed in the language in use (French: unchanged). Results are cached.
std::string tr(std::string_view french);
/// Same, without a copy: valid until the end of the frame (endFrame).
std::string_view display(std::string_view french);
/// Called once per frame by the text renderer (keeps the cache bounded).
void endFrame();

/// While one exists (on this thread), texts are displayed exactly as given: the players'
/// own words (pseudonyms, comments, names of their levels, text being typed).
class Verbatim {
public:
    explicit Verbatim(bool on = true);
    ~Verbatim();
    Verbatim(const Verbatim&) = delete;
    Verbatim& operator=(const Verbatim&) = delete;

private:
    bool on_;
};
bool verbatim();
/// Marks `shown` as already displayed text (e.g. a translated text shortened with « … »):
/// drawn as it is. Returns it.
std::string keep(std::string shown);

/// 2.75.4: checking that no text is left in the other language (--i18n-audit <file>, tests).
/// While on, each text shown (once) is judged. In English: « absent » (not every word known),
/// « identique » (unchanged although some of its words are not English), « inséré » (a model
/// matched but a part put in its slot stayed French). In French: « anglais » (an English
/// translation of the dictionary shown as it is). The players' own words (Verbatim) are never
/// judged. Off by default: nothing is checked, nothing costs.
struct AuditFinding {
    std::string reason;  // absent, identique, inséré, anglais
    std::string text;    // as handed to the text renderer
    std::string shown;   // as displayed
    std::string part;    // inséré: the part left untranslated
};
/// Starts (clears what was found). `fileUtf8`: each finding is also appended to it, one JSON
/// object per line (the file is emptied first); "" = kept in memory only.
void startAudit(const std::string& fileUtf8 = {});
void stopAudit();
bool auditing();
std::vector<AuditFinding> auditFindings();

}  // namespace i18n
}  // namespace cosmic
