// 2.80: an administrator's test of Asterion played with invited players (docs/RAIDS.md §22).
//
// ADMINISTRATION › Raids › « Tester Asterion » (one zone, or the whole raid simulated) stays local and
// offline while nobody is invited. With invited players, the test becomes a relayed TEST (raid_tests,
// migration 0195): a CoopRoom marked as a test (coop.ts, `test`), the administrator on seat 0, each
// invited player on another seat once they accepted. Every computer of the test builds the same test
// from its settings (`config`, checked here) and plays the same steps: the simulated players are
// computed by every computer alike, the relay gives the real players' frames and, at a step
// (TEST_SEATS), where each one plays (group, seat of the test) and whether they are invincible.
//
// * Only an active administrator creates a test, invites, cancels, changes a player's group or
//   « Invincible », and ends it — the session's role, checked on every request (requireAdminRole: for
//   anyone else the routes do not exist). The actor is always the session, never an id of the body.
// * An invitation goes to a player connected now (their presence, shown), never one blocked either way;
//   rate-limited; it lasts 3 minutes (INVITE_TTL_MS), or until the test ends; the player accepts or
//   declines it. Only the administrator and the players who accepted reach the relay of the test.
// * A real player takes the seat of a simulated one in their group, never one more than the group's
//   seats; a group full of real players refuses another one.
// * Version: an invited game must have the test's fingerprint (the game's version, the rules of the
//   simulation and of the titan): another one is refused with a clear message.
// * A test never counts: no raid instance, no member, no report (raidZones.ts refuses any report that
//   names a test), no contribution, reward, ranking or progress of the titan.

import { requireAuth } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { MAX_RAID_PLAYERS, TEST_NONE, ask, stub } from "./coop";
import { PRESENCE_TIMEOUT_MS, presenceOf } from "./friends";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, likePrefix, normalizeUsername, paging, queryString } from "./validate";

/** An invitation is valid this long (or until the test ends). */
export const INVITE_TTL_MS = 3 * 60 * 1000;
/** Real players invited at once at most (the relay's seats, the administrator's aside). */
export const MAX_GUESTS = MAX_RAID_PLAYERS - 1;
/** The full raid's simulation forms up to this many reinforcement groups after its own (raid::SimSetup::maxReinforcements). */
export const REINFORCEMENTS = 8;

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const FINGERPRINT_RE = /^[A-Za-z0-9._:|+-]{8,64}$/;
const BRANCH_RE = /^[a-z0-9_]{1,40}$/;

type Mode = "zone" | "raid";

interface TestRow {
    id: string;
    admin_id: string;
    mode: Mode;
    config: string;
    fingerprint: string;
    groups: number;
    seats: number;
    status: "open" | "ended";
    created_at: string;
    ended_at: string | null;
}

interface InvitationRow {
    test_id: string;
    player_id: string;
    group_no: number;
    invincible: number;
    status: string;
    created_at: string;
    expires_at: string;
    answered_at: string | null;
    username: string | null;
    role: string | null;
}

/** The status as the player sees it now (a pending invitation past its time is expired). */
function statusOf(r: Pick<InvitationRow, "status" | "expires_at">, now: string): string {
    return r.status === "pending" && r.expires_at < now ? "expired" : r.status;
}

function invitationJson(r: InvitationRow, now: string) {
    return {
        player: { id: r.player_id, username: r.username ?? "", role: r.role === "admin" ? "admin" : "player" },
        group: r.group_no,
        invincible: r.invincible === 1,
        status: statusOf(r, now),
        created_at: r.created_at,
        expires_at: r.expires_at,
        answered_at: r.answered_at,
    };
}

function testJson(t: TestRow) {
    return {
        id: t.id,
        mode: t.mode,
        config: JSON.parse(t.config) as Record<string, unknown>,
        fingerprint: t.fingerprint,
        groups: t.groups,
        seats: t.seats,
        status: t.status,
        created_at: t.created_at,
        ended_at: t.ended_at,
    };
}

/** « une zone » / « raid complet simulé »: what the invitation says of the test (never a text of the game). */
function summary(mode: Mode): string {
    return mode === "zone" ? "une zone" : "raid complet simulé";
}

/**
 * The settings of a test, checked field by field and written again by the server (bounded numbers,
 * booleans, a branch id): every computer of the test builds the same test from them.
 */
function readConfig(mode: Mode, raw: unknown, fail: (m: string) => never): { config: Record<string, unknown>; groups: number; seats: number } {
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) fail("objet attendu");
    const c = raw as Record<string, unknown>;
    const common = ["skill", "invincible", "bots_invincible"];
    const keys = mode === "zone" ? [...common, "branch", "zone", "players", "start_phase", "parts", "relics"] : [...common, "groups", "bots"];
    const v = new Validator(c, keys);
    const out: Record<string, unknown> = {};
    out.skill = v.int("skill", { min: 0, max: 2 });
    out.invincible = v.bool("invincible");
    out.bots_invincible = v.bool("bots_invincible", { optional: true }) ?? false;
    let groups = 1;
    let seats = 2;
    if (mode === "zone") {
        out.branch = v.str("branch", { min: 1, max: 40, pattern: BRANCH_RE });
        out.zone = v.int("zone", { min: -1, max: 64 });
        out.players = v.int("players", { min: 1, max: MAX_RAID_PLAYERS });
        out.start_phase = v.int("start_phase", { min: 1, max: 4 });
        out.parts = v.int("parts", { min: 0, max: 127 });
        out.relics = v.int("relics", { min: 0, max: 7 });
        seats = (out.players as number | undefined) ?? 1;
    } else {
        out.groups = v.int("groups", { min: 1, max: 14 });
        out.bots = v.int("bots", { min: 1, max: MAX_RAID_PLAYERS - 1 });
        groups = ((out.groups as number | undefined) ?? 1) + REINFORCEMENTS;
        seats = ((out.bots as number | undefined) ?? 1) + 1;
    }
    if (v.errors.length) fail(v.errors.map((e) => `${e.field} : ${e.message}`).join(", "));
    return { config: out, groups, seats };
}

async function testOf(ctx: Ctx, id: string): Promise<TestRow | null> {
    if (!ID_RE.test(id)) return null;
    return await ctx.env.DB.prepare("SELECT * FROM raid_tests WHERE id = ?1").bind(id).first<TestRow>();
}

/** The administrator's own test, still open (another administrator's does not exist for them). */
async function ownOpenTest(ctx: Ctx, me: string): Promise<TestRow> {
    const t = await testOf(ctx, ctx.params.testId ?? "");
    if (!t || t.admin_id !== me) throw Errors.notFound("RAID_TEST_NOT_FOUND", "Test introuvable.");
    if (t.status !== "open") throw Errors.conflict("RAID_TEST_ENDED", "Ce test est terminé.");
    return t;
}

async function invitationsOf(ctx: Ctx, testId: string): Promise<InvitationRow[]> {
    const rows = await ctx.env.DB.prepare(
        `SELECT i.*, p.username, p.role FROM raid_test_invitations i LEFT JOIN players p ON p.id = i.player_id
         WHERE i.test_id = ?1 ORDER BY i.created_at, i.player_id`,
    )
        .bind(testId)
        .all<InvitationRow>();
    return rows.results;
}

/** The test ends: its pending invitations expire, its relay tells every game (nothing is kept). */
async function endTestNow(ctx: Ctx, id: string): Promise<void> {
    const now = nowIso();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("UPDATE raid_tests SET status = 'ended', ended_at = COALESCE(ended_at, ?2) WHERE id = ?1").bind(id, now),
        ctx.env.DB.prepare("UPDATE raid_test_invitations SET status = 'expired' WHERE test_id = ?1 AND status = 'pending'").bind(id),
    ]);
    await ask(ctx.env, id, "/test-end", {});
}

/**
 * Whether `group` of the test has a seat for one more real player (`except`: a player already counted
 * there who moves), and the test one more guest: the invitations pending or accepted.
 */
async function checkRoom(ctx: Ctx, t: TestRow, group: number, except: string): Promise<void> {
    const now = nowIso();
    const rows = await ctx.env.DB.prepare(
        `SELECT player_id, group_no FROM raid_test_invitations WHERE test_id = ?1 AND player_id <> ?2
           AND (status = 'accepted' OR (status = 'pending' AND expires_at >= ?3))`,
    )
        .bind(t.id, except, now)
        .all<{ player_id: string; group_no: number }>();
    if (rows.results.length >= MAX_GUESTS) throw Errors.conflict("RAID_TEST_FULL", `Un test reçoit ${MAX_GUESTS} joueurs invités au plus.`);
    const inGroup = rows.results.filter((r) => r.group_no === group).length;
    if (inGroup >= t.seats - 1)
        throw Errors.conflict(
            "RAID_TEST_GROUP_FULL",
            t.seats <= 1
                ? "Aucun joueur simulé à remplacer : ajoutez-en au moins un avant d'inviter."
                : "Ce groupe n'a plus de joueur simulé à remplacer : choisissez-en un autre.",
        );
}

// ------------------------------------------------------------------ the administrator

/**
 * POST /admin/raid-tests {mode: "zone" | "raid", config, fingerprint}: a relayed test, its relay made
 * at once (the administrator starts it on their seat). An older open test of the same administrator ends.
 */
export async function create(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidTestCreate, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["mode", "config", "fingerprint"]);
    const mode = v.oneOf("mode", ["zone", "raid"] as const);
    const fingerprint = v.str("fingerprint", { min: 8, max: 64, pattern: FINGERPRINT_RE });
    v.done();
    const { config, groups, seats } = readConfig(mode!, body.config, (m) => {
        v.fail("config", m);
        v.done();
        throw new Error("unreachable");
    });
    const db = ctx.env.DB;
    const open = await db.prepare("SELECT id FROM raid_tests WHERE admin_id = ?1 AND status = 'open'").bind(me).all<{ id: string }>();
    for (const r of open.results) await endTestNow(ctx, r.id);
    const id = crypto.randomUUID();
    const now = nowIso();
    await db
        .prepare(
            `INSERT INTO raid_tests (id, admin_id, mode, config, fingerprint, groups, seats, status, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'open', ?8)`,
        )
        .bind(id, me, mode, JSON.stringify(config), fingerprint, groups, seats, now)
        .run();
    await ask(ctx.env, id, "/create", {
        id,
        host: me,
        host_name: auth.player.username,
        level: { source: "raid_test", id: "raid_test", hash: fingerprint, title: "Test d'Asterion" },
        test: { test_id: id, groups, seats, invincible: config.invincible === true },
    });
    const row = (await testOf(ctx, id))!;
    return json(ctx, { test: testJson(row), invitations: [] }, { status: 201 });
}

/** GET /admin/raid-tests/{testId}: my test, its invitations and where each real player plays now. */
export async function get(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const t = await testOf(ctx, ctx.params.testId ?? "");
    if (!t || t.admin_id !== auth.player.id) throw Errors.notFound("RAID_TEST_NOT_FOUND", "Test introuvable.");
    const now = nowIso();
    const rows = await invitationsOf(ctx, t.id);
    let places: { player_id: string; group: number; game_seat: number; connected: boolean; absent: boolean }[] = [];
    if (t.status === "open") {
        const st = await ask<{
            seats: { seat: number; player_id: string; connected: boolean; absent: boolean; vacant: boolean }[];
            test: { places: { seat: number; player_id: string; group: number; game_seat: number }[] } | null;
        }>(ctx.env, t.id, "/status");
        places = (st.test?.places ?? [])
            .filter((p) => p.player_id && p.group !== TEST_NONE)
            .map((p) => {
                const s = st.seats.find((x) => x.seat === p.seat);
                return { player_id: p.player_id, group: p.group, game_seat: p.game_seat, connected: !!s?.connected, absent: !!s?.absent };
            });
    }
    return json(ctx, {
        test: testJson(t),
        invitations: rows.map((r) => {
            const where = places.find((p) => p.player_id === r.player_id);
            return { ...invitationJson(r, now), playing: where ? { group: where.group, seat: where.game_seat, connected: where.connected && !where.absent } : null };
        }),
    });
}

/**
 * GET /admin/raid-test-players?q=&test=&limit=&offset=: the players connected now (their presence, shown
 * and fresh), active, never me nor a player blocked either way, by name (`q`, anywhere in it); with
 * `test`, those already invited to it (waiting or accepted) are left out. Administrators are listed too.
 */
export async function players(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const me = auth.player.id;
    const page = paging(ctx.url, 30, 50);
    const q = queryString(ctx.url, "q", /^[A-Za-z0-9_.-]{1,20}$/);
    const test = queryString(ctx.url, "test", ID_RE);
    const now = nowIso();
    const binds: unknown[] = [me, new Date(Date.now() - PRESENCE_TIMEOUT_MS).toISOString()];
    const where = [
        "p.account_status = 'active'",
        "p.id <> ?1",
        "p.presence_state IS NOT NULL",
        "p.presence_at >= ?2",
        "p.presence_visible = 1",
        "NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = ?1 AND b.blocked_id = p.id) OR (b.player_id = p.id AND b.blocked_id = ?1))",
    ];
    if (q) {
        binds.push("%" + likePrefix(normalizeUsername(q)));
        where.push(`p.username_normalized LIKE ?${binds.length} ESCAPE '\\'`);
    }
    if (test) {
        binds.push(test, now);
        where.push(
            `NOT EXISTS (SELECT 1 FROM raid_test_invitations i WHERE i.test_id = ?${binds.length - 1} AND i.player_id = p.id
               AND (i.status = 'accepted' OR (i.status = 'pending' AND i.expires_at >= ?${binds.length})))`,
        );
    }
    const cond = where.join(" AND ");
    const count = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM players p WHERE ${cond}`)
        .bind(...binds)
        .first<{ n: number }>();
    binds.push(page.limit + 1, page.offset);
    const rows = await ctx.env.DB.prepare(
        `SELECT p.id, p.username, p.role, p.presence_state, p.presence_at, p.presence_visible FROM players p WHERE ${cond}
         ORDER BY p.username_normalized, p.id LIMIT ?${binds.length - 1} OFFSET ?${binds.length}`,
    )
        .bind(...binds)
        .all<{ id: string; username: string; role: string | null; presence_state: string | null; presence_at: string | null; presence_visible: number }>();
    const list = rows.results.slice(0, page.limit);
    return json(ctx, {
        players: list.map((p) => ({ id: p.id, username: p.username, role: p.role === "admin" ? "admin" : "player", presence: presenceOf(p) })),
        total: count?.n ?? 0,
        offset: page.offset,
        has_more: rows.results.length > page.limit,
    });
}

/**
 * POST /admin/raid-tests/{testId}/invitations {player_id, group?, invincible?}: invites a player connected
 * now (an administrator or not) to my open test, in a group (the whole raid's simulation; 0 for a zone),
 * « Invincible » or not. They are told (a notification) and answer within 3 minutes.
 */
export async function invite(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const me = auth.player.id;
    const t = await ownOpenTest(ctx, me);
    await rateLimit(ctx, RULES.raidTestInvite, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id", "group", "invincible"]);
    const target = v.str("player_id", { min: 36, max: 36, pattern: ID_RE })!;
    const group = v.int("group", { min: 0, max: t.groups - 1, optional: true }) ?? 0;
    const invincible = v.bool("invincible", { optional: true }) ?? false;
    v.done();
    if (target === me) throw Errors.conflict("RAID_TEST_SELF", "Vous êtes déjà dans votre test.");
    const db = ctx.env.DB;
    const p = await db
        .prepare(
            `SELECT p.id, p.username, p.role, p.account_status, p.presence_state, p.presence_at, p.presence_visible,
                    EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = ?1 AND b.blocked_id = ?2) OR (b.player_id = ?2 AND b.blocked_id = ?1)) AS blocked
             FROM players p WHERE p.id = ?2`,
        )
        .bind(me, target)
        .first<{ id: string; username: string; role: string | null; account_status: string; presence_state: string | null; presence_at: string | null; presence_visible: number; blocked: number }>();
    if (!p || p.account_status !== "active") throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    if (p.blocked) throw Errors.forbidden("RAID_TEST_BLOCKED", "Ce joueur ne peut pas être invité.");
    const presence = presenceOf(p);
    if (presence !== "online" && presence !== "away") throw Errors.conflict("PLAYER_OFFLINE", "Ce joueur n'est plus connecté.");
    await checkRoom(ctx, t, group, target);
    const now = nowIso();
    const expires = new Date(Date.now() + INVITE_TTL_MS).toISOString();
    // Once per player and test: again after a refusal, a cancellation or an expiry, never over a valid one.
    const res = await db
        .prepare(
            `INSERT INTO raid_test_invitations (test_id, player_id, group_no, invincible, status, created_at, expires_at)
             VALUES (?1, ?2, ?3, ?4, 'pending', ?5, ?6)
             ON CONFLICT (test_id, player_id) DO UPDATE SET group_no = ?3, invincible = ?4, status = 'pending', created_at = ?5, expires_at = ?6, answered_at = NULL
             WHERE raid_test_invitations.status IN ('declined', 'expired', 'cancelled')
                OR (raid_test_invitations.status = 'pending' AND raid_test_invitations.expires_at < ?5)`,
        )
        .bind(t.id, target, group, invincible ? 1 : 0, now, expires)
        .run();
    if (!res.meta.changes) throw Errors.conflict("RAID_TEST_ALREADY_INVITED", "Ce joueur est déjà invité à ce test.");
    await ask(ctx.env, t.id, "/test-place", { player_id: target, group, invincible });
    await notify(ctx, target, "raid", { actor: me, data: { event: "test_invite", test_id: t.id, mode: t.mode, title: summary(t.mode) } });
    const row = (await invitationsOf(ctx, t.id)).find((r) => r.player_id === target)!;
    return json(ctx, { invitation: invitationJson(row, now) }, { status: 201 });
}

/**
 * PATCH /admin/raid-tests/{testId}/invitations/{playerId} {invincible?, group?}: a player's « Invincible »
 * or group, before or during the test: the relay gives it to every computer from its next step.
 */
export async function update(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const me = auth.player.id;
    const t = await ownOpenTest(ctx, me);
    await rateLimit(ctx, RULES.adminWrite, "player:" + me);
    const target = ctx.params.playerId ?? "";
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["invincible", "group"]);
    const invincible = v.bool("invincible", { optional: true });
    const group = v.int("group", { min: 0, max: t.groups - 1, optional: true });
    v.done();
    const now = nowIso();
    const cur = (await invitationsOf(ctx, t.id)).find((r) => r.player_id === target);
    const st = cur ? statusOf(cur, now) : "";
    if (!cur || (st !== "pending" && st !== "accepted")) throw Errors.notFound("RAID_TEST_INVITATION_NOT_FOUND", "Invitation introuvable.");
    const g = group ?? cur.group_no;
    const inv = invincible ?? cur.invincible === 1;
    if (g !== cur.group_no) await checkRoom(ctx, t, g, target);
    await ctx.env.DB.prepare("UPDATE raid_test_invitations SET group_no = ?3, invincible = ?4 WHERE test_id = ?1 AND player_id = ?2")
        .bind(t.id, target, g, inv ? 1 : 0)
        .run();
    await ask(ctx.env, t.id, "/test-place", { player_id: target, group: g, invincible: inv });
    return json(ctx, { invitation: invitationJson({ ...cur, group_no: g, invincible: inv ? 1 : 0 }, now) });
}

/**
 * DELETE /admin/raid-tests/{testId}/invitations/{playerId}: cancels an invitation (waiting or accepted):
 * a player in the test leaves it at once, a simulated player takes the seat back.
 */
export async function cancel(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const me = auth.player.id;
    const t = await ownOpenTest(ctx, me);
    await rateLimit(ctx, RULES.adminWrite, "player:" + me);
    const target = ctx.params.playerId ?? "";
    const now = nowIso();
    const res = await ctx.env.DB.prepare(
        "UPDATE raid_test_invitations SET status = 'cancelled', answered_at = ?3 WHERE test_id = ?1 AND player_id = ?2 AND status IN ('pending', 'accepted')",
    )
        .bind(t.id, target, now)
        .run();
    if (!res.meta.changes) throw Errors.notFound("RAID_TEST_INVITATION_NOT_FOUND", "Invitation introuvable.");
    await ask(ctx.env, t.id, "/test-kick", { player_id: target });
    const row = (await invitationsOf(ctx, t.id)).find((r) => r.player_id === target)!;
    return json(ctx, { invitation: invitationJson(row, now) });
}

/**
 * POST /admin/raid-tests/{testId}/seat {group}: the administrator's own place in the whole raid's
 * simulation (a group's free place, or -1: watching), given to every computer from the next step.
 */
export async function seat(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const me = auth.player.id;
    const t = await ownOpenTest(ctx, me);
    await rateLimit(ctx, RULES.adminWrite, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["group"]);
    const group = v.int("group", { min: -1, max: t.mode === "zone" ? 0 : t.groups - 1 })!;
    v.done();
    const config = JSON.parse(t.config) as { invincible?: boolean };
    await ask(ctx.env, t.id, "/test-place", { player_id: me, group, invincible: config.invincible === true });
    return json(ctx, { group });
}

/** POST /admin/raid-tests/{testId}/end: the administrator ends the test (they left it): every game is told. */
export async function end(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const t = await testOf(ctx, ctx.params.testId ?? "");
    if (!t || t.admin_id !== auth.player.id) throw Errors.notFound("RAID_TEST_NOT_FOUND", "Test introuvable.");
    if (t.status === "open") await endTestNow(ctx, t.id);
    return json(ctx, { ended: true });
}

// ------------------------------------------------------------------ the invited players

interface PendingRow {
    test_id: string;
    mode: Mode;
    config: string;
    fingerprint: string;
    group_no: number;
    invincible: number;
    created_at: string;
    expires_at: string;
    admin_id: string;
    admin_name: string | null;
}

/** My invitations still to answer (valid, their test open), newest first. */
export async function pendingFor(db: D1Database, me: string, limit = 5): Promise<PendingRow[]> {
    try {
        const rows = await db
            .prepare(
                `SELECT i.test_id, t.mode, t.config, t.fingerprint, i.group_no, i.invincible, i.created_at, i.expires_at, t.admin_id, p.username AS admin_name
                 FROM raid_test_invitations i JOIN raid_tests t ON t.id = i.test_id LEFT JOIN players p ON p.id = t.admin_id
                 WHERE i.player_id = ?1 AND i.status = 'pending' AND i.expires_at >= ?2 AND t.status = 'open'
                 ORDER BY i.created_at DESC LIMIT ?3`,
            )
            .bind(me, nowIso(), limit)
            .all<PendingRow>();
        return rows.results;
    } catch {
        return []; // before migration 0195
    }
}

function pendingJson(r: PendingRow) {
    return {
        test_id: r.test_id,
        mode: r.mode,
        title: summary(r.mode),
        config: JSON.parse(r.config) as Record<string, unknown>,
        fingerprint: r.fingerprint,
        group: r.group_no,
        invincible: r.invincible === 1,
        admin: { id: r.admin_id, username: r.admin_name ?? "" },
        created_at: r.created_at,
        expires_at: r.expires_at,
    };
}

/** For GET /social/unread: my invitations to answer (the game tells them at once). */
export async function pendingSummary(db: D1Database, me: string) {
    return (await pendingFor(db, me)).map(pendingJson);
}

/** GET /raid-tests/invitations: my invitations to a test still to answer. */
export async function invitations(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    return json(ctx, { invitations: (await pendingFor(ctx.env.DB, auth.player.id, 10)).map(pendingJson) });
}

async function myInvitation(ctx: Ctx, me: string): Promise<{ t: TestRow; inv: InvitationRow }> {
    const t = await testOf(ctx, ctx.params.testId ?? "");
    const inv = t ? (await invitationsOf(ctx, t.id)).find((r) => r.player_id === me) : undefined;
    if (!t || !inv || inv.status === "cancelled") throw Errors.notFound("RAID_TEST_INVITATION_NOT_FOUND", "Invitation introuvable.");
    if (t.status !== "open") throw Errors.conflict("RAID_TEST_ENDED", "Ce test est terminé.");
    return { t, inv };
}

/**
 * POST /raid-tests/{testId}/accept {fingerprint}: I accept (an administrator or not). My game must
 * have the test's fingerprint (the same version and rules); then I may reach the test's relay.
 */
export async function accept(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["fingerprint"]);
    const fingerprint = v.str("fingerprint", { min: 8, max: 64, pattern: FINGERPRINT_RE })!;
    v.done();
    const { t, inv } = await myInvitation(ctx, me);
    const now = nowIso();
    const st = statusOf(inv, now);
    if (st === "expired") throw Errors.conflict("RAID_TEST_INVITATION_EXPIRED", "Cette invitation a expiré.");
    if (st !== "pending" && st !== "accepted") throw Errors.conflict("RAID_TEST_INVITATION_ANSWERED", "Vous avez déjà refusé cette invitation.");
    if (fingerprint !== t.fingerprint)
        throw Errors.conflict(
            "RAID_TEST_VERSION",
            "Ce test se joue avec une autre version du jeu ou de ses règles : mettez le jeu à jour (vous et l'administrateur devez avoir la même version).",
            { test: t.fingerprint, yours: fingerprint },
        );
    const room = await ask<{ exists: boolean; started: boolean; test: { ended: boolean } | null }>(ctx.env, t.id, "/status");
    if (!room.exists || !room.test || room.test.ended) throw Errors.conflict("RAID_TEST_ENDED", "Ce test est terminé.");
    if (!room.started) throw Errors.conflict("RAID_TEST_NOT_STARTED", "Le test n'a pas encore commencé : réessayez dans un instant.");
    await ctx.env.DB.prepare("UPDATE raid_test_invitations SET status = 'accepted', answered_at = ?3 WHERE test_id = ?1 AND player_id = ?2 AND status IN ('pending', 'accepted')")
        .bind(t.id, me, now)
        .run();
    await ask(ctx.env, t.id, "/invite", { player_id: me });
    await ask(ctx.env, t.id, "/test-place", { player_id: me, group: inv.group_no, invincible: inv.invincible === 1 });
    const admin = await ctx.env.DB.prepare("SELECT username FROM players WHERE id = ?1").bind(t.admin_id).first<{ username: string }>();
    return json(ctx, {
        test: { ...testJson(t), admin: { id: t.admin_id, username: admin?.username ?? "" } },
        invitation: invitationJson({ ...inv, status: "accepted", answered_at: now }, now),
    });
}

/** POST /raid-tests/{testId}/decline: I decline (the administrator sees it). */
export async function decline(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const t = await testOf(ctx, ctx.params.testId ?? "");
    if (!t) throw Errors.notFound("RAID_TEST_INVITATION_NOT_FOUND", "Invitation introuvable.");
    const res = await ctx.env.DB.prepare(
        "UPDATE raid_test_invitations SET status = 'declined', answered_at = ?3 WHERE test_id = ?1 AND player_id = ?2 AND status = 'pending'",
    )
        .bind(t.id, me, nowIso())
        .run();
    if (!res.meta.changes) throw Errors.notFound("RAID_TEST_INVITATION_NOT_FOUND", "Invitation introuvable.");
    return json(ctx, { declined: true });
}

/**
 * GET /raid-tests/{testId}/socket: the WebSocket of the test's game — its administrator, or a player
 * who accepted (never anyone else, the relay checks it again).
 */
export async function socket(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    if (ctx.request.headers.get("upgrade")?.toLowerCase() !== "websocket") throw new ApiError(426, "UPGRADE_REQUIRED", "Connexion WebSocket attendue.");
    const t = await testOf(ctx, ctx.params.testId ?? "");
    if (!t || t.status !== "open") throw Errors.notFound("RAID_TEST_NOT_FOUND", "Test introuvable.");
    if (t.admin_id !== me) {
        const inv = await ctx.env.DB.prepare("SELECT status FROM raid_test_invitations WHERE test_id = ?1 AND player_id = ?2")
            .bind(t.id, me)
            .first<{ status: string }>();
        if (inv?.status !== "accepted") throw Errors.notFound("RAID_TEST_NOT_FOUND", "Test introuvable.");
    } else if (auth.player.role !== "admin" || auth.player.account_status !== "active") {
        throw Errors.notFound("RAID_TEST_NOT_FOUND", "Test introuvable.");
    }
    const headers = new Headers(ctx.request.headers);
    headers.set("x-player-id", me);
    headers.set("x-player-name", auth.player.username);
    headers.delete("x-raid-watch");
    const res = await stub(ctx.env, t.id).fetch(new Request("https://coop/socket", { headers }));
    if (res.status === 403 || res.status === 404) throw Errors.notFound("RAID_TEST_NOT_FOUND", "Test introuvable.");
    return res;
}

/** Whether `id` names an administrator's test (raidZones.ts refuses any report naming one). */
export async function isTest(db: D1Database, id: string): Promise<boolean> {
    try {
        return !!(await db.prepare("SELECT 1 AS x FROM raid_tests WHERE id = ?1").bind(id).first());
    } catch {
        return false; // before migration 0195
    }
}
