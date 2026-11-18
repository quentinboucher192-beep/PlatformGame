#pragma once
// CLANS › RAIDS (phase 1 of the clan raids « ASTERION, LE DÉVOREUR DE MONDES », maquette
// « Onglet Raids du clan »): the hero card of the next world event (local time and UTC,
// countdown, format, difficulty, clans and players registered, my clan's count, « Voir
// l'événement » and the player's own button), the clan's raids (« Organiser un raid du clan… »
// for the chief and the officers), the invitations to raids in common, the rewards to claim
// (2.68: granted by the server from validated results, « Récupérer »), the clan's history, and
// the panel of the raid notifications (« Tout marquer lu », the player's reminders).
//
// Every rule is the server's (server/src/raids.ts); every read goes through OnlineService
// (never blocking, kept in the cache for offline display). The times are shown in the
// computer's own zone, with its daylight saving time, never stored; the game sends UTC.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/Raids.h"
#include "ui/widgets/AsterionFigure.h"

namespace cosmic {

class GameServices;

class ClanRaids {
public:
    ClanRaids(GameServices& g, std::function<void(const std::string&, bool)> say);

    /// Reads the tab (again with `force`): GET /raids and the raid notifications.
    void enter(bool force = false);
    /// Read again every minute while `shown`.
    void update(float dt, bool shown);
    void draw(const Rect& body);
    /// « Organiser un raid du clan… » is open.
    bool modal() const { return organizing_; }
    void drawModal();
    /// Unread raid notifications (the tab's badge).
    int unread() const { return unread_; }
    bool fromCache() const { return cache_; }

private:
    void load();
    void loadNotes();
    void act(const raids::Action& a, const std::string& eventId);
    void answer(const std::string& eventId, bool accept);
    void openEvent(const std::string& id);
    void markAllRead();
    void openNote(const Json& n);
    void putPrefs(const Json& body);
    void openOrganize();
    /// 2.68: POST /raids/rewards/claim (every reward waiting), then the claimed ones remembered.
    void claimRewards();
    void rememberClaimed(const Json& list);

    void drawHero(const Rect& r, const raids::TabLayout& lay);
    void drawClanRaids(const Rect& r, const raids::TabLayout& lay);
    void drawInvitations(const Rect& r, const raids::TabLayout& lay);
    void drawHistory(const Rect& r, const raids::TabLayout& lay);
    void drawNotes(const Rect& r);
    /// 2.73: « Fiches d'Asterion » (ui/screens/RaidSheetsScene), its measured width.
    void sheetsButton(const std::string& id, const Rect& r);
    float sheetsButtonWidth();

    std::string clanId() const;
    std::string clanRole() const;
    std::string clanTag() const;

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    Json data_;  // GET /raids
    bool loaded_ = false, cache_ = false, acting_ = false, claiming_ = false;
    int gen_ = 0, unread_ = 0;
    int64_t skew_ = 0;
    AsterionFigure titan_;  // the titan of the hero card, its tooltip drawn last
    std::string error_;
    float timer_ = 0.0f;

    Json notes_ = Json::array();  // GET /notifications?filter=raids
    bool notesLoaded_ = false;
    int notesGen_ = 0;

    // « Organiser un raid du clan… »
    bool organizing_ = false, sending_ = false;
    int formatIdx_ = 1, lockIdx_ = 2;
    std::string date_, clock_, formError_;
    float places_ = 8.0f;
};

}  // namespace cosmic
