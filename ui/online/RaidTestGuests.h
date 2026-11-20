#pragma once
// 2.80 (docs/RAIDS.md §22): the players an administrator invites to their test of Asterion (one zone or
// the whole raid simulated), in one window shared by the test's settings (ADMINISTRATION › Raids › Tester
// Asterion: chosen before the launch, the invitations leave with it) and the test itself (the pause:
// invited, cancelled, changed while it runs):
//
// * at the left, the players invited: name, administrator or player, their group (the whole raid), their
//   « Invincible » box (off by default), the state of their invitation (to send at the launch, waiting with
//   its time left, accepted — playing or away —, declined, expired, cancelled) and its action (remove,
//   cancel, remove from the test, invite again);
// * at the right, the players connected now (GET /admin/raid-test-players: their presence, shown and
//   fresh), searched by name, scrolled, those already invited left out, each with « Inviter » — refused
//   when the group has no simulated player left to replace (a real player takes a simulated player's
//   seat, never one more) or the test already has its 7 invited players.
//
// Everything is real: each change goes to the server at once (once the test exists), whose answer is
// shown; the states are read again every 3 s. Keyboard, gamepad (every control takes the focus, the
// lists scroll to it, Échap / B closes) and mouse. Every text is measured into its room (layout(), checked
// at 1280 × 720 and 1920 × 1080 in French and in English by tests/test_raid_test_guests.cpp).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
class UIContext;
struct RaidTestGuest;

class RaidTestGuests {
public:
    struct Guest {
        std::string id, name;
        bool admin = false;
        int group = 0;
        bool invincible = false;
        /// "" chosen, sent with the launch; "pending", "accepted", "declined", "expired", "cancelled".
        std::string status;
        int64_t expires = 0;  // a pending invitation: its end (UTC seconds)
        bool playing = false, connected = false;
        bool busy = false;    // a request about it is under way
    };
    /// A connected player offered by the picker.
    struct Candidate {
        std::string id, name;
        bool admin = false;
    };

    explicit RaidTestGuests(GameServices& g);
    ~RaidTestGuests();

    /// The groups an invited player may be placed in (their names; one: a test of one zone) and the
    /// seats of a group (the administrator's place included: seats − 1 simulated players to replace).
    void setShape(std::vector<std::string> groups, int seatsPerGroup);
    /// The group a new invited player goes to (the administrator's own group; 0: the first one).
    void setDefaultGroup(int group) { defaultGroup_ = group; }
    /// The players chosen in the test's settings (sent at the launch), and back.
    void setChosen(const std::vector<RaidTestGuest>& list);
    std::vector<RaidTestGuest> chosen() const;
    /// The test exists on the server: every invitation not sent yet leaves now, each change goes at once.
    void attach(const std::string& testId);
    const std::string& testId() const { return testId_; }
    const std::vector<Guest>& guests() const { return guests_; }
    /// The invitations not over (chosen, waiting, accepted): what the test's settings and the pause tell.
    int active() const;
    /// « 2 joueurs invités : 1 en jeu, 1 en attente » (empty when nobody).
    std::string summary() const;

    bool isOpen() const { return open_; }
    void open();
    void close() { open_ = false; }
    /// Reads the states again every 3 s once attached (and the picker's list while open).
    void update(float dt);
    /// The window, over everything (call while open; Échap / B or « Fermer » close it).
    void draw();

    // ---------------------------------------------------------------- layout (measured, tested)
    struct RowLayout {
        Rect row, name, role, state;  // first line: the name, its role's pill, the state's pill
        Rect group, invincible, action;  // second line: the group's choice (empty for a zone), the box, the action
        float actionFont = 0.0f;
    };
    struct PickRowLayout {
        Rect row, name, role, invite;
        float inviteFont = 0.0f;
    };
    struct Layout {
        Rect box, title, close, list, listHead, pick, pickHead, search, pickList, status;
        std::vector<Rect> intro;  // its lines
        std::vector<std::string> introLines;
        float rowH = 0.0f, pickRowH = 0.0f, closeFont = 0.0f;
        bool stacked = false;  // a narrow screen: the picker under the list
    };
    /// The window's places for the screen of `ui`; `attached`: the test exists (the intro differs).
    static Layout layout(UIContext& ui, bool attached);
    /// The places of a guest's row at `row` (`groups`: the group's choice shown), its texts as drawn.
    static RowLayout rowLayout(UIContext& ui, const Rect& row, bool groups, const std::string& role, const std::string& state,
                               const std::string& action, const std::vector<std::string>& groupNames);
    static PickRowLayout pickRowLayout(UIContext& ui, const Rect& row, const std::string& role);
    /// The texts of a guest's state and action (French; translated when drawn).
    static std::string stateText(const Guest& g, int64_t now);
    static std::string actionText(const Guest& g);

private:
    void load();
    void search();
    void invite(const Candidate& c);
    void send(Guest& g);
    void cancel(size_t index);
    void patch(size_t index, bool group);
    int inGroup(int group, const std::string& except) const;
    bool roomFor(int group, const std::string& except, std::string* why) const;
    void merge(const Json& data);
    void say(const std::string& text, bool error);

    GameServices& g_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::vector<std::string> groups_ = {"Groupe 1"};
    int seats_ = 2;
    std::vector<Guest> guests_;
    std::string testId_;
    bool open_ = false;
    float pollIn_ = 0.0f;
    int64_t skew_ = 0;
    // The picker.
    std::string query_, searched_;
    float searchIn_ = 0.0f;
    std::vector<Candidate> candidates_;
    int total_ = 0;
    bool hasMore_ = false, loading_ = false, loaded_ = false;
    int gen_ = 0;
    bool searchMore_ = false;   // the next page of the picker (appended)
    bool focusSearch_ = false;  // the window just opened: the search field takes the focus
    int defaultGroup_ = 0;
    std::string status_;
    bool statusError_ = false;
};

}  // namespace cosmic
