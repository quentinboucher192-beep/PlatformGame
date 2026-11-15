-- Daily challenges (« Défis du jour », game 2.31, docs/online/DAILY_CHALLENGES.md): one
-- challenge per UTC day, drawn by the server (campaign levels only) or chosen by the
-- administrator (scripts/challenge-set.mjs, campaign or community level), and the runs
-- players submit on it. A challenge is drawn up to 7 days ahead but never shown before its
-- day; once its day has begun it never changes (the trigger below, on top of the guards of
-- every statement that writes it).

CREATE TABLE daily_challenges (
    id                   TEXT PRIMARY KEY,                      -- 'daily-AAAA-MM-JJ'
    kind                 TEXT NOT NULL DEFAULT 'daily' CHECK (kind = 'daily'),
    date                 TEXT NOT NULL UNIQUE                   -- UTC day 'AAAA-MM-JJ' (00:00Z to 24:00Z)
        CHECK (date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
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
    -- Author of a community level chosen for the day (« créateur à l'honneur »): kept for the
    -- badges of a later step, and notified once when the challenge becomes the current one.
    featured_player_id   TEXT REFERENCES players (id) ON DELETE SET NULL,
    featured_notified_at TEXT,
    created_at           TEXT NOT NULL,
    updated_at           TEXT NOT NULL,
    CHECK (id = 'daily-' || date)
);
CREATE INDEX idx_daily_challenges_level ON daily_challenges (level_source, level_id, date);
CREATE INDEX idx_daily_challenges_featured ON daily_challenges (featured_player_id);

-- A challenge whose day has begun is frozen. Only the creator notification bookkeeping
-- (featured_notified_at) and the author link (set to NULL when that account is deleted) may
-- still change; created_at / updated_at follow the rest.
CREATE TRIGGER daily_challenges_frozen
BEFORE UPDATE OF id, kind, date, source, level_source, level_id, level_version, level_hash, level_title, hearts, slots, powers,
    stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage ON daily_challenges
WHEN OLD.date <= date('now')
BEGIN
    SELECT RAISE(ABORT, 'daily challenge already started: it cannot change');
END;

-- Runs submitted on a challenge (from its start to 10 minutes after its end). Idempotent by
-- (player, attempt). The tier is computed by the server; the status follows the decision
-- rules of the records (replay checked by the server).
CREATE TABLE challenge_runs (
    id                  TEXT PRIMARY KEY,
    challenge_id        TEXT NOT NULL REFERENCES daily_challenges (id),
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
CREATE INDEX idx_challenge_runs_board ON challenge_runs (challenge_id, status, time_ms, created_at);
CREATE INDEX idx_challenge_runs_player ON challenge_runs (player_id, challenge_id, status);
CREATE INDEX idx_challenge_runs_held ON challenge_runs (status, replay_status, created_at);
