-- Live spectating between friends (2.35): « Mes amis peuvent me regarder jouer », on by
-- default. Only this setting is stored: the run a player streams stays in the memory of
-- their room (Durable Object SpectateRoom) and is never written to D1 or R2. Friends can
-- watch only with this setting on and the presence shown (presence_visible); a blocked
-- player never can, and the player watched is never told who watches.
ALTER TABLE players ADD COLUMN spectate_allowed INTEGER NOT NULL DEFAULT 1 CHECK (spectate_allowed IN (0, 1));
