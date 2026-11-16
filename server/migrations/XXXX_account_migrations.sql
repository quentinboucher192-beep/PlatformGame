-- 2.50: « Migrer vers un nouveau compte » (src/accountMigration.ts, docs/online/DEVICES.md).
-- An account is bound to one computer (2.47): a player who moves to a new computer creates a
-- new account there and brings the old one's server data to it, with a one-time code made on
-- the old computer and the old account's password; or an admin does it (ADMINISTRATION ›
-- Joueurs), after a full backup. The best of both sides is kept; the old account is closed.

-- A closed account (its data migrated to another one). account_status keeps its CHECK
-- ('active', 'suspended'): a closed account is 'suspended' (no login, no session, hidden from
-- the leaderboards and profiles) and closed_at says why and since when. Its username stays
-- reserved 30 days; then the daily maintenance (or a registration that wants it) frees it:
-- username_normalized becomes '~' || id, which no username can be (USERNAME_RE).
-- merged_into: the account that received its data (no foreign key: a trace, never a link).
ALTER TABLE players ADD COLUMN closed_at TEXT;
ALTER TABLE players ADD COLUMN merged_into TEXT;
CREATE INDEX idx_players_closed ON players (closed_at) WHERE closed_at IS NOT NULL;

-- The name of the computer that sent a copy of the progression (shown to the admins).
ALTER TABLE progress_copies ADD COLUMN device_name TEXT;

-- The one-time migration code of an account (made on its computer), one per account: a new
-- one replaces the previous. Never stored in clear: SHA-256 of the account id and the
-- normalised code (9 characters without 0/O/1/I/L). Valid 24 hours, used once.
CREATE TABLE account_migration_codes (
    player_id  TEXT PRIMARY KEY REFERENCES players (id) ON DELETE CASCADE,
    code_hash  TEXT NOT NULL,
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    used_at    TEXT,
    -- The account that used it, and the migration (account_migrations.id): no foreign key.
    used_by    TEXT,
    claim_id   TEXT
);

-- Every migration done. `checked` is computed by the INSERT itself, first statement of the
-- migration's single D1 batch (after the code is claimed): both accounts still active, the code
-- claimed by this migration, an admin left. Anything else fails the CHECK and the whole batch
-- is rolled back: nothing is half migrated.
CREATE TABLE account_migrations (
    id              TEXT PRIMARY KEY,
    source_id       TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    source_username TEXT NOT NULL,
    target_id       TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    target_username TEXT NOT NULL,
    mode            TEXT NOT NULL CHECK (mode IN ('merge', 'replace')),
    items           TEXT NOT NULL,                        -- JSON array: records, achievements, rewards, playtime, levels, progress
    close_source    INTEGER NOT NULL CHECK (close_source IN (0, 1)),
    -- The administrator (NULL: the player, with a code); the backup made just before.
    by_player       TEXT,
    by_username     TEXT,
    backup_id       TEXT,
    created_at      TEXT NOT NULL,
    checked         INTEGER NOT NULL CHECK (checked = 1)
);
CREATE INDEX idx_account_migrations_source ON account_migrations (source_id, created_at);
CREATE INDEX idx_account_migrations_target ON account_migrations (target_id, created_at);

-- A progression to restore on the new computer: the old account's latest copy (2.47), copied
-- for this account under progress/<player>/restore/ (deleted with the account, and once
-- restored). One at a time: a newer migration replaces it.
CREATE TABLE progress_restores (
    player_id       TEXT PRIMARY KEY REFERENCES players (id) ON DELETE CASCADE,
    migration_id    TEXT NOT NULL,
    from_player_id  TEXT,
    from_username   TEXT NOT NULL,
    file_key        TEXT NOT NULL,
    size            INTEGER NOT NULL,
    sha256          TEXT NOT NULL,
    summary         TEXT NOT NULL,
    game_version    TEXT NOT NULL,
    copy_created_at TEXT NOT NULL,
    device_name     TEXT,
    created_at      TEXT NOT NULL,
    postponed_at    TEXT
);
