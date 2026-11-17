-- 2.59 ADMINISTRATION › Créations: three more kinds besides the suits (creations.ts) — the
-- trails (« Traînées »), the avatar frames (« Cadres ») and the victory effects (« Effets de
-- victoire »). Each is two colours and a style among those every game already draws (the
-- lists: creations.ts, TRAIL_STYLES, FRAME_STYLES, VICTORY_STYLES); a trail also has its width,
-- how long it lasts and its sparkles. They are given as rewards of a challenge like the suits
-- (reward_kind 'trail', 'frame' or 'victory'; reward_ref 'srvtrail:<id>', 'srvframe:<id>',
-- 'srvvictory:<id>') and worn only once won. Display only: no run, replay or record changes.
--
-- SQLite cannot change a CHECK constraint: the three tables that name the kinds are rebuilt,
-- every row copied unchanged (the ids of the creations kept), nothing removed.

PRAGMA defer_foreign_keys = true;

-- A suit keeps its three colours (suit_color, accent_color, scarf_color: NULL for the other
-- kinds). A trail (start, end), a frame (border, glow) or a victory effect (main, second): two
-- colours color_a, color_b, '#rrggbb' in lower case, and its style. A trail: width (percent of
-- the usual one, 50 to 200), life_ms (how long its trace lasts, 200 to 1000 ms), particles
-- (sparkles around it, 0 or 1); NULL for the other kinds.
CREATE TABLE creations_new (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    kind          TEXT NOT NULL CHECK (kind IN ('suit', 'trail', 'frame', 'victory')),
    name          TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 32),
    name_key      TEXT NOT NULL,
    suit_color    TEXT CHECK (suit_color IS NULL OR suit_color GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    accent_color  TEXT CHECK (accent_color IS NULL OR accent_color GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    scarf_color   TEXT CHECK (scarf_color IS NULL OR scarf_color GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    color_a       TEXT CHECK (color_a IS NULL OR color_a GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    color_b       TEXT CHECK (color_b IS NULL OR color_b GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    style         TEXT CHECK (style IS NULL OR (length(style) BETWEEN 1 AND 24 AND style NOT GLOB '*[^a-z_]*')),
    width         INTEGER CHECK (width IS NULL OR width BETWEEN 50 AND 200),
    life_ms       INTEGER CHECK (life_ms IS NULL OR life_ms BETWEEN 200 AND 1000),
    particles     INTEGER CHECK (particles IS NULL OR particles IN (0, 1)),
    created_by    TEXT REFERENCES players (id) ON DELETE SET NULL,
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL,
    archived      INTEGER NOT NULL DEFAULT 0 CHECK (archived IN (0, 1)),
    archived_at   TEXT,
    CHECK (kind <> 'suit' OR (suit_color IS NOT NULL AND accent_color IS NOT NULL AND scarf_color IS NOT NULL)),
    CHECK (kind = 'suit' OR (suit_color IS NULL AND accent_color IS NULL AND scarf_color IS NULL)),
    CHECK (kind = 'suit' OR (color_a IS NOT NULL AND color_b IS NOT NULL AND style IS NOT NULL)),
    CHECK (kind <> 'suit' OR (color_a IS NULL AND color_b IS NULL AND style IS NULL)),
    CHECK (kind <> 'trail' OR (width IS NOT NULL AND life_ms IS NOT NULL AND particles IS NOT NULL)),
    CHECK (kind = 'trail' OR (width IS NULL AND life_ms IS NULL AND particles IS NULL))
);
INSERT INTO creations_new (id, kind, name, name_key, suit_color, accent_color, scarf_color, created_by, created_at, updated_at, archived, archived_at)
SELECT id, kind, name, name_key, suit_color, accent_color, scarf_color, created_by, created_at, updated_at, archived, archived_at FROM creations;
DROP TABLE creations;
ALTER TABLE creations_new RENAME TO creations;
CREATE UNIQUE INDEX creations_name ON creations (kind, name_key) WHERE archived = 0;
CREATE INDEX creations_kind ON creations (kind, archived, id);

-- The rewards of the challenges may now be these creations too (0087 for the suits).
CREATE TABLE challenge_reward_rules_new (
    challenge_id    TEXT NOT NULL,
    position        INTEGER NOT NULL CHECK (position BETWEEN 0 AND 15),
    reward_kind     TEXT NOT NULL CHECK (reward_kind IN ('item', 'title', 'suit', 'trail', 'frame', 'victory')),
    reward_ref      TEXT NOT NULL,
    condition       TEXT NOT NULL CHECK (condition IN ('tier', 'top', 'finished')),
    condition_value INTEGER NOT NULL,
    PRIMARY KEY (challenge_id, position)
);
INSERT INTO challenge_reward_rules_new (challenge_id, position, reward_kind, reward_ref, condition, condition_value)
SELECT challenge_id, position, reward_kind, reward_ref, condition, condition_value FROM challenge_reward_rules;
DROP TABLE challenge_reward_rules;
ALTER TABLE challenge_reward_rules_new RENAME TO challenge_reward_rules;

CREATE TABLE player_won_rewards_new (
    player_id    TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    challenge_id TEXT NOT NULL,
    reward_kind  TEXT NOT NULL CHECK (reward_kind IN ('item', 'title', 'suit', 'trail', 'frame', 'victory')),
    reward_ref   TEXT NOT NULL,
    granted_at   TEXT NOT NULL,
    PRIMARY KEY (player_id, challenge_id, reward_kind, reward_ref)
);
INSERT INTO player_won_rewards_new (player_id, challenge_id, reward_kind, reward_ref, granted_at)
SELECT player_id, challenge_id, reward_kind, reward_ref, granted_at FROM player_won_rewards;
DROP TABLE player_won_rewards;
ALTER TABLE player_won_rewards_new RENAME TO player_won_rewards;
CREATE INDEX player_won_rewards_challenge ON player_won_rewards (challenge_id, granted_at);
CREATE INDEX player_won_rewards_ref ON player_won_rewards (reward_kind, reward_ref);
