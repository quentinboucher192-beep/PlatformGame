-- Cosmic Online — « Cadeaux » (src/gifts.ts, docs/COLLECTION.md « Cadeaux », docs/online/API.md §8.10 sexies): an administrator gives rewards of the real
-- catalogue (an item of src/cosmeticCatalog.json, or a profile badge the server grants: raidRewards.ts
-- RAID_ACHIEVEMENTS) to one or more players. A gift grants nothing until its recipient opens it: opening is the
-- server's own write into the existing ownership (player_won_rewards 'gift:<id>' for an item, a claimed
-- raid_reward_grants row, event 'gift:<id>', for a badge), once (opened_at, written by a conditional UPDATE).

-- One send of the ADMINISTRATION › Cadeaux tab: its sender, wrap, message and rewards (a JSON list of
-- {kind, id}, in their order), written to the Journal too. `sender_name`: the sender's username at the time
-- (kept when the account is gone).
CREATE TABLE gift_sends (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    sender_id   TEXT REFERENCES players (id) ON DELETE SET NULL,
    sender_name TEXT NOT NULL,
    wrap        TEXT NOT NULL CHECK (wrap IN ('or', 'amethyste', 'azur', 'rubis', 'emeraude', 'nebuleuse', 'givre', 'braise', 'cosmos', 'rouage', 'asterion')),
    message     TEXT NOT NULL DEFAULT '' CHECK (length(message) <= 140),
    rewards     TEXT NOT NULL,
    created_at  TEXT NOT NULL
);
CREATE INDEX gift_sends_created ON gift_sends (created_at, id);

-- One gift: one reward for one recipient. `reward_kind`: 'suit', 'trail', 'frame', 'title', 'victory' (an item
-- of the catalogue, `reward_id` its id) or 'badge' (`reward_id` the achievement id). Its content stays hidden
-- from the recipient until `opened_at`; `cancelled_at`: withdrawn by an administrator before it was opened.
CREATE TABLE gifts (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    send_id      INTEGER NOT NULL REFERENCES gift_sends (id) ON DELETE CASCADE,
    recipient_id TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    sender_id    TEXT REFERENCES players (id) ON DELETE SET NULL,
    reward_kind  TEXT NOT NULL CHECK (reward_kind IN ('suit', 'trail', 'frame', 'title', 'victory', 'badge')),
    reward_id    TEXT NOT NULL,
    wrap         TEXT NOT NULL,
    message      TEXT NOT NULL DEFAULT '' CHECK (length(message) <= 140),
    created_at   TEXT NOT NULL,
    opened_at    TEXT,
    cancelled_at TEXT,
    CHECK (opened_at IS NULL OR cancelled_at IS NULL)
);
CREATE INDEX gifts_recipient ON gifts (recipient_id, created_at);
CREATE INDEX gifts_send ON gifts (send_id);
CREATE INDEX gifts_reward ON gifts (reward_kind, reward_id, recipient_id);
