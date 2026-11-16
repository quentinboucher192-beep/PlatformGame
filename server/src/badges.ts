// Profile badges (2.41): achievements a player chose in the game's Collection, shown on their
// profile next to their name and title. The server never trusts the game: each id must be an
// achievement of its own catalogue (src/achievementCatalog.json, generated from the game's
// data) that the player declared (player_achievements), each once, and there may be no more
// of them than the slots opened by the stars the server counts itself from those achievements
// (the steps `badgeSlots` of the same catalogue: the game's numbers). The choice is kept in
// its order; what a profile shows is checked again on every reading, so that an achievement
// no longer declared (or no longer in the catalogue) and stars below a step leave only the
// valid badges.
import catalog from "./achievementCatalog.json";
import { requireAuth } from "./auth";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { rateLimit, RULES } from "./ratelimit";
import { Validator } from "./validate";

interface BadgeStep {
    stars: number;
    slots: number;
}

const CATALOG = catalog as unknown as { stars: Record<string, number>; badgeSlots?: BadgeStep[] };
const STARS: Record<string, number> = CATALOG.stars;
/** From `stars` stars, `slots` badges (increasing steps, as in the game). */
export const BADGE_STEPS: readonly BadgeStep[] = [...(CATALOG.badgeSlots ?? [])].sort((a, b) => a.stars - b.stars);
export const MAX_BADGES = BADGE_STEPS.reduce((m, s) => Math.max(m, s.slots), 0);
const ID_RE = /^[a-z0-9_]{1,64}$/;

/** Badges a player may show with `stars` stars (0 below the first step). */
export function badgeSlots(stars: number): number {
    let n = 0;
    for (const s of BADGE_STEPS) if (stars >= s.stars) n = s.slots;
    return n;
}

/** The achievements of the catalogue a player declared, and their stars (counted here). */
export interface DeclaredAchievements {
    ids: Set<string>;
    stars: number;
}

function parseIds(text: string | null | undefined): string[] {
    let v: unknown = [];
    try {
        v = JSON.parse(text ?? "[]");
    } catch {
        v = [];
    }
    return Array.isArray(v) ? v.filter((x): x is string => typeof x === "string") : [];
}

export async function declaredAchievements(ctx: Ctx, playerId: string): Promise<DeclaredAchievements> {
    const r = await ctx.env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(playerId).first<{ ids: string }>();
    const ids = new Set(parseIds(r?.ids).filter((id) => STARS[id] !== undefined));
    let stars = 0;
    for (const id of ids) stars += STARS[id]!;
    return { ids, stars };
}

/** What may be shown of `wanted`, in its order: known, declared, each once, within the slots. */
export function validBadges(wanted: readonly string[], d: DeclaredAchievements): string[] {
    const slots = badgeSlots(d.stars);
    const out: string[] = [];
    for (const id of wanted) {
        if (out.length >= slots) break;
        if (STARS[id] === undefined || !d.ids.has(id) || out.includes(id)) continue;
        out.push(id);
    }
    return out;
}

/** [{id, stars}]: the game draws each badge from its own catalogue (name, category). */
export function badgesJson(ids: readonly string[]) {
    return ids.map((id) => ({ id, stars: STARS[id] ?? 0 }));
}

async function stored(ctx: Ctx, playerId: string): Promise<string[]> {
    const r = await ctx.env.DB.prepare("SELECT ids FROM player_badges WHERE player_id = ?1").bind(playerId).first<{ ids: string }>();
    return parseIds(r?.ids);
}

/** The badges a profile shows now (only those still valid). */
export async function profileBadges(ctx: Ctx, playerId: string) {
    return badgesJson(validBadges(await stored(ctx, playerId), await declaredAchievements(ctx, playerId)));
}

/** PUT /players/me/badges {badges: [achievement ids]}: replaces the badges of the profile. */
export async function putBadges(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.badges, "player:" + me);
    const body = await readJsonObject(ctx, 2048);
    const v = new Validator(body, ["badges"]);
    const list = body.badges;
    if (!Array.isArray(list)) v.fail("badges", "liste d'identifiants de succès attendue");
    v.done();
    const ids = list as unknown[];
    if (ids.length > MAX_BADGES) throw Errors.validation([{ field: "badges", message: `${MAX_BADGES} badges au plus` }]);
    for (const id of ids)
        if (typeof id !== "string" || !ID_RE.test(id)) throw Errors.validation([{ field: "badges", message: "identifiants de succès attendus" }]);
    const wanted = ids as string[];
    if (new Set(wanted).size !== wanted.length) throw Errors.validation([{ field: "badges", message: "un même succès ne peut être affiché qu'une fois" }]);
    // Only achievements of this server's catalogue that this player declared.
    const d = await declaredAchievements(ctx, me);
    const foreign = wanted.filter((id) => STARS[id] === undefined || !d.ids.has(id));
    if (foreign.length)
        throw new ApiError(403, "BADGE_NOT_UNLOCKED", "Un badge doit être un succès obtenu et déclaré par votre jeu.", { badges: foreign });
    // As many as the stars counted here open.
    const slots = badgeSlots(d.stars);
    if (wanted.length > slots) {
        const first = BADGE_STEPS[0]?.stars ?? 0;
        throw new ApiError(
            403,
            "BADGE_SLOTS",
            slots === 0 ? `Les badges du profil s'ouvrent à ${first} étoiles (${d.stars} déclarées).` : `${slots} badges au plus avec ${d.stars} étoiles.`,
            { slots, stars: d.stars },
        );
    }
    await ctx.env.DB.prepare(
        `INSERT INTO player_badges (player_id, ids, updated_at) VALUES (?1, ?2, ?3)
         ON CONFLICT (player_id) DO UPDATE SET ids = excluded.ids, updated_at = excluded.updated_at`,
    )
        .bind(me, JSON.stringify(wanted), nowIso())
        .run();
    return json(ctx, { badges: badgesJson(wanted), slots, stars: d.stars });
}
