-- 2.65: clan raids « Asterion », phase 2 (raidZones.ts, docs/RAIDS.md): the branch « BRAS DROIT »,
-- its instances of up to 8 players (a group of a clan, relayed by a CoopRoom in raid mode), the
-- reports of their games and the consensus that accepts them, the state of the branch's zones
-- (objectives done, idempotent by objective id) and the journal of the rejected reports.

-- An instance: one group of one clan on one branch of an event (its relay is the Durable Object
-- named by its id). `zone`: where the group is (accepted zone reports); `players`: its active
-- players now (the relay writes it), the base of the majority.
CREATE TABLE raid_instances (
    id         TEXT PRIMARY KEY,
    event_id   TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    clan_id    TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    group_no   INTEGER NOT NULL CHECK (group_no >= 1),
    branch     TEXT NOT NULL,
    zone       INTEGER NOT NULL DEFAULT 1 CHECK (zone BETWEEN 1 AND 32),
    players    INTEGER NOT NULL DEFAULT 0 CHECK (players BETWEEN 0 AND 8),
    started_at TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    UNIQUE (event_id, clan_id, group_no, branch)
);
CREATE INDEX raid_instances_event ON raid_instances (event_id, branch);

-- Who plays in an instance (written by its relay): `active` while in play (not absent, not
-- catching up), `left_at` once its seat was freed (60 s of absence).
CREATE TABLE raid_instance_members (
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    seat        INTEGER NOT NULL CHECK (seat BETWEEN 0 AND 7),
    active      INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),
    joined_at   TEXT NOT NULL,
    left_at     TEXT,
    PRIMARY KEY (instance_id, player_id)
);

-- One report per player and key ("o:<objective>" or "z:<zone>@<step>"): its value
-- "<zone>:<step>:<digest>".
CREATE TABLE raid_reports (
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    report_key  TEXT NOT NULL,
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    value       TEXT NOT NULL,
    tick        INTEGER NOT NULL,
    created_at  TEXT NOT NULL,
    PRIMARY KEY (instance_id, report_key, player_id)
);

-- The value a majority of the instance agreed on, once per key.
CREATE TABLE raid_accepted (
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    report_key  TEXT NOT NULL,
    value       TEXT NOT NULL,
    accepted_at TEXT NOT NULL,
    PRIMARY KEY (instance_id, report_key)
);

-- The state of a branch: its objectives done (once each, whoever did it first).
CREATE TABLE raid_branch_objectives (
    event_id     TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    branch       TEXT NOT NULL,
    objective_id TEXT NOT NULL,
    instance_id  TEXT REFERENCES raid_instances (id) ON DELETE SET NULL,
    accepted_at  TEXT NOT NULL,
    PRIMARY KEY (event_id, branch, objective_id)
);

-- The journal of the reports refused (a minority that disagreed, an impossible report).
CREATE TABLE raid_report_rejections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    instance_id TEXT NOT NULL REFERENCES raid_instances (id) ON DELETE CASCADE,
    player_id   TEXT REFERENCES players (id) ON DELETE SET NULL,
    report_key  TEXT NOT NULL,
    value       TEXT NOT NULL,
    reason      TEXT NOT NULL,
    created_at  TEXT NOT NULL
);
CREATE INDEX raid_report_rejections_instance ON raid_report_rejections (instance_id, created_at);
