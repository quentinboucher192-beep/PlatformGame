-- Full backups of the online service (ADMINISTRATION › Sauvegardes, docs/online/BACKUP.md):
-- server-side copies (D1 rows in R2 under backups/<id>/, the R2 files listed where they are),
-- encrypted downloads (.cpbk) and their import, which replaces all the data. These tables are
-- the backup machinery itself: they are never part of a backup nor replaced by an import.

-- One server-side copy: `auto` (daily cron), `manual` (an administrator), `pre_import` (the
-- state just before an import replaces everything). The content is in R2 under backups/<id>/.
CREATE TABLE backups (
    id                  TEXT PRIMARY KEY,
    kind                TEXT NOT NULL CHECK (kind IN ('auto', 'manual', 'pre_import')),
    status              TEXT NOT NULL CHECK (status IN ('running', 'ready', 'failed')),
    -- The administrator who asked for it (no foreign key: the account may be gone after a restore).
    created_by          TEXT,
    created_by_username TEXT,
    created_at          TEXT NOT NULL,
    finished_at         TEXT,
    -- Last applied migration (d1_migrations.name) when the copy was made.
    schema_version      TEXT,
    tables_count        INTEGER NOT NULL DEFAULT 0,
    rows_count          INTEGER NOT NULL DEFAULT 0,
    files_count         INTEGER NOT NULL DEFAULT 0,
    -- Total plaintext bytes (rows as JSON lines + file contents).
    bytes               INTEGER NOT NULL DEFAULT 0,
    error               TEXT
);
CREATE INDEX idx_backups_kind ON backups (kind, status, created_at);

-- An uploaded .cpbk file (in R2 under imports/<id>/), checked then applied step by step.
CREATE TABLE backup_imports (
    id                  TEXT PRIMARY KEY,
    created_by          TEXT NOT NULL,
    created_by_username TEXT,
    created_at          TEXT NOT NULL,
    updated_at          TEXT NOT NULL,
    size                INTEGER NOT NULL,
    sha256              TEXT NOT NULL,
    part_size           INTEGER NOT NULL,
    parts               INTEGER NOT NULL,
    status              TEXT NOT NULL CHECK (status IN ('uploading', 'checked', 'applying', 'done', 'failed', 'abandoned', 'expired')),
    -- 1 once a full read of the received parts gave the announced SHA-256.
    sha256_verified     INTEGER NOT NULL DEFAULT 0,
    -- Result of the last successful check (JSON: summary, tables and their columns, problems).
    checked             TEXT,
    compatible          INTEGER NOT NULL DEFAULT 0,
    -- Progress of the apply (JSON: step, position in the file, counters). Never a passphrase.
    state               TEXT,
    pre_import_id       TEXT,
    -- One apply request at a time: a lease taken with a conditional UPDATE.
    lease_until         TEXT,
    finished_at         TEXT,
    error               TEXT
);
CREATE INDEX idx_backup_imports_status ON backup_imports (status, updated_at);

CREATE TABLE backup_import_parts (
    import_id           TEXT NOT NULL REFERENCES backup_imports (id) ON DELETE CASCADE,
    n                   INTEGER NOT NULL,
    size                INTEGER NOT NULL,
    PRIMARY KEY (import_id, n)
);

-- Small key/value state of the server. `maintenance`: while an import replaces the data, every
-- write request that is not an administration one answers 503 MAINTENANCE (value: JSON with
-- the import and an expiry renewed by each apply request, so a forgotten import cannot block
-- the service for ever).
CREATE TABLE server_state (
    key                 TEXT PRIMARY KEY,
    value               TEXT NOT NULL,
    updated_at          TEXT NOT NULL
);
