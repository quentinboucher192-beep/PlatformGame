#pragma once
// 2.64: the screens of the tutorial of the first launch (game/tutorial/Tutorial.h): its welcome
// (start it, or skip it once confirmed), its explanation cards over the waiting run
// (« Compris, je continue »), its end card (« Bravo, Nova ! », the souvenir, « Commencer
// l'aventure »), and Bip, the small robot who floats by Nova and presents every card.

#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"
#include "game/tutorial/Tutorial.h"

namespace cosmic {

class GameServices;
class GameScene;
class Renderer;
class UIContext;
struct PowerKeyMap;

/// Bip, centred on `c`, `size` pixels wide (its body), floating with `time` (display only).
void drawBip(Renderer& r, Vec2 c, float size, float time, float alpha = 1.0f);

/// Where everything of an explanation card goes, measured (nothing overlaps, nothing leaves the
/// card): the scene draws it, the tests check it at every window size and in both languages.
struct TutorialCardLayout {
    Rect panel, bip, kicker, title, preview, text, note, button;
    std::vector<Rect> keys;  // one per key row of the card, in its order
    float kickerPx = 0, titlePx = 0, textPx = 0, notePx = 0, buttonPx = 0, keyPx = 0, glyphH = 0;
    Rect buttonText;  // « Compris, je continue » inside the button (its key glyph to the right)
    float buttonGlyphW = 0;
};
TutorialCardLayout layoutTutorialCard(UIContext& ui, const tutorial::Card& card, const std::string& kicker, const PowerKeyMap* keys);
/// The width of a key row of a card (« Sauter : [Espace] »), keys at `glyphH`, label at `px`.
float tutorialKeyRowWidth(UIContext& ui, const tutorial::KeyRow& row, const PowerKeyMap* keys, float px, float glyphH, bool draw = false,
                          Vec2 at = {});

inline constexpr const char* kTutorialPauseNote = "Le jeu est en pause tant que tu n'as pas lu.";
inline constexpr const char* kTutorialContinue = "Compris, je continue";

/// The end card's layout (the same checks).
struct TutorialEndLayout {
    Rect panel, bip, title, subtitle, learntTitle, souvenirBox, souvenirLabel, souvenirPill, souvenirWhere, reminder, wear, start;
    std::vector<Rect> learnt;
    float titlePx = 0, subtitlePx = 0, itemPx = 0, souvenirPx = 0, reminderPx = 0, buttonPx = 0;
};
TutorialEndLayout layoutTutorialEnd(UIContext& ui);

/// The welcome of the first launch (`confirm`: « Passer le didacticiel ? »), the same checks.
struct TutorialWelcomeLayout {
    Rect panel, bip, titleRect, textRect, start, skip;
    std::string title, text, startText, skipText;
    float titlePx = 0, textPx = 0, buttonPx = 0;
};
TutorialWelcomeLayout layoutTutorialWelcome(UIContext& ui, bool confirm);

std::unique_ptr<Scene> makeTutorialCard(GameServices& g, GameScene& game, const std::string& card);
std::unique_ptr<Scene> makeTutorialEnd(GameServices& g);

}  // namespace cosmic
