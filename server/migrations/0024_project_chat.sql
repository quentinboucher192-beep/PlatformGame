-- Cosmic Online — the chat of a collaborative project (live editor). Messages are kept with
-- the project; a message may answer another one (reply_to). Only members read and write them
-- (checked by the Worker). A deleted account takes its messages with it (CASCADE); a reply to
-- a deleted message keeps its text but loses the link. Append-only (see 0001).

CREATE TABLE project_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    author_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    body TEXT NOT NULL,
    reply_to INTEGER REFERENCES project_messages(id) ON DELETE SET NULL,
    created_at TEXT NOT NULL
);
CREATE INDEX project_messages_project ON project_messages(project_id, id);
