-- 2.74: clan raids « Asterion », phase 7 (docs/RAIDS.md §15 « Fiabilité, charge et anti-triche »):
-- the order of the zone reports, the frozen official results kept whatever statement is tried, and
-- the indexes the load tests showed missing (server/test/raidLoad.test.ts checks every hot query's
-- plan: no full scan of a raid table).

-- The step of the zone report that placed an instance on the map: a zone report accepted after a
-- newer one (two requests that crossed on the network) never moves the group back.
ALTER TABLE raid_instances ADD COLUMN zone_tick INTEGER;

-- A frozen official result is never deleted (except with its event itself) and never replaced: an
-- INSERT OR REPLACE would delete the row without firing the update trigger of migration 0147.
CREATE TRIGGER raid_event_results_kept BEFORE DELETE ON raid_event_results
WHEN EXISTS (SELECT 1 FROM raid_events WHERE id = OLD.event_id)
BEGIN
    SELECT RAISE(ABORT, 'official results are frozen');
END;

CREATE TRIGGER raid_event_results_once BEFORE INSERT ON raid_event_results
WHEN EXISTS (SELECT 1 FROM raid_event_results WHERE event_id = NEW.event_id)
BEGIN
    SELECT RAISE(IGNORE);
END;

-- The journal of the refused reports by report (the consensus journals a minority once, the
-- aggregation leaves out the second objective of a pair refused to the same group).
CREATE INDEX raid_report_rejections_key ON raid_report_rejections (instance_id, report_key, player_id);

-- The rewards of an event by reward (the cron's end-of-event grants, the results' checks).
CREATE INDEX raid_reward_grants_event ON raid_reward_grants (event_id, reward);

-- The events by their end (the crons of every minute look for those that just ended: before, the
-- end-of-event rewards' query read every gigaboss fight ever fought, each minute).
CREATE INDEX raid_events_end ON raid_events (ends_at);
