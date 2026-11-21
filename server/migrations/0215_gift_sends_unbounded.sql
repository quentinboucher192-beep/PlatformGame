-- Cosmic Online — « Cadeaux » without limits (src/gifts.ts, docs/online/API.md §8.10 sexies): a send may give any
-- number of rewards of the catalogue to any number of players, or to every player. It is written in steps (one D1
-- batch per group of players, each with the send's progress), invisible to its recipients until its last batch
-- makes it « sent » together with their notifications: a send is never seen half-written. Sent again with the same
-- request key, it goes on where it stopped (or answers what was sent), never twice.

-- `state`: 'writing' while its steps are written (its gifts and parcels shown to nobody), then 'sent'. The sends
-- written before stay 'sent'. `request_key`: the key of the request that made it (one send per key and sender);
-- `request_hash`: what that request asked (the same key with another request is refused). `progress`: where the
-- next step starts (the index in the players given, or the last player id done with « all »). `skipped`: the pairs
-- skipped so far (already owned or waiting). `touched_at`: its last step (a send left 'writing' for an hour is
-- removed, with what it had written).
ALTER TABLE gift_sends ADD COLUMN state TEXT NOT NULL DEFAULT 'sent';
ALTER TABLE gift_sends ADD COLUMN request_key TEXT;
ALTER TABLE gift_sends ADD COLUMN request_hash TEXT;
ALTER TABLE gift_sends ADD COLUMN progress TEXT;
ALTER TABLE gift_sends ADD COLUMN skipped INTEGER NOT NULL DEFAULT 0;
ALTER TABLE gift_sends ADD COLUMN touched_at TEXT;
CREATE UNIQUE INDEX gift_sends_request ON gift_sends (sender_id, request_key) WHERE request_key IS NOT NULL;
CREATE INDEX gift_sends_writing ON gift_sends (state, touched_at) WHERE state = 'writing';

-- A step written twice (two identical requests at once) writes nothing more: one gift per reward and recipient of a
-- send (the gifts a parcel writes once opened are apart), one parcel per recipient of a send.
CREATE UNIQUE INDEX gifts_send_unit ON gifts (send_id, recipient_id, reward_kind, reward_id) WHERE parcel_id IS NULL;
CREATE UNIQUE INDEX gift_parcels_send_unit ON gift_parcels (send_id, recipient_id);
