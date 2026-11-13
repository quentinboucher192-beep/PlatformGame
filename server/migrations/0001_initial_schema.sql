-- Cosmic Online Ranking - initial schema (D1 / SQLite).
-- Migrations are append-only: never edit an applied migration, add a new file instead.

-- ------------------------------------------------------------------ players
-- Public identity and privacy settings. Passwords live in player_credentials.
CREATE TABLE players (
    id                    TEXT PRIMARY KEY,               -- UUID v4
    username              TEXT NOT NULL,
    username_normalized   TEXT NOT NULL UNIQUE,           -- lower-case, used for uniqueness and search
    avatar                TEXT NOT NULL DEFAULT 'avatar_01',
    avatar_custom_key     TEXT,                           -- R2 key of an uploaded avatar (validated PNG)
    country_code          TEXT,                           -- ISO 3166-1 alpha-2, never a precise location
    region_code           TEXT,                           -- optional large region (ISO 3166-2), never a city
    location_visibility   TEXT NOT NULL DEFAULT 'hidden'
        CHECK (location_visibility IN ('hidden', 'country', 'globe')),
    profile_visibility    TEXT NOT NULL DEFAULT 'public'
        CHECK (profile_visibility IN ('public', 'private')),
    allow_friend_requests INTEGER NOT NULL DEFAULT 1 CHECK (allow_friend_requests IN (0, 1)),
    trust_level           INTEGER NOT NULL DEFAULT 0 CHECK (trust_level BETWEEN 0 AND 3),
    tos_version           TEXT NOT NULL,
    tos_accepted_at       TEXT NOT NULL,
    created_at            TEXT NOT NULL,
    last_seen_at          TEXT NOT NULL,
    account_status        TEXT NOT NULL DEFAULT 'active'
        CHECK (account_status IN ('active', 'suspended'))
);
CREATE INDEX idx_players_country ON players (country_code, location_visibility);

CREATE TABLE player_credentials (
    player_id           TEXT PRIMARY KEY REFERENCES players (id) ON DELETE CASCADE,
    password_hash       TEXT NOT NULL,                    -- PBKDF2-HMAC-SHA256, base64
    password_salt       TEXT NOT NULL,                    -- 16 random bytes, base64
    password_iterations INTEGER NOT NULL,
    failed_attempts     INTEGER NOT NULL DEFAULT 0,
    locked_until        TEXT,
    updated_at          TEXT NOT NULL
);

-- Opaque tokens are only stored hashed (SHA-256). The access token expires quickly,
-- the refresh token is rotated on every use.
CREATE TABLE player_sessions (
    id                     TEXT PRIMARY KEY,
    player_id              TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    token_hash             TEXT NOT NULL UNIQUE,
    refresh_hash           TEXT NOT NULL UNIQUE,
    previous_refresh_hash  TEXT,
    rotated_at             TEXT,
    created_at             TEXT NOT NULL,
    expires_at             TEXT NOT NULL,                 -- access token expiry
    refresh_expires_at     TEXT NOT NULL,
    last_used_at           TEXT NOT NULL,
    revoked_at             TEXT,
    client_info            TEXT
);
CREATE INDEX idx_sessions_player ON player_sessions (player_id);
CREATE INDEX idx_sessions_previous ON player_sessions (previous_refresh_hash);
CREATE INDEX idx_sessions_expiry ON player_sessions (refresh_expires_at);

-- ------------------------------------------------------------------- levels
CREATE TABLE levels (
    id               TEXT PRIMARY KEY,
    name             TEXT NOT NULL,
    planet_id        TEXT NOT NULL,
    planet_name      TEXT NOT NULL DEFAULT '',
    level_order      INTEGER NOT NULL DEFAULT 0,
    current_version  INTEGER NOT NULL,
    content_hash     TEXT NOT NULL,                       -- hash of current_version
    ranking_enabled  INTEGER NOT NULL DEFAULT 1 CHECK (ranking_enabled IN (0, 1))
);
CREATE INDEX idx_levels_planet ON levels (planet_id, level_order);

-- Every published revision of a level. Records are always tied to one revision: a
-- leaderboard never mixes two content hashes.
CREATE TABLE level_versions (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    level_id             TEXT NOT NULL REFERENCES levels (id),
    version              INTEGER NOT NULL,
    content_hash         TEXT NOT NULL,
    compatibility_status TEXT NOT NULL DEFAULT 'active'
        CHECK (compatibility_status IN ('active', 'legacy', 'disabled')),
    min_game_version     TEXT NOT NULL,
    total_collectibles   INTEGER NOT NULL,
    total_enemies        INTEGER NOT NULL DEFAULT 0,
    time_limit_ms        INTEGER NOT NULL,
    min_time_ms          INTEGER NOT NULL,                -- below this a time is impossible
    max_speed_px_s       REAL NOT NULL DEFAULT 1500,       -- replay plausibility bound
    bounds_w             REAL NOT NULL,
    bounds_h             REAL NOT NULL,
    spawn_x              REAL NOT NULL,
    spawn_y              REAL NOT NULL,
    exit_x               REAL NOT NULL,
    exit_y               REAL NOT NULL,
    exit_w               REAL NOT NULL,
    exit_h               REAL NOT NULL,
    created_at           TEXT NOT NULL,
    UNIQUE (level_id, version),
    UNIQUE (level_id, content_hash)
);

-- ------------------------------------------------------------------ records
-- Every submitted attempt (history is kept). One attempt can count in several
-- categories: (player_id, attempt_id, category) is unique, which makes a resubmission
-- idempotent.
CREATE TABLE records (
    id                  TEXT PRIMARY KEY,
    player_id           TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    level_id            TEXT NOT NULL REFERENCES levels (id),
    level_version       INTEGER NOT NULL,
    level_hash          TEXT NOT NULL,
    mode                TEXT NOT NULL DEFAULT 'STANDARD',
    category            TEXT NOT NULL
        CHECK (category IN ('ANY_PERCENT', 'HUNDRED_PERCENT', 'NO_DEATH', 'NO_DAMAGE', 'SPEEDRUN')),
    time_ms             INTEGER NOT NULL,
    deaths              INTEGER NOT NULL,
    damage_taken        INTEGER NOT NULL DEFAULT 0,
    collectibles        INTEGER NOT NULL DEFAULT 0,
    total_collectibles  INTEGER NOT NULL DEFAULT 0,
    kills               INTEGER NOT NULL DEFAULT 0,
    score               INTEGER NOT NULL DEFAULT 0,
    game_version        TEXT NOT NULL,
    attempt_id          TEXT NOT NULL,
    verification_status TEXT NOT NULL
        CHECK (verification_status IN ('PENDING', 'VERIFIED', 'REJECTED')),
    rejection_reason    TEXT,
    verification_notes  TEXT,
    replay_id           TEXT,
    created_at          TEXT NOT NULL,
    verified_at         TEXT,
    UNIQUE (player_id, attempt_id, category)
);
CREATE INDEX idx_records_board ON records (level_id, level_hash, category, verification_status, time_ms);
CREATE INDEX idx_records_player ON records (player_id, level_id, category, created_at);
CREATE INDEX idx_records_week ON records (level_hash, category, verification_status, created_at);
CREATE INDEX idx_records_status ON records (verification_status, created_at);

-- Historic leaderboard: best VERIFIED record of each player for each level revision and
-- category. Maintained by the API when a record is verified.
CREATE TABLE player_bests (
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    level_id    TEXT NOT NULL,
    level_hash  TEXT NOT NULL,
    category    TEXT NOT NULL,
    record_id   TEXT NOT NULL REFERENCES records (id) ON DELETE CASCADE,
    time_ms     INTEGER NOT NULL,
    achieved_at TEXT NOT NULL,
    PRIMARY KEY (player_id, level_hash, category)
);
CREATE INDEX idx_bests_board ON player_bests (level_hash, category, time_ms, achieved_at);
CREATE INDEX idx_bests_level ON player_bests (level_id, category);

-- ------------------------------------------------------------------ replays
CREATE TABLE replay_metadata (
    id                  TEXT PRIMARY KEY,
    record_id           TEXT NOT NULL REFERENCES records (id) ON DELETE CASCADE,
    player_id           TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    attempt_id          TEXT NOT NULL,
    storage_key         TEXT NOT NULL,                    -- private R2 key, served only through the API
    file_size           INTEGER NOT NULL,
    sha256              TEXT NOT NULL,
    format_version      INTEGER NOT NULL,
    frame_count         INTEGER NOT NULL,
    duration_ms         INTEGER NOT NULL,
    verification_status TEXT NOT NULL
        CHECK (verification_status IN ('PENDING', 'VERIFIED', 'REJECTED')),
    analysis            TEXT,                             -- JSON summary of the server analysis
    created_at          TEXT NOT NULL,
    UNIQUE (player_id, attempt_id)
);
CREATE INDEX idx_replays_record ON replay_metadata (record_id);

-- ------------------------------------------------------------------ friends
-- One row per direction. A request is (requester -> target, 'pending'); acceptance turns
-- it into two 'accepted' rows.
CREATE TABLE player_friends (
    player_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    friend_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    status     TEXT NOT NULL CHECK (status IN ('pending', 'accepted')),
    created_at TEXT NOT NULL,
    PRIMARY KEY (player_id, friend_id),
    CHECK (player_id <> friend_id)
);
CREATE INDEX idx_friends_target ON player_friends (friend_id, status);

-- ---------------------------------------------------------------- countries
-- Approximate representation points (centre of each country). Players are never shown
-- anywhere more precise than this.
CREATE TABLE countries (
    code      TEXT PRIMARY KEY,
    name_fr   TEXT NOT NULL,
    name_en   TEXT NOT NULL,
    latitude  REAL,
    longitude REAL
);

-- ---------------------------------------------------------------- operations
CREATE TABLE rate_limits (
    bucket       TEXT PRIMARY KEY,                        -- "<rule>:<hashed key>"
    window_start INTEGER NOT NULL,                        -- unix seconds
    count        INTEGER NOT NULL
);

-- Sensitive operations (logins, account changes, moderation). IPs are stored hashed.
CREATE TABLE audit_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    player_id  TEXT,
    action     TEXT NOT NULL,
    ip_hash    TEXT,
    details    TEXT,
    created_at TEXT NOT NULL
);
CREATE INDEX idx_audit_player ON audit_log (player_id, created_at);
CREATE INDEX idx_audit_created ON audit_log (created_at);
