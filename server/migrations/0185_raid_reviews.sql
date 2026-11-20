-- 2.77.4: the players' reviews of the raid events (raidReviews.ts, docs/RAIDS.md §24).
--
-- After playing an event (a seat in one of its instances and a contribution accepted by its consensus:
-- the server checks its own tables), a player gives it 1 to 5 stars and, if they wish, a comment of
-- 500 characters at most. One review per player and event (the unique key), changed or deleted by its
-- author. The comments' moderation (workshop.ts): reports (hidden after 3, `hidden_by` 'reports'), an
-- administrator masks or restores a comment (`hidden_by` their id); a masked comment keeps its stars.
-- Nothing else is changed: the clan raids scheduled before 2.77.4 stay as they are, until their end.
CREATE TABLE raid_reviews (
    id           TEXT PRIMARY KEY,
    event_id     TEXT NOT NULL REFERENCES raid_events (id) ON DELETE CASCADE,
    player_id    TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    stars        INTEGER NOT NULL CHECK (stars BETWEEN 1 AND 5),
    body         TEXT NOT NULL DEFAULT '' CHECK (length(body) <= 500),
    hidden       INTEGER NOT NULL DEFAULT 0 CHECK (hidden IN (0, 1)),
    hidden_by    TEXT,
    hidden_at    TEXT,
    report_count INTEGER NOT NULL DEFAULT 0 CHECK (report_count >= 0),
    created_at   TEXT NOT NULL,
    edited_at    TEXT,
    UNIQUE (event_id, player_id)
);
CREATE INDEX raid_reviews_event ON raid_reviews (event_id, created_at);
CREATE INDEX raid_reviews_player ON raid_reviews (player_id);

-- The reports of a review's comment: one per player and review (the same reasons as the comments).
CREATE TABLE raid_review_reports (
    review_id   TEXT NOT NULL REFERENCES raid_reviews (id) ON DELETE CASCADE,
    reporter_id TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    reason      TEXT NOT NULL,
    created_at  TEXT NOT NULL,
    PRIMARY KEY (review_id, reporter_id)
);
