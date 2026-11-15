-- Cosmic Online 2.19 — the social side of the campaign: players' ratings of the official
-- levels, comments on the official levels (shown in ONLINE only) and comments on the runs of
-- the leaderboards. The comment threads follow the rules and use the routes of the community
-- levels' comments (src/workshop.ts). Append-only (see 0001).

-- One rating (1 to 5 stars) per player and official level, given only after a run of that
-- level verified by the server (src/campaign.ts). Never sent by the game as an average.
CREATE TABLE level_ratings (
    level_id TEXT NOT NULL REFERENCES levels(id),
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    stars INTEGER NOT NULL CHECK (stars BETWEEN 1 AND 5),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    PRIMARY KEY (level_id, player_id)
);
CREATE INDEX level_ratings_player ON level_ratings(player_id);

-- Average of each level, recomputed from level_ratings in the same batch as every change
-- (rating, removal, account deletion): the game reads it for every level at once.
CREATE TABLE level_rating_stats (
    level_id TEXT PRIMARY KEY REFERENCES levels(id),
    rating_sum INTEGER NOT NULL DEFAULT 0,
    rating_count INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL
);

-- Comments of the campaign: same columns and rules as community_comments (one level of
-- replies, reactions, reports, moderation). `thread` is 'level:<official level id>' or
-- 'run:<record id>'. A run's thread goes with its record (account deletion of its author).
CREATE TABLE campaign_comments (
    id TEXT PRIMARY KEY,
    thread TEXT NOT NULL,
    level_id TEXT NOT NULL REFERENCES levels(id),
    record_id TEXT REFERENCES records(id) ON DELETE CASCADE,
    author_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    parent_id TEXT REFERENCES campaign_comments(id) ON DELETE CASCADE,
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
CREATE INDEX campaign_comments_thread ON campaign_comments(thread, parent_id, created_at);
CREATE INDEX campaign_comments_author ON campaign_comments(author_id, created_at);
CREATE INDEX campaign_comments_record ON campaign_comments(record_id);
CREATE INDEX campaign_comments_parent ON campaign_comments(parent_id);

CREATE TABLE campaign_comment_reactions (
    comment_id TEXT NOT NULL REFERENCES campaign_comments(id) ON DELETE CASCADE,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    kind TEXT NOT NULL CHECK (kind IN ('like', 'love', 'funny', 'impressive', 'not_useful')),
    created_at TEXT NOT NULL,
    PRIMARY KEY (comment_id, player_id)
);
CREATE INDEX campaign_comment_reactions_player ON campaign_comment_reactions(player_id);

CREATE TABLE campaign_comment_reports (
    comment_id TEXT NOT NULL REFERENCES campaign_comments(id) ON DELETE CASCADE,
    reporter_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    reason TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY (comment_id, reporter_id)
);
