#pragma once
// 2.81: every mini-film and presentation of the game in one list (ui/screens/FilmCatalog), for ADMINISTRATION › Films
// (all of them, each played whatever its lock, the tutorial replayed without touching anything) and Collection ›
// Galerie › Films (the player's: those unlocked). Each entry: its thumbnail, its title, its kind, its length and where
// players see it. Played by the scenes that play them for the players (BossStoryScene, AsterionFilm, the trailer, the
// tutorial's run), with their normal controls (« Passer », « Quitter »); a film that needs the server's data plays
// with its generic text. Local content: offline as online.

#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"

namespace cosmic {

class GameServices;
class UIContext;

struct FilmEntry {
    std::string id;      // « story:<boss> », « asterion_film », « asterion_victoire », « asterion_apres », « trailer », « tutorial »
    std::string title;   // French (translated when drawn)
    std::string kind;    // « Histoire de giga-boss », « Film d'Asterion », « Bande-annonce », « Didacticiel »
    std::string where;   // where the players see it
    float seconds = 0.0f;   // its length (the tutorial: 0, it is played)
    std::string thumb;      // its thumbnail (assets/…); empty: Bip, drawn (the tutorial)
    bool tutorial = false;  // played, not watched: « Rejouer le didacticiel »
    bool unlocked = true;   // for the player (Collection): may be watched now
    std::string lock;       // why not yet (French)
};

/// Every film and presentation found in the game's data (the gigaboss stories of data/stories, Asterion's three films,
/// the trailer, the tutorial), in that order. `forPlayer`: Collection's list (Asterion's films and the trailer, each
/// with its lock for this player; the stories are in Galerie › Gardiens, the tutorial is not replayed by players).
std::vector<FilmEntry> filmCatalog(GameServices& g, bool forPlayer = false);
/// « 1 min 21 s », « 45 s », « environ 6 min, à jouer ».
std::string filmLengthText(const FilmEntry& e);
/// Plays it (whatever its lock: the caller decides): its scene to push (the tutorial's replay: a scene to switch to).
std::unique_ptr<Scene> makeFilmScene(GameServices& g, const FilmEntry& e);

/// One row of a films' list, measured: its thumbnail (16:9) at the left, the button at the right, between them the
/// title (one line, fitted then shortened), « kind · length » and where players see it (two lines at most). Every
/// rect inside the row, none overlapping.
struct FilmRowLayout {
    Rect thumb, title, meta, where, button;
    float titlePx = 0.0f, metaPx = 0.0f, wherePx = 0.0f;
    std::string titleShown, metaShown, buttonText;
    std::vector<std::string> whereLines;
};
FilmRowLayout layoutFilmRow(UIContext& ui, const Rect& row, const FilmEntry& e, bool forPlayer);
/// The rows' height for this window.
float filmRowHeight(UIContext& ui);

/// The list itself (search, filters, rows, scrolling; keyboard, gamepad, mouse), drawn in `r`. Its button plays the
/// entry (FilmEntry::unlocked for the player). `forPlayer`: Collection's.
class FilmList {
public:
    FilmList(GameServices& g, bool forPlayer) : g_(g), forPlayer_(forPlayer) {}
    void draw(const Rect& r);
    /// Tests: the entries shown with the current search and filter.
    std::vector<FilmEntry> shown() const;
    std::string search;  // what the search field holds
    int filter = 0;      // 0 all, 1 the gigaboss stories, 2 Asterion's films, 3 the others

private:
    GameServices& g_;
    bool forPlayer_ = false;
    float t_ = 0.0f;
};

}  // namespace cosmic
