-- 2.40: the administrator role of an account, kept by the server and checked on every admin
-- action from the authenticated session (no administration key anywhere in the game). The
-- first administrator is the owner's account, NovaRider; the others are named from the game
-- by an administrator (ADMINISTRATION › Joueurs), each change written to audit_log.
ALTER TABLE players ADD COLUMN role TEXT NOT NULL DEFAULT 'player' CHECK (role IN ('player', 'admin'));
CREATE INDEX idx_players_role ON players (role);
CREATE INDEX idx_audit_action ON audit_log (action, created_at);

UPDATE players SET role = 'admin' WHERE username_normalized = 'novarider';
INSERT INTO audit_log (player_id, action, ip_hash, details, created_at)
SELECT NULL, 'admin_role_granted', NULL,
       '{"by":"migration","target":"' || id || '","target_username":"' || username || '","role":"admin"}',
       strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
FROM players WHERE username_normalized = 'novarider';
