-- 2.41: the badges a player shows on their profile, chosen in the game's Collection: ids of
-- achievements, in the order chosen (JSON array). Checked when sent against the achievements
-- the player declared (player_achievements) and the slots their stars open (counted by the
-- server from its own catalogue, src/achievementCatalog.json); checked again on every
-- reading of the profile, which shows only those still valid.
CREATE TABLE player_badges (
    player_id TEXT PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,
    ids TEXT NOT NULL DEFAULT '[]',
    updated_at TEXT NOT NULL
);
