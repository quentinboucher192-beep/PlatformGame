// 2.61 Studio de Créations: the image of a creation (ADMINISTRATION › Créations › Studio), beside
// its parameters (creations.ts):
//   - a suit: Nova's whole sheet, cut as hers (320 × 864, frames of 32 × 32);
//   - a trail or a victory effect: an animated particle (frames side by side, played over the
//     particle's life) and its emitter (EMITTER_BOUNDS);
//   - an avatar frame: a ring (square frames side by side, looped).
// The game keeps the same rules (game/cosmetics/CreationImages.cpp).
//
// Sending (PUT /admin/creations/{id}/image, the PNG as the body, its layout in the query): the
// administrator comes from the session only (requireAdminRole); the PNG is checked — its
// signature, its IHDR chunk (CRC included) and every chunk up to IEND, its size in bytes and in
// pixels — its SHA-256 computed, and it is kept in R2 under a key nobody outside the Worker
// reads (creation-images/<id>/<hash>.png). Every sending and every refusal is written to the
// Journal.
//
// Reading (GET /creations/{id}/image/{hash}): an administrator reads any; a player only the
// image of a creation they own, or one that is worn by a player (it shows on a board, a profile,
// a ghost) or given by a challenge that has begun — never a creation of the reserve that nobody
// was given. Anyone else gets the 404 of an unknown image. The answer is cached by its hash
// (ETag, If-None-Match, immutable). GET /creations/images lists the images the caller may read.
// Display only: nothing here touches a run, a replay or a record.

import { audit, type AuthInfo } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { creationRef, creationRefSql, type CreationKind } from "./challengeRewards";
import { ApiError, Errors, json, nowIso, readLimited, type Ctx } from "./http";
import { RULES, rateLimit } from "./ratelimit";

/** At most this many bytes of PNG. */
export const IMAGE_MAX_BYTES = 512 * 1024;
/** Nova's sheet: a suit's image has its size. */
export const SUIT_SHEET = { width: 320, height: 864, frame: 32 } as const;
/** A particle's frames (trail, victory effect), a ring's (avatar frame), how many, how fast. */
export const PARTICLE_FRAME = { min: 4, max: 64 } as const;
export const RING_FRAME = { min: 32, max: 128 } as const;
export const IMAGE_FRAMES = { min: 1, max: 16 } as const;
export const IMAGE_FPS = { min: 1, max: 30 } as const;
/** The emitter of a trail or a victory effect: each setting and its bounds (CreationImages.cpp). */
export const EMITTER_BOUNDS = {
    rate: { min: 1, max: 120 },
    life_ms: { min: 100, max: 1000 },
    size: { min: 25, max: 400 },
    speed: { min: 0, max: 400 },
    spread: { min: 0, max: 360 },
    gravity: { min: -600, max: 600 },
    spin: { min: -720, max: 720 },
    fade: { min: 0, max: 2 },
} as const;
type EmitterKey = keyof typeof EMITTER_BOUNDS;
export interface Emitter extends Record<EmitterKey, number> {
    sparkles: boolean;
}

const ID_RE = /^[1-9]\d{0,9}$/;
const HASH_RE = /^[0-9a-f]{64}$/;
const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

interface ImageRow {
    creation_id: number;
    kind: CreationKind;
    hash: string;
    r2_key: string;
    bytes: number;
    width: number;
    height: number;
    frame_w: number;
    frame_h: number;
    frames: number;
    fps: number;
    emitter: string | null;
    uploaded_at: string;
}

interface CreationRow {
    id: number;
    kind: CreationKind;
    name: string;
    archived: number;
}

// ------------------------------------------------------------------ PNG

let crcTable: Uint32Array | null = null;
function crc32(data: Uint8Array, from: number, to: number): number {
    if (!crcTable) {
        crcTable = new Uint32Array(256);
        for (let i = 0; i < 256; i++) {
            let c = i;
            for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
            crcTable[i] = c >>> 0;
        }
    }
    let c = 0xffffffff;
    for (let i = from; i < to; i++) c = crcTable[(c ^ data[i]!) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

const be32 = (d: Uint8Array, at: number) => ((d[at]! << 24) | (d[at + 1]! << 16) | (d[at + 2]! << 8) | d[at + 3]!) >>> 0;
const typeAt = (d: Uint8Array, at: number) => String.fromCharCode(d[at]!, d[at + 1]!, d[at + 2]!, d[at + 3]!);

/** The size of a PNG, or why it is not one this server keeps (in French). */
export function readPng(d: Uint8Array): { width: number; height: number } | { problem: string } {
    if (d.length < 8 || PNG_SIGNATURE.some((b, i) => d[i] !== b)) return { problem: "Pas un fichier PNG." };
    if (d.length < 8 + 25 || be32(d, 8) !== 13 || typeAt(d, 12) !== "IHDR") return { problem: "Fichier PNG invalide (en-tête IHDR absent)." };
    if (crc32(d, 12, 29) !== be32(d, 29)) return { problem: "Fichier PNG abîmé (en-tête IHDR)." };
    const width = be32(d, 16);
    const height = be32(d, 20);
    const depth = d[24]!;
    const color = d[25]!;
    const depthOk =
        (color === 0 && [1, 2, 4, 8, 16].includes(depth)) ||
        (color === 3 && [1, 2, 4, 8].includes(depth)) ||
        ([2, 4, 6].includes(color) && [8, 16].includes(depth));
    if (!depthOk || d[26] !== 0 || d[27] !== 0 || d[28]! > 1) return { problem: "Fichier PNG invalide (format de couleur)." };
    if (width === 0 || height === 0) return { problem: "Image vide." };
    // The chunks one after the other, each whole, up to IEND (after IDAT).
    let at = 8;
    let data = false;
    for (;;) {
        if (at + 12 > d.length) return { problem: "Fichier PNG tronqué." };
        const len = be32(d, at);
        if (len > d.length - at - 12) return { problem: "Fichier PNG tronqué." };
        const type = typeAt(d, at + 4);
        if (type === "IDAT") data = true;
        at += 12 + len;
        if (type === "IEND") break;
    }
    if (!data) return { problem: "Fichier PNG sans image (IDAT absent)." };
    return { width, height };
}

/** Why a layout is refused for `kind` ("" = accepted): the same rules as the game's. */
export function layoutProblem(kind: CreationKind, w: number, h: number, fw: number, fh: number, frames: number, fps: number): string {
    if (kind === "suit")
        return w === SUIT_SHEET.width && h === SUIT_SHEET.height
            ? ""
            : `Une combinaison est la planche entière de Nova : ${SUIT_SHEET.width} × ${SUIT_SHEET.height} pixels (ici ${w} × ${h}).`;
    if (frames < IMAGE_FRAMES.min || frames > IMAGE_FRAMES.max) return `De ${IMAGE_FRAMES.min} à ${IMAGE_FRAMES.max} images.`;
    if (fps < IMAGE_FPS.min || fps > IMAGE_FPS.max) return `De ${IMAGE_FPS.min} à ${IMAGE_FPS.max} images par seconde.`;
    if (kind === "frame") {
        if (fw !== fh) return "Les images d'un cadre sont carrées.";
        if (fw < RING_FRAME.min || fw > RING_FRAME.max) return `Un cadre mesure de ${RING_FRAME.min} à ${RING_FRAME.max} pixels de côté.`;
    } else if (fw < PARTICLE_FRAME.min || fw > PARTICLE_FRAME.max || fh < PARTICLE_FRAME.min || fh > PARTICLE_FRAME.max) {
        return `Une particule mesure de ${PARTICLE_FRAME.min} à ${PARTICLE_FRAME.max} pixels de côté.`;
    }
    if (w !== fw * frames || h !== fh) return `La planche doit mesurer ${fw * frames} × ${fh} pixels (ses images côte à côte).`;
    return "";
}

async function sha256Hex(data: Uint8Array): Promise<string> {
    const digest = await crypto.subtle.digest("SHA-256", data);
    return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// ------------------------------------------------------------------ helpers

const NOT_FOUND = () => Errors.notFound("CREATION_NOT_FOUND", "Création introuvable.");
const IMAGE_NOT_FOUND = () => Errors.notFound("CREATION_IMAGE_NOT_FOUND", "Image introuvable.");

function imageJson(r: ImageRow) {
    const out: Record<string, unknown> = {
        ref: creationRef(r.kind, r.creation_id),
        kind: r.kind,
        hash: r.hash,
        bytes: r.bytes,
        width: r.width,
        height: r.height,
        updated_at: r.uploaded_at,
    };
    if (r.kind !== "suit") Object.assign(out, { frame_w: r.frame_w, frame_h: r.frame_h, frames: r.frames, fps: r.fps });
    if (r.emitter) out.emitter = JSON.parse(r.emitter);
    return out;
}

/** A refusal: written to the Journal (admin_refused, with its reason), then answered. */
async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "admin_refused", { ...details, reason: error.code, by_username: auth.player.username });
    throw error;
}

/** An integer of the query in [min, max]; undefined (and a problem) otherwise. */
function queryInt(url: URL, key: string, min: number, max: number, problems: string[]): number | undefined {
    const raw = url.searchParams.get(key);
    if (raw === null || !/^-?\d{1,6}$/.test(raw)) {
        problems.push(`${key} : nombre entier attendu`);
        return undefined;
    }
    const n = Number(raw);
    if (n < min || n > max) {
        problems.push(`${key} : de ${min} à ${max}`);
        return undefined;
    }
    return n;
}

/** SQL: the creation `c` shows somewhere a player may see it (worn, or given by a challenge begun). */
const VISIBLE_SQL = `(
    EXISTS (SELECT 1 FROM player_cosmetics pc
            WHERE (c.kind = 'suit' AND pc.suit = ${creationRefSql("c")}) OR (c.kind = 'trail' AND pc.trail = ${creationRefSql("c")})
               OR (c.kind = 'frame' AND pc.frame = ${creationRefSql("c")}) OR (c.kind = 'victory' AND pc.victory = ${creationRefSql("c")}))
    OR EXISTS (SELECT 1 FROM challenge_reward_rules r JOIN challenges ch ON ch.id = r.challenge_id
               WHERE r.reward_kind = c.kind AND r.reward_ref = ${creationRefSql("c")} AND ch.date <= ?1)
    OR (?2 IS NOT NULL AND EXISTS (SELECT 1 FROM player_won_rewards w
               WHERE w.player_id = ?2 AND w.reward_kind = c.kind AND w.reward_ref = ${creationRefSql("c")})))`;

const today = () => new Date().toISOString().slice(0, 10);

/** Whether the viewer is an administrator (an active account with the role). */
const isAdmin = (viewer: AuthInfo | null) => viewer !== null && viewer.player.role === "admin" && viewer.player.account_status === "active";

// ------------------------------------------------------------------ routes

/**
 * PUT /admin/creations/{creationId}/image (body: the PNG, image/png or application/octet-stream).
 * Query, besides a suit: frame_w, frame_h, frames, fps; a trail or a victory effect also its
 * emitter: rate, life_ms, size, speed, spread, gravity, spin, fade, sparkles (0 or 1). Refused:
 * not an administrator (404), not a PNG (400 NOT_PNG), more than IMAGE_MAX_BYTES (413
 * IMAGE_TOO_LARGE), sizes not of its kind (400 IMAGE_BAD_SIZE), a bad setting (400
 * VALIDATION_ERROR), an archived creation (409 CREATION_ARCHIVED). Answers `{ image, changed }`.
 */
export async function putCreationImage(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = ctx.params.creationId ?? "";
    const what: Record<string, unknown> = { creation: { action: "image", id: ID_RE.test(id) ? Number(id) : null } };
    if (!ID_RE.test(id)) return refuse(ctx, auth, NOT_FOUND(), what);
    const creation = await ctx.env.DB.prepare("SELECT id, kind, name, archived FROM creations WHERE id = ?1").bind(Number(id)).first<CreationRow>();
    if (!creation) return refuse(ctx, auth, NOT_FOUND(), what);
    const ref = creationRef(creation.kind, creation.id);
    what.creation = { action: "image", id: creation.id, ref, kind: creation.kind, name: creation.name };
    const type = (ctx.request.headers.get("content-type") ?? "").toLowerCase();
    if (!type.startsWith("image/png") && !type.startsWith("application/octet-stream"))
        return refuse(ctx, auth, new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le corps doit être l'image PNG (image/png)."), what);
    let bytes: Uint8Array;
    try {
        bytes = await readLimited(ctx.request, IMAGE_MAX_BYTES);
    } catch (e) {
        if (e instanceof ApiError && e.status === 413)
            return refuse(ctx, auth, new ApiError(413, "IMAGE_TOO_LARGE", `Image trop lourde : ${IMAGE_MAX_BYTES / 1024} Ko au plus.`), what);
        throw e;
    }
    const png = readPng(bytes);
    if ("problem" in png) return refuse(ctx, auth, new ApiError(400, "NOT_PNG", png.problem), { ...what, bytes: bytes.length });
    // The layout and the emitter, from the query (only the keys of its kind).
    const url = ctx.url;
    const problems: string[] = [];
    const allowed = creation.kind === "suit" ? [] : ["frame_w", "frame_h", "frames", "fps"];
    if (creation.kind === "trail" || creation.kind === "victory") allowed.push(...Object.keys(EMITTER_BOUNDS), "sparkles");
    for (const k of url.searchParams.keys()) if (!allowed.includes(k)) problems.push(`${k} : inconnu`);
    let fw: number = SUIT_SHEET.frame,
        fh: number = SUIT_SHEET.frame,
        frames = 1,
        fps = 1;
    let emitter: Emitter | null = null;
    if (creation.kind !== "suit") {
        const ringOrParticle = creation.kind === "frame" ? RING_FRAME : PARTICLE_FRAME;
        fw = queryInt(url, "frame_w", ringOrParticle.min, ringOrParticle.max, problems) ?? 0;
        fh = queryInt(url, "frame_h", ringOrParticle.min, ringOrParticle.max, problems) ?? 0;
        frames = queryInt(url, "frames", IMAGE_FRAMES.min, IMAGE_FRAMES.max, problems) ?? 0;
        fps = queryInt(url, "fps", IMAGE_FPS.min, IMAGE_FPS.max, problems) ?? 0;
    }
    if (creation.kind === "trail" || creation.kind === "victory") {
        const e: Partial<Emitter> = {};
        for (const [k, b] of Object.entries(EMITTER_BOUNDS)) e[k as EmitterKey] = queryInt(url, k, b.min, b.max, problems);
        const sp = url.searchParams.get("sparkles");
        if (sp !== "0" && sp !== "1") problems.push("sparkles : 0 ou 1");
        e.sparkles = sp === "1";
        emitter = e as Emitter;
    }
    const tried = { bytes: bytes.length, width: png.width, height: png.height, frame_w: fw, frame_h: fh, frames, fps, emitter };
    if (problems.length > 0)
        return refuse(ctx, auth, Errors.validation(problems.map((p) => ({ field: p.split(" : ")[0], message: p.split(" : ")[1] ?? p }))), { ...what, image: tried });
    const layout = layoutProblem(creation.kind, png.width, png.height, fw, fh, frames, fps);
    if (layout) return refuse(ctx, auth, new ApiError(400, "IMAGE_BAD_SIZE", layout), { ...what, image: tried });
    if (creation.archived === 1)
        return refuse(ctx, auth, Errors.conflict("CREATION_ARCHIVED", "Cette création est archivée : restaurez-la avant de lui donner une image."), what);
    const hash = await sha256Hex(bytes);
    const before = await ctx.env.DB.prepare("SELECT * FROM creation_images WHERE creation_id = ?1").bind(creation.id).first<ImageRow>();
    const emitterText = emitter ? JSON.stringify(emitter) : null;
    if (
        before &&
        before.hash === hash &&
        before.frame_w === fw &&
        before.frame_h === fh &&
        before.frames === frames &&
        before.fps === fps &&
        (before.emitter ?? null) === emitterText
    )
        return json(ctx, { image: imageJson(before), changed: false });
    // Kept under a key of its own (the previous one removed once the row points to the new one).
    const key = `creation-images/${creation.id}/${hash}.png`;
    await ctx.env.STORAGE.put(key, bytes, { httpMetadata: { contentType: "image/png" }, customMetadata: { sha256: hash, creation: ref } });
    const now = nowIso();
    await ctx.env.DB.prepare(
        `INSERT INTO creation_images (creation_id, kind, hash, r2_key, bytes, width, height, frame_w, frame_h, frames, fps, emitter, uploaded_by, uploaded_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)
         ON CONFLICT (creation_id) DO UPDATE SET kind = ?2, hash = ?3, r2_key = ?4, bytes = ?5, width = ?6, height = ?7, frame_w = ?8, frame_h = ?9,
           frames = ?10, fps = ?11, emitter = ?12, uploaded_by = ?13, uploaded_at = ?14`,
    )
        .bind(creation.id, creation.kind, hash, key, bytes.length, png.width, png.height, fw, fh, frames, fps, emitterText, auth.player.id, now)
        .run();
    if (before && before.r2_key !== key) await ctx.env.STORAGE.delete(before.r2_key);
    const after = (await ctx.env.DB.prepare("SELECT * FROM creation_images WHERE creation_id = ?1").bind(creation.id).first<ImageRow>())!;
    await audit(ctx, auth.player.id, "admin_creation_image", {
        creation: what.creation,
        image: { hash, ...tried, previous_hash: before?.hash ?? null },
        by_username: auth.player.username,
    });
    return json(ctx, { image: imageJson(after), changed: true });
}

/** GET /admin/creations/images: `{ images }`, every creation's image (the Studio). */
export async function adminImages(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const rows = await ctx.env.DB.prepare("SELECT * FROM creation_images ORDER BY creation_id LIMIT 500").all<ImageRow>();
    return json(ctx, { images: rows.results.map(imageJson) });
}

/**
 * GET /creations/images (session optional): `{ images }`, the images the caller may read: of the
 * creations worn by a player or given by a challenge that has begun, and of those the caller
 * won (an administrator: all of them).
 */
export async function publicImages(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const rows = isAdmin(viewer)
        ? await ctx.env.DB.prepare("SELECT * FROM creation_images ORDER BY creation_id LIMIT 500").all<ImageRow>()
        : await ctx.env.DB.prepare(
              `SELECT ci.* FROM creation_images ci JOIN creations c ON c.id = ci.creation_id AND c.kind = ci.kind
               WHERE ${VISIBLE_SQL} ORDER BY ci.creation_id LIMIT 500`,
          )
              .bind(today(), viewer?.player.id ?? null)
              .all<ImageRow>();
    return json(ctx, { images: rows.results.map(imageJson) });
}

/**
 * GET /creations/{creationId}/image/{hash}: the PNG (image/png), when the caller may read it
 * (publicImages' rules) and `hash` is its current one; else 404 CREATION_IMAGE_NOT_FOUND, the
 * same for an image that does not exist. ETag "<hash>": If-None-Match answers 304.
 */
export async function getCreationImage(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const id = ctx.params.creationId ?? "";
    const hash = ctx.params.hash ?? "";
    if (!ID_RE.test(id) || !HASH_RE.test(hash)) throw IMAGE_NOT_FOUND();
    const row = isAdmin(viewer)
        ? await ctx.env.DB.prepare("SELECT * FROM creation_images WHERE creation_id = ?1 AND hash = ?2").bind(Number(id), hash).first<ImageRow>()
        : await ctx.env.DB.prepare(
              `SELECT ci.* FROM creation_images ci JOIN creations c ON c.id = ci.creation_id AND c.kind = ci.kind
               WHERE ci.creation_id = ?3 AND ci.hash = ?4 AND ${VISIBLE_SQL}`,
          )
              .bind(today(), viewer?.player.id ?? null, Number(id), hash)
              .first<ImageRow>();
    if (!row) throw IMAGE_NOT_FOUND();
    const etag = `"${row.hash}"`;
    const headers: Record<string, string> = {
        etag,
        // The address holds the hash: what it gives never changes. Private: the reading is checked.
        "cache-control": "private, max-age=31536000, immutable",
        "x-content-type-options": "nosniff",
    };
    const match = ctx.request.headers.get("if-none-match");
    if (match && match.split(",").some((t) => t.trim().replace(/^W\//, "") === etag)) return new Response(null, { status: 304, headers });
    const obj = await ctx.env.STORAGE.get(row.r2_key);
    if (!obj) throw IMAGE_NOT_FOUND();
    return new Response(obj.body, { status: 200, headers: { ...headers, "content-type": "image/png", "content-length": String(row.bytes) } });
}
