-- Cosmic Online — « Colis » (src/gifts.ts, docs/COLLECTION.md « Cadeaux », docs/online/API.md §8.10 sexies): an
-- administrator who gives several rewards may send them as ONE parcel per player instead of separate gifts. A parcel
-- is a single wrapped entry for its recipient: neither its rewards nor how many it holds are known before it is
-- opened. Opening it writes one opened gift per reward (gifts.parcel_id, in the parcel's order) and grants each
-- through the existing ownership (player_won_rewards 'gift:<id>', a claimed raid_reward_grants row), once.

-- `bundle`: 1 when the send made parcels (one per recipient), 0 for separate gifts.
ALTER TABLE gift_sends ADD COLUMN bundle INTEGER NOT NULL DEFAULT 0;

-- One parcel: the rewards of one send for one recipient (a JSON list of {kind, id}, in their order: only those
-- the recipient did not own nor have waiting when it was sent). `cancelled_at`: withdrawn before it was opened.
CREATE TABLE gift_parcels (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    send_id      INTEGER NOT NULL REFERENCES gift_sends (id) ON DELETE CASCADE,
    recipient_id TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    sender_id    TEXT REFERENCES players (id) ON DELETE SET NULL,
    wrap         TEXT NOT NULL,
    message      TEXT NOT NULL DEFAULT '' CHECK (length(message) <= 140),
    rewards      TEXT NOT NULL,
    created_at   TEXT NOT NULL,
    opened_at    TEXT,
    cancelled_at TEXT,
    CHECK (opened_at IS NULL OR cancelled_at IS NULL)
);
CREATE INDEX gift_parcels_recipient ON gift_parcels (recipient_id, created_at);
CREATE INDEX gift_parcels_send ON gift_parcels (send_id);

-- The gifts a parcel held, written when it is opened (never before: no id is spent on a wrapped parcel).
ALTER TABLE gifts ADD COLUMN parcel_id INTEGER REFERENCES gift_parcels (id) ON DELETE CASCADE;
CREATE INDEX gifts_parcel ON gifts (parcel_id);
