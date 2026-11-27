-- The training curve of the machine's runs (ADMINISTRATION › TAS, docs/TAS.md « Courbe
-- d'entraînement », server/src/tas.ts). Per level, category and VERSION of the level (as
-- tas_training): a new version starts a new curve, the old ones stay.
--
-- tas_curve_points: one row each time the best time of the training improved, sent by the
-- administrator's game with the heartbeat or the release of its lease, placed by the server on
-- its own measure of the training time (training_ms: within the time of that heartbeat) and of
-- the attempts (explored); time_ms strictly below the previous point's; at most 1 000 rows per
-- version (beyond, the last one gives way: the curve keeps its start and its best). The runs
-- published are the curve's other marks: tas_runs (training_ms_at_publish).
CREATE TABLE IF NOT EXISTS tas_curve_points (
    level_id TEXT NOT NULL,
    category TEXT NOT NULL,
    level_hash TEXT NOT NULL,
    sim_version INTEGER NOT NULL,
    seq INTEGER NOT NULL,
    kind TEXT NOT NULL,             -- 'start' (the run the training started from) | 'improvement'
    time_ms INTEGER NOT NULL,
    training_ms INTEGER NOT NULL,
    explored INTEGER NOT NULL,
    session INTEGER NOT NULL,       -- tas_training.sessions of the lease that found it
    found_by TEXT,
    found_by_username TEXT,
    found_at TEXT NOT NULL,
    PRIMARY KEY (level_id, category, level_hash, sim_version, seq)
);

-- tas_curve_windows: the attempts of the search, counted by the game a few seconds at a time and
-- sent as a summary (at most 4 windows a heartbeat): how many, how many finished and their best /
-- median / worst clock, where the others stopped (stopped, broken: JSON arrays, one count per
-- section). start_ms / end_ms: the training time it covers, on the server's measure. At most
-- 1 500 rows per version (beyond, a window is added to the last one of its session).
CREATE TABLE IF NOT EXISTS tas_curve_windows (
    level_id TEXT NOT NULL,
    category TEXT NOT NULL,
    level_hash TEXT NOT NULL,
    sim_version INTEGER NOT NULL,
    seq INTEGER NOT NULL,
    session INTEGER NOT NULL,
    round INTEGER NOT NULL,
    section INTEGER NOT NULL,
    sections INTEGER NOT NULL,
    start_ms INTEGER NOT NULL,
    end_ms INTEGER NOT NULL,
    attempts INTEGER NOT NULL,
    finished INTEGER NOT NULL,
    best_ms INTEGER NOT NULL,
    median_ms INTEGER NOT NULL,
    worst_ms INTEGER NOT NULL,
    route_median INTEGER NOT NULL,
    route_max INTEGER NOT NULL,
    stopped TEXT NOT NULL,
    broken TEXT NOT NULL,
    found_by TEXT,
    found_by_username TEXT,
    at TEXT NOT NULL,
    PRIMARY KEY (level_id, category, level_hash, sim_version, seq)
);
