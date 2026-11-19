-- 2.75: clan raids « Asterion » (raidWipe.ts, docs/RAIDS.md §20): a group WIPED in a zone (every
-- player of its instance down at once, accepted by the consensus of its games) is out of that zone
-- for good: its instance never plays again (no seat, no report), its players are spectators for the
-- rest of the event (a join gives them a spectator link, never a seat). The zone stays « à reprendre »
-- until another group that finished its own zone takes it again in a fresh instance of that zone
-- (`start_zone`, `relieves`: the wiped instance it relieves) and finishes it.

ALTER TABLE raid_instances ADD COLUMN wiped_zone INTEGER;
ALTER TABLE raid_instances ADD COLUMN wiped_at TEXT;
ALTER TABLE raid_instances ADD COLUMN start_zone INTEGER;
ALTER TABLE raid_instances ADD COLUMN relieves TEXT;

CREATE TABLE raid_spectators (
    event_id    TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    branch      TEXT NOT NULL,
    zone        INTEGER NOT NULL,
    created_at  TEXT NOT NULL,
    PRIMARY KEY (event_id, player_id)
);
CREATE INDEX raid_instances_wiped ON raid_instances (event_id, branch, wiped_zone);
CREATE INDEX raid_instances_relieves ON raid_instances (relieves);
