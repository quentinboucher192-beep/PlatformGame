-- 2.47: an account is private to ONE computer (devices.ts, docs/online/DEVICES.md). The game
-- draws a random id for the computer (no hardware fingerprint) and sends it at registration
-- and login; the account is bound to it, and a login from another computer is refused (the
-- owner gets a « security » notification). One account per computer. An admin may unbind an
-- account (its next login, from any computer, binds it again) or bind it to the computer of a
-- refused attempt.
ALTER TABLE players ADD COLUMN device_id TEXT;
ALTER TABLE players ADD COLUMN device_name TEXT;
ALTER TABLE players ADD COLUMN device_bound_at TEXT;
ALTER TABLE players ADD COLUMN device_rebind INTEGER NOT NULL DEFAULT 0;
CREATE UNIQUE INDEX idx_players_device ON players (device_id) WHERE device_id IS NOT NULL;

ALTER TABLE player_sessions ADD COLUMN device_id TEXT;

-- The refused logins of an account (the right password from another computer): the owner's
-- alerts, and what an admin may bind the account to. Country: approximate only.
CREATE TABLE login_refusals (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    player_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    device_id  TEXT NOT NULL,
    country    TEXT,
    created_at TEXT NOT NULL
);
CREATE INDEX idx_login_refusals_player ON login_refusals (player_id, id);

-- The copies of the progression the game sends (kept on the computer only otherwise): the
-- private R2 bucket under progress/<player>/, the 5 most recent per account. Used to migrate
-- an account to a new one (2.48) and by the admins.
CREATE TABLE progress_copies (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    player_id    TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    file_key     TEXT NOT NULL,
    size         INTEGER NOT NULL,
    sha256       TEXT NOT NULL,
    summary      TEXT NOT NULL,
    game_version TEXT NOT NULL,
    created_at   TEXT NOT NULL
);
CREATE INDEX idx_progress_copies_player ON progress_copies (player_id, id);

-- Every account is bound at its next login with a 2.47 game: the open sessions end now.
UPDATE player_sessions SET revoked_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE revoked_at IS NULL;
