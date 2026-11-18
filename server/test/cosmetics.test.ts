import { env } from "cloudflare:test";
import { RAID_ACHIEVEMENTS } from "../src/raidRewards";
import { beforeEach, describe, expect, it } from "vitest";
import achievementCatalog from "../src/achievementCatalog.json";
import catalog from "../src/cosmeticCatalog.json";
import game from "../../data/cosmetics/cosmetics.json";
import { LEVEL2_HASH, api, registerPlayer, seedAll, verifiedRun, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const STARS = (achievementCatalog as { stars: Record<string, number> }).stars;
const ALL_IDS = Object.keys(STARS);
const DEFAULTS = { suit: "suit_standard", trail: "trail_none", frame: "frame_simple", title: "title_none", victory: "victory_classic" };

/** Achievement ids worth at least `stars` stars (plus `extra`), never those in `without`. */
function worth(stars: number, extra: string[] = [], without: string[] = []): string[] {
    const out = [...extra];
    let n = extra.reduce((s, id) => s + STARS[id]!, 0);
    for (const id of ALL_IDS) {
        if (n >= stars) break;
        if (out.includes(id) || without.includes(id)) continue;
        out.push(id);
        n += STARS[id]!;
    }
    return out;
}

async function declare(p: TestPlayer, ids: string[]) {
    const r = await api("PUT", "/api/v1/players/me/achievements", { token: p.token, body: { unlocked: ids } });
    expect(r.status).toBe(200);
}

async function wear(p: TestPlayer, items: Partial<typeof DEFAULTS>) {
    return api("PUT", "/api/v1/players/me/cosmetics", { token: p.token, body: { ...DEFAULTS, ...items } });
}

const ids = (c: Record<string, { id: string }>) => Object.fromEntries(Object.entries(c).map(([k, v]) => [k, v.id]));

describe("cosmetics worn online", () => {
    it("needs an account and refuses malformed input", async () => {
        const a = await registerPlayer("Nova");
        expect((await api("PUT", "/api/v1/players/me/cosmetics", { body: DEFAULTS })).status).toBe(401);
        const { suit: _suit, ...noSuit } = DEFAULTS;
        for (const bad of [
            noSuit,
            { ...DEFAULTS, suit: 3 },
            { ...DEFAULTS, suit: "../x" },
            { ...DEFAULTS, suit: "Suit_Givre" },
            { ...DEFAULTS, frame: "" },
            { ...DEFAULTS, title: "a".repeat(65) },
            { ...DEFAULTS, title: "Le meilleur joueur du monde" },
            { ...DEFAULTS, player_id: a.id },
        ])
            expect((await api("PUT", "/api/v1/players/me/cosmetics", { token: a.token, body: bad })).status).toBe(400);
        // A small body only.
        const big = await api("PUT", "/api/v1/players/me/cosmetics", { token: a.token, raw: JSON.stringify({ ...DEFAULTS, pad: "x".repeat(2000) }), headers: { "content-type": "application/json" } });
        expect(big.status).toBe(413);
    });

    it("keeps what the declared achievements unlock, puts the default instead of locked or unknown ids", async () => {
        const a = await registerPlayer("Nova");
        // Nothing declared yet: everything locked comes back as the default.
        const locked = await wear(a, { suit: "suit_givre", frame: "frame_gold", title: "title_explorateur", trail: "trail_comet" });
        expect(locked.status).toBe(200);
        expect(ids(locked.body.data.cosmetics)).toEqual(DEFAULTS);
        // 120 stars with « voyage_5 »: gold frame, the Explorer title, the Frost suit.
        await declare(a, worth(120, ["voyage_5"], ["gardiens_echoes"]));
        const ok = await wear(a, { suit: "suit_givre", frame: "frame_gold", title: "title_explorateur", trail: "trail_comet", victory: "victory_supernova" });
        expect(ok.status).toBe(200);
        // Supernova needs 220 stars: still locked.
        expect(ids(ok.body.data.cosmetics)).toEqual({ suit: "suit_givre", trail: "trail_comet", frame: "frame_gold", title: "title_explorateur", victory: "victory_classic" });
        expect(ok.body.data.cosmetics.frame).toEqual({ id: "frame_gold", look: "gold", name: "Or" });
        expect(ok.body.data.cosmetics.title).toEqual({ id: "title_explorateur", look: "Explorateur", name: "Explorateur" });
        // Unlocked by an achievement that was not declared.
        const echo = await wear(a, { suit: "suit_echo" });
        expect(echo.body.data.cosmetics.suit.id).toBe("suit_standard");
        // Ids of a newer game, or of another category: the default, never stored as sent.
        const unknown = await wear(a, { suit: "suit_futur", frame: "title_explorateur", trail: "trail_comet" });
        expect(unknown.status).toBe(200);
        expect(ids(unknown.body.data.cosmetics)).toMatchObject({ suit: "suit_standard", frame: "frame_simple", trail: "trail_comet" });
        const prof = await api("GET", `/api/v1/players/${a.id}`);
        expect(ids(prof.body.data.cosmetics)).toMatchObject({ suit: "suit_standard", frame: "frame_simple", trail: "trail_comet" });
    });

    it("« all » needs every star of the server's catalogue", async () => {
        const a = await registerPlayer("Nova");
        await declare(a, ALL_IDS.slice(1));
        expect((await wear(a, { frame: "frame_constellation" })).body.data.cosmetics.frame.id).toBe("frame_simple");
        // 2.68: the raid's badges (communaute_pyroclast; 2.69: those of Voidworm, Graviton, Titanus) are declared
        // only once their reward is claimed (raidRewards.ts).
        for (const reward of Object.values(RAID_ACHIEVEMENTS))
            await env.DB.prepare("INSERT INTO raid_reward_grants (player_id, reward, event_id, reason, granted_at, claimed_at) VALUES (?1, ?2, 'ev', 'test', ?3, ?3)")
                .bind(a.id, reward, new Date().toISOString())
                .run();
        await declare(a, ALL_IDS);
        const r = await wear(a, { frame: "frame_constellation", suit: "suit_echo", title: "title_nouveau_maitre" });
        expect(ids(r.body.data.cosmetics)).toMatchObject({ frame: "frame_constellation", suit: "suit_echo", title: "title_nouveau_maitre" });
    });

    it("takes off what fewer declared achievements no longer unlock", async () => {
        const a = await registerPlayer("Nova");
        await declare(a, worth(120, ["voyage_5"]));
        await wear(a, { suit: "suit_givre", frame: "frame_gold", title: "title_explorateur", trail: "trail_stardust" });
        // Down to 30 stars without « voyage_5 »: the gold frame and the title go, the rest stays.
        await declare(a, worth(30, [], ["voyage_5"]));
        const prof = await api("GET", `/api/v1/players/${a.id}`);
        expect(ids(prof.body.data.cosmetics)).toEqual({ ...DEFAULTS, suit: "suit_givre", trail: "trail_stardust" });
        // More achievements again do not put anything back by themselves (the game sends its choice).
        await declare(a, worth(120, ["voyage_5"]));
        expect((await api("GET", `/api/v1/players/${a.id}`)).body.data.cosmetics.frame.id).toBe("frame_simple");
    });

    it("shows on the profile, defaults for a player who never sent anything, hidden by a private profile", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        expect(ids((await api("GET", `/api/v1/players/${b.id}`)).body.data.cosmetics)).toEqual(DEFAULTS);
        await declare(a, worth(60, ["voyage_5"]));
        await wear(a, { suit: "suit_seve", frame: "frame_silver", title: "title_explorateur" });
        const pub = await api("GET", `/api/v1/players/${a.id}`, { token: b.token });
        expect(pub.body.data.cosmetics.suit).toEqual({ id: "suit_seve", look: "seve", name: "Sève" });
        await api("PATCH", "/api/v1/players/me", { token: a.token, body: { profile_visibility: "private" } });
        expect((await api("GET", `/api/v1/players/${a.id}`, { token: b.token })).body.data.cosmetics).toBeNull();
        expect((await api("GET", `/api/v1/players/${a.id}`)).body.data.cosmetics).toBeNull();
        // The owner still sees what they wear.
        const self = await api("GET", `/api/v1/players/${a.id}`, { token: a.token });
        expect(self.body.data.cosmetics.frame.id).toBe("frame_silver");
        // In the data export.
        const exp = await api("GET", "/api/v1/players/me/export", { token: a.token });
        expect(exp.body.data.cosmetics[0]).toMatchObject({ suit: "suit_seve", frame: "frame_silver", title: "title_explorateur" });
    });

    it("every leaderboard entry carries the frame, the title and the suit, except for a private profile", async () => {
        const gold = await registerPlayer("Amelie");
        const plain = await registerPlayer("Bastien");
        const hidden = await registerPlayer("Carter");
        await declare(gold, worth(120, ["voyage_5"]));
        await wear(gold, { suit: "suit_givre", frame: "frame_gold", title: "title_explorateur" });
        await declare(hidden, worth(120, ["voyage_5"]));
        await wear(hidden, { suit: "suit_givre", frame: "frame_gold", title: "title_explorateur" });
        await api("PATCH", "/api/v1/players/me", { token: hidden.token, body: { profile_visibility: "private" } });
        for (const [p, t] of [[gold, 61000], [plain, 62000], [hidden, 60000]] as const) {
            await verifiedRun(p, t);
            await verifiedRun(p, t + 30000, { level_id: "test_02", level_hash: LEVEL2_HASH });
        }
        const check = (entries: { player: Record<string, unknown> }[]) => {
            const by = Object.fromEntries(entries.map((e) => [e.player.username, e.player]));
            expect(by.Amelie).toMatchObject({ frame: "gold", title: { id: "title_explorateur", name: "Explorateur" }, suit: "givre" });
            expect(by.Bastien).toMatchObject({ frame: "simple", title: null, suit: "standard" });
            expect(by.Carter).toBeDefined();
            for (const k of ["frame", "title", "suit"]) expect(by.Carter).not.toHaveProperty(k);
        };
        for (const path of [
            "/api/v1/leaderboards/level/test_01",
            "/api/v1/leaderboards/global?level_id=test_01",
            "/api/v1/leaderboards/weekly?level_id=test_01",
            "/api/v1/leaderboards/global",
            "/api/v1/leaderboards/planet/testworld",
        ]) {
            const r = await api("GET", path, { token: gold.token });
            expect(r.status).toBe(200);
            check(r.body.data.entries);
            expect(r.body.data.me.entry.player).toMatchObject({ frame: "gold", suit: "givre" });
        }
        const friends = await api("GET", "/api/v1/leaderboards/friends?level_id=test_01", { token: plain.token });
        expect(friends.body.data.entries[0].player).toMatchObject({ frame: "simple", title: null });
        // Even its owner's private entry shows nothing: it is what the others see.
        const own = await api("GET", "/api/v1/leaderboards/level/test_01", { token: hidden.token });
        expect(own.body.data.me.entry.player).not.toHaveProperty("frame");
    });

    it("the server's catalogue is the game's", () => {
        type GameItem = {
            id: string;
            category: string;
            name: string;
            look?: string;
            default?: boolean;
            stars?: number | "all";
            achievement?: string;
            challenge?: unknown;
            challengeStreak?: unknown;
            souvenir?: string;
            raid?: string;
        };
        // 2.64: the souvenirs (given on the player's computer) are not the server's.
        const items = (game as unknown as { items: GameItem[] }).items.filter((i) => !i.souvenir).map((i) => {
            const o: Record<string, unknown> = { id: i.id, category: i.category, look: i.look ?? "", name: i.name };
            if (i.default) o.default = true;
            else if (i.achievement) o.achievement = i.achievement;
            else if (i.challenge) o.challenge = i.challenge;
            else if (i.challengeStreak) o.challengeStreak = i.challengeStreak;
            else if (i.raid) o.raid = i.raid; // 2.68: a raid's reward (player_won_rewards once claimed)
            else o.stars = i.stars;
            return o;
        });
        expect((catalog as unknown as { items: unknown[] }).items).toEqual(items);
        // Every achievement that unlocks something exists on the server.
        for (const i of (catalog as { items: GameItem[] }).items) if (i.achievement) expect(STARS[i.achievement]).toBeGreaterThan(0);
    });
});
