// Player profiles, privacy settings, statistics, search, GDPR export and account deletion,
// custom avatars.

import { audit, countryExists, requireAuth, type AuthInfo } from "./auth";
import { verifyPassword } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { playerSummary, privateProfile, recordJson, visibleCountry, type PlayerRow, type RecordRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import {
    AVATAR_RE,
    CATEGORIES,
    COUNTRY_RE,
    LEVEL_ID_RE,
    LOCATION_VISIBILITY,
    PROFILE_VISIBILITY,
    REGION_RE,
    USERNAME_RE,
    Validator,
    isReservedUsername,
    likePrefix,
    normalizeUsername,
    paging,
    queryEnum,
    queryString,
} from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

async function loadPlayer(ctx: Ctx, id: string): Promise<PlayerRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const p = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(id).first<PlayerRow>();
    if (!p || p.account_status !== "active") throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    return p;
}

function isSelf(viewer: AuthInfo | null, id: string): boolean {
    return viewer?.player.id === id;
}

async function statistics(ctx: Ctx, playerId: string) {
    const counts = await ctx.env.DB.prepare(
        `SELECT
           (SELECT COUNT(*) FROM records WHERE player_id = ?1 AND verification_status = 'VERIFIED') AS verified_records,
           (SELECT COUNT(*) FROM records WHERE player_id = ?1) AS submitted_records,
           (SELECT COUNT(*) FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash WHERE b.player_id = ?1) AS personal_bests,
           (SELECT COUNT(DISTINCT b.level_id) FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash
              WHERE b.player_id = ?1 AND b.category = 'ANY_PERCENT') AS levels_completed`,
    )
        .bind(playerId)
        .first<{ verified_records: number; submitted_records: number; personal_bests: number; levels_completed: number }>();
    // First places on current revisions (ties broken by the earliest record).
    const first = await ctx.env.DB.prepare(
        `SELECT COUNT(*) AS n FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash
         WHERE b.player_id = ?1 AND NOT EXISTS (
           SELECT 1 FROM player_bests o JOIN players op ON op.id = o.player_id
           WHERE o.level_hash = b.level_hash AND o.category = b.category AND op.account_status = 'active' AND o.player_id <> b.player_id
             AND (o.time_ms < b.time_ms OR (o.time_ms = b.time_ms AND o.achieved_at < b.achieved_at)))`,
    )
        .bind(playerId)
        .first<{ n: number }>();
    const bests = await ctx.env.DB.prepare(
        `SELECT b.level_id, l.name AS level_name, l.planet_id, b.category, b.time_ms, b.record_id, b.achieved_at,
           (SELECT COUNT(*) FROM player_bests o JOIN players op ON op.id = o.player_id
             WHERE o.level_hash = b.level_hash AND o.category = b.category AND op.account_status = 'active'
               AND (o.time_ms < b.time_ms OR (o.time_ms = b.time_ms AND (o.achieved_at < b.achieved_at
                    OR (o.achieved_at = b.achieved_at AND o.player_id < b.player_id))))) + 1 AS rank
         FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash
         WHERE b.player_id = ?1 ORDER BY l.planet_id, l.level_order, b.category`,
    )
        .bind(playerId)
        .all<{ level_id: string; level_name: string; planet_id: string; category: string; time_ms: number; record_id: string; achieved_at: string; rank: number }>();
    const planets = await ctx.env.DB.prepare(
        `SELECT l.planet_id, MAX(l.planet_name) AS planet_name, COUNT(*) AS levels,
                SUM(CASE WHEN EXISTS (SELECT 1 FROM player_bests b WHERE b.player_id = ?1 AND b.level_hash = l.content_hash
                                       AND b.category = 'ANY_PERCENT') THEN 1 ELSE 0 END) AS done
         FROM levels l WHERE l.ranking_enabled = 1 GROUP BY l.planet_id ORDER BY l.planet_id`,
    )
        .bind(playerId)
        .all<{ planet_id: string; planet_name: string; levels: number; done: number }>();
    return {
        verified_records: counts?.verified_records ?? 0,
        submitted_records: counts?.submitted_records ?? 0,
        personal_bests: counts?.personal_bests ?? 0,
        levels_completed: counts?.levels_completed ?? 0,
        first_places: first?.n ?? 0,
        planets: planets.results.map((p) => ({ planet_id: p.planet_id, planet_name: p.planet_name, levels: p.levels, completed_levels: p.done, completed: p.done === p.levels })),
        planets_completed: planets.results.filter((p) => p.done === p.levels).length,
        best_times: bests.results,
    };
}

/** GET /players/{id}: public profile (reduced when the profile is private). */
export async function getPlayer(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    if (isSelf(viewer, p.id)) return json(ctx, { player: { ...privateProfile(p), hidden: false }, statistics: await statistics(ctx, p.id) });
    if (p.profile_visibility === "private")
        return json(ctx, { player: { ...playerSummary(p), hidden: true }, statistics: null });
    let friendship: string | null = null;
    if (viewer) {
        const f = await ctx.env.DB.prepare(
            `SELECT player_id, status FROM player_friends WHERE (player_id = ?1 AND friend_id = ?2) OR (player_id = ?2 AND friend_id = ?1)`,
        )
            .bind(viewer.player.id, p.id)
            .all<{ player_id: string; status: string }>();
        if (f.results.some((r) => r.status === "accepted")) friendship = "friends";
        else if (f.results.some((r) => r.player_id === viewer.player.id)) friendship = "request_sent";
        else if (f.results.length) friendship = "request_received";
    }
    return json(ctx, {
        player: { ...playerSummary(p), region_code: visibleCountry(p) ? p.region_code : null, created_at: p.created_at, hidden: false, friendship },
        statistics: await statistics(ctx, p.id),
    });
}

/** GET /players/{id}/statistics */
export async function getStatistics(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    if (!isSelf(viewer, p.id) && p.profile_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    return json(ctx, { player_id: p.id, statistics: await statistics(ctx, p.id) });
}

/** GET /players/{id}/records: verified records for others, every status for the owner. */
export async function getPlayerRecords(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    const self = isSelf(viewer, p.id);
    if (!self && p.profile_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    const page = paging(ctx.url, 50, 200);
    const levelId = queryString(ctx.url, "level_id", LEVEL_ID_RE);
    const category = ctx.url.searchParams.get("category") ? queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT") : undefined;
    const status = self ? queryString(ctx.url, "status", /^(PENDING|VERIFIED|REJECTED)$/) : "VERIFIED";
    const where = ["player_id = ?1"];
    const binds: unknown[] = [p.id];
    if (levelId) {
        binds.push(levelId);
        where.push(`level_id = ?${binds.length}`);
    }
    if (category) {
        binds.push(category);
        where.push(`category = ?${binds.length}`);
    }
    if (status) {
        binds.push(status);
        where.push(`verification_status = ?${binds.length}`);
    }
    const sql = `SELECT *, COUNT(*) OVER () AS total FROM records WHERE ${where.join(" AND ")} ORDER BY created_at DESC LIMIT ?${binds.length + 1} OFFSET ?${binds.length + 2}`;
    const rows = await ctx.env.DB.prepare(sql).bind(...binds, page.limit, page.offset).all<RecordRow & { total: number }>();
    const total = rows.results[0]?.total ?? 0;
    return json(
        ctx,
        { player_id: p.id, records: rows.results.map((r) => recordJson(r, { owner: self })) },
        { meta: { limit: page.limit, offset: page.offset, total, next_offset: page.offset + page.limit < total ? page.offset + page.limit : null } },
    );
}

/** GET /players/search?q=: pseudonym prefix search (public identities only). */
export async function searchPlayers(ctx: Ctx): Promise<Response> {
    const q = queryString(ctx.url, "q", /^[A-Za-z0-9_.-]{2,20}$/);
    if (!q) throw Errors.validation([{ field: "q", message: "2 caractères minimum" }]);
    const rows = await ctx.env.DB.prepare(
        `SELECT * FROM players WHERE username_normalized LIKE ?1 ESCAPE '\\' AND account_status = 'active'
         ORDER BY length(username_normalized), username_normalized LIMIT 20`,
    )
        .bind(likePrefix(normalizeUsername(q)))
        .all<PlayerRow>();
    return json(ctx, { players: rows.results.map(playerSummary) });
}

/** PATCH /players/me: profile and privacy settings. */
export async function patchMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.profileWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, [
        "username", "avatar", "country_code", "region_code", "location_visibility", "profile_visibility", "allow_friend_requests",
    ]);
    const username = v.str("username", { min: 3, max: 20, pattern: USERNAME_RE, trim: true, optional: true });
    const avatar = v.str("avatar", { max: 16, pattern: AVATAR_RE, optional: true });
    // country_code / region_code accept null to clear them.
    const clearCountry = body.country_code === null;
    const clearRegion = body.region_code === null;
    const country = v.str("country_code", { max: 2, pattern: COUNTRY_RE, optional: true });
    const region = v.str("region_code", { max: 6, pattern: REGION_RE, optional: true });
    const locVis = v.oneOf("location_visibility", LOCATION_VISIBILITY, { optional: true });
    const profVis = v.oneOf("profile_visibility", PROFILE_VISIBILITY, { optional: true });
    const allowFriends = v.bool("allow_friend_requests", { optional: true });
    if (username && isReservedUsername(username)) v.fail("username", "pseudonyme réservé");
    v.done();
    const p = auth.player;
    const next = {
        username: username ?? p.username,
        avatar: avatar ?? p.avatar,
        country: clearCountry ? null : (country ?? p.country_code),
        region: clearRegion || clearCountry ? null : (region ?? p.region_code),
        locVis: locVis ?? p.location_visibility,
        profVis: profVis ?? p.profile_visibility,
        allowFriends: allowFriends === undefined ? p.allow_friend_requests : allowFriends ? 1 : 0,
    };
    // Without a country nothing can be shown: visibility falls back to hidden.
    if (!next.country) next.locVis = "hidden";
    if (next.region && (!next.country || !next.region.startsWith(next.country + "-")))
        throw Errors.validation([{ field: "region_code", message: "la région doit appartenir au pays choisi" }]);
    if (country && !(await countryExists(ctx, country))) throw Errors.validation([{ field: "country_code", message: "pays inconnu" }]);
    try {
        await ctx.env.DB.prepare(
            `UPDATE players SET username = ?1, username_normalized = ?2, avatar = ?3, country_code = ?4, region_code = ?5,
               location_visibility = ?6, profile_visibility = ?7, allow_friend_requests = ?8 WHERE id = ?9`,
        )
            .bind(next.username, normalizeUsername(next.username), next.avatar, next.country, next.region, next.locVis, next.profVis, next.allowFriends, p.id)
            .run();
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("USERNAME_TAKEN", "Ce pseudonyme est déjà utilisé.");
        throw e;
    }
    if (avatar && p.avatar === "custom" && p.avatar_custom_key) {
        await ctx.env.STORAGE.delete(p.avatar_custom_key);
        await ctx.env.DB.prepare("UPDATE players SET avatar_custom_key = NULL WHERE id = ?1").bind(p.id).run();
    }
    const changed = Object.keys(body);
    if (username && username !== p.username) await audit(ctx, p.id, "username_changed", { from: p.username, to: username });
    if (changed.some((k) => k.endsWith("visibility") || k.startsWith("country") || k.startsWith("region")))
        await audit(ctx, p.id, "privacy_changed", { fields: changed });
    const fresh = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(p.id).first<PlayerRow>();
    return json(ctx, { player: privateProfile(fresh!) });
}

// -------------------------------------------------------------------- GDPR

/** GET /players/me/export: every personal data item stored about the player. */
export async function exportMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = auth.player.id;
    const [sessions, records, friends, replays, auditRows, conversations, messages, blocks] = await ctx.env.DB.batch([
        ctx.env.DB.prepare("SELECT id, created_at, last_used_at, expires_at, refresh_expires_at, revoked_at, client_info FROM player_sessions WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT * FROM records WHERE player_id = ?1 ORDER BY created_at").bind(id),
        ctx.env.DB.prepare("SELECT player_id, friend_id, status, created_at FROM player_friends WHERE player_id = ?1 OR friend_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT id, record_id, attempt_id, file_size, sha256, format_version, frame_count, duration_ms, verification_status, created_at FROM replay_metadata WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT action, details, created_at FROM audit_log WHERE player_id = ?1 ORDER BY created_at").bind(id),
        ctx.env.DB.prepare(
            `SELECT c.id, c.kind, c.name, m.role, m.joined_at, m.last_read_at FROM conversation_members m
             JOIN conversations c ON c.id = m.conversation_id WHERE m.player_id = ?1 ORDER BY m.joined_at`,
        ).bind(id),
        ctx.env.DB.prepare("SELECT id, conversation_id, body, created_at, deleted_at FROM messages WHERE sender_id = ?1 ORDER BY created_at").bind(id),
        ctx.env.DB.prepare("SELECT blocked_id, created_at FROM player_blocks WHERE player_id = ?1").bind(id),
    ]);
    await audit(ctx, id, "data_export");
    return json(ctx, {
        exported_at: nowIso(),
        player: privateProfile(auth.player),
        sessions: sessions?.results ?? [],
        records: records?.results ?? [],
        friends: friends?.results ?? [],
        replays: replays?.results ?? [],
        audit_log: auditRows?.results ?? [],
        conversations: conversations?.results ?? [],
        messages_sent: messages?.results ?? [],
        blocked_players: blocks?.results ?? [],
        notes: "Le mot de passe n'est stocké que sous forme d'empreinte PBKDF2 et n'est pas exportable. Les adresses IP ne sont jamais stockées en clair.",
    });
}

/** DELETE /players/me: deletes the account and every personal data item (password required). */
export async function deleteMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.accountDelete, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["password", "confirm"]);
    const password = v.str("password", { min: 1, max: 128 });
    const confirm = v.str("confirm", { max: 16 });
    if (confirm !== undefined && confirm !== "DELETE") v.fail("confirm", "doit valoir DELETE");
    v.done();
    const cred = await ctx.env.DB.prepare("SELECT password_hash, password_salt, password_iterations FROM player_credentials WHERE player_id = ?1")
        .bind(auth.player.id)
        .first<{ password_hash: string; password_salt: string; password_iterations: number }>();
    if (!cred || !(await verifyPassword(password!, { hash: cred.password_hash, salt: cred.password_salt, iterations: cred.password_iterations })))
        throw new ApiError(401, "INVALID_CREDENTIALS", "Mot de passe incorrect.");
    const id = auth.player.id;
    // Files first (R2), then rows (D1 cascades sessions, records, bests, replays, friends).
    const keys = await ctx.env.DB.prepare("SELECT storage_key FROM replay_metadata WHERE player_id = ?1").bind(id).all<{ storage_key: string }>();
    const toDelete = keys.results.map((k) => k.storage_key);
    if (auth.player.avatar_custom_key) toDelete.push(auth.player.avatar_custom_key);
    for (let i = 0; i < toDelete.length; i += 500) await ctx.env.STORAGE.delete(toDelete.slice(i, i + 500));
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("DELETE FROM audit_log WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("DELETE FROM players WHERE id = ?1").bind(id),
        // Groups the player owned go to their oldest member; emptied conversations disappear.
        ctx.env.DB.prepare(
            `UPDATE conversation_members SET role = 'owner' WHERE rowid IN (
               SELECT (SELECT m2.rowid FROM conversation_members m2 WHERE m2.conversation_id = c.id ORDER BY m2.joined_at LIMIT 1)
               FROM conversations c WHERE c.kind = 'group'
                 AND NOT EXISTS (SELECT 1 FROM conversation_members m3 WHERE m3.conversation_id = c.id AND m3.role = 'owner'))`,
        ),
        ctx.env.DB.prepare("DELETE FROM conversations WHERE NOT EXISTS (SELECT 1 FROM conversation_members m WHERE m.conversation_id = conversations.id)"),
    ]);
    // A trace without any personal identifier.
    await audit(ctx, null, "account_deleted");
    return json(ctx, { deleted: true });
}

// ------------------------------------------------------------------ avatars

const MAX_AVATAR_BYTES = 64 * 1024;

/** Strict PNG check: signature, IHDR first, square 32-256 px, 8-bit, IEND present. */
export function checkAvatarPng(data: Uint8Array): string | null {
    const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
    if (data.byteLength < 45 || sig.some((b, i) => data[i] !== b)) return "fichier PNG attendu";
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    if (view.getUint32(8) !== 13 || String.fromCharCode(...data.subarray(12, 16)) !== "IHDR") return "en-tête PNG invalide";
    const w = view.getUint32(16);
    const h = view.getUint32(20);
    if (w !== h || w < 32 || w > 256) return "image carrée de 32 à 256 pixels attendue";
    if (data[24] !== 8) return "profondeur 8 bits attendue";
    // Walk the chunks: lengths must be consistent and the file must end with IEND.
    let at = 8;
    let sawEnd = false;
    while (at + 12 <= data.byteLength) {
        const len = view.getUint32(at);
        const type = String.fromCharCode(...data.subarray(at + 4, at + 8));
        if (!/^[A-Za-z]{4}$/.test(type) || at + 12 + len > data.byteLength) return "structure PNG invalide";
        at += 12 + len;
        if (type === "IEND") {
            sawEnd = true;
            break;
        }
    }
    if (!sawEnd || at !== data.byteLength) return "structure PNG invalide";
    return null;
}

export async function putAvatar(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.profileWrite, "player:" + auth.player.id);
    if (!(ctx.request.headers.get("content-type") ?? "").toLowerCase().startsWith("image/png"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Avatar PNG attendu (image/png).");
    const data = await readLimited(ctx.request, MAX_AVATAR_BYTES);
    const problem = checkAvatarPng(data);
    if (problem) throw new ApiError(422, "INVALID_IMAGE", `Avatar refusé : ${problem}.`);
    const key = `avatars/${auth.player.id}.png`;
    await ctx.env.STORAGE.put(key, data, { httpMetadata: { contentType: "image/png" } });
    await ctx.env.DB.prepare("UPDATE players SET avatar = 'custom', avatar_custom_key = ?1 WHERE id = ?2").bind(key, auth.player.id).run();
    await audit(ctx, auth.player.id, "avatar_uploaded", { bytes: data.byteLength });
    const fresh = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(auth.player.id).first<PlayerRow>();
    return json(ctx, { player: privateProfile(fresh!) });
}

export async function getAvatar(ctx: Ctx): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    if (p.avatar !== "custom" || !p.avatar_custom_key) throw Errors.notFound("AVATAR_NOT_FOUND", "Pas d'avatar personnalisé.");
    const obj = await ctx.env.STORAGE.get(p.avatar_custom_key);
    if (!obj) throw Errors.notFound("AVATAR_NOT_FOUND", "Pas d'avatar personnalisé.");
    return new Response(obj.body, {
        headers: { "content-type": "image/png", "cache-control": "public, max-age=300", "x-content-type-options": "nosniff" },
    });
}
