-- Cosmic Online 2.81 — « Après Asterion » (src/raidHeart.ts, docs/RAIDS.md §25): the film of what the fall changes in
-- the world, watched together. Stamped once, when the Heart's fall is validated (never from a game's claim): its start
-- (`film_at`, the fall plus the victory film and a margin), the clans that had a player in a Heart instance at that
-- moment (`film_clans`, a JSON list of clan ids, in their order), and when their online members were told
-- (`notified_film`, once).
ALTER TABLE raid_heart_events ADD COLUMN film_at TEXT;
ALTER TABLE raid_heart_events ADD COLUMN film_clans TEXT;
ALTER TABLE raid_heart_events ADD COLUMN notified_film TEXT;
