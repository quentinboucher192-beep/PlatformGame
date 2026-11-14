-- Cosmic Online — live editing of collaborative projects (phases 3-4). Presence, cursors
-- and temporary locks live only in memory (Durable Object ProjectRoom); the operations
-- below are persistent: each has a sequence number given by the room, in one order for
-- every member, and an id chosen by the game (a retried operation is never applied twice).
-- Append-only (see 0001).

CREATE TABLE project_ops (
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    seq INTEGER NOT NULL,
    client_op_id TEXT NOT NULL,
    author_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    op TEXT NOT NULL,                       -- JSON: add / set / del / doc
    created_at TEXT NOT NULL,
    PRIMARY KEY (project_id, seq),
    UNIQUE (project_id, client_op_id)
);

-- Last operation included in a revision (snapshot) and the last operation of the project.
ALTER TABLE project_revisions ADD COLUMN ops_seq INTEGER NOT NULL DEFAULT 0;
ALTER TABLE projects ADD COLUMN ops_seq INTEGER NOT NULL DEFAULT 0;
