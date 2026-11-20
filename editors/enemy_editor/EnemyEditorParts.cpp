// Enemy editor, Boss › Parties (2.76.2, docs/HITBOX_PARTS.md): the hit parts of a boss shown on its sprite, the
// pose and the frame chosen, each in the colour and the pattern of its role; the rules of a part — its role
// (weak point, normal, armour, hurts), its damage multiplier, the attacks during which it also hurts.
//
// The rules are the game's (data/enemies/<id>.json « parts »), like a native's definition (2.45): read-only
// for the player; in ADMINISTRATION › Ennemis an administrator changes them and « Enregistrer les parties »
// writes them into that file (every other key, the order and the indentation kept). They are not sent by
// « Publier… »: they go with the next version of the game. The player's own entities fight with their box
// (their rules would not be read): their parts are shown, never set. Nothing here changes a rule of the
// simulation: a boss already fighting by its parts is told that changing them changes its fights.

#include <algorithm>
#include <cmath>

#include "editors/enemy_editor/EnemyEditorScene.h"
#include "editors/shared/PropertyEditor.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/online/LevelFingerprint.h"
#include "ui/widgets/HitPartsView.h"

namespace cosmic {

namespace {

/// The sheets of an enemy that have hit parts: its own, then each phase's that differs (with the first
/// phase that wears it).
std::vector<std::pair<std::string, int>> partSheets(const EnemyDefinition& d) {
    std::vector<std::pair<std::string, int>> out;
    auto add = [&](const std::string& sheet, int phase) {
        if (sheet.empty() || !HitPartsSheet::of(sheet)) return;
        for (const auto& s : out)
            if (s.first == sheet) return;
        out.push_back({sheet, phase});
    };
    add(d.animation, 1);
    for (size_t i = 0; i < d.phases.size(); ++i) add(d.phases[i].animation, static_cast<int>(i) + 1);
    return out;
}

/// The multiplier a part takes when it leaves « Blesse » for `role`.
float defaultDamage(PartRole role) {
    switch (role) {
        case PartRole::Weak: return 2.0f;
        case PartRole::Armour: return 0.5f;
        case PartRole::Hurt: return 0.0f;
        case PartRole::Normal: break;
    }
    return 1.0f;
}

}  // namespace

bool EnemyEditorScene::hasHitParts() const { return !partSheets(work_).empty(); }

void EnemyEditorScene::loadParts() {
    partsFor_ = selectedId_;
    // A native's rules, from the game's file; the player's own entities fight with their box (none).
    partsWork_ = editingNative() ? PartRules::of(selectedId_) : PartRules{};
    partsDirty_ = false;
}

void EnemyEditorScene::saveParts() {
    if (!access().parts || !partsDirty_) return;
    std::string error;
    if (!savePartRules(selectedId_, partsWork_, &error)) {
        flash(error.empty() ? std::string("Échec de l'enregistrement des parties") : error, true);
        return;
    }
    loadParts();  // as the file now says
    confirmQuit_ = false;
    arenaDirty_ = true;  // the arena's boss with its new rules
    flash("Parties enregistrées dans data/enemies/" + selectedId_ + ".json");
    g_.uiSound("ui_confirm");
}

void EnemyEditorScene::tabParts(float x, float& y, float w) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    PartsLayout& L = partsLayout_;
    L = PartsLayout{};
    if (partsFor_ != selectedId_) loadParts();
    const EnemyEditAccess acc = access();
    L.editable = acc.parts;
    const bool roBefore = ui.readOnly;
    const float gap = ui.s(8);
    // A text over the whole width, wrapped and measured: the next element below it.
    auto note = [&](const std::string& t, Color c) {
        const float size = 15.0f;
        const Vec2 m = ui.text().measureWrapped(t, FontFace::Body, ui.s(size), w);
        ui.labelWrapped(t, {x, y}, w, size, c);
        const Rect box{x, y, w, m.y};
        L.texts.push_back({t, box, ui.s(size), FontFace::Body});
        L.boxes.push_back(box);
        y += m.y + gap;
    };
    // A text on one line in `box`: smaller if needed, then shortened. `own`: an element of the panel (else
    // drawn inside another, the stage's caption).
    auto line = [&](const std::string& t, const Rect& box, float size, Color c, FontFace face, bool own = true) {
        const float fs = ui.fitFont(t, face, size, box.w, 0.7f);
        const std::string shown = ui.elide(t, face, fs, box.w);
        ui.label(shown, {box.x, box.center().y}, fs, c, Align::Left, face, VAlign::Middle);
        L.texts.push_back({shown, box, ui.s(fs), face});
        if (own) L.boxes.push_back(box);
    };
    auto widget = [&](const Rect& box) {
        L.boxes.push_back(box);
        return box;
    };

    // Who may change them, where they go, whether its fight uses them.
    const bool native = editingNative();
    if (!native)
        note("Vos entités se battent avec leur boîte : seules les entités du jeu se battent par parties. Ses parties sont montrées ici, "
             "sans règles.",
             ui.theme().textDim);
    else if (acc.parts)
        note("Administration : « Enregistrer les parties » écrit leurs règles dans data/enemies/" + selectedId_ +
                 ".json de ce jeu ; elles partent avec sa prochaine version (« Publier… » ne les envoie pas).",
             ui.theme().warm);
    else
        note("Boss du jeu : ses parties sont en lecture seule ; seul un administrateur règle leurs rôles (Administration › Ennemis).",
             ui.theme().warm);
    const bool fights = partsWork_.fromVersion > 0 && partsWork_.fromVersion <= kSimulationVersion;
    if (native) {
        if (partsWork_.fromVersion == 0) note("Ses parties : affichage seulement ; son combat se joue sur sa boîte.", ui.theme().textDim);
        else if (fights) {
            note("Il se bat par ses parties depuis les règles de simulation " + std::to_string(partsWork_.fromVersion) + ".", ui.theme().textDim);
            note("Tout le corps blesse au contact ; les parties rouges blessent aussi pendant leurs attaques.", ui.theme().textDim);
        }
        else note("Il se battra par ses parties à partir des règles de simulation " + std::to_string(partsWork_.fromVersion) + ".", ui.theme().textDim);
        if (acc.parts && fights)
            note("Attention : changer leurs règles change ses combats ; les replays enregistrés ne se rejoueraient plus à l'identique. "
                 "Une telle modification demande une nouvelle version de simulation (docs/HITBOX_PARTS.md).",
                 ui.theme().danger);
    }
    if (acc.parts) {
        UIContext::ButtonStyle sv;
        sv.small = true;
        sv.primary = partsDirty_;
        sv.enabled = partsDirty_;
        ui.readOnly = false;
        if (ui.button("parts_save", widget({x, y, w * 0.58f, ui.s(46)}), partsDirty_ ? "Enregistrer les parties*" : "Enregistrer les parties", sv))
            saveParts();
        UIContext::ButtonStyle rv;
        rv.small = true;
        rv.enabled = partsDirty_;
        if (ui.button("parts_revert", widget({x + w * 0.61f, y, w * 0.39f, ui.s(46)}), "Annuler", rv)) {
            loadParts();
            flash("Modifications des parties annulées");
        }
        y += ui.s(46) + gap * 1.5f;
    }

    // What is shown: the sheet (a phase may wear another), the pose, the frame. Never read-only.
    ui.readOnly = false;
    const auto sheets = partSheets(work_);
    if (sheets.empty()) {
        ui.readOnly = roBefore;
        return;
    }
    partsSheet_ = std::clamp(partsSheet_, 0, static_cast<int>(sheets.size()) - 1);
    if (sheets.size() > 1) {
        std::vector<std::string> names;
        for (const auto& s : sheets) names.push_back("Phase " + std::to_string(s.second));
        if (ui.cycle("parts_sheet", widget({x, y, w, ui.s(44)}), "Planche", partsSheet_, names)) {
            partsPose_ = 0;
            partsStage_.restart();
        }
        y += ui.s(44) + gap;
    }
    const std::string& sheetPath = sheets[static_cast<size_t>(partsSheet_)].first;
    if (!work_.assetRoot.empty()) g_.app.resources().pushMount(work_.assetRoot);
    AnimationSetPtr set = g_.app.resources().animation(sheetPath);
    if (!work_.assetRoot.empty()) g_.app.resources().popMount();
    const HitPartsSheet* hp = HitPartsSheet::of(sheetPath);
    std::vector<std::string> clips;
    if (set)
        for (const auto& [name, clip] : set->clips) clips.push_back(name);
    const std::vector<std::string> poses = poseClips(clips);
    if (!set || !hp || poses.empty()) {
        ui.readOnly = roBefore;
        return;
    }
    partsPose_ = std::clamp(partsPose_, 0, static_cast<int>(poses.size()) - 1);
    {
        std::vector<std::string> labels;
        for (const std::string& p : poses) labels.push_back(poseLabel(p));
        if (ui.cycle("parts_pose", widget({x, y, w, ui.s(44)}), "Pose", partsPose_, labels)) partsStage_.restart();
        y += ui.s(44) + gap;
    }
    const AnimationClip* clip = set->clip(poses[static_cast<size_t>(partsPose_)]);
    const int frames = clip ? clip->frameCount() : 0;
    // 2.77.1: the stage of the diagrams (ui/widgets/HitPartsView): its bar (pause, frame by frame, zoom, « Recentrer »),
    // its drawing clipped to its frame, the tooltip of the part under the mouse or chosen with Tab.
    const float dt = g_.app.frameTime() / 1000.0f;
    partsStage_.advance(dt);
    std::vector<float> durations;
    for (int i = 0; i < frames; ++i) durations.push_back(clip->durations[static_cast<size_t>(i)]);
    const int frame = partsStage_.frame(durations);
    size_t mark = 0, markText = 0;
    auto takeStage = [&]() {  // what the stage drew since the last time: the panel's elements and texts
        const HitStageLayout& sl = partsStage_.layout();
        for (size_t i = mark; i < sl.boxes.size(); ++i) L.boxes.push_back(sl.boxes[i]);
        for (size_t i = markText; i < sl.texts.size(); ++i) L.texts.push_back({sl.texts[i].text, sl.texts[i].r, sl.texts[i].px, sl.texts[i].face});
        mark = sl.boxes.size();
        markText = sl.texts.size();
    };
    y += partsStage_.controls(ui, "parts_stage", x, y, w) + gap;
    takeStage();

    // The stage: its sprite as the game places it (its feet at (0, 0), its pivot × its scale), its parts on
    // that very frame, the chosen one outlined in white, its feet a cross (where the origins are measured from).
    const Rect stage = widget({x, y, w, ui.s(300)});
    const Rect sprite{-set->pivot.x * work_.scale, -set->pivot.y * work_.scale, static_cast<float>(set->frameWidth) * work_.scale,
                      static_cast<float>(set->frameHeight) * work_.scale};
    Rect win = sprite;
    const float margin = std::max(win.w, win.h) * 0.05f;
    win = {win.x - margin, win.y - margin, win.w + 2 * margin, win.h + 2 * margin};
    partsStage_.place(stage, win, ui.s(8));
    const bool click = partsStage_.input(ui, "parts_stage", dt);
    const std::vector<HitPartPiece>* pieces = clip && frames > 0 ? hp->frame(*set, *clip, frame) : nullptr;
    if (partsChosen_.empty() && !hp->parts.empty()) partsChosen_ = hp->parts.front();
    const Vec2 pivot = set->pivot;
    const float sc = work_.scale;
    auto toScreen = [&](Vec2 v) { return partsStage_.toScreen(Vec2{(v.x - pivot.x) * sc, (v.y - pivot.y) * sc}); };
    // Tab / Y go from part to part (the list follows); a click chooses the part under the mouse (the smallest piece there).
    partsStage_.choose(partsChosen_);
    if (pieces) partsStage_.parts(ui, *hp, *pieces, toScreen);
    partsChosen_ = partsStage_.chosen();
    if (click && !partsStage_.shownPart().empty()) partsChosen_ = partsStage_.shownPart();
    r.fillRoundedRect(stage, ui.s(10), Color{11, 18, 38, 255});
    r.pushClip(stage);
    if (set->texture && clip && frames > 0) {
        const Rect src = set->frameRect(*clip, frame);
        r.drawTexture(*set->texture, &src, partsStage_.toScreen(sprite), work_.tint);
    }
    if (pieces) drawHitPieces(r, std::max(1.5f, ui.s(2.0f)), *hp, *pieces, partsWork_, toScreen, 80, partsChosen_);
    const Vec2 f0 = partsStage_.toScreen(Vec2{0.0f, 0.0f});
    const float arm = ui.s(8), th = std::max(1.5f, ui.s(2.0f));
    r.drawLine({f0.x - arm, f0.y}, {f0.x + arm, f0.y}, Color{255, 255, 255, 220}, th);
    r.drawLine({f0.x, f0.y - arm}, {f0.x, f0.y + arm}, Color{255, 255, 255, 220}, th);
    r.popClip();
    partsStage_.drawFocus(ui);
    if (!partsStage_.shownPart().empty()) partsStage_.setTip(partTip(partFacts(partsStage_.shownPart(), partsWork_, work_)));
    partsStage_.layout().boxes.push_back(stage);
    mark = partsStage_.layout().boxes.size();
    partsStage_.layout().body = partsStage_.toScreen(Rect::fromBottomCenter({0.0f, 0.0f}, work_.size));  // never drawn (tests)
    y = stage.bottom() + gap;
    y += partsStage_.help(ui, x, y, w, true) + gap;
    takeStage();

    // The legend: each role, its colour and its pattern (the same in the sheets « Où frapper »).
    {
        const float cw = (w - gap) * 0.5f, rh = ui.s(28), sw = ui.s(18);
        const PartRole roles[] = {PartRole::Weak, PartRole::Normal, PartRole::Armour, PartRole::Hurt};
        for (int i = 0; i < 4; ++i) {
            const float cx = x + static_cast<float>(i % 2) * (cw + gap), cy = y + static_cast<float>(i / 2) * rh;
            const Rect sq = widget({cx, cy + (rh - sw) * 0.5f, sw, sw});
            drawPartSwatch(r, sq, roles[i], ui.s(1.0f));
            line(partRoleLabel(roles[i]), {sq.right() + ui.s(8), cy, cw - sw - ui.s(8), rh}, 15.0f, ui.theme().text, FontFace::Body);
        }
        y += rh * 2 + gap;
    }

    // Its parts: chosen with the keys, the gamepad or the mouse.
    L.parts = static_cast<int>(hp->parts.size());
    for (size_t i = 0; i < hp->parts.size(); ++i) {
        const std::string& name = hp->parts[i];
        const PartRule rule = partsWork_.ruleOr(name);
        std::string sub = std::string(partRoleLabel(rule.role)) + " · " + partDamageLabel(rule);
        if (const std::string during = hurtDuringLabel(rule, work_); !during.empty()) sub += " · " + during;  // its attacks' names
        if (ui.listItem("parts_part" + std::to_string(i), widget({x, y, w, ui.s(52)}), partNameLabel(name), name == partsChosen_, sub,
                        partRoleColor(rule.role)))
            partsChosen_ = name;
        y += ui.s(52) + ui.s(6);
    }
    y += gap;

    // The rules of the chosen part: read-only unless an administrator edits a native.
    if (!partsChosen_.empty() && std::find(hp->parts.begin(), hp->parts.end(), partsChosen_) != hp->parts.end()) {
        line("Partie : " + partNameLabel(partsChosen_), {x, y, w, ui.s(30)}, 19.0f, ui.theme().accent, FontFace::Bold);
        y += ui.s(30) + gap;
        ui.readOnly = !acc.parts;
        PartRule cur = partsWork_.ruleOr(partsChosen_);
        bool c = false;
        std::vector<std::string> roleNames;
        for (PartRole role : {PartRole::Weak, PartRole::Normal, PartRole::Armour, PartRole::Hurt}) roleNames.push_back(partRoleLabel(role));
        int role = static_cast<int>(cur.role);
        if (ui.cycle("parts_role", widget({x, y, w, ui.s(44)}), "Rôle", role, roleNames)) {
            const PartRole next = static_cast<PartRole>(role);
            if (cur.role == PartRole::Hurt || next == PartRole::Hurt) cur.damage = defaultDamage(next);
            if (next == PartRole::Hurt) cur.hurtDuring.clear();
            cur.role = next;
            c = true;
        }
        y += ui.s(44) + gap;
        if (cur.role == PartRole::Hurt) {
            note("Une partie qui blesse ne prend aucun coup ; pendant ses attaques, elle frappe avec leurs dégâts.", ui.theme().textDim);
        } else {
            FieldRow row{ui, x, w, w * 0.5f, y};
            const Rect f = row.field("Multiplicateur de dégâts");
            L.boxes.push_back({x, f.y, w, f.h});
            c |= ui.numberField("parts_damage", f, cur.damage, 0.25f, 0.0f, 4.0f, "%.2f");
            line("Blesse aussi pendant ces attaques :", {x, y, w, ui.s(28)}, 16.0f, ui.theme().text, FontFace::Body);
            y += ui.s(28) + gap * 0.5f;
            // Its attacks by their names, then any attack its rules name that it does not have (so that it can be
            // removed), by the name another enemy gives it — never an identifier.
            std::vector<std::pair<std::string, std::string>> attacks;
            for (const AttackDef& a : work_.attacks) attacks.push_back({a.id, a.name.empty() ? std::string("Attaque sans nom") : a.name});
            for (const std::string& id : cur.hurtDuring)
                if (std::none_of(attacks.begin(), attacks.end(), [&](const auto& a) { return a.first == id; })) {
                    std::string name = "Attaque absente";
                    for (const EnemyDefinition* e : g_.content.enemies())
                        for (const AttackDef& a : e->attacks)
                            if (a.id == id && !a.name.empty() && name == "Attaque absente") name = a.name + " (absente de ce boss)";
                    attacks.push_back({id, name});
                }
            if (attacks.empty()) note("Il n'a pas d'attaque.", ui.theme().textDim);
            for (size_t i = 0; i < attacks.size(); ++i) {
                bool on = std::find(cur.hurtDuring.begin(), cur.hurtDuring.end(), attacks[i].first) != cur.hurtDuring.end();
                const Rect tr = widget({x, y, w, ui.s(44)});
                if (ui.toggle("parts_hurt" + std::to_string(i), tr, ui.elide(attacks[i].second, FontFace::Body, 22.0f * 0.6f, tr.w - ui.s(116)), on)) {
                    if (on) cur.hurtDuring.push_back(attacks[i].first);
                    else cur.hurtDuring.erase(std::remove(cur.hurtDuring.begin(), cur.hurtDuring.end(), attacks[i].first), cur.hurtDuring.end());
                    c = true;
                }
                y += ui.s(44) + ui.s(6);
            }
        }
        if (c && acc.parts) {
            PartRule* slot = nullptr;
            for (PartRule& pr : partsWork_.rules)
                if (pr.name == partsChosen_) slot = &pr;
            if (!slot) {
                partsWork_.rules.push_back(cur);
                slot = &partsWork_.rules.back();
            }
            *slot = cur;
            partsDirty_ = true;
            confirmQuit_ = false;
        }
    }
    ui.readOnly = roBefore;
}

}  // namespace cosmic
