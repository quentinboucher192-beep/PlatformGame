-- Cosmic Online 2.66 — clans (src/clans.ts). One clan per player (clan_members.player_id is the
-- key), 50 members at most (checked inside the INSERT that adds a member: atomic), one chief
-- (partial unique index), officers and members. Joining: by invitation, by application (a
-- short message) or open, as the chief chooses. The clan's discussion is a group conversation
-- of the social tables (conversations.clan_id): its members follow the clan's. The clan's
-- points (ranking of the clans) are the sum, over the official EXTRÊME levels, of the clan's
-- best VERIFIED result of the « course à la vérification » (race_runs), kept per level in
-- clan_level_bests by the server (never sent by a game). Append-only (see 0001).

CREATE TABLE clans (
    id               TEXT PRIMARY KEY,
    name             TEXT NOT NULL,                       -- 3 to 32 characters
    name_normalized  TEXT NOT NULL,                       -- lower case, without accents (search)
    tag              TEXT NOT NULL,                       -- 2 to 5 letters or digits, shown as [TAG]
    tag_normalized   TEXT NOT NULL,                       -- upper case: unique whatever the case
    motto            TEXT NOT NULL DEFAULT '',            -- « devise », 80 characters at most
    emblem_shape     TEXT NOT NULL DEFAULT 'orb'
        CHECK (emblem_shape IN ('orb', 'star', 'shield', 'diamond', 'hexagon', 'crescent')),
    emblem_primary   TEXT NOT NULL DEFAULT '#ffd27a',     -- '#rrggbb': the shape
    emblem_secondary TEXT NOT NULL DEFAULT '#a66bff',     -- '#rrggbb': the border and the background
    join_policy      TEXT NOT NULL DEFAULT 'request' CHECK (join_policy IN ('invite', 'request', 'open')),
    points           REAL NOT NULL DEFAULT 0,             -- sum of the bests per EXTRÊME level (server)
    points_levels    INTEGER NOT NULL DEFAULT 0,          -- EXTRÊME levels with a result
    points_at        TEXT,                                -- when the current points were reached (ties)
    created_by       TEXT REFERENCES players (id) ON DELETE SET NULL,
    created_at       TEXT NOT NULL,
    updated_at       TEXT NOT NULL
);
CREATE UNIQUE INDEX clans_tag ON clans (tag_normalized);
CREATE INDEX clans_name ON clans (name_normalized);
CREATE INDEX clans_points ON clans (points DESC, points_at, id);
CREATE INDEX clans_created ON clans (created_at);  -- ADMINISTRATION › Clans, newest first

CREATE TABLE clan_members (
    player_id TEXT PRIMARY KEY REFERENCES players (id) ON DELETE CASCADE,  -- one clan per player
    clan_id   TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    role      TEXT NOT NULL DEFAULT 'member' CHECK (role IN ('leader', 'officer', 'member')),
    joined_at TEXT NOT NULL
);
CREATE INDEX clan_members_clan ON clan_members (clan_id, role, joined_at);
-- One chief per clan, whatever two requests do at the same time.
CREATE UNIQUE INDEX clan_members_one_leader ON clan_members (clan_id) WHERE role = 'leader';

-- Pending invitations (sent by the chief or an officer) and applications (sent by a player).
CREATE TABLE clan_requests (
    clan_id    TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    player_id  TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    kind       TEXT NOT NULL CHECK (kind IN ('invite', 'apply')),
    message    TEXT,                                      -- an application's short message
    by_player  TEXT REFERENCES players (id) ON DELETE SET NULL,  -- who invited
    created_at TEXT NOT NULL,
    PRIMARY KEY (clan_id, player_id)
);
CREATE INDEX clan_requests_player ON clan_requests (player_id, kind, created_at);
CREATE INDEX clan_requests_clan ON clan_requests (clan_id, kind, created_at);

-- The clan's best team on every official EXTRÊME level, per team size (1 = Solo; 2 Duo and
-- 3 Trio when the cooperation comes): recomputed by the server when a member's attempt is
-- verified, when a member joins or leaves, and when the campaign's revisions change.
CREATE TABLE clan_level_bests (
    clan_id     TEXT NOT NULL REFERENCES clans (id) ON DELETE CASCADE,
    level_id    TEXT NOT NULL,
    team_size   INTEGER NOT NULL DEFAULT 1 CHECK (team_size BETWEEN 1 AND 3),
    score       REAL NOT NULL,                            -- progress score 0..100 (race.ts)
    finished    INTEGER NOT NULL DEFAULT 0,
    run_id      TEXT,
    players     TEXT NOT NULL,                            -- JSON array of the team's player ids
    achieved_at TEXT NOT NULL,
    PRIMARY KEY (clan_id, level_id, team_size)
);

-- The clan's discussion: a group conversation whose members are the clan's.
ALTER TABLE conversations ADD COLUMN clan_id TEXT REFERENCES clans (id) ON DELETE CASCADE;
CREATE UNIQUE INDEX conversations_clan ON conversations (clan_id) WHERE clan_id IS NOT NULL;
