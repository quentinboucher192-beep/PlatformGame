// Languages: the dictionary (exact texts, models with slots, inserted parts, plural, numbers),
// its coverage of every text the game shows (tools/i18n/catalog.json, written by
// tools/i18n/extract.py), the language setting and the installer's choice.

#include <regex>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
i18n::Dictionary sample() {
    i18n::Dictionary d;
    d.add("Jouer", "Play");
    d.add("Sentinelle Prime", "Prime Sentinel");
    d.add("Or", "Gold");
    d.add("Niveau {0}", "Level {0}");
    d.add("{0} vaincu sans dégât !", "{0} defeated without taking damage!");
    d.add("{0} niveaux", "{0} {0|level|levels}");
    d.add("{1} sur {0}", "{0} out of {1}");
    d.add("{0} < {1}", "{0} < {1}");
    d.add("Pause", "Pause");
    d.add("Paramètres", "Settings");
    d.add("Connexion impossible : {0}", "Cannot connect: {0}");
    d.add("délai dépassé", "timed out");
    return d;
}

/// Slots of a text: {0}, {1|a|b}… → {"0", "1"}.
std::set<std::string> slots(const std::string& s) {
    std::set<std::string> out;
    static const std::regex re(R"(\{(\d+)(\|[^{}]*)?\})");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it) out.insert((*it)[1].str());
    return out;
}
}  // namespace

TEST_CASE("i18n: exact texts, models and inserted parts translated themselves") {
    i18n::Dictionary d = sample();
    CHECK(d.translate("Jouer") == "Play");
    CHECK(d.translate("Niveau 3") == "Level 3");
    CHECK(d.translate("Sentinelle Prime vaincu sans dégât !") == "Prime Sentinel defeated without taking damage!");
    // Slots may change places; inserted words are translated, unknown names kept.
    CHECK(d.translate("12 sur 30") == "30 out of 12");
    CHECK(d.translate("Bob vaincu sans dégât !") == "Bob defeated without taking damage!");
    CHECK(d.translate("Connexion impossible : délai dépassé") == "Cannot connect: timed out");
    // A model with no word of its own only applies when what it holds is known.
    CHECK(d.translate("Or < 1:23.45") == "Gold < 1:23.45");
    CHECK(d.translate("Mon niveau < préféré") == "Mon niveau < préféré");
    // Unknown text: shown in French, never empty.
    CHECK(d.translate("Texte inconnu") == "Texte inconnu");
    CHECK(d.translate("") == "");
}

TEST_CASE("i18n: plural words, numbers, case, pieces around a known text") {
    i18n::Dictionary d = sample();
    CHECK(d.translate("1 niveaux") == "1 level");
    CHECK(d.translate("0 niveaux") == "0 levels");
    CHECK(d.translate("14 niveaux") == "14 levels");
    // Numbers in English: decimal point, groups of thousands, no space before %.
    CHECK(d.translate("2,45") == "2.45");
    CHECK(d.translate("12 345 / 100 000") == "12,345 / 100,000");
    CHECK(d.translate("45 %") == "45%");
    CHECK(d.translate("Niveau 2,5") == "Level 2.5");
    // Capitals, first letter, signs around, separators, lines.
    CHECK(d.translate("PARAMÈTRES") == "SETTINGS");
    CHECK(d.translate("paramètres") == "settings");
    CHECK(d.translate("• Jouer :") == "• Play:");
    CHECK(d.translate("« Paramètres »") == "\"Settings\"");
    CHECK(d.translate("Jouer  ·  Paramètres  ·  Niveau 4") == "Play  ·  Settings  ·  Level 4");
    CHECK(d.translate("Jouer\nParamètres") == "Play\nSettings");
    // Last resort: known words between signs and numbers.
    CHECK(d.translate("Or < 3:20") == "Gold < 3:20");
    CHECK(d.translate("Paramètres : 3 / 4") == "Settings: 3 / 4");
    std::string out;
    CHECK(!d.lookup("Jouer · Texte inconnu", out));
    CHECK(out == "Play · Texte inconnu");  // partial: the known piece is translated
}

TEST_CASE("i18n: display in the language in use, cached, player texts verbatim") {
    i18n::setLanguage(Language::French);
    CHECK(i18n::tr("Jouer") == "Jouer");
    i18n::setLanguage(Language::English);
    CHECK(i18n::language() == Language::English);
    CHECK(i18n::tr("Jouer") == "Play");
    CHECK(i18n::tr("Play") == "Play");  // already shown text is never translated again
    {
        i18n::Verbatim mine;  // a pseudonym, a comment…
        CHECK(i18n::tr("Jouer") == "Jouer");
    }
    CHECK(i18n::keep("Pla…") == "Pla…");
    CHECK(i18n::tr("Pla…") == "Pla…");
    i18n::setLanguage(Language::French);
    CHECK(i18n::tr("Jouer") == "Jouer");
    CHECK(i18n::parse("english") == Language::English);
    CHECK(i18n::parse("FR") == Language::French);
    CHECK(!i18n::parse("klingon"));
}

TEST_CASE("i18n: every text of the catalogue has an English translation with the same slots") {
    auto catalog = FileUtil::loadJson("tools/i18n/catalog.json");
    auto dict = FileUtil::loadJson("data/i18n/en.json");
    CHECK(catalog && dict);
    const Json& strings = (*dict)["strings"];
    CHECK(strings.is_object());
    int missing = 0, wrongSlots = 0, total = 0;
    for (const auto& e : (*catalog)["strings"]) {
        std::string fr = e["fr"].get<std::string>();
        ++total;
        auto it = strings.find(fr);
        if (it == strings.end() || !it->is_string() || it->get<std::string>().empty()) {
            if (++missing <= 20) std::cerr << "  sans traduction : " << fr << "\n";
            continue;
        }
        if (slots(fr) != slots(it->get<std::string>())) {
            if (++wrongSlots <= 20) std::cerr << "  emplacements différents : " << fr << " -> " << it->get<std::string>() << "\n";
        }
    }
    if (missing) std::cerr << "  " << missing << " / " << total << " textes sans traduction (python3 tools/i18n/extract.py)\n";
    CHECK(total > 1000);
    CHECK(missing == 0);
    CHECK(wrongSlots == 0);
}

TEST_CASE("i18n: an English text is never itself a French text translated differently") {
    // The renderer may receive a text already translated (measured, then drawn): showing
    // it again must not change it.
    auto dict = FileUtil::loadJson("data/i18n/en.json");
    CHECK(dict);
    const Json& strings = (*dict)["strings"];
    int conflicts = 0;
    for (auto it = strings.begin(); it != strings.end(); ++it) {
        if (!it.value().is_string()) continue;
        std::string en = it.value().get<std::string>();
        if (en == it.key()) continue;
        auto other = strings.find(en);
        if (other != strings.end() && other->is_string() && other->get<std::string>() != en) {
            if (++conflicts <= 20) std::cerr << "  « " << it.key() << " » -> « " << en << " » -> « " << other->get<std::string>() << " »\n";
        }
    }
    CHECK(conflicts == 0);
    // The whole dictionary loads, and its models are usable.
    i18n::Dictionary d;
    CHECK(d.load(*dict) == strings.size());
    CHECK(d.translate("Jouer") != "Jouer");
}

TEST_CASE("i18n: the language is a setting; the installer's choice applies once") {
    Settings s;
    CHECK(settingsLanguage(s) == Language::French);
    s.language = "en";
    Settings back = Settings::fromJson(s.toJson());
    CHECK(back.language == "en");
    CHECK(settingsLanguage(back) == Language::English);

    fs::path dir = fs::temp_directory_path() / "cosmic_i18n_test";
    fs::create_directories(dir);
    fs::path ini = dir / "language.ini";
    FileUtil::writeTextAtomic(ini, "[Language]\r\nLanguage=english\r\n");
    Settings fresh;
    CHECK(applyInstallerLanguage(fresh, ini));
    CHECK(settingsLanguage(fresh) == Language::English);
    // The player changes it in the game: the same installation does not change it back.
    fresh.language = "fr";
    CHECK(!applyInstallerLanguage(fresh, ini));
    CHECK(settingsLanguage(fresh) == Language::French);
    // Reinstalled in French: applied.
    FileUtil::writeTextAtomic(ini, "\xEF\xBB\xBF[Language]\nLanguage=french\n");
    fresh.language = "en";
    CHECK(applyInstallerLanguage(fresh, ini));
    CHECK(settingsLanguage(fresh) == Language::French);
    // No file (Linux, portable build): nothing changes.
    Settings none;
    CHECK(!applyInstallerLanguage(none, dir / "absent.ini"));
    CHECK(none.language.empty());
    fs::remove_all(dir);
}
