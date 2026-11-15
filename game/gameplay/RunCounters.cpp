#include "game/gameplay/RunCounters.h"

#include "game/achievements/Achievements.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/records/Records.h"

namespace cosmic {

bool noteRunCounters(AchievementStats& a, const GameWorld& w, bool completed) {
    if (w.training()) return false;
    const RunStats& s = w.stats();
    a.add("kills", s.kills);
    for (const auto& [family, n] : s.familyKills) a.add("kills_" + family, n);  // 2.29: by family
    a.add("deaths", s.deaths);
    a.add("shards", s.shards);
    a.add("relics", s.relics);
    a.add("secrets", s.secrets);
    for (const auto& pw : w.player().powers()) {
        a.insert("powers_owned", pw->id());
        if (completed) a.add("carried_" + pw->id());  // passive powers: finished while owned
    }
    a.raise("run_kills", s.kills);
    if (completed) a.add("runs_completed");
    return true;
}

RunResult runResultOf(const GameWorld& w, bool completed) {
    RunResult r;
    r.levelId = w.level().id;
    r.completed = completed;
    r.time = w.timer().elapsed();
    const RunStats& s = w.stats();
    r.deaths = s.deaths;
    r.collectibles = s.collectibles();
    r.totalCollectibles = s.totalCollectibles();
    r.relics = s.relics;
    r.secrets = s.secrets;
    r.totalSecrets = s.totalSecrets;
    r.kills = s.kills;
    r.powersUsed.assign(s.powersUsed.begin(), s.powersUsed.end());
    r.splits = w.timer().splits();
    r.training = w.training();
    return r;
}

}  // namespace cosmic
