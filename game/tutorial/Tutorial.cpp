#include "game/tutorial/Tutorial.h"

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/progression/Progression.h"

namespace cosmic::tutorial {

const std::vector<Card>& cards() {
    static const std::vector<Card> v = {
        {"move", "Se déplacer",
         "Bonjour Nova, je suis Bip ! Pour marcher, utilise les touches de direction ou le stick gauche. Va vers la droite : la "
         "Pépinière d'étoiles t'attend.",
         {{"Marcher", {Action::MoveLeft, Action::MoveRight}, {}}},
         Preview::Walk},
        {"jump", "Sauter",
         "Appuie sur Saut pour bondir. Plus tu gardes la touche enfoncée, plus Nova monte haut : un petit appui pour une marche, un "
         "long pour le muret. En l'air, appuie encore une fois : double saut !",
         {{"Sauter", {Action::Jump}, {}}},
         Preview::Jump},
        {"mushroom", "Les champignons rebondissants",
         "Saute sur un champignon : il te renvoie très haut, tout seul. Profites-en pour atteindre la corniche et les éclats qui "
         "flottent en l'air !",
         {{"Sauter", {Action::Jump}, {}}},
         Preview::Bounce},
        {"shards", "Les éclats d'étoile",
         "Ton premier éclat ! Il y en a un peu partout : ramasse-les en passant. Ceux qui flottent très haut s'attrapent en "
         "rebondissant. Aucun n'est obligatoire.",
         {},
         Preview::Shards},
        {"attack", "Attaquer",
         "Chut… ces petites bêtes dorment et ne te feront aucun mal. Approche-toi et frappe-les : chaque coup les fait clignoter. "
         "Quand les trois sont parties, la porte du jardin s'ouvre.",
         {{"Attaquer", {Action::Attack}, {}}},
         Preview::Attack},
        {"power", "Ton premier pouvoir : la Ruée",
         "La tablette t'a donné la Ruée (le Dash) : Nova file d'un coup dans la direction choisie, même en l'air, et brise les murs "
         "de cristal. Elle prend un peu d'énergie, qui se recharge toute seule.",
         {{"Ruée", {}, "dash"}},
         Preview::Dash},
        {"bubbles", "Les bulles porteuses",
         "Pose-toi sur une bulle : elle t'emporte au-dessus du ruisseau, puis éclate au bout de sa route et renaît à sa place. "
         "Si tu tombes, je te ramène aussitôt sur la terre ferme.",
         {{"Sauter", {Action::Jump}, {}}},
         Preview::Bubble},
        {"lantern", "La lanterne",
         "Cette lanterne est un point de passage : si Nova perd toute sa vie, elle repart d'ici aussitôt, avec ses pouvoirs. Ici, "
         "rien n'est compté : ni le temps, ni les chutes.",
         {},
         Preview::Lantern},
        {"boss", "La gardienne câline",
         "Voici Mycélia, la gardienne câline ! Avant chaque attaque, elle se prépare un long moment : c'est le signal pour sauter "
         "ou filer avec la Ruée. Puis frappe-la jusqu'à vider sa barre de vie.",
         {{"Sauter", {Action::Jump}, {}}, {"Attaquer", {Action::Attack}, {}}, {"Ruée", {}, "dash"}},
         Preview::Boss},
    };
    return v;
}

const Card* card(const std::string& id) {
    const auto& v = cards();
    auto it = std::find_if(v.begin(), v.end(), [&](const Card& c) { return c.id == id; });
    return it == v.end() ? nullptr : &*it;
}

std::vector<std::string> levelCards(const LevelData& level) {
    std::vector<std::string> out;
    for (const LevelEvent& e : level.events)
        for (const EventAction& a : e.actions) {
            if (a.type != "tutorial") continue;
            std::string id = jstr(a.params, "card");
            if (!id.empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(std::move(id));
        }
    return out;
}

const std::vector<std::string>& learnt() {
    static const std::vector<std::string> v = {
        "Se déplacer et sauter", "Sauter plus haut, double saut", "Rebondir sur les champignons", "Ramasser les éclats",
        "Attaquer",              "Utiliser un pouvoir : la Ruée", "Voyager en bulle",             "Repartir d'une lanterne",
        "Vaincre un gardien",
    };
    return v;
}

bool due(const Progression& p) { return p.tutorial.empty() && p.completedCount() == 0; }

std::optional<LevelData> loadLevel(std::string* error) {
    const fs::path file = Paths::dataDir() / kLevelFile;
    auto j = FileUtil::loadJson(file);
    if (!j) {
        if (error) *error = "fichier introuvable : " + Paths::toUtf8(file);
        return std::nullopt;
    }
    return LevelData::fromJson(*j, error);
}

}  // namespace cosmic::tutorial
