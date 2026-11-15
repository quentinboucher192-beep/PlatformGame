// What a player wears online (2.21), chosen in the game's Collection: the avatar frame and the
// title follow them on their profile and in the leaderboards, the suit on their profile and
// their ghosts. The server never trusts the game: each id must exist in its own catalogue
// (src/cosmeticCatalog.json, generated from the game's data) and be unlocked by the
// achievements the player declared (player_achievements). Anything else becomes the
// category's default before it is stored; names shown are always the catalogue's.
import achievementCatalog from "./achievementCatalog.json";
import catalog from "./cosmeticCatalog.json";
import { requireAuth } from "./auth";
import { json, nowIso, readJsonObject, type Ctx } from "./http";
import { rateLimit, RULES } from "./ratelimit";
import { Validator } from "./validate";

export const COSMETIC_CATEGORIES = ["suit", "trail", "frame", "title", "victory"] as const;
export type CosmeticCategory = (typeof COSMETIC_CATEGORIES)[number];

export interface CosmeticItem {
    id: string;
    category: string;
    look: string;
    name: string;
    default?: boolean;
    stars?: number | "all";
    achievement?: string;
}

const ITEMS: CosmeticItem[] = (catalog as { items: CosmeticItem[] }).items;
const BY_ID = new Map(ITEMS.map((i) => [i.id, i]));
const DEFAULTS = new Map(ITEMS.filter((i) => i.default).map((i) => [i.category, i]));
/** Every star of the server's achievement catalogue (« Constellation »). */
const ALL_STARS = Object.values((achievementCatalog as { stars: Record<string, number> }).stars).reduce((s, v) => s + v, 0);
const ID_RE = /^[a-z0-9_]{1,64}$/;

export type Equipped = Record<CosmeticCategory, string>;

/** What the declared achievements unlock: their stars (counted by the server) and their ids. */
export interface CosmeticProgress {
    stars: number;
    ids: Set<string>;
}

export function defaultItem(category: CosmeticCategory): CosmeticItem {
    return DEFAULTS.get(category)!;
}

export function unlocked(item: CosmeticItem, p: CosmeticProgress): boolean {
    if (item.default) return true;
    if (item.achievement) return p.ids.has(item.achievement);
    if (item.stars === "all") return ALL_STARS > 0 && p.stars >= ALL_STARS;
    return p.stars >= (item.stars ?? Number.MAX_SAFE_INTEGER);
}

/** The item a category shows: the id if known in this category, else the default. */
function shown(category: CosmeticCategory, id: string | null | undefined): CosmeticItem {
    const i = id ? BY_ID.get(id) : undefined;
    return i && i.category === category ? i : defaultItem(category);
}

/** What may be worn: unknown (newer game) or locked ids give way to the default. */
export function effective(wanted: Partial<Record<CosmeticCategory, string>>, p: CosmeticProgress): Equipped {
    const out = {} as Equipped;
    for (const c of COSMETIC_CATEGORIES) {
        const i = wanted[c] ? BY_ID.get(wanted[c]!) : undefined;
        out[c] = i && i.category === c && unlocked(i, p) ? i.id : defaultItem(c).id;
    }
    return out;
}

export async function progressOf(ctx: Ctx, playerId: string): Promise<CosmeticProgress> {
    const r = await ctx.env.DB.prepare("SELECT ids, stars FROM player_achievements WHERE player_id = ?1").bind(playerId).first<{ ids: string; stars: number }>();
    let ids: unknown = [];
    try {
        ids = JSON.parse(r?.ids ?? "[]");
    } catch {
        ids = [];
    }
    return { stars: r?.stars ?? 0, ids: new Set(Array.isArray(ids) ? ids.filter((v): v is string => typeof v === "string") : []) };
}

type StoredRow = Record<CosmeticCategory, string>;

async function stored(ctx: Ctx, playerId: string): Promise<StoredRow | null> {
    return ctx.env.DB.prepare("SELECT suit, trail, frame, title, victory FROM player_cosmetics WHERE player_id = ?1").bind(playerId).first<StoredRow>();
}

async function store(ctx: Ctx, playerId: string, e: Equipped): Promise<void> {
    await ctx.env.DB.prepare(
        `INSERT INTO player_cosmetics (player_id, suit, trail, frame, title, victory, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
         ON CONFLICT (player_id) DO UPDATE SET suit = excluded.suit, trail = excluded.trail, frame = excluded.frame,
           title = excluded.title, victory = excluded.victory, updated_at = excluded.updated_at`,
    )
        .bind(playerId, e.suit, e.trail, e.frame, e.title, e.victory, nowIso())
        .run();
}

/** {suit: {id, look, name}, ...}: what the profile shows (defaults when nothing was sent). */
export function cosmeticsJson(row: Partial<StoredRow> | null) {
    const out = {} as Record<CosmeticCategory, { id: string; look: string; name: string }>;
    for (const c of COSMETIC_CATEGORIES) {
        const i = shown(c, row?.[c]);
        out[c] = { id: i.id, look: i.look, name: i.name };
    }
    return out;
}

export async function profileCosmetics(ctx: Ctx, playerId: string) {
    return cosmeticsJson(await stored(ctx, playerId));
}

/** PUT /players/me/cosmetics {suit, trail, frame, title, victory}: item ids. */
export async function putCosmetics(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.cosmetics, "player:" + me);
    const body = await readJsonObject(ctx, 1024);
    const v = new Validator(body, COSMETIC_CATEGORIES);
    const wanted: Partial<Record<CosmeticCategory, string>> = {};
    for (const c of COSMETIC_CATEGORIES) wanted[c] = v.str(c, { min: 1, max: 64, pattern: ID_RE });
    v.done();
    const e = effective(wanted, await progressOf(ctx, me));
    await store(ctx, me, e);
    return json(ctx, { cosmetics: cosmeticsJson(e) });
}

/**
 * After the declared achievements changed: what they no longer unlock goes back to the
 * default (never the other way: the game sends its choice again).
 */
export async function revalidateCosmetics(ctx: Ctx, playerId: string): Promise<void> {
    const row = await stored(ctx, playerId);
    if (!row) return;
    const e = effective(row, await progressOf(ctx, playerId));
    if (COSMETIC_CATEGORIES.some((c) => e[c] !== row[c])) await store(ctx, playerId, e);
}

/** In a leaderboard entry's `player`: the look of the frame and the suit, the title. */
export interface BoardCosmetics {
    frame: string;
    title: { id: string; name: string } | null;
    suit: string;
}

/**
 * The leaderboard decorations of some players. A private profile gets nothing (the game
 * then draws the simple frame and no title).
 */
export async function boardCosmetics(ctx: Ctx, playerIds: Iterable<string>): Promise<Map<string, BoardCosmetics>> {
    const ids = [...new Set(playerIds)];
    const out = new Map<string, BoardCosmetics>();
    for (let at = 0; at < ids.length; at += 90) {
        const chunk = ids.slice(at, at + 90);
        const rows = await ctx.env.DB.prepare(
            `SELECT p.id, c.suit, c.frame, c.title FROM players p LEFT JOIN player_cosmetics c ON c.player_id = p.id
             WHERE p.profile_visibility <> 'private' AND p.id IN (${chunk.map((_, i) => `?${i + 1}`).join(", ")})`,
        )
            .bind(...chunk)
            .all<{ id: string; suit: string | null; frame: string | null; title: string | null }>();
        for (const r of rows.results) {
            const title = shown("title", r.title);
            out.set(r.id, {
                frame: shown("frame", r.frame).look,
                title: title.default || !title.look ? null : { id: title.id, name: title.name },
                suit: shown("suit", r.suit).look,
            });
        }
    }
    return out;
}

/** `player` with its decorations when the profile is visible. */
export function withCosmetics<T extends { id: string }>(player: T, decorations: Map<string, BoardCosmetics>): T & Partial<BoardCosmetics> {
    const d = decorations.get(player.id);
    return d ? { ...player, ...d } : player;
}
