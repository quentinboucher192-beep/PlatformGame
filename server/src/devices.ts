// 2.47: an account is private to ONE computer (docs/online/DEVICES.md). The game draws a random
// id for the computer when it is installed (no hardware fingerprint), keeps it in the user
// folder and sends it with the registration and every login. The account is bound to the first
// computer that uses it; the right password typed on another computer is refused, and the
// owner gets a « security » notification. One account per computer. An admin may unbind an
// account (its next login, from any computer, binds it again) or bind it to the computer of a
// refused attempt. The game also sends copies of its progression (the computer's saves), which
// let an admin, and later a migration, bring everything to a new account.
//
// Never trust the client: the device id is only a hint the game sends (a copied id file would
// pass), never a secret; the session, the password and the binding are the server's.

import { audit, requireAuth } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import type { PlayerRow } from "./model";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { SEMVER_RE, Validator } from "./validate";

export interface DeviceInfo {
    id: string;
    name: string;
}

export const DEVICE_ID_RE = /^[a-f0-9]{32}$/;
const NAME_MAX = 40;
/** At most this many copies of the progression per account (the oldest go). */
export const PROGRESS_COPIES_KEPT = 5;
export const MAX_PROGRESS_COPY_BYTES = 4 * 1024 * 1024;
const PROGRESS_FILES = ["progress", "records", "boss_stats", "achievements", "collection", "segments", "training"] as const;

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

function cleanName(raw: unknown): string | null {
    if (typeof raw !== "string") return null;
    const name = raw.replace(/\s+/g, " ").trim();
    if (name.length < 1 || name.length > NAME_MAX || /[\u0000-\u001f\u007f<>]/.test(name)) return null;
    return name;
}

/**
 * The `device` of a registration or a login: `{ id, name }`. A game older than 2.47 sends none:
 * 426, the game must be updated (its accounts are bound to a computer now).
 */
export function readDevice(raw: unknown): DeviceInfo {
    if (raw === undefined || raw === null)
        throw new ApiError(426, "GAME_VERSION_UNSUPPORTED", "Mettez le jeu à jour : un compte est maintenant lié à un seul ordinateur (version 2.47).");
    const problems: { field: string; message: string }[] = [];
    if (!isObject(raw)) throw Errors.validation([{ field: "device", message: "objet { id, name } attendu" }]);
    const id = typeof raw.id === "string" ? raw.id.toLowerCase() : "";
    if (!DEVICE_ID_RE.test(id)) problems.push({ field: "device.id", message: "identifiant invalide" });
    const name = cleanName(raw.name);
    if (!name) problems.push({ field: "device.name", message: `de 1 à ${NAME_MAX} caractères` });
    if (problems.length) throw Errors.validation(problems);
    return { id, name: name! };
}

function boundElsewhere(): ApiError {
    return new ApiError(403, "ACCOUNT_BOUND_ELSEWHERE", "Ce compte est lié à un autre ordinateur : il ne s'utilise que sur l'ordinateur où il a été créé.");
}

function deviceTaken(): ApiError {
    return Errors.conflict("DEVICE_BOUND", "Cet ordinateur est déjà lié à un compte : un seul compte par ordinateur.");
}

/** The account bound to this computer, other than `playerId`. */
async function otherAccountOf(ctx: Ctx, deviceId: string, playerId: string | null): Promise<string | null> {
    const r = await ctx.env.DB.prepare("SELECT id FROM players WHERE device_id = ?1 AND id <> ?2").bind(deviceId, playerId ?? "").first<{ id: string }>();
    return r?.id ?? null;
}

/** A registration: this computer must not be bound to an account already. */
export async function checkFreeDevice(ctx: Ctx, device: DeviceInfo): Promise<void> {
    if (await otherAccountOf(ctx, device.id, null)) throw deviceTaken();
}

function country(ctx: Ctx): string | null {
    const c = (ctx.request as unknown as { cf?: { country?: unknown } }).cf?.country;
    return typeof c === "string" && /^[A-Z]{2}$/.test(c) ? c : null;
}

/**
 * A login with the right password: the account's computer, or its first one (bound now), or
 * a computer an admin allowed (bound now, the other computer's sessions end). Anything else is
 * refused: written down, and the owner is told (one notification while unread).
 */
export async function bindAtLogin(ctx: Ctx, player: PlayerRow, device: DeviceInfo): Promise<void> {
    if (player.device_id === device.id) return;
    const now = nowIso();
    if (!player.device_id || player.device_rebind === 1) {
        if (await otherAccountOf(ctx, device.id, player.id)) throw deviceTaken();
        const res = await ctx.env.DB.prepare(
            `UPDATE players SET device_id = ?2, device_name = ?3, device_bound_at = ?4, device_rebind = 0
             WHERE id = ?1 AND (device_id IS NULL OR device_rebind = 1)`,
        )
            .bind(player.id, device.id, device.name, now)
            .run()
            .catch((e) => {
                if (String(e).includes("UNIQUE")) throw deviceTaken();
                throw e;
            });
        if (!res.meta.changes) throw boundElsewhere();
        const rebound = player.device_rebind === 1 && !!player.device_id;
        if (rebound)
            await ctx.env.DB.prepare("UPDATE player_sessions SET revoked_at = ?2 WHERE player_id = ?1 AND revoked_at IS NULL").bind(player.id, now).run();
        await audit(ctx, player.id, rebound ? "device_rebound" : "device_bound", { device_name: device.name });
        await notify(ctx, player.id, "security", { data: { reason: rebound ? "device_rebound" : "device_bound", device_name: device.name } });
        return;
    }
    // Several refusals in a row from this computer: it waits (as for wrong passwords).
    await rateLimit(ctx, RULES.loginRefused, "device:" + device.id);
    const where = country(ctx);
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("INSERT INTO login_refusals (player_id, device_id, country, created_at) VALUES (?1, ?2, ?3, ?4)").bind(player.id, device.id, where, now),
        ctx.env.DB.prepare(
            "DELETE FROM login_refusals WHERE player_id = ?1 AND id NOT IN (SELECT id FROM login_refusals WHERE player_id = ?1 ORDER BY id DESC LIMIT 20)",
        ).bind(player.id),
    ]);
    await audit(ctx, player.id, "login_refused_other_device", { country: where });
    await notify(ctx, player.id, "security", { data: { reason: "login_refused", country: where }, groupKey: "security:login_refused" });
    throw boundElsewhere();
}

// ------------------------------------------------------------------ the player

async function eventsOf(ctx: Ctx, p: PlayerRow) {
    const refusals = await ctx.env.DB.prepare("SELECT country, created_at FROM login_refusals WHERE player_id = ?1 ORDER BY id DESC LIMIT 10")
        .bind(p.id)
        .all<{ country: string | null; created_at: string }>();
    const events: { kind: string; country?: string | null; device_name?: string | null; at: string }[] = refusals.results.map((r) => ({
        kind: "refused",
        country: r.country,
        at: r.created_at,
    }));
    if (p.device_bound_at) events.push({ kind: "bound", device_name: p.device_name ?? null, at: p.device_bound_at });
    events.sort((a, b) => (a.at < b.at ? 1 : a.at > b.at ? -1 : 0));
    return events;
}

async function latestCopy(ctx: Ctx, playerId: string) {
    const c = await ctx.env.DB.prepare("SELECT id, size, summary, game_version, created_at FROM progress_copies WHERE player_id = ?1 ORDER BY id DESC LIMIT 1")
        .bind(playerId)
        .first<{ id: number; size: number; summary: string; game_version: string; created_at: string }>();
    return c ? copyJson(c) : null;
}

function copyJson(c: { id: number; size: number; summary: string; game_version: string; created_at: string }) {
    let summary: unknown = null;
    try {
        summary = JSON.parse(c.summary);
    } catch {
        summary = null;
    }
    return { id: c.id, size: c.size, game_version: c.game_version, created_at: c.created_at, summary };
}

/** GET /players/me/device: the computer the account is bound to, its security events, the last copy of the progression. */
export async function myDevice(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const p = auth.player;
    return json(ctx, {
        device: p.device_id ? { name: p.device_name, bound_at: p.device_bound_at } : null,
        events: await eventsOf(ctx, p),
        copy: await latestCopy(ctx, p.id),
    });
}

/** PATCH /players/me/device {name}: the computer's name (shown to its owner and to the admins). */
export async function renameDevice(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.deviceRename, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["name"]);
    v.str("name", { min: 1, max: NAME_MAX });
    v.done();
    const name = cleanName(body.name);
    if (!name) throw Errors.validation([{ field: "name", message: `de 1 à ${NAME_MAX} caractères` }]);
    if (!auth.player.device_id) throw Errors.conflict("NO_DEVICE", "Ce compte n'est lié à aucun ordinateur.");
    await ctx.env.DB.prepare("UPDATE players SET device_name = ?2 WHERE id = ?1").bind(auth.player.id, name).run();
    return json(ctx, { device: { name, bound_at: auth.player.device_bound_at } });
}

/**
 * PUT /players/me/progress-copy {format: "cosmic.progress_copy", game_version, summary, files}:
 * a copy of the computer's progression (its saves as the game writes them). Kept in the private
 * bucket, the 5 most recent per account; nothing in it is ever shown to other players.
 */
export async function putProgressCopy(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.progressCopy, "player:" + auth.player.id);
    const body = await readJsonObject(ctx, MAX_PROGRESS_COPY_BYTES);
    const v = new Validator(body, ["format", "game_version", "summary", "files"]);
    const format = v.str("format", { max: 40 });
    const gameVersion = v.str("game_version", { max: 16, pattern: SEMVER_RE });
    if (format !== undefined && format !== "cosmic.progress_copy") v.fail("format", "cosmic.progress_copy attendu");
    const s = body.summary;
    const summary: Record<string, number> = {};
    if (!isObject(s)) v.fail("summary", "objet attendu");
    else
        for (const k of ["levels", "achievements", "stars", "play_seconds", "bosses"]) {
            const n = s[k];
            if (typeof n !== "number" || !Number.isInteger(n) || n < 0 || n > 100_000_000) v.fail(`summary.${k}`, "entier positif attendu");
            else summary[k] = n;
        }
    const f = body.files;
    if (!isObject(f)) v.fail("files", "objet attendu");
    else
        for (const [k, val] of Object.entries(f)) {
            if (!(PROGRESS_FILES as readonly string[]).includes(k)) v.fail(`files.${k}`, "fichier inconnu");
            else if (!isObject(val)) v.fail(`files.${k}`, "objet JSON attendu");
        }
    v.done();
    const now = nowIso();
    const text = JSON.stringify({ format, game_version: gameVersion, player_id: auth.player.id, created_at: now, summary, files: f });
    const bytes = new TextEncoder().encode(text);
    const sha = await sha256Hex(bytes);
    const key = `progress/${auth.player.id}/${now.replace(/[:.]/g, "-")}-${sha.slice(0, 12)}.json`;
    await ctx.env.STORAGE.put(key, bytes, { httpMetadata: { contentType: "application/json; charset=utf-8" }, customMetadata: { sha256: sha } });
    await ctx.env.DB.prepare(
        "INSERT INTO progress_copies (player_id, file_key, size, sha256, summary, game_version, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
    )
        .bind(auth.player.id, key, bytes.byteLength, sha, JSON.stringify(summary), gameVersion, now)
        .run();
    // The oldest go (rows and files).
    const old = await ctx.env.DB.prepare("SELECT id, file_key FROM progress_copies WHERE player_id = ?1 ORDER BY id DESC LIMIT -1 OFFSET ?2")
        .bind(auth.player.id, PROGRESS_COPIES_KEPT)
        .all<{ id: number; file_key: string }>();
    if (old.results.length) {
        await ctx.env.STORAGE.delete(old.results.map((o) => o.file_key));
        await ctx.env.DB.prepare(`DELETE FROM progress_copies WHERE id IN (${old.results.map(() => "?").join(",")})`)
            .bind(...old.results.map((o) => o.id))
            .run();
    }
    return json(ctx, { copy: await latestCopy(ctx, auth.player.id) }, { status: 201 });
}

/** GET /players/me/progress-copy: the last copy (its summary, when). */
export async function getProgressCopy(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    return json(ctx, { copy: await latestCopy(ctx, auth.player.id) });
}

/** The R2 keys of a player's copies (account deletion). */
export async function progressCopyKeys(ctx: Ctx, playerId: string): Promise<string[]> {
    const r = await ctx.env.DB.prepare("SELECT file_key FROM progress_copies WHERE player_id = ?1").bind(playerId).all<{ file_key: string }>();
    return r.results.map((x) => x.file_key);
}

// ------------------------------------------------------------------ administration

async function playerOr404(ctx: Ctx): Promise<PlayerRow> {
    const id = ctx.params.playerId ?? "";
    const p = /^[A-Za-z0-9-]{8,64}$/.test(id) ? await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(id).first<PlayerRow>() : null;
    if (!p) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    return p;
}

/** GET /admin/players/{playerId}/device: the bound computer, the refused attempts, the copies of the progression. */
export async function adminDevice(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const p = await playerOr404(ctx);
    const [attempts, copies] = await Promise.all([
        ctx.env.DB.prepare("SELECT id, country, created_at FROM login_refusals WHERE player_id = ?1 ORDER BY id DESC LIMIT 20")
            .bind(p.id)
            .all<{ id: number; country: string | null; created_at: string }>(),
        ctx.env.DB.prepare("SELECT id, size, summary, game_version, created_at FROM progress_copies WHERE player_id = ?1 ORDER BY id DESC")
            .bind(p.id)
            .all<{ id: number; size: number; summary: string; game_version: string; created_at: string }>(),
    ]);
    return json(ctx, {
        player: { id: p.id, username: p.username },
        device: p.device_id ? { name: p.device_name, bound_at: p.device_bound_at } : null,
        rebind_allowed: p.device_rebind === 1,
        attempts: attempts.results,
        copies: copies.results.map(copyJson),
    });
}

/**
 * POST /admin/players/{playerId}/device {action: "unbind"} | {action: "bind", attempt_id}:
 * « unbind »: the next login, from any computer, binds the account again; « bind »: bound now
 * to the computer of a refused attempt (its sessions elsewhere end). Journal + notification.
 */
export async function adminSetDevice(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const p = await playerOr404(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["action", "attempt_id"]);
    const action = v.oneOf("action", ["unbind", "bind"] as const);
    const attemptId = v.int("attempt_id", { min: 1, max: Number.MAX_SAFE_INTEGER, optional: action !== "bind" });
    v.done();
    const now = nowIso();
    const who = { target: p.id, target_username: p.username, by_username: auth.player.username };
    if (action === "unbind") {
        await ctx.env.DB.prepare("UPDATE players SET device_rebind = 1 WHERE id = ?1").bind(p.id).run();
        await audit(ctx, auth.player.id, "admin_device_unbound", { ...who, device_name: p.device_name ?? null });
        await notify(ctx, p.id, "security", { data: { reason: "admin_unbound" } });
    } else {
        const a = await ctx.env.DB.prepare("SELECT device_id FROM login_refusals WHERE id = ?1 AND player_id = ?2")
            .bind(attemptId, p.id)
            .first<{ device_id: string }>();
        if (!a) throw Errors.notFound("ATTEMPT_NOT_FOUND", "Tentative introuvable.");
        if (await otherAccountOf(ctx, a.device_id, p.id)) throw deviceTaken();
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(
                "UPDATE players SET device_id = ?2, device_name = 'Nouvel ordinateur', device_bound_at = ?3, device_rebind = 0 WHERE id = ?1",
            ).bind(p.id, a.device_id, now),
            ctx.env.DB.prepare("UPDATE player_sessions SET revoked_at = ?2 WHERE player_id = ?1 AND revoked_at IS NULL").bind(p.id, now),
        ]);
        await audit(ctx, auth.player.id, "admin_device_bound", { ...who });
        await notify(ctx, p.id, "security", { data: { reason: "admin_bound" } });
    }
    const after = (await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(p.id).first<PlayerRow>())!;
    return json(ctx, {
        device: after.device_id ? { name: after.device_name, bound_at: after.device_bound_at } : null,
        rebind_allowed: after.device_rebind === 1,
    });
}
