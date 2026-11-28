-- The clouds of the training curve (2.98, ADMINISTRATION › TAS, docs/TAS.md « Courbe
-- d'entraînement », server/src/tas.ts): each window of attempts sent with a heartbeat or a release
-- carries its finished attempts as points and, per checkpoint of the level (CP1 first), how many
-- attempts reached it, their best / median / worst time and a sample of them. Stored as JSON on the
-- window's row, each point [training ms on the server's measure, time ms]:
--   finishes: [[training_ms, time_ms], ...]                       (96 a window at most)
--   checkpoints: [{reached, best_ms, median_ms, worst_ms, sample: [[training_ms, time_ms], ...]}]
--                                                                  (8 a checkpoint and a window at most)
--   points: how many points the row holds (the version's clouds are bounded: beyond 60 000, a new
--           window keeps only its best points).
-- The windows stored before have none ('[]', 0).
ALTER TABLE tas_curve_windows ADD COLUMN finishes TEXT NOT NULL DEFAULT '[]';
ALTER TABLE tas_curve_windows ADD COLUMN checkpoints TEXT NOT NULL DEFAULT '[]';
ALTER TABLE tas_curve_windows ADD COLUMN points INTEGER NOT NULL DEFAULT 0;
