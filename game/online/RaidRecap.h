#pragma once
// 2.78.1: the « fiche récapitulative » of a finished clan raid (CLANS › RAIDS › Raids terminés,
// docs/RAIDS.md §24), as the server sends it (server/src/raidRecap.ts): the clan's finished raids
// (GET /clans/{id}/raids) and one of them whole (GET /clans/{id}/raids/{event}/recap). Every figure
// is the server's, from what its consensus accepted; nothing here computes a result. The texts are
// French (translated where they are drawn), the times shown in the computer's own zone.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic::raids {

/// A finished raid of the clan (the left list of the recap).
struct RecapEntry {
    bool valid = false;
    std::string id, kind = "clan", format = "standard";
    bool catchup = false, heartFallen = false, final = false;
    int64_t startsAt = 0, endsAt = 0, endAt = 0;
    int durationS = 0;
    int partsDown = 0, partsTotal = 7;
};
std::vector<RecapEntry> parseRecapList(const Json& data);
/// « Asterion · 20 nov. » (the start, in the computer's zone).
std::string recapEntryTitle(const RecapEntry& e);
/// « Asterion vaincu · 2 h 47 min », « 3 / 7 parties · temps écoulé ».
std::string recapEntryLine(const RecapEntry& e);

/// « 2 h 47 min », « 47 min », « moins d'une minute ».
std::string durationText(int64_t seconds);

/// The group whose validated report downed a part (its instance).
struct RecapGroup {
    bool valid = false;
    std::string clanTag;
    int group = 0;
    bool reinforcement = false;  // a group of reinforcement (it took a wiped zone again)
    bool mine = false;           // a group of the clan
    bool me = false;             // a group I played in
};

/// A part of the titan (the seven in the order of its body, then the heart).
struct RecapPart {
    std::string part, name, boss;  // « crane », the server's names (« CRÂNE », « Skullmaw »)
    bool fallen = false;
    int64_t at = 0;  // UTC seconds of its validated fall (0: standing)
    RecapGroup by;
    bool relic = false;  // the relic of its branch carried back (validated)
    int phase = 0;       // the highest phase reached against its gigaboss (4: fallen, 0: never fought)
};

/// The heart: its fall, the groups that struck it, the clan's part of its shared health.
struct RecapHeart : RecapPart {
    bool open = false;
    int taken = 0;  // ten-thousandths of its health taken (validated parts)
    int groups = 0, clanGroups = 0;
    double clanShare = 0.0;  // the clan's instances' part of its whole health
};

/// The best of the clan in a category of « Les meilleurs du raid ».
struct RecapBest {
    std::string category;  // gigaboss_damage, heart_damage, support, revives, mechanisms, objectives, fewest_deaths
    bool followed = false;  // the server counts it (false: not followed yet, never a guess)
    bool has = false;       // someone of the clan has a value in it
    std::string playerId, username, avatar;
    bool me = false;
    int value = 0;
    double share = 0.0;  // of the clan's total (damage)
};

/// A reward the server granted to players of the clan for this raid.
struct RecapReward {
    std::string reward, name, kind, ref;  // kind "badge" (an achievement) or "item" (a cosmetic)
    int players = 0;
    bool mine = false, gigaboss = false;
};

/// One of my groups in this raid.
struct RecapMyGroup {
    std::string clanTag, part;
    int group = 0;
    bool reinforcement = false, wiped = false, mine = false;
};

struct Recap {
    bool valid = false;
    std::string clanId, clanTag, clanName, emblemShape, emblemPrimary, emblemSecondary;
    std::string eventId, kind = "clan", format = "standard";
    bool catchup = false, heartFallen = false, final = false;
    bool member = true;  // the viewer is a member of the clan (an administrator may read it without being one)
    int64_t startsAt = 0, endsAt = 0, endAt = 0;
    int durationS = 0;
    int pendingTables = 0, pendingObjectives = 0;
    int worldRank = 0, worldClans = 0;  // a world event's official ranking (0: not ranked)
    int registered = 0, players = 0, groups = 0, reinforcements = 0;
    int partsDown = 0, partsTotal = 7, zonesDone = 0, zonesTotal = 56, zonesByClan = 0, relics = 0, relicsTotal = 7, revives = 0;
    int deaths = -1;  // -1: not followed by the server
    int wiped = 0, retaken = 0;
    std::vector<RecapPart> parts;
    RecapHeart heart;
    std::vector<RecapBest> best;  // in the order of recapCategories()
    std::vector<RecapReward> rewards;
    std::vector<std::string> titles;  // « Briseurs d'étoile », « Dévoreurs dévorés · 2ᵉ »
    struct Me {
        bool played = false;
        std::vector<RecapMyGroup> groups;
        std::vector<std::string> parts;  // parts my groups downed
        int zones = 0, revives = 0;
        int deaths = -1;  // -1: not followed by the server
        double heartShare = 0.0;
        int heartRank = 0;
    } me;
};
Recap parseRecap(const Json& data);

/// Whether figures may still change (the event not final, or tables waiting for their majority).
bool recapPending(const Recap& r);
/// « ASTERION EST TOMBÉ » or « RAID TERMINÉ ».
std::string recapTitle(const Recap& r);
/// « Événement du jeudi 20 novembre · Raid standard · 21:00 → 23:47 · durée 2 h 47 min ».
std::string recapEventLine(const Recap& r);
/// « Clan [NOVA] · 23 joueurs · 3 groupes · 2 renforts ».
std::string recapClanLine(const Recap& r);
/// « Fiche provisoire : 2 tableaux en attente de validation », « » when final.
std::string recapPendingText(const Recap& r);

/// One key figure: its value, its caption and its colour family (0 green, 1 heart red, 2 sky, 3 gold, 4 grey).
struct RecapFigure {
    std::string id, value, caption;
    int tone = 4;
};
std::vector<RecapFigure> recapFigures(const Recap& r);

/// The categories of « Les meilleurs du raid », in order, and their labels.
struct RecapCategory {
    std::string id, label;
    int tone = 0;  // as RecapFigure
};
const std::vector<RecapCategory>& recapCategories();
/// « 18 % », « 2 340 », « 0 » ; « — » when nobody has one.
std::string recapBestValue(const RecapBest& b);

/// The time of a fall on the timeline and who downed it: « 21:58 · Groupe 1 », « 22:52 · Groupe 2 (renfort) »,
/// « 22:24 · [AUBE] groupe 3 », the heart « 23:47 · 12 groupes »; a part standing: « debout · phase 2 / 3 ».
std::string recapPartLabel(const RecapPart& p, bool heart, int heartGroups);
/// The lines of « VOTRE RAID »: label and value.
std::vector<std::pair<std::string, std::string>> recapMyLines(const Recap& r, const std::vector<std::pair<std::string, std::string>>& partNames);

}  // namespace cosmic::raids
