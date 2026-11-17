// 2.59 ADMINISTRATION › Créations: besides the suits, the trails (two colours, a style, a width,
// a length in time, sparkles), the avatar frames (border, glow, a style) and the victory
// effects (main, second, a style). For each kind: created, edited and archived by an admin
// only (the session decides, never an id sent by the game), strictly checked (#rrggbb colours,
// a style of its list, bounds), every change and refusal in the Journal; given as the reward
// of a challenge, granted after its end, worn only by its winners, and shown where its
// category shows (the profile; the boards: the frame, and the trail behind a ghost).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { addDays, grantDuePrizes, periodBounds, periodOf, utcDate } from "../src/challenges";
import { FRAME_STYLES, TRAIL_STYLES, VICTORY_STYLES } from "../src/creations";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

const V = "/api/v1";
const HASH = "2".repeat(64);
const RULES_HASH = "a".repeat(64);

beforeEach(seedAll);

type Kind = "trail" | "frame" | "victory";

/** A valid body of each kind (its look), and the category's default item (PUT /players/me/cosmetics). */
const LOOKS: Record<Kind, Record<string, unknown>> = {
    trail: { colors: { start: "#FFD75A", end: "#9646ff" }, style: "comet", width: 140, life_ms: 700, particles: true },
    frame: { colors: { border: "#C0C8D8", glow: "#5ad2ff" }, style: "orbit" },
    victory: { colors: { main: "#ff7696", second: "#6ed6ff" }, style: "fireworks" },
};
const LOWER: Record<Kind, Record<string, unknown>> = {
    trail: { colors: { start: "#ffd75a", end: "#9646ff" }, style: "comet", width: 140, life_ms: 700, particles: true },
    frame: { colors: { border: "#c0c8d8", glow: "#5ad2ff" }, style: "orbit" },
    victory: { colors: { main: "#ff7696", second: "#6ed6ff" }, style: "fireworks" },
};
const DEFAULTS = { suit: "suit_standard", trail: "trail_none", frame: "frame_simple", title: "title_none", victory: "victory_classic" };
const KINDS: Kind[] = ["trail", "frame", "victory"];

async function admin(name = "NovaRider"): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

const create = (a: TestPlayer, kind: string, name: string, look: Record<string, unknown>) =>
    api("POST", `${V}/admin/creations`, { token: a.token, body: { kind, name, ...look } });

async function journal(a: TestPlayer, kind: string): Promise<any[]> {
    const r = await api("GET", `${V}/admin/log?kind=${kind}`, { token: a.token });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    return r.body.data.entries;
}

const count = async (sql: string) => (await env.DB.prepare(sql).first<{ n: number }>())!.n;
const ctx = () => ({ env, requestId: "test", params: {}, url: new URL("https://test.invalid/"), ip: "test" }) as any;

/** A challenge inserted directly (its period may have begun or be over). */
async function challenge(date: string): Promise<string> {
    const p = periodBounds("daily", periodOf("daily", date))!;
    const id = `daily-${p.period}`;
    await env.DB.prepare(
        `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title,
           hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, created_at, updated_at)
         VALUES (?1, 'daily', ?2, ?3, ?4, 'admin', 'campaign', 'glacior_02', 1, ?5, 'Glacior', 3, 4, '[]', 1, 0, ?6, 90000, 70000, 1, 'speed', ?7, ?7)`,
    )
        .bind(id, p.period, p.start, p.end, HASH, RULES_HASH, new Date().toISOString())
        .run();
    return id;
}

async function run(who: TestPlayer, challengeId: string, tier: "bronze" | "silver" | "gold" = "gold") {
    await env.DB.prepare(
        `INSERT INTO challenge_runs (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash, game_version,
           tier, status, late, created_at) VALUES (?1, ?2, ?3, ?4, 60000, 0, 0, ?5, ?6, '2.59.0', ?7, 'VERIFIED', 0, ?8)`,
    )
        .bind(crypto.randomUUID(), challengeId, who.id, crypto.randomUUID(), HASH, RULES_HASH, tier, new Date().toISOString())
        .run();
}

describe("creations 2.59: trails, frames and victory effects in the administration", () => {
    it("are made by an administrator only: 404 for a player, 401 without a session, never by an id sent in the body", async () => {
        const p = await registerPlayer("Kestrel");
        for (const kind of KINDS) {
            expect((await create(p, kind, "Aurore", LOOKS[kind])).status).toBe(404);
            expect((await api("POST", `${V}/admin/creations`, { body: { kind, name: "Aurore", ...LOOKS[kind] } })).status).toBe(401);
            // A player id in the body is no role: refused as an unknown field, and still not an admin.
            const forged = await api("POST", `${V}/admin/creations`, { token: p.token, body: { kind, name: "Aurore", ...LOOKS[kind], created_by: p.id } });
            expect(forged.status).toBe(404);
        }
        expect(await count("SELECT COUNT(*) AS n FROM creations")).toBe(0);
    });

    it("creates each kind with its colours (#rrggbb, lower case) and style, the reference of its kind, in the Journal", async () => {
        const a = await admin();
        const made: Record<string, any> = {};
        for (const kind of KINDS) {
            const r = await create(a, kind, "  Aurore   polaire ", LOOKS[kind]);
            expect(r.status, `${kind} ${JSON.stringify(r.body)}`).toBe(201);
            const c = r.body.data.creation;
            expect(c).toMatchObject({ kind, name: "Aurore polaire", ...LOWER[kind], archived: false, uses: 0, owners: 0, created_by: "NovaRider" });
            expect(c.ref).toBe(`srv${kind}:${c.id}`);
            made[kind] = c;
        }
        // The same name in another kind is fine; twice in one kind is not (accents and case aside).
        for (const kind of KINDS) {
            const dup = await create(a, kind, "AURORE POLAIRÉ", LOOKS[kind]);
            expect(dup.status).toBe(409);
            expect(dup.body.error.code).toBe("CREATION_EXISTS");
            expect(dup.body.error.details.creation.id).toBe(made[kind].id);
        }
        // The list gives the styles each kind accepts.
        const list = await api("GET", `${V}/admin/creations`, { token: a.token });
        expect(list.body.data.kinds).toEqual(["suit", "trail", "frame", "victory"]);
        expect(list.body.data.styles).toEqual({ trail: [...TRAIL_STYLES], frame: [...FRAME_STYLES], victory: [...VICTORY_STYLES] });
        expect(list.body.data.creations).toHaveLength(3);
        const log = await journal(a, "creation");
        for (const kind of KINDS) {
            const e = log.find((x) => x.action === "admin_creation_created" && x.creation?.kind === kind);
            expect(e.creation).toMatchObject({ ref: made[kind].ref, name: "Aurore polaire", ...LOWER[kind] });
        }
    });

    it("refuses what is not valid, kind by kind, and writes each refusal to the Journal", async () => {
        const a = await admin();
        const bad: [Kind | "suit", Record<string, unknown>][] = [
            // Trails.
            ["trail", { ...LOWER.trail, colors: { start: "#fff", end: "#000000" } }],
            ["trail", { ...LOWER.trail, colors: { start: "jaune", end: "#000000" } }],
            ["trail", { ...LOWER.trail, colors: { start: "#ffd75a" } }],
            ["trail", { ...LOWER.trail, colors: { suit: "#ffd75a", accent: "#000000", scarf: "#111111" } }],
            ["trail", { ...LOWER.trail, colors: { start: "#ffd75a", end: "#000000", glow: "#111111" } }],
            ["trail", { ...LOWER.trail, style: "rainbow" }],
            ["trail", { ...LOWER.trail, style: "fireworks" }],
            ["trail", { ...LOWER.trail, style: undefined }],
            ["trail", { ...LOWER.trail, width: 49 }],
            ["trail", { ...LOWER.trail, width: 201 }],
            ["trail", { ...LOWER.trail, width: 100.5 }],
            ["trail", { ...LOWER.trail, life_ms: 199 }],
            ["trail", { ...LOWER.trail, life_ms: 1001 }],
            ["trail", { ...LOWER.trail, particles: "oui" }],
            ["trail", { ...LOWER.trail, particles: undefined }],
            ["trail", { ...LOWER.trail, extra: 1 }],
            // Frames.
            ["frame", { ...LOWER.frame, colors: { border: "#c0c8d8", glow: "#5ad2fg" } }],
            ["frame", { ...LOWER.frame, colors: { start: "#c0c8d8", end: "#5ad2ff" } }],
            ["frame", { ...LOWER.frame, style: "crown" }],
            ["frame", { ...LOWER.frame, style: "Metal" }],
            ["frame", { ...LOWER.frame, width: 100 }],
            // Victory effects.
            ["victory", { ...LOWER.victory, colors: { main: "#ff7696", second: 0xffffff } }],
            ["victory", { ...LOWER.victory, colors: "#ff7696" }],
            ["victory", { ...LOWER.victory, style: "confetti" }],
            ["victory", { ...LOWER.victory, particles: true }],
            // A suit never takes a style.
            ["suit", { colors: { suit: "#c8e1ff", accent: "#78c8ff", scarf: "#ebf5ff" }, style: "comet" }],
        ];
        for (const [kind, look] of bad) {
            const r = await create(a, kind, "Correcte", look);
            expect(r.status, `${kind} ${JSON.stringify(look)}`).toBe(400);
            expect(r.body.error.code).toBe("VALIDATION_ERROR");
        }
        // A name too long or with an emoji, an unknown kind.
        expect((await create(a, "frame", "x".repeat(33), LOOKS.frame)).status).toBe(400);
        expect((await create(a, "victory", "Étoile ⭐", LOOKS.victory)).status).toBe(400);
        expect((await create(a, "title", "Correcte", LOOKS.frame)).status).toBe(400);
        expect(await count("SELECT COUNT(*) AS n FROM creations")).toBe(0);
        const refused = (await journal(a, "refused")).filter((e) => e.reason === "VALIDATION_ERROR" && e.creation?.action === "create");
        expect(refused.length).toBeGreaterThanOrEqual(bad.length + 3);
        expect(refused.some((e) => e.creation.kind === "trail" && e.creation.width === null && e.creation.style === "comet")).toBe(true);
        // The database refuses a style outside its rules too (even written by hand).
        await expect(
            env.DB.prepare(
                "INSERT INTO creations (kind, name, name_key, color_a, color_b, style, created_at, updated_at) VALUES ('frame', 'X', 'x', '#000000', '#ffffff', 'Bad Style', 'n', 'n')",
            ).run(),
        ).rejects.toThrow();
        await expect(
            env.DB.prepare(
                "INSERT INTO creations (kind, name, name_key, color_a, color_b, style, created_at, updated_at) VALUES ('trail', 'X', 'x', '#000000', '#ffffff', 'comet', 'n', 'n')",
            ).run(),
        ).rejects.toThrow();
    });

    it("edits each kind (its kind never changes), refuses a stale or invalid edit, archives and restores", async () => {
        const a = await admin();
        const edited: Record<Kind, Record<string, unknown>> = {
            trail: { colors: { start: "#ffffff", end: "#000000" }, style: "bubbles", width: 60, life_ms: 300, particles: false },
            frame: { colors: { border: "#ffd75a", glow: "#ff0000" }, style: "constellation" },
            victory: { colors: { main: "#ffffff", second: "#123456" }, style: "corona" },
        };
        for (const kind of KINDS) {
            const c = (await create(a, kind, "Aurore", LOOKS[kind])).body.data.creation;
            const put = (body: Record<string, unknown>) => api("PUT", `${V}/admin/creations/${c.id}`, { token: a.token, body });
            const ok = await put({ name: "Aurore dorée", ...edited[kind], updated_at: c.updated_at });
            expect(ok.status, JSON.stringify(ok.body)).toBe(200);
            expect(ok.body.data).toMatchObject({ changed: true, creation: { id: c.id, kind, name: "Aurore dorée", ...edited[kind] } });
            expect((await put({ name: "Aurore dorée", ...edited[kind] })).body.data.changed).toBe(false);
            expect((await put({ name: "Aurore", ...LOOKS[kind], updated_at: c.updated_at })).body.error.code).toBe("CREATION_CHANGED");
            // Another kind's look, or a kind sent: refused.
            const other = kind === "frame" ? LOOKS.victory : LOOKS.frame;
            expect((await put({ name: "Aurore", ...other })).status).toBe(400);
            expect((await put({ name: "Aurore", ...LOOKS[kind], kind: "suit" })).status).toBe(400);
            const archive = (archived: boolean) => api("POST", `${V}/admin/creations/${c.id}/archive`, { token: a.token, body: { archived } });
            expect((await archive(true)).body.data.creation.archived).toBe(true);
            expect((await put({ name: "Aurore", ...LOOKS[kind] })).body.error.code).toBe("CREATION_ARCHIVED");
            expect((await archive(false)).body.data.creation.archived).toBe(false);
        }
        const log = await journal(a, "creation");
        const upd = log.find((e) => e.action === "admin_creation_updated" && e.creation.kind === "trail");
        expect(upd.creation).toMatchObject({ style: "bubbles", width: 60, life_ms: 300, particles: false, previous: { style: "comet", width: 140, particles: true } });
        for (const kind of KINDS) {
            expect(log.some((e) => e.action === "admin_creation_archived" && e.creation.kind === kind)).toBe(true);
            expect(log.some((e) => e.action === "admin_creation_restored" && e.creation.kind === kind)).toBe(true);
        }
    });
});

describe("creations 2.59: the games draw them, a challenge gives them, their winners wear them", () => {
    it("lists every kind publicly with what the games need to draw it", async () => {
        const a = await admin();
        const suit = (await create(a, "suit", "Givre", { colors: { suit: "#c8e1ff", accent: "#78c8ff", scarf: "#ebf5ff" } })).body.data.creation;
        const made = [];
        for (const kind of KINDS) made.push((await create(a, kind, "Aurore", LOOKS[kind])).body.data.creation);
        const pub = (await api("GET", `${V}/creations`)).body.data.creations;
        // A suit's entry as in 2.45 (the games before 2.59 read it), the others with their style.
        expect(Object.keys(pub[0]).sort()).toEqual(["colors", "id", "kind", "name", "ref", "updated_at"]);
        expect(pub[0].ref).toBe(suit.ref);
        expect(pub.slice(1)).toEqual(
            made.map((c) => ({ id: c.id, ref: c.ref, kind: c.kind, name: "Aurore", ...LOWER[c.kind as Kind], updated_at: c.updated_at })),
        );
        // Archived and given to nobody: gone from the public list.
        await api("POST", `${V}/admin/creations/${made[1].id}/archive`, { token: a.token, body: { archived: true } });
        expect((await api("GET", `${V}/creations`)).body.data.creations.map((c: any) => c.ref)).toEqual([suit.ref, made[0].ref, made[2].ref]);
    });

    it("gives each kind as a reward (checked), grants it after the end, and shows it on the profile and the boards of its winners only", async () => {
        const a = await admin();
        const made: Record<Kind, any> = {} as any;
        for (const kind of KINDS) made[kind] = (await create(a, kind, `Aurore ${kind}`, LOOKS[kind])).body.data.creation;
        const archived = (await create(a, "frame", "Brume", LOOKS.frame)).body.data.creation;
        await api("POST", `${V}/admin/creations/${archived.id}/archive`, { token: a.token, body: { archived: true } });

        // The rewards editor: a creation of the right kind, known, not archived.
        const coming = `daily-${addDays(utcDate(), 3)}`;
        const setRewards = (rewards: unknown[]) => api("PUT", `${V}/admin/challenges/${coming}/rewards`, { token: a.token, body: { rewards } });
        const wrong: [string, string][] = [
            ["trail", made.frame.ref],
            ["frame", "srvframe:999999"],
            ["victory", "srvvictory:0"],
            ["trail", "trail_comet"],
            ["frame", archived.ref],
        ];
        for (const [kind, ref] of wrong) expect((await setRewards([{ kind, ref, condition: "finished" }])).status, `${kind} ${ref}`).toBe(400);
        const set = await setRewards(KINDS.map((kind) => ({ kind, ref: made[kind].ref, condition: "tier", value: 3 })));
        expect(set.status, JSON.stringify(set.body)).toBe(200);
        expect(set.body.data.rewards).toEqual(
            KINDS.map((kind) => ({
                kind,
                ref: made[kind].ref,
                condition: "tier",
                value: 3,
                item: null,
                title: null,
                creation: { id: made[kind].ref, kind, name: `Aurore ${kind}`, ...LOWER[kind] },
            })),
        );
        const listed = (await api("GET", `${V}/admin/creations`, { token: a.token })).body.data.creations;
        for (const kind of KINDS) expect(listed.find((c: any) => c.id === made[kind].id).uses).toBe(1);
        const entry = (await journal(a, "challenge")).find((e) => e.action === "admin_challenge_rewards");
        expect(entry.challenge.rewards.map((r: any) => [r.kind, r.category, r.name])).toEqual(KINDS.map((k) => [k, k, `Aurore ${k}`]));

        // A challenge that ended: each one granted by its rule.
        const id = await challenge(addDays(utcDate(), -3));
        for (const [i, kind] of KINDS.entries())
            await env.DB.prepare(
                "INSERT INTO challenge_reward_rules (challenge_id, position, reward_kind, reward_ref, condition, condition_value) VALUES (?1, ?2, ?3, ?4, 'tier', 3)",
            )
                .bind(id, i, kind, made[kind].ref)
                .run();
        const [gold, silver] = await Promise.all(["Aster", "Borealis"].map((n) => registerPlayer(n)));
        await run(gold!, id, "gold");
        await run(silver!, id, "silver");
        expect((await api("GET", `${V}/challenges/${id}`)).body.data.rewards.map((r: any) => r.creation?.id)).toEqual(KINDS.map((k) => made[k].ref));
        expect(await grantDuePrizes(ctx(), { limit: 50 })).toBe(3);
        const mine = (await api("GET", `${V}/players/me/rewards`, { token: gold!.token })).body.data.rewards;
        for (const kind of KINDS)
            expect(mine).toContainEqual(
                expect.objectContaining({ kind, ref: made[kind].ref, item: null, title: null, creation: { id: made[kind].ref, kind, name: `Aurore ${kind}`, ...LOWER[kind] } }),
            );
        expect((await api("GET", `${V}/players/me/rewards`, { token: silver!.token })).body.data.rewards).toEqual([]);
        const note = (await api("GET", `${V}/notifications`, { token: gold!.token })).body.data.notifications.find((n: any) => n.kind === "challenge_reward");
        expect(note.text).toContain("« Aurore frame »");

        // Worn only by the winner, and only in its own category.
        const wear = (who: TestPlayer, items: Record<string, string>) => api("PUT", `${V}/players/me/cosmetics`, { token: who.token, body: { ...DEFAULTS, ...items } });
        const looks = { trail: made.trail.ref, frame: made.frame.ref, victory: made.victory.ref };
        const refused = (await wear(silver!, looks)).body.data.cosmetics;
        expect([refused.trail.id, refused.frame.id, refused.victory.id]).toEqual(["trail_none", "frame_simple", "victory_classic"]);
        const crossed = (await wear(gold!, { trail: made.frame.ref, frame: made.trail.ref })).body.data.cosmetics;
        expect([crossed.trail.id, crossed.frame.id]).toEqual(["trail_none", "frame_simple"]);
        expect((await wear(gold!, { frame: "srvframe:01" })).status).toBe(400);
        const worn = (await wear(gold!, looks)).body.data.cosmetics;
        for (const kind of KINDS) expect(worn[kind]).toEqual({ id: made[kind].ref, look: made[kind].ref, name: `Aurore ${kind}` });
        // The profile names them; the boards give the frame (drawn on the row) and the trail (behind the ghost).
        const profile = (await api("GET", `${V}/players/${gold!.id}`)).body.data.cosmetics;
        for (const kind of KINDS) expect(profile[kind]).toEqual({ id: made[kind].ref, look: made[kind].ref, name: `Aurore ${kind}` });
        const board = (await api("GET", `${V}/challenges/${id}/leaderboard`)).body.data.entries;
        const g = board.find((e: any) => e.player.id === gold!.id).player;
        expect([g.frame, g.trail]).toEqual([made.frame.ref, made.trail.ref]);
        const s = board.find((e: any) => e.player.id === silver!.id).player;
        expect([s.frame, s.trail]).toEqual(["simple", "none"]);
        // Edited later: every game follows (the public list, the names on the profile).
        await api("PUT", `${V}/admin/creations/${made.frame.id}`, { token: a.token, body: { name: "Cadre boréal", ...LOOKS.frame, style: "nebula" } });
        expect((await api("GET", `${V}/players/${gold!.id}`)).body.data.cosmetics.frame.name).toBe("Cadre boréal");
        expect((await api("GET", `${V}/creations`)).body.data.creations.find((c: any) => c.id === made.frame.id).style).toBe("nebula");
        // Declaring achievements again never takes them off.
        await api("PUT", `${V}/players/me/achievements`, { token: gold!.token, body: { unlocked: [] } });
        const after = (await api("GET", `${V}/players/${gold!.id}`)).body.data.cosmetics;
        expect([after.trail.id, after.frame.id, after.victory.id]).toEqual(KINDS.map((k) => made[k].ref));
    });

    it("keeps the suits of 2.45 as they were, ids and rewards included (migration 0115)", async () => {
        const a = await admin();
        const suit = (await create(a, "suit", "Givre", { colors: { suit: "#c8e1ff", accent: "#78c8ff", scarf: "#ebf5ff" } })).body.data.creation;
        const trail = (await create(a, "trail", "Givre", LOOKS.trail)).body.data.creation;
        // Ids follow each other across the kinds (one table, its sequence kept).
        expect(trail.id).toBe(suit.id + 1);
        const row = await env.DB.prepare("SELECT * FROM creations WHERE id = ?1").bind(suit.id).first<Record<string, unknown>>();
        expect(row).toMatchObject({ kind: "suit", suit_color: "#c8e1ff", color_a: null, style: null, width: null });
    });
});
