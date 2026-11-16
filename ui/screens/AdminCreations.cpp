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

const char* const kColorNames[3] = {"Combinaison", "Accent", "Écharpe"};
const char* const kChannelNames[3] = {"Rouge", "Vert", "Bleu"};

/// The poses of the preview, one after the other.
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
    if (f.rfind("colors", 0) == 0) return "Couleurs";
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

}  // namespace

AdminCreations::AdminCreations(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)) {}

void AdminCreations::open(const std::string& arg) { wantOpen_ = arg; }

void AdminCreations::enter(bool force) {
    if (!loaded_ || force) load();
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
                      // The suit in the editor follows the server (archived or edited elsewhere), changes kept.
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
    colorTab_ = std::clamp(colorTab_, 0, 2);
    return colorTab_ == 0 ? draft_.colors.suit : colorTab_ == 1 ? draft_.colors.accent : draft_.colors.scarf;
}

bool AdminCreations::dirty() const { return editing_ && (draft_.name != base_.name || draft_.colors != base_.colors); }

std::string AdminCreations::problem() const {
    if (draft_.archived) return "Archivée : restaurez-la pour la modifier.";
    const std::string name = cleaned(draft_.name);
    if (name.empty()) return "Donnez-lui un nom.";
    if (codepoints(name) > kNameMax) return "Nom : 32 caractères au plus.";
    return "";
}

void AdminCreations::startNew() {
    draft_ = Draft{};
    draft_.colors = kDefaultColors;
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
    d.name = jstr(c, "name");
    d.colors = suitColorsFromJson(jfield(c, "colors", Json())).value_or(kDefaultColors);
    d.updatedAt = jstr(c, "updated_at");
    d.archived = jget<bool>(c, "archived", false);
    draft_ = d;
    base_ = d;
    editing_ = true;
    hexTab_ = -1;
    pendingChoice_ = 0;
}

void AdminCreations::choose(int id) {
    // Already open: nothing to do (a new suit being made stays as it is).
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

void AdminCreations::save() {
    if (saving_ || !editing_ || !problem().empty()) return;
    saving_ = true;
    const std::string name = cleaned(draft_.name);
    const bool isNew = draft_.id == 0;
    Json body = Json::object();
    body["name"] = name;
    body["colors"] = suitColorsToJson(draft_.colors);
    std::string path = "/api/v1/admin/creations";
    if (isNew) body["kind"] = "suit";
    else {
        path += "/" + std::to_string(draft_.id);
        body["updated_at"] = draft_.updatedAt;
    }
    g_.online.send(isNew ? "POST" : "PUT", path, body, OnlineService::guard(life_, [this, isNew](const ApiResult& r) {
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
                       if (isNew) msg_ = "Combinaison « " + n + " » créée. À donner en récompense : Calendrier des défis › Récompenses.";
                       else if (jget<bool>(r.data, "changed", true))
                           msg_ = "Combinaison « " + n + " » enregistrée : les jeux la redessinent à leur prochaine lecture de la liste.";
                       else msg_ = "Rien à changer : le serveur l'avait déjà ainsi.";
                       msgError_ = false;
                       msgWarn_ = false;
                       say_(msg_, false);
                       load();
                       g_.refreshServerSuits();  // this game draws it at once
                   }));
}

void AdminCreations::setArchived(bool archived) {
    if (saving_ || !editing_ || draft_.id <= 0) return;
    saving_ = true;
    Json body = Json::object();
    body["archived"] = archived;
    g_.online.send("POST", "/api/v1/admin/creations/" + std::to_string(draft_.id) + "/archive", body,
                   OnlineService::guard(life_, [this, archived](const ApiResult& r) {
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
                       const std::string n = jstr(c, "name", draft_.name);
                       msg_ = archived ? "Combinaison « " + n + " » archivée : elle n'est plus proposée en récompense ; ceux qui l'ont gagnée la gardent."
                                       : "Combinaison « " + n + " » restaurée : de nouveau proposée en récompense.";
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
        wantOpen_.clear();
    }
    ui.pushId("creations");
    const float gap = ui.s(20);
    const float lw = std::floor((in.w - gap) * 0.36f);
    drawList({in.x, in.y, lw, in.h});
    drawEditor({in.x + lw + gap, in.y, in.w - lw - gap, in.h});
    ui.popId();
}

void AdminCreations::drawList(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.pushId("list");
    float y = r.y;
    // Title and counts.
    int inService = 0, archived = 0;
    for (const Json& c : list_) (jget<bool>(c, "archived", false) ? archived : inService)++;
    {
        const std::string count = loaded_ ? std::to_string(inService) + " en service" +
                                                (archived > 0 ? " · " + std::to_string(archived) + (archived > 1 ? " archivées" : " archivée") : std::string())
                                          : std::string();
        const float cw = count.empty() ? 0.0f : std::min(r.w * 0.5f, ui.text().measure(count, FontFace::Body, ui.s(fz(ui, 15))).x);
        fitLabel(ui, "Combinaisons", {r.x, y + ui.s(14)}, 21, r.w - cw - ui.s(12), ui.theme().text, Align::Left, FontFace::Bold);
        if (!count.empty()) fitLabel(ui, count, {r.right(), y + ui.s(14)}, 15, cw, ui.theme().textDim, Align::Right);
        y += ui.s(36);
    }
    // « Nouvelle combinaison », « Actualiser », « Archivées ».
    {
        const std::vector<std::string> labels = {"+ Nouvelle combinaison", "Actualiser", "Archivées"};
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
        const std::string info = "Dessinées par tous les jeux, sans mise à jour. À donner en récompense : Calendrier des défis › Récompenses.";
        const float fs = fz(ui, 14.5f);
        const float th = ui.text().measureWrapped(info, FontFace::Body, ui.s(fs), r.w).y;
        ui.labelWrapped(info, {r.x, y}, r.w, fs, ui.theme().textDim);
        y += th + ui.s(12);
    }
    // The rows: those in service, then (« Archivées ») the archived ones.
    std::vector<const Json*> rows;
    for (const Json& c : list_)
        if (showArchived_ || !jget<bool>(c, "archived", false)) rows.push_back(&c);
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
            t = archived > 1 ? "Aucune combinaison en service : « Archivées » montre les " + std::to_string(archived) + " combinaisons archivées."
                             : std::string("Aucune combinaison en service : « Archivées » montre celle qui est archivée.");
        } else {
            t = "Aucune combinaison créée pour l'instant : « Nouvelle combinaison » pour commencer.";
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
        drawRow(*rows[i], row, static_cast<int>(i));
    }
    ui.endScroll();
    ui.popId();
}

void AdminCreations::drawRow(const Json& c, const Rect& row, int /*index*/) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
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
    // Nova in the suit (its colours as the server has them; the editor's draft for the one open).
    const SuitColors colors = selected ? draft_.colors : suitColorsFromJson(jfield(c, "colors", Json())).value_or(kDefaultColors);
    const float ps = row.h - ui.s(14);
    Rect pv{row.x + ui.s(8), row.y + ui.s(7), ps, ps};
    r.fillRoundedRect(pv, ui.s(8), Color{11, 18, 38, 255});
    r.pushClip(pv);
    drawNova(colors, "idle", ui.time(), Rect::fromCenter({pv.center().x, pv.center().y - ps * 0.04f}, {ps * 1.1f, ps * 1.1f}),
             archived ? Color{255, 255, 255, 140} : Color::white());
    r.popClip();
    // The three colours at the top right.
    const float sw = ui.s(16), sg = ui.s(4);
    const float swX = row.right() - ui.s(12) - 3 * sw - 2 * sg;
    const SuitColors shown = suitColorsFromJson(jfield(c, "colors", Json())).value_or(kDefaultColors);
    const Rgb8 three[3] = {shown.suit, shown.accent, shown.scarf};
    for (int k = 0; k < 3; ++k) {
        Rect s{swX + static_cast<float>(k) * (sw + sg), row.y + ui.s(14), sw, sw};
        r.fillRoundedRect(s, ui.s(3), colorOf(three[k]));
        r.drawRoundedRect(s, ui.s(3), Color{8, 12, 26, 255}, std::max(1.0f, ui.s(1.0f)));
    }
    const float tx = pv.right() + ui.s(12), room = row.right() - ui.s(12) - tx;
    // Its name (in the room left of the colours), what it is used for, who made it.
    fitLabel(ui, jstr(c, "name"), {tx, row.y + ui.s(22)}, 18, swX - ui.s(10) - tx, archived ? ui.theme().textDim : ui.theme().text, Align::Left,
             FontFace::Bold);
    const int uses = jget<int>(c, "uses", 0), owners = jget<int>(c, "owners", 0);
    std::string use;
    if (uses == 0 && owners == 0) use = "Pas encore donnée en récompense";
    else {
        use = uses == 0 ? std::string("Donnée par aucun défi") : "Donnée par " + std::to_string(uses) + (uses > 1 ? " défis" : " défi");
        use += " · " + (owners == 0 ? std::string("gagnée par personne") : "gagnée par " + std::to_string(owners) + (owners > 1 ? " joueurs" : " joueur"));
    }
    fitLabel(ui, use, {tx, row.y + ui.s(47)}, 14, room, ui.theme().textDim);
    float lx = tx;
    if (archived) {
        const std::string a = "archivée";
        if (pillWidth(ui, a) < room) lx += pill(ui, a, tx, row.y + ui.s(70), ui.theme().warm) + ui.s(8);
    }
    const std::string by = jstr(c, "created_by");
    const std::string made = (by.empty() ? std::string("Créée") : "Créée par " + by) + " le " + dayOf(jstr(c, "created_at"));
    fitLabel(ui, made, {lx, row.y + ui.s(70)}, 13, row.right() - ui.s(12) - lx, ui.theme().textDim);
}

void AdminCreations::drawEditor(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    rr.fillRoundedRect(r, ui.s(12), Color{14, 20, 42, 235});
    rr.drawRoundedRect(r, ui.s(12), ui.theme().border, ui.s(1));
    const Rect in{r.x + ui.s(18), r.y + ui.s(16), r.w - ui.s(36), r.h - ui.s(32)};
    ui.pushId("ed");
    if (!editing_) {
        const std::string t =
            "Choisissez une combinaison dans la liste pour la modifier, ou « Nouvelle combinaison » pour en créer une : un nom et trois couleurs, "
            "appliquées à Nova comme pour les combinaisons du jeu. Elle se donne ensuite en récompense d'un défi (Calendrier des défis › "
            "Récompenses) : ceux qui la gagnent la portent, et tous les jeux la dessinent sans mise à jour.";
        ui.labelWrapped(t, {in.x, in.y + ui.s(6)}, std::min(in.w, ui.s(760)), fz(ui, 17), ui.theme().textDim);
        ui.popId();
        return;
    }
    const float colGap = ui.s(22);
    const float cw = std::floor((in.w - colGap) * 0.58f);
    const Rect preview{in.x + cw + colGap, in.y, in.w - cw - colGap, in.h};
    float y = in.y;
    const float x = in.x;
    // Title (and « archivée »).
    {
        const std::string title = draft_.id == 0 ? "Nouvelle combinaison" : "Modifier la combinaison";
        const std::string a = "archivée";
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
        if (draft_.archived) note = "Archivée : elle n'est plus proposée en récompense ; ceux qui l'ont gagnée la gardent. « Restaurer » pour la modifier.";
        else if (saving_) note = "Envoi au serveur…";
        else if (dirty() && !p.empty()) note = p;
        else if (dirty()) note = draft_.id == 0 ? "Pas encore créée : « Créer » l'envoie au serveur." : "Modifications non enregistrées.";
        noteC = draft_.archived || (dirty() && !p.empty()) ? ui.theme().warm : ui.theme().textDim;
    }
    const float noteFs = fz(ui, 15);
    const float noteH = note.empty() ? 0.0f : ui.text().measureWrapped(note, FontFace::Body, ui.s(noteFs), cw).y;
    const float noteY = by - ui.s(10) - noteH;
    // The controls between the title and the message (they scroll when the window is small).
    const float nameH = ui.s(46), cardH = ui.s(58), barH = ui.s(34), barGap = ui.s(8), hexH = ui.s(40);
    const float paletteH = paletteHeight(ui, cw - ui.s(16));  // the narrower width: never more rows than drawn
    const float contentH = ui.s(26) + nameH + ui.s(18) + ui.s(26) + cardH + ui.s(14) + 3 * (barH + barGap) + ui.s(6) + hexH + ui.s(14) + paletteH;
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
        ui.textField("name", {x, cy, w, nameH}, draft_.name, "Nom de la combinaison (32 caractères au plus)", 160);
        if (codepoints(draft_.name) > kNameMax + 8) draft_.name = firstCodepoints(draft_.name, kNameMax + 8);  // the field stays short
        cy += nameH + ui.s(18);
    }
    // The three colours, the one edited, its bars, its code, the palette.
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

float AdminCreations::drawColorCards(float x, float y, float w) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const float gap = ui.s(10), h = ui.s(58);
    const float cw = (w - 2 * gap) / 3.0f;
    const Rgb8 colors[3] = {draft_.colors.suit, draft_.colors.accent, draft_.colors.scarf};
    for (int i = 0; i < 3; ++i) {
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
        r.fillRoundedRect(sq, ui.s(6), colorOf(colors[i]));
        r.drawRoundedRect(sq, ui.s(6), Color{230, 236, 245, 200}, std::max(1.0f, ui.s(1.0f)));
        const float tx = sq.right() + ui.s(10), room = card.right() - ui.s(8) - tx;
        fitLabel(ui, kColorNames[i], {tx, card.y + ui.s(19)}, 15, room, on ? Color::white() : ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle,
                 0.6f);
        fitLabel(ui, hexColor(colors[i]), {tx, card.y + ui.s(40)}, 13, room, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle, 0.6f);
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
    if (ui.acceptsInput() && hovered && in.mousePressed(SDL_BUTTON_LEFT)) dragChannel_ = channel;
    if (dragChannel_ == channel) {
        if (!ui.acceptsInput() || !in.mouseDown(SDL_BUTTON_LEFT)) dragChannel_ = -1;
        else value = static_cast<int>(std::lround(std::clamp((in.mousePos().x - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f) * 255.0f));
    }
    value = std::clamp(value, 0, 255);
    const bool changed = value != v;
    v = static_cast<uint8_t>(value);
    // The bar: the gradient of this channel, the other two as they are.
    r.fillRoundedRect(rc, ui.s(8), focused ? Color{28, 40, 74, 245} : Color{18, 26, 50, 235});
    r.drawRoundedRect(rc, ui.s(8), focused || hovered || dragChannel_ == channel ? ui.theme().accent : Color{57, 70, 100, 160},
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

void AdminCreations::drawNova(const SuitColors& c, const std::string& clip, float t, const Rect& dst, Color tint) {
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
    TexturePtr tex = g_.suitTextures.get(g_.app.resources(), g_.app.renderer(), c);
    if (!tex) tex = set->texture;
    const Rect src = set->frameRect(*cl, frame);
    g_.app.renderer().drawTexture(*tex, &src, dst, tint);
}

void AdminCreations::drawPreview(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    rr.fillRoundedRectGradient(r, ui.s(14), Color{17, 26, 54, 255}, Color{10, 15, 32, 255});
    rr.drawRoundedRect(r, ui.s(14), ui.theme().accent.withAlpha(80), std::max(1.0f, ui.s(1.5f)));
    const float pad = ui.s(14);
    // Its name at the top (the draft's), then Nova, large, on a ground, in pose after pose.
    const std::string name = cleaned(draft_.name).empty() ? std::string("Sans nom") : cleaned(draft_.name);
    fitLabel(ui, firstCodepoints(name, kNameMax), {r.center().x, r.y + pad + ui.s(14)}, 21, r.w - 2 * pad,
             cleaned(draft_.name).empty() ? ui.theme().textDim : ui.theme().text, Align::Center, FontFace::Bold);
    const int poses = static_cast<int>(sizeof(kPoses) / sizeof(kPoses[0]));
    const float t = ui.time();
    const Pose& pose = kPoses[static_cast<int>(t / kPoseSeconds) % poses];
    const std::string caption = std::string("Aperçu : ") + pose.name;
    const float capY = r.bottom() - pad - ui.s(12);
    fitLabel(ui, caption, {r.center().x, capY}, 15, r.w - 2 * pad, ui.theme().textDim, Align::Center);
    const float top = r.y + pad + ui.s(40), bottom = capY - ui.s(24);
    const float ground = bottom - (bottom - top) * 0.12f;
    rr.fillRect({r.x + ui.s(2), ground, r.w - ui.s(4), ui.s(2)}, ui.theme().accent.withAlpha(90));
    const float size = std::max(ui.s(32), std::min(r.w - 2 * pad, (ground - top) * 1.05f));
    rr.pushClip({r.x + ui.s(2), top, r.w - ui.s(4), bottom - top});
    // The sheet's frames stand on their bottom edge (pivot at the feet): Nova stands on the ground.
    drawNova(draft_.colors, pose.clip, std::fmod(t, kPoseSeconds), Rect{r.center().x - size * 0.5f, ground - size, size, size});
    rr.popClip();
}

}  // namespace cosmic
