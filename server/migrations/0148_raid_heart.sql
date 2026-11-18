-- 2.72 (phase 6, docs/RAIDS.md §15): the Cœur d'Asterion, the final fight of the clan raid.
--
-- Every group of every clan fights the heart at the same time, each instance in its own simulation;
-- its health is SHARED: each instance's consensus validates the part of it that instance took
-- (ten-thousandths of the heart's health, `damage`), kept here as the highest value accepted (a late
-- or repeated report never lowers it nor counts twice: one row per instance). The shared health is
-- 10 000 minus the sum of these rows (capped); the relay of each heart instance gives its games what
-- the OTHER instances took, at a step (RAID_STATE). The heart falls for the world once the sum reaches
-- 10 000: its objective (coeur_asterion) is then written once in raid_branch_objectives.
CREATE TABLE raid_heart_damage (
    event_id    TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    damage      INTEGER NOT NULL CHECK (damage >= 0 AND damage <= 10000),
    tick        INTEGER NOT NULL,
    accepted_at TEXT NOT NULL,
    PRIMARY KEY (event_id, instance_id)
);
CREATE INDEX raid_heart_damage_event ON raid_heart_damage (event_id);

-- What the heart's opening and fall did, once per event (each column written once, before the action:
-- two acceptances at the same moment notify and grant once): the seven gigaboss fallen (the heart
-- opens, the registered players told), the heart fallen (validated: its film, its rewards, the
-- « Asterion est tombé » notifications).
CREATE TABLE raid_heart_events (
    event_id        TEXT PRIMARY KEY REFERENCES raid_events (id) ON DELETE CASCADE,
    opened_at       TEXT,
    fallen_at       TEXT,
    fallen_instance TEXT,
    rewards_at      TEXT,
    notified_open   TEXT,
    notified_fall   TEXT
);
