// Game installers (Windows): the latest version for the in-game "nouvelle version"
// notification, and the download itself, streamed from R2 with its SHA-256 so the game can
// check the file before offering to run it. Public (no account needed), rate limited.

import { Errors, json, type Ctx } from "./http";
import { RULES, rateLimit } from "./ratelimit";

interface ReleaseRow {
    version: string;
    file_key: string;
    file_name: string;
    size: number;
    sha256: string;
    notes: string;
    notes_en: string;
    published_at: string;
}

const VERSION = /^\d{1,3}\.\d{1,3}\.\d{1,4}$/;

/** GET /client/latest?platform=windows */
export async function latest(ctx: Ctx): Promise<Response> {
    const platform = ctx.url.searchParams.get("platform") ?? "windows";
    if (!/^[a-z]{1,16}$/.test(platform)) throw Errors.validation("Plateforme invalide.");
    const r = await ctx.env.DB.prepare(
        "SELECT version, file_key, file_name, size, sha256, notes, notes_en, published_at FROM client_releases WHERE platform = ?1 ORDER BY version_key DESC LIMIT 1",
    )
        .bind(platform)
        .first<ReleaseRow>();
    if (!r) return json(ctx, { release: null });
    // 2.62: `since` = the version the player has: the notes of every release published after it,
    // newest first, so that a player several versions behind sees everything that changed.
    const since = ctx.url.searchParams.get("since");
    // 2.76.2: `notes_en` = the same notes in English (written from the game's dictionary when the
    // installer was registered, "" when there are none): the game shows those of its language.
    let changes: { version: string; notes: string; notes_en: string; published_at: string }[] | undefined;
    if (since !== null) {
        if (!VERSION.test(since)) throw Errors.validation("Version invalide.");
        const [a = 0, b = 0, c = 0] = since.split(".").map(Number);
        const rows = await ctx.env.DB.prepare(
            "SELECT version, notes, notes_en, published_at FROM client_releases WHERE platform = ?1 AND version_key > ?2 ORDER BY version_key DESC LIMIT 100",
        )
            .bind(platform, a * 1000000 + b * 1000 + c)
            .all<{ version: string; notes: string; notes_en: string; published_at: string }>();
        changes = rows.results ?? [];
    }
    return json(ctx, {
        ...(changes ? { changes } : {}),
        release: {
            version: r.version,
            file_name: r.file_name,
            size: r.size,
            sha256: r.sha256,
            notes: r.notes,
            notes_en: r.notes_en,
            published_at: r.published_at,
            download: `/api/v1/client/download/${r.version}`,
        },
    });
}

/** GET /client/download/{version} : the installer itself. */
export async function download(ctx: Ctx): Promise<Response> {
    await rateLimit(ctx, RULES.clientDownload, "ip:" + ctx.ip);
    const v = ctx.params.version ?? "";
    if (!VERSION.test(v)) throw Errors.notFound("RELEASE_NOT_FOUND", "Version introuvable.");
    const r = await ctx.env.DB.prepare("SELECT version, file_key, file_name, size, sha256, notes, notes_en, published_at FROM client_releases WHERE version = ?1")
        .bind(v)
        .first<ReleaseRow>();
    if (!r) throw Errors.notFound("RELEASE_NOT_FOUND", "Version introuvable.");
    const obj = await ctx.env.STORAGE.get(r.file_key);
    if (!obj) throw Errors.notFound("RELEASE_NOT_FOUND", "Fichier introuvable.");
    return new Response(obj.body, {
        headers: {
            "content-type": "application/octet-stream",
            "content-disposition": `attachment; filename="${r.file_name}"`,
            "content-length": String(obj.size),
            "x-content-sha256": r.sha256,
            "x-request-id": ctx.requestId,
            "x-content-type-options": "nosniff",
        },
    });
}
