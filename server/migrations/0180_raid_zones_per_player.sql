-- 2.78.0: clan raids « Asterion » (docs/RAIDS.md §23).
--
-- Each player of an instance changes zone alone (the group may be spread over several zones of its
-- branch): the games report where each seat is (report « zones », the same consensus as every report)
-- and the server keeps it on the instance (`seat_zones`: « z0,z1,…,z7 », 0 for a free seat or an armory),
-- for its maps (who is in which zone), its plausibility checks and the retakes. A group wiped across
-- several zones (every player of the instance down, whatever their zone) leaves each of them to retake
-- (`wiped_zones`: « 3,6 »; `wiped_zone` stays the zone of the most players, as before).
ALTER TABLE raid_instances ADD COLUMN seat_zones TEXT;
ALTER TABLE raid_instances ADD COLUMN seat_zones_tick INTEGER;
ALTER TABLE raid_instances ADD COLUMN wiped_zones TEXT;

-- The zones' « Tableaux des contributions » count, per player: the damage to the zone's mini-bosses (in
-- tenths, part of boss_damage), the deaths (times down), the revivals done for the others and those
-- received. NULL on the lines accepted before 2.78 (the games of before did not count them): shown « — ».
ALTER TABLE raid_zone_contributions ADD COLUMN mini_damage INTEGER;
ALTER TABLE raid_zone_contributions ADD COLUMN deaths INTEGER;
ALTER TABLE raid_zone_contributions ADD COLUMN revives INTEGER;
ALTER TABLE raid_zone_contributions ADD COLUMN revived INTEGER;
