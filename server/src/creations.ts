// ADMINISTRATION › Créations: things an administrator makes on the server that every game
// draws without an update. For now one kind, the suits (« Combinaisons »): a name and three
// colours (the suit, its accent, its scarf), which the game applies to Nova's sheet the way
// tools/assetgen/characters/nova_suits.py makes the catalogue's suits (game/cosmetics/
// SuitRecolor.cpp). A suit is given as the reward of a challenge (challengeRewards.ts:
// reward_kind 'suit', ref "srvsuit:<id>") and worn only once won (cosmetics.ts).
//
// The administrator comes from the session only (requireAdminRole: any other account gets the
// 404 of an unknown route); every change and every refusal is written to the Journal. A
// creation is never deleted, only archived: it is no longer offered as a new reward, and the
// players who won it keep it (the public list still gives its colours while anyone has it, or
// a challenge gives it). Nothing here touches a run, a replay or a record.

import { audit, type AuthInfo } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { TITLE_MAX_CHARS, cleanTitleText, suitRef, titleKey } from "./challengeRewards";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";

export const CREATION_KINDS = ["suit"] as const;
export type CreationKind = (typeof CREATION_KINDS)[number];
/** A creation's name: at most this many characters (the same rules as a server title). */
export const CREATION_NAME_MAX = TITLE_MAX_CHARS;
/** The colours of a suit, in the order of nova_suits.py: the suit, its accent (visor, energy), its scarf. */
export const SUIT_COLOR_KEYS = ["suit", "accent", "scarf"] as const;
/** At most this many creations are listed (administration, public list). */
const LIST_LIMIT = 500;

const COLOR_RE = /^#[0-9a-fA-F]{6}$/;
const ID_RE = /^[1-9]\d{0,9}$/;

interface CreationRow {
    id: number;
    kind: string;
    name: string;
    suit_color: string;
    accent_color: string;
    scarf_color: string;
    created_at: string;
    updated_at: string;
    archived: number;
    archived_at: string | null;
    created_by_name?: string | null;
    uses?: number;
    owners?: number;
}

type Colors = Record<(typeof SUIT_COLOR_KEYS)[number], string>;

function colorsOf(r: Pick<CreationRow, "suit_color" | "accent_color" | "scarf_color">): Colors {
    return { suit: r.suit_color, accent: r.accent_color, scarf: r.scarf_color };
}

/** A creation as the administration lists it. */
function adminJson(r: CreationRow) {
    return {
        id: r.id,
        ref: suitRef(r.id),
        kind: r.kind,
        name: r.name,
        colors: colorsOf(r),
        created_at: r.created_at,
        updated_at: r.updated_at,
        created_by: r.created_by_name ?? null,
        archived: r.archived === 1,
        archived_at: r.archived_at,
        uses: r.uses ?? 0,
        owners: r.owners ?? 0,
    };
}

/** What the Journal keeps of a creation (and, for an edit, of what it was). */
function journalOf(r: Pick<CreationRow, "id" | "kind" | "name" | "suit_color" | "accent_color" | "scarf_color">) {
    return { id: r.id, ref: suitRef(r.id), kind: r.kind, name: r.name, colors: colorsOf(r) };
}

const ADMIN_SQL = `SELECT c.*, p.username AS created_by_name,
    (SELECT COUNT(DISTINCT r.challenge_id) FROM challenge_reward_rules r WHERE r.reward_kind = 'suit' AND r.reward_ref = 'srvsuit:' || c.id) AS uses,
    (SELECT COUNT(DISTINCT w.player_id) FROM player_won_rewards w WHERE w.reward_kind = 'suit' AND w.reward_ref = 'srvsuit:' || c.id) AS owners
  FROM creations c LEFT JOIN players p ON p.id = c.created_by`;

/** A refusal: written to the Journal (admin_refused, with its reason), then answered. */
async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "admin_refused", { ...details, reason: error.code, by_username: auth.player.username });
    throw error;
}

const NOT_FOUND = () => Errors.notFound("CREATION_NOT_FOUND", "Création introuvable.");
const EXISTS = (existing?: CreationRow | null) =>
    Errors.conflict(
        "CREATION_EXISTS",
        "Une combinaison porte déjà ce nom : choisissez-en un autre.",
        existing ? { creation: adminJson(existing) } : undefined,
    );

/** The body's name and colours, checked; `v` collects the problems. */
function readSuit(body: Record<string, unknown>, v: Validator): { name: string | null; colors: Colors | null; rawName: string } {
    const raw = v.str("name", { min: 1, max: 128, trim: true });
    const name = raw === undefined ? null : cleanTitleText(raw);
    if (raw !== undefined && !name) v.fail("name", `de 1 à ${CREATION_NAME_MAX} caractères : lettres, chiffres, ponctuation et espaces`);
    const c = body.colors;
    let colors: Colors | null = null;
    if (typeof c !== "object" || c === null || Array.isArray(c)) {
        v.fail("colors", "objet { suit, accent, scarf } attendu");
    } else {
        const cv = new Validator(c as Record<string, unknown>, SUIT_COLOR_KEYS);
        const out = {} as Colors;
        for (const k of SUIT_COLOR_KEYS) out[k] = (cv.str(k, { min: 7, max: 7, pattern: COLOR_RE }) ?? "").toLowerCase();
        // A colour of the right type but not "#rrggbb": said plainly.
        const plain = (m: string) => (m === "format invalide" || m.startsWith("longueur") ? "couleur « #rrggbb » attendue" : m);
        for (const e of cv.errors) v.fail(`colors.${e.field}`, plain(e.message));
        if (cv.errors.length === 0) colors = out;
    }
    return { name, colors, rawName: (raw ?? "").slice(0, 64) };
}

/** The creation of the route's {creationId}; a malformed id is « not found ». */
async function creationOf(ctx: Ctx): Promise<CreationRow> {
    const id = ctx.params.creationId ?? "";
    if (!ID_RE.test(id)) throw NOT_FOUND();
    const row = await ctx.env.DB.prepare(`${ADMIN_SQL} WHERE c.id = ?1`).bind(Number(id)).first<CreationRow>();
    if (!row) throw NOT_FOUND();
    return row;
}

/** Another creation of the same kind, not archived, with the same name (accents and case aside). */
async function sameName(ctx: Ctx, kind: string, key: string, except: number): Promise<CreationRow | null> {
    return ctx.env.DB.prepare(`${ADMIN_SQL} WHERE c.kind = ?1 AND c.name_key = ?2 AND c.archived = 0 AND c.id <> ?3`)
        .bind(kind, key, except)
        .first<CreationRow>();
}

const isUniqueError = (e: unknown) => e instanceof Error && /UNIQUE constraint failed/i.test(e.message);

/**
 * GET /admin/creations: `{ creations: [{ id, ref: "srvsuit:<n>", kind, name, colors: { suit,
 * accent, scarf }, created_at, updated_at, created_by, archived, archived_at, uses, owners }],
 * kinds, max_chars }`: every creation, those in use first then the archived ones, the newest
 * first (`uses`: the challenges that give it; `owners`: the players who won it).
 */
export async function listCreations(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const rows = await ctx.env.DB.prepare(`${ADMIN_SQL} ORDER BY c.archived, c.id DESC LIMIT ${LIST_LIMIT}`).all<CreationRow>();
    return json(ctx, { creations: rows.results.map(adminJson), kinds: CREATION_KINDS, max_chars: CREATION_NAME_MAX });
}

/**
 * POST /admin/creations `{ kind: "suit", name, colors: { suit, accent, scarf } }`: a new suit.
 * The name: trimmed, inner spaces collapsed, 1 to 32 characters, letters, digits, punctuation
 * and spaces only; another suit in use with the same name (accents and case aside) is refused
 * (409 CREATION_EXISTS, `details.creation` the existing one). Colours: "#rrggbb" (stored in
 * lower case). Written to the Journal, refusals too. Answers 201 `{ creation }`.
 */
export async function createCreation(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx, 2048);
    const v = new Validator(body, ["kind", "name", "colors"]);
    const kind = v.oneOf("kind", CREATION_KINDS);
    const { name, colors, rawName } = readSuit(body, v);
    const what = { creation: { action: "create", kind: kind ?? null, name: rawName, colors } };
    if (v.errors.length > 0 || !kind || !name || !colors) await refuse(ctx, auth, Errors.validation(v.errors), { ...what, field: v.errors[0]?.field ?? "name" });
    const key = titleKey(name!);
    const now = nowIso();
    let row: CreationRow | null = null;
    try {
        row = await ctx.env.DB.prepare(
            `INSERT INTO creations (kind, name, name_key, suit_color, accent_color, scarf_color, created_by, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8)
             ON CONFLICT DO NOTHING RETURNING *`,
        )
            .bind(kind, name, key, colors!.suit, colors!.accent, colors!.scarf, auth.player.id, now)
            .first<CreationRow>();
    } catch (e) {
        if (!isUniqueError(e)) throw e;
    }
    if (!row) return refuse(ctx, auth, EXISTS(await sameName(ctx, kind!, key, 0)), what);
    const creation = adminJson({ ...row, created_by_name: auth.player.username });
    await audit(ctx, auth.player.id, "admin_creation_created", { creation: journalOf(row), by_username: auth.player.username });
    return json(ctx, { creation }, { status: 201 });
}

/**
 * PUT /admin/creations/{creationId} `{ name, colors: { suit, accent, scarf }, updated_at? }`:
 * a suit renamed or recoloured (every game that draws it follows: the players who won it keep
 * it). `updated_at`, when sent, is the version the administrator edited: changed since (by
 * another administrator), the edit is refused (409 CREATION_CHANGED, `details.creation` the
 * current one). An archived creation is not edited (409 CREATION_ARCHIVED: restore it first).
 * Answers `{ creation, changed }` (nothing changed: no Journal entry).
 */
export async function updateCreation(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const before = await creationOf(ctx);
    const body = await readJsonObject(ctx, 2048);
    const v = new Validator(body, ["name", "colors", "updated_at"]);
    const { name, colors, rawName } = readSuit(body, v);
    const base = v.str("updated_at", { min: 10, max: 40, optional: true });
    const what = { creation: { action: "update", id: before.id, ref: suitRef(before.id), kind: before.kind, name: rawName, colors, previous: journalOf(before) } };
    if (v.errors.length > 0 || !name || !colors) await refuse(ctx, auth, Errors.validation(v.errors), { ...what, field: v.errors[0]?.field ?? "name" });
    if (before.archived === 1)
        await refuse(ctx, auth, Errors.conflict("CREATION_ARCHIVED", "Cette création est archivée : restaurez-la avant de la modifier."), what);
    if (base !== undefined && base !== before.updated_at)
        await refuse(
            ctx,
            auth,
            Errors.conflict("CREATION_CHANGED", "Cette création a changé entre-temps : relisez-la avant de la modifier.", { creation: adminJson(before) }),
            what,
        );
    const same = before.name === name && colorsOf(before).suit === colors!.suit && before.accent_color === colors!.accent && before.scarf_color === colors!.scarf;
    if (same) return json(ctx, { creation: adminJson(before), changed: false });
    const key = titleKey(name!);
    const clash = await sameName(ctx, before.kind, key, before.id);
    if (clash) await refuse(ctx, auth, EXISTS(clash), what);
    let res: D1Result | null = null;
    try {
        // Only the version read (another administrator's edit in between: changed).
        res = await ctx.env.DB.prepare(
            `UPDATE creations SET name = ?2, name_key = ?3, suit_color = ?4, accent_color = ?5, scarf_color = ?6, updated_at = ?7
             WHERE id = ?1 AND archived = 0 AND updated_at = ?8`,
        )
            .bind(before.id, name, key, colors!.suit, colors!.accent, colors!.scarf, nowIso(), before.updated_at)
            .run();
    } catch (e) {
        if (!isUniqueError(e)) throw e;
        await refuse(ctx, auth, EXISTS(await sameName(ctx, before.kind, key, before.id)), what);
    }
    if (!res?.meta.changes)
        await refuse(ctx, auth, Errors.conflict("CREATION_CHANGED", "Cette création a changé entre-temps : relisez-la avant de la modifier."), what);
    const after = (await ctx.env.DB.prepare(`${ADMIN_SQL} WHERE c.id = ?1`).bind(before.id).first<CreationRow>())!;
    await audit(ctx, auth.player.id, "admin_creation_updated", {
        creation: { ...journalOf(after), previous: journalOf(before) },
        by_username: auth.player.username,
    });
    return json(ctx, { creation: adminJson(after), changed: true });
}

/**
 * POST /admin/creations/{creationId}/archive `{ archived: true|false }`: archived, a creation
 * is no longer offered as a new reward (the challenges that already give it keep it, the
 * players who won it keep it); restored, it is offered again (refused when another one in use
 * has its name: 409 CREATION_EXISTS). Answers `{ creation, changed }`.
 */
export async function archiveCreation(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const before = await creationOf(ctx);
    const body = await readJsonObject(ctx, 256);
    const v = new Validator(body, ["archived"]);
    const archived = v.bool("archived");
    const what = { creation: { ...journalOf(before), action: archived === false ? "restore" : "archive" } };
    if (v.errors.length > 0 || archived === undefined) await refuse(ctx, auth, Errors.validation(v.errors), { ...what, field: v.errors[0]?.field ?? "archived" });
    if ((before.archived === 1) === archived) return json(ctx, { creation: adminJson(before), changed: false });
    const now = nowIso();
    if (!archived) {
        const clash = await sameName(ctx, before.kind, titleKey(before.name), before.id);
        if (clash) await refuse(ctx, auth, EXISTS(clash), what);
    }
    try {
        await ctx.env.DB.prepare("UPDATE creations SET archived = ?2, archived_at = ?3, updated_at = ?4 WHERE id = ?1")
            .bind(before.id, archived ? 1 : 0, archived ? now : null, now)
            .run();
    } catch (e) {
        if (!isUniqueError(e)) throw e;
        await refuse(ctx, auth, EXISTS(await sameName(ctx, before.kind, titleKey(before.name), before.id)), what);
    }
    const after = (await ctx.env.DB.prepare(`${ADMIN_SQL} WHERE c.id = ?1`).bind(before.id).first<CreationRow>())!;
    await audit(ctx, auth.player.id, archived ? "admin_creation_archived" : "admin_creation_restored", {
        creation: journalOf(after),
        by_username: auth.player.username,
    });
    return json(ctx, { creation: adminJson(after), changed: true });
}

/**
 * GET /creations (public, no account): `{ creations: [{ id, ref: "srvsuit:<n>", kind, name,
 * colors: { suit, accent, scarf }, updated_at }] }`: the creations every game may have to
 * draw (another player's suit on a profile, a board, a ghost; a challenge's reward): those in
 * use, and the archived ones still won by a player or given by a challenge. The game keeps
 * the list on disk (drawn offline after one reading).
 */
export async function publicCreations(ctx: Ctx): Promise<Response> {
    const rows = await ctx.env.DB.prepare(
        `SELECT c.id, c.kind, c.name, c.suit_color, c.accent_color, c.scarf_color, c.updated_at FROM creations c
         WHERE c.archived = 0
            OR EXISTS (SELECT 1 FROM player_won_rewards w WHERE w.reward_kind = 'suit' AND w.reward_ref = 'srvsuit:' || c.id)
            OR EXISTS (SELECT 1 FROM challenge_reward_rules r WHERE r.reward_kind = 'suit' AND r.reward_ref = 'srvsuit:' || c.id)
         ORDER BY c.id LIMIT ${LIST_LIMIT}`,
    ).all<CreationRow>();
    return json(ctx, {
        creations: rows.results.map((r) => ({ id: r.id, ref: suitRef(r.id), kind: r.kind, name: r.name, colors: colorsOf(r), updated_at: r.updated_at })),
    });
}

// ------------------------------------------------- rewards of the challenges (adminChallenges.ts)

/** The suits created on the server among `ids` (the rewards editor): known, and whether archived. */
export async function knownSuits(ctx: Ctx, ids: number[]): Promise<Map<number, { archived: boolean }>> {
    const out = new Map<number, { archived: boolean }>();
    if (ids.length === 0) return out;
    const rows = await ctx.env.DB.prepare("SELECT id, archived FROM creations WHERE kind = 'suit' AND id IN (SELECT value FROM json_each(?1))")
        .bind(JSON.stringify(ids))
        .all<{ id: number; archived: number }>();
    for (const r of rows.results) out.set(r.id, { archived: r.archived === 1 });
    return out;
}
