-- Cosmic Online 2.9 — Workshop: favorites, comments and reactions on community levels,
-- creators and followers, notification centre. Append-only (see 0001).

-- Counters kept on the level card (updated in the same batch as their source rows).
ALTER TABLE community_levels ADD COLUMN favorite_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE community_levels ADD COLUMN comment_count INTEGER NOT NULL DEFAULT 0;

-- Favorites: one per player and level. No foreign key on the level: a favorite survives a
-- level that is hidden or removed (the list shows it as unavailable, with the title and
-- author kept at the time it was added).
CREATE TABLE community_favorites (
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    level_id TEXT NOT NULL,
    level_title TEXT NOT NULL,
    author_name TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY (player_id, level_id)
);
CREATE INDEX community_favorites_level ON community_favorites(level_id);

-- Comments: one level of replies (parent_id). Deleting a comment that has replies keeps it
-- as "deleted" (deleted_at) so the thread stays readable; reports hide it until reviewed.
CREATE TABLE community_comments (
    id TEXT PRIMARY KEY,
    level_id TEXT NOT NULL REFERENCES community_levels(id) ON DELETE CASCADE,
    author_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    parent_id TEXT REFERENCES community_comments(id) ON DELETE CASCADE,
    body TEXT NOT NULL,
    body_hash TEXT NOT NULL,
    created_at TEXT NOT NULL,
    edited_at TEXT,
    deleted_at TEXT,
    pinned_at TEXT,
    hidden INTEGER NOT NULL DEFAULT 0,          -- 1: hidden after reports, pending moderation
    report_count INTEGER NOT NULL DEFAULT 0,
    reply_count INTEGER NOT NULL DEFAULT 0,
    r_like INTEGER NOT NULL DEFAULT 0,
    r_love INTEGER NOT NULL DEFAULT 0,
    r_funny INTEGER NOT NULL DEFAULT 0,
    r_impressive INTEGER NOT NULL DEFAULT 0,
    r_not_useful INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX community_comments_level ON community_comments(level_id, parent_id, created_at);
CREATE INDEX community_comments_author ON community_comments(author_id, created_at);

-- One active reaction per player and comment (it can be changed).
CREATE TABLE community_comment_reactions (
    comment_id TEXT NOT NULL REFERENCES community_comments(id) ON DELETE CASCADE,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    kind TEXT NOT NULL CHECK (kind IN ('like', 'love', 'funny', 'impressive', 'not_useful')),
    created_at TEXT NOT NULL,
    PRIMARY KEY (comment_id, player_id)
);

CREATE TABLE community_comment_reports (
    comment_id TEXT NOT NULL REFERENCES community_comments(id) ON DELETE CASCADE,
    reporter_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    reason TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY (comment_id, reporter_id)
);

-- Players who downloaded or finished a level (unique players of a creator).
CREATE TABLE community_level_players (
    level_id TEXT NOT NULL REFERENCES community_levels(id) ON DELETE CASCADE,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    first_at TEXT NOT NULL,
    PRIMARY KEY (level_id, player_id)
);

-- Followers: following a creator is not a friendship.
CREATE TABLE player_follows (
    follower_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    creator_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    notify INTEGER NOT NULL DEFAULT 1,   -- notifications of the creator's new levels
    created_at TEXT NOT NULL,
    PRIMARY KEY (follower_id, creator_id),
    CHECK (follower_id <> creator_id)
);
CREATE INDEX player_follows_creator ON player_follows(creator_id, created_at);

-- Who can see the lists of a player's followers and followed creators.
ALTER TABLE players ADD COLUMN follows_visibility TEXT NOT NULL DEFAULT 'public'
    CHECK (follows_visibility IN ('public', 'private'));
-- Notification categories the player turned off (comma-separated kinds).
ALTER TABLE players ADD COLUMN notifications_off TEXT NOT NULL DEFAULT '';

-- Notification centre. group_key merges repeated events while unread (reactions to one
-- comment...) instead of piling them up.
CREATE TABLE notifications (
    id TEXT PRIMARY KEY,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    kind TEXT NOT NULL,
    actor_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    level_id TEXT,
    comment_id TEXT,
    data TEXT NOT NULL DEFAULT '{}',
    group_key TEXT,
    count INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL,
    read_at TEXT
);
CREATE INDEX notifications_player ON notifications(player_id, created_at);
CREATE INDEX notifications_unread ON notifications(player_id, read_at);
CREATE INDEX notifications_group ON notifications(player_id, group_key, read_at);

-- Time played (profiles): per player and level, reported by the game at the end of each
-- session and capped by the server (never more than the time elapsed since the last report).
CREATE TABLE player_playtime (
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    level_key TEXT NOT NULL,             -- official level id, community level id, or 'other'
    seconds INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL,
    PRIMARY KEY (player_id, level_key)
);
ALTER TABLE players ADD COLUMN playtime_reported_at TEXT;
