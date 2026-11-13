-- Social: private conversations between friends, group discussions, blocks and reports.
-- A player's messages are personal data: they are deleted with the account (CASCADE) and
-- included in the GDPR export.

CREATE TABLE conversations (
    id              TEXT PRIMARY KEY,
    kind            TEXT NOT NULL CHECK (kind IN ('direct', 'group')),
    name            TEXT,                                   -- groups only
    direct_key      TEXT UNIQUE,                            -- direct only: "<smaller id>|<larger id>"
    created_by      TEXT REFERENCES players (id) ON DELETE SET NULL,
    created_at      TEXT NOT NULL,
    last_message_at TEXT
);

CREATE TABLE conversation_members (
    conversation_id TEXT NOT NULL REFERENCES conversations (id) ON DELETE CASCADE,
    player_id       TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    role            TEXT NOT NULL DEFAULT 'member' CHECK (role IN ('owner', 'member')),
    joined_at       TEXT NOT NULL,
    last_read_at    TEXT,
    PRIMARY KEY (conversation_id, player_id)
);
CREATE INDEX idx_conversation_members_player ON conversation_members (player_id);

CREATE TABLE messages (
    id              TEXT PRIMARY KEY,
    conversation_id TEXT NOT NULL REFERENCES conversations (id) ON DELETE CASCADE,
    sender_id       TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    body            TEXT NOT NULL,
    created_at      TEXT NOT NULL,
    deleted_at      TEXT
);
CREATE INDEX idx_messages_conversation ON messages (conversation_id, created_at);
CREATE INDEX idx_messages_sender ON messages (sender_id);

CREATE TABLE player_blocks (
    player_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    blocked_id TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    created_at TEXT NOT NULL,
    PRIMARY KEY (player_id, blocked_id)
);
CREATE INDEX idx_player_blocks_blocked ON player_blocks (blocked_id);

CREATE TABLE message_reports (
    id          TEXT PRIMARY KEY,
    message_id  TEXT NOT NULL REFERENCES messages (id) ON DELETE CASCADE,
    reporter_id TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    reason      TEXT,
    created_at  TEXT NOT NULL,
    UNIQUE (message_id, reporter_id)
);
