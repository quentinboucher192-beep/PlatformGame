-- 2.40.1: the SPEEDRUN category is removed: the same runs as ANY%, and twice the notifications.
-- The API refuses it from now on; its records go, with their bests and their run comments (by
-- cascade). A replay belongs to one record of its attempt (replay_metadata.record_id) and is
-- shared by the others (records.replay_id): when that record is a SPEEDRUN one, the replay is
-- first handed to another record of the same attempt (ANY% first), so that no other category
-- loses it. The CHECK of records.category still names SPEEDRUN (SQLite cannot change it in
-- place); nothing writes it any more.
UPDATE replay_metadata
SET record_id = (
    SELECT r2.id FROM records r2
    WHERE r2.player_id = replay_metadata.player_id AND r2.attempt_id = replay_metadata.attempt_id AND r2.category <> 'SPEEDRUN'
    ORDER BY CASE r2.category WHEN 'ANY_PERCENT' THEN 0 WHEN 'HUNDRED_PERCENT' THEN 1 WHEN 'NO_DEATH' THEN 2 ELSE 3 END
    LIMIT 1)
WHERE record_id IN (SELECT id FROM records WHERE category = 'SPEEDRUN')
  AND EXISTS (SELECT 1 FROM records r2
              WHERE r2.player_id = replay_metadata.player_id AND r2.attempt_id = replay_metadata.attempt_id AND r2.category <> 'SPEEDRUN');

DELETE FROM notifications WHERE kind = 'record_beaten' AND json_extract(data, '$.category') = 'SPEEDRUN';
DELETE FROM player_bests WHERE category = 'SPEEDRUN';
DELETE FROM records WHERE category = 'SPEEDRUN';
