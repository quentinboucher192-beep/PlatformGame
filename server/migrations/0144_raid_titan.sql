-- 2.68: clan raids « Asterion », phase 4 (raidMap.ts, docs/RAIDS.md): the seven branches of the
-- titan and the placement of the players in instances (spec §8: 8 per instance, up to 50 per clan).
-- The state of each branch stays raid_branch_objectives (0142); the links between branches are
-- computed from it (raidMap.ts), nothing new is stored for them.

-- Where a registered player plays in an event: one instance (a group of a clan on a branch), at
-- most 8 players placed in one (raidMap.ts places with one conditional statement). `clan_id`: the
-- player's clan (a helper may be placed in another clan's instance). `placed_by`: themselves, or
-- the raid leader / chief / officer who moved them; `reason`: choix, aide, chef, equilibrage.
CREATE TABLE raid_placements (
    event_id    TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    clan_id     TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    branch      TEXT NOT NULL,
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    placed_by   TEXT REFERENCES players (id) ON DELETE SET NULL,
    reason      TEXT NOT NULL CHECK (reason IN ('choix', 'aide', 'chef', 'equilibrage')),
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL,
    PRIMARY KEY (event_id, player_id)
);
CREATE INDEX raid_placements_instance ON raid_placements (instance_id);
CREATE INDEX raid_placements_clan ON raid_placements (event_id, clan_id, branch);

-- The journal of the placements: every change of instance, by whom and why.
CREATE TABLE raid_placement_log (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id      TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    player_id     TEXT REFERENCES players (id) ON DELETE SET NULL,
    from_instance TEXT,
    to_instance   TEXT NOT NULL,
    by_player     TEXT REFERENCES players (id) ON DELETE SET NULL,
    reason        TEXT NOT NULL,
    created_at    TEXT NOT NULL
);
CREATE INDEX raid_placement_log_event ON raid_placement_log (event_id, created_at);

-- The instances of an event by clan and branch (placement, the titan's map).
CREATE INDEX raid_instances_clan ON raid_instances (event_id, clan_id, branch);
