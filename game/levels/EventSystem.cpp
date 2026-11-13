#include "game/levels/EventSystem.h"

#include "engine/audio/AudioSystem.h"
#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

const std::vector<std::pair<std::string, std::string>>& EventSystem::actionTypes() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        {"open", "Ouvrir (porte, barrière, portail)"},
        {"close", "Fermer"},
        {"toggle", "Basculer"},
        {"activate", "Activer (plateforme, ennemi, checkpoint...)"},
        {"disable", "Désactiver"},
        {"enable", "Réactiver"},
        {"spawn", "Faire apparaître l'ennemi"},
        {"startBoss", "Démarrer le combat de boss"},
        {"showText", "Afficher un titre"},
        {"hint", "Afficher une indication"},
        {"dialogue", "Dialogue"},
        {"cameraPan", "Travelling caméra vers la cible"},
        {"lockCamera", "Verrouiller la caméra sur une zone"},
        {"unlockCamera", "Libérer la caméra"},
        {"shake", "Tremblement de caméra"},
        {"flash", "Flash lumineux"},
        {"playMusic", "Changer de musique"},
        {"playSound", "Jouer un son"},
        {"effect", "Effet de particules sur la cible"},
        {"addTime", "Ajouter du temps au chrono"},
        {"givePower", "Donner un pouvoir"},
        {"heal", "Soigner le joueur"},
        {"killPlayer", "Tuer le joueur"},
        {"completeLevel", "Terminer le niveau"},
    };
    return v;
}

const std::vector<std::pair<std::string, std::string>>& EventSystem::triggerTypes() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        {"enter", "Le joueur entre dans la zone"},
        {"exit", "Le joueur sort de la zone"},
        {"activate", "Activé (levier, récepteur, obstacle détruit...)"},
        {"cleared", "Arène nettoyée"},
        {"start", "Combat d'arène commencé"},
        {"killed", "Ennemi vaincu"},
        {"bossStart", "Combat de boss commencé"},
        {"bossPhase", "Boss : nouvelle phase"},
        {"bossDefeated", "Boss vaincu"},
        {"collected", "Objet ramassé"},
        {"checkpoint", "Checkpoint atteint"},
        {"powerUsed", "Pouvoir utilisé (source = id du pouvoir)"},
        {"unlocked", "Pouvoir débloqué (source = id du pouvoir)"},
        {"levelStart", "Début du niveau (source = level)"},
        {"time", "Temps écoulé (source = level, secondes)"},
    };
    return v;
}

void EventSystem::load(const std::vector<LevelEvent>& events) {
    events_ = events;
    fired_.assign(events_.size(), false);
    pending_.clear();
}

int EventSystem::firedCount() const {
    int n = 0;
    for (bool f : fired_) n += f ? 1 : 0;
    return n;
}

bool EventSystem::matches(const LevelEvent& e, const Json& params) const {
    if (e.trigger == "bossPhase" && e.params.contains("phase")) {
        return jget<int>(params, "phase", 0) == jget<int>(e.params, "phase", 0);
    }
    return true;
}

void EventSystem::signal(GameWorld& w, const std::string& source, const std::string& trigger, const Json& params) {
    for (size_t i = 0; i < events_.size(); ++i) {
        const LevelEvent& e = events_[i];
        if (e.source != source || e.trigger != trigger) continue;
        if (e.once && fired_[i]) continue;
        if (!matches(e, params)) continue;
        if (e.trigger == "powerUsed" && e.params.contains("zone")) {
            LevelEntity* zone = w.entity(jstr(e.params, "zone"));
            Vec2 at{jget<float>(params, "x", 0.0f), jget<float>(params, "y", 0.0f)};
            if (!zone || !zone->box.contains(at)) continue;
        }
        fired_[i] = true;
        if (e.delay > 0.0f)
            pending_.push_back({e.delay, e.actions});
        else
            for (const auto& a : e.actions) runAction(w, a);
    }
}

void EventSystem::fixedUpdate(GameWorld& w, float dt, float levelTime) {
    for (size_t i = 0; i < events_.size(); ++i) {
        const LevelEvent& e = events_[i];
        if (e.trigger != "time" || fired_[i]) continue;
        if (levelTime >= jget<float>(e.params, "seconds", 0.0f)) {
            fired_[i] = true;
            pending_.push_back({e.delay, e.actions});
        }
    }
    for (auto& p : pending_) p.timer -= dt;
    std::vector<Pending> ready;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->timer <= 0.0f) {
            ready.push_back(std::move(*it));
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& p : ready)
        for (const auto& a : p.actions) runAction(w, a);
}

namespace {
std::vector<std::string> ids(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ' ' || c == ';') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
}  // namespace

void EventSystem::runAction(GameWorld& w, const EventAction& a) {
    const std::string& t = a.type;
    const Json& p = a.params;
    if (t == "open" || t == "close" || t == "toggle" || t == "activate" || t == "disable" || t == "enable" ||
        t == "spawn" || t == "break" || t == "startBoss" || t == "kill") {
        for (const auto& id : ids(a.target)) {
            if (LevelEntity* e = w.entity(id))
                e->handleAction(w, t, p);
            else
                Log::warn("Event action '{}' targets unknown object '{}'", t, id);
        }
    } else if (t == "showText") {
        WorldMessage m;
        m.style = WorldMessage::Style::Title;
        m.text = jstr(p, "text");
        m.subtitle = jstr(p, "subtitle");
        m.duration = jget<float>(p, "duration", 3.0f);
        w.message(m);
    } else if (t == "hint") {
        w.hint(jstr(p, "text"), jget<float>(p, "duration", 3.5f));
    } else if (t == "dialogue") {
        WorldMessage m;
        m.style = WorldMessage::Style::Dialogue;
        m.speaker = jstr(p, "speaker");
        m.text = jstr(p, "text");
        m.duration = jget<float>(p, "duration", 4.0f);
        w.message(m);
    } else if (t == "cameraPan") {
        CinematicDef c;
        c.letterbox = jget<bool>(p, "letterbox", true);
        c.skippable = true;
        CinematicStep go;
        go.type = "camera";
        go.duration = jget<float>(p, "duration", 1.2f);
        go.params = a.target.empty() ? p : Json{{"target", a.target}};
        CinematicStep hold;
        hold.type = "wait";
        hold.duration = jget<float>(p, "hold", 1.0f);
        CinematicStep back;
        back.type = "camera";
        back.duration = jget<float>(p, "duration", 1.2f) * 0.8f;
        back.params = {{"player", true}};
        c.steps = {go, hold, back};
        if (!w.cinematic().active()) w.cinematic().start(c, w);
    } else if (t == "lockCamera") {
        if (LevelEntity* e = w.entity(a.target)) {
            CameraZone z;
            z.area = e->box;
            z.clampToArea = true;
            z.zoom = jget<float>(p, "zoom", 0.0f);
            w.lockCameraZone(&z);
        }
    } else if (t == "unlockCamera") {
        w.lockCameraZone(nullptr);
    } else if (t == "shake") {
        w.shake(jget<float>(p, "amount", 0.5f));
    } else if (t == "flash") {
        w.flash(colorFromJson(p.value("color", Json()), Color::white()), jget<float>(p, "duration", 0.3f));
    } else if (t == "playMusic") {
        std::string m = jstr(p, "music");
        if (m.empty())
            w.restoreLevelMusic();
        else
            w.setMusic(m);
    } else if (t == "playSound") {
        w.playSound(jstr(p, "sound"), jget<float>(p, "volume", 1.0f));
    } else if (t == "effect") {
        Vec2 pos{jget<float>(p, "x", 0.0f), jget<float>(p, "y", 0.0f)};
        if (LevelEntity* e = w.entity(a.target)) pos = e->box.center();
        w.particles().emit(jstr(p, "effect", "explosion"), pos, jget<float>(p, "scale", 1.0f));
    } else if (t == "addTime") {
        w.timer().addTime(jget<float>(p, "seconds", 30.0f));
        w.hint("+" + std::to_string(static_cast<int>(jget<float>(p, "seconds", 30.0f))) + " s", 2.0f);
    } else if (t == "givePower") {
        if (const PowerDefinition* d = w.content().power(jstr(p, "power"))) w.player().grantPower(*d);
    } else if (t == "heal") {
        w.player().heal(jget<int>(p, "amount", 10));
    } else if (t == "killPlayer") {
        w.player().kill(w);
    } else if (t == "completeLevel") {
        for (auto& e : w.entities())
            if (e->type() == "exit") {
                w.reachExit(*e);
                break;
            }
    } else {
        Log::warn("Unknown event action '{}'", t);
    }
}

}  // namespace cosmic
