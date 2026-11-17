// Enemy editor property tabs.

#include <format>

#include "editors/enemy_editor/EnemyEditorScene.h"
#include "game/gameplay/BeamRenderer.h"
#include "editors/shared/PropertyEditor.h"
#include "game/gameplay/BossBadge.h"
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
    const EnemyEditAccess acc = access();
    if (admin_) {  // (a native for the player: the read-only banner above the preview)
        // 2.45: a native is read-only for the player (the campaign's: usable everywhere, never
        // modified); in the administration its definition is published online.
        std::string head, hint;
        Color col = ui.theme().accent;
        if (admin_ && adminMissing_) {
            head = "Révision en ligne absente de ce jeu";
            hint = "La révision " + std::to_string(adminBase_) +
                   " de cette entité n'est pas téléchargée : revenez à l'administration et actualisez, ou mettez le jeu à jour.";
            col = ui.theme().danger;
        } else if (admin_) {
            head = "Administration : définition de l'entité native";
            hint = "Ses réglages se modifient ici. Ses planches d'animation et la durée de leurs images restent celles du jeu (lecture seule). "
                   "« Publier… » met la définition en ligne.";
            col = ui.theme().warm;
        }
        const float bw = r.w - ui.s(20), tw = bw - ui.s(24);
        const float hs = ui.fitFont(head, FontFace::Bold, 19.0f, tw);
        const float headH = ui.text().measure(head, FontFace::Bold, ui.s(hs)).y;
        const float hintH = ui.text().measureWrapped(hint, FontFace::Body, ui.s(16), tw).y;
        Rect banner{r.x + ui.s(10), area.y, bw, ui.s(10) + headH + ui.s(8) + hintH + ui.s(10)};
        ui.r().fillRoundedRect(banner, ui.s(8), col.withAlpha(40));
        ui.label(head, {banner.x + ui.s(12), banner.y + ui.s(10)}, hs, col, Align::Left, FontFace::Bold);
        ui.labelWrapped(hint, {banner.x + ui.s(12), banner.y + ui.s(10) + headH + ui.s(8)}, tw, 16, ui.theme().textDim);
        area.y += banner.h + ui.s(8);
        area.h -= banner.h + ui.s(8);
    }
    float x = r.x + ui.s(16), w = r.w - ui.s(44);
    if (scrollTo_ >= 0.0f) {  // automated screenshots: the selected attack's settings in view
        ui.setScroll("scroll" + std::to_string(tab_), ui.s(scrollTo_));
        scrollTo_ = -1.0f;
    }
    ui.beginScroll("scroll" + std::to_string(tab_), area, ui.s(2200));
    float y = area.y + ui.s(6) - ui.scrollOffset();
    // Read-only: every field shown greyed, reachable, unchanged (the lists of attacks and phases
    // still choose what is shown).
    const bool roBefore = ui.readOnly;
    ui.readOnly = acc.readOnly();
    switch (tab_) {
        case 0: tabGeneral(x, y, w); break;
        case 1: tabBehaviour(x, y, w); break;
        case 2: tabAttacks(x, y, w); break;
        case 3: tabPowers(x, y, w); break;
        case 4: tabResistances(x, y, w); break;
        case 5: tabRewards(x, y, w); break;
        case 6: tabBoss(x, y, w); break;
    }
    ui.readOnly = roBefore;
    ui.endScroll();
    ui.popId();
}

void EnemyEditorScene::tabGeneral(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    FieldRow row{ui, x, w, w * 0.4f, y};
    bool c = false;
    const bool ro = ui.readOnly;
    const EnemyEditAccess acc = access();
    c |= ui.textField("name", row.field("Nom"), work_.name);
    ui.readOnly = ro || !acc.id;  // the administration keeps the native's identifier
    c |= ui.textField("id", row.field("Identifiant"), work_.id, "", 48);
    ui.readOnly = ro;
    c |= ui.textField("desc", row.field("Description"), work_.description, "", 300);
    c |= enumCycle(ui, "family", {x, y, w, ui.s(46)}, "Famille", work_.family, &familyLabel);
    y += ui.s(54);
    std::vector<std::string> anims;
    for (const auto& a : PropertyEditor::assetFiles("animations", ".json"))
        if (auto j = g_.app.resources().json(a); j && j->contains("clips")) anims.push_back(a);
    ui.readOnly = ro || !acc.animation;  // a native's sheet stays the game's
    if (stringCycle(ui, "anim", {x, y, w, ui.s(46)}, "Sprites", work_.animation, anims)) {
        c = true;
        loadAnimation();
    }
    ui.readOnly = ro;
    y += ui.s(54);
    c |= ui.numberField("sw", row.field("Hitbox largeur"), work_.size.x, 1, 2, 400, "%.0f");
    c |= ui.numberField("sh", row.field("Hitbox hauteur"), work_.size.y, 1, 2, 400, "%.0f");
    c |= ui.numberField("scale", row.field("Échelle du sprite"), work_.scale, 0.1f, 0.25f, 4, "%.2f");
    {
        // Colour the sprite is multiplied by (empty = its own colours).
        std::string tint = work_.tint == Color::white() ? "" : work_.tint.toHex();
        if (PropertyEditor::colorPicker(ui, "tint", x, y, w, w * 0.4f, "Teinte du sprite", tint)) {
            work_.tint = tint.empty() ? Color::white() : Color::fromHex(tint, Color::white());
            c = true;
        }
    }
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
            for (const char* drawn : {"feather", "soul", "bone"}) proj.push_back(drawn);  // drawn by the game (2.56, 2.57)
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
        if (a.type == AttackType::Leap)
            c |= ui.numberField("leapt", row.field("Durée du saut (s)"), a.leapTime, 0.05f, 0.3f, 2.0f, "%.2f");
        // Smarter attacks (bosses and anyone else)
        if (a.isProjectileType() || a.type == AttackType::Lunge || a.type == AttackType::Leap)
            c |= ui.numberField("lead", row.field("Anticipation (0 = vise le joueur, 1 = où il va)"), a.lead, 0.05f, 0, 1, "%.2f");
        if (a.type == AttackType::Projectile || a.type == AttackType::Spread || a.type == AttackType::Arc) {
            float b = static_cast<float>(a.bursts);
            if (ui.numberField("bursts", row.field("Salves"), b, 1, 1, 8, "%.0f")) a.bursts = static_cast<int>(b), c = true;
            if (a.bursts > 1) c |= ui.numberField("bint", row.field("Intervalle entre salves (s)"), a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
            c |= ui.numberField("homing", row.field("Tête chercheuse (rad/s, 0 = non)"), a.homing, 0.25f, 0, 12, "%.2f");
            if (a.homing > 0.0f) c |= ui.numberField("homt", row.field("Durée du guidage (s)"), a.homingTime, 0.1f, 0, 6, "%.1f");
        }
        if (a.type == AttackType::Rain || a.type == AttackType::Eruption)
            c |= ui.numberField("stag", row.field("Impacts successifs en ligne (s, 0 = ensemble)"), a.stagger, 0.02f, 0, 1, "%.2f");
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
            // Texture of the beam (animations/beams.json) and its thickness, with a live preview.
            const auto& styles = BeamRenderer::styles();
            int si = 0;
            for (size_t k = 0; k < styles.size(); ++k)
                if (styles[k] == a.beamTexture) si = static_cast<int>(k);
            if (ui.cycle("ltex", {x, y, w, ui.s(46)}, "Texture du rayon", si, BeamRenderer::styleLabels())) {
                a.beamTexture = styles[static_cast<size_t>(si)];
                c = true;
            }
            y += ui.s(54);
            if (!a.beamTexture.empty()) {
                c |= ui.numberField("lwid", row.field("Épaisseur du rayon (px)"), a.beamWidth, 1, 4, 48, "%.0f");
                Rect pv{x, y, w, ui.s(44)};
                ui.r().fillRoundedRect(pv, ui.s(8), {6, 9, 20, 255});
                WorldView saved = ui.r().view();
                ui.r().setView(WorldView{});
                ui.r().pushClip(pv);
                BeamRenderer::draw(ui.r(), g_.app.resources(), a.beamTexture, {pv.x + ui.s(10), pv.center().y}, 0.0f,
                                   pv.w - ui.s(20), std::min(a.beamWidth * ui.s(1.6f), pv.h - ui.s(8)), ui.time());
                ui.r().popClip();
                ui.r().setView(saved);
                y += pv.h + ui.s(10);
            }
        }
        if (a.isGuardianType()) {
            // 2.19: the attacks of the guardians of Aethra. A value left at 0 takes the type's
            // default (see game/enemies/GuardianAttacks.cpp).
            auto num = [&](const char* id, const char* label, float& v, float step, float lo, float hi, const char* fmt) {
                c |= ui.numberField(id, row.field(label), v, step, lo, hi, fmt);
            };
            auto whole = [&](const char* id, const char* label, int& v, int lo, int hi) {
                float f = static_cast<float>(v);
                if (ui.numberField(id, row.field(label), f, 1, static_cast<float>(lo), static_cast<float>(hi), "%.0f")) {
                    v = static_cast<int>(f);
                    c = true;
                }
            };
            bool shots = a.type == AttackType::Cage || a.type == AttackType::Orbit || a.type == AttackType::Spiral;
            if (shots) {
                std::vector<std::string> proj;
                if (auto pa = g_.app.resources().animation("animations/projectiles.json"))
                    for (const auto& [k, _] : pa->clips) proj.push_back(k);
                for (const char* drawn : {"feather", "soul", "bone"}) proj.push_back(drawn);  // drawn by the game (2.56, 2.57)
                std::sort(proj.begin(), proj.end());
                c |= stringCycle(ui, "gproj", {x, y, w, ui.s(46)}, a.type == AttackType::Orbit ? "Flammes" : "Projectile", a.projectile, proj);
                y += ui.s(54);
            }
            switch (a.type) {
                case AttackType::Tracker:
                    num("gdur", "Poursuite sous le sol (s)", a.duration, 0.1f, 0, 10, "%.1f");
                    num("gspd", "Vitesse de la racine (max 130)", a.projectileSpeed, 5, 20, 130, "%.0f");
                    num("grad", "Demi-largeur du pic", a.radius, 1, 0, 60, "%.0f");
                    num("ghgt", "Hauteur du pic", a.height, 2, 0, 200, "%.0f");
                    break;
                case AttackType::Cage:
                    num("gdur", "Durée de la cage (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    num("grad", "Demi-écart des murs", a.radius, 5, 0, 400, "%.0f");
                    num("ghgt", "Hauteur des murs", a.height, 4, 0, 400, "%.0f");
                    num("gspd", "Chute des spores", a.projectileSpeed, 5, 10, 300, "%.0f");
                    num("gint", "Intervalle des spores (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    break;
                case AttackType::Vortex:
                case AttackType::Well:
                    num("gdur", "Durée (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    num("gpull", "Attraction (px/s, max 140)", a.pull, 5, 0, 140, "%.0f");
                    if (a.type == AttackType::Well) num("grad", "Rayon du cœur", a.radius, 1, 0, 80, "%.0f");
                    break;
                case AttackType::Mirror:
                    num("gdur", "Vie des doubles (s, 0 = jusqu'au coup)", a.duration, 0.5f, 0, 60, "%.1f");
                    break;
                case AttackType::FrostFloor:
                    num("gdur", "Durée du verglas (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    num("ggrip", "Adhérence (1 = normale)", a.grip, 0.05f, 0, 1, "%.2f");
                    break;
                case AttackType::Prism:
                    whole("gbounce", "Rebonds", a.bounces, 0, 6);
                    num("glen", "Longueur d'un segment", a.laserLength, 10, 20, 2000, "%.0f");
                    break;
                case AttackType::Orbit:
                    num("gdur", "Durée de la couronne (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    whole("gcnt", "Flammes", a.count, 1, 24);
                    num("grad", "Rayon le plus large", a.radius, 5, 0, 600, "%.0f");
                    num("gspr", "Rotation (°/s)", a.spread, 5, 0, 360, "%.0f");
                    whole("garms", "Bras de spirale (0 = aucun)", a.arms, 0, 8);
                    break;
                case AttackType::Spiral:
                    num("gdur", "Durée de la spirale (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    whole("garms", "Bras", a.arms, 0, 8);
                    num("gspr", "Rotation par salve (°)", a.spread, 1, 0, 90, "%.0f");
                    num("gint", "Intervalle des salves (s)", a.burstInterval, 0.02f, 0.05f, 3, "%.2f");
                    num("gspd", "Vitesse des tirs", a.projectileSpeed, 5, 10, 1000, "%.0f");
                    break;
                case AttackType::Flood:
                    num("gdur", "Lave haute pendant (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    num("ghgt", "Hauteur de la lave", a.height, 4, 0, 400, "%.0f");
                    break;
                case AttackType::Blink:
                    num("grad", "Rayon du coup", a.radius, 2, 0, 200, "%.0f");
                    break;
                case AttackType::Ring:
                    num("grad", "Rayon final", a.radius, 10, 0, 1200, "%.0f");
                    num("gspd", "Vitesse de l'anneau", a.projectileSpeed, 5, 40, 600, "%.0f");
                    num("gspr", "Ouverture (°)", a.spread, 2, 0, 180, "%.0f");
                    break;
                default: break;
            }
        }
        if (a.isDragonType()) {
            // 2.50: the attacks of the dragon (Pyrrhax). A value left at 0 takes the type's
            // default (see game/enemies/DragonAttacks.cpp).
            auto num = [&](const char* id, const char* label, float& v, float step, float lo, float hi, const char* fmt) {
                c |= ui.numberField(id, row.field(label), v, step, lo, hi, fmt);
            };
            auto whole = [&](const char* id, const char* label, int& v, int lo, int hi) {
                float f = static_cast<float>(v);
                if (ui.numberField(id, row.field(label), f, 1, static_cast<float>(lo), static_cast<float>(hi), "%.0f")) {
                    v = static_cast<int>(f);
                    c = true;
                }
            };
            if (a.type == AttackType::CloudShot || a.type == AttackType::Gust) {
                std::vector<std::string> proj;
                if (auto pa = g_.app.resources().animation("animations/projectiles.json"))
                    for (const auto& [k, _] : pa->clips) proj.push_back(k);
                std::sort(proj.begin(), proj.end());
                c |= stringCycle(ui, "dproj", {x, y, w, ui.s(46)}, a.type == AttackType::Gust ? "Braises" : "Boule de feu", a.projectile, proj);
                y += ui.s(54);
            }
            switch (a.type) {
                case AttackType::Dive:
                    num("dspd", "Vitesse du piqué", a.lungeSpeed, 10, 300, 1600, "%.0f");
                    whole("dcnt", "Piqués enchaînés", a.count, 1, 4);
                    num("drad", "Au-delà de la cible (px)", a.radius, 10, 0, 800, "%.0f");
                    num("dint", "Visée du piqué suivant (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    break;
                case AttackType::Perch:
                    num("dspd", "Vitesse de la descente", a.lungeSpeed, 10, 240, 1600, "%.0f");
                    num("drad", "Portée du souffle au sol (px)", a.radius, 10, 0, 800, "%.0f");
                    num("dhgt", "Hauteur du nuage", a.height, 2, 0, 200, "%.0f");
                    num("ddur", "Durée du nuage (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    whole("darms", "Côtés du souffle (1 ou 2)", a.arms, 0, 2);
                    num("dvul", "Dégâts reçus posé (x, 0 = 1,5)", a.vulnerability, 0.1f, 0, 4, "%.1f");
                    break;
                case AttackType::CloudShot:
                    whole("dcnt", "Boules de feu", a.count, 1, 8);
                    num("dgrav", "Gravité (0 = tir droit)", a.gravity, 20, 0, 2000, "%.0f");
                    num("dpspd", "Vitesse des boules", a.projectileSpeed, 10, 40, 1000, "%.0f");
                    num("dspr", a.gravity > 0.0f ? "Espacement des impacts (px)" : "Écart (°)", a.spread, 1, 0, 200, "%.0f");
                    num("drad", "Rayon du nuage", a.radius, 2, 0, 200, "%.0f");
                    num("ddur", "Durée du nuage (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    break;
                case AttackType::Gust:
                    num("ddur", "Durée de la rafale (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    num("dpull", "Poussée (px/s, max 140)", a.pull, 5, 0, 140, "%.0f");
                    whole("dcnt", "Braises par salve", a.count, 1, 8);
                    num("dint", "Intervalle des salves (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    num("dpspd", "Vitesse des braises", a.projectileSpeed, 10, 40, 1000, "%.0f");
                    break;
                default: break;
            }
            if (a.type != AttackType::Gust)
                c |= ui.numberField("dlead", row.field("Anticipation (0 = vise le joueur, 1 = où il va)"), a.lead, 0.05f, 0, 1, "%.2f");
        }
        if (a.isGolemType()) {
            // 2.52: the attacks of the golem (the Monolithe). A value left at 0 takes the type's
            // default (see game/enemies/GolemAttacks.cpp).
            auto num = [&](const char* id, const char* label, float& v, float step, float lo, float hi, const char* fmt) {
                c |= ui.numberField(id, row.field(label), v, step, lo, hi, fmt);
            };
            auto whole = [&](const char* id, const char* label, int& v, int lo, int hi) {
                float f = static_cast<float>(v);
                if (ui.numberField(id, row.field(label), f, 1, static_cast<float>(lo), static_cast<float>(hi), "%.0f")) {
                    v = static_cast<int>(f);
                    c = true;
                }
            };
            switch (a.type) {
                case AttackType::Shell:
                    num("cspd", "Vitesse de la carapace", a.lungeSpeed, 10, 160, 900, "%.0f");
                    whole("cbnc", "Rebonds sur les murs (0 = 1)", a.bounces, 0, 4);
                    num("carm", "Dégâts reçus en carapace (x, 0 = 0,35)", a.armor, 0.05f, 0, 1, "%.2f");
                    num("cdur", "Étourdi après la roulade (s)", a.duration, 0.1f, 0, 10, "%.1f");
                    num("cvul", "Dégâts reçus étourdi (x, 0 = 1,5)", a.vulnerability, 0.1f, 0, 4, "%.1f");
                    break;
                case AttackType::Quake:
                    num("qdur", "Durée du séisme (s)", a.duration, 0.1f, 0, 20, "%.1f");
                    num("qint", "Intervalle des secousses (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    whole("qcnt", "Fissures par secousse", a.count, 1, 7);
                    num("qspr", "Écart entre les fissures (px)", a.spread, 5, 0, 400, "%.0f");
                    num("qdel", "Délai avant l'éruption (s)", a.stagger, 0.05f, 0, 1, "%.2f");
                    num("qrad", "Demi-largeur d'une fissure", a.radius, 1, 0, 80, "%.0f");
                    num("qhgt", "Hauteur de l'éruption", a.height, 2, 0, 200, "%.0f");
                    whole("qarms", "Rochers par secousse (0 = 2)", a.arms, 0, 8);
                    num("qprad", "Rayon d'un rocher", a.projectileRadius, 1, 0, 40, "%.0f");
                    break;
                case AttackType::Skyfall:
                    num("sdur", "Hors de vue (s)", a.duration, 0.1f, 0, 10, "%.1f");
                    num("srad", "Écrasement autour de lui (px)", a.radius, 2, 0, 200, "%.0f");
                    whole("scnt", "Ondes de choc", a.count, 1, 4);
                    num("sint", "Intervalle des ondes (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    num("sspd", "Vitesse des ondes", a.projectileSpeed, 10, 60, 1000, "%.0f");
                    num("sprad", "Hauteur des ondes (rayon)", a.projectileRadius, 1, 0, 40, "%.0f");
                    break;
                default: break;
            }
        }
        if (a.isPhoenixType()) {
            // 2.56: the attacks of the phoenix (the Phénix). A value left at 0 takes the type's
            // default (see game/enemies/PhoenixAttacks.cpp).
            auto num = [&](const char* id, const char* label, float& v, float step, float lo, float hi, const char* fmt) {
                c |= ui.numberField(id, row.field(label), v, step, lo, hi, fmt);
            };
            auto whole = [&](const char* id, const char* label, int& v, int lo, int hi) {
                float f = static_cast<float>(v);
                if (ui.numberField(id, row.field(label), f, 1, static_cast<float>(lo), static_cast<float>(hi), "%.0f")) {
                    v = static_cast<int>(f);
                    c = true;
                }
            };
            switch (a.type) {
                case AttackType::Sunbeam:
                    whole("pcnt", "Colonnes (par côté)", a.count, 1, 11);
                    num("pspr", "Écart entre les colonnes (px)", a.spread, 5, 0, 400, "%.0f");
                    num("pdel", "Première colonne après (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    num("pstg", "Entre deux colonnes (s)", a.stagger, 0.02f, 0, 1, "%.2f");
                    num("prad", "Demi-largeur d'une colonne", a.radius, 1, 0, 80, "%.0f");
                    num("pdur", "Durée d'une colonne (s)", a.duration, 0.05f, 0, 3, "%.2f");
                    whole("parms", "Depuis les deux murs (2)", a.arms, 0, 2);
                    c |= ui.numberField("plead", row.field("Anticipation (0 = vise le joueur, 1 = où il va)"), a.lead, 0.05f, 0, 1, "%.2f");
                    break;
                case AttackType::Sweep: {
                    std::vector<std::string> proj;
                    if (auto pa = g_.app.resources().animation("animations/projectiles.json"))
                        for (const auto& [k, _] : pa->clips) proj.push_back(k);
                    proj.push_back("feather");
                    std::sort(proj.begin(), proj.end());
                    c |= stringCycle(ui, "pproj", {x, y, w, ui.s(46)}, "Plumes de la lame", a.projectile, proj);
                    y += ui.s(54);
                    whole("pcnt", "Lames (basse et haute)", a.count, 1, 4);
                    num("pint", "Entre deux lames (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    num("pspd", "Vitesse de la lame", a.projectileSpeed, 10, 120, 1000, "%.0f");
                    num("phgt", "Hauteur de la lame basse", a.height, 2, 0, 80, "%.0f");
                    num("pprad", "Rayon d'une plume", a.projectileRadius, 1, 0, 20, "%.0f");
                    break;
                }
                default: break;
            }
        }
        if (a.isLicheType()) {
            // 2.57: the spells of the lich (the Liche). A value left at 0 takes the type's default
            // (see game/enemies/LicheAttacks.cpp).
            auto num = [&](const char* id, const char* label, float& v, float step, float lo, float hi, const char* fmt) {
                c |= ui.numberField(id, row.field(label), v, step, lo, hi, fmt);
            };
            auto whole = [&](const char* id, const char* label, int& v, int lo, int hi) {
                float f = static_cast<float>(v);
                if (ui.numberField(id, row.field(label), f, 1, static_cast<float>(lo), static_cast<float>(hi), "%.0f")) {
                    v = static_cast<int>(f);
                    c = true;
                }
            };
            switch (a.type) {
                case AttackType::Glyph:
                    whole("lcnt", "Runes (par côté avec 2 bras)", a.count, 1, 12);
                    num("lspr", "Écart entre les runes (px)", a.spread, 5, 0, 300, "%.0f");
                    num("larm", "Armée après (s)", a.burstInterval, 0.05f, 0.05f, 3, "%.2f");
                    num("lstg", "Entre deux runes (s)", a.stagger, 0.02f, 0, 1, "%.2f");
                    num("ldur", "Reste armée (s)", a.duration, 0.1f, 0, 12, "%.1f");
                    num("lrad", "Demi-largeur d'une rune", a.radius, 1, 0, 60, "%.0f");
                    num("lhgt", "Hauteur de son éclat", a.height, 2, 0, 200, "%.0f");
                    whole("larms", "Autour du joueur (2)", a.arms, 0, 2);
                    c |= ui.numberField("llead", row.field("Anticipation (0 = vise le joueur, 1 = où il va)"), a.lead, 0.05f, 0, 1, "%.2f");
                    break;
                case AttackType::Hex:
                    num("hrad", "Rayon du cercle", a.radius, 2, 0, 200, "%.0f");
                    num("hdur", "Durée de la malédiction (s)", a.duration, 0.25f, 0, 12, "%.2f");
                    num("hcur", "Hauteur des sauts maudits (part)", a.curse, 0.02f, 0, 1, "%.2f");
                    c |= ui.numberField("hlead", row.field("Anticipation (0 = vise le joueur, 1 = où il va)"), a.lead, 0.05f, 0, 1, "%.2f");
                    break;
                default: break;
            }
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
    c |= ui.numberField("enrage", row.field("Rage (s, 0 = jamais)"), work_.enrageTime, 5, 0, 600, "%.0f");
    {
        // Aspect on the level progress bar (and in the level editor): its portrait or a symbol.
        ui.label("Aspect sur la barre de progression", {x, y}, 18, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        const auto& ids = BossBadge::icons();
        int cur = 0;
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == work_.barIcon) cur = static_cast<int>(i);
        float preview = ui.s(64);
        if (ui.cycle("baricon", {x, y, w - preview - ui.s(12), ui.s(46)}, "Icône", cur, BossBadge::iconLabels())) {
            work_.barIcon = ids[static_cast<size_t>(cur)];
            c = true;
        }
        Rect pv{x + w - preview, y - ui.s(6), preview, preview};
        ui.r().fillRoundedRect(pv, ui.s(10), {6, 9, 20, 255});
        BossBadge::draw(ui.r(), g_.app.resources(), work_, pv.center(), ui.s(20), false, ui.time());
        y += ui.s(66);
        c |= PropertyEditor::colorPicker(ui, "barcolor", x, y, w - preview - ui.s(12), (w - preview) * 0.4f, "Couleur", work_.barColor);
    }
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
        {
            float cb = static_cast<float>(p.combo);
            if (ui.numberField("pcombo", pr.field("Attaques enchaînées (combo)"), cb, 1, 1, 5, "%.0f")) p.combo = static_cast<int>(cb), c = true;
            c |= ui.numberField("ptel", pr.field("Durée d'annonce x"), p.telegraphMultiplier, 0.05f, 0.4f, 2, "%.2f");
            // 2.33: rage counted from the start of this phase (Singularis's Supernova).
            c |= ui.numberField("prage", pr.field("Rage dans la phase (s, 0 = non)"), p.enrageTime, 5, 0, 600, "%.0f");
        }
        ui.label("Attaques de la phase (le boss choisit selon la situation)", {x, y}, 18, ui.theme().textDim);
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
