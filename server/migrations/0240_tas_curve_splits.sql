-- Where the gain is (2.99, ADMINISTRATION › TAS, docs/TAS.md « Où est le gain », server/src/tas.ts):
-- each best point of the training curve sent with a heartbeat or a release may carry the splits of
-- its run at the level's checkpoints (CP1 first, the level's clock in ms, 0 where not reached; 32
-- at most, never beyond the run's time, never going back) and where the gain came from (beam,
-- guide, splice, polish...). Stored as JSON on the point's row; the game compares a point's
-- splits with the best point before it to say in which segment the gain is.
-- The points stored before have none ('[]', '').
ALTER TABLE tas_curve_points ADD COLUMN splits TEXT NOT NULL DEFAULT '[]';
ALTER TABLE tas_curve_points ADD COLUMN source TEXT NOT NULL DEFAULT '';
