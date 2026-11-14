-- Community levels: levels made by players, published as validated .cplanet packages in R2,
-- with versions, ratings, reports and per-level leaderboards. Deleted with their author.

CREATE TABLE community_levels (
    id              TEXT PRIMARY KEY,                -- UUID
    author_id       TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    title           TEXT NOT NULL,
    title_norm      TEXT NOT NULL,                   -- lowercase, for the search
    description     TEXT NOT NULL DEFAULT '',
    planet          TEXT NOT NULL,
    difficulty      REAL,                            -- computed rating declared by the game (0.5 .. 5)
    status          TEXT NOT NULL DEFAULT 'draft' CHECK (status IN ('draft', 'published', 'hidden', 'removed')),
    current_version INTEGER NOT NULL DEFAULT 0,      -- 0 until a version is published
    downloads       INTEGER NOT NULL DEFAULT 0,
    completions     INTEGER NOT NULL DEFAULT 0,
    rating_sum      INTEGER NOT NULL DEFAULT 0,
    rating_count    INTEGER NOT NULL DEFAULT 0,
    report_count    INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL
);
CREATE INDEX idx_community_levels_author ON community_levels (author_id);
CREATE INDEX idx_community_levels_status ON community_levels (status, updated_at);

CREATE TABLE community_level_versions (
    level_id          TEXT NOT NULL REFERENCES community_levels (id) ON DELETE CASCADE,
    version           INTEGER NOT NULL,
    status            TEXT NOT NULL DEFAULT 'uploading' CHECK (status IN ('uploading', 'published', 'superseded')),
    level_hash        TEXT NOT NULL,                 -- ranked fingerprint declared by the game (replays must match)
    package_key       TEXT,                          -- R2 key once validated
    package_sha256    TEXT,
    package_size      INTEGER,
    thumbnail_key     TEXT,
    replay_key        TEXT,                          -- the author's completion replay
    completion_ms     INTEGER,
    min_game_version  TEXT NOT NULL,
    bounds_w REAL, bounds_h REAL, spawn_x REAL, spawn_y REAL, exit_x REAL, exit_y REAL, exit_w REAL, exit_h REAL,
    time_limit_ms     INTEGER,
    created_at        TEXT NOT NULL,
    published_at      TEXT,
    PRIMARY KEY (level_id, version)
);

CREATE TABLE community_ratings (
    level_id   TEXT NOT NULL REFERENCES community_levels (id) ON DELETE CASCADE,
    player_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    stars      INTEGER NOT NULL CHECK (stars BETWEEN 1 AND 5),
    created_at TEXT NOT NULL,
    PRIMARY KEY (level_id, player_id)
);

CREATE TABLE community_reports (
    id          TEXT PRIMARY KEY,
    level_id    TEXT NOT NULL REFERENCES community_levels (id) ON DELETE CASCADE,
    reporter_id TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    reason      TEXT,
    created_at  TEXT NOT NULL,
    UNIQUE (level_id, reporter_id)
);

-- Per-level leaderboard: one best time per player and version; a time counts only once its
-- replay has been checked (structure, level version, duration, trajectory).
CREATE TABLE community_records (
    id          TEXT PRIMARY KEY,
    level_id    TEXT NOT NULL REFERENCES community_levels (id) ON DELETE CASCADE,
    version     INTEGER NOT NULL,
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    attempt_id  TEXT NOT NULL,
    time_ms     INTEGER NOT NULL,
    deaths      INTEGER NOT NULL DEFAULT 0,
    status      TEXT NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING', 'REPLAY_OK', 'REJECTED')),
    replay_key  TEXT,
    reasons     TEXT,
    created_at  TEXT NOT NULL,
    UNIQUE (player_id, attempt_id)
);
CREATE INDEX idx_community_records_board ON community_records (level_id, version, status, time_ms);
