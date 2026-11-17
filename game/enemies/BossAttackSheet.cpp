#include "game/enemies/BossAttackSheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cosmic {

std::string sheetNumber(float v, int decimals) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", std::max(0, decimals), v);
    std::string t = b;
    if (t.find('.') != std::string::npos) {
        while (!t.empty() && t.back() == '0') t.pop_back();
        if (!t.empty() && t.back() == '.') t.pop_back();
    }
    if (t == "-0") t = "0";
    if (auto dot = t.find('.'); dot != std::string::npos) t[dot] = ',';
    return t;
}

std::string sheetTimes(float v) { return "×" + sheetNumber(v, 2); }

namespace {

/// The value the attack uses: its own when set (> 0), else its type's default.
float orDefault(float v, float d) { return v > 0.0f ? v : d; }

std::string secs(float v, int decimals = 2) { return sheetNumber(v, decimals) + " s"; }
std::string px(float v) { return sheetNumber(v, 0) + " px"; }
std::string percent(float ratio) { return sheetNumber(ratio * 100.0f, 0) + " %"; }

/// « 1 », « 1 et 3 », « 1, 2 et 4 ».
std::string joinNumbers(const std::vector<int>& phases) {
    std::string out;
    for (size_t i = 0; i < phases.size(); ++i) {
        if (i > 0) out += i + 1 == phases.size() ? " et " : ", ";
        out += std::to_string(phases[i] + 1);
    }
    return out;
}

/// Its value in each phase, joined: « 0,6 s » when every phase gives the same,
/// « 0,6 s (phase 1), 0,45 s (phase 3) » otherwise; the last stand counts as its last phase.
std::string perPhase(const EnemyDefinition& d, const std::vector<int>& phases, bool final, const std::function<float(int)>& value) {
    std::vector<std::pair<std::string, std::string>> parts;  // value, where
    for (int p : phases) parts.push_back({secs(value(p)), "phase " + std::to_string(p + 1)});
    if (final) {
        const int last = std::max(0, static_cast<int>(d.phases.size()) - 1);
        parts.push_back({secs(value(last)), std::string("dernier souffle")});
    }
    if (parts.empty()) return {};
    bool same = true;
    for (const auto& p : parts) same = same && p.first == parts.front().first;
    if (same) return parts.front().first;
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += ", ";
        out += p.first + " (" + p.second + ")";
    }
    return out;
}

bool usesActiveWindow(AttackType t) {
    // The types whose attack lasts its `active` time (the others set their own: an effect's
    // duration, a dash's distance, a landing…).
    switch (t) {
        case AttackType::Melee:
        case AttackType::Projectile:
        case AttackType::Spread:
        case AttackType::Arc:
        case AttackType::Slam:
        case AttackType::Laser:
        case AttackType::Summon:
        case AttackType::Rain:
        case AttackType::Eruption: return true;
        default: return false;
    }
}

}  // namespace

std::string attackSummary(const AttackDef& a) {
    std::string details = std::string(attackTypeLabel(a.type)) + "  ·  Dégâts " + sheetNumber(a.damage) + "  ·  Annonce " + sheetNumber(a.telegraph, 2) +
                          " s  ·  Recharge " + sheetNumber(a.cooldown, 2) + " s";
    // 2.50: the dragon's attacks say what they chain or leave behind.
    if (a.type == AttackType::Dive) {
        if (a.count > 1) details += "  ·  " + std::to_string(a.count) + " piqués enchaînés";
    } else if (a.type == AttackType::Gust) {
        details += "  ·  Repousse " + sheetNumber(orDefault(a.duration, 2.2f), 1) + " s";
    } else if (a.type == AttackType::Shell) {  // 2.52: the golem's attacks
        const int bounces = a.bounces > 0 ? a.bounces : 1;
        details += "  ·  " + std::to_string(bounces) + (bounces > 1 ? " rebonds" : " rebond") + "  ·  Carapace : dégâts reçus " +
                   sheetTimes(orDefault(a.armor, 0.35f)) + "  ·  Étourdi : dégâts reçus " + sheetTimes(orDefault(a.vulnerability, 1.5f));
    } else if (a.type == AttackType::Quake) {
        details += "  ·  " + std::to_string(a.count) + (a.count > 1 ? " fissures par secousse" : " fissure par secousse") + "  ·  Séisme " +
                   sheetNumber(orDefault(a.duration, 3.6f), 1) + " s";
    } else if (a.type == AttackType::Skyfall) {
        details += "  ·  " + std::to_string(a.count) + (a.count > 1 ? " ondes de choc" : " onde de choc");
    } else if (a.type == AttackType::Sunbeam) {  // 2.56: the phoenix's attacks
        const int columns = a.arms >= 2 ? 2 * a.count + 1 : a.count;
        details += "  ·  " + std::to_string(columns) + (columns > 1 ? " colonnes" : " colonne") + (a.arms >= 2 ? " depuis les deux murs" : " en marche");
    } else if (a.type == AttackType::Sweep) {
        details += "  ·  " + std::to_string(a.count) + (a.count > 1 ? " lames, basse et haute" : " lame, basse ou haute");
    } else if (a.type == AttackType::Glyph) {  // 2.57: the lich's spells
        const int runes = a.arms >= 2 ? 2 * a.count + 1 : a.count;
        details += "  ·  " + std::to_string(runes) + (runes > 1 ? " runes" : " rune") + (a.arms >= 2 ? " autour du joueur" : " en ligne");
    } else if (a.type == AttackType::Hex) {
        details += "  ·  Sauts alourdis " + sheetNumber(orDefault(a.duration, 5.0f), 1) + " s";
    } else if (a.type == AttackType::Press) {  // 2.58: the war machine's machinery
        if (a.arms == 2) details += "  ·  Toutes sauf une";
        else if (a.arms == 3) details += "  ·  En marche, l'une après l'autre";
        else details += "  ·  Une sur deux, " + std::to_string(a.count) + (a.count > 1 ? " vagues" : " vague");
    } else if (a.type == AttackType::Missile) {
        details += "  ·  " + std::to_string(a.count) + (a.count > 1 ? " missiles verrouillés" : " missile verrouillé");
    } else if (a.type == AttackType::Belt) {
        details += std::string(a.arms == 1 ? "  ·  Loin de lui" : "  ·  Vers lui") + "  ·  " + sheetNumber(orDefault(a.duration, 5.0f), 1) + " s";
    } else if (a.count > 1) {
        details += "  ·  " + std::to_string(a.count) + " projectiles";
    }
    if (a.bursts > 1) details += "  ·  " + std::to_string(a.bursts) + " salves";
    if (a.type == AttackType::Perch || a.type == AttackType::CloudShot)
        details += "  ·  Nuage de feu " + sheetNumber(orDefault(a.duration, a.type == AttackType::Perch ? 3.5f : 3.0f), 1) + " s";
    if (a.type == AttackType::Perch) details += "  ·  Posé : dégâts reçus " + sheetTimes(orDefault(a.vulnerability, 1.5f));
    return details;
}

std::string attackTypeDescription(AttackType t) {
    switch (t) {
        case AttackType::Contact: return "Dégâts infligés au simple contact du corps du boss.";
        case AttackType::Melee: return "Le boss frappe au corps à corps, juste devant lui : reculez hors de sa portée pendant l'annonce.";
        case AttackType::Lunge: return "Le boss bondit sur vous : écartez-vous ou sautez au moment où il s'élance.";
        case AttackType::Projectile: return "Des tirs visés sur vous : changez de hauteur ou de place pendant leur vol.";
        case AttackType::Spread: return "Une salve de projectiles en éventail : glissez-vous entre deux tirs ou sautez par-dessus.";
        case AttackType::Arc: return "Des tirs en cloche qui retombent là où vous êtes : changez de place pendant l'annonce.";
        case AttackType::Slam:
            return "Le boss frappe le sol : un impact sous lui et deux ondes de choc qui filent au ras du sol de chaque côté. Sautez par-dessus.";
        case AttackType::Charge: return "Le boss fonce droit devant lui au ras du sol : sautez par-dessus ou montez sur une plateforme.";
        case AttackType::Laser: return "Un rayon tiré vers vous, qui peut balayer l'arène : sortez de sa ligne pendant l'annonce.";
        case AttackType::Summon: return "Le boss appelle des renforts autour de lui.";
        case AttackType::Rain: return "Des projectiles tombent d'en haut sur des points marqués avant l'impact : quittez les marques.";
        case AttackType::Eruption: return "Le sol jaillit sur des points marqués avant l'éruption : quittez les marques.";
        case AttackType::Leap: return "Le boss saute sur vous et retombe en frappant le sol : deux ondes de choc partent de chaque côté à l'atterrissage.";
        case AttackType::Tracker: return "Une bosse court sous le sol jusqu'à vous, puis un pic jaillit à l'endroit atteint : changez de place.";
        case AttackType::Cage: return "Deux murs s'élèvent de part et d'autre de vous et vous enferment, des projectiles tombent entre eux.";
        case AttackType::Vortex: return "Le boss vous aspire vers lui : une aspiration limitée, courir dans l'autre sens suffit.";
        case AttackType::Mirror: return "Des doubles translucides du boss copient ses attaques (dégâts réduits) ; un coup suffit à briser un double.";
        case AttackType::FrostFloor: return "Le sol de l'arène gèle un moment : vous glissez bien plus longtemps.";
        case AttackType::Prism: return "Un rayon qui rebondit sur les murs et le plafond ; son trajet est montré pendant l'annonce.";
        case AttackType::Orbit: return "Des tirs tournent autour du boss ; leur cercle s'élargit puis se resserre.";
        case AttackType::Spiral: return "Des tirs partent en spirale tournante, sur plusieurs bras.";
        case AttackType::Flood: return "Le fond de l'arène est inondé jusqu'aux plateformes un moment, puis l'inondation redescend : montez.";
        case AttackType::Well: return "Un puits gravitationnel vous attire et dévie vos tirs ; son cœur blesse.";
        case AttackType::Blink: return "Une marque apparaît derrière vous ; le boss s'y téléporte et frappe autour de lui.";
        case AttackType::Ring: return "Un anneau s'étend depuis le boss avec une seule ouverture : passez par l'ouverture.";
        case AttackType::Dive:
            return "Une ligne se trace vers l'endroit où vous serez, puis le boss fonce le long de cette ligne, la dépasse et remonte.";
        case AttackType::Perch: return "Le boss se pose là où vous êtes, souffle un nuage de feu durable au ras du sol, puis repart.";
        case AttackType::CloudShot: return "Des boules de feu qui laissent un nuage brûlant là où elles éclatent.";
        case AttackType::Gust: return "Les ailes du boss vous repoussent : une poussée limitée, que vous pouvez remonter en courant.";
        case AttackType::Shell:
            return "Le boss se roule en carapace et fonce sur vous en rebondissant sur les murs : blindé pendant le roulement, étourdi ensuite.";
        case AttackType::Quake: return "L'arène tremble : des fissures s'ouvrent sous vous puis éclatent, des rochers tombent du plafond sur des marques.";
        case AttackType::Skyfall: return "Le boss bondit hors de vue, son ombre vous suit puis il retombe dessus : des ondes de choc filent le long du sol.";
        case AttackType::Sunbeam: return "Des colonnes de lumière tombent de la voûte sur des marques, l'une après l'autre.";
        case AttackType::Sweep: return "Les ailes du boss lancent une lame de plumes à travers l'arène : basse, sautez par-dessus ; haute, restez au sol.";
        case AttackType::Glyph:
            return "Des runes tracées sur le sol où vous vous tenez : armées, elles éclatent quand vous marchez dessus, toutes éclatent à la fin.";
        case AttackType::Hex: return "Un cercle de runes se referme autour de vous : encore dedans quand il se ferme, vos sauts s'alourdissent un moment. Un pas hors du cercle suffit.";
        case AttackType::Press: return "Les presses de la fonderie du boss s'abattent de la voûte ; des bandes au sol montrent où avant leur chute.";
        case AttackType::Missile:
            return "Des réticules se posent sur vous et vous suivent, puis se verrouillent ; un missile tombe sur chacun. Bouger suffit.";
        case AttackType::Belt: return "Les tapis roulants de la fonderie du boss démarrent : ils vous emportent moins vite que vous ne courez, un saut vous libère.";
        case AttackType::Count: break;
    }
    return {};
}

std::string attackDescription(const AttackDef& a) {
    std::string out = attackTypeDescription(a.type);
    // What its definition adds (each sentence only when the value is set).
    if (a.lead > 0.0f && a.type != AttackType::Dive) out += " Elle vise l'endroit où vous serez.";
    if (a.homing > 0.0f) out += " Ses tirs vous suivent un moment.";
    if (a.stagger > 0.0f && (a.type == AttackType::Rain || a.type == AttackType::Eruption)) out += " Les impacts se suivent le long d'une ligne.";
    if (a.explosionRadius > 0.0f) out += " Ses projectiles explosent à l'impact.";
    if (a.slow > 0.0f && a.slowDuration > 0.0f) out += " Un coup reçu vous ralentit.";
    if (a.bursts > 1) out += " Elle tire plusieurs salves de suite.";
    if (a.type == AttackType::Prism && a.arms >= 2) out += " Deux rayons partent de chaque côté et se croisent.";
    if (a.type == AttackType::Sunbeam) out += a.arms >= 2 ? " Les colonnes viennent des deux murs." : " Les colonnes avancent en marche.";
    if (a.type == AttackType::Press) {
        if (a.arms == 2) out += " Toutes tombent sauf une : la colonne épargnée s'éclaire en vert.";
        else if (a.arms == 3) out += " Elles tombent en marche, l'une après l'autre : revenez sous une presse déjà remontée.";
        else out += " Une sur deux tombe, puis l'autre moitié : entrez dans une colonne qui vient de remonter.";
    }
    if (a.type == AttackType::Belt) out += a.arms == 1 ? " Ils vous emportent loin de lui." : " Ils vous emportent vers lui.";
    return out;
}

std::vector<int> attackPhases(const EnemyDefinition& d, const std::string& attackId) {
    std::vector<int> out;
    if (d.phases.empty()) {
        if (d.attack(attackId)) out.push_back(0);
        return out;
    }
    for (size_t i = 0; i < d.phases.size(); ++i)
        if (std::find(d.phases[i].attacks.begin(), d.phases[i].attacks.end(), attackId) != d.phases[i].attacks.end())
            out.push_back(static_cast<int>(i));
    return out;
}

namespace {
bool isFinal(const EnemyDefinition& d, const std::string& attackId) {
    return std::find(d.finalAttacks.begin(), d.finalAttacks.end(), attackId) != d.finalAttacks.end();
}
}  // namespace

std::string attackWhere(const EnemyDefinition& d, const std::string& attackId) {
    const std::vector<int> phases = attackPhases(d, attackId);
    std::string out;
    if (d.phases.empty() && !phases.empty()) out = "Phase unique";
    else if (phases.size() == 1) out = "Phase " + joinNumbers(phases);
    else if (!phases.empty()) out = "Phases " + joinNumbers(phases);
    if (isFinal(d, attackId)) out += out.empty() ? std::string("Dernier souffle") : std::string("  ·  Dernier souffle");
    return out.empty() ? std::string("Aucune phase") : out;
}

float attackWindUp(const EnemyDefinition& d, const AttackDef& a, int phase) {
    // As Enemy::beginAttack: telegraph × the phase's announce multiplier ÷ its speed (never
    // under 0.3), at least 0.3 s for a boss.
    float k = 1.0f, speed = 1.0f;
    if (d.isBoss && phase >= 0 && phase < static_cast<int>(d.phases.size())) {
        k = d.phases[static_cast<size_t>(phase)].telegraphMultiplier;
        speed = d.phases[static_cast<size_t>(phase)].speedMultiplier;
    }
    float t = a.telegraph * k / std::max(0.3f, speed);
    if (d.isBoss) t = std::max(0.3f, t);
    return t;
}

float attackCooldown(const EnemyDefinition& d, const AttackDef& a, int phase) {
    // As Enemy::finishAttack: cooldown × the phase's cooldown multiplier.
    float k = 1.0f;
    if (d.isBoss && phase >= 0 && phase < static_cast<int>(d.phases.size())) k = d.phases[static_cast<size_t>(phase)].cooldownMultiplier;
    return a.cooldown * k;
}

std::vector<AttackFact> attackFacts(const EnemyDefinition& d, const AttackDef& a, const EnemyNameFn& nameOf) {
    std::vector<AttackFact> f;
    auto add = [&](std::string label, std::string value) { f.push_back({std::move(label), std::move(value)}); };
    const std::vector<int> phases = attackPhases(d, a.id);
    const bool final = isFinal(d, a.id);
    add("Type", attackTypeLabel(a.type));
    // Its phases, by name.
    {
        std::string names;
        for (int p : phases) {
            if (!names.empty()) names += ", ";
            names += std::to_string(p + 1);
            if (p < static_cast<int>(d.phases.size()) && !d.phases[static_cast<size_t>(p)].name.empty())
                names += " (" + d.phases[static_cast<size_t>(p)].name + ")";
        }
        if (final) names += names.empty() ? std::string("Dernier souffle") : std::string(", dernier souffle");
        if (d.phases.empty() && !phases.empty()) names = "Phase unique";
        add(phases.size() + (final ? 1 : 0) > 1 ? "Phases" : "Phase", names.empty() ? std::string("Aucune") : names);
    }
    add("Dégâts", sheetNumber(a.damage));
    add("Annonce", perPhase(d, phases, final, [&](int p) { return attackWindUp(d, a, p); }));
    if (usesActiveWindow(a.type)) add("Durée de l'attaque", secs(a.active));
    add("Récupération", perPhase(d, phases, final, [&](int p) {
            float k = d.isBoss && p >= 0 && p < static_cast<int>(d.phases.size()) ? d.phases[static_cast<size_t>(p)].cooldownMultiplier : 1.0f;
            return a.recover * k;
        }));
    add("Recharge", perPhase(d, phases, final, [&](int p) { return attackCooldown(d, a, p); }));
    if (a.minRange > 0.0f) add("Portée", "de " + px(a.minRange) + " à " + px(a.range));
    else add("Portée", "jusqu'à " + px(a.range));

    // What its type uses.
    switch (a.type) {
        case AttackType::Projectile:
        case AttackType::Spread:
        case AttackType::Arc:
        case AttackType::Rain:
        case AttackType::Eruption: {
            std::string n = std::to_string(std::max(1, a.count));
            if (a.bursts > 1) n += " × " + std::to_string(a.bursts) + " salves (une toutes les " + secs(a.burstInterval) + ")";
            add(a.type == AttackType::Eruption ? "Éruptions" : "Projectiles", n);
            if (a.type != AttackType::Eruption) add("Vitesse des projectiles", sheetNumber(a.projectileSpeed, 0) + " px/s");
            if (a.type == AttackType::Spread && a.count > 1 && a.spread > 0.0f) add("Écart entre deux tirs", sheetNumber(a.spread, 0) + "°");
            if ((a.type == AttackType::Rain || a.type == AttackType::Eruption) && a.count > 1)
                add("Écart entre deux impacts", px(std::max(a.spread, 24.0f)));
            if (a.type == AttackType::Rain) add("Hauteur de chute", px(a.rainHeight));
            if (a.stagger > 0.0f) add("Impacts en ligne", "un toutes les " + secs(a.stagger));
            if (a.explosionRadius > 0.0f) add("Rayon d'explosion", px(a.explosionRadius));
            if (a.homing > 0.0f) add("Tirs à tête chercheuse", "pendant " + secs(a.homingTime));
            break;
        }
        case AttackType::Slam:
            add("Ondes de choc", "2, à " + sheetNumber(a.projectileSpeed, 0) + " px/s sur " + px(a.range));
            break;
        case AttackType::Lunge:
        case AttackType::Charge: add("Vitesse", sheetNumber(a.lungeSpeed, 0) + " px/s"); break;
        case AttackType::Leap: add("Durée du saut", secs(a.leapTime)); break;
        case AttackType::Laser:
            add("Longueur du rayon", px(a.laserLength));
            if (a.sweepDegrees != 0.0f) add("Balayage", sheetNumber(std::abs(a.sweepDegrees), 0) + "°");
            break;
        case AttackType::Summon: {
            std::string who = nameOf ? nameOf(a.summonId) : a.summonId;
            if (who.empty()) who = a.summonId;
            add("Renforts", std::to_string(a.summonCount) + " × " + who);
            add("Renforts présents au plus", std::to_string(a.maxSummoned));
            break;
        }
        case AttackType::Tracker:
            add("Trajet sous le sol", secs(orDefault(a.duration, 2.5f)));
            add("Pic", px(2.0f * orDefault(a.radius, 16.0f)) + " de large, " + px(orDefault(a.height, 44.0f)) + " de haut");
            break;
        case AttackType::Cage:
            add("Écart entre les murs", px(2.0f * orDefault(a.radius, 130.0f)));
            add("Hauteur des murs", px(orDefault(a.height, 136.0f)));
            add("Durée", secs(orDefault(a.duration, 4.0f)));
            break;
        case AttackType::Vortex:
            add("Aspiration", sheetNumber(orDefault(a.pull, 80.0f), 0) + " px/s");
            add("Durée", secs(orDefault(a.duration, 2.0f)));
            break;
        case AttackType::Mirror: add("Vie des doubles", a.duration > 0.0f ? secs(a.duration) : std::string("jusqu'à ce qu'un coup les brise")); break;
        case AttackType::FrostFloor:
            add("Adhérence du sol gelé", percent(orDefault(a.grip, 0.15f)));
            add("Durée", secs(orDefault(a.duration, 5.0f)));
            break;
        case AttackType::Prism:
            add("Rayons", a.arms >= 2 ? std::string("2, croisés") : std::string("1"));
            if (a.bounces > 0) add("Rebonds", std::to_string(a.bounces));
            add("Longueur du rayon", px(a.laserLength));
            break;
        case AttackType::Orbit:
            add("Durée", secs(orDefault(a.duration, 4.0f)));
            add("Rayon le plus large", px(orDefault(a.radius, 150.0f)));
            break;
        case AttackType::Spiral:
            if (a.arms > 0) add("Bras de la spirale", std::to_string(a.arms));
            add("Durée", secs(orDefault(a.duration, 3.0f)));
            break;
        case AttackType::Flood:
            add("Hauteur de l'inondation", px(orDefault(a.height, 64.0f)));
            add("Durée à son plus haut", secs(orDefault(a.duration, 4.0f)));
            break;
        case AttackType::Well:
            add("Aspiration", sheetNumber(orDefault(a.pull, 85.0f), 0) + " px/s");
            add("Rayon du cœur", px(orDefault(a.radius, 22.0f)));
            add("Durée", secs(orDefault(a.duration, 4.0f)));
            break;
        case AttackType::Blink: add("Rayon de la frappe", px(orDefault(a.radius, 46.0f))); break;
        case AttackType::Ring:
            add("Portée de l'anneau", px(orDefault(a.radius, 560.0f)));
            add("Ouverture", sheetNumber(orDefault(a.spread, 50.0f), 0) + "°");
            add("Vitesse de l'anneau", sheetNumber(std::max(40.0f, a.projectileSpeed), 0) + " px/s");
            break;
        case AttackType::Dive:
            if (a.count > 1) add("Piqués enchaînés", std::to_string(a.count));
            break;
        case AttackType::Perch:
            add("Nuage de feu", secs(orDefault(a.duration, 3.5f), 1));
            add("Posé : dégâts reçus", sheetTimes(orDefault(a.vulnerability, 1.5f)));
            break;
        case AttackType::CloudShot:
            if (a.count > 1) add("Boules de feu", std::to_string(a.count));
            add("Nuage de feu", secs(orDefault(a.duration, 3.0f), 1));
            break;
        case AttackType::Gust: add("Poussée", secs(orDefault(a.duration, 2.2f), 1)); break;
        case AttackType::Shell: {
            const int bounces = a.bounces > 0 ? a.bounces : 1;
            add("Rebonds", std::to_string(bounces));
            add("En carapace : dégâts reçus", sheetTimes(orDefault(a.armor, 0.35f)));
            add("Étourdi : dégâts reçus", sheetTimes(orDefault(a.vulnerability, 1.5f)));
            break;
        }
        case AttackType::Quake:
            add("Fissures par secousse", std::to_string(a.count));
            add("Séisme", secs(orDefault(a.duration, 3.6f), 1));
            break;
        case AttackType::Skyfall: add("Ondes de choc", std::to_string(a.count)); break;
        case AttackType::Sunbeam: {
            const int columns = a.arms >= 2 ? 2 * a.count + 1 : a.count;
            add("Colonnes", std::to_string(columns) + (a.arms >= 2 ? " depuis les deux murs" : " en marche"));
            break;
        }
        case AttackType::Sweep: add("Lames", std::to_string(a.count) + (a.count > 1 ? ", basse et haute" : ", basse ou haute")); break;
        case AttackType::Glyph: {
            const int runes = a.arms >= 2 ? 2 * a.count + 1 : a.count;
            add("Runes", std::to_string(runes) + (a.arms >= 2 ? " autour du joueur" : " en ligne"));
            break;
        }
        case AttackType::Hex:
            add("Rayon du cercle", px(orDefault(a.radius, 72.0f)));
            add("Sauts alourdis", secs(orDefault(a.duration, 5.0f), 1));
            add("Hauteur des sauts maudits", percent(orDefault(a.curse, 0.72f)));
            break;
        case AttackType::Press:
            add("Motif", a.arms == 2   ? std::string("toutes sauf une")
                         : a.arms == 3 ? std::string("en marche, l'une après l'autre")
                                       : "une sur deux, " + std::to_string(a.count) + (a.count > 1 ? " vagues" : " vague"));
            break;
        case AttackType::Missile: add("Missiles verrouillés", std::to_string(a.count)); break;
        case AttackType::Belt:
            add("Sens", a.arms == 1 ? std::string("loin de lui") : std::string("vers lui"));
            add("Durée", secs(orDefault(a.duration, 5.0f), 1));
            break;
        default: break;
    }
    if (a.slow > 0.0f && a.slowDuration > 0.0f) add("Ralentit", "à " + percent(a.slow) + " de votre vitesse pendant " + secs(a.slowDuration));
    if (a.lead > 0.0f && a.type != AttackType::Dive) add("Visée anticipée", percent(std::min(1.0f, a.lead)));
    return f;
}

AttackPreviewPlan attackPreviewPlan(const EnemyDefinition& d, const AttackDef& a) {
    AttackPreviewPlan p;
    if (a.type == AttackType::Contact) {
        p.reason = "Ce sont les dégâts de son corps au contact : il n'y a pas d'attaque à lancer pour l'exemple.";
        return p;
    }
    const std::vector<int> phases = attackPhases(d, a.id);
    if (!phases.empty()) {
        p.showable = true;
        p.phase = phases.front();
        return p;
    }
    if (isFinal(d, a.id)) {
        // Only in its last stand: shown there (out of health, it gives its last attacks).
        p.showable = true;
        p.lastStand = true;
        p.phase = std::max(0, static_cast<int>(d.phases.size()) - 1);
        return p;
    }
    p.reason = "Aucune de ses phases ne l'utilise : le boss ne la lance jamais en combat.";
    return p;
}

std::vector<BossAttackEntry> bossAttackEntries(const EnemyDefinition& d, const EnemyNameFn& nameOf) {
    std::vector<BossAttackEntry> out;
    out.reserve(d.attacks.size());
    for (const AttackDef& a : d.attacks) {
        BossAttackEntry e;
        e.attack = &a;
        e.phases = attackPhases(d, a.id);
        e.final = isFinal(d, a.id);
        e.summary = attackSummary(a);
        e.where = attackWhere(d, a.id);
        e.description = attackDescription(a);
        e.facts = attackFacts(d, a, nameOf);
        e.plan = attackPreviewPlan(d, a);
        out.push_back(std::move(e));
    }
    return out;
}

// ------------------------------------------------------------------------------- the fold

bool AttackFold::expanded(const std::string& id) const { return std::find(order_.begin(), order_.end(), id) != order_.end(); }

bool AttackFold::allExpanded(const std::vector<std::string>& ids) const {
    if (ids.empty()) return false;
    return std::all_of(ids.begin(), ids.end(), [&](const std::string& id) { return expanded(id); });
}

void AttackFold::forget(const std::string& id) {
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    if (preview_ == id) preview_ = order_.empty() ? std::string{} : order_.back();
    if (focus_ == id) {
        focus_.clear();
        focusTime_ = 0.0f;
    }
}

void AttackFold::expand(const std::string& id) {
    if (id.empty()) return;
    if (!expanded(id)) order_.push_back(id);
    preview_ = id;  // the last unfolded plays its example
    focus_ = id;
    focusTime_ = kDwell;
}

void AttackFold::collapse(const std::string& id) { forget(id); }

void AttackFold::toggle(const std::string& id) {
    if (expanded(id)) collapse(id);
    else expand(id);
}

void AttackFold::expandAll(const std::vector<std::string>& ids) {
    for (const std::string& id : ids)
        if (!id.empty() && !expanded(id)) order_.push_back(id);
    if (preview_.empty() || !expanded(preview_)) {
        preview_.clear();
        for (const std::string& id : ids)
            if (!id.empty()) {
                preview_ = id;
                break;
            }
    }
}

void AttackFold::collapseAll() {
    order_.clear();
    preview_.clear();
    focus_.clear();
    focusTime_ = 0.0f;
}

void AttackFold::update(const std::string& focused, float dt) {
    if (focused.empty() || !expanded(focused)) {
        // The focus elsewhere (another widget, a folded attack): the example stays.
        focus_.clear();
        focusTime_ = 0.0f;
        if (!preview_.empty() && !expanded(preview_)) preview_ = order_.empty() ? std::string{} : order_.back();
        return;
    }
    if (focused != focus_) {
        focus_ = focused;
        focusTime_ = 0.0f;
    } else {
        focusTime_ += std::max(0.0f, dt);
    }
    if (focused != preview_ && (preview_.empty() || focusTime_ >= kDwell)) preview_ = focused;
}

}  // namespace cosmic
