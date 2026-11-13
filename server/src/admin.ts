// Moderation routes, enabled only when the ADMIN_TOKEN secret is set (wrangler secret put).
// The token is never distributed with the game: these routes are for the operators.

import { audit } from "./auth";
import { secretsEqual } from "./crypto";
import { ApiError, Errors, json, readJsonObject, type Ctx } from "./http";
import type { RecordRow } from "./model";
import { applyDecision } from "./records";
import { Validator, paging } from "./validate";

async function requireAdmin(ctx: Ctx): Promise<void> {
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
