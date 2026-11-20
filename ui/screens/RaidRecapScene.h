#pragma once
// 2.78.1: CLANS › RAIDS › « Raids terminés »: the « fiche récapitulative » of a finished raid of the clan
// (docs/RAIDS.md §27, maquette « Récapitulatif d'un raid »). At the left, the clan's finished raids (won
// or not) and the titan of the one shown, its fallen parts and the relics carried back; at the right, its
// header over a backdrop of Asterion's fall (the victory film's art, dimmed), seven key figures, « LES
// SEPT PARTIES ET LE CŒUR » (each Gigaboss's animated portrait, the time of its fall and the group that
// downed it), « LES MEILLEURS DU RAID » (the clan's best in each category, with their avatar), « RÉCOMPENSES
// OBTENUES PAR LE CLAN » (each reward drawn with its own texture: badge, title, suit on Nova, frame,
// victory effect), « VOTRE RAID », and the buttons whose target exists. Everything comes from the server
// (server/src/raidRecap.ts, accepted data only); what may still change says « en attente de validation ».
//
// Keyboard, gamepad and mouse: the list's rows, the titan's parts and the buttons are focus stops;
// Échap / B goes back. Every text is placed by layoutRaidRecap(): measured into its rect (smaller, then
// shortened, never under 11.5 pixels), one after the other; tests/test_raid_recap.cpp checks that nothing
// overlaps at 1280 × 720 and 1920 × 1080, in French and in English.

#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Text.h"
#include "game/online/RaidRecap.h"

namespace cosmic {

class GameServices;
class Scene;
class UIContext;

/// One text of the recap: its rect, its text as drawn (fitted, else shortened) and its size.
struct RecapText {
    Rect r;
    std::string text;
    float size = 0.0f;  // unscaled (as UIContext::label takes it)
    float px = 0.0f;    // on screen
    FontFace face = FontFace::Body;
    Align align = Align::Left;
    bool verbatim = false;  // a player's name: never translated
};

/// What the screen offers beside the server's data (the buttons whose target exists).
struct RaidRecapOptions {
    bool victoryFilm = true;   // the film of Asterion's fall can be played (its files are here)
    bool titanMap = true;      // the titan's map of the event can be read by this player
    bool eventSheet = true;    // the sheet of the event (its official results, its world ranking)
    std::string rateLabel;     // 2.77.4: « Noter ce raid » / « Modifier mon avis » when the server lets this player review it
    std::string status;        // a message of the reviews (sent, refused…), shown in the place of the hint for a moment
    int rewardPage = 0;
};

struct RaidRecapLayout {
    Rect screen, back;
    RecapText crumb;
    // The left column: the clan's finished raids, the titan of the one shown.
    Rect list;
    float rowH = 0.0f, rowGap = 0.0f;
    struct Row {
        Rect r, mark;
        RecapText title, line;
    };
    std::vector<Row> rows;  // at scroll 0 (the rows below the list's bottom scroll)
    RecapText listNote;
    Rect titanCard, titanBox;
    std::vector<RecapText> titanCaption;
    // The header and its backdrop.
    Rect header, emblem, aside;
    RecapText title, eventLine, clanLine, pending;
    // 2.77.4: « Note des joueurs », its five stars and « 4,3 · 17 avis » (only when the server sent the reviews).
    RecapText ratingCaption, ratingText;
    Rect ratingStars;
    float ratingStarSize = 0.0f;  // unscaled, as raidreviewsui::drawStars takes it
    // The seven key figures.
    struct Card {
        Rect r, icon;  // icon: empty when the value needs the whole card
        RecapText value, caption;
        std::string id;
        int tone = 4;
    };
    std::vector<Card> cards;
    // « LES SEPT PARTIES ET LE CŒUR ».
    Rect timeline;
    RecapText timelineTitle, timelineSub;
    struct TRow {
        Rect r, portrait, relic, track;
        RecapText name, boss, label, label2;  // label: the time of its fall (« debout »), label2: who downed it
        float mark = -1.0f;  // x of its fall on the track (-1: standing)
        std::string part;
        bool heart = false;
    };
    std::vector<TRow> trows;
    Rect axis;  // the line under the rows
    std::vector<RecapText> ticks;
    std::vector<float> tickX;
    int64_t axisFrom = 0, axisTo = 0;  // UTC seconds of the track's left and right ends
    // « LES MEILLEURS DU RAID ».
    Rect best;
    RecapText bestTitle, bestPending;
    struct BRow {
        Rect r, bar, avatar;
        RecapText label, name, value;
        std::string category;
        int tone = 0;
    };
    std::vector<BRow> brows;
    RecapText bestEmpty;
    // « RÉCOMPENSES OBTENUES PAR LE CLAN ».
    Rect rewards;
    RecapText rewardsTitle, rewardsPage, rewardsNote, rewardsEmpty;
    Rect prev, next;  // the pages' buttons (empty: one page)
    struct Tile {
        Rect r, preview;
        RecapText kind, name, count;
        int index = -1;  // in tiles(): a reward, or -1 for the badges of the gigaboss together
    };
    std::vector<Tile> tiles;
    int rewardPages = 1, rewardPage = 0;
    // « VOTRE RAID ».
    Rect me;
    RecapText meTitle, mePending;
    struct MLine {
        RecapText label, value;
    };
    std::vector<MLine> mlines;
    std::vector<RecapText> meNote;
    // The buttons.
    struct Button {
        std::string id;
        Rect r;
        RecapText text;
    };
    std::vector<Button> buttons;
    RecapText hint;
    /// Every text placed (tests: each in its rect, none over another).
    std::vector<const RecapText*> texts() const;
};

/// The screen's layout for `recap` and `list` at the size of `ui` (its begin() called for this frame).
RaidRecapLayout layoutRaidRecap(UIContext& ui, const raids::Recap& recap, const std::vector<raids::RecapEntry>& list, const RaidRecapOptions& opt);

/// The recap screen of `eventId` (empty: the newest) for `clanId`, pushed over the current screen.
std::unique_ptr<Scene> makeRaidRecap(GameServices& g, const std::string& clanId, const std::string& eventId = {});
/// The same, drawn from given answers of the server (tests and captures: no request is made).
std::unique_ptr<Scene> makeRaidRecapFrom(GameServices& g, const Json& list, const Json& recap);

/// What the screen shows now (tests).
struct RaidRecapView {
    bool valid = false, loaded = false;
    std::string eventId;
    int rows = 0, cards = 0, timelineRows = 0, bestRows = 0, tiles = 0, buttons = 0;
    int portraits = 0;  // gigaboss portraits drawn with their sprite
    int previews = 0;   // rewards drawn with their texture (badge, item)
    bool titan = false, backdrop = false;
};
RaidRecapView raidRecapView(const Scene* s);

}  // namespace cosmic
