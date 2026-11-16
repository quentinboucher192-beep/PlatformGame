#pragma once
// ADMINISTRATION › Créations: what an administrator makes on Cosmic Online that every game
// draws without an update. For now the suits (« Combinaisons »): a name and three colours (the
// suit, its accent, its scarf), applied to Nova's sheet at run time the way the catalogue's
// suits are made (game/cosmetics/SuitRecolor.h), with an animated live preview. The list
// (those in service, the archived ones on demand), « Nouvelle combinaison », the editor (name,
// three colour pickers usable with the keyboard, the gamepad and the mouse), « Enregistrer »
// and « Archiver » / « Restaurer »: each change is sent to the server, which checks everything
// again and answers (shown as it is: nothing is ever taken for done before its answer). A suit
// is then given as the reward of a challenge in « Calendrier des défis › Récompenses ».

#include <functional>
#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/cosmetics/SuitRecolor.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class GameServices;

class AdminCreations {
public:
    AdminCreations(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// "new" (automated screenshots): the editor opened on a new suit once the list is read.
    void open(const std::string& arg);
    /// The tab is shown: the list is read once (again with `force`).
    void enter(bool force = false);
    void draw(const Rect& inner);

private:
    struct Draft {
        int id = 0;  // 0: a new suit
        std::string name;
        SuitColors colors;
        std::string updatedAt;
        bool archived = false;
    };

    void load();
    void startNew();
    void select(const Json& creation);
    /// A row chosen (`id`, -1: « Nouvelle combinaison »): at once, or (changes not saved) on a second choice.
    void choose(int id);
    void save();
    void setArchived(bool archived);
    bool dirty() const;
    /// Why « Enregistrer » cannot be used ("" = it can).
    std::string problem() const;
    const Json* byId(int id) const;
    Rgb8& current();

    void drawList(const Rect& r);
    void drawRow(const Json& c, const Rect& row, int index);
    void drawEditor(const Rect& r);
    float drawColorCards(float x, float y, float w);
    float drawChannels(float x, float y, float w);
    /// One channel (0 red, 1 green, 2 blue) of the colour being edited: a bar of its gradient,
    /// focusable (← → by 5, Maj by 1), held with the mouse. Returns true when it changed.
    bool channelBar(int channel, const Rect& r);
    float drawPalette(float x, float y, float w);
    void drawPreview(const Rect& r);
    /// A frame of `clip` of Nova in the suit `c` at `dst`.
    void drawNova(const SuitColors& c, const std::string& clip, float t, const Rect& dst, Color tint = Color::white());

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    Json list_ = Json::array();
    bool loaded_ = false, loading_ = false, saving_ = false;
    std::string loadError_;
    int loadGen_ = 0;
    bool showArchived_ = false;
    bool editing_ = false;  // the editor shows a draft
    Draft draft_, base_;    // base_: as the server last answered (or the defaults of a new one)
    int colorTab_ = 0;      // the colour edited: 0 the suit, 1 the accent, 2 the scarf
    std::string hex_;       // the « #rrggbb » field
    Rgb8 hexShown_;         // the colour the field was last written from
    int hexTab_ = -1;
    int dragChannel_ = -1;  // the channel bar held with the mouse
    int pendingChoice_ = 0; // a row chosen while the draft had changes (0: none; -1: a new suit)
    std::string msg_;
    bool msgError_ = false, msgWarn_ = false;
    std::string wantOpen_;
    bool focusName_ = false;  // a new suit: the name field gets the focus
};

}  // namespace cosmic
