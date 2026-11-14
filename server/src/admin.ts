// Moderation routes, enabled only when the ADMIN_TOKEN secret is set (wrangler secret put).
// The token is never distributed with the game: these routes are for the operators.

import { audit } from "./auth";
import { secretsEqual } from "./crypto";
import { ApiError, Errors, json, readJsonObject, type Ctx } from "./http";
import type { RecordRow } from "./model";
import { applyDecision } from "./records";
import { HASH_RE, LEVEL_ID_RE, PLANET_ID_RE, SEMVER_RE, Validator, paging } from "./validate";

export async function requireAdmin(ctx: Ctx): Promise<void> {
    const secret = ctx.env.ADMIN_TOKEN;
    if (!secret || secret.length < 32) throw Errors.notFound();
    const h = ctx.request.headers.get("authorization") ?? "";
    const token = h.startsWith("Bearer ") ? h.slice(7) : "";
    if (!token || !(await secretsEqual(token, secret))) {
        await audit(ctx, null, "admin_auth_failed");
        throw Errors.unauthorized();
    }
}

/** GET /admin/records?status=PENDING */
export async function listForReview(ctx: Ctx): Promise<Response> {
    await requireAdmin(ctx);
    const page = paging(ctx.url, 50, 200);
    const status = ctx.url.searchParams.get("status") ?? "PENDING";
    if (!["PENDING", "VERIFIED", "REJECTED"].includes(status)) throw Errors.validation([{ field: "status", message: "statut inconnu" }]);
    const rows = await ctx.env.DB.prepare(
        `SELECT r.*, p.username, p.trust_level, m.analysis FROM records r JOIN players p ON p.id = r.player_id
         LEFT JOIN replay_metadata m ON m.id = r.replay_id
         WHERE r.verification_status = ?1 ORDER BY r.created_at LIMIT ?2 OFFSET ?3`,
    )
        .bind(status, page.limit, page.offset)
        .all();
    return json(ctx, { records: rows.results });
}

/** POST /admin/records/{id}/review {"decision": "VERIFIED"|"REJECTED", "reason": "..."} */
export async function review(ctx: Ctx): Promise<Response> {
    await requireAdmin(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["decision", "reason"]);
    const decision = v.oneOf("decision", ["VERIFIED", "REJECTED"] as const);
    const reason = v.str("reason", { max: 200, optional: true });
    v.done();
    const r = await ctx.env.DB.prepare("SELECT * FROM records WHERE id = ?1").bind(ctx.params.recordId).first<RecordRow>();
    if (!r) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    if (r.verification_status !== "PENDING") throw new ApiError(409, "NOT_PENDING", "Ce record n'est pas en attente.");
    const ranking = await applyDecision(ctx, r, {
        status: decision!,
        reason: decision === "REJECTED" ? (reason ?? "MODERATION") : null,
        note: reason ?? "Vérifié manuellement.",
    });
    await audit(ctx, r.player_id, "moderation_review", { record_id: r.id, decision });
    return json(ctx, { record_id: r.id, verification_status: decision, ranking });
}

interface ManifestLevel {
    id: string;
    name: string;
    planet_id: string;
    planet_name: string;
    order: number;
    version: number;
    content_hash: string;
    total_collectibles: number;
    total_enemies: number;
    time_limit_ms: number;
    min_time_ms: number;
    bounds: [number, number];
    spawn: [number, number];
    exit: [number, number, number, number];
}

function parseManifestLevel(raw: unknown, index: number): ManifestLevel {
    const where = `levels[${index}]`;
    if (raw === null || typeof raw !== "object" || Array.isArray(raw)) throw Errors.validation([{ field: where, message: "objet attendu" }]);
    const o = raw as Record<string, unknown>;
    const v = new Validator(o, ["id", "name", "planet_id", "planet_name", "order", "version", "content_hash", "total_collectibles",
        "total_enemies", "time_limit_ms", "min_time_ms", "bounds", "spawn", "exit"]);
    v.str("id", { max: 64, pattern: LEVEL_ID_RE });
    v.str("name", { min: 1, max: 80 });
    v.str("planet_id", { max: 64, pattern: PLANET_ID_RE });
    v.str("planet_name", { max: 80, optional: true });
    v.int("order", { min: 0, max: 1000 });
    v.int("version", { min: 1, max: 1_000_000 });
    v.str("content_hash", { min: 64, max: 64, pattern: HASH_RE });
    v.int("total_collectibles", { min: 0, max: 10_000 });
    v.int("total_enemies", { min: 0, max: 100_000 });
    v.int("time_limit_ms", { min: 1000, max: 24 * 3600 * 1000 });
    v.int("min_time_ms", { min: 0, max: 24 * 3600 * 1000 });
    const nums = (k: string, n: number) => {
        const a = o[k];
        if (!Array.isArray(a) || a.length !== n || !a.every((x) => typeof x === "number" && Number.isFinite(x))) v.fail(k, `${n} nombres attendus`);
    };
    nums("bounds", 2);
    nums("spawn", 2);
    nums("exit", 4);
    if (v.errors.length) throw Errors.validation(v.errors.map((e) => ({ field: `${where}.${e.field}`, message: e.message })));
    return o as unknown as ManifestLevel;
}

/**
 * POST /admin/levels: registers the level revisions of a game release (body = the output
 * of `CosmicPlatformer --online-manifest`). New revisions become current and older ones
 * legacy (their leaderboards stay readable, never mixed). Re-registering is idempotent;
 * a different hash under an existing revision number is refused.
 */
export async function registerLevels(ctx: Ctx): Promise<Response> {
    await requireAdmin(ctx);
    const body = await readJsonObject(ctx, 256 * 1024);
    if (!Array.isArray(body.levels) || body.levels.length === 0 || body.levels.length > 500)
        throw Errors.validation([{ field: "levels", message: "liste de 1 à 500 niveaux attendue" }]);
    const levels = body.levels.map((l, i) => parseManifestLevel(l, i));
    const minGameVersion = typeof body.game_version === "string" && SEMVER_RE.test(body.game_version) ? body.game_version : ctx.env.MIN_GAME_VERSION;
    const report: { id: string; version: number; result: string }[] = [];
    const now = new Date().toISOString();
    for (const l of levels) {
        const sameVersion = await ctx.env.DB.prepare("SELECT content_hash FROM level_versions WHERE level_id = ?1 AND version = ?2")
            .bind(l.id, l.version)
            .first<{ content_hash: string }>();
        if (sameVersion && sameVersion.content_hash !== l.content_hash)
            throw Errors.conflict("REVISION_CONFLICT", `${l.id} : la révision ${l.version} existe avec un autre contenu (augmentez metadata.revision).`);
        const current = await ctx.env.DB.prepare("SELECT current_version FROM levels WHERE id = ?1").bind(l.id).first<{ current_version: number }>();
        const becomesCurrent = !current || l.version >= current.current_version;
        const stmts = [
            ctx.env.DB.prepare(
                `INSERT INTO levels (id, name, planet_id, planet_name, level_order, current_version, content_hash, ranking_enabled)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 1)
                 ON CONFLICT (id) DO UPDATE SET name = ?2, planet_id = ?3, planet_name = ?4, level_order = ?5`,
            ).bind(l.id, l.name, l.planet_id, l.planet_name ?? "", l.order, l.version, l.content_hash),
            ctx.env.DB.prepare(
                `INSERT INTO level_versions (level_id, version, content_hash, compatibility_status, min_game_version, total_collectibles,
                   total_enemies, time_limit_ms, min_time_ms, bounds_w, bounds_h, spawn_x, spawn_y, exit_x, exit_y, exit_w, exit_h, created_at)
                 VALUES (?1, ?2, ?3, 'active', ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)
                 ON CONFLICT (level_id, version) DO NOTHING`,
            ).bind(l.id, l.version, l.content_hash, minGameVersion, l.total_collectibles, l.total_enemies, l.time_limit_ms, l.min_time_ms,
                l.bounds[0], l.bounds[1], l.spawn[0], l.spawn[1], l.exit[0], l.exit[1], l.exit[2], l.exit[3], now),
        ];
        if (becomesCurrent) {
            stmts.push(
                ctx.env.DB.prepare("UPDATE levels SET current_version = ?2, content_hash = ?3 WHERE id = ?1").bind(l.id, l.version, l.content_hash),
                ctx.env.DB.prepare("UPDATE level_versions SET compatibility_status = 'legacy' WHERE level_id = ?1 AND version < ?2 AND compatibility_status = 'active'")
                    .bind(l.id, l.version),
            );
        }
        await ctx.env.DB.batch(stmts);
        report.push({ id: l.id, version: l.version, result: sameVersion ? "unchanged" : becomesCurrent ? "current" : "added_as_legacy" });
    }
    await audit(ctx, null, "levels_registered", { count: levels.length });
    return json(ctx, { levels: report });
}
