-- 2.75: clan raids « Asterion » (docs/RAIDS.md §19): the zones finished and their tables.
--
-- A zone of a branch is finished when every objective of it that is not optional is accepted (the
-- secret zone: all of them). The server writes it ONCE per event, branch and zone (the primary key:
-- two acceptances, or two instances, tell it once), and the raid's feed (the titan's alerts) shows it.
CREATE TABLE raid_zone_completions (
    event_id TEXT NOT NULL,
    branch TEXT NOT NULL,
    zone INTEGER NOT NULL,
    instance_id TEXT,             -- the instance whose accepted objective finished it
    completed_at TEXT NOT NULL,
    PRIMARY KEY (event_id, branch, zone)
);

-- The « Tableau des contributions » of a zone, as the games of an instance computed it and its
-- majority confirmed (report « zone_table », the consensus of the other reports): one line per player
-- (seat and the step they took it from, mapped to the member as raid_contributions), damage in tenths.
CREATE TABLE raid_zone_contributions (
    instance_id TEXT NOT NULL,
    event_id TEXT NOT NULL,
    branch TEXT NOT NULL,
    zone INTEGER NOT NULL,
    seat INTEGER NOT NULL,
    from_tick INTEGER NOT NULL,
    player_id TEXT,
    boss_damage INTEGER NOT NULL DEFAULT 0,
    damage INTEGER NOT NULL DEFAULT 0,
    support INTEGER NOT NULL DEFAULT 0,
    mechanisms INTEGER NOT NULL DEFAULT 0,
    objectives INTEGER NOT NULL DEFAULT 0,
    tick INTEGER NOT NULL,
    accepted_at TEXT NOT NULL,
    PRIMARY KEY (instance_id, zone, seat, from_tick)
);
CREATE INDEX raid_zone_contributions_event ON raid_zone_contributions (event_id, instance_id, zone);
