-- 2.60: ADMINISTRATION › Calendrier des défis: a challenge that has begun (a day, a week or a
-- month that runs) may still change — an administrator's choice, or back to the server's draw —
-- as long as nobody has a result on it: anything the server received for it, whatever its
-- status (a run, an attempt of « Plus loin », a reward won, its rewards granted). A challenge
-- whose period is over never changes. The routes (src/adminChallenges.ts) check it and each of
-- their writes carries the condition; these triggers back both (they replace the trigger
-- challenges_frozen of 0058 and 0079, which froze a challenge from its first day).
--
-- A run or an attempt is only inserted on the version of the challenge it was checked against
-- (its level_hash, rules_hash and updated_at, in the same statement): the results of two
-- versions of one challenge are never mixed.
--
-- Only the creator notification bookkeeping (featured_notified_at), the author link (set to
-- NULL when that account is deleted) and created_at / updated_at stay outside, as before.
-- Deleting a challenge is not a trigger's: the foreign keys of challenge_runs and
-- challenge_attempts refuse it once a run or an attempt exists, and the backups' import empties
-- the table before restoring it.

DROP TRIGGER IF EXISTS challenges_frozen;

CREATE TRIGGER challenges_frozen
BEFORE UPDATE OF id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
    slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, bronze_progress, silver_progress,
    replaced_title ON challenges
WHEN OLD.end_date <= date('now')
BEGIN
    SELECT RAISE(ABORT, 'challenge already over: it cannot change');
END;

CREATE TRIGGER challenges_have_results
BEFORE UPDATE OF id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
    slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, bronze_progress, silver_progress,
    replaced_title ON challenges
WHEN OLD.date <= date('now')
    AND (EXISTS (SELECT 1 FROM challenge_runs WHERE challenge_id = OLD.id)
      OR EXISTS (SELECT 1 FROM challenge_attempts WHERE challenge_id = OLD.id)
      OR EXISTS (SELECT 1 FROM player_won_rewards WHERE challenge_id = OLD.id)
      OR EXISTS (SELECT 1 FROM challenge_reward_grants WHERE challenge_id = OLD.id))
BEGIN
    SELECT RAISE(ABORT, 'challenge has results: it cannot change');
END;
