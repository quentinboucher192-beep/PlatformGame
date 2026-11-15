-- 2.21: what a player wears online (Collection of the game): the frame and the title follow
-- them on their profile and in the leaderboards, the suit on their profile and their ghosts.
-- Item ids of the server's catalogue (src/cosmeticCatalog.json), checked against the
-- achievements declared in player_achievements: an unknown or locked id is never stored, the
-- category's default takes its place (and is put back when fewer achievements are declared).
CREATE TABLE player_cosmetics (
    player_id TEXT PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,
    suit TEXT NOT NULL,
    trail TEXT NOT NULL,
    frame TEXT NOT NULL,
    title TEXT NOT NULL,
    victory TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
