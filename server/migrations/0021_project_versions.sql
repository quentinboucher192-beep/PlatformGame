-- Cosmic Online — named versions of collaborative projects (phase 5). A version names one
-- revision of the project; it keeps the SHA-256 of that revision's content, checked when
-- the version is created and whenever it is read. Versions are immutable: no route changes
-- or deletes them (they go only with their project). Append-only (see 0001).

CREATE TABLE project_versions (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    notes TEXT NOT NULL DEFAULT '',
    revision INTEGER NOT NULL,
    content_hash TEXT NOT NULL,
    created_by TEXT REFERENCES players(id) ON DELETE SET NULL,
    created_at TEXT NOT NULL,
    UNIQUE (project_id, name)
);
CREATE INDEX project_versions_project ON project_versions(project_id, created_at);
