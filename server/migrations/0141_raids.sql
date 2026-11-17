-- Clan raids « ASTERION, LE DÉVOREUR DE MONDES », phase 1 (raids.ts): the events and the
-- registrations, without the combat (later phases). Every time is stored in UTC (ISO 8601,
-- « …Z »); each game converts to its own time zone and never sends one.
--
-- raid_events: a world event (kind 'world', scheduled by the game's administrators, a draft
-- until published) or a clan raid (kind 'clan', scheduled by the chief or an officer of
-- `clan_id`, internal to that clan and to the clans it invites). lock_at = starts_at minus
-- lock_minutes (registrations locked), ends_at = starts_at plus the format's longest duration:
-- both written by the server with starts_at, so that the rules are plain comparisons.
CREATE TABLE raid_events (
    id                TEXT PRIMARY KEY,
    kind              TEXT NOT NULL CHECK (kind IN ('world', 'clan')),
    raid              TEXT NOT NULL,                       -- the definition (raids.ts RAIDS)
    format            TEXT NOT NULL CHECK (format IN ('court', 'standard', 'legendaire')),
    clan_id           TEXT REFERENCES clans (id) ON DELETE CASCADE,
    starts_at         TEXT NOT NULL,
    lock_minutes      INTEGER NOT NULL DEFAULT 30 CHECK (lock_minutes BETWEEN 0 AND 1440),
    lock_at           TEXT NOT NULL,
    ends_at           TEXT NOT NULL,
    registration_open INTEGER NOT NULL DEFAULT 1 CHECK (registration_open IN (0, 1)),
    capacity          INTEGER NOT NULL DEFAULT 0 CHECK (capacity >= 0),          -- players in all, 0: no limit
    clan_capacity     INTEGER NOT NULL DEFAULT 50 CHECK (clan_capacity BETWEEN 1 AND 50),
    difficulty        INTEGER NOT NULL DEFAULT 3 CHECK (difficulty BETWEEN 1 AND 5),
    rewards_text      TEXT NOT NULL DEFAULT '',
    published_at      TEXT,                                -- NULL: a world event's draft
    cancelled_at      TEXT,
    created_by        TEXT REFERENCES players (id) ON DELETE SET NULL,
    created_at        TEXT NOT NULL,
    updated_at        TEXT NOT NULL,
    CHECK ((kind = 'clan') = (clan_id IS NOT NULL))
);
CREATE INDEX raid_events_start ON raid_events (starts_at);
CREATE INDEX raid_events_clan ON raid_events (clan_id, starts_at);

-- The clans taking part in an event: registered by their chief or an officer (world event), the
-- organizer and the clans that accepted its invitation (clan raid). Each has its raid leader
-- (one of its members) and its internal instructions.
CREATE TABLE raid_clan_entries (
    event_id      TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    clan_id       TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    registered_by TEXT REFERENCES players (id) ON DELETE SET NULL,
    leader_id     TEXT REFERENCES players (id) ON DELETE SET NULL,
    instructions  TEXT NOT NULL DEFAULT '',
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL,
    PRIMARY KEY (event_id, clan_id)
);
CREATE INDEX raid_entries_clan ON raid_clan_entries (clan_id);

-- The players registered (themselves), with the clan they take part for. The capacity of the
-- event and of each clan is checked by the INSERT itself (one statement: two registrations at
-- the same moment can never both take the last place).
CREATE TABLE raid_registrations (
    event_id   TEXT NOT NULL,
    player_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    clan_id    TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY (event_id, player_id),
    FOREIGN KEY (event_id, clan_id) REFERENCES raid_clan_entries (event_id, clan_id) ON DELETE CASCADE
);
CREATE INDEX raid_registrations_clan ON raid_registrations (event_id, clan_id, created_at);
CREATE INDEX raid_registrations_player ON raid_registrations (player_id);

-- A clan raid in common: the organizer invites other clans; their chief or an officer answers.
CREATE TABLE raid_invitations (
    event_id    TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    clan_id     TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    by_player   TEXT REFERENCES players (id) ON DELETE SET NULL,
    status      TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'accepted', 'declined')),
    created_at  TEXT NOT NULL,
    answered_at TEXT,
    PRIMARY KEY (event_id, clan_id)
);
CREATE INDEX raid_invitations_clan ON raid_invitations (clan_id, status);

-- The reminders sent (24 h, 1 h, 10 min before, and 0: the start), per player and per start
-- time: written in the same transaction as the notifications, a reminder is never sent twice
-- (an event moved to another time reminds again).
CREATE TABLE raid_reminders_sent (
    event_id       TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    player_id      TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    offset_minutes INTEGER NOT NULL,
    starts_at      TEXT NOT NULL,
    sent_at        TEXT NOT NULL,
    PRIMARY KEY (event_id, player_id, offset_minutes, starts_at)
);

-- Each player's reminders (minutes before the start, among 1440, 60 and 10) and whether the
-- (un)registrations of their clan's members reach them (grouped) when they lead it.
CREATE TABLE raid_prefs (
    player_id      TEXT PRIMARY KEY REFERENCES players (id) ON DELETE CASCADE,
    reminders      TEXT NOT NULL DEFAULT '1440,60,10',
    member_updates INTEGER NOT NULL DEFAULT 1 CHECK (member_updates IN (0, 1))
);
