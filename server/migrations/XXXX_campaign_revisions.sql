-- 2.45: the campaign edited by an administrator (ADMINISTRATION › Campagne, docs/online/CAMPAIGN_REVISIONS.md).
-- A level of the campaign opened in the game's editor is published as a new revision of that
-- level, without a game update: its file is kept in the private R2 bucket (campaign/<level>/…)
-- and served through the Worker; the games download it and play it instead of the bundled file.
-- Every revision keeps its own content hash, hence its own leaderboards (level_versions), as
-- the revisions of a game release always did.

-- The file of a revision published by an administrator (the revisions of a game release have
-- none: the game carries them). sha256: of the stored bytes (the games check it); rating: the
-- difficulty computed by the game before publishing (JSON of DifficultyRating, or NULL).
CREATE TABLE campaign_level_files (
    level_id            TEXT NOT NULL REFERENCES levels (id),
    version             INTEGER NOT NULL,
    content_hash        TEXT NOT NULL,
    file_key            TEXT NOT NULL,
    file_sha256         TEXT NOT NULL,
    file_size           INTEGER NOT NULL,
    rating              TEXT,
    -- The administrator (no foreign key: the account may be deleted, the revision stays).
    published_by        TEXT,
    published_by_username TEXT,
    published_at        TEXT NOT NULL,
    PRIMARY KEY (level_id, version),
    UNIQUE (level_id, content_hash)
);

-- What made a revision current: a publication or a return to an earlier revision, with the
-- REQUIRED choice about the records: 'reset' (the revision has its own boards, the others stay
-- archived under their revision) or 'keep' (the change has no effect on play: the bests of the
-- revision it replaces are carried over to it). carried_bests: the bests copied ('keep').
CREATE TABLE campaign_level_changes (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    level_id            TEXT NOT NULL REFERENCES levels (id),
    action              TEXT NOT NULL CHECK (action IN ('publish', 'revert')),
    version             INTEGER NOT NULL,
    content_hash        TEXT NOT NULL,
    from_version        INTEGER NOT NULL,
    from_hash           TEXT NOT NULL,
    records             TEXT NOT NULL CHECK (records IN ('reset', 'keep')),
    carried_bests       INTEGER NOT NULL DEFAULT 0,
    by_player           TEXT,
    by_username         TEXT,
    created_at          TEXT NOT NULL
);
CREATE INDEX campaign_level_changes_level ON campaign_level_changes (level_id, created_at);

-- « Garder les records »: the boards of `to_hash` also count the runs played on `from_hash`
-- (kept transitive: a chain of compatible revisions points to every earlier one). The bests
-- are copied into player_bests at once; the weekly boards read the VERIFIED records of every
-- linked hash; a run of `from_hash` verified later also improves the bests of `to_hash`.
CREATE TABLE campaign_board_links (
    level_id            TEXT NOT NULL,
    to_hash             TEXT NOT NULL,
    from_hash           TEXT NOT NULL,
    created_at          TEXT NOT NULL,
    PRIMARY KEY (to_hash, from_hash)
);
CREATE INDEX campaign_board_links_from ON campaign_board_links (from_hash);
