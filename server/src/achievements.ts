// Achievements declared by the game (2.18.0). They are unlocked on the player's computer
// and cannot be verified here: the profile shows them as « déclarés par le jeu ». The server
// only keeps the ids it knows and counts the stars from its own copy of the catalogue.
// 2.46.1: a game that keeps its achievements by account (each one belongs to the account that
// was connected when it was unlocked; those unlocked without an account to the first account
// connected afterwards) sends `merge: true`: its list is ADDED to the account's, so the lists of
// every computer of the account add up. An older game, whose list is the whole computer's
// whatever the account, still replaces it (adding it up would mix two accounts of a computer
// for good). An id the catalogue no longer has is dropped.
import catalog from "./achievementCatalog.json";
import { requireAuth } from "./auth";
import { revalidateCosmetics } from "./cosmetics";
import { Errors, json, readJsonObject, type Ctx } from "./http";
import { claimedAchievements, RAID_ACHIEVEMENTS } from "./raidRewards";
import { rateLimit, RULES } from "./ratelimit";

const STARS: Record<string, number> = (catalog as { stars: Record<string, number> }).stars;
const ID_RE = /^[a-z0-9_]{1,64}$/;
export const TOTAL_STARS = Object.values(STARS).reduce((s, v) => s + v, 0);
export const TOTAL_COUNT = Object.keys(STARS).length;

export interface AchievementSummary {
    stars: number;
    count: number;
    total_stars: number;
    total_count: number;
    declared: true; // unlocked on the player's computer, not verified by the server
    updated_at: string | null;
}

export async function achievementSummary(ctx: Ctx, playerId: string): Promise<AchievementSummary> {
    const r = await ctx.env.DB.prepare("SELECT stars, count, updated_at FROM player_achievements WHERE player_id = ?1")
        .bind(playerId)
        .first<{ stars: number; count: number; updated_at: string }>();
    return {
        stars: r?.stars ?? 0,
        count: r?.count ?? 0,
        total_stars: TOTAL_STARS,
        total_count: TOTAL_COUNT,
        declared: true,
        updated_at: r?.updated_at ?? null,
    };
}

/** PUT /players/me/achievements {unlocked: [ids], merge?: true}: replaces the declared list, or (merge) adds it to the account's. */
export async function putAchievements(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.achievements, "player:" + me);
    const body = await readJsonObject(ctx, 32 * 1024);
    const list = body.unlocked;
    if (body.merge !== undefined && typeof body.merge !== "boolean") throw Errors.validation([{ field: "merge", message: "booléen attendu" }]);
    if (!Array.isArray(list) || list.length > 1000) throw Errors.validation([{ field: "unlocked", message: "liste de 1000 identifiants au plus" }]);
    const known = new Set<string>();
    for (const v of list) {
        if (typeof v !== "string" || !ID_RE.test(v)) throw Errors.validation([{ field: "unlocked", message: "identifiants attendus" }]);
        if (STARS[v] !== undefined) known.add(v); // ids of a newer game: ignored until the server knows them
    }
    // What the account already had (from this computer or another one), still in the catalogue.
    const prev = body.merge === true
        ? await ctx.env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(me).first<{ ids: string }>()
        : null;
    if (prev) {
        try {
            const old = JSON.parse(prev.ids) as unknown;
            if (Array.isArray(old)) for (const v of old) if (typeof v === "string" && STARS[v] !== undefined) known.add(v);
        } catch {
            // a damaged row: the list declared now replaces it
        }
    }
    // 2.67: a badge of the raids is never the game's word: only one whose reward the server granted
    // and the player claimed (raidRewards.ts).
    if ([...known].some((id) => RAID_ACHIEVEMENTS[id] !== undefined)) {
        const allowed = await claimedAchievements(ctx.env.DB, me);
        for (const id of Object.keys(RAID_ACHIEVEMENTS)) if (!allowed.has(id)) known.delete(id);
    }
    const ids = [...known].sort();
    const stars = ids.reduce((s, id) => s + STARS[id]!, 0);
    const now = new Date().toISOString();
    await ctx.env.DB.prepare(
        `INSERT INTO player_achievements (player_id, ids, stars, count, updated_at) VALUES (?1, ?2, ?3, ?4, ?5)
         ON CONFLICT (player_id) DO UPDATE SET ids = excluded.ids, stars = excluded.stars, count = excluded.count, updated_at = excluded.updated_at`,
    )
        .bind(me, JSON.stringify(ids), stars, ids.length, now)
        .run();
    // Fewer achievements (an older game's list, or ids the catalogue dropped): what they no longer unlock is taken off (frame, title, suit...).
    await revalidateCosmetics(ctx, me);
    return json(ctx, { achievements: await achievementSummary(ctx, me) });
}
