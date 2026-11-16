-- ADMINISTRATION › Ennemis (enemyRevisions.ts, docs/online/ENEMY_REVISIONS.md): a native entity
-- of the game (data/enemies, src/enemyCatalog.json) edited by an administrator and published as
-- a numbered revision. Its definition file is kept in the private R2 bucket
-- (enemies/<id>/<revision>-<sha256>.json) and served by the Worker. Every revision is kept: a
-- campaign level revision names the enemy revisions it plays (metadata.enemyRevisions of its
-- file, part of its fingerprint), so an older level revision (a challenge, a replay, a record)
-- keeps playing the definition it was published with. Never deleted, never edited.
--
-- The publication is one batch: this row first, then the next revision of every campaign level
-- that uses the entity (campaign_level_files, level_versions, campaign_level_changes, with each
-- level's choice about its records). `base_checked` is computed in that batch from « every
-- level is still at the revision the administrator prepared, and no other revision of this
-- entity was published »: when it is not, the CHECK fails and nothing of the batch is kept.
CREATE TABLE enemy_revisions (
    enemy_id              TEXT NOT NULL,
    revision              INTEGER NOT NULL CHECK (revision >= 1),
    file_key              TEXT NOT NULL,
    file_sha256           TEXT NOT NULL,
    file_size             INTEGER NOT NULL,
    min_game_version      TEXT NOT NULL,
    -- [{ level_id, version, content_hash, from_version, records, carried_bests }]
    levels                TEXT NOT NULL DEFAULT '[]',
    published_by          TEXT REFERENCES players (id) ON DELETE SET NULL,
    published_by_username TEXT,
    published_at          TEXT NOT NULL,
    base_checked          INTEGER NOT NULL CHECK (base_checked = 1),
    PRIMARY KEY (enemy_id, revision)
);
