-- 2.67: clan raids « Asterion », phase 3 (raidZones.ts, raidRewards.ts, docs/RAIDS.md): the
-- gigaboss PYROCLAST of the branch « BRAS DROIT », the players' contributions computed by the
-- games of each instance (accepted by the consensus of the instance, as the objectives), the
-- state of each instance's fight, and the rewards granted from validated results only (once per
-- player, reward and event: a reconnection, a repeated request or two acceptances at once never
-- grant one twice), claimed by the player (« Récupérer »).

-- The step from which a player holds their seat (the relay writes it when the seat is taken): the
-- contributions of a seat are matched with whoever held it from that step.
ALTER TABLE raid_instance_members ADD COLUMN seat_from INTEGER;

-- The contributions of the players of an instance, as its consensus accepted them: one row per
-- player who held a seat (seat, from_tick), the latest table accepted kept (its numbers only grow).
-- Tenths of health for the damage; `ticks` the steps in play (120 per second); `score` the mixed
-- score the server computes itself (never only the damage).
CREATE TABLE raid_contributions (
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    event_id    TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    seat        INTEGER NOT NULL CHECK (seat BETWEEN 0 AND 7),
    from_tick   INTEGER NOT NULL CHECK (from_tick >= 0),
    player_id   TEXT REFERENCES players (id) ON DELETE SET NULL,
    damage      INTEGER NOT NULL DEFAULT 0,
    boss_damage INTEGER NOT NULL DEFAULT 0,
    avoided     INTEGER NOT NULL DEFAULT 0,
    blocks      INTEGER NOT NULL DEFAULT 0,
    rescues     INTEGER NOT NULL DEFAULT 0,
    vents       INTEGER NOT NULL DEFAULT 0,
    weak_points INTEGER NOT NULL DEFAULT 0,
    objectives  INTEGER NOT NULL DEFAULT 0,
    ticks       INTEGER NOT NULL DEFAULT 0,
    gigaboss    INTEGER NOT NULL DEFAULT 0 CHECK (gigaboss IN (0, 1)),
    score       INTEGER NOT NULL DEFAULT 0,
    tick        INTEGER NOT NULL,
    accepted_at TEXT NOT NULL,
    PRIMARY KEY (instance_id, seat, from_tick)
);
CREATE INDEX raid_contributions_event ON raid_contributions (event_id, player_id);

-- Each instance's fight against the gigaboss of its branch, as its consensus accepted it: the
-- highest phase reached (1 the fight began, 2 and 3 its phases, 4 it fell).
CREATE TABLE raid_gigaboss_fights (
    instance_id   TEXT PRIMARY KEY REFERENCES raid_instances (id) ON DELETE CASCADE,
    event_id      TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    branch        TEXT NOT NULL,
    boss          TEXT NOT NULL,
    phase         INTEGER NOT NULL CHECK (phase BETWEEN 1 AND 4),
    defeated_tick INTEGER,
    defeated_at   TEXT,
    updated_at    TEXT NOT NULL
);
CREATE INDEX raid_gigaboss_fights_event ON raid_gigaboss_fights (event_id, branch);

-- The rewards of the raids: granted by the server from validated results only, once per player,
-- reward and event (the unique key). `claimed_at`: the player took it (« Récupérer »); its items
-- then go to player_won_rewards (challenge_id « raid:<event> »), the badge to their achievements.
-- No foreign key on the event: a claimed reward stays even if its event is deleted.
CREATE TABLE raid_reward_grants (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    reward      TEXT NOT NULL,
    event_id    TEXT NOT NULL,
    instance_id TEXT,
    reason      TEXT NOT NULL,
    granted_at  TEXT NOT NULL,
    claimed_at  TEXT,
    UNIQUE (player_id, reward, event_id)
);
CREATE INDEX raid_reward_grants_player ON raid_reward_grants (player_id, claimed_at);
