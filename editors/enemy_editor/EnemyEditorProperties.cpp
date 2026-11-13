// Enemy editor property tabs.

#include <format>

#include "editors/enemy_editor/EnemyEditorScene.h"
#include "editors/shared/PropertyEditor.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"

namespace cosmic {

namespace {
template <typename E>
bool enumCycle(UIContext& ui, const std::string& id, const Rect& r, const std::string& label, E& value,
               const char* (*labelFn)(E)) {
    std::vector<std::string> names;
    for (int i = 0; i < static_cast<int>(E::Count); ++i) names.push_back(labelFn(static_cast<E>(i)));
    int idx = static_cast<int>(value);
    if (ui.cycle(id, r, label, idx, names)) {
        value = static_cast<E>(idx);
        return true;
    }
    return false;
}

bool stringCycle(UIContext& ui, const std::string& id, const Rect& r, const std::string& label, std::string& value,
                 const std::vector<std::string>& options) {
    if (options.empty()) return false;
    int idx = 0;
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i] == value) idx = static_cast<int>(i);
    std::vector<std::string> shown;
    for (const auto& o : options) shown.push_back(o.empty() ? "(aucun)" : o);
    if (ui.cycle(id, r, label, idx, shown)) {
        value = options[idx];
        return true;
    }
    return false;
}
}  // namespace

void EnemyEditorScene::drawProperties(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {12, 16, 32, 245});
    ui.r().fillRect({r.x, r.y, 1, r.h}, ui.theme().border);
    ui.pushId("props");
    std::vector<std::string> tabs = {"Général", "IA", "Attaques", "Pouvoirs", "Résist.", "Butin", "Boss"};
    ui.tabs("tabs", {r.x + ui.s(8), r.y + ui.s(8), r.w - ui.s(16), ui.s(44)}, tabs, tab_);
    Rect area{r.x, r.y + ui.s(60), r.w, r.h - ui.s(70)};
    float x = r.x + ui.s(16), w = r.w - ui.s(44);
    ui.beginScroll("scroll" + std::to_string(tab_), area, ui.s(2200));
    float y = area.y + ui.s(6) - ui.scrollOffset();
    switch (tab_) {
        case 0: tabGeneral(x, y, w); break;
        case 1: tabBehaviour(x, y, w); break;
        case 2: tabAttacks(x, y, w); break;
        case 3: tabPowers(x, y, w); break;
        case 4: tabResistances(x, y, w); break;
        case 5: tabRewards(x, y, w); break;
        case 6: tabBoss(x, y, w); break;
    }
    ui.endScroll();
    ui.popId();
}

void EnemyEditorScene::tabGeneral(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    FieldRow row{ui, x, w, w * 0.4f, y};
    bool c = false;
    c |= ui.textField("name", row.field("Nom"), work_.name);
    c |= ui.textField("id", row.field("Identifiant"), work_.id, "", 48);
    c |= ui.textField("desc", row.field("Description"), work_.description, "", 300);
    c |= enumCycle(ui, "family", {x, y, w, ui.s(46)}, "Famille", work_.family, &familyLabel);
    y += ui.s(54);
    std::vector<std::string> anims;
    for (const auto& a : PropertyEditor::assetFiles("animations", ".json"))
        if (auto j = g_.app.resources().json(a); j && j->contains("clips")) anims.push_back(a);
    if (stringCycle(ui, "anim", {x, y, w, ui.s(46)}, "Sprites", work_.animation, anims)) {
        c = true;
        loadAnimation();
    }
    y += ui.s(54);
    c |= ui.numberField("sw", row.field("Hitbox largeur"), work_.size.x, 1, 2, 400, "%.0f");
    c |= ui.numberField("sh", row.field("Hitbox hauteur"), work_.size.y, 1, 2, 400, "%.0f");
    c |= ui.numberField("scale", row.field("Échelle du sprite"), work_.scale, 0.1f, 0.25f, 4, "%.2f");
    c |= ui.numberField("hp", row.field("Points de vie"), work_.hp, 1, 0.5f, 10000, "%.1f");
    c |= ui.numberField("contact", row.field("Dégâts de contact"), work_.contactDamage, 1, 0, 50, "%.0f");
    c |= ui.numberField("speed", row.field("Vitesse (px/s)"), work_.speed, 5, 0, 1000, "%.0f");
    c |= ui.numberField("chase", row.field("Multiplicateur poursuite"), work_.chaseSpeedMultiplier, 0.1f, 0.1f, 5, "%.1f");
    c |= ui.toggle("flying", {x, y, w, ui.s(44)}, "Vole (ignore la gravité)", work_.flying);
    y += ui.s(52);
    c |= ui.slider("kb", {x, y, w, ui.s(44)}, "Résistance au recul", work_.knockbackResist, 0, 1, 0.05f, "%.2f");
    y += ui.s(52);
    std::string col = work_.lightColor.toHex();
    if (ui.textField("light", row.field("Couleur de lueur"), col, "#rrggbb", 9)) {
        work_.lightColor = Color::fromHex(col, work_.lightColor);
        c = true;
    }
    c |= ui.numberField("lr", row.field("Rayon de lueur"), work_.lightRadius, 2, 0, 400, "%.0f");
    auto sounds = PropertyEditor::assetFiles("sounds", ".wav");
    c |= stringCycle(ui, "shurt", {x, y, w, ui.s(46)}, "Son : blessure", work_.hurtSound, sounds);
    y += ui.s(54);
    c |= stringCycle(ui, "sdeath", {x, y, w, ui.s(46)}, "Son : mort", work_.deathSound, sounds);
    y += ui.s(54);
    c |= stringCycle(ui, "salert", {x, y, w, ui.s(46)}, "Son : alerte", work_.alertSound, sounds);
    y += ui.s(54);
    if (c) changed();
}

void EnemyEditorScene::tabBehaviour(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    FieldRow row{ui, x, w, w * 0.45f, y};
    bool c = false;
    c |= enumCycle(ui, "move", {x, y, w, ui.s(46)}, "Déplacement", work_.movement, &movementLabel);
    y += ui.s(54);
    c |= enumCycle(ui, "react", {x, y, w, ui.s(46)}, "Réaction au joueur", work_.reaction, &reactionLabel);
    y += ui.s(54);
    c |= ui.numberField("det", row.field("Portée de détection"), work_.detectionRange, 10, 0, 2000, "%.0f");
    c |= ui.numberField("atk", row.field("Portée d'attaque"), work_.attackRange, 4, 0, 2000, "%.0f");
    c |= ui.numberField("lose", row.field("Abandon de poursuite"), work_.loseRange, 10, 0, 4000, "%.0f");
    c |= ui.toggle("los", {x, y, w, ui.s(44)}, "Exige une ligne de vue", work_.needsLineOfSight);
    y += ui.s(52);
    c |= ui.slider("flee", {x, y, w, ui.s(44)}, "Fuite sous (% PV)", work_.fleeHealthRatio, 0, 1, 0.05f, "%.2f");
    y += ui.s(52);
    c |= ui.numberField("pref", row.field("Distance préférée (tir)"), work_.preferredDistance, 5, 0, 1000, "%.0f");
    c |= ui.numberField("hop", row.field("Force des sauts"), work_.hopVelocity, 10, 0, 1000, "%.0f");
    y += ui.s(10);
    ui.labelWrapped("Patrouille, surveillance, vol, sauts ou tourelle fixe ; puis poursuite, tir à distance, fuite ou "
                    "protection d'une zone une fois le joueur repéré. Toutes les attaques sont annoncées (télégraphiées).",
                    {x, y}, w, 17, ui.theme().textDim);
    y += ui.s(80);
    if (c) changed();
}

void EnemyEditorScene::tabAttacks(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    bool c = false;
    for (size_t i = 0; i < work_.attacks.size(); ++i) {
        if (ui.listItem("a" + std::to_string(i), {x, y, w, ui.s(50)}, work_.attacks[i].name, selectedAttack_ == static_cast<int>(i),
                        attackTypeLabel(work_.attacks[i].type)))
            selectedAttack_ = static_cast<int>(i);
        y += ui.s(56);
    }
    UIContext::ButtonStyle st;
    st.small = true;
    if (ui.button("add", {x, y, w * 0.48f, ui.s(46)}, "Ajouter une attaque", st)) {
        AttackDef a;
        a.id = "attaque" + std::to_string(work_.attacks.size() + 1);
        a.name = "Nouvelle attaque";
        work_.attacks.push_back(a);
        selectedAttack_ = static_cast<int>(work_.attacks.size()) - 1;
        c = true;
    }
    UIContext::ButtonStyle d;
    d.small = true;
    d.danger = true;
    d.enabled = !work_.attacks.empty();
    if (ui.button("rm", {x + w * 0.52f, y, w * 0.48f, ui.s(46)}, "Retirer", d) && d.enabled) {
        work_.attacks.erase(work_.attacks.begin() + selectedAttack_);
        selectedAttack_ = std::max(0, selectedAttack_ - 1);
        c = true;
    }
    y += ui.s(60);
    if (!work_.attacks.empty()) {
        selectedAttack_ = std::clamp(selectedAttack_, 0, static_cast<int>(work_.attacks.size()) - 1);
        AttackDef& a = work_.attacks[selectedAttack_];
        ui.separator({x, y}, w);
        y += ui.s(12);
        FieldRow row{ui, x, w, w * 0.45f, y};
        c |= ui.textField("aid", row.field("Identifiant"), a.id, "", 32);
        c |= ui.textField("aname", row.field("Nom"), a.name);
        c |= enumCycle(ui, "atype", {x, y, w, ui.s(46)}, "Type", a.type, &attackTypeLabel);
        y += ui.s(54);
        c |= ui.numberField("dmg", row.field("Dégâts"), a.damage, 1, 0, 50, "%.0f");
        c |= ui.numberField("range", row.field("Portée max"), a.range, 4, 0, 2000, "%.0f");
        c |= ui.numberField("minr", row.field("Portée min"), a.minRange, 4, 0, 2000, "%.0f");
        c |= ui.numberField("tel", row.field("Télégraphie (s)"), a.telegraph, 0.05f, 0, 10, "%.2f");
        c |= ui.numberField("act", row.field("Durée active (s)"), a.active, 0.05f, 0, 10, "%.2f");
        c |= ui.numberField("rec", row.field("Récupération (s)"), a.recover, 0.05f, 0, 10, "%.2f");
        c |= ui.numberField("cd", row.field("Temps de recharge (s)"), a.cooldown, 0.1f, 0, 60, "%.1f");
        c |= ui.numberField("weight", row.field("Poids (probabilité)"), a.weight, 0.1f, 0.01f, 10, "%.1f");
        auto clips = clipNames();
        c |= stringCycle(ui, "tanim", {x, y, w, ui.s(46)}, "Anim. télégraphie", a.telegraphAnim, clips);
        y += ui.s(54);
        c |= stringCycle(ui, "aanim", {x, y, w, ui.s(46)}, "Anim. attaque", a.attackAnim, clips);
        y += ui.s(54);
        c |= ui.numberField("kb", row.field("Recul infligé"), a.knockback, 10, 0, 1000, "%.0f");
        if (a.isProjectileType() || a.type == AttackType::Slam) {
            std::vector<std::string> proj;
            if (auto pa = g_.app.resources().animation("animations/projectiles.json"))
                for (const auto& [k, _] : pa->clips) proj.push_back(k);
            std::sort(proj.begin(), proj.end());
            if (a.type != AttackType::Slam) {
                c |= stringCycle(ui, "proj", {x, y, w, ui.s(46)}, "Projectile", a.projectile, proj);
                y += ui.s(54);
            }
            c |= ui.numberField("pspd", row.field("Vitesse projectile"), a.projectileSpeed, 10, 10, 1000, "%.0f");
            if (a.type != AttackType::Slam) {
                float cnt = static_cast<float>(a.count);
                if (ui.numberField("cnt", row.field("Nombre"), cnt, 1, 1, 24, "%.0f")) {
                    a.count = static_cast<int>(cnt);
                    c = true;
                }
                bool spacing = a.type == AttackType::Rain || a.type == AttackType::Eruption;
                c |= ui.numberField("spr", row.field(spacing ? "Espacement (px)" : "Écart (°)"), a.spread, 1, 0,
                                    spacing ? 200.0f : 90.0f, "%.0f");
                c |= ui.numberField("grav", row.field("Gravité"), a.gravity, 20, 0, 2000, "%.0f");
                c |= ui.numberField("prad", row.field("Rayon du projectile (0 = auto)"), a.projectileRadius, 0.5f, 0, 40, "%.1f");
                c |= ui.numberField("expl", row.field("Rayon d'explosion (bombes)"), a.explosionRadius, 2, 0, 200, "%.0f");
                if (a.type == AttackType::Rain)
                    c |= ui.numberField("rainh", row.field("Hauteur de la pluie"), a.rainHeight, 10, 40, 800, "%.0f");
                if (a.type == AttackType::Eruption) {
                    static const std::vector<std::string> warn = {"eruption_warning_fire", "eruption_warning_ice",
                                                                  "eruption_warning_spore"};
                    static const std::vector<std::string> burst = {"eruption_fire", "eruption_ice", "eruption_spore"};
                    c |= stringCycle(ui, "ewarn", {x, y, w, ui.s(46)}, "Avertissement", a.warningEffect, warn);
                    y += ui.s(54);
                    c |= stringCycle(ui, "eburst", {x, y, w, ui.s(46)}, "Éruption", a.eruptionEffect, burst);
                    y += ui.s(54);
                }
            }
        }
        c |= ui.numberField("slow", row.field("Ralentit le joueur à (0 = non)"), a.slow, 0.05f, 0, 1, "%.2f");
        if (a.slow > 0.0f) c |= ui.numberField("slowd", row.field("Durée du ralentissement (s)"), a.slowDuration, 0.1f, 0, 10, "%.1f");
        if (a.isProjectileType() || a.type == AttackType::Laser) {
            float ox = a.origin.x, oy = a.origin.y;
            bool o1 = ui.numberField("orx", row.field("Origine X (depuis les pieds)"), ox, 1, -200, 200, "%.0f");
            bool o2 = ui.numberField("ory", row.field("Origine Y (depuis les pieds)"), oy, 1, -200, 200, "%.0f");
            if (o1 || o2) {
                a.origin = {ox, oy};
                a.hasOrigin = true;
                c = true;
            }
        }
        if (a.type == AttackType::Lunge || a.type == AttackType::Charge)
            c |= ui.numberField("lunge", row.field("Vitesse d'élan"), a.lungeSpeed, 10, 10, 1000, "%.0f");
        if (a.type == AttackType::Summon) {
            auto ids = PropertyEditor::enemyIds(g_);
            c |= stringCycle(ui, "sumid", {x, y, w, ui.s(46)}, "Renfort", a.summonId, ids);
            y += ui.s(54);
            float sc = static_cast<float>(a.summonCount), mx = static_cast<float>(a.maxSummoned);
            if (ui.numberField("sumc", row.field("Nombre par appel"), sc, 1, 1, 8, "%.0f")) a.summonCount = static_cast<int>(sc), c = true;
            if (ui.numberField("summ", row.field("Maximum en vie"), mx, 1, 1, 16, "%.0f")) a.maxSummoned = static_cast<int>(mx), c = true;
        }
        if (a.type == AttackType::Laser) {
            c |= ui.numberField("llen", row.field("Longueur du rayon"), a.laserLength, 10, 20, 2000, "%.0f");
            c |= ui.numberField("lsw", row.field("Balayage (°)"), a.sweepDegrees, 5, 0, 180, "%.0f");
            static const std::vector<std::string> beams = {"#ff4d6d", "#ff8a3d", "#5fb0ff", "#39f0d8", "#b04cff", "#ffd98a"};
            static const std::vector<std::string> beamNames = {"Rouge", "Feu", "Glace", "Cyan", "Violet", "Or"};
            int bi = 0;
            for (size_t k = 0; k < beams.size(); ++k)
                if (Color::fromHex(beams[k]).toHex() == a.beamColor.toHex()) bi = static_cast<int>(k);
            if (ui.cycle("lcol", {x, y, w, ui.s(46)}, "Couleur du rayon", bi, beamNames)) {
                a.beamColor = Color::fromHex(beams[static_cast<size_t>(bi)]);
                c = true;
            }
            y += ui.s(54);
        }
        auto sounds = PropertyEditor::assetFiles("sounds", ".wav");
        sounds.insert(sounds.begin(), "");
        c |= stringCycle(ui, "asnd", {x, y, w, ui.s(46)}, "Son de l'attaque", a.sound, sounds);
        y += ui.s(54);
        c |= stringCycle(ui, "tsnd", {x, y, w, ui.s(46)}, "Son de télégraphie", a.telegraphSound, sounds);
        y += ui.s(54);
    }
    if (c) changed();
}

void EnemyEditorScene::tabPowers(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    FieldRow row{ui, x, w, w * 0.45f, y};
    bool c = false;
    ui.label("Téléportation", {x, y}, 21, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(34);
    c |= ui.toggle("tp", {x, y, w, ui.s(44)}, "Activée", work_.canTeleport);
    y += ui.s(52);
    c |= ui.numberField("tpcd", row.field("Temps de recharge (s)"), work_.teleportCooldown, 0.5f, 0.5f, 60, "%.1f");
    c |= ui.numberField("tpr", row.field("Distance"), work_.teleportRange, 5, 10, 1000, "%.0f");
    y += ui.s(10);
    ui.label("Défense (garde frontale)", {x, y}, 21, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(34);
    c |= ui.toggle("def", {x, y, w, ui.s(44)}, "Activée", work_.canDefend);
    y += ui.s(52);
    c |= ui.numberField("defd", row.field("Durée de garde (s)"), work_.defendDuration, 0.1f, 0.1f, 10, "%.1f");
    c |= ui.numberField("defc", row.field("Temps de recharge (s)"), work_.defendCooldown, 0.5f, 0, 60, "%.1f");
    y += ui.s(10);
    ui.label("Appel de renforts (alerte)", {x, y}, 21, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(34);
    c |= ui.toggle("rf", {x, y, w, ui.s(44)}, "Alerte les ennemis proches", work_.callsReinforcements);
    y += ui.s(52);
    c |= ui.numberField("rfr", row.field("Rayon d'alerte"), work_.alertRadius, 10, 10, 2000, "%.0f");
    y += ui.s(6);
    ui.labelWrapped("Pour invoquer des créatures, ajoutez une attaque de type « Appel de renforts ».", {x, y}, w, 17,
                    ui.theme().textDim);
    y += ui.s(50);
    if (c) changed();
}

void EnemyEditorScene::tabResistances(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    bool c = false;
    ui.labelWrapped("Multiplicateur de dégâts par source : 0 = immunisé, < 1 = résistance, > 1 = faiblesse.", {x, y}, w, 18,
                    ui.theme().textDim);
    y += ui.s(60);
    for (int i = 0; i < static_cast<int>(DamageType::Count); ++i) {
        auto t = static_cast<DamageType>(i);
        if (t == DamageType::Contact) continue;
        float& m = work_.damageMultipliers[i];
        c |= ui.slider(std::string("dm") + damageTypeId(t), {x, y, w, ui.s(44)}, damageTypeLabel(t), m, 0.0f, 3.0f, 0.1f, "x%.1f");
        y += ui.s(52);
    }
    if (c) changed();
}

void EnemyEditorScene::tabRewards(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    FieldRow row{ui, x, w, w * 0.45f, y};
    bool c = false;
    float score = static_cast<float>(work_.scoreValue);
    if (ui.numberField("score", row.field("Points"), score, 10, 0, 100000, "%.0f")) {
        work_.scoreValue = static_cast<int>(score);
        c = true;
    }
    ui.label("Objets lâchés", {x, y}, 21, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(34);
    std::vector<std::string> types = {"energy_orb", "health_orb", "shard"};
    for (size_t i = 0; i < work_.drops.size(); ++i) {
        DropDef& d = work_.drops[i];
        ui.pushId("drop" + std::to_string(i));
        c |= stringCycle(ui, "type", {x, y, w * 0.55f, ui.s(44)}, "Objet", d.type, types);
        c |= ui.numberField("chance", {x + w * 0.57f, y, w * 0.28f, ui.s(44)}, d.chance, 0.05f, 0, 1, "%.2f");
        UIContext::ButtonStyle dd;
        dd.small = true;
        dd.danger = true;
        if (ui.button("rm", {x + w * 0.87f, y, w * 0.13f, ui.s(44)}, "x", dd)) {
            work_.drops.erase(work_.drops.begin() + static_cast<long>(i));
            c = true;
            ui.popId();
            break;
        }
        ui.popId();
        y += ui.s(52);
    }
    UIContext::ButtonStyle st;
    st.small = true;
    if (ui.button("adddrop", {x, y, w, ui.s(46)}, "Ajouter un objet", st)) {
        work_.drops.push_back({"energy_orb", 0.5f});
        c = true;
    }
    y += ui.s(56);
    ui.label("À sa mort", {x, y}, 21, ui.theme().accent, Align::Left, FontFace::Bold);
    y += ui.s(34);
    auto ids = PropertyEditor::enemyIds(g_);
    ids.insert(ids.begin(), "");
    c |= stringCycle(ui, "dspawn", {x, y, w, ui.s(46)}, "Se divise en", work_.deathSpawnId, ids);
    y += ui.s(54);
    float dc = static_cast<float>(work_.deathSpawnCount);
    FieldRow row2{ui, x, w, w * 0.45f, y};
    if (ui.numberField("dcount", row2.field("Nombre"), dc, 1, 0, 6, "%.0f")) {
        work_.deathSpawnCount = static_cast<int>(dc);
        c = true;
    }
    if (c) changed();
}

void EnemyEditorScene::tabBoss(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    bool c = false;
    c |= ui.toggle("isboss", {x, y, w, ui.s(44)}, "Mini-boss (phases, jauge de vie)", work_.isBoss);
    y += ui.s(52);
    if (!work_.isBoss) {
        if (c) changed();
        return;
    }
    FieldRow row{ui, x, w, w * 0.4f, y};
    c |= ui.textField("title", row.field("Titre affiché"), work_.bossTitle);
    for (size_t i = 0; i < work_.phases.size(); ++i) {
        if (ui.listItem("ph" + std::to_string(i), {x, y, w, ui.s(48)}, work_.phases[i].name, selectedPhase_ == static_cast<int>(i),
                        std::format("sous {:.0f}% PV", work_.phases[i].hpThreshold * 100)))
            selectedPhase_ = static_cast<int>(i);
        y += ui.s(54);
    }
    UIContext::ButtonStyle st;
    st.small = true;
    if (ui.button("addph", {x, y, w * 0.48f, ui.s(46)}, "Ajouter une phase", st)) {
        BossPhase p;
        p.name = "Phase " + std::to_string(work_.phases.size() + 1);
        p.hpThreshold = work_.phases.empty() ? 1.0f : std::max(0.05f, work_.phases.back().hpThreshold - 0.33f);
        for (const auto& a : work_.attacks) p.attacks.push_back(a.id);
        work_.phases.push_back(p);
        selectedPhase_ = static_cast<int>(work_.phases.size()) - 1;
        c = true;
    }
    UIContext::ButtonStyle d;
    d.small = true;
    d.danger = true;
    d.enabled = !work_.phases.empty();
    if (ui.button("rmph", {x + w * 0.52f, y, w * 0.48f, ui.s(46)}, "Retirer", d) && d.enabled) {
        work_.phases.erase(work_.phases.begin() + selectedPhase_);
        selectedPhase_ = std::max(0, selectedPhase_ - 1);
        c = true;
    }
    y += ui.s(60);
    if (!work_.phases.empty()) {
        selectedPhase_ = std::clamp(selectedPhase_, 0, static_cast<int>(work_.phases.size()) - 1);
        BossPhase& p = work_.phases[selectedPhase_];
        FieldRow pr{ui, x, w, w * 0.45f, y};
        c |= ui.textField("pname", pr.field("Nom"), p.name);
        c |= ui.slider("pth", {x, y, w, ui.s(44)}, "Débute sous (% PV)", p.hpThreshold, 0.05f, 1.0f, 0.05f, "%.2f");
        y += ui.s(52);
        c |= ui.numberField("pspd", pr.field("Vitesse x"), p.speedMultiplier, 0.05f, 0.2f, 4, "%.2f");
        c |= ui.numberField("pcd", pr.field("Recharge x"), p.cooldownMultiplier, 0.05f, 0.1f, 4, "%.2f");
        c |= ui.textField("ptext", pr.field("Texte d'entrée"), p.enterText);
        c |= ui.toggle("psum", {x, y, w, ui.s(44)}, "Invoque des renforts en entrant", p.summonOnEnter);
        y += ui.s(52);
        ui.label("Attaques de la phase (dans l'ordre du cycle)", {x, y}, 18, ui.theme().textDim);
        y += ui.s(30);
        for (const auto& a : work_.attacks) {
            bool on = std::find(p.attacks.begin(), p.attacks.end(), a.id) != p.attacks.end();
            if (ui.toggle("pa_" + a.id, {x, y, w, ui.s(42)}, a.name, on)) {
                if (on) p.attacks.push_back(a.id);
                else p.attacks.erase(std::remove(p.attacks.begin(), p.attacks.end(), a.id), p.attacks.end());
                c = true;
            }
            y += ui.s(48);
        }
    }
    if (c) {
        std::sort(work_.phases.begin(), work_.phases.end(), [](const auto& a, const auto& b) { return a.hpThreshold > b.hpThreshold; });
        changed();
    }
}

}  // namespace cosmic
