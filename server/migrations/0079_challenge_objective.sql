-- 2.41: ADMINISTRATION › Calendrier des défis, and the objective of a challenge.
--
-- `objective`: what ranks the players of a challenge.
--   'speed'  (« Vitesse », every challenge before 2.41): the best times; the tiers are times
--            (Bronze: finished, Argent: silver_ms, Or: gold_ms, without damage if gold_no_damage).
--   'finish' (« Finir le niveau »): the progress, like the board « Plus loin » (furthest first,
--            then the first to finish); only on a level version with a race layout. The tiers
--            are shares of the route reached, from the server's score of each attempt's replay
--            (Bronze: bronze_progress %, Argent: silver_progress %, Or: finished, without damage
--            if gold_no_damage); silver_ms and gold_ms only keep the older games consistent.
-- The objective changes the ranking, never the simulation: it is not part of the canonical
-- rules (rules_hash), and the replays stay those of 2.40.
ALTER TABLE challenges ADD COLUMN objective TEXT NOT NULL DEFAULT 'speed' CHECK (objective IN ('speed', 'finish'));
ALTER TABLE challenges ADD COLUMN bronze_progress INTEGER NOT NULL DEFAULT 50 CHECK (bronze_progress BETWEEN 10 AND 95);
ALTER TABLE challenges ADD COLUMN silver_progress INTEGER NOT NULL DEFAULT 80 CHECK (silver_progress BETWEEN 10 AND 95);
-- The server's draw an administrator's choice replaced (its level's title), shown in the
-- calendar's editor; NULL for a draw and for a choice made over an empty period.
ALTER TABLE challenges ADD COLUMN replaced_title TEXT;

-- The tier an attempt of a 'finish' challenge reached (computed by the server from its
-- verified progress; NULL: none, and always NULL on a 'speed' challenge).
ALTER TABLE challenge_attempts ADD COLUMN tier TEXT CHECK (tier IS NULL OR tier IN ('bronze', 'silver'));

-- A challenge whose first day has begun is frozen: the objective and its tiers too.
DROP TRIGGER IF EXISTS challenges_frozen;
CREATE TRIGGER challenges_frozen
BEFORE UPDATE OF id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
    slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, bronze_progress, silver_progress,
    replaced_title ON challenges
WHEN OLD.date <= date('now')
BEGIN
    SELECT RAISE(ABORT, 'challenge already started: it cannot change');
END;
