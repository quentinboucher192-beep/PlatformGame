-- Challenges of the week and of the month (game 2.33, docs/online/CHALLENGES_PERIODS.md): the
-- daily challenges of 0049 become one table of three kinds, each over a UTC period:
--   daily   'daily-AAAA-MM-JJ'  the day,                   00:00Z -> 00:00Z
--   weekly  'weekly-AAAA-Www'   the ISO 8601 week,         Monday 00:00Z -> next Monday 00:00Z
--   monthly 'monthly-AAAA-MM'   the month,                 the 1st 00:00Z -> the 1st of next month
-- and a run sent after the end of its challenge (+ 10 min) is kept with late = 1 (it counts
-- neither for the streaks nor for the board of the challenge's own period).
--
-- SQLite cannot change a CHECK constraint (kind = 'daily', id = 'daily-' || date) nor the
-- table a foreign key points to: both tables are rebuilt, their rows copied unchanged (every
-- run of 0049 was received in time: late = 0). The frozen trigger keeps its meaning: a
-- challenge whose first day has begun never changes.

PRAGMA defer_foreign_keys = true;

CREATE TABLE challenges (
    id                   TEXT PRIMARY KEY,                      -- '<kind>-<period>'
    kind                 TEXT NOT NULL CHECK (kind IN ('daily', 'weekly', 'monthly')),
    period               TEXT NOT NULL,                         -- 'AAAA-MM-JJ' | 'AAAA-Www' (ISO 8601) | 'AAAA-MM'
    date                 TEXT NOT NULL                          -- first UTC day of the period
        CHECK (date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
    end_date             TEXT NOT NULL                          -- first UTC day after the period (exclusive end)
        CHECK (end_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]' AND end_date > date),
    source               TEXT NOT NULL CHECK (source IN ('auto', 'admin')),
    level_source         TEXT NOT NULL CHECK (level_source IN ('campaign', 'community')),
    level_id             TEXT NOT NULL,                         -- campaign level id, or community level UUID
    level_version        INTEGER NOT NULL,                      -- revision (campaign) or published version (community), frozen
    level_hash           TEXT NOT NULL,                         -- ranked fingerprint of that revision / version
    level_title          TEXT NOT NULL,
    hearts               INTEGER NOT NULL CHECK (hearts BETWEEN 1 AND 20),
    slots                INTEGER NOT NULL CHECK (slots BETWEEN 1 AND 8),
    powers               TEXT NOT NULL DEFAULT '[]',            -- JSON array of power ids, sorted ascending
    stations             INTEGER NOT NULL CHECK (stations IN (0, 1)),
    checkpoints          INTEGER NOT NULL CHECK (checkpoints IN (0, 1)),
    rules_hash           TEXT NOT NULL,                         -- SHA-256 of the canonical form of the rules
    silver_ms            INTEGER NOT NULL CHECK (silver_ms > 0),
    gold_ms              INTEGER NOT NULL CHECK (gold_ms > 0 AND gold_ms <= silver_ms),
    gold_no_damage       INTEGER NOT NULL DEFAULT 1 CHECK (gold_no_damage IN (0, 1)),
    -- Author of a community level chosen by the administrator (« créateur à l'honneur »):
    -- kept for the badges of a later step, notified once when the challenge begins.
    featured_player_id   TEXT REFERENCES players (id) ON DELETE SET NULL,
    featured_notified_at TEXT,
    created_at           TEXT NOT NULL,
    updated_at           TEXT NOT NULL,
    UNIQUE (kind, period),
    CHECK (id = kind || '-' || period),
    CHECK ((kind = 'daily' AND period = date AND end_date = date(date, '+1 day'))
        OR (kind = 'weekly' AND period GLOB '[0-9][0-9][0-9][0-9]-W[0-5][0-9]' AND strftime('%w', date) = '1'
            AND end_date = date(date, '+7 days'))
        OR (kind = 'monthly' AND period = substr(date, 1, 7) AND substr(date, 9, 2) = '01' AND end_date = date(date, '+1 month')))
);

INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title,
    hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id,
    featured_notified_at, created_at, updated_at)
SELECT id, 'daily', date, date, date(date, '+1 day'), source, level_source, level_id, level_version, level_hash, level_title,
    hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id,
    featured_notified_at, created_at, updated_at
FROM daily_challenges;

-- Runs submitted on a challenge: during it (until 10 minutes after its end) or later
-- (late = 1, « hors délai »: only in the board « Depuis », never in a streak). Idempotent by
-- (player, attempt). The tier is computed by the server; the status follows the decision
-- rules of the records (replay checked by the server).
CREATE TABLE challenge_runs_new (
    id                  TEXT PRIMARY KEY,
    challenge_id        TEXT NOT NULL REFERENCES challenges (id),
    player_id           TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    attempt_id          TEXT NOT NULL,
    time_ms             INTEGER NOT NULL,
    deaths              INTEGER NOT NULL,
    damage_taken        INTEGER NOT NULL,
    level_hash          TEXT NOT NULL,
    rules_hash          TEXT NOT NULL,
    game_version        TEXT NOT NULL,
    tier                TEXT NOT NULL CHECK (tier IN ('bronze', 'silver', 'gold')),
    status              TEXT NOT NULL CHECK (status IN ('PENDING', 'VERIFIED', 'REJECTED')),
    late                INTEGER NOT NULL DEFAULT 0 CHECK (late IN (0, 1)),
    rejection_reason    TEXT,
    verification_notes  TEXT,
    replay_key          TEXT,                                  -- private R2 key, served only through the API
    replay_status       TEXT CHECK (replay_status IN ('VERIFIED', 'REJECTED')),
    replay_sha256       TEXT,
    replay_size         INTEGER,
    replay_format       INTEGER,
    replay_analysis     TEXT,                                  -- JSON summary of the server analysis
    replay_at           TEXT,
    created_at          TEXT NOT NULL,
    verified_at         TEXT,
    UNIQUE (player_id, attempt_id)
);

INSERT INTO challenge_runs_new (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash,
    game_version, tier, status, late, rejection_reason, verification_notes, replay_key, replay_status, replay_sha256, replay_size,
    replay_format, replay_analysis, replay_at, created_at, verified_at)
SELECT id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash,
    game_version, tier, status, 0, rejection_reason, verification_notes, replay_key, replay_status, replay_sha256, replay_size,
    replay_format, replay_analysis, replay_at, created_at, verified_at
FROM challenge_runs;

DROP TABLE challenge_runs;
DROP TABLE daily_challenges;
ALTER TABLE challenge_runs_new RENAME TO challenge_runs;

CREATE INDEX idx_challenges_kind ON challenges (kind, date);
CREATE INDEX idx_challenges_span ON challenges (date, end_date);
CREATE INDEX idx_challenges_level ON challenges (level_source, level_id, date);
CREATE INDEX idx_challenges_featured ON challenges (featured_player_id);
CREATE INDEX idx_challenge_runs_board ON challenge_runs (challenge_id, status, late, time_ms, created_at);
CREATE INDEX idx_challenge_runs_player ON challenge_runs (player_id, challenge_id, status);
CREATE INDEX idx_challenge_runs_held ON challenge_runs (status, replay_status, created_at);

-- A challenge whose first day has begun is frozen. Only the creator notification
-- bookkeeping (featured_notified_at) and the author link (set to NULL when that account is
-- deleted) may still change; created_at / updated_at follow the rest.
CREATE TRIGGER challenges_frozen
BEFORE UPDATE OF id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
    slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage ON challenges
WHEN OLD.date <= date('now')
BEGIN
    SELECT RAISE(ABORT, 'challenge already started: it cannot change');
END;
