#pragma once
// ADMINISTRATION › Cadeaux (docs/COLLECTION.md « Cadeaux »): an administrator gives rewards of the real catalogue to
// players, in three columns:
//   1. the rewards: the categories (combinaisons, traînées, cadres d'avatar, titres, effets de victoire, badges de
//      profil), a grid of their items drawn by the game's own renderers (several may be chosen), then one of the
//      eleven wraps;
//   2. the players: a search, « En ligne », « Tous », the administrator's clan, « Choisis »; each with the rewards
//      chosen it already owns (« possède déjà… », as the SERVER counts it: they are skipped);
//   3. the message (140 characters), with two rewards or more « N cadeaux séparés » or « 1 colis de N cadeaux »
//      (one parcel per player, what it holds hidden until opened), the summary « N récompenses × M joueurs =
//      K cadeaux (or colis) · Ignoré : … », « Aperçu de l'ouverture » (the real scene) and « Envoyer K cadeaux »
//      after a confirmation; then « Derniers envois »: each send (« Colis (N) » for parcels) with its gifts
//      opened, and its unopened gifts or parcels withdrawn on demand.
// The server checks the role on every request and writes each send and each withdrawal to the Journal.

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/Gifts.h"
#include "game/online/OnlineTypes.h"
#include "ui/screens/GiftsUi.h"

namespace cosmic {

class GameServices;
class OnlineUi;

class AdminGifts {
public:
    AdminGifts(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);
    ~AdminGifts();

    /// The tab is shown: the players and the sends are read (again with `force`).
    void enter(bool force = false);
    void draw(const Rect& inner);

    /// What may be given in a category (the tab's grid, in the catalogue's order): the items of the Collection
    /// (never a category's default, nor a souvenir of this computer), or the profile badges the server grants.
    static std::vector<gifts::Reward> catalogue(GameServices& g, const std::string& kind);

private:
    struct Chosen {
        std::string id, username;
    };
    void loadPlayers(bool more);
    void loadChosenOwned();
    void loadHistory();
    void send();
    void cancelSend(int64_t id, bool parcels);
    /// One parcel per player is chosen (and there are at least two rewards to put in it).
    bool bundled() const { return bundle_ && rewards_.size() >= 2; }
    std::string rewardsQuery() const;
    bool isChosen(const std::string& id) const;
    void toggle(const Json& player);

    void drawRewards(const Rect& col);
    void drawPlayers(const Rect& col);
    void drawSend(const Rect& col);
    /// The rewards a player owns (or has waiting) among those chosen (`pending`: waiting in a gift).
    std::vector<std::string> ownedOf(const std::string& playerId, bool pending) const;

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::unique_ptr<OnlineUi> online_;
    giftsui::AdminLayout L_;

    // 1. the rewards
    int kind_ = 0;
    std::vector<gifts::Reward> rewards_;
    std::string wrap_ = "or";

    // 2. the players
    int filter_ = 0;  // 0 online, 1 all, 2 clan, 3 chosen
    std::string query_, lastQuery_, listRewards_;
    float queryT_ = 0.0f;
    bool queryDirty_ = false;
    Json players_ = Json::array();
    Json clan_;
    bool loaded_ = false, busy_ = false, more_ = false;
    int gen_ = 0;
    std::vector<Chosen> chosen_;
    std::map<std::string, Json> owned_;  // player id → {owned: [...], pending: [...]} (the chosen ones, and those listed)
    std::string ownedKey_;               // the rewards and players the chosen ones' tags were read for
    int ownedGen_ = 0;

    // 3. the message and the send
    std::string message_;
    bool bundle_ = false;  // « 1 colis de N cadeaux » (with two rewards or more) rather than N separate gifts
    bool confirm_ = false, sending_ = false;
    Json history_ = Json::array();
    bool historyLoaded_ = false, historyMore_ = false;
    int64_t cancelAsk_ = 0;
};

}  // namespace cosmic
