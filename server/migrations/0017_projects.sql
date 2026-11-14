-- Cosmic Online — collaborative projects (phase 2): a level edited by several players, with
-- roles checked by the server, invitations (keys stored as SHA-256 only), join requests and
-- the revisions of its content (each save is a new revision: nothing is overwritten).
-- Append-only (see 0001).

CREATE TABLE projects (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    planet TEXT,
    -- private: members only; invite: joined with an invitation only; request: listed, players
    -- ask to join; open: listed, anyone joins with join_role.
    visibility TEXT NOT NULL DEFAULT 'private' CHECK (visibility IN ('private', 'invite', 'request', 'open')),
    join_role TEXT NOT NULL DEFAULT 'viewer' CHECK (join_role IN ('editor', 'tester', 'viewer')),
    revision INTEGER NOT NULL DEFAULT 0,       -- current revision (0: no content yet)
    member_count INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    deleted_at TEXT
);
CREATE INDEX projects_owner ON projects(owner_id);
CREATE INDEX projects_listed ON projects(visibility, updated_at);

CREATE TABLE project_members (
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK (role IN ('owner', 'admin', 'editor', 'tester', 'viewer')),
    joined_at TEXT NOT NULL,
    added_by TEXT,
    PRIMARY KEY (project_id, player_id)
);
CREATE INDEX project_members_player ON project_members(player_id);

-- Invitations: a key (shown once to its creator, kept only as SHA-256) with a role, a number
-- of uses and an expiry; or a direct invitation to one player (no key: accepted from his
-- account).
CREATE TABLE project_invites (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    key_hash TEXT UNIQUE,
    key_hint TEXT,                             -- last 4 characters, to recognise a key in the list
    target_player_id TEXT REFERENCES players(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK (role IN ('admin', 'editor', 'tester', 'viewer')),
    max_uses INTEGER,                          -- NULL: unlimited (keys only)
    uses INTEGER NOT NULL DEFAULT 0,
    expires_at TEXT,
    revoked_at TEXT,
    created_by TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL
);
CREATE INDEX project_invites_project ON project_invites(project_id, created_at);
CREATE INDEX project_invites_target ON project_invites(target_player_id);

CREATE TABLE project_join_requests (
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    message TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    PRIMARY KEY (project_id, player_id)
);

-- Content: every save is a new revision (R2 projects/{id}/r{revision}.json), never replaced.
CREATE TABLE project_revisions (
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    revision INTEGER NOT NULL,
    content_key TEXT NOT NULL,
    content_hash TEXT NOT NULL,                -- SHA-256 of the stored JSON
    size INTEGER NOT NULL,
    author_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    message TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    PRIMARY KEY (project_id, revision)
);

-- What happened in a project (members, roles, invitations, saves), shown to its members.
CREATE TABLE project_activity (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    actor_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    action TEXT NOT NULL,
    details TEXT NOT NULL DEFAULT '{}',
    created_at TEXT NOT NULL
);
CREATE INDEX project_activity_project ON project_activity(project_id, created_at);
