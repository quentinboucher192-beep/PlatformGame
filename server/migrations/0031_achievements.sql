-- 2.18.0: the achievements a player's game declares (stars shown on the profile).
-- Declared by the game, never verified by the server: the profile says so. The stars are
-- recomputed here from the server's own catalogue (src/achievementCatalog.json); unknown ids
-- are ignored.
CREATE TABLE player_achievements (
    player_id TEXT PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,
    ids TEXT NOT NULL DEFAULT '[]',      -- JSON array of known achievement ids
    stars INTEGER NOT NULL DEFAULT 0,
    count INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL
);
