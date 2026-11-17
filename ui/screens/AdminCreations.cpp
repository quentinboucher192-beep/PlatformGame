#include "ui/screens/AdminCreations.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

#include "engine/input/InputManager.h"
#include "game/GameServices.h"
#include "game/online/OnlineSession.h"
#include "ui/screens/ChallengeUi.h"

namespace cosmic {

using challenge_ui::fitLabel;
using challenge_ui::fz;

namespace {

/// A name: at most this many characters (the server checks it again).
constexpr size_t kNameMax = 32;

/// The colours of a new suit: those of Nova's own suit (white, cyan visor, red scarf), to start from.
const SuitColors kDefaultColors{{220, 230, 240}, {80, 220, 255}, {230, 60, 60}};

/// Quick choices of the palette (any colour remains possible with the bars or the code).
const Rgb8 kPalette[] = {{240, 244, 250}, {180, 190, 210}, {80, 90, 120},  {35, 35, 45},   {220, 40, 60},  {255, 150, 50},
                         {255, 215, 90},  {150, 230, 80},  {60, 180, 100}, {50, 200, 180}, {90, 230, 255}, {60, 120, 240},
                         {150, 80, 255},  {240, 120, 200}, {140, 90, 70},  {140, 20, 48}};
constexpr int kPaletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

const char* const kChannelNames[3] = {"Rouge", "Vert", "Bleu"};

/// The words of each kind (the order of the selector: suits, trails, frames, victory effects).
struct KindWords {
    const char* id;
    const char* tab;          // the selector
    const char* title;        // above the list
    const char* add;          // « + Nouvelle combinaison »
    const char* newTitle;     // the editor's title for a new one
    const char* editTitle;    // ... for one being edited
    const char* placeholder;  // the name field
    const char* info;         // what they are for, under the list's buttons
    const char* none;         // nothing created yet
    const char* intro;        // the editor while nothing is open
    const char* styleLabel;   // « Forme », « Style », « Effet » ("" for a suit)
    const char* colorNames[3];
    bool feminine;            // « créée », « archivée »...
};
const KindWords kKinds[4] = {
    {"suit", "Combinaisons", "Combinaisons", "+ Nouvelle combinaison", "Nouvelle combinaison", "Modifier la combinaison",
     "Nom de la combinaison (32 caractères au plus)",
     "Dessinées par tous les jeux, sans mise à jour. À donner en récompense : Calendrier des défis › Récompenses.",
     "Aucune combinaison créée pour l'instant : « Nouvelle combinaison » pour commencer.",
     "Choisissez une combinaison dans la liste pour la modifier, ou « Nouvelle combinaison » pour en créer une : un nom et trois couleurs, "
     "appliquées à Nova comme pour les combinaisons du jeu. Elle se donne ensuite en récompense d'un défi (Calendrier des défis › "
     "Récompenses) : ceux qui la gagnent la portent, et tous les jeux la dessinent sans mise à jour.",
     "", {"Combinaison", "Accent", "Écharpe"}, true},
    {"trail", "Traînées", "Traînées", "+ Nouvelle traînée", "Nouvelle traînée", "Modifier la traînée", "Nom de la traînée (32 caractères au plus)",
     "Derrière Nova quand elle court ou fonce, et derrière ses fantômes. À donner en récompense : Calendrier des défis › Récompenses.",
     "Aucune traînée créée pour l'instant : « Nouvelle traînée » pour commencer.",
     "Choisissez une traînée dans la liste pour la modifier, ou « Nouvelle traînée » pour en créer une : un nom, une forme que le jeu sait "
     "dessiner, deux couleurs (près de Nova, puis au bout), sa largeur, sa durée et ses étincelles. Elle se donne ensuite en récompense "
     "d'un défi : ceux qui la gagnent l'équipent, et tous les jeux la dessinent sans mise à jour, derrière eux et derrière leurs fantômes.",
     "Forme", {"Début", "Fin", ""}, true},
    {"frame", "Cadres", "Cadres d'avatar", "+ Nouveau cadre", "Nouveau cadre", "Modifier le cadre", "Nom du cadre (32 caractères au plus)",
     "Autour de l'avatar, sur le profil et dans les classements. À donner en récompense : Calendrier des défis › Récompenses.",
     "Aucun cadre créé pour l'instant : « Nouveau cadre » pour commencer.",
     "Choisissez un cadre dans la liste pour le modifier, ou « Nouveau cadre » pour en créer un : un nom, un style que le jeu sait dessiner "
     "et deux couleurs (le bord, la lueur). Il se donne ensuite en récompense d'un défi : ceux qui le gagnent l'équipent, et tous les jeux "
     "le dessinent sans mise à jour, sur leur profil et dans les classements.",
     "Style", {"Bord", "Lueur", ""}, false},
    {"victory", "Effets de victoire", "Effets de victoire", "+ Nouvel effet", "Nouvel effet de victoire", "Modifier l'effet de victoire",
     "Nom de l'effet (32 caractères au plus)", "Joués à la sortie du niveau. À donner en récompense : Calendrier des défis › Récompenses.",
     "Aucun effet de victoire créé pour l'instant : « Nouvel effet » pour commencer.",
     "Choisissez un effet dans la liste pour le modifier, ou « Nouvel effet » pour en créer un : un nom, un effet que le jeu sait jouer et "
     "deux couleurs. Il se donne ensuite en récompense d'un défi : ceux qui le gagnent l'équipent, et tous les jeux le jouent sans mise à "
     "jour à la sortie de leurs niveaux.",
     "Effet", {"Principale", "Secondaire", ""}, false},
};
constexpr int kKindCount = 4;

int kindIndex(const std::string& id) {
    for (int i = 0; i < kKindCount; ++i)
        if (id == kKinds[i].id) return i;
    return -1;
}

/// The poses of the suit's preview, one after the other.
struct Pose {
    const char* clip;
    const char* name;
};
const Pose kPoses[] = {{"idle", "repos"}, {"run", "course"}, {"dash", "ruée"}, {"glide", "vol plané"}, {"victory", "victoire"}};
constexpr float kPoseSeconds = 2.6f;

Color colorOf(Rgb8 c) { return {c.r, c.g, c.b, 255}; }

/// Characters (code points) of a UTF-8 text.
size_t codepoints(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

/// The first `n` characters of a UTF-8 text.
std::string firstCodepoints(const std::string& s, size_t n) {
    size_t i = 0, count = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (count == n) break;
            ++count;
        }
        ++i;
    }
    return s.substr(0, i);
}

/// Without the spaces at its ends, the inner ones collapsed (as the server stores it).
std::string cleaned(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

/// "16/11/2026" (local date of this computer).
std::string dayOf(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return "—";
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[48];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d", m.tm_mday, m.tm_mon + 1, m.tm_year + 1900);
    return b;
}

/// The French label of a field of the server's validation.
std::string fieldLabel(const std::string& f) {
    if (f == "name") return "Nom";
    if (f == "colors.suit") return "Couleur de la combinaison";
    if (f == "colors.accent") return "Couleur d'accent";
    if (f == "colors.scarf") return "Couleur de l'écharpe";
    if (f == "colors.start") return "Couleur du début";
    if (f == "colors.end") return "Couleur de la fin";
    if (f == "colors.border") return "Couleur du bord";
    if (f == "colors.glow") return "Couleur de la lueur";
    if (f == "colors.main") return "Couleur principale";
    if (f == "colors.second") return "Couleur secondaire";
    if (f.rfind("colors", 0) == 0) return "Couleurs";
    if (f == "style") return "Style";
    if (f == "width") return "Largeur";
    if (f == "life_ms") return "Durée";
    if (f == "particles") return "Étincelles";
    if (f == "archived") return "Archivage";
    return "Données";
}

/// What the server said, in a sentence.
std::string errorText(const ApiResult& r) {
    if (r.networkFailure()) return "Connexion impossible : rien n'a changé sur le serveur.";
    if (r.code == "VALIDATION_ERROR" && r.details.is_array() && !r.details.empty() && r.details[0].is_object())
        return fieldLabel(jstr(r.details[0], "field")) + " : " + jstr(r.details[0], "message");
    return r.message.empty() ? std::string("Refusé par le serveur.") : r.message;
}

/// What a creation became, said after the server's answer (each kind with its own words).
std::string createdText(const std::string& kind, const std::string& n) {
    if (kind == "trail") return "Traînée « " + n + " » créée. À donner en récompense : Calendrier des défis › Récompenses.";
    if (kind == "frame") return "Cadre « " + n + " » créé. À donner en récompense : Calendrier des défis › Récompenses.";
    if (kind == "victory") return "Effet de victoire « " + n + " » créé. À donner en récompense : Calendrier des défis › Récompenses.";
    return "Combinaison « " + n + " » créée. À donner en récompense : Calendrier des défis › Récompenses.";
}
std::string savedText(const std::string& kind, const std::string& n) {
    if (kind == "trail") return "Traînée « " + n + " » enregistrée : les jeux la redessinent à leur prochaine lecture de la liste.";
    if (kind == "frame") return "Cadre « " + n + " » enregistré : les jeux le redessinent à leur prochaine lecture de la liste.";
    if (kind == "victory") return "Effet de victoire « " + n + " » enregistré : les jeux le rejouent à leur prochaine lecture de la liste.";
    return "Combinaison « " + n + " » enregistrée : les jeux la redessinent à leur prochaine lecture de la liste.";
}
std::string archivedText(const std::string& kind, const std::string& n, bool archived) {
    if (kind == "trail")
        return archived ? "Traînée « " + n + " » archivée : elle n'est plus proposée en récompense ; ceux qui l'ont gagnée la gardent."
                        : "Traînée « " + n + " » restaurée : de nouveau proposée en récompense.";
    if (kind == "frame")
        return archived ? "Cadre « " + n + " » archivé : il n'est plus proposé en récompense ; ceux qui l'ont gagné le gardent."
                        : "Cadre « " + n + " » restauré : de nouveau proposé en récompense.";
    if (kind == "victory")
        return archived ? "Effet de victoire « " + n + " » archivé : il n'est plus proposé en récompense ; ceux qui l'ont gagné le gardent."
                        : "Effet de victoire « " + n + " » restauré : de nouveau proposé en récompense.";
    return archived ? "Combinaison « " + n + " » archivée : elle n'est plus proposée en récompense ; ceux qui l'ont gagnée la gardent."
                    : "Combinaison « " + n + " » restaurée : de nouveau proposée en récompense.";
}

/// The look of a creation of the list (`kind` its kind): nullopt when its fields are not readable.
std::optional<CreationLook> lookOf(const Json& c) { return creationLookFromJson(jstr(c, "kind"), c); }

/// Buttons side by side in `w` (shrunk, and their text with them, when they do not fit).
struct ButtonRow {
    std::vector<float> widths;
    float font = 16.0f;
};
ButtonRow fitButtons(UIContext& ui, const std::vector<std::string>& labels, float w, float gap, float font = 16.0f) {
    ButtonRow row;
    row.font = font;
    float total = gap * static_cast<float>(labels.empty() ? 0 : labels.size() - 1);
    for (const std::string& l : labels) {
        row.widths.push_back(ui.text().measure(l, FontFace::Bold, ui.s(font)).x + ui.s(36));
        total += row.widths.back();
    }
    if (total > w && !labels.empty()) {
        const float k = (w - gap * static_cast<float>(labels.size() - 1)) / (total - gap * static_cast<float>(labels.size() - 1));
        for (size_t i = 0; i < labels.size(); ++i) {
            row.widths[i] *= k;
            row.font = std::min(row.font, ui.fitFont(labels[i], FontFace::Bold, font, row.widths[i] - ui.s(24), 0.6f));
        }
    }
    return row;
}

/// A small rounded pill (« archivée »…) whose left edge is at `x`, centred on `cy`; returns its width.
float pill(UIContext& ui, const std::string& t, float x, float cy, Color c) {
    const float fs = fz(ui, 12.5f);
    const float w = ui.text().measure(t, FontFace::Bold, ui.s(fs)).x + ui.s(16), h = ui.s(fs) + ui.s(8);
    Rect r{x, cy - h * 0.5f, w, h};
    ui.r().fillRoundedRect(r, h * 0.5f, c.withAlphaF(0.15f));
    ui.r().drawRoundedRect(r, h * 0.5f, c, std::max(1.0f, ui.s(1.0f)));
    ui.label(t, r.center(), fs, c, Align::Center, FontFace::Bold, VAlign::Middle);
    return w;
}

float pillWidth(UIContext& ui, const std::string& t) { return ui.text().measure(t, FontFace::Bold, ui.s(fz(ui, 12.5f))).x + ui.s(16); }

/// The palette in `w`: its label, then as many swatches per row as fit.
struct PaletteLayout {
    float size = 0, gap = 0, labelW = 0, height = 0;
    int perRow = 1;
};
PaletteLayout paletteLayout(UIContext& ui, float w) {
    PaletteLayout l;
    l.size = ui.s(30);
    l.gap = ui.s(8);
    l.labelW = ui.text().measure("Palette", FontFace::Body, ui.s(15)).x + ui.s(14);
    const float room = std::max(l.size, w - l.labelW);
    l.perRow = std::max(1, static_cast<int>((room + l.gap) / (l.size + l.gap)));
    const int rows = (kPaletteCount + l.perRow - 1) / l.perRow;
    l.height = static_cast<float>(rows) * (l.size + l.gap) - l.gap;
    return l;
}
float paletteHeight(UIContext& ui, float w) { return paletteLayout(ui, w).height; }

/// "140 %", "700 ms": the value of a trail's setting.
std::string widthText(int v) { return std::to_string(v) + " %"; }
std::string lifeText(int v) { return std::to_string(v) + " ms"; }

}  // namespace

AdminCreations::AdminCreations(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)), online_(g) {}

void AdminCreations::open(const std::string& arg) {
    // "<kind>[:new|:<id>]": that kind's list at once; the rest once the list is read.
    const size_t colon = arg.find(':');
    const int k = kindIndex(arg.substr(0, colon));
    if (k >= 0) {
        kind_ = k;
        wantOpen_ = colon == std::string::npos ? std::string() : arg.substr(colon + 1);
    } else {
        wantOpen_ = arg;
    }
}

void AdminCreations::enter(bool force) {
    if (!loaded_ || force) load();
}

const std::string& AdminCreations::kindId() const {
    static const std::string ids[kKindCount] = {"suit", "trail", "frame", "victory"};
    return ids[std::clamp(kind_, 0, kKindCount - 1)];
}

void AdminCreations::load() {
    loading_ = true;
    int gen = ++loadGen_;
    g_.online.get("/api/v1/admin/creations", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != loadGen_) return;
                      loading_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) {
                          loadError_ = errorText(r);
                          return say_(loadError_, true);
                      }
                      loadError_.clear();
                      list_ = jfield(r.data, "creations", Json::array());
                      loaded_ = true;
                      // The creation in the editor follows the server (archived or edited elsewhere), changes kept.
                      if (editing_ && draft_.id > 0)
                          if (const Json* c = byId(draft_.id)) {
                              base_.archived = draft_.archived = jget<bool>(*c, "archived", false);
                              if (!dirty()) select(*c);
                          }
                  }),
                  false);
}

const Json* AdminCreations::byId(int id) const {
    if (!list_.is_array()) return nullptr;
    for (const Json& c : list_)
        if (jget<int>(c, "id", 0) == id) return &c;
    return nullptr;
}

Rgb8& AdminCreations::current() {
    colorTab_ = std::clamp(colorTab_, 0, colorCount() - 1);
    if (suitKind()) return colorTab_ == 0 ? draft_.colors.suit : colorTab_ == 1 ? draft_.colors.accent : draft_.colors.scarf;
    return colorTab_ == 0 ? draft_.look.a : draft_.look.b;
}

Rgb8 AdminCreations::colorAt(int i) const {
    if (suitKind()) return i == 0 ? draft_.colors.suit : i == 1 ? draft_.colors.accent : draft_.colors.scarf;
    return i == 0 ? draft_.look.a : draft_.look.b;
}

bool AdminCreations::dirty() const {
    if (!editing_) return false;
    if (draft_.name != base_.name) return true;
    return draft_.kind == "suit" ? draft_.colors != base_.colors : draft_.look != base_.look;
}

std::string AdminCreations::problem() const {
    const bool fem = kKinds[kind_].feminine;
    if (draft_.archived) return fem ? "Archivée : restaurez-la pour la modifier." : "Archivé : restaurez-le pour le modifier.";
    const std::string name = cleaned(draft_.name);
    if (name.empty()) return "Donnez-lui un nom.";
    if (codepoints(name) > kNameMax) return "Nom : 32 caractères au plus.";
    return "";
}

void AdminCreations::startNew() {
    draft_ = Draft{};
    draft_.kind = kindId();
    draft_.colors = kDefaultColors;
    draft_.look = defaultCreationLook(suitKind() ? "trail" : kindId());
    base_ = draft_;
    editing_ = true;
    colorTab_ = 0;
    hexTab_ = -1;
    pendingChoice_ = 0;
    msg_.clear();
    focusName_ = true;  // given by drawEditor (the field's id is relative to its place)
}

void AdminCreations::select(const Json& c) {
    Draft d;
    d.id = jget<int>(c, "id", 0);
    d.kind = jstr(c, "kind", "suit");
    d.name = jstr(c, "name");
    d.colors = suitColorsFromJson(jfield(c, "colors", Json())).value_or(kDefaultColors);
    d.look = lookOf(c).value_or(defaultCreationLook(d.kind == "suit" ? "trail" : d.kind));
    d.updatedAt = jstr(c, "updated_at");
    d.archived = jget<bool>(c, "archived", false);
    if (const int k = kindIndex(d.kind); k >= 0) kind_ = k;
    draft_ = d;
    base_ = d;
    editing_ = true;
    hexTab_ = -1;
    pendingChoice_ = 0;
    pendingKind_ = -1;
}

void AdminCreations::choose(int id) {
    // Already open: nothing to do (a new one being made stays as it is).
    if (editing_ && ((id == -1 && draft_.id == 0) || (id > 0 && draft_.id == id))) return;
    if (dirty() && pendingChoice_ != id) {
        pendingChoice_ = id;
        msg_ = "Modifications non enregistrées : « Enregistrer », ou faites le même choix encore une fois pour les abandonner.";
        msgError_ = false;
        msgWarn_ = true;
        return;
    }
    msg_.clear();
    if (id == -1) return startNew();
    if (const Json* c = byId(id)) select(*c);
}

void AdminCreations::chooseKind(int kind) {
    if (kind == kind_ || kind < 0 || kind >= kKindCount) return;
    if (dirty() && pendingKind_ != kind) {
        pendingKind_ = kind;
        msg_ = "Modifications non enregistrées : « Enregistrer », ou faites le même choix encore une fois pour les abandonner.";
        msgError_ = false;
        msgWarn_ = true;
        return;
    }
    kind_ = kind;
    pendingKind_ = -1;
    pendingChoice_ = 0;
    editing_ = false;
    msg_.clear();
    colorTab_ = 0;
    hexTab_ = -1;
}

void AdminCreations::save() {
    if (saving_ || !editing_ || !problem().empty()) return;
    saving_ = true;
    const std::string name = cleaned(draft_.name);
    const bool isNew = draft_.id == 0;
    const std::string kind = draft_.kind;
    Json body = Json::object();
    if (kind == "suit") body["colors"] = suitColorsToJson(draft_.colors);
    else body = creationLookToJson(draft_.look);
    body["name"] = name;
    std::string path = "/api/v1/admin/creations";
    if (isNew) body["kind"] = kind;
    else {
        path += "/" + std::to_string(draft_.id);
        body["updated_at"] = draft_.updatedAt;
    }
    g_.online.send(isNew ? "POST" : "PUT", path, body, OnlineService::guard(life_, [this, isNew, kind](const ApiResult& r) {
                       saving_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           msg_ = errorText(r);
                           msgError_ = true;
                           msgWarn_ = false;
                           say_(msg_, true);
                           if (r.code == "CREATION_CHANGED" || r.code == "CREATION_ARCHIVED") load();  // what it is now (the draft kept)
                           return;
                       }
                       const Json& c = jfield(r.data, "creation", Json());
                       if (!c.is_object()) {
                           msg_ = "Réponse du serveur illisible : relisez la liste.";
                           msgError_ = true;
                           msgWarn_ = false;
                           return load();
                       }
                       select(c);
                       const std::string n = jstr(c, "name");
                       if (isNew) msg_ = createdText(kind, n);
                       else if (jget<bool>(r.data, "changed", true)) msg_ = savedText(kind, n);
                       else msg_ = "Rien à changer : le serveur l'avait déjà ainsi.";
                       msgError_ = false;
                       msgWarn_ = false;
                       say_(msg_, false);
                       load();
                       g_.refreshServerSuits();  // this game draws it at once (the suits and the other kinds)
                   }));
}

void AdminCreations::setArchived(bool archived) {
    if (saving_ || !editing_ || draft_.id <= 0) return;
    saving_ = true;
    Json body = Json::object();
    body["archived"] = archived;
    const std::string kind = draft_.kind;
    g_.online.send("POST", "/api/v1/admin/creations/" + std::to_string(draft_.id) + "/archive", body,
                   OnlineService::guard(life_, [this, archived, kind](const ApiResult& r) {
                       saving_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           msg_ = errorText(r);
                           msgError_ = true;
                           msgWarn_ = false;
                           say_(msg_, true);
                           return load();
                       }
                       const Json& c = jfield(r.data, "creation", Json());
                       if (c.is_object()) select(c);
                       msg_ = archivedText(kind, jstr(c, "name", draft_.name), archived);
                       msgError_ = false;
                       msgWarn_ = false;
                       say_(msg_, false);
                       if (archived) showArchived_ = true;  // it stays in sight
                       load();
                   }));
}

// ------------------------------------------------------------------ drawing

void AdminCreations::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    if (!wantOpen_.empty() && loaded_) {
        if (wantOpen_ == "new") startNew();
        else if (const int id = std::atoi(wantOpen_.c_str()); id > 0)
            if (const Json* c = byId(id)) select(*c);
        wantOpen_.clear();
    }
    ui.pushId("creations");
    // The kinds over the whole width, then the list and the editor.
    const float kh = ui.s(42);
    drawKinds({in.x, in.y, in.w, kh});
    const float top = in.y + kh + ui.s(14);
    const Rect body{in.x, top, in.w, in.bottom() - top};
    const float gap = ui.s(20);
    const float lw = std::floor((body.w - gap) * 0.36f);
    drawList({body.x, body.y, lw, body.h});
    drawEditor({body.x + lw + gap, body.y, body.w - lw - gap, body.h});
    ui.popId();
}

void AdminCreations::drawKinds(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.pushId("kinds");
    std::vector<std::string> labels;
    for (const KindWords& k : kKinds) labels.emplace_back(k.tab);
    const float gap = ui.s(8);
    // Equal tabs as wide as the longest label needs (never wider than the row).
    float tw = 0.0f;
    for (const std::string& l : labels) tw = std::max(tw, ui.text().measure(l, FontFace::Bold, ui.s(17)).x + ui.s(40));
    tw = std::min(tw, (r.w - gap * (kKindCount - 1)) / kKindCount);
    float font = 17.0f;
    for (const std::string& l : labels) font = std::min(font, ui.fitFont(l, FontFace::Bold, 17.0f, tw - ui.s(24), 0.6f));
    for (int i = 0; i < kKindCount; ++i) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = font;
        st.selected = i == kind_;
        st.enabled = !saving_;
        if (ui.button("k" + std::to_string(i), {r.x + static_cast<float>(i) * (tw + gap), r.y, tw, r.h}, labels[static_cast<size_t>(i)], st)) chooseKind(i);
    }
    ui.popId();
}

void AdminCreations::drawList(const Rect& r) {
    UIContext& ui = g_.ui;
    const KindWords& words = kKinds[kind_];
    ui.pushId("list");
    float y = r.y;
    // Title and counts (of this kind).
    int inService = 0, archived = 0;
    for (const Json& c : list_)
        if (jstr(c, "kind", "suit") == kindId()) (jget<bool>(c, "archived", false) ? archived : inService)++;
    {
        const std::string count = loaded_ ? std::to_string(inService) + " en service" + (archived > 0 ? " · " + std::to_string(archived) + " en archive" : std::string())
                                          : std::string();
        const float cw = count.empty() ? 0.0f : std::min(r.w * 0.5f, ui.text().measure(count, FontFace::Body, ui.s(fz(ui, 15))).x);
        fitLabel(ui, words.title, {r.x, y + ui.s(14)}, 21, r.w - cw - ui.s(12), ui.theme().text, Align::Left, FontFace::Bold);
        if (!count.empty()) fitLabel(ui, count, {r.right(), y + ui.s(14)}, 15, cw, ui.theme().textDim, Align::Right);
        y += ui.s(36);
    }
    // « Nouvelle… », « Actualiser », « Archives ».
    {
        const std::vector<std::string> labels = {words.add, "Actualiser", "Archives"};
        const float bh = ui.s(42), gap = ui.s(8);
        ButtonRow row = fitButtons(ui, labels, r.w, gap);
        float x = r.x;
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = row.font;
        st.primary = true;
        st.enabled = !saving_;
        if (ui.button("new", {x, y, row.widths[0], bh}, labels[0], st)) choose(-1);
        x += row.widths[0] + gap;
        st.primary = false;
        st.enabled = !loading_;
        if (ui.button("refresh", {x, y, row.widths[1], bh}, labels[1], st)) load();
        x += row.widths[1] + gap;
        st.enabled = true;
        st.selected = showArchived_;
        if (ui.button("archived", {x, y, row.widths[2], bh}, labels[2], st)) showArchived_ = !showArchived_;
        y += bh + ui.s(12);
    }
    // What they are for.
    {
        const float fs = fz(ui, 14.5f);
        const float th = ui.text().measureWrapped(words.info, FontFace::Body, ui.s(fs), r.w).y;
        ui.labelWrapped(words.info, {r.x, y}, r.w, fs, ui.theme().textDim);
        y += th + ui.s(12);
    }
    // The rows of this kind: those in service, then (« Archives ») the archived ones.
    std::vector<const Json*> rows;
    for (const Json& c : list_)
        if (jstr(c, "kind", "suit") == kindId() && (showArchived_ || !jget<bool>(c, "archived", false))) rows.push_back(&c);
    Rect area{r.x, y, r.w, r.bottom() - y};
    if (rows.empty()) {
        std::string t;
        Color col = ui.theme().textDim;
        if (!loadError_.empty()) {
            t = "Liste indisponible : " + loadError_;
            col = ui.theme().danger;
        } else if (!loaded_) {
            t = "Chargement…";
        } else if (archived > 0) {
            t = "Rien en service ici : « Archives » en montre " + std::to_string(archived) + ".";
        } else {
            t = words.none;
        }
        ui.labelWrapped(t, {area.x, area.y + ui.s(6)}, area.w, fz(ui, 16), col);
        ui.popId();
        return;
    }
    const float rowH = ui.s(88), gap = ui.s(8);
    const float content = static_cast<float>(rows.size()) * (rowH + gap) - gap;
    const bool scrolls = content > area.h;
    ui.beginScroll("rows", area, content);
    const float so = ui.scrollOffset();
    for (size_t i = 0; i < rows.size(); ++i) {
        Rect row{area.x, area.y - so + static_cast<float>(i) * (rowH + gap), area.w - (scrolls ? ui.s(16) : 0.0f), rowH};
        if (row.bottom() < area.y || row.y > area.bottom()) {
            // Off screen: still a stop of the keyboard / gamepad navigation (the scroll follows it).
            if (ui.hotspot("row" + std::to_string(jget<int>(*rows[i], "id", 0)), row)) choose(jget<int>(*rows[i], "id", 0));
            continue;
        }
        drawRow(*rows[i], row);
    }
    ui.endScroll();
    ui.popId();
}

void AdminCreations::drawRowPreview(const Json& c, const Rect& pv, bool selected, bool archived) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const float ps = pv.h;
    const float t = ui.time();
    r.pushClip(pv);
    if (suitKind()) {
        // Nova in the suit (its colours as the server has them; the editor's draft for the one open).
        const SuitColors colors = selected ? draft_.colors : suitColorsFromJson(jfield(c, "colors", Json())).value_or(kDefaultColors);
        drawNova(&colors, "idle", t, Rect::fromCenter({pv.center().x, pv.center().y - ps * 0.04f}, {ps * 1.1f, ps * 1.1f}),
                 archived ? Color{255, 255, 255, 140} : Color::white());
    } else if (auto look = selected ? std::optional<CreationLook>(draft_.look) : lookOf(c)) {
        const float fade = archived ? 0.55f : 1.0f;
        if (look->kind == "trail") {
            drawTrailSample(r, "", pv, t, fade, &*look);
        } else if (look->kind == "frame") {
            const float scale = ps / 80.0f;
            const float rad = std::max(ui.s(6), ps * 0.5f - kAvatarFrameMargin * scale - ui.s(1));
            online_.drawAvatar("avatar_01", "", Rect::fromCenter(pv.center(), {rad * 2, rad * 2}));
            drawAvatarFrame(r, pv.center(), rad, "", scale, t, &*look);
        } else {
            // A small Nova at the exit, the effect around her (replayed).
            const float ns = ps * 0.42f;
            const Vec2 at{pv.center().x, pv.bottom() - ps * 0.08f};
            drawNova(nullptr, "victory", t, Rect{at.x - ns * 0.5f, at.y - ns, ns, ns});
            drawVictoryFx(r, "", at, std::fmod(t, 2.4f) + 0.15f, false, ps / 190.0f, &*look);
        }
        if (archived) r.fillRect(pv, Color{11, 18, 38, 110});
    }
    r.popClip();
}

void AdminCreations::drawRow(const Json& c, const Rect& row) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const bool fem = kKinds[kind_].feminine;
    const int id = jget<int>(c, "id", 0);
    const bool archived = jget<bool>(c, "archived", false);
    const bool selected = editing_ && draft_.id == id;
    bool hovered = false;
    const std::string name = "row" + std::to_string(id);
    if (ui.hotspot(name, row, &hovered)) choose(id);
    const bool focused = ui.focused(name);
    r.fillRoundedRect(row, ui.s(10), selected ? Color{26, 38, 72, 240} : archived ? Color{22, 22, 32, 235} : Color{18, 26, 50, 235});
    r.drawRoundedRect(row, ui.s(10), selected || focused || hovered ? ui.theme().accent : Color{57, 70, 100, 140},
                      std::max(1.0f, ui.s(selected || focused ? 1.8f : 1.0f)));
    // Its preview (as every game draws it).
    const float ps = row.h - ui.s(14);
    Rect pv{row.x + ui.s(8), row.y + ui.s(7), ps, ps};
    r.fillRoundedRect(pv, ui.s(8), Color{11, 18, 38, 255});
    drawRowPreview(c, pv, selected, archived);
    // Its colours at the top right (as the server has them).
    std::vector<Rgb8> swatches;
    if (suitKind()) {
        const SuitColors s = suitColorsFromJson(jfield(c, "colors", Json())).value_or(kDefaultColors);
        swatches = {s.suit, s.accent, s.scarf};
    } else if (auto l = lookOf(c)) {
        swatches = {l->a, l->b};
    }
    const float sw = ui.s(16), sg = ui.s(4);
    const float swX = row.right() - ui.s(12) - static_cast<float>(swatches.size()) * sw - static_cast<float>(std::max<size_t>(1, swatches.size()) - 1) * sg;
    for (size_t k = 0; k < swatches.size(); ++k) {
        Rect s{swX + static_cast<float>(k) * (sw + sg), row.y + ui.s(14), sw, sw};
        r.fillRoundedRect(s, ui.s(3), colorOf(swatches[k]));
        r.drawRoundedRect(s, ui.s(3), Color{8, 12, 26, 255}, std::max(1.0f, ui.s(1.0f)));
    }
    const float tx = pv.right() + ui.s(12), room = row.right() - ui.s(12) - tx;
    // Its name (in the room left of the colours), its style and what it is used for, who made it.
    fitLabel(ui, jstr(c, "name"), {tx, row.y + ui.s(22)}, 18, swX - ui.s(10) - tx, archived ? ui.theme().textDim : ui.theme().text, Align::Left,
             FontFace::Bold);
    const int uses = jget<int>(c, "uses", 0), owners = jget<int>(c, "owners", 0);
    std::string use;
    if (uses == 0 && owners == 0) use = fem ? "Pas encore donnée en récompense" : "Pas encore donné en récompense";
    else {
        if (fem) use = uses == 0 ? std::string("Donnée par aucun défi") : "Donnée par " + std::to_string(uses) + (uses > 1 ? " défis" : " défi");
        else use = uses == 0 ? std::string("Donné par aucun défi") : "Donné par " + std::to_string(uses) + (uses > 1 ? " défis" : " défi");
        if (fem) use += " · " + (owners == 0 ? std::string("gagnée par personne") : "gagnée par " + std::to_string(owners) + (owners > 1 ? " joueurs" : " joueur"));
        else use += " · " + (owners == 0 ? std::string("gagné par personne") : "gagné par " + std::to_string(owners) + (owners > 1 ? " joueurs" : " joueur"));
    }
    if (!suitKind())
        if (const std::string style = creationStyleName(kindId(), jstr(c, "style")); !style.empty()) use = style + " · " + use;
    fitLabel(ui, use, {tx, row.y + ui.s(47)}, 14, room, ui.theme().textDim);
    float lx = tx;
    if (archived) {
        const std::string a = fem ? "archivée" : "archivé";
        if (pillWidth(ui, a) < room) lx += pill(ui, a, tx, row.y + ui.s(70), ui.theme().warm) + ui.s(8);
    }
    const std::string by = jstr(c, "created_by");
    std::string made;
    if (fem) made = (by.empty() ? std::string("Créée") : "Créée par " + by) + " le " + dayOf(jstr(c, "created_at"));
    else made = (by.empty() ? std::string("Créé") : "Créé par " + by) + " le " + dayOf(jstr(c, "created_at"));
    fitLabel(ui, made, {lx, row.y + ui.s(70)}, 13, row.right() - ui.s(12) - lx, ui.theme().textDim);
}

void AdminCreations::drawEditor(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const KindWords& words = kKinds[kind_];
    rr.fillRoundedRect(r, ui.s(12), Color{14, 20, 42, 235});
    rr.drawRoundedRect(r, ui.s(12), ui.theme().border, ui.s(1));
    const Rect in{r.x + ui.s(18), r.y + ui.s(16), r.w - ui.s(36), r.h - ui.s(32)};
    ui.pushId("ed");
    if (!editing_) {
        // The message of a kind changed with changes left (or any other) stays in sight.
        float y = in.y + ui.s(6);
        if (!msg_.empty()) {
            const float fs = fz(ui, 16);
            ui.labelWrapped(msg_, {in.x, y}, std::min(in.w, ui.s(760)), fs, msgError_ ? ui.theme().danger : msgWarn_ ? ui.theme().warm : ui.theme().success);
            y += ui.text().measureWrapped(msg_, FontFace::Body, ui.s(fs), std::min(in.w, ui.s(760))).y + ui.s(14);
        }
        ui.labelWrapped(words.intro, {in.x, y}, std::min(in.w, ui.s(760)), fz(ui, 17), ui.theme().textDim);
        ui.popId();
        return;
    }
    const float colGap = ui.s(22);
    const float cw = std::floor((in.w - colGap) * 0.58f);
    const Rect preview{in.x + cw + colGap, in.y, in.w - cw - colGap, in.h};
    float y = in.y;
    const float x = in.x;
    const bool fem = words.feminine;
    // Title (and « archivée »).
    {
        const std::string title = draft_.id == 0 ? words.newTitle : words.editTitle;
        const std::string a = fem ? "archivée" : "archivé";
        const float aw = draft_.archived ? pillWidth(ui, a) + ui.s(12) : 0.0f;
        Vec2 ts = fitLabel(ui, title, {x, y + ui.s(17)}, 25, cw - aw, ui.theme().text, Align::Left, FontFace::Title);
        if (draft_.archived) pill(ui, a, x + ts.x + ui.s(12), y + ui.s(17), ui.theme().warm);
        y += ui.s(42);
    }
    // At the bottom: the buttons, the message above them (measured first: nothing overlaps). A
    // success said by the server gives way once the draft changes again.
    if (!msgError_ && !msgWarn_ && dirty()) msg_.clear();
    const float bh = ui.s(48);
    const float by = in.bottom() - bh;
    std::string note = msg_;
    Color noteC = msgError_ ? ui.theme().danger : msgWarn_ ? ui.theme().warm : ui.theme().success;
    if (note.empty()) {
        const std::string p = problem();
        if (draft_.archived)
            note = fem ? "Archivée : elle n'est plus proposée en récompense ; ceux qui l'ont gagnée la gardent. « Restaurer » pour la modifier."
                       : "Archivé : il n'est plus proposé en récompense ; ceux qui l'ont gagné le gardent. « Restaurer » pour le modifier.";
        else if (saving_) note = "Envoi au serveur…";
        else if (dirty() && !p.empty()) note = p;
        else if (dirty() && draft_.id == 0) note = fem ? "Pas encore créée : « Créer » l'envoie au serveur." : "Pas encore créé : « Créer » l'envoie au serveur.";
        else if (dirty()) note = "Modifications non enregistrées.";
        noteC = draft_.archived || (dirty() && !p.empty()) ? ui.theme().warm : ui.theme().textDim;
    }
    const float noteFs = fz(ui, 15);
    const float noteH = note.empty() ? 0.0f : ui.text().measureWrapped(note, FontFace::Body, ui.s(noteFs), cw).y;
    const float noteY = by - ui.s(10) - noteH;
    // The controls between the title and the message (they scroll when the window is small).
    const float nameH = ui.s(46), cardH = ui.s(58), barH = ui.s(34), barGap = ui.s(8), hexH = ui.s(40);
    const float narrow = cw - ui.s(16);  // the narrower width (with a scroll bar): never more rows than drawn
    const float paletteH = paletteHeight(ui, narrow);
    float contentH = ui.s(26) + nameH + ui.s(18) + ui.s(26) + cardH + ui.s(14) + 3 * (barH + barGap) + ui.s(6) + hexH + ui.s(14) + paletteH;
    if (!suitKind()) contentH += ui.s(26) + drawStyles(0, 0, narrow, true) + ui.s(18);
    if (kindId() == "trail") contentH += drawTrailSettings(0, 0, narrow, true) + ui.s(18);
    const Rect ctl{x, y, cw, std::max(ui.s(40), noteY - ui.s(12) - y)};
    const bool scrolls = contentH > ctl.h;
    const float w = cw - (scrolls ? ui.s(16) : 0.0f);
    // Read-only while archived or while the server answers.
    const bool prevInput = ui.inputEnabled;
    if (draft_.archived || saving_) ui.inputEnabled = false;
    ui.pushId("ctl");
    ui.beginScroll("scroll", ctl, contentH);
    float cy = ctl.y - ui.scrollOffset();
    // Name and its length.
    {
        const size_t n = codepoints(cleaned(draft_.name));
        const std::string count = std::to_string(n) + " / " + std::to_string(kNameMax);
        const float cw2 = ui.text().measure(count, FontFace::Body, ui.s(fz(ui, 14))).x;
        fitLabel(ui, "Nom", {x, cy + ui.s(11)}, 16, w - cw2 - ui.s(12), ui.theme().accent, Align::Left, FontFace::Bold);
        ui.label(count, {x + w, cy + ui.s(11)}, fz(ui, 14), n > kNameMax ? ui.theme().danger : ui.theme().textDim, Align::Right, FontFace::Body,
                 VAlign::Middle);
        cy += ui.s(26);
        if (focusName_) {
            focusName_ = false;
            ui.setFocus("name");
        }
        ui.textField("name", {x, cy, w, nameH}, draft_.name, words.placeholder, 160);
        if (codepoints(draft_.name) > kNameMax + 8) draft_.name = firstCodepoints(draft_.name, kNameMax + 8);  // the field stays short
        cy += nameH + ui.s(18);
    }
    // The style (a trail's shape, a frame's style, a victory's effect): those the game draws.
    if (!suitKind()) {
        fitLabel(ui, words.styleLabel, {x, cy + ui.s(11)}, 16, w, ui.theme().accent, Align::Left, FontFace::Bold);
        cy += ui.s(26);
        cy += drawStyles(x, cy, w, false) + ui.s(18);
    }
    // A trail: its width, its length in time, its sparkles (before its colours).
    if (kindId() == "trail") {
        cy += drawTrailSettings(x, cy, w, false) + ui.s(18);
    }
    // The colours, the one edited, its bars, its code, the palette.
    {
        const std::string hint = "← → : de 5 en 5 · Maj : de 1 en 1 · souris : glisser";
        const float hw = std::min(w * 0.62f, ui.text().measure(hint, FontFace::Body, ui.s(fz(ui, 13))).x);
        fitLabel(ui, "Couleurs", {x, cy + ui.s(11)}, 16, w - hw - ui.s(12), ui.theme().accent, Align::Left, FontFace::Bold);
        fitLabel(ui, hint, {x + w, cy + ui.s(11)}, 13, hw, ui.theme().textDim, Align::Right);
        cy += ui.s(26);
        cy += drawColorCards(x, cy, w) + ui.s(14);
        cy += drawChannels(x, cy, w) + ui.s(6);
        // « Code » and the field « #rrggbb » (an exact colour typed with the keyboard).
        Rgb8& cur = current();
        auto typedColor = [this]() {
            std::string t = hex_;
            if (!t.empty() && t[0] != '#') t = "#" + t;
            return parseHexColor(t);
        };
        // The field follows the colour, except while it is typed in (an unfinished code is
        // replaced once the field is left).
        const bool typing = ui.focused("hex") && ui.editingText();
        if (hexTab_ != colorTab_ || (!typing && (cur != hexShown_ || !typedColor()))) {
            hex_ = hexColor(cur);
            hexShown_ = cur;
            hexTab_ = colorTab_;
        }
        const float lw = ui.text().measure("Code", FontFace::Body, ui.s(16)).x + ui.s(14);
        fitLabel(ui, "Code", {x, cy + hexH * 0.5f}, 16, lw - ui.s(8), ui.theme().text);
        const float fw = std::min(w - lw, ui.text().measure("#000000", FontFace::Body, ui.s(22)).x + ui.s(56));
        ui.textField("hex", {x + lw, cy, fw, hexH}, hex_, "#rrggbb", 7);
        if (auto c = typedColor(); c && *c != cur) {
            cur = *c;
            hexShown_ = *c;
        }
        const std::string example = "par exemple #c8e1ff";
        if (x + lw + fw + ui.s(14) + ui.s(60) < x + w)
            fitLabel(ui, example, {x + lw + fw + ui.s(14), cy + hexH * 0.5f}, 13, w - lw - fw - ui.s(14), ui.theme().textDim);
        cy += hexH + ui.s(14);
        cy += drawPalette(x, cy, w);
    }
    ui.endScroll();
    ui.popId();
    ui.inputEnabled = prevInput;
    // The message, then the buttons.
    if (!note.empty()) ui.labelWrapped(note, {x, noteY}, cw, noteFs, noteC);
    {
        std::vector<std::string> labels = {draft_.id == 0 ? "Créer" : "Enregistrer", "Annuler les modifications"};
        if (draft_.id > 0) labels.push_back(draft_.archived ? "Restaurer" : "Archiver");
        ButtonRow row = fitButtons(ui, labels, cw, ui.s(10), 18);
        float bx = x;
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = row.font;
        st.primary = true;
        st.enabled = !saving_ && dirty() && problem().empty();
        if (ui.button("save", {bx, by, row.widths[0], bh}, saving_ ? "Envoi…" : labels[0], st)) save();
        bx += row.widths[0] + ui.s(10);
        st.primary = false;
        st.enabled = !saving_ && dirty();
        if (ui.button("revert", {bx, by, row.widths[1], bh}, labels[1], st)) {
            draft_ = base_;
            pendingChoice_ = 0;
            pendingKind_ = -1;
            msg_.clear();
        }
        if (labels.size() > 2) {
            bx += row.widths[1] + ui.s(10);
            st.enabled = !saving_ && !dirty();
            st.danger = !draft_.archived;
            if (ui.button("archive", {bx, by, row.widths[2], bh}, labels[2], st)) setArchived(!draft_.archived);
        }
    }
    drawPreview(preview);
    ui.popId();
}

float AdminCreations::drawStyles(float x, float y, float w, bool measure) {
    UIContext& ui = g_.ui;
    const std::vector<std::string>& styles = creationStyles(kindId());
    if (styles.empty()) return 0.0f;
    // Equal buttons, as wide as the longest name needs, as many per row as fit.
    const float gap = ui.s(8), h = ui.s(40);
    float bw = 0.0f;
    for (const std::string& s : styles) bw = std::max(bw, ui.text().measure(creationStyleName(kindId(), s), FontFace::Bold, ui.s(15)).x + ui.s(30));
    bw = std::min(bw, w);
    const int perRow = std::max(1, static_cast<int>((w + gap) / (bw + gap)));
    bw = (w - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);  // the row filled
    const int rows = (static_cast<int>(styles.size()) + perRow - 1) / perRow;
    const float height = static_cast<float>(rows) * (h + gap) - gap;
    if (measure) return height;
    float font = 15.0f;
    for (const std::string& s : styles) font = std::min(font, ui.fitFont(creationStyleName(kindId(), s), FontFace::Bold, 15.0f, bw - ui.s(20), 0.6f));
    for (size_t i = 0; i < styles.size(); ++i) {
        const int col = static_cast<int>(i) % perRow, row = static_cast<int>(i) / perRow;
        Rect b{x + static_cast<float>(col) * (bw + gap), y + static_cast<float>(row) * (h + gap), bw, h};
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = font;
        st.selected = draft_.look.style == styles[i];
        if (ui.button("style" + std::to_string(i), b, creationStyleName(kindId(), styles[i]), st)) draft_.look.style = styles[i];
    }
    return height;
}

float AdminCreations::drawColorCards(float x, float y, float w) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const int n = colorCount();
    const float gap = ui.s(10), h = ui.s(58);
    const float cw = (w - static_cast<float>(n - 1) * gap) / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        Rect card{x + static_cast<float>(i) * (cw + gap), y, cw, h};
        const std::string name = "card" + std::to_string(i);
        bool hovered = false;
        if (ui.hotspot(name, card, &hovered)) colorTab_ = i;
        const bool on = colorTab_ == i, focused = ui.focused(name);
        r.fillRoundedRect(card, ui.s(10), on ? ui.theme().accent.withAlpha(36) : Color{18, 26, 50, 235});
        r.drawRoundedRect(card, ui.s(10), on || focused || hovered ? ui.theme().accent : Color{57, 70, 100, 160},
                          std::max(1.0f, ui.s(on || focused ? 2.0f : 1.0f)));
        const float sw = h - ui.s(20);
        Rect sq{card.x + ui.s(10), card.y + ui.s(10), sw, sw};
        r.fillRoundedRect(sq, ui.s(6), colorOf(colorAt(i)));
        r.drawRoundedRect(sq, ui.s(6), Color{230, 236, 245, 200}, std::max(1.0f, ui.s(1.0f)));
        const float tx = sq.right() + ui.s(10), room = card.right() - ui.s(8) - tx;
        fitLabel(ui, kKinds[kind_].colorNames[i], {tx, card.y + ui.s(19)}, 15, room, on ? Color::white() : ui.theme().text, Align::Left, FontFace::Bold,
                 VAlign::Middle, 0.6f);
        fitLabel(ui, hexColor(colorAt(i)), {tx, card.y + ui.s(40)}, 13, room, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle, 0.6f);
    }
    return h;
}

float AdminCreations::drawChannels(float x, float y, float w) {
    UIContext& ui = g_.ui;
    const float barH = ui.s(34), gap = ui.s(8);
    float lw = 0.0f;
    for (const char* n : kChannelNames) lw = std::max(lw, ui.text().measure(n, FontFace::Body, ui.s(15)).x);
    lw = std::min(lw + ui.s(14), w * 0.3f);
    const float vw = ui.text().measure("255", FontFace::Bold, ui.s(16)).x + ui.s(14);
    for (int ch = 0; ch < 3; ++ch) {
        const float cy = y + static_cast<float>(ch) * (barH + gap);
        fitLabel(ui, kChannelNames[ch], {x, cy + barH * 0.5f}, 15, lw - ui.s(8), ui.theme().text);
        channelBar(ch, {x + lw, cy, w - lw - vw, barH});
        const Rgb8 c = current();
        const int v = ch == 0 ? c.r : ch == 1 ? c.g : c.b;
        ui.label(std::to_string(v), {x + w, cy + barH * 0.5f}, 16, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
    }
    return 3 * (barH + gap) - gap;
}

bool AdminCreations::channelBar(int channel, const Rect& rc) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    InputManager& in = ui.input();
    const std::string name = "ch" + std::to_string(channel);
    bool hovered = false;
    ui.hotspot(name, rc, &hovered);
    const bool focused = ui.focused(name);
    Rgb8& c = current();
    uint8_t& v = channel == 0 ? c.r : channel == 1 ? c.g : c.b;
    int value = v;
    const Rect track{rc.x + ui.s(10), rc.center().y - ui.s(7), rc.w - ui.s(20), ui.s(14)};
    if (ui.acceptsInput() && focused && !ui.editingText()) {
        const int step = in.shift() ? 1 : 5;
        if (in.menuPressed(Action::MenuLeft)) value -= step;
        if (in.menuPressed(Action::MenuRight)) value += step;
        ui.consumeNavigation(true, false);  // ← → change the value, never the focus
    }
    if (ui.acceptsInput() && hovered && in.mousePressed(SDL_BUTTON_LEFT)) dragBar_ = channel;
    if (dragBar_ == channel) {
        if (!ui.acceptsInput() || !in.mouseDown(SDL_BUTTON_LEFT)) dragBar_ = -1;
        else value = static_cast<int>(std::lround(std::clamp((in.mousePos().x - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f) * 255.0f));
    }
    value = std::clamp(value, 0, 255);
    const bool changed = value != v;
    v = static_cast<uint8_t>(value);
    // The bar: the gradient of this channel, the other two as they are.
    r.fillRoundedRect(rc, ui.s(8), focused ? Color{28, 40, 74, 245} : Color{18, 26, 50, 235});
    r.drawRoundedRect(rc, ui.s(8), focused || hovered || dragBar_ == channel ? ui.theme().accent : Color{57, 70, 100, 160},
                      std::max(1.0f, ui.s(focused ? 2.0f : 1.0f)));
    constexpr int kSteps = 32;
    for (int i = 0; i < kSteps; ++i) {
        Rgb8 s = c;
        const uint8_t k = static_cast<uint8_t>(std::lround(255.0 * (i + 0.5) / kSteps));
        (channel == 0 ? s.r : channel == 1 ? s.g : s.b) = k;
        const float x0 = track.x + track.w * static_cast<float>(i) / kSteps, x1 = track.x + track.w * static_cast<float>(i + 1) / kSteps;
        r.fillRect({x0, track.y, x1 - x0 + 0.5f, track.h}, colorOf(s));
    }
    r.drawRect(track, Color{8, 12, 26, 255}, std::max(1.0f, ui.s(1.0f)));
    const Vec2 knob{track.x + track.w * static_cast<float>(value) / 255.0f, track.center().y};
    r.fillCircle(knob, ui.s(9), Color{8, 12, 26, 255});
    r.fillCircle(knob, ui.s(7), Color::white());
    r.fillCircle(knob, ui.s(5), colorOf(c));
    return changed;
}

float AdminCreations::drawPalette(float x, float y, float w) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const PaletteLayout l = paletteLayout(ui, w);
    const float sz = l.size, gap = l.gap, lw = l.labelW;
    const int perRow = l.perRow;
    fitLabel(ui, "Palette", {x, y + sz * 0.5f}, 15, lw - ui.s(8), ui.theme().text);
    Rgb8& cur = current();
    for (int i = 0; i < kPaletteCount; ++i) {
        Rect s{x + lw + static_cast<float>(i % perRow) * (sz + gap), y + static_cast<float>(i / perRow) * (sz + gap), sz, sz};
        const std::string name = "pal" + std::to_string(i);
        bool hovered = false;
        if (ui.hotspot(name, s, &hovered)) cur = kPalette[i];
        const bool focused = ui.focused(name), same = cur == kPalette[i];
        r.fillRoundedRect(s, ui.s(6), colorOf(kPalette[i]));
        r.drawRoundedRect(s, ui.s(6), focused || hovered ? ui.theme().accent : same ? Color::white() : Color{8, 12, 26, 255},
                          std::max(1.0f, ui.s(focused || hovered || same ? 2.4f : 1.0f)));
    }
    return l.height;
}

float AdminCreations::drawTrailSettings(float x, float y, float w, bool measure) {
    UIContext& ui = g_.ui;
    const float barH = ui.s(34), gap = ui.s(8), toggleH = ui.s(48);
    const float height = ui.s(26) + 2 * (barH + gap) + toggleH;
    if (measure) return height;
    const std::string hint = "← → : grands pas · Maj : petits pas";
    const float hw = std::min(w * 0.6f, ui.text().measure(hint, FontFace::Body, ui.s(fz(ui, 13))).x);
    fitLabel(ui, "Réglages", {x, y + ui.s(11)}, 16, w - hw - ui.s(12), ui.theme().accent, Align::Left, FontFace::Bold);
    fitLabel(ui, hint, {x + w, y + ui.s(11)}, 13, hw, ui.theme().textDim, Align::Right);
    float cy = y + ui.s(26);
    // The labels and the values measured first: the bars take what is left between them.
    float lw = 0.0f;
    for (const char* n : {"Largeur", "Durée"}) lw = std::max(lw, ui.text().measure(n, FontFace::Body, ui.s(15)).x);
    lw = std::min(lw + ui.s(14), w * 0.3f);
    const float vw = std::max(ui.text().measure(widthText(kTrailWidthMax), FontFace::Bold, ui.s(16)).x,
                              ui.text().measure(lifeText(kTrailLifeMax), FontFace::Bold, ui.s(16)).x) +
                     ui.s(14);
    fitLabel(ui, "Largeur", {x, cy + barH * 0.5f}, 15, lw - ui.s(8), ui.theme().text);
    valueBar("width", 10, {x + lw, cy, w - lw - vw, barH}, draft_.look.width, kTrailWidthMin, kTrailWidthMax, 10, 1);
    ui.label(widthText(draft_.look.width), {x + w, cy + barH * 0.5f}, 16, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
    cy += barH + gap;
    fitLabel(ui, "Durée", {x, cy + barH * 0.5f}, 15, lw - ui.s(8), ui.theme().text);
    valueBar("life", 11, {x + lw, cy, w - lw - vw, barH}, draft_.look.lifeMs, kTrailLifeMin, kTrailLifeMax, 50, 10);
    ui.label(lifeText(draft_.look.lifeMs), {x + w, cy + barH * 0.5f}, 16, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
    cy += barH + gap;
    ui.toggle("sparkles", {x, cy, w, toggleH}, "Étincelles autour de la traînée", draft_.look.particles);
    return height;
}

bool AdminCreations::valueBar(const std::string& name, int bar, const Rect& rc, int& value, int minV, int maxV, int step, int fine) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    InputManager& in = ui.input();
    bool hovered = false;
    ui.hotspot(name, rc, &hovered);
    const bool focused = ui.focused(name);
    int v = value;
    const Rect track{rc.x + ui.s(10), rc.center().y - ui.s(5), rc.w - ui.s(20), ui.s(10)};
    if (ui.acceptsInput() && focused && !ui.editingText()) {
        const int d = in.shift() ? fine : step;
        if (in.menuPressed(Action::MenuLeft)) v -= d;
        if (in.menuPressed(Action::MenuRight)) v += d;
        ui.consumeNavigation(true, false);  // ← → change the value, never the focus
    }
    if (ui.acceptsInput() && hovered && in.mousePressed(SDL_BUTTON_LEFT)) dragBar_ = bar;
    if (dragBar_ == bar) {
        if (!ui.acceptsInput() || !in.mouseDown(SDL_BUTTON_LEFT)) dragBar_ = -1;
        else {
            const float k = std::clamp((in.mousePos().x - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
            v = minV + static_cast<int>(std::lround(k * static_cast<float>(maxV - minV) / static_cast<float>(fine))) * fine;
        }
    }
    v = std::clamp(v, minV, maxV);
    const bool changed = v != value;
    value = v;
    r.fillRoundedRect(rc, ui.s(8), focused ? Color{28, 40, 74, 245} : Color{18, 26, 50, 235});
    r.drawRoundedRect(rc, ui.s(8), focused || hovered || dragBar_ == bar ? ui.theme().accent : Color{57, 70, 100, 160},
                      std::max(1.0f, ui.s(focused ? 2.0f : 1.0f)));
    const float k = static_cast<float>(v - minV) / static_cast<float>(std::max(1, maxV - minV));
    r.fillRoundedRect(track, track.h * 0.5f, Color{40, 50, 70, 255});
    r.fillRoundedRect({track.x, track.y, track.w * k, track.h}, track.h * 0.5f, ui.theme().accent);
    const Vec2 knob{track.x + track.w * k, track.center().y};
    r.fillCircle(knob, ui.s(9), Color{8, 12, 26, 255});
    r.fillCircle(knob, ui.s(7), Color::white());
    return changed;
}

void AdminCreations::drawNova(const SuitColors* c, const std::string& clip, float t, const Rect& dst, Color tint, bool flip) {
    AnimationSetPtr set = g_.app.resources().animation("animations/nova.json");
    if (!set || !set->texture) return;
    const AnimationClip* cl = set->clip(clip);
    if (!cl) cl = set->clip("idle");
    if (!cl || cl->frameCount() == 0) return;
    const float total = std::max(0.01f, cl->totalDuration());
    const float local = std::fmod(t, total);
    float acc = 0.0f;
    int frame = 0;
    for (int i = 0; i < cl->frameCount(); ++i) {
        acc += cl->durations[static_cast<size_t>(i)];
        if (local < acc) {
            frame = i;
            break;
        }
    }
    TexturePtr tex = c ? g_.suitTextures.get(g_.app.resources(), g_.app.renderer(), *c) : nullptr;
    if (!tex) tex = set->texture;
    const Rect src = set->frameRect(*cl, frame);
    g_.app.renderer().drawTexture(*tex, &src, dst, tint, BlendMode::Alpha, flip);
}

void AdminCreations::drawPreview(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    rr.fillRoundedRectGradient(r, ui.s(14), Color{17, 26, 54, 255}, Color{10, 15, 32, 255});
    rr.drawRoundedRect(r, ui.s(14), ui.theme().accent.withAlpha(80), std::max(1.0f, ui.s(1.5f)));
    const float pad = ui.s(14);
    // Its name at the top (the draft's); what is shown below, then its caption at the bottom.
    const std::string name = cleaned(draft_.name).empty() ? std::string("Sans nom") : cleaned(draft_.name);
    fitLabel(ui, firstCodepoints(name, kNameMax), {r.center().x, r.y + pad + ui.s(14)}, 21, r.w - 2 * pad,
             cleaned(draft_.name).empty() ? ui.theme().textDim : ui.theme().text, Align::Center, FontFace::Bold);
    const float top = r.y + pad + ui.s(40);
    const float bottom = r.bottom() - pad - ui.s(36);  // the caption's line under it
    if (kindId() == "trail") drawTrailPreview(r, top, bottom);
    else if (kindId() == "frame") drawFramePreview(r, top, bottom);
    else if (kindId() == "victory") drawVictoryPreview(r, top, bottom);
    else drawSuitPreview(r, top, bottom);
}

void AdminCreations::drawSuitPreview(const Rect& r, float top, float bottom) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const float pad = ui.s(14);
    // Nova, large, on a ground, in pose after pose.
    const int poses = static_cast<int>(sizeof(kPoses) / sizeof(kPoses[0]));
    const float t = ui.time();
    const Pose& pose = kPoses[static_cast<int>(t / kPoseSeconds) % poses];
    fitLabel(ui, std::string("Aperçu : ") + pose.name, {r.center().x, r.bottom() - pad - ui.s(12)}, 15, r.w - 2 * pad, ui.theme().textDim, Align::Center);
    const float ground = bottom - (bottom - top) * 0.12f;
    rr.fillRect({r.x + ui.s(2), ground, r.w - ui.s(4), ui.s(2)}, ui.theme().accent.withAlpha(90));
    const float size = std::max(ui.s(32), std::min(r.w - 2 * pad, (ground - top) * 1.05f));
    rr.pushClip({r.x + ui.s(2), top, r.w - ui.s(4), bottom - top});
    // The sheet's frames stand on their bottom edge (pivot at the feet): Nova stands on the ground.
    drawNova(&draft_.colors, pose.clip, std::fmod(t, kPoseSeconds), Rect{r.center().x - size * 0.5f, ground - size, size, size});
    rr.popClip();
}

void AdminCreations::drawTrailPreview(const Rect& r, float top, float bottom) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const float pad = ui.s(14);
    // Nova runs across, again and again (coming in on the left, going out on the right),
    // dashing halfway: her trail follows her (the game's own trail, fed with her drawn centre,
    // in screen pixels).
    const float t = ui.time();
    const float dt = previewClock_ < 0.0f ? 0.0f : std::clamp(t - previewClock_, 0.0f, 0.05f);
    previewClock_ = t;
    const float ground = top + (bottom - top) * 0.68f;
    const float size = std::max(ui.s(32), std::min(r.w * 0.42f, (ground - top) * 0.7f));
    const float pass = 1.8f;  // seconds to cross
    const float p = std::fmod(t, pass) / pass;  // 0..1
    // Faster in the middle (the dash): the speed is 1 - 0.63 cos(2πp) times the mean one.
    const float f = p - 0.1f * std::sin(p * 6.2831853f);
    const float left = r.x - size * 0.4f, right = r.right() + size * 0.4f;
    const float cx = left + (right - left) * f;
    const bool dash = p > 0.38f && p < 0.62f;
    const Vec2 center{cx, ground - size * 0.42f};
    if (previewTrailRect_.x != r.x || previewTrailRect_.y != r.y || previewTrailRect_.w != r.w || previewTrailRect_.h != r.h) {
        previewTrail_.reset();
        previewTrailRect_ = r;
    }
    previewTrail_.update(center, dt, true);
    fitLabel(ui, dash ? "Aperçu : ruée" : "Aperçu : course", {r.center().x, r.bottom() - pad - ui.s(12)}, 15, r.w - 2 * pad, ui.theme().textDim,
             Align::Center);
    rr.fillRect({r.x + ui.s(2), ground, r.w - ui.s(4), ui.s(2)}, ui.theme().accent.withAlpha(90));
    rr.pushClip({r.x + ui.s(2), top, r.w - ui.s(4), bottom - top});
    previewTrail_.draw(rr, "", false, size / 40.0f, 1.0f, &draft_.look);
    drawNova(nullptr, dash ? "dash" : "run", t, Rect{cx - size * 0.5f, ground - size, size, size});
    rr.popClip();
}

void AdminCreations::drawFramePreview(const Rect& r, float top, float bottom) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const float pad = ui.s(14);
    const float t = ui.time();
    const bool logged = g_.online.loggedIn();
    const std::string avatar = logged ? jstr(g_.online.profile(), "avatar", "avatar_01") : std::string("avatar_01");
    const std::string pid = logged ? g_.online.playerId() : std::string();
    fitLabel(ui, "Aperçu : profil et classements", {r.center().x, r.bottom() - pad - ui.s(12)}, 15, r.w - 2 * pad, ui.theme().textDim, Align::Center);
    // Below: a row as the leaderboards draw it (the avatar at their size, the admin's name).
    const float rowH = ui.s(56);
    const Rect row{r.x + pad, bottom - rowH, r.w - 2 * pad, rowH};
    const float capFs = fz(ui, 14);
    const float capY = row.y - ui.s(14);
    // Above: the profile's avatar, as large as the room left allows (its frame included).
    const float room = capY - ui.s(14) - top - ui.s(30);  // the caption « Profil » under it
    const float k = 1.0f + kAvatarFrameMargin / 40.0f;    // the avatar and its frame, in radii
    const float rad = std::max(ui.s(16), std::min((r.w - 2 * pad) * 0.5f, room * 0.5f) / k);
    const float scale = rad / 40.0f;
    const Vec2 c{r.center().x, top + kAvatarFrameMargin * scale + rad + ui.s(4)};
    online_.drawAvatar(avatar, pid, Rect::fromCenter(c, {rad * 2, rad * 2}));
    drawAvatarFrame(rr, c, rad, "", scale, t, &draft_.look);
    fitLabel(ui, "Profil", {c.x, c.y + rad + kAvatarFrameMargin * scale + ui.s(14)}, 14, r.w - 2 * pad, ui.theme().textDim, Align::Center);
    fitLabel(ui, "Classements", {r.center().x, capY}, capFs, r.w - 2 * pad, ui.theme().textDim, Align::Center);
    rr.fillRoundedRect(row, ui.s(10), Color{18, 26, 50, 235});
    rr.drawRoundedRect(row, ui.s(10), Color{57, 70, 100, 140}, std::max(1.0f, ui.s(1.0f)));
    const float avs = row.h - ui.s(14);
    const Vec2 avc{row.x + ui.s(12) + kAvatarFrameMargin * ui.s(0.5f) + avs * 0.5f, row.center().y};
    online_.drawAvatar(avatar, pid, Rect::fromCenter(avc, {avs, avs}));
    drawAvatarFrame(rr, avc, avs * 0.5f, "", ui.s(0.5f), t, &draft_.look);
    const float nx = avc.x + avs * 0.5f + ui.s(kAvatarFrameMargin * 0.5f + 10);
    const std::string who = logged ? g_.online.username() : std::string("Votre pseudo");
    fitLabel(ui, who, {nx, row.center().y}, 17, row.right() - ui.s(12) - nx, ui.theme().text, Align::Left, FontFace::Bold);
}

void AdminCreations::drawVictoryPreview(const Rect& r, float top, float bottom) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const float pad = ui.s(14);
    // Nova at the exit of a level, the effect played around her, again and again (the game's
    // effect at its proportions: it spreads about 120 pixels on each side and 170 above her).
    const float t = ui.time();
    const float cycle = kVictoryFxTime + 0.5f;
    const float tt = std::fmod(t, cycle);
    fitLabel(ui, "Aperçu : sortie du niveau", {r.center().x, r.bottom() - pad - ui.s(12)}, 15, r.w - 2 * pad, ui.theme().textDim, Align::Center);
    const float scale = std::max(0.2f, std::min((r.w - 2 * pad) / 230.0f, (bottom - top) / 240.0f));
    const float ground = std::min(bottom - ui.s(8), (top + bottom) * 0.5f + 60.0f * scale);
    rr.fillRect({r.x + ui.s(2), ground, r.w - ui.s(4), ui.s(2)}, ui.theme().accent.withAlpha(90));
    const float size = 52.0f * scale;
    const Vec2 at{r.center().x, ground};
    rr.pushClip({r.x + ui.s(2), top, r.w - ui.s(4), bottom - top});
    drawNova(nullptr, tt < kVictoryFxTime ? "victory" : "idle", tt, Rect{at.x - size * 0.5f, ground - size, size, size});
    drawVictoryFx(rr, "", at, tt, false, scale, &draft_.look);
    rr.popClip();
}

}  // namespace cosmic
