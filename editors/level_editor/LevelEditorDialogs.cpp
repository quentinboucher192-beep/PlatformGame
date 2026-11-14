// Level editor dialogs: open / new, level settings, events, cinematics, help.

#include <algorithm>
#include <format>

#include "editors/level_editor/LevelEditorScene.h"
#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/levels/EventSystem.h"
#include "game/levels/LevelTimer.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {
int indexOf(const std::vector<std::string>& v, const std::string& s) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == s) return static_cast<int>(i);
    return 0;
}

std::vector<std::string> withNone(std::vector<std::string> v) {
    v.insert(v.begin(), "");
    return v;
}

std::vector<std::string> labelsNone(const std::vector<std::string>& v) {
    std::vector<std::string> out;
    for (const auto& s : v) out.push_back(s.empty() ? "(aucune)" : s);
    return out;
}

bool pickString(UIContext& ui, const std::string& id, const Rect& r, const std::string& label, std::string& value,
                const std::vector<std::string>& options, const std::vector<std::string>& labels) {
    int i = indexOf(options, value);
    if (ui.cycle(id, r, label, i, labels)) {
        value = options[i];
        return true;
    }
    return false;
}

const std::vector<std::pair<std::string, std::string>>& stepTypes() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        {"camera", "Travelling caméra"}, {"cut", "Coupe caméra"},      {"zoom", "Zoom"},
        {"wait", "Attente"},             {"text", "Titre / texte"},    {"dialogue", "Dialogue"},
        {"spawnPlayer", "Apparition du joueur"}, {"hidePlayer", "Cacher le joueur"}, {"playerAnim", "Animation du joueur"},
        {"playerWalk", "Marche du joueur"}, {"shake", "Tremblement"},  {"flash", "Flash"},
        {"fade", "Fondu"},               {"letterbox", "Bandes cinéma"}, {"sound", "Son"},
        {"music", "Musique"},            {"effect", "Effet de particules"}, {"action", "Action d'événement"},
    };
    return v;
}
}  // namespace

void LevelEditorScene::drawModal() {
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, {0, 0, 0, 160});
    // Large dialogs leave a band at the bottom of the screen for the editor's messages.
    float w = std::min(ui.screenW() - ui.s(120), ui.s(1300)), h = std::min(ui.screenH() - ui.s(170), ui.s(900));
    float y = (ui.screenH() - h) * 0.5f - ui.s(30);
    if (modal_ == Modal::New || modal_ == Modal::ConfirmQuit) {
        w = ui.s(760);
        h = ui.s(400);
        y = (ui.screenH() - h) * 0.5f;
    }
    Rect m{(ui.screenW() - w) * 0.5f, y, w, h};
    modalRect_ = m;
    ui.panel(m, true, 16);
    ui.pushId("modal");
    Rect c{m.x + ui.s(30), m.y + ui.s(24), m.w - ui.s(60), m.h - ui.s(48)};
    switch (modal_) {
        case Modal::Open: modalOpen(c); break;
        case Modal::New: modalNew(c); break;
        case Modal::Settings: modalSettings(c); break;
        case Modal::Events: modalEvents(c); break;
        case Modal::Cinematics: modalCinematics(c); break;
        case Modal::Help: modalHelp(c); break;
        case Modal::ConfirmQuit: {
            ui.label("Quitter l'éditeur ?", {c.x, c.y}, 30, ui.theme().text, Align::Left, FontFace::Bold);
            ui.labelWrapped("Des modifications ne sont pas enregistrées.", {c.x, c.y + ui.s(60)}, c.w, 22, ui.theme().textDim);
            float by = c.bottom() - ui.s(60);
            UIContext::ButtonStyle p;
            p.primary = true;
            if (ui.button("saveq", {c.x, by, ui.s(300), ui.s(56)}, "Enregistrer et quitter", p) && save())
                g_.app.switchScene(makeContentManager(g_));
            UIContext::ButtonStyle d;
            d.danger = true;
            if (ui.button("discard", {c.x + ui.s(316), by, ui.s(190), ui.s(56)}, "Abandonner", d)) {
                autosave();
                g_.app.switchScene(makeContentManager(g_));
            }
            if (ui.button("cancelq", {c.right() - ui.s(170), by, ui.s(170), ui.s(56)}, "Annuler")) modal_ = Modal::None;
            break;
        }
        case Modal::None: break;
    }
    ui.popId();
    if (ui.backPressed() && modal_ != Modal::None && !ui.editingText()) modal_ = Modal::None;
}

// ====================================================================== open

void LevelEditorScene::modalOpen(const Rect& c) {
    UIContext& ui = g_.ui;
    ui.label("Ouvrir un niveau", {c.x, c.y}, 30, ui.theme().text, Align::Left, FontFace::Bold);
    ui.labelWrapped("Les niveaux officiels et importés sont protégés : l'éditeur en crée une copie modifiable.",
                    {c.x, c.y + ui.s(44)}, c.w, 19, ui.theme().textDim);
    Rect list{c.x, c.y + ui.s(90), c.w, c.h - ui.s(170)};
    auto levels = g_.content.levels();
    float rh = ui.s(64);
    ui.beginScroll("openlist", list, levels.size() * (rh + ui.s(8)));
    float y = list.y - ui.scrollOffset();
    for (size_t i = 0; i < levels.size(); ++i) {
        const LevelEntry* l = levels[i];
        std::string sub = std::string(contentSourceLabel(l->source)) + " · " + l->id + " · planète " + l->planet;
        if (ui.listItem("o" + std::to_string(i), {list.x, y, list.w - ui.s(14), rh}, l->name, false, sub)) {
            openLevel(l->id);
            modal_ = Modal::None;
        }
        y += rh + ui.s(8);
    }
    ui.endScroll();
    float by = c.bottom() - ui.s(58);
    UIContext::ButtonStyle p;
    p.primary = true;
    if (ui.button("tonew", {c.x, by, ui.s(280), ui.s(56)}, "Nouveau niveau", p)) modal_ = Modal::New;
    if (autosaveAvailable_ &&
        ui.button("restore", {c.x + ui.s(296), by, ui.s(380), ui.s(56)}, "Restaurer la sauvegarde auto.")) {
        if (auto j = FileUtil::loadJson(Paths::autosaveDir() / (FileUtil::slugify(level_.id) + ".json"))) {
            applyJson(*j);
            markChanged("Restauration");
        }
        modal_ = Modal::None;
    }
    if (ui.button("close", {c.right() - ui.s(170), by, ui.s(170), ui.s(56)}, "Fermer")) modal_ = Modal::None;
}

void LevelEditorScene::modalNew(const Rect& c) {
    UIContext& ui = g_.ui;
    ui.label("Nouveau niveau", {c.x, c.y}, 30, ui.theme().text, Align::Left, FontFace::Bold);
    float y = c.y + ui.s(64);
    FieldRow row{ui, c.x, c.w, ui.s(200), y};
    ui.textField("nname", row.field("Nom"), newName_, "Mon niveau");
    ui.textField("nid", row.field("Identifiant"), newId_, "généré depuis le nom");
    float by = c.bottom() - ui.s(58);
    UIContext::ButtonStyle p;
    p.primary = true;
    p.enabled = !newName_.empty() || !newId_.empty();
    if (ui.button("create", {c.x, by, ui.s(220), ui.s(56)}, "Créer", p)) {
        newLevel(newId_, newName_);
        modal_ = Modal::None;
    }
    if (ui.button("cancel", {c.right() - ui.s(170), by, ui.s(170), ui.s(56)}, "Annuler")) modal_ = Modal::None;
}

// ================================================================== settings

void LevelEditorScene::modalSettings(const Rect& c) {
    UIContext& ui = g_.ui;
    ui.label("Paramètres du niveau", {c.x, c.y}, 30, ui.theme().text, Align::Left, FontFace::Bold);
    bool changed = false;
    float colW = (c.w - ui.s(40)) * 0.5f;
    float y = c.y + ui.s(56);
    float lw = colW * 0.42f;
    Rect area{c.x, y, c.w, c.h - ui.s(130)};
    ui.beginScroll("setscroll", area, ui.s(1100));
    float y1 = y - ui.scrollOffset(), y2 = y1;
    FieldRow left{ui, c.x, colW, lw, y1};
    FieldRow right{ui, c.x + colW + ui.s(40), colW, lw, y2};
    changed |= ui.textField("name", left.field("Nom"), level_.name);
    changed |= ui.textField("desc", left.field("Description"), level_.description, "", 400);
    changed |= ui.textField("author", left.field("Auteur"), level_.author);
    changed |= ui.textField("version", left.field("Version"), level_.version);
    {
        std::vector<std::string> ids, names;
        for (const auto* p : g_.content.planets()) {
            ids.push_back(p->id);
            names.push_back(p->name);
        }
        changed |= pickString(ui, "planet", {left.x, left.y, colW, ui.s(46)}, "Planète", level_.planet, ids, names);
        left.y += ui.s(54);
    }
    {
        // The difficulty is computed from the level (jumps, hazards, enemies, boss,
        // checkpoints, healing), never typed in.
        Rect row{left.x, left.y, colW, ui.s(46)};
        ui.r().fillRoundedRect(row, ui.s(6), Color{14, 20, 38, 235});
        const auto& dr = g_.ratings.draft();
        std::string t = g_.ratings.draftPending() ? "Difficulté : calcul…" : dr ? "Difficulté calculée : " + dr->label() : "Difficulté : —";
        ui.label(t, {row.x + ui.s(12), row.center().y}, 18, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = !g_.ratings.draftPending();
        if (ui.button("rerate", {row.right() - ui.s(150), row.y + ui.s(5), ui.s(144), row.h - ui.s(10)}, "Recalculer", st))
            g_.ratings.requestDraft(level_);
        left.y += ui.s(50);
        if (dr && !g_.ratings.draftPending()) {
            ui.labelWrapped(dr->summary(), {left.x + ui.s(4), left.y}, colW - ui.s(8), 14, ui.theme().textDim);
            left.y += ui.s(26);
        }
        if (dr && !g_.ratings.draftPending()) level_.difficulty = ratingStars(dr->score);
        left.y += ui.s(4);
        // Thumbnail of the level (WORKSHOP, ONLINE LEVELS, packages): the editor's current view.
        UIContext::ButtonStyle th;
        th.small = true;
        th.enabled = !readOnly_;
        if (ui.button("thumb", {left.x, left.y, colW, ui.s(42)}, "Capturer la vignette (vue actuelle de l'éditeur)", th)) captureThumbnail();
        left.y += ui.s(50);
    }
    auto music = withNone(PropertyEditor::assetFiles("music", ".ogg"));
    changed |= pickString(ui, "music", {left.x, left.y, colW, ui.s(46)}, "Musique", level_.music, music, labelsNone(music));
    left.y += ui.s(54);
    changed |= pickString(ui, "boss", {left.x, left.y, colW, ui.s(46)}, "Musique de boss", level_.bossMusic, music, labelsNone(music));
    left.y += ui.s(54);
    changed |= pickString(ui, "amb", {left.x, left.y, colW, ui.s(46)}, "Ambiance", level_.ambience, music, labelsNone(music));
    left.y += ui.s(54);
    changed |= ui.numberField("bw", left.field("Largeur (px)"), level_.bounds.w, 160, 480, 200000, "%.0f");
    changed |= ui.numberField("bh", left.field("Hauteur (px)"), level_.bounds.h, 80, 270, 50000, "%.0f");
    changed |= ui.numberField("kill", left.field("Limite de chute Y (0 = auto)"), level_.killY, 16, 0, 60000, "%.0f");
    changed |= ui.numberField("zoom", left.field("Zoom caméra"), level_.cameraZoom, 0.05f, 0.5f, 2.0f, "%.2f");
    std::string amb = level_.ambientLight.toHex();
    if (ui.textField("ambient", left.field("Lumière ambiante"), amb, "#rrggbb", 9)) {
        level_.ambientLight = Color::fromHex(amb, level_.ambientLight);
        changed = true;
    }

    // Right column: timer, medals, start powers
    ui.label("Chronomètre", {right.x, right.y}, 22, ui.theme().accent, Align::Left, FontFace::Bold);
    right.y += ui.s(36);
    int mode = static_cast<int>(level_.timerMode);
    if (ui.cycle("mode", {right.x, right.y, colW, ui.s(46)}, "Mode", mode,
                 {"Temps limité", "Temps illimité", "Contre-la-montre", "Mode défi"})) {
        level_.timerMode = static_cast<TimerMode>(mode);
        changed = true;
    }
    right.y += ui.s(54);
    changed |= ui.numberField("limit", right.field("Durée maximale (s)"), level_.timeLimit, 10, 10, 36000, "%.0f");
    int exp = static_cast<int>(level_.onExpire);
    if (ui.cycle("expire", {right.x, right.y, colW, ui.s(46)}, "À la fin du temps", exp,
                 {"Fin de partie", "Retour au checkpoint", "Continuer"})) {
        level_.onExpire = static_cast<ExpireAction>(exp);
        changed = true;
    }
    right.y += ui.s(54);
    changed |= ui.numberField("ref", right.field("Temps de référence (s)"), level_.referenceTime, 5, 1, 36000, "%.0f");
    float cmd = static_cast<float>(level_.challengeMaxDeaths);
    if (ui.numberField("cmd", right.field("Défi : morts autorisées"), cmd, 1, 0, 99, "%.0f")) {
        level_.challengeMaxDeaths = static_cast<int>(cmd);
        changed = true;
    }
    {
        // Rules shown to the player before playing (level card): damage factor and lives.
        float df = level_.damageFactor;
        if (ui.numberField("dmgf", right.field("Facteur de dégâts reçus"), df, 0.25f, 0.25f, 5.0f, "x%.2f")) {
            level_.damageFactor = df;
            changed = true;
        }
        float lv = static_cast<float>(level_.lives);
        if (ui.numberField("lives", right.field("Vies (0 = illimitées)"), lv, 1, 0, 99, "%.0f")) {
            level_.lives = static_cast<int>(lv);
            changed = true;
        }
    }
    right.y += ui.s(10);
    ui.label("Médailles (0 = pas de condition)", {right.x, right.y}, 22, ui.theme().accent, Align::Left, FontFace::Bold);
    right.y += ui.s(36);
    static const char* mnames[4] = {"Bronze", "Argent", "Or", "Platine"};
    for (int i = 0; i < 4; ++i) {
        MedalRule& m = level_.medals[i];
        ui.label(mnames[i], {right.x, right.y + ui.s(22)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        float fx = right.x + ui.s(100), fw = (colW - ui.s(100)) / 3.0f - ui.s(6);
        changed |= ui.numberField("mt" + std::to_string(i), {fx, right.y, fw, ui.s(44)}, m.maxTime, 5, 0, 36000, "%.0f s");
        float md = static_cast<float>(m.maxDeaths);
        if (ui.numberField("md" + std::to_string(i), {fx + fw + ui.s(6), right.y, fw, ui.s(44)}, md, 1, -1, 99, "%.0f morts")) {
            m.maxDeaths = static_cast<int>(md);
            changed = true;
        }
        float mc = static_cast<float>(m.minCollectiblesPercent);
        if (ui.numberField("mc" + std::to_string(i), {fx + 2 * (fw + ui.s(6)), right.y, fw, ui.s(44)}, mc, 5, 0, 100, "%.0f%%")) {
            m.minCollectiblesPercent = static_cast<int>(mc);
            changed = true;
        }
        right.y += ui.s(52);
    }
    ui.label("(-1 morts = illimité)", {right.x, right.y}, 16, ui.theme().textDim);
    right.y += ui.s(30);
    {
        // Power slots: picking up a tablet with every slot taken asks to replace a power.
        static const std::vector<std::string> opts = {"Sans limite", "1", "2", "3", "4 (par défaut)", "5", "6", "7", "8"};
        int idx = std::clamp(level_.maxPowers, 0, LevelData::kMaxPowerSlots);
        if (ui.cycle("maxpow", {right.x, right.y, colW, ui.s(46)}, "Pouvoirs portés au maximum", idx, opts)) {
            level_.maxPowers = idx;
            changed = true;
        }
        right.y += ui.s(54);
    }
    {
        int n = static_cast<int>(level_.startPowers.size());
        bool over = level_.maxPowers > 0 && n > level_.maxPowers;
        std::string t = "Pouvoirs au départ" + (level_.maxPowers > 0 ? "  " + std::to_string(n) + " / " + std::to_string(level_.maxPowers) : "");
        ui.label(t, {right.x, right.y}, 22, over ? ui.theme().warm : ui.theme().accent, Align::Left, FontFace::Bold);
        if (over) ui.label("plus que la limite : elle passe à " + std::to_string(n), {right.x + colW, right.y + ui.s(4)}, 15, ui.theme().warm, Align::Right);
    }
    right.y += ui.s(36);
    for (const auto* p : g_.content.powers()) {
        bool has = std::find(level_.startPowers.begin(), level_.startPowers.end(), p->id) != level_.startPowers.end();
        if (ui.toggle("sp_" + p->id, {right.x, right.y, colW, ui.s(44)}, p->name, has)) {
            if (has) level_.startPowers.push_back(p->id);
            else level_.startPowers.erase(std::remove(level_.startPowers.begin(), level_.startPowers.end(), p->id), level_.startPowers.end());
            changed = true;
        }
        right.y += ui.s(50);
    }
    ui.endScroll();
    if (changed) markChanged("Paramètres du niveau");
    if (ui.button("ok", {c.right() - ui.s(200), c.bottom() - ui.s(58), ui.s(200), ui.s(56)}, "Fermer")) modal_ = Modal::None;
}

// ==================================================================== events

void LevelEditorScene::modalEvents(const Rect& c) {
    UIContext& ui = g_.ui;
    ui.label("Événements", {c.x, c.y}, 30, ui.theme().text, Align::Left, FontFace::Bold);
    ui.label("Quand <source> <déclencheur> alors <actions>", {c.x + ui.s(220), c.y + ui.s(10)}, 19, ui.theme().textDim);
    bool changed = false;
    float listW = ui.s(380);
    Rect list{c.x, c.y + ui.s(56), listW, c.h - ui.s(140)};
    float rh = ui.s(58);
    ui.beginScroll("evlist", list, level_.events.size() * (rh + ui.s(6)));
    float y = list.y - ui.scrollOffset();
    for (size_t i = 0; i < level_.events.size(); ++i) {
        const LevelEvent& e = level_.events[i];
        std::string title = e.name.empty() ? e.id : e.name;
        if (ui.listItem("ev" + std::to_string(i), {list.x, y, list.w - ui.s(14), rh}, title, selectedEvent_ == static_cast<int>(i),
                        e.source + " · " + e.trigger + " · " + std::to_string(e.actions.size()) + " action(s)"))
            selectedEvent_ = static_cast<int>(i);
        y += rh + ui.s(6);
    }
    ui.endScroll();
    float by = c.bottom() - ui.s(58);
    if (ui.button("add", {c.x, by, ui.s(180), ui.s(56)}, "Ajouter")) {
        LevelEvent e;
        e.id = level_.nextEventId();
        e.source = primary() ? primary()->id : "level";
        e.trigger = primary() ? "enter" : "levelStart";
        e.actions.push_back({"hint", "", {{"text", "Nouvel événement"}}});
        level_.events.push_back(e);
        selectedEvent_ = static_cast<int>(level_.events.size()) - 1;
        changed = true;
    }
    UIContext::ButtonStyle d;
    d.danger = true;
    d.enabled = selectedEvent_ < static_cast<int>(level_.events.size());
    if (ui.button("del", {c.x + ui.s(196), by, ui.s(180), ui.s(56)}, "Supprimer", d) && d.enabled) {
        level_.events.erase(level_.events.begin() + selectedEvent_);
        selectedEvent_ = std::max(0, selectedEvent_ - 1);
        changed = true;
    }
    if (ui.button("close", {c.right() - ui.s(180), by, ui.s(180), ui.s(56)}, "Fermer")) modal_ = Modal::None;

    if (selectedEvent_ < static_cast<int>(level_.events.size())) {
        LevelEvent& e = level_.events[selectedEvent_];
        float x = c.x + listW + ui.s(30), w = c.right() - x;
        Rect area{x, c.y + ui.s(56), w, c.h - ui.s(140)};
        ui.beginScroll("evedit", area, ui.s(260) + e.actions.size() * ui.s(330) + ui.s(120));
        float ey = area.y - ui.scrollOffset();
        FieldRow row{ui, x, w - ui.s(16), (w - ui.s(16)) * 0.36f, ey};
        changed |= ui.textField("ename", row.field("Nom"), e.name);
        Rect src = row.field("Source (id d'objet, pouvoir ou level)");
        changed |= ui.textField("esrc", {src.x, src.y, src.w - ui.s(170), src.h}, e.source);
        if (ui.button("esel", {src.right() - ui.s(160), src.y, ui.s(160), src.h}, "Sélection") && primary()) {
            e.source = primary()->id;
            changed = true;
        }
        std::vector<std::string> trigIds, trigLabels;
        for (const auto& [id, label] : EventSystem::triggerTypes()) {
            trigIds.push_back(id);
            trigLabels.push_back(label);
        }
        changed |= pickString(ui, "etrig", {x, row.y, w - ui.s(16), ui.s(46)}, "Déclencheur", e.trigger, trigIds, trigLabels);
        row.y += ui.s(54);
        if (e.trigger == "time") {
            float s = jget<float>(e.params, "seconds", 30.0f);
            if (ui.numberField("esec", row.field("Après (s)"), s, 5, 0, 36000, "%.0f")) {
                e.params["seconds"] = s;
                changed = true;
            }
        }
        if (e.trigger == "bossPhase") {
            float ph = jget<float>(e.params, "phase", 2.0f);
            if (ui.numberField("eph", row.field("Phase"), ph, 1, 1, 10, "%.0f")) {
                e.params["phase"] = static_cast<int>(ph);
                changed = true;
            }
        }
        if (e.trigger == "powerUsed") {
            std::string z = jstr(e.params, "zone");
            if (ui.textField("ezone", row.field("Dans la zone (id, optionnel)"), z)) {
                if (z.empty()) e.params.erase("zone");
                else e.params["zone"] = z;
                changed = true;
            }
        }
        changed |= ui.toggle("eonce", {x, row.y, (w - ui.s(16)) * 0.5f - ui.s(6), ui.s(44)}, "Une seule fois", e.once);
        changed |= ui.numberField("edelay", {x + (w - ui.s(16)) * 0.5f, row.y, (w - ui.s(16)) * 0.5f, ui.s(44)}, e.delay, 0.1f, 0, 600, "délai %.1f s");
        row.y += ui.s(60);
        ui.separator({x, row.y}, w - ui.s(16));
        row.y += ui.s(12);
        std::vector<std::string> actIds, actLabels;
        for (const auto& [id, label] : EventSystem::actionTypes()) {
            actIds.push_back(id);
            actLabels.push_back(label);
        }
        for (size_t ai = 0; ai < e.actions.size(); ++ai) {
            EventAction& a = e.actions[ai];
            ui.pushId("act" + std::to_string(ai));
            changed |= pickString(ui, "type", {x, row.y, w - ui.s(200), ui.s(46)}, "Action " + std::to_string(ai + 1), a.type, actIds, actLabels);
            UIContext::ButtonStyle dd;
            dd.danger = true;
            dd.small = true;
            if (ui.button("rm", {x + w - ui.s(190), row.y, ui.s(174), ui.s(46)}, "Retirer", dd)) {
                e.actions.erase(e.actions.begin() + static_cast<long>(ai));
                changed = true;
                ui.popId();
                break;
            }
            row.y += ui.s(54);
            Rect tr = row.field("Cible (ids)");
            changed |= ui.textField("tgt", {tr.x, tr.y, tr.w - ui.s(170), tr.h}, a.target);
            if (ui.button("tsel", {tr.right() - ui.s(160), tr.y, ui.s(160), tr.h}, "Sélection") && !selection_.empty()) {
                std::string ids;
                for (const auto& s : selection_) ids += (ids.empty() ? "" : ",") + s;
                a.target = ids;
                changed = true;
            }
            auto strParam = [&](const char* key, const char* label) {
                std::string v = jstr(a.params, key);
                if (ui.textField(key, row.field(label), v, "", 300)) {
                    a.params[key] = v;
                    changed = true;
                }
            };
            auto numParam = [&](const char* key, const char* label, float def, float step, float mx) {
                float v = jget<float>(a.params, key, def);
                if (ui.numberField(key, row.field(label), v, step, 0, mx, "%.2f")) {
                    a.params[key] = v;
                    changed = true;
                }
            };
            const std::string& t = a.type;
            if (t == "showText") strParam("text", "Texte"), strParam("subtitle", "Sous-titre"), numParam("duration", "Durée (s)", 3, 0.5f, 60);
            if (t == "hint") strParam("text", "Texte"), numParam("duration", "Durée (s)", 3.5f, 0.5f, 60);
            if (t == "dialogue") strParam("speaker", "Personnage"), strParam("text", "Texte"), numParam("duration", "Durée (s)", 4, 0.5f, 60);
            if (t == "cameraPan") numParam("duration", "Durée du travelling", 1.2f, 0.1f, 20), numParam("hold", "Maintien (s)", 1.0f, 0.1f, 20);
            if (t == "shake") numParam("amount", "Intensité", 0.5f, 0.05f, 1);
            if (t == "flash") strParam("color", "Couleur (#rrggbb)"), numParam("duration", "Durée (s)", 0.3f, 0.05f, 5);
            if (t == "playMusic") {
                auto music = withNone(PropertyEditor::assetFiles("music", ".ogg"));
                std::string v = jstr(a.params, "music");
                if (pickString(ui, "music", {x, row.y, w - ui.s(16), ui.s(46)}, "Musique", v, music, labelsNone(music))) {
                    a.params["music"] = v;
                    changed = true;
                }
                row.y += ui.s(54);
            }
            if (t == "playSound") {
                auto sounds = PropertyEditor::assetFiles("sounds", ".wav");
                std::string v = jstr(a.params, "sound", sounds.empty() ? "" : sounds.front());
                if (pickString(ui, "sound", {x, row.y, w - ui.s(16), ui.s(46)}, "Son", v, sounds, sounds)) {
                    a.params["sound"] = v;
                    changed = true;
                }
                row.y += ui.s(54);
            }
            if (t == "effect") {
                auto fx = PropertyEditor::effectNames(g_);
                std::string v = jstr(a.params, "effect", "explosion");
                if (pickString(ui, "effect", {x, row.y, w - ui.s(16), ui.s(46)}, "Effet", v, fx, fx)) {
                    a.params["effect"] = v;
                    changed = true;
                }
                row.y += ui.s(54);
            }
            if (t == "addTime") numParam("seconds", "Secondes", 30, 5, 3600);
            if (t == "givePower") {
                auto pw = PropertyEditor::powerIds(g_);
                std::string v = jstr(a.params, "power", pw.empty() ? "" : pw.front());
                if (pickString(ui, "power", {x, row.y, w - ui.s(16), ui.s(46)}, "Pouvoir", v, pw, pw)) {
                    a.params["power"] = v;
                    changed = true;
                }
                row.y += ui.s(54);
            }
            if (t == "heal") numParam("amount", "Points de vie", 5, 1, 20);
            row.y += ui.s(8);
            ui.separator({x, row.y}, w - ui.s(16));
            row.y += ui.s(12);
            ui.popId();
        }
        if (ui.button("addact", {x, row.y, ui.s(260), ui.s(48)}, "Ajouter une action")) {
            e.actions.push_back({"open", "", Json::object()});
            changed = true;
        }
        ui.endScroll();
    }
    if (changed) markChanged("Événements");
}

// =============================================================== cinematics

void LevelEditorScene::modalCinematics(const Rect& c) {
    UIContext& ui = g_.ui;
    ui.label("Cinématiques", {c.x, c.y}, 30, ui.theme().text, Align::Left, FontFace::Bold);
    int prev = cinematicTab_;
    ui.tabs("cinetabs", {c.x + ui.s(260), c.y, ui.s(420), ui.s(44)}, {"Introduction", "Conclusion"}, cinematicTab_);
    if (prev != cinematicTab_) selectedStep_ = 0;
    CinematicDef& def = cinematicTab_ == 0 ? level_.intro : level_.outro;
    bool changed = false;
    float listW = ui.s(420);
    changed |= ui.toggle("skip", {c.x, c.y + ui.s(56), listW * 0.5f - ui.s(4), ui.s(44)}, "Passable", def.skippable);
    changed |= ui.toggle("lb", {c.x + listW * 0.5f + ui.s(4), c.y + ui.s(56), listW * 0.5f - ui.s(4), ui.s(44)}, "Bandes", def.letterbox);
    Rect list{c.x, c.y + ui.s(110), listW, c.h - ui.s(196)};
    float rh = ui.s(52);
    ui.beginScroll("steps", list, def.steps.size() * (rh + ui.s(6)));
    float y = list.y - ui.scrollOffset();
    for (size_t i = 0; i < def.steps.size(); ++i) {
        const CinematicStep& s = def.steps[i];
        std::string label = std::to_string(i + 1) + ". " + s.type;
        for (const auto& [id, name] : stepTypes())
            if (id == s.type) label = std::to_string(i + 1) + ". " + name;
        if (ui.listItem("st" + std::to_string(i), {list.x, y, list.w - ui.s(14), rh}, label, selectedStep_ == static_cast<int>(i),
                        std::format("{:.1f} s{}", s.duration, s.wait ? "" : " · simultané")))
            selectedStep_ = static_cast<int>(i);
        y += rh + ui.s(6);
    }
    ui.endScroll();
    float by = c.bottom() - ui.s(58);
    if (ui.button("add", {c.x, by, ui.s(130), ui.s(56)}, "Ajouter")) {
        CinematicStep s;
        s.type = "wait";
        s.duration = 1.0f;
        def.steps.insert(def.steps.begin() + std::min<size_t>(def.steps.size(), selectedStep_ + 1), s);
        selectedStep_ = std::min<int>(selectedStep_ + 1, static_cast<int>(def.steps.size()) - 1);
        changed = true;
    }
    bool has = selectedStep_ < static_cast<int>(def.steps.size());
    if (ui.button("up", {c.x + ui.s(140), by, ui.s(70), ui.s(56)}, "^") && has && selectedStep_ > 0) {
        std::swap(def.steps[selectedStep_], def.steps[selectedStep_ - 1]);
        --selectedStep_;
        changed = true;
    }
    if (ui.button("down", {c.x + ui.s(218), by, ui.s(70), ui.s(56)}, "v") && has && selectedStep_ + 1 < static_cast<int>(def.steps.size())) {
        std::swap(def.steps[selectedStep_], def.steps[selectedStep_ + 1]);
        ++selectedStep_;
        changed = true;
    }
    UIContext::ButtonStyle d;
    d.danger = true;
    d.enabled = has;
    if (ui.button("del", {c.x + ui.s(296), by, ui.s(124), ui.s(56)}, "Retirer", d) && has) {
        def.steps.erase(def.steps.begin() + selectedStep_);
        selectedStep_ = std::max(0, selectedStep_ - 1);
        changed = true;
    }
    UIContext::ButtonStyle p;
    p.primary = true;
    if (ui.button("preview", {c.right() - ui.s(420), by, ui.s(230), ui.s(56)}, "Prévisualiser", p)) {
        markChanged("Cinématique");
        modal_ = Modal::None;
        testPlay(false);
    }
    if (ui.button("close", {c.right() - ui.s(180), by, ui.s(180), ui.s(56)}, "Fermer")) modal_ = Modal::None;

    if (has) {
        CinematicStep& s = def.steps[selectedStep_];
        float x = c.x + listW + ui.s(30), w = c.right() - x;
        float sy = c.y + ui.s(110);
        FieldRow row{ui, x, w, w * 0.36f, sy};
        std::vector<std::string> ids, labels;
        for (const auto& [id, name] : stepTypes()) {
            ids.push_back(id);
            labels.push_back(name);
        }
        changed |= pickString(ui, "type", {x, row.y, w, ui.s(46)}, "Type", s.type, ids, labels);
        row.y += ui.s(54);
        changed |= ui.numberField("dur", row.field("Durée (s)"), s.duration, 0.1f, 0, 120, "%.1f");
        changed |= ui.toggle("wait", {x, row.y, w, ui.s(44)}, "Attendre la fin avant l'étape suivante", s.wait);
        row.y += ui.s(54);
        auto str = [&](const char* key, const char* label) {
            std::string v = jstr(s.params, key);
            if (ui.textField(key, row.field(label), v, "", 300)) {
                s.params[key] = v;
                changed = true;
            }
        };
        auto num = [&](const char* key, const char* label, float def0, float step, float mn, float mx) {
            float v = jget<float>(s.params, key, def0);
            if (ui.numberField(key, row.field(label), v, step, mn, mx, "%.2f")) {
                s.params[key] = v;
                changed = true;
            }
        };
        auto flag = [&](const char* key, const char* label) {
            bool v = jget<bool>(s.params, key, false);
            if (ui.toggle(key, {x, row.y, w, ui.s(44)}, label, v)) {
                s.params[key] = v;
                changed = true;
            }
            row.y += ui.s(52);
        };
        const std::string& t = s.type;
        if (t == "camera" || t == "cut" || t == "effect") {
            flag("player", "Cibler le joueur");
            flag("start", "Cibler le départ du joueur");
            str("target", "Objet cible (id)");
            if (ui.button("camhere", {x, row.y, w, ui.s(46)}, "Utiliser la position actuelle de la caméra")) {
                s.params["x"] = cam_.center.x;
                s.params["y"] = cam_.center.y;
                s.params.erase("target");
                s.params["player"] = false;
                s.params["start"] = false;
                changed = true;
            }
            row.y += ui.s(54);
            num("x", "X", cam_.center.x, 16, -1e5f, 1e6f);
            num("y", "Y", cam_.center.y, 16, -1e5f, 1e6f);
            if (t == "camera") str("ease", "Courbe (inOutCubic, linear, outBack…)");
            if (t == "effect") str("effect", "Effet"), num("scale", "Échelle", 1, 0.1f, 0.1f, 10);
        }
        if (t == "zoom") num("zoom", "Zoom", 1, 0.05f, 0.3f, 3);
        if (t == "text") str("text", "Texte"), str("subtitle", "Sous-titre"), str("style", "Style (title / caption)");
        if (t == "dialogue") str("speaker", "Personnage"), str("text", "Texte");
        if (t == "playerAnim") str("anim", "Animation (victory, idle…)");
        if (t == "playerWalk") num("speed", "Vitesse (px/s, négatif = gauche)", 60, 10, -300, 300);
        if (t == "shake") num("amount", "Intensité", 0.5f, 0.05f, 0, 1);
        if (t == "flash") str("color", "Couleur (#rrggbb)");
        if (t == "fade") num("to", "Opacité finale (0-1)", 1, 0.1f, 0, 1);
        if (t == "letterbox") flag("on", "Activer");
        if (t == "sound") str("sound", "Son (sounds/...wav)");
        if (t == "music") str("music", "Musique (music/...ogg)");
        if (t == "action") str("action", "Action (open, close, spawn…)"), str("target", "Cible (ids)");
    }
    if (changed) markChanged("Cinématique");
}

void LevelEditorScene::modalHelp(const Rect& c) {
    UIContext& ui = g_.ui;
    ui.label("Raccourcis de l'éditeur", {c.x, c.y}, 30, ui.theme().text, Align::Left, FontFace::Bold);
    const char* lines[] = {
        "Clic gauche : sélectionner / placer · Maj+clic : sélection multiple · glisser dans le vide : rectangle de sélection",
        "Glisser un objet sélectionné : déplacer (aimanté à la grille) · poignées : redimensionner",
        "Terrain, liquides, zones : tracez un rectangle pour les créer",
        "Clic droit / molette enfoncée / Espace+glisser : déplacer la vue · molette : zoom · Origine : zoom 100 %",
        "ZQSD / WASD / flèches : déplacer la vue (Maj : plus vite) · Alt+flèches : décaler la sélection",
        "Ctrl+Z / Ctrl+Y : annuler / rétablir · Ctrl+C / Ctrl+V / Ctrl+D : copier, coller, dupliquer · Ctrl+A : tout",
        "Suppr : supprimer · R / Maj+R : rotation · F : miroir · G : grille · V : sélection · H : main",
        "Ctrl+S : enregistrer · F5 : tester depuis le départ · Maj+F5 : tester depuis la vue · « Tester ici » : depuis l'objet sélectionné",
        "Ctrl+G : grouper · Ctrl+Maj+G : dégrouper · Alt+clic : un seul objet d'un groupe · M : carte (clic pour s'y rendre)",
        "Plusieurs objets sélectionnés : aligner (gauche, centres, droite, haut, bas) et répartir dans le panneau de droite",
        "Onglets de gauche : bibliothèque (recherche), calques, objets (filtre), historique (cliquez pour revenir à une étape), vérifier",
        "Niveaux de la campagne et importés : lecture seule, « Dupliquer » crée votre copie modifiable",
        "Sauvegarde automatique régulière dans le dossier utilisateur (levels/autosave).",
    };
    float y = c.y + ui.s(60);
    for (const char* l : lines) {
        Vec2 s = ui.labelWrapped(l, {c.x, y}, c.w, 19, ui.theme().text);
        y += s.y + ui.s(10);
    }
    if (ui.button("guide", {c.right() - ui.s(180) - ui.s(16) - ui.s(300), c.bottom() - ui.s(58), ui.s(300), ui.s(56)}, "Guide des pouvoirs"))
        g_.app.pushScene(makePowerGuide(g_));
    if (ui.button("close", {c.right() - ui.s(180), c.bottom() - ui.s(58), ui.s(180), ui.s(56)}, "Fermer")) modal_ = Modal::None;
}

}  // namespace cosmic
