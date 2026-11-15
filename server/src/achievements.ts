// Achievements declared by the game (2.18.0). They are unlocked on the player's computer
// and cannot be verified here: the profile shows them as « déclarés par le jeu ». The server
// only keeps the ids it knows and counts the stars from its own copy of the catalogue.
import catalog from "./achievementCatalog.json";
import { requireAuth } from "./auth";
import { revalidateCosmetics } from "./cosmetics";
import { Errors, json, readJsonObject, type Ctx } from "./http";
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

/** PUT /players/me/achievements {unlocked: [ids]}: replaces the declared list. */
export async function putAchievements(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.achievements, "player:" + me);
    const body = await readJsonObject(ctx, 32 * 1024);
    const list = body.unlocked;
    if (!Array.isArray(list) || list.length > 1000) throw Errors.validation([{ field: "unlocked", message: "liste de 1000 identifiants au plus" }]);
    const known = new Set<string>();
    for (const v of list) {
        if (typeof v !== "string" || !ID_RE.test(v)) throw Errors.validation([{ field: "unlocked", message: "identifiants attendus" }]);
        if (STARS[v] !== undefined) known.add(v); // ids of a newer game: ignored until the server knows them
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
    // Fewer achievements: what they no longer unlock is taken off (frame, title, suit...).
    await revalidateCosmetics(ctx, me);
    return json(ctx, { achievements: await achievementSummary(ctx, me) });
}
