-- 2.61 Studio de Créations (ADMINISTRATION › Créations › Studio): a creation may have an image
-- besides its parameters (creationAssets.ts):
--   - a suit: Nova's whole sheet, cut as hers (320 × 864, frames of 32 × 32);
--   - a trail or a victory effect: an animated particle (its frames side by side, from 4 to 64
--     pixels, 1 to 16 of them) and its emitter (`emitter`, JSON: rate, life_ms, size, speed,
--     spread, gravity, spin, fade, sparkles; NULL for the other kinds);
--   - an avatar frame: a ring (square frames of 32 to 128 pixels, 1 to 16 of them, looped).
-- The PNG itself is kept in R2 under `r2_key` (creation-images/<id>/<sha256>.png), never read
-- from outside the Worker; `hash` is its SHA-256 (the address it is read at, cached by it).
-- One image per creation (the last one sent). A creation is never deleted, only archived: its
-- image stays with it. Display only: no run, replay or record changes.

CREATE TABLE creation_images (
    creation_id  INTEGER PRIMARY KEY,
    kind         TEXT NOT NULL CHECK (kind IN ('suit', 'trail', 'frame', 'victory')),
    hash         TEXT NOT NULL CHECK (length(hash) = 64 AND hash NOT GLOB '*[^0-9a-f]*'),
    r2_key       TEXT NOT NULL,
    bytes        INTEGER NOT NULL CHECK (bytes BETWEEN 1 AND 524288),
    width        INTEGER NOT NULL CHECK (width BETWEEN 1 AND 1024),
    height       INTEGER NOT NULL CHECK (height BETWEEN 1 AND 1024),
    frame_w      INTEGER NOT NULL CHECK (frame_w BETWEEN 4 AND 128),
    frame_h      INTEGER NOT NULL CHECK (frame_h BETWEEN 4 AND 128),
    frames       INTEGER NOT NULL CHECK (frames BETWEEN 1 AND 16),
    fps          INTEGER NOT NULL CHECK (fps BETWEEN 1 AND 30),
    emitter      TEXT CHECK (emitter IS NULL OR json_valid(emitter)),
    uploaded_by  TEXT REFERENCES players (id) ON DELETE SET NULL,
    uploaded_at  TEXT NOT NULL,
    CHECK (kind <> 'suit' OR (width = 320 AND height = 864 AND frame_w = 32 AND frame_h = 32)),
    CHECK ((kind IN ('trail', 'victory')) = (emitter IS NOT NULL))
);
