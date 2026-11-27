-- The machine's runs (TAS) improved by the administrators on their own computers
-- (ADMINISTRATION › TAS, docs/TAS.md, server/src/tas.ts). Nothing is computed here: the
-- server keeps a LEASE per level, category and VERSION of the level (one administrator trains
-- a pair at a time), the training time and attempts counted from the heartbeats, and every
-- run published. A version of a level is its ranked fingerprint (level_hash) and the version of
-- the simulation (sim_version): a level edited, regenerated, an enemy revision or new rules make
-- a new version, whose runs and training start from nothing; the old ones stay (never deleted).

-- One row per level, category and version ever trained.
--   total_training_ms: the time of the leases, measured by the server between two heartbeats
--                      (at most the lease's life each time), summed over every session;
--   total_explored:    the attempts reported by the heartbeats, bounded by that time;
--   lease_*:           the lease in force (lease_id NULL: free; lease_expires_ms in ms since
--                      the epoch: past it, the pair is free again, whatever the row says).
CREATE TABLE IF NOT EXISTS tas_training (
    level_id TEXT NOT NULL,
    category TEXT NOT NULL,
    level_hash TEXT NOT NULL,
    sim_version INTEGER NOT NULL,
    total_training_ms INTEGER NOT NULL DEFAULT 0,
    total_explored INTEGER NOT NULL DEFAULT 0,
    sessions INTEGER NOT NULL DEFAULT 0,
    lease_id TEXT,
    lease_holder TEXT,
    lease_holder_username TEXT,
    lease_started_at TEXT,
    lease_beat_ms INTEGER,
    lease_expires_ms INTEGER,
    lease_explored INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (level_id, category, level_hash, sim_version)
);
CREATE UNIQUE INDEX IF NOT EXISTS tas_training_lease ON tas_training (lease_id);

-- Every run published (never deleted; its files in R2 under
-- tas/<level>/<category>/<revision>-<sha256>.json|.cprz). `revision` counts the publications
-- of the pair; the run of a version is its latest revision (each one faster than the last).
CREATE TABLE IF NOT EXISTS tas_runs (
    level_id TEXT NOT NULL,
    category TEXT NOT NULL,
    revision INTEGER NOT NULL,
    level_hash TEXT NOT NULL,
    level_version INTEGER NOT NULL,
    sim_version INTEGER NOT NULL,
    time_ms INTEGER NOT NULL,
    explored INTEGER NOT NULL,
    record_key TEXT NOT NULL,
    record_sha256 TEXT NOT NULL,
    record_size INTEGER NOT NULL,
    replay_key TEXT NOT NULL,
    replay_sha256 TEXT NOT NULL,
    replay_size INTEGER NOT NULL,
    training_ms_at_publish INTEGER NOT NULL DEFAULT 0,
    explored_at_publish INTEGER NOT NULL DEFAULT 0,
    published_by TEXT,
    published_by_username TEXT,
    published_at TEXT NOT NULL,
    PRIMARY KEY (level_id, category, revision)
);
CREATE INDEX IF NOT EXISTS tas_runs_version ON tas_runs (level_id, category, level_hash, sim_version, revision);
