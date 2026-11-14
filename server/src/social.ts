// Social: private conversations between friends, group discussions, unread counts for the
// game's notifications, blocks and reports. A player only ever sees the conversations they
// belong to; the server always identifies the player from the session.

import { audit, requireAuth } from "./auth";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
export const MAX_GROUP_MEMBERS = 20;
export const MAX_MESSAGE_LENGTH = 500;

interface ConversationRow {
    id: string;
    kind: "direct" | "group";
    name: string | null;
    direct_key: string | null;
    created_by: string | null;
    created_at: string;
    last_message_at: string | null;
}

interface MemberRow {
    role: "owner" | "member";
    last_read_at: string | null;
}

/** Message text: no control characters (line breaks kept), trimmed, at most 3 blank lines. */
export function cleanMessage(raw: string): string {
    return raw
        .replace(/\r\n?/g, "\n")
        .replace(/\t/g, " ")
        // eslint-disable-next-line no-control-regex
        .replace(/[\u0000-\u0009\u000b-\u001f\u007f-\u009f​-‏‪-‮⁦-⁩]/g, "")
        .replace(/\n{4,}/g, "\n\n\n")
        .trim();
}

async function areFriends(ctx: Ctx, a: string, b: string): Promise<boolean> {
    const r = await ctx.env.DB.prepare("SELECT 1 AS x FROM player_friends WHERE player_id = ?1 AND friend_id = ?2 AND status = 'accepted'")
        .bind(a, b)
        .first();
    return r !== null;
}

async function blockedBetween(ctx: Ctx, a: string, b: string): Promise<boolean> {
    const r = await ctx.env.DB.prepare(
        "SELECT 1 AS x FROM player_blocks WHERE (player_id = ?1 AND blocked_id = ?2) OR (player_id = ?2 AND blocked_id = ?1)",
    )
        .bind(a, b)
        .first();
    return r !== null;
}

async function activePlayer(ctx: Ctx, id: string): Promise<PlayerRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const p = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(id).first<PlayerRow>();
    if (!p || p.account_status !== "active") throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    return p;
}

async function conversationFor(ctx: Ctx, id: string, me: string): Promise<{ conv: ConversationRow; member: MemberRow }> {
    if (!ID_RE.test(id)) throw Errors.notFound("CONVERSATION_NOT_FOUND", "Conversation introuvable.");
    const row = await ctx.env.DB.prepare(
        `SELECT c.*, m.role AS m_role, m.last_read_at AS m_last_read_at FROM conversations c
         JOIN conversation_members m ON m.conversation_id = c.id AND m.player_id = ?2 WHERE c.id = ?1`,
    )
        .bind(id, me)
        .first<ConversationRow & { m_role: MemberRow["role"]; m_last_read_at: string | null }>();
    if (!row) throw Errors.notFound("CONVERSATION_NOT_FOUND", "Conversation introuvable.");
    return { conv: row, member: { role: row.m_role, last_read_at: row.m_last_read_at } };
}

async function membersOf(ctx: Ctx, ids: string[]): Promise<Map<string, Array<ReturnType<typeof playerSummary> & { role: string }>>> {
    const out = new Map<string, Array<ReturnType<typeof playerSummary> & { role: string }>>();
    if (ids.length === 0) return out;
    const placeholders = ids.map((_, i) => `?${i + 1}`).join(",");
    const rows = await ctx.env.DB.prepare(
        `SELECT cm.conversation_id, cm.role, p.* FROM conversation_members cm JOIN players p ON p.id = cm.player_id
         WHERE cm.conversation_id IN (${placeholders}) ORDER BY cm.joined_at`,
    )
        .bind(...ids)
        .all<PlayerRow & { conversation_id: string; role: string }>();
    for (const r of rows.results) {
        const list = out.get(r.conversation_id) ?? [];
        list.push({ ...playerSummary(r), role: r.role });
        out.set(r.conversation_id, list);
    }
    return out;
}

function conversationJson(
    c: ConversationRow,
    me: string,
    members: Array<ReturnType<typeof playerSummary> & { role: string }>,
    extra: Record<string, unknown> = {},
) {
    const others = members.filter((m) => m.id !== me);
    return {
        id: c.id,
        kind: c.kind,
        // A private conversation is named after the other player.
        name: c.kind === "group" ? c.name : (others[0]?.username ?? "Joueur supprimé"),
        members,
        created_at: c.created_at,
        last_message_at: c.last_message_at,
        ...extra,
    };
}

/** Unread messages of a member, blocked senders excluded. */
const UNREAD_SQL = `(SELECT COUNT(*) FROM messages x WHERE x.conversation_id = c.id AND x.sender_id <> ?1 AND x.deleted_at IS NULL
    AND (m.last_read_at IS NULL OR x.created_at > m.last_read_at)
    AND x.sender_id NOT IN (SELECT blocked_id FROM player_blocks WHERE player_id = ?1))`;

// ------------------------------------------------------------------- routes

/** GET /social/conversations */
export async function listConversations(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const rows = await ctx.env.DB.prepare(
        `SELECT c.*, m.role AS m_role, ${UNREAD_SQL} AS unread,
           (SELECT x.id || char(31) || COALESCE(x.sender_id, '') || char(31) || x.created_at || char(31) || substr(x.body, 1, 120)
              FROM messages x WHERE x.conversation_id = c.id AND x.deleted_at IS NULL
                AND x.sender_id NOT IN (SELECT blocked_id FROM player_blocks WHERE player_id = ?1)
              ORDER BY x.created_at DESC LIMIT 1) AS last
         FROM conversations c JOIN conversation_members m ON m.conversation_id = c.id AND m.player_id = ?1
         ORDER BY COALESCE(c.last_message_at, c.created_at) DESC LIMIT 100`,
    )
        .bind(me)
        .all<ConversationRow & { m_role: string; unread: number; last: string | null }>();
    const members = await membersOf(ctx, rows.results.map((r) => r.id));
    let total = 0;
    const conversations = rows.results.map((r) => {
        total += r.unread;
        const list = members.get(r.id) ?? [];
        let last = null;
        if (r.last) {
            const [id, senderId, createdAt, body] = r.last.split(String.fromCharCode(31));
            const sender = list.find((m) => m.id === senderId);
            last = { id, sender_id: senderId, sender: sender?.username ?? null, created_at: createdAt, body };
        }
        return conversationJson(r, me, list, { role: r.m_role, unread: r.unread, last_message: last });
    });
    return json(ctx, { conversations, unread_total: total });
}

/** GET /social/unread: what the game polls (counts and the newest unread messages). */
export async function unread(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const total = await ctx.env.DB.prepare(
        `SELECT COALESCE(SUM(${UNREAD_SQL}), 0) AS n FROM conversations c
         JOIN conversation_members m ON m.conversation_id = c.id AND m.player_id = ?1`,
    )
        .bind(me)
        .first<{ n: number }>();
    const latest = await ctx.env.DB.prepare(
        `SELECT x.id, x.conversation_id, x.created_at, substr(x.body, 1, 120) AS body, c.kind, c.name, p.username AS sender
         FROM messages x JOIN conversations c ON c.id = x.conversation_id
         JOIN conversation_members m ON m.conversation_id = c.id AND m.player_id = ?1
         JOIN players p ON p.id = x.sender_id
         WHERE x.sender_id <> ?1 AND x.deleted_at IS NULL AND (m.last_read_at IS NULL OR x.created_at > m.last_read_at)
           AND x.sender_id NOT IN (SELECT blocked_id FROM player_blocks WHERE player_id = ?1)
         ORDER BY x.created_at DESC LIMIT 5`,
    )
        .bind(me)
        .all<{ id: string; conversation_id: string; created_at: string; body: string; kind: string; name: string | null; sender: string }>();
    return json(ctx, { total: total?.n ?? 0, latest: latest.results });
}

/** POST /social/direct {player_id}: the private conversation with a friend (created once). */
export async function openDirect(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id"]);
    const other = v.str("player_id", { min: 36, max: 36, pattern: ID_RE });
    v.done();
    if (other === me) throw Errors.validation([{ field: "player_id", message: "conversation avec vous-même impossible" }]);
    const p = await activePlayer(ctx, other!);
    if (!(await areFriends(ctx, me, p.id))) throw Errors.forbidden("NOT_FRIENDS", "Vous ne pouvez écrire qu'à vos amis.");
    if (await blockedBetween(ctx, me, p.id)) throw Errors.forbidden("BLOCKED", "Conversation impossible avec ce joueur.");
    const key = [me, p.id].sort().join("|");
    let conv = await ctx.env.DB.prepare("SELECT * FROM conversations WHERE direct_key = ?1").bind(key).first<ConversationRow>();
    if (!conv) {
        const id = crypto.randomUUID();
        const now = nowIso();
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(
                "INSERT OR IGNORE INTO conversations (id, kind, direct_key, created_by, created_at) VALUES (?1, 'direct', ?2, ?3, ?4)",
            ).bind(id, key, me, now),
            ctx.env.DB.prepare(
                `INSERT OR IGNORE INTO conversation_members (conversation_id, player_id, role, joined_at)
                 SELECT id, ?2, 'member', ?3 FROM conversations WHERE direct_key = ?1`,
            ).bind(key, me, now),
            ctx.env.DB.prepare(
                `INSERT OR IGNORE INTO conversation_members (conversation_id, player_id, role, joined_at)
                 SELECT id, ?2, 'member', ?3 FROM conversations WHERE direct_key = ?1`,
            ).bind(key, p.id, now),
        ]);
        conv = await ctx.env.DB.prepare("SELECT * FROM conversations WHERE direct_key = ?1").bind(key).first<ConversationRow>();
    }
    const members = await membersOf(ctx, [conv!.id]);
    return json(ctx, { conversation: conversationJson(conv!, me, members.get(conv!.id) ?? []) });
}

/** POST /social/groups {name, member_ids}: a group with friends of the creator. */
export async function createGroup(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.socialGroup, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["name", "member_ids"]);
    const name = v.str("name", { min: 1, max: 40, trim: true });
    const raw = body.member_ids;
    if (!Array.isArray(raw) || raw.length < 1 || raw.length > MAX_GROUP_MEMBERS - 1 || !raw.every((x) => typeof x === "string" && ID_RE.test(x)))
        v.fail("member_ids", `1 à ${MAX_GROUP_MEMBERS - 1} identifiants de joueurs`);
    v.done();
    const ids = [...new Set(raw as string[])].filter((id) => id !== me);
    if (ids.length === 0) throw Errors.validation([{ field: "member_ids", message: "au moins un ami" }]);
    for (const id of ids) {
        await activePlayer(ctx, id);
        if (!(await areFriends(ctx, me, id))) throw Errors.forbidden("NOT_FRIENDS", "Un groupe ne réunit que vos amis.");
        if (await blockedBetween(ctx, me, id)) throw Errors.forbidden("BLOCKED", "Un des joueurs ne peut pas être ajouté.");
    }
    const id = crypto.randomUUID();
    const now = nowIso();
    const stmts = [
        ctx.env.DB.prepare("INSERT INTO conversations (id, kind, name, created_by, created_at) VALUES (?1, 'group', ?2, ?3, ?4)").bind(
            id,
            name!,
            me,
            now,
        ),
        ctx.env.DB.prepare("INSERT INTO conversation_members (conversation_id, player_id, role, joined_at, last_read_at) VALUES (?1, ?2, 'owner', ?3, ?3)").bind(
            id,
            me,
            now,
        ),
        ...ids.map((pid) =>
            ctx.env.DB.prepare("INSERT INTO conversation_members (conversation_id, player_id, role, joined_at) VALUES (?1, ?2, 'member', ?3)").bind(
                id,
                pid,
                now,
            ),
        ),
    ];
    await ctx.env.DB.batch(stmts);
    await audit(ctx, me, "group_created", { conversation_id: id, members: ids.length + 1 });
    const conv = await ctx.env.DB.prepare("SELECT * FROM conversations WHERE id = ?1").bind(id).first<ConversationRow>();
    const members = await membersOf(ctx, [id]);
    return json(ctx, { conversation: conversationJson(conv!, me, members.get(id) ?? []) }, { status: 201 });
}

/** PATCH /social/groups/{id} {name}: the owner renames the group. */
export async function renameGroup(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const { conv, member } = await conversationFor(ctx, ctx.params.conversationId!, me);
    if (conv.kind !== "group") throw Errors.conflict("NOT_A_GROUP", "Cette conversation n'est pas un groupe.");
    if (member.role !== "owner") throw Errors.forbidden("NOT_OWNER", "Seul le créateur du groupe peut le renommer.");
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["name"]);
    const name = v.str("name", { min: 1, max: 40, trim: true });
    v.done();
    await ctx.env.DB.prepare("UPDATE conversations SET name = ?2 WHERE id = ?1").bind(conv.id, name!).run();
    return json(ctx, { id: conv.id, name });
}

/** POST /social/groups/{id}/members {player_id}: the owner adds one of their friends. */
export async function addMember(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const { conv, member } = await conversationFor(ctx, ctx.params.conversationId!, me);
    if (conv.kind !== "group") throw Errors.conflict("NOT_A_GROUP", "Cette conversation n'est pas un groupe.");
    if (member.role !== "owner") throw Errors.forbidden("NOT_OWNER", "Seul le créateur du groupe peut ajouter des membres.");
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id"]);
    const pid = v.str("player_id", { min: 36, max: 36, pattern: ID_RE });
    v.done();
    const p = await activePlayer(ctx, pid!);
    if (!(await areFriends(ctx, me, p.id))) throw Errors.forbidden("NOT_FRIENDS", "Vous ne pouvez ajouter que vos amis.");
    if (await blockedBetween(ctx, me, p.id)) throw Errors.forbidden("BLOCKED", "Ce joueur ne peut pas être ajouté.");
    const count = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM conversation_members WHERE conversation_id = ?1").bind(conv.id).first<{ n: number }>();
    if ((count?.n ?? 0) >= MAX_GROUP_MEMBERS) throw Errors.conflict("GROUP_FULL", `Un groupe compte au plus ${MAX_GROUP_MEMBERS} membres.`);
    await ctx.env.DB.prepare(
        "INSERT OR IGNORE INTO conversation_members (conversation_id, player_id, role, joined_at) VALUES (?1, ?2, 'member', ?3)",
    )
        .bind(conv.id, p.id, nowIso())
        .run();
    const members = await membersOf(ctx, [conv.id]);
    return json(ctx, { conversation: conversationJson(conv, me, members.get(conv.id) ?? []) });
}

/**
 * DELETE /social/groups/{id}/members/{playerId}: leave the group (yourself) or, for the owner,
 * remove a member. The owner who leaves hands the group to the oldest member; an empty group
 * is deleted.
 */
export async function removeMember(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const { conv, member } = await conversationFor(ctx, ctx.params.conversationId!, me);
    if (conv.kind !== "group") throw Errors.conflict("NOT_A_GROUP", "Cette conversation n'est pas un groupe.");
    const target = ctx.params.playerId!;
    if (target !== me && member.role !== "owner") throw Errors.forbidden("NOT_OWNER", "Seul le créateur du groupe peut retirer un membre.");
    const res = await ctx.env.DB.prepare("DELETE FROM conversation_members WHERE conversation_id = ?1 AND player_id = ?2").bind(conv.id, target).run();
    if (!res.meta.changes) throw Errors.notFound("MEMBER_NOT_FOUND", "Ce joueur n'est pas dans le groupe.");
    const left = await ctx.env.DB.prepare("SELECT player_id, role FROM conversation_members WHERE conversation_id = ?1 ORDER BY joined_at").bind(conv.id).all<{
        player_id: string;
        role: string;
    }>();
    if (left.results.length === 0) {
        await ctx.env.DB.prepare("DELETE FROM conversations WHERE id = ?1").bind(conv.id).run();
        return json(ctx, { removed: true, group_deleted: true });
    }
    if (!left.results.some((r) => r.role === "owner"))
        await ctx.env.DB.prepare("UPDATE conversation_members SET role = 'owner' WHERE conversation_id = ?1 AND player_id = ?2")
            .bind(conv.id, left.results[0]!.player_id)
            .run();
    return json(ctx, { removed: true, group_deleted: false });
}

/** GET /social/conversations/{id}/messages?before=<ISO date>&limit=<1..100> */
export async function listMessages(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const { conv } = await conversationFor(ctx, ctx.params.conversationId!, me);
    const limit = Math.min(100, Math.max(1, Number(ctx.url.searchParams.get("limit") ?? "50") || 50));
    const before = ctx.url.searchParams.get("before");
    if (before !== null && !/^\d{4}-\d{2}-\d{2}T[\d:.]+Z$/.test(before))
        throw Errors.validation([{ field: "before", message: "date ISO attendue" }]);
    const rows = await ctx.env.DB.prepare(
        `SELECT x.id, x.sender_id, x.body, x.created_at, x.deleted_at, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility, p.id AS pid
         FROM messages x JOIN players p ON p.id = x.sender_id
         WHERE x.conversation_id = ?1 AND (?2 IS NULL OR x.created_at < ?2)
           AND x.sender_id NOT IN (SELECT blocked_id FROM player_blocks WHERE player_id = ?3)
         ORDER BY x.created_at DESC LIMIT ?4`,
    )
        .bind(conv.id, before, me, limit + 1)
        .all<{ id: string; sender_id: string; body: string; created_at: string; deleted_at: string | null } & PlayerRow & { pid: string }>();
    const more = rows.results.length > limit;
    const messages = rows.results
        .slice(0, limit)
        .reverse()
        .map((r) => ({
            id: r.id,
            sender: playerSummary({ ...r, id: r.pid }),
            mine: r.sender_id === me,
            body: r.deleted_at ? "" : r.body,
            deleted: r.deleted_at !== null,
            created_at: r.created_at,
        }));
    return json(ctx, { conversation_id: conv.id, messages, has_more: more });
}

/** POST /social/conversations/{id}/messages {body} */
export async function sendMessage(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.socialMessage, "player:" + me);
    const { conv } = await conversationFor(ctx, ctx.params.conversationId!, me);
    const body = await readJsonObject(ctx, 4 * 1024);
    const v = new Validator(body, ["body"]);
    // Not v.str(): line breaks are legitimate in a message (cleanMessage strips the rest).
    const raw = body.body;
    if (typeof raw !== "string" || raw.length > MAX_MESSAGE_LENGTH * 2) v.fail("body", `1 à ${MAX_MESSAGE_LENGTH} caractères`);
    v.done();
    const text = cleanMessage(raw as string);
    if (text.length < 1 || text.length > MAX_MESSAGE_LENGTH)
        throw Errors.validation([{ field: "body", message: `1 à ${MAX_MESSAGE_LENGTH} caractères` }]);
    if (conv.kind === "direct") {
        const other = (conv.direct_key ?? "").split("|").find((id) => id !== me) ?? "";
        if (!(await areFriends(ctx, me, other))) throw Errors.forbidden("NOT_FRIENDS", "Vous n'êtes plus amis avec ce joueur.");
        if (await blockedBetween(ctx, me, other)) throw Errors.forbidden("BLOCKED", "Conversation impossible avec ce joueur.");
    }
    const id = crypto.randomUUID();
    const now = nowIso();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("INSERT INTO messages (id, conversation_id, sender_id, body, created_at) VALUES (?1, ?2, ?3, ?4, ?5)").bind(id, conv.id, me, text, now),
        ctx.env.DB.prepare("UPDATE conversations SET last_message_at = ?2 WHERE id = ?1").bind(conv.id, now),
        ctx.env.DB.prepare("UPDATE conversation_members SET last_read_at = ?3 WHERE conversation_id = ?1 AND player_id = ?2").bind(conv.id, me, now),
    ]);
    return json(ctx, { message: { id, body: text, created_at: now, mine: true } }, { status: 201 });
}

/** POST /social/conversations/{id}/read: everything up to now is read. */
export async function markRead(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const { conv } = await conversationFor(ctx, ctx.params.conversationId!, me);
    const now = nowIso();
    await ctx.env.DB.prepare("UPDATE conversation_members SET last_read_at = ?3 WHERE conversation_id = ?1 AND player_id = ?2").bind(conv.id, me, now).run();
    return json(ctx, { read_at: now });
}

/** DELETE /social/messages/{id}: the sender removes their message (its text is erased). */
export async function deleteMessage(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = ctx.params.messageId!;
    if (!ID_RE.test(id)) throw Errors.notFound("MESSAGE_NOT_FOUND", "Message introuvable.");
    const res = await ctx.env.DB.prepare("UPDATE messages SET body = '', deleted_at = ?3 WHERE id = ?1 AND sender_id = ?2 AND deleted_at IS NULL")
        .bind(id, auth.player.id, nowIso())
        .run();
    if (!res.meta.changes) throw Errors.notFound("MESSAGE_NOT_FOUND", "Message introuvable.");
    return json(ctx, { deleted: true });
}

/** POST /social/messages/{id}/report {reason}: reported to the moderators (audit log + table). */
export async function reportMessage(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.socialReport, "player:" + me);
    const id = ctx.params.messageId!;
    if (!ID_RE.test(id)) throw Errors.notFound("MESSAGE_NOT_FOUND", "Message introuvable.");
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["reason"]);
    const reason = v.str("reason", { max: 200, optional: true, trim: true });
    v.done();
    const msg = await ctx.env.DB.prepare(
        `SELECT x.id, x.sender_id FROM messages x JOIN conversation_members m ON m.conversation_id = x.conversation_id AND m.player_id = ?2
         WHERE x.id = ?1`,
    )
        .bind(id, me)
        .first<{ id: string; sender_id: string }>();
    if (!msg) throw Errors.notFound("MESSAGE_NOT_FOUND", "Message introuvable.");
    if (msg.sender_id === me) throw Errors.validation([{ field: "message", message: "vous ne pouvez pas signaler votre propre message" }]);
    await ctx.env.DB.prepare("INSERT OR IGNORE INTO message_reports (id, message_id, reporter_id, reason, created_at) VALUES (?1, ?2, ?3, ?4, ?5)")
        .bind(crypto.randomUUID(), id, me, reason ?? null, nowIso())
        .run();
    await audit(ctx, me, "message_reported", { message_id: id, sender_id: msg.sender_id });
    return json(ctx, { reported: true }, { status: 201 });
}

/** GET /social/blocks */
export async function listBlocks(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const rows = await ctx.env.DB.prepare(
        "SELECT p.*, b.created_at AS blocked_at FROM player_blocks b JOIN players p ON p.id = b.blocked_id WHERE b.player_id = ?1 ORDER BY b.created_at DESC",
    )
        .bind(auth.player.id)
        .all<PlayerRow & { blocked_at: string }>();
    return json(ctx, { blocked: rows.results.map((r) => ({ player: playerSummary(r), since: r.blocked_at })) });
}

/** POST /social/blocks {player_id}: blocks a player (and ends the friendship). */
export async function blockPlayer(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.socialBlock, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id"]);
    const pid = v.str("player_id", { min: 36, max: 36, pattern: ID_RE });
    v.done();
    if (pid === me) throw Errors.validation([{ field: "player_id", message: "vous ne pouvez pas vous bloquer vous-même" }]);
    const p = await activePlayer(ctx, pid!);
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("DELETE FROM player_follows WHERE (follower_id = ?1 AND creator_id = ?2) OR (follower_id = ?2 AND creator_id = ?1)").bind(me, p.id),
        ctx.env.DB.prepare("INSERT OR IGNORE INTO player_blocks (player_id, blocked_id, created_at) VALUES (?1, ?2, ?3)").bind(me, p.id, nowIso()),
        ctx.env.DB.prepare("DELETE FROM player_friends WHERE (player_id = ?1 AND friend_id = ?2) OR (player_id = ?2 AND friend_id = ?1)").bind(me, p.id),
    ]);
    await audit(ctx, me, "player_blocked", { blocked_id: p.id });
    return json(ctx, { blocked: true, player: playerSummary(p) }, { status: 201 });
}

/** DELETE /social/blocks/{playerId} */
export async function unblockPlayer(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const pid = ctx.params.playerId!;
    if (!ID_RE.test(pid)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const res = await ctx.env.DB.prepare("DELETE FROM player_blocks WHERE player_id = ?1 AND blocked_id = ?2").bind(auth.player.id, pid).run();
    if (!res.meta.changes) throw Errors.notFound("BLOCK_NOT_FOUND", "Ce joueur n'est pas bloqué.");
    return json(ctx, { unblocked: true });
}
