-- Presence of friends (2.32.3): the game says every minute whether its player is active or
-- idle (no input for 5 minutes); friends see « en ligne », « absent » or « hors ligne ».
-- Nothing else is kept: no history, only the last state and its time. A player can hide it.
ALTER TABLE players ADD COLUMN presence_state TEXT CHECK (presence_state IN ('active', 'idle'));
ALTER TABLE players ADD COLUMN presence_at TEXT;
ALTER TABLE players ADD COLUMN presence_visible INTEGER NOT NULL DEFAULT 1 CHECK (presence_visible IN (0, 1));
