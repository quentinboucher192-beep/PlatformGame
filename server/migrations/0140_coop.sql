-- 2.62: cooperation on the EXTRÊME levels, 2 or 3 players (docs/COOP.md).
--
-- coop_sessions: what the online relay (CoopRoom) saw of a game: which accounts played it,
-- by seat, and the SHA-256 of the inputs it relayed for the steps of the attempt. Written by
-- the relay only; a team replay of an online game must carry exactly these inputs.
CREATE TABLE coop_sessions (
    attempt_id  TEXT PRIMARY KEY,
    room_id     TEXT NOT NULL,
    level_id    TEXT NOT NULL,
    level_hash  TEXT NOT NULL,
    size        INTEGER NOT NULL CHECK (size IN (2, 3)),
    players     TEXT NOT NULL,           -- JSON array of player ids, by seat
    ticks       INTEGER,                 -- steps of the attempt (set at its end)
    inputs_hash TEXT,                    -- SHA-256 of the canonical inputs of those steps
    created_at  TEXT NOT NULL
);
CREATE INDEX idx_coop_sessions_created ON coop_sessions (created_at);

-- coop_runs: the team attempts of the « course à la vérification » (Duo: size 2, Trio: size 3;
-- they never mix, nor with the solo race_runs). A team is its exact players: team_key is their
-- ids sorted and joined by '+'. clan_tag is reserved for the clans (later), always NULL.
CREATE TABLE coop_runs (
    id           TEXT PRIMARY KEY,
    board        TEXT NOT NULL,          -- as race_runs.board (o:<level>:<hash> or c:<level>:<version>)
    size         INTEGER NOT NULL CHECK (size IN (2, 3)),
    team_key     TEXT NOT NULL,
    clan_tag     TEXT,
    attempt_id   TEXT NOT NULL UNIQUE,
    submitter_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    mode         TEXT NOT NULL CHECK (mode IN ('online', 'lan', 'same_pc')),
    claim        TEXT NOT NULL,
    time_ms      INTEGER NOT NULL,
    status       TEXT NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING', 'VERIFIED', 'REJECTED')),
    score        REAL,
    route        REAL,
    revives      INTEGER,
    incomplete   INTEGER,
    finished     INTEGER,
    detail       TEXT,
    reasons      TEXT,
    replay_key   TEXT,
    created_at   TEXT NOT NULL,
    verified_at  TEXT
);
CREATE INDEX idx_coop_runs_board ON coop_runs (board, size, status, score);
CREATE INDEX idx_coop_runs_team ON coop_runs (team_key);

-- The players of a team attempt, by seat.
CREATE TABLE coop_run_members (
    run_id    TEXT NOT NULL REFERENCES coop_runs (id) ON DELETE CASCADE,
    seat      INTEGER NOT NULL CHECK (seat BETWEEN 0 AND 2),
    player_id TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    PRIMARY KEY (run_id, seat)
);
CREATE INDEX idx_coop_members_player ON coop_run_members (player_id);

-- Each player confirms an attempt with their own account, from their own game (the submitter
-- cannot put anybody in a team without their consent): a team attempt is ranked once every
-- one of its players confirmed it. A confirmation may come before the attempt is submitted.
CREATE TABLE coop_confirms (
    attempt_id TEXT NOT NULL,
    player_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    created_at TEXT NOT NULL,
    PRIMARY KEY (attempt_id, player_id)
);
