-- « Plus loin » (game 2.34, docs/online/CHALLENGES_PERIODS.md §5): the attempts of a
-- challenge that did not reach the end, ranked by their progress, like the « course à la
-- vérification » (src/race.ts): 40 points for the route reached along the layout of the
-- level version (level_versions.race / community_level_versions.race), 60 for the bosses'
-- health removed and the ambushes' waves beaten. The score is computed by the server from the
-- attempt's replay, never taken from the game; a finished run (challenge_runs) counts 100.
-- Only the player's best attempt of a challenge keeps its replay.

CREATE TABLE challenge_attempts (
    id              TEXT PRIMARY KEY,
    challenge_id    TEXT NOT NULL REFERENCES challenges (id),
    player_id       TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    attempt_id      TEXT NOT NULL,                             -- the game's id of the attempt (UUID), unique per player
    time_ms         INTEGER NOT NULL,
    level_hash      TEXT NOT NULL,
    rules_hash      TEXT NOT NULL,
    game_version    TEXT NOT NULL,
    claim           TEXT NOT NULL,                             -- JSON { finished, bosses, waves } as declared: compared with the replay, never trusted
    late            INTEGER NOT NULL DEFAULT 0 CHECK (late IN (0, 1)),
    status          TEXT NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING', 'VERIFIED', 'REJECTED')),
    progress        REAL CHECK (progress IS NULL OR (progress >= 0 AND progress < 100)),
    route           REAL,                                      -- furthest fraction of the layout's route reached
    detail          TEXT,                                      -- JSON: bosses' health removed, ambushes' waves beaten
    reasons         TEXT,                                      -- JSON: why the attempt was refused
    replay_key      TEXT,                                      -- private R2 key (the player's best attempt only)
    replay_sha256   TEXT,
    replay_size     INTEGER,
    replay_at       TEXT,
    created_at      TEXT NOT NULL,
    verified_at     TEXT,
    UNIQUE (player_id, attempt_id)
);
CREATE INDEX idx_challenge_attempts_board ON challenge_attempts (challenge_id, status, late, progress, created_at);
CREATE INDEX idx_challenge_attempts_player ON challenge_attempts (player_id, challenge_id, status);
