-- 2.43: the rewards of one challenge, chosen by an administrator (ADMINISTRATION › Calendrier
-- des défis › Récompenses) while its period has not begun, and granted by the server once the
-- period is over, from the VERIFIED results only (challengeRewards.ts). A reward is an item of
-- the game's catalogue (src/cosmeticCatalog.json) or a title created on the server (no game
-- update needed); a reward won is never taken back.

-- Titles created by an administrator: their text (at most 32 characters, checked by the
-- route) and their colour. `text_key`: the text folded (no accents, lower case) so that the
-- same title is never created twice. Worn as the title « srv:<id> ».
CREATE TABLE server_titles (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    text        TEXT NOT NULL,
    text_key    TEXT NOT NULL UNIQUE,
    color       TEXT NOT NULL CHECK (color IN ('gold', 'silver', 'cyan', 'violet')),
    created_by  TEXT REFERENCES players (id) ON DELETE SET NULL,
    created_at  TEXT NOT NULL
);

-- The rewards of a challenge, in their order. `challenge_id`: '<kind>-<period>' (the period's
-- id: no foreign key, so that « Revenir au tirage » keeps them for the period's new draw).
-- reward_ref: an item id of the catalogue ('item'), or 'srv:<server_titles.id>' ('title').
-- condition / condition_value: 'tier' (1 Bronze, 2 Argent, 3 Or: that tier or better),
-- 'top' (1..100: a place of the final ranking, the challenge's main board), 'finished' (0).
CREATE TABLE challenge_reward_rules (
    challenge_id    TEXT NOT NULL,
    position        INTEGER NOT NULL CHECK (position BETWEEN 0 AND 15),
    reward_kind     TEXT NOT NULL CHECK (reward_kind IN ('item', 'title')),
    reward_ref      TEXT NOT NULL,
    condition       TEXT NOT NULL CHECK (condition IN ('tier', 'top', 'finished')),
    condition_value INTEGER NOT NULL,
    PRIMARY KEY (challenge_id, position)
);

-- A challenge whose rewards were granted (once: its period over, its results settled).
CREATE TABLE challenge_reward_grants (
    challenge_id TEXT PRIMARY KEY,
    granted_at   TEXT NOT NULL
);

-- The rewards each player won (one row per player, challenge and reward: granting twice adds nothing).
CREATE TABLE player_won_rewards (
    player_id    TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    challenge_id TEXT NOT NULL,
    reward_kind  TEXT NOT NULL CHECK (reward_kind IN ('item', 'title')),
    reward_ref   TEXT NOT NULL,
    granted_at   TEXT NOT NULL,
    PRIMARY KEY (player_id, challenge_id, reward_kind, reward_ref)
);
CREATE INDEX player_won_rewards_challenge ON player_won_rewards (challenge_id, granted_at);
