-- 2.80: the administrators' tests of Asterion played with invited players (raidTests.ts,
-- docs/RAIDS.md « Tester avec des joueurs invités »). ADMINISTRATION › Raids › « Tester Asterion » (one zone or the whole raid
-- simulated) stays local and offline while nobody is invited; with invited players the test goes
-- through a relay of its own (a CoopRoom marked as a test): the administrator's computer and every
-- invited one play the same steps (the simulated players computed by every computer alike, the
-- seats of the invited players and their « Invincible » given at a step by the relay).
--
-- A test is never a raid: it has no raid_instances row, no event, no member, no report, no
-- contribution, no reward, no ranking and no progress of the titan (reports naming a test are refused).

CREATE TABLE raid_tests (
    id          TEXT PRIMARY KEY,
    admin_id    TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    mode        TEXT NOT NULL CHECK (mode IN ('zone', 'raid')),
    -- The test's settings as the server checked them (JSON): every computer builds the same test from them.
    config      TEXT NOT NULL,
    -- The game's version and rules the test needs (an invited game with another one cannot join).
    fingerprint TEXT NOT NULL,
    -- The groups an invited player may be placed in (a zone: 1; the whole raid: its groups and reinforcements)
    -- and the seats of a group (the administrator's place included).
    groups      INTEGER NOT NULL,
    seats       INTEGER NOT NULL,
    status      TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'ended')),
    created_at  TEXT NOT NULL,
    ended_at    TEXT
);
CREATE INDEX raid_tests_admin ON raid_tests (admin_id, status);

-- An invitation: answered within its time (expires_at, 3 min), cancelled by the administrator, or
-- over with the test. `group_no` (0-based) and `invincible` are the administrator's choices for the player.
CREATE TABLE raid_test_invitations (
    test_id     TEXT NOT NULL REFERENCES raid_tests (id) ON DELETE CASCADE,
    player_id   TEXT NOT NULL REFERENCES players (id) ON DELETE CASCADE,
    group_no    INTEGER NOT NULL DEFAULT 0,
    invincible  INTEGER NOT NULL DEFAULT 0,
    status      TEXT NOT NULL CHECK (status IN ('pending', 'accepted', 'declined', 'expired', 'cancelled')),
    created_at  TEXT NOT NULL,
    expires_at  TEXT NOT NULL,
    answered_at TEXT,
    PRIMARY KEY (test_id, player_id)
);
CREATE INDEX raid_test_invitations_player ON raid_test_invitations (player_id, status);
