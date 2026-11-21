#pragma once
// 2.83: the catalogue of the raids as the game presents them (ADMINISTRATION › Raids: its « Raid » list,
// the raid's poster, its formats and difficulty range). One entry per raid file of data/raids
// (format « cosmic.raid_body ») that has a `catalog` object: its title, name, subtitle and pitch, its
// icon and poster (crops of an existing image of assets/), its formats (id, name, minutes), its
// difficulty range, the part that is its heart and the sheet it opens. Its counts come from the same
// file's `parts`: every part, the gigaboss (a part with a boss) and whether its heart is one of them.
// A raid is added by data: a new file, nothing in the screens. Pure (no screen): tests/test_admin_raids.cpp.

#include <filesystem>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic::raid {

/// An image of assets/ and the part of it shown (`crop` empty: all of it).
struct CatalogArt {
    std::string image;
    Rect crop;
    bool valid() const { return !image.empty(); }
};

struct CatalogFormat {
    std::string id, name;  // "court", « Court »
    int minMinutes = 0, maxMinutes = 0;
};

struct CatalogEntry {
    std::string id;     // "asterion": the server's id of the raid (an event's `raid`)
    std::string file;   // its data file, relative to data/ ("raids/asterion.json")
    std::string title;  // « Asterion, le Dévoreur de mondes »
    std::string name, subtitle, pitch;
    std::string sheet;       // the sheet it opens (makeRaidSheets), empty: none
    std::string heartLabel;  // « le Cœur », empty when it has no heart
    CatalogArt icon, poster;
    std::vector<CatalogFormat> formats;
    int defaultFormat = 0;
    int difficultyMin = 1, difficultyMax = 5, difficultyDefault = 3;
    int parts = 0, gigaboss = 0;
    std::vector<std::string> gigabossParts;  // the names of its parts with a gigaboss, in their order (« CRÂNE »…)
    bool hasHeart = false;
    int order = 0;

    /// The index of a format by its id, -1 when the raid has none of that id.
    int formatIndex(const std::string& formatId) const;
    /// Its format's name (« Légendaire »), else the id itself.
    std::string formatName(const std::string& formatId) const;
};

/// One raid file of data/raids; `valid` false (and `problem`: the missing field) when it is not a raid of the catalogue.
CatalogEntry parseCatalogEntry(const Json& j, bool& valid, std::string* problem = nullptr);
/// Every raid of `dir` (data/raids), in their `order`, then by id.
std::vector<CatalogEntry> loadCatalog(const std::filesystem::path& dir, std::vector<std::string>* problems = nullptr);
/// The game's catalogue (data/raids, read once).
const std::vector<CatalogEntry>& catalog();
/// A raid of the catalogue by its id, nullptr when unknown.
const CatalogEntry* findRaid(const std::string& id);

}  // namespace cosmic::raid
