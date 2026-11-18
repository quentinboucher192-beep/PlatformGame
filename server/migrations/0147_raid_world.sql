-- 2.69: clan raids « Asterion », phase 5 (raidWorld.ts, docs/RAIDS.md §13 à §16): the world events
-- shared by every clan registered, the aggregation of the instances' validated results into the
-- clans' totals and the world's, the official result frozen at the close, the exclusive rewards of
-- the event (and the silver ones of the catch-up sessions), the administrators' live panel and its
-- journal. Every time stays in UTC.

-- The event suspended by an administrator (no entry into play, no report accepted until it is
-- resumed; the time lost is given back to the event at the resumption), its reason; the late
-- arrivals (minutes after the start during which a registered player may still enter play for the
-- first time, NULL: until the end; a player already placed always comes back); a catch-up session
-- (« session de rattrapage ») points to the official world event it follows and never changes its
-- result; the latest check of the titan's map (« Valider la carte », JSON).
ALTER TABLE raid_events ADD COLUMN suspended_at TEXT;
ALTER TABLE raid_events ADD COLUMN suspended_reason TEXT NOT NULL DEFAULT '';
ALTER TABLE raid_events ADD COLUMN late_minutes INTEGER CHECK (late_minutes IS NULL OR late_minutes BETWEEN 0 AND 1440);
ALTER TABLE raid_events ADD COLUMN catchup_of TEXT;
ALTER TABLE raid_events ADD COLUMN map_check TEXT;
CREATE INDEX raid_events_catchup ON raid_events (catchup_of);

-- The reconnections of an instance (a player back in play after an absence), counted by the
-- database itself when the relay writes a member active again (raid_instance_members, coop.ts).
ALTER TABLE raid_instances ADD COLUMN reconnects INTEGER NOT NULL DEFAULT 0;
CREATE TRIGGER raid_member_reconnect AFTER UPDATE OF active ON raid_instance_members
WHEN OLD.active = 0 AND NEW.active = 1
BEGIN
    UPDATE raid_instances SET reconnects = reconnects + 1 WHERE id = NEW.instance_id;
END;

-- The journal of a world event for its administrators: their actions (suspend, resume, the map
-- validated, a catch-up session opened…), the anomalies of the aggregation (an objective validated
-- by two instances: counted once) and its milestones (the start, the close). `once_key`: an entry
-- written once per event (the start, the close), whoever writes it first. The refused reports stay
-- in raid_report_rejections (0142); the live panel shows both.
CREATE TABLE raid_world_journal (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id   TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    kind       TEXT NOT NULL CHECK (kind IN ('admin', 'anomaly', 'info')),
    code       TEXT NOT NULL,
    once_key   TEXT,
    message    TEXT NOT NULL,
    data       TEXT NOT NULL DEFAULT '{}',
    actor_id   TEXT REFERENCES players (id) ON DELETE SET NULL,
    created_at TEXT NOT NULL,
    UNIQUE (event_id, once_key)
);
CREATE INDEX raid_world_journal_event ON raid_world_journal (event_id, created_at);

-- The official result of a world event, frozen once (after the grace of the last reports): the
-- world's progress, the clans', the groups' and the players' rankings from the validated results
-- only (JSON). Never changed afterwards (the trigger): a catch-up session, a late report or a
-- reconnection cannot touch it. `rewards_at`: the rewards of the final ranking granted;
-- `notified_at`: the clans and players told (each step once).
CREATE TABLE raid_event_results (
    event_id    TEXT PRIMARY KEY REFERENCES raid_events (id) ON DELETE CASCADE,
    outcome     TEXT NOT NULL CHECK (outcome IN ('vaincu', 'survecu')),
    data        TEXT NOT NULL,
    frozen_at   TEXT NOT NULL,
    rewards_at  TEXT,
    notified_at TEXT
);
CREATE TRIGGER raid_event_results_frozen
BEFORE UPDATE OF event_id, outcome, data, frozen_at ON raid_event_results
BEGIN
    SELECT RAISE(ABORT, 'official results are frozen');
END;

-- The titles a clan won in a world event (« Dévoreurs dévorés » : top 10 of the official ranking),
-- once per clan, event and title.
CREATE TABLE raid_clan_titles (
    clan_id    TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    event_id   TEXT NOT NULL,
    title      TEXT NOT NULL,
    rank       INTEGER NOT NULL CHECK (rank >= 1),
    granted_at TEXT NOT NULL,
    PRIMARY KEY (clan_id, event_id, title)
);
