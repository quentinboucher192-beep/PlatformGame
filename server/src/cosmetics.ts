// What a player wears online (2.23), chosen in the game's Collection: the avatar frame and the
// title follow them on their profile and in the leaderboards, the suit on their profile and
// their ghosts. The server never trusts the game: each id must exist in its own catalogue
// (src/cosmeticCatalog.json, generated from the game's data) and be unlocked by the
// achievements the player declared (player_achievements) or, from 2.35, by the challenge
// results this server verified itself (challenge_runs, challengeRewards.ts: never the game's
// word), or (2.43) won as the reward of one challenge (player_won_rewards). From 2.43 a title
// may also be one an administrator created on the server ("srv:<id>"), worn only by the
// players who won it. Anything else becomes the category's default before it is stored; names
// shown are always the catalogue's (or the server title's).
import achievementCatalog from "./achievementCatalog.json";
import catalog from "./cosmeticCatalog.json";
import {
    challengeRewardProgress,
    resultsMet,
    streakMet,
    titleIdOf,
    wonRewards,
    type ChallengeRewardProgress,
    type ResultsCondition,
    type StreakCondition,
    type WonRewards,
} from "./challengeRewards";
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
    /** 2.35: challenges with a verified run at a tier or better. */
    challenge?: ResultsCondition;
    /** 2.35: periods in a row with a verified run received in time. */
    challengeStreak?: StreakCondition;
}

const ITEMS: CosmeticItem[] = (catalog as unknown as { items: CosmeticItem[] }).items;
const BY_ID = new Map(ITEMS.map((i) => [i.id, i]));
const DEFAULTS = new Map(ITEMS.filter((i) => i.default).map((i) => [i.category, i]));
/** Every star of the server's achievement catalogue (« Constellation »). */
const ALL_STARS = Object.values((achievementCatalog as { stars: Record<string, number> }).stars).reduce((s, v) => s + v, 0);
/** An item of the catalogue, or (2.43) a title created on the server. */
const ID_RE = /^(?:[a-z0-9_]{1,64}|srv:[1-9][0-9]{0,9})$/;

export type Equipped = Record<CosmeticCategory, string>;

/**
 * What unlocks the rewards: the declared achievements (their stars, counted by the server, and
 * their ids) and the challenge results the server verified (none known: those stay locked).
 */
export interface CosmeticProgress {
    stars: number;
    ids: Set<string>;
    challenges?: ChallengeRewardProgress;
    /** 2.43: the rewards of single challenges won (never taken back). */
    won?: WonRewards;
}

export function defaultItem(category: CosmeticCategory): CosmeticItem {
    return DEFAULTS.get(category)!;
}

export function unlocked(item: CosmeticItem, p: CosmeticProgress): boolean {
    if (item.default) return true;
    if (p.won?.items.has(item.id)) return true;
    if (item.achievement) return p.ids.has(item.achievement);
    if (item.challenge) return !!p.challenges && resultsMet(p.challenges, item.challenge);
    if (item.challengeStreak) return !!p.challenges && streakMet(p.challenges, item.challengeStreak);
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
        // 2.43: a server title, only when won.
        if (c === "title" && titleIdOf(wanted.title) !== null && p.won?.titles.has(wanted.title!)) out.title = wanted.title!;
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
    return {
        stars: r?.stars ?? 0,
        ids: new Set(Array.isArray(ids) ? ids.filter((v): v is string => typeof v === "string") : []),
        challenges: await challengeRewardProgress(ctx, playerId),
        won: await wonRewards(ctx, playerId),
    };
}

/** A title created on the server ("srv:<id>"), worn: its text and its colour. */
export interface ServerTitleLook {
    id: string;
    text: string;
    color: string;
}

/** The server titles among `refs` ("srv:<id>"; anything else ignored), by reference. */
export async function serverTitles(ctx: Ctx, refs: Iterable<string | null | undefined>): Promise<Map<string, ServerTitleLook>> {
    const ids = [...new Set([...refs].map(titleIdOf).filter((i): i is number => i !== null))];
    const out = new Map<string, ServerTitleLook>();
    if (ids.length === 0) return out;
    const rows = await ctx.env.DB.prepare("SELECT id, text, color FROM server_titles WHERE id IN (SELECT value FROM json_each(?1))")
        .bind(JSON.stringify(ids))
        .all<{ id: number; text: string; color: string }>();
    for (const r of rows.results) out.set(`srv:${r.id}`, { id: `srv:${r.id}`, text: r.text, color: r.color });
    return out;
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

/**
 * {suit: {id, look, name}, ...}: what the profile shows (defaults when nothing was sent). A
 * server title (2.43): `{ id: "srv:<n>", look: its text, name: its text, color }` (`titles`:
 * the server titles known; one that is not falls back to the default).
 */
export function cosmeticsJson(row: Partial<StoredRow> | null, titles: Map<string, ServerTitleLook> = new Map()) {
    const out = {} as Record<CosmeticCategory, { id: string; look: string; name: string; color?: string }>;
    for (const c of COSMETIC_CATEGORIES) {
        const t = c === "title" ? titles.get(row?.title ?? "") : undefined;
        if (t) {
            out[c] = { id: t.id, look: t.text, name: t.text, color: t.color };
            continue;
        }
        const i = shown(c, row?.[c]);
        out[c] = { id: i.id, look: i.look, name: i.name };
    }
    return out;
}

export async function profileCosmetics(ctx: Ctx, playerId: string) {
    const row = await stored(ctx, playerId);
    return cosmeticsJson(row, await serverTitles(ctx, [row?.title]));
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
    return json(ctx, { cosmetics: cosmeticsJson(e, await serverTitles(ctx, [e.title])) });
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

/** In a leaderboard entry's `player`: the look of the frame and the suit, the title (2.43: a server title's colour). */
export interface BoardCosmetics {
    frame: string;
    title: { id: string; name: string; color?: string } | null;
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
            `SELECT p.id, c.suit, c.frame, c.title, t.text AS title_text, t.color AS title_color
             FROM players p LEFT JOIN player_cosmetics c ON c.player_id = p.id
             LEFT JOIN server_titles t ON c.title LIKE 'srv:%' AND t.id = CAST(substr(c.title, 5) AS INTEGER)
             WHERE p.profile_visibility <> 'private' AND p.id IN (${chunk.map((_, i) => `?${i + 1}`).join(", ")})`,
        )
            .bind(...chunk)
            .all<{ id: string; suit: string | null; frame: string | null; title: string | null; title_text: string | null; title_color: string | null }>();
        for (const r of rows.results) {
            const title = shown("title", r.title);
            out.set(r.id, {
                frame: shown("frame", r.frame).look,
                // 2.43: a server title shows its text in its colour.
                title:
                    r.title_text && r.title
                        ? { id: r.title, name: r.title_text, color: r.title_color ?? "gold" }
                        : title.default || !title.look
                          ? null
                          : { id: title.id, name: title.name },
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
