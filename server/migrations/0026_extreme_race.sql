-- Cosmic Online — EXTRÊME levels and their « course à la vérification ». An extreme level is
-- played with one life; every attempt is ranked by its progress score (route 40 points,
-- bosses and ambushes 60), computed by the server from the attempt's replay. The first
-- player to finish becomes the level's verifier. A community extreme level may be published
-- without its author's completion run. Append-only (see 0001).

ALTER TABLE community_levels ADD COLUMN extreme INTEGER NOT NULL DEFAULT 0;
ALTER TABLE community_levels ADD COLUMN verified_by TEXT REFERENCES players(id) ON DELETE SET NULL;
ALTER TABLE community_levels ADD COLUMN verified_at TEXT;
ALTER TABLE community_levels ADD COLUMN verified_version INTEGER;
-- The race layout of a version (intended route, bosses, ambushes), declared with it.
ALTER TABLE community_level_versions ADD COLUMN race TEXT;
ALTER TABLE level_versions ADD COLUMN race TEXT;

CREATE TABLE race_runs (
    id TEXT PRIMARY KEY,
    board TEXT NOT NULL,                       -- 'c:<level>:<version>' (community) or 'o:<level>:<level hash>' (official)
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    attempt_id TEXT NOT NULL,
    claim TEXT NOT NULL,                       -- what the game declared (checked against the replay)
    time_ms INTEGER NOT NULL,
    score REAL,                                -- computed by the server from the replay
    route REAL,
    bosses_beaten INTEGER,
    waves INTEGER,
    finished INTEGER NOT NULL DEFAULT 0,
    detail TEXT,                               -- per boss and ambush (display)
    status TEXT NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING', 'VERIFIED', 'REJECTED')),
    reasons TEXT,
    replay_key TEXT,
    created_at TEXT NOT NULL,
    verified_at TEXT,
    UNIQUE (player_id, attempt_id)
);
CREATE INDEX race_runs_board ON race_runs(board, status, score DESC, verified_at);
CREATE INDEX race_runs_player ON race_runs(player_id, created_at);

-- The first to finish (once per board, never replaced).
CREATE TABLE race_victors (
    board TEXT PRIMARY KEY,
    player_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    run_id TEXT,
    at TEXT NOT NULL
);
