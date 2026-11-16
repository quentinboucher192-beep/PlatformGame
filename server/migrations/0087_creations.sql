-- ADMINISTRATION › Créations: things an administrator makes on the server, that every game
-- draws without an update (creations.ts). For now one kind: the suits (« Combinaisons »), each
-- three colours (the suit, its accent, its scarf) that the game applies to Nova's sheet the way
-- tools/assetgen/characters/nova_suits.py makes the catalogue's suits. A suit is given as the
-- reward of a challenge (challenge_reward_rules, reward_kind 'suit', reward_ref
-- 'srvsuit:<creations.id>') and worn only once won. A creation is never deleted: it is
-- archived (no longer offered as a new reward; the players who won it keep it).

-- name: at most 32 characters (checked by the route: letters, digits, punctuation, spaces);
-- name_key: the name folded (no accents, lower case), unique among the creations of a kind
-- that are not archived. Colours: '#rrggbb', lower case.
CREATE TABLE creations (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    kind          TEXT NOT NULL CHECK (kind IN ('suit')),
    name          TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 32),
    name_key      TEXT NOT NULL,
    suit_color    TEXT NOT NULL CHECK (suit_color GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    accent_color  TEXT NOT NULL CHECK (accent_color GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    scarf_color   TEXT NOT NULL CHECK (scarf_color GLOB '#[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    created_by    TEXT REFERENCES players (id) ON DELETE SET NULL,
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL,
    archived      INTEGER NOT NULL DEFAULT 0 CHECK (archived IN (0, 1)),
    archived_at   TEXT
);
CREATE UNIQUE INDEX creations_name ON creations (kind, name_key) WHERE archived = 0;
CREATE INDEX creations_kind ON creations (kind, archived, id);

-- A reward may now be a suit created here: SQLite cannot change a CHECK constraint, so the two
-- tables of 0081 that name the kinds of rewards are rebuilt, their rows copied unchanged.
PRAGMA defer_foreign_keys = true;

CREATE TABLE challenge_reward_rules_new (
    challenge_id    TEXT NOT NULL,
    position        INTEGER NOT NULL CHECK (position BETWEEN 0 AND 15),
    reward_kind     TEXT NOT NULL CHECK (reward_kind IN ('item', 'title', 'suit')),
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
    reward_kind  TEXT NOT NULL CHECK (reward_kind IN ('item', 'title', 'suit')),
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
