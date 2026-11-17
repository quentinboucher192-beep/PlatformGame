// ADMINISTRATION › Créations: suits made by an administrator (a name, three colours), listed,
// created, edited and archived by the admins only (404 for anyone else), every change and every
// refusal in the Journal (kind « creation »); a public list that every game reads to draw them;
// and a suit given as the reward of a challenge, granted like the other rewards, worn only once
// won (profile, boards).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { addDays, grantDuePrizes, periodBounds, periodOf, utcDate } from "../src/challenges";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

const V = "/api/v1";
const HASH = "2".repeat(64);
const RULES_HASH = "a".repeat(64);
const GIVRE = { suit: "#c8e1ff", accent: "#78c8ff", scarf: "#ebf5ff" };

beforeEach(seedAll);

async function admin(name = "NovaRider"): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

async function create(a: TestPlayer, name: string, colors: Record<string, unknown> = GIVRE, extra: Record<string, unknown> = {}) {
    return api("POST", `${V}/admin/creations`, { token: a.token, body: { kind: "suit", name, colors, ...extra } });
}

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
           tier, status, late, created_at) VALUES (?1, ?2, ?3, ?4, 60000, 0, 0, ?5, ?6, '2.45.0', ?7, 'VERIFIED', 0, ?8)`,
    )
        .bind(crypto.randomUUID(), challengeId, who.id, crypto.randomUUID(), HASH, RULES_HASH, tier, new Date().toISOString())
        .run();
}

describe("creations: administration", () => {
    it("does not exist for a player, and needs a session", async () => {
        const p = await registerPlayer("Kestrel");
        const routes = [
            ["GET", `${V}/admin/creations`, undefined],
            ["POST", `${V}/admin/creations`, { kind: "suit", name: "Aurore", colors: GIVRE }],
            ["PUT", `${V}/admin/creations/1`, { name: "Aurore", colors: GIVRE }],
            ["POST", `${V}/admin/creations/1/archive`, { archived: true }],
        ] as const;
        for (const [m, path, body] of routes) {
            expect((await api(m, path, { body })).status, `${m} ${path}`).toBe(401);
            const res = await api(m, path, { token: p.token, body });
            expect(res.status, `${m} ${path}`).toBe(404);
            expect(res.body.error.code).toBe("NOT_FOUND");
        }
        // A suspended administrator neither.
        const a = await admin();
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(a.id).run();
        expect((await create(a, "Aurore")).status).toBe(403);
        expect(await count("SELECT COUNT(*) AS n FROM creations")).toBe(0);
        expect(await count("SELECT COUNT(*) AS n FROM audit_log WHERE action LIKE 'admin_creation_%'")).toBe(0);
    });

    it("creates suits: name cleaned and checked, three #rrggbb colours (lower case), never the same name twice", async () => {
        const a = await admin();
        const ok = await create(a, "  Aurore   polaire ", { suit: "#C8E1FF", accent: "#78c8ff", scarf: "#EBF5FF" });
        expect(ok.status, JSON.stringify(ok.body)).toBe(201);
        const c = ok.body.data.creation;
        expect(c).toMatchObject({ kind: "suit", name: "Aurore polaire", colors: GIVRE, archived: false, uses: 0, owners: 0, created_by: "NovaRider" });
        expect(c.ref).toBe(`srvsuit:${c.id}`);
        const bad: [string, Record<string, unknown>, Record<string, unknown>?][] = [
            ["x".repeat(33), GIVRE],
            ["Bonjour\u0007", GIVRE],
            ["Étoile ⭐", GIVRE],
            ["   ", GIVRE],
            ["Correcte", { ...GIVRE, suit: "#fff" }],
            ["Correcte", { ...GIVRE, accent: "rouge" }],
            ["Correcte", { ...GIVRE, scarf: "#12345g" }],
            ["Correcte", { suit: GIVRE.suit, accent: GIVRE.accent }],
            ["Correcte", { ...GIVRE, glow: "#ffffff" }],
            ["Correcte", GIVRE, { kind: "title" }],
            ["Correcte", GIVRE, { extra: 1 }],
        ];
        for (const [name, colors, extra] of bad) {
            const r = await create(a, name, colors, extra ?? {});
            expect(r.status, `${name} ${JSON.stringify(colors)} ${JSON.stringify(extra)}`).toBe(400);
            expect(r.body.error.code).toBe("VALIDATION_ERROR");
        }
        expect((await api("POST", `${V}/admin/creations`, { token: a.token, body: { kind: "suit", name: "Sans couleurs" } })).status).toBe(400);
        expect((await create(a, "é".repeat(32))).status).toBe(201);
        // The same name, accents and case aside: refused, the existing one given back.
        const dup = await create(a, "AURORE POLAIRÉ", { suit: "#000000", accent: "#111111", scarf: "#222222" });
        expect(dup.status).toBe(409);
        expect(dup.body.error.code).toBe("CREATION_EXISTS");
        expect(dup.body.error.details.creation.id).toBe(c.id);
        expect(await count("SELECT COUNT(*) AS n FROM creations")).toBe(2);
        // The list: newest first, with who made it.
        const list = await api("GET", `${V}/admin/creations`, { token: a.token });
        expect(list.status).toBe(200);
        expect(list.body.data.creations.map((x: any) => x.name)).toEqual(["é".repeat(32), "Aurore polaire"]);
        expect(list.body.data.max_chars).toBe(32);
        // The Journal: two creations, and each refusal with what was tried.
        const done = await journal(a, "creation");
        expect(done.filter((e) => e.action === "admin_creation_created")).toHaveLength(2);
        expect(done.find((e) => e.creation?.name === "Aurore polaire").creation).toMatchObject({ ref: c.ref, kind: "suit", colors: GIVRE });
        const refused = await journal(a, "refused");
        expect(refused.some((e) => e.reason === "CREATION_EXISTS" && e.creation?.name === "AURORE POLAIRÉ")).toBe(true);
        expect(refused.filter((e) => e.reason === "VALIDATION_ERROR" && e.creation?.action === "create").length).toBeGreaterThanOrEqual(bad.length);
        // « Toutes les actions » shows them too.
        expect((await journal(a, "all")).some((e) => e.action === "admin_creation_created")).toBe(true);
    });

    it("edits a suit (name, colours), refuses an edit of a version that changed, archives and restores it", async () => {
        const a = await admin();
        const b = await admin("Vega");
        const c = (await create(a, "Aurore")).body.data.creation;
        const other = (await create(a, "Braise")).body.data.creation;
        const put = (who: TestPlayer, id: number | string, body: Record<string, unknown>) =>
            api("PUT", `${V}/admin/creations/${id}`, { token: who.token, body });
        const recolored = { suit: "#ffd700", accent: "#ffffff", scarf: "#c81e3c" };
        const edit = await put(a, c.id, { name: "Aurore dorée", colors: recolored, updated_at: c.updated_at });
        expect(edit.status, JSON.stringify(edit.body)).toBe(200);
        expect(edit.body.data).toMatchObject({ changed: true, creation: { id: c.id, name: "Aurore dorée", colors: recolored } });
        // Unchanged: nothing written.
        const same = await put(a, c.id, { name: "Aurore dorée", colors: recolored });
        expect(same.body.data.changed).toBe(false);
        // Another administrator edits the version read before: refused.
        const stale = await put(b, c.id, { name: "Aurore", colors: GIVRE, updated_at: c.updated_at });
        expect(stale.status).toBe(409);
        expect(stale.body.error.code).toBe("CREATION_CHANGED");
        // Another suit's name, an unknown or malformed id, bad colours.
        expect((await put(a, c.id, { name: "braise", colors: recolored })).body.error.code).toBe("CREATION_EXISTS");
        expect((await put(a, 999999, { name: "X", colors: GIVRE })).body.error.code).toBe("CREATION_NOT_FOUND");
        expect((await put(a, "abc", { name: "X", colors: GIVRE })).status).toBe(404);
        expect((await put(a, c.id, { name: "Aurore", colors: { ...GIVRE, suit: "blue" } })).status).toBe(400);
        // Archived: no longer edited; its name is free again; restored only while no other suit in use has it.
        const archive = (id: number, archived: unknown) => api("POST", `${V}/admin/creations/${id}/archive`, { token: a.token, body: { archived } });
        expect((await archive(other.id, "yes")).status).toBe(400);
        const arch = await archive(other.id, true);
        expect(arch.status).toBe(200);
        expect(arch.body.data).toMatchObject({ changed: true, creation: { archived: true } });
        expect(arch.body.data.creation.archived_at).toBeTruthy();
        expect((await archive(other.id, true)).body.data.changed).toBe(false);
        expect((await put(a, other.id, { name: "Braise", colors: GIVRE })).body.error.code).toBe("CREATION_ARCHIVED");
        const again = await create(a, "Braise");
        expect(again.status).toBe(201);
        expect((await archive(other.id, false)).body.error.code).toBe("CREATION_EXISTS");
        await archive(again.body.data.creation.id, true);
        const restored = await archive(other.id, false);
        expect(restored.status).toBe(200);
        expect(restored.body.data.creation).toMatchObject({ archived: false, archived_at: null });
        // The list: those in use first, then the archived ones.
        const list = (await api("GET", `${V}/admin/creations`, { token: a.token })).body.data.creations;
        expect(list.map((x: any) => [x.id, x.archived])).toEqual([
            [other.id, false],
            [c.id, false],
            [again.body.data.creation.id, true],
        ]);
        // The Journal: each action, an edit with what it was.
        const log = await journal(a, "creation");
        const actions = log.map((e) => e.action);
        expect(actions.filter((x) => x === "admin_creation_updated")).toHaveLength(1);
        expect(actions.filter((x) => x === "admin_creation_archived")).toHaveLength(2);
        expect(actions.filter((x) => x === "admin_creation_restored")).toHaveLength(1);
        const upd = log.find((e) => e.action === "admin_creation_updated");
        expect(upd.creation).toMatchObject({ name: "Aurore dorée", colors: recolored, previous: { name: "Aurore", colors: GIVRE } });
        expect(upd.actor.username).toBe("NovaRider");
        const refused = await journal(b, "refused");
        expect(refused.some((e) => e.reason === "CREATION_CHANGED" && e.actor.username === "Vega")).toBe(true);
        expect(refused.some((e) => e.reason === "CREATION_ARCHIVED")).toBe(true);
    });
});

describe("creations: the games draw them, and a challenge gives them", () => {
    it("lists publicly (no account) the suits a challenge that has begun gives, and those won (archived too), never the others", async () => {
        const a = await admin();
        const kept = (await create(a, "Aurore")).body.data.creation;
        const gone = (await create(a, "Brume", { suit: "#101010", accent: "#202020", scarf: "#303030" })).body.data.creation;
        const won = (await create(a, "Cendre", { suit: "#404040", accent: "#505050", scarf: "#606060" })).body.data.creation;
        const unseen = (await create(a, "Dune", { suit: "#707070", accent: "#808080", scarf: "#909090" })).body.data.creation;
        for (const id of [gone.id, won.id]) await api("POST", `${V}/admin/creations/${id}/archive`, { token: a.token, body: { archived: true } });
        const p = await registerPlayer("Orion");
        await env.DB.prepare("INSERT INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at) VALUES (?1, 'daily-2026-01-01', 'suit', ?2, ?3)")
            .bind(p.id, won.ref, new Date().toISOString())
            .run();
        // 2.60: in service but given by no challenge (« Dune »): not listed; « Aurore », given by today's challenge, is.
        const today = await challenge(utcDate());
        await env.DB.prepare(
            "INSERT INTO challenge_reward_rules (challenge_id, position, reward_kind, reward_ref, condition, condition_value) VALUES (?1, 0, 'suit', ?2, 'finished', 0)",
        )
            .bind(today, kept.ref)
            .run();
        const pub = await api("GET", `${V}/creations`);
        expect(pub.status).toBe(200);
        expect(pub.body.data.creations).toEqual([
            { id: kept.id, ref: kept.ref, kind: "suit", name: "Aurore", colors: GIVRE, updated_at: kept.updated_at },
            expect.objectContaining({ id: won.id, ref: won.ref, name: "Cendre", colors: { suit: "#404040", accent: "#505050", scarf: "#606060" } }),
        ]);
        expect(pub.body.data.creations.map((c: any) => c.id)).not.toContain(unseen.id);
        // Nothing of the administration in it.
        expect(Object.keys(pub.body.data.creations[0]).sort()).toEqual(["colors", "id", "kind", "name", "ref", "updated_at"]);
    });

    it("gives a suit as the reward of a coming challenge (checked), grants it after the end, and lets only its winners wear it", async () => {
        const a = await admin();
        const suit = (await create(a, "Aurore")).body.data.creation;
        const archived = (await create(a, "Brume")).body.data.creation;
        await api("POST", `${V}/admin/creations/${archived.id}/archive`, { token: a.token, body: { archived: true } });

        // The rewards editor: a suit of the server, checked like the titles.
        const coming = `daily-${addDays(utcDate(), 3)}`;
        const setRewards = (rewards: unknown[]) => api("PUT", `${V}/admin/challenges/${coming}/rewards`, { token: a.token, body: { rewards } });
        for (const ref of ["srvsuit:999999", "srvsuit:abc", "srv:1", archived.ref]) {
            const r = await setRewards([{ kind: "suit", ref, condition: "finished" }]);
            expect(r.status, ref).toBe(400);
        }
        const set = await setRewards([{ kind: "suit", ref: suit.ref, condition: "tier", value: 3 }]);
        expect(set.status, JSON.stringify(set.body)).toBe(200);
        expect(set.body.data.rewards).toEqual([
            { kind: "suit", ref: suit.ref, condition: "tier", value: 3, item: null, title: null, suit: { id: suit.ref, name: "Aurore", colors: GIVRE } },
        ]);
        expect((await api("GET", `${V}/admin/creations`, { token: a.token })).body.data.creations.find((c: any) => c.id === suit.id).uses).toBe(1);
        const entry = (await journal(a, "challenge")).find((e) => e.action === "admin_challenge_rewards");
        expect(entry.challenge.rewards).toEqual([expect.objectContaining({ kind: "suit", name: "Aurore", category: "suit" })]);
        // Archived after being given: the challenge keeps it (its list saved again as it is).
        await api("POST", `${V}/admin/creations/${suit.id}/archive`, { token: a.token, body: { archived: true } });
        expect((await setRewards([{ kind: "suit", ref: suit.ref, condition: "tier", value: 3 }])).status).toBe(200);
        // 2.60: the reward of a coming challenge only: still unknown to the players (listed once it begins).
        expect((await api("GET", `${V}/creations`)).body.data.creations.map((c: any) => c.ref)).toEqual([]);
        await api("POST", `${V}/admin/creations/${suit.id}/archive`, { token: a.token, body: { archived: false } });

        // A challenge that ended: granted by its rule, like the other rewards.
        const id = await challenge(addDays(utcDate(), -3));
        await env.DB.prepare(
            "INSERT INTO challenge_reward_rules (challenge_id, position, reward_kind, reward_ref, condition, condition_value) VALUES (?1, 0, 'suit', ?2, 'tier', 3)",
        )
            .bind(id, suit.ref)
            .run();
        const [gold, silver] = await Promise.all(["Aster", "Borealis"].map((n) => registerPlayer(n)));
        await run(gold!, id, "gold");
        await run(silver!, id, "silver");
        // The challenge's public form shows it while it runs and after.
        expect((await api("GET", `${V}/challenges/${id}`)).body.data.rewards[0]).toMatchObject({ kind: "suit", suit: { id: suit.ref, name: "Aurore" } });
        expect(await grantDuePrizes(ctx(), { limit: 50 })).toBe(1);
        const mine = (await api("GET", `${V}/players/me/rewards`, { token: gold!.token })).body.data.rewards;
        expect(mine).toEqual([
            expect.objectContaining({ kind: "suit", ref: suit.ref, item: null, title: null, suit: { id: suit.ref, name: "Aurore", colors: GIVRE } }),
        ]);
        expect((await api("GET", `${V}/players/me/rewards`, { token: silver!.token })).body.data.rewards).toEqual([]);
        const notes = (await api("GET", `${V}/notifications`, { token: gold!.token })).body.data.notifications.filter((n: any) => n.kind === "challenge_reward");
        expect(notes).toHaveLength(1);
        expect(notes[0].text).toContain("« Aurore »");
        // Once: a second pass grants nothing more.
        expect(await grantDuePrizes(ctx(), { limit: 50 })).toBe(0);

        // Worn only by its winner: the profile names it, the boards give its reference.
        const wear = (who: TestPlayer, s: string) =>
            api("PUT", `${V}/players/me/cosmetics`, {
                token: who.token,
                body: { suit: s, trail: "trail_none", frame: "frame_simple", title: "title_none", victory: "victory_classic" },
            });
        expect((await wear(silver!, suit.ref)).body.data.cosmetics.suit.id).toBe("suit_standard");
        expect((await wear(gold!, archived.ref)).body.data.cosmetics.suit.id).toBe("suit_standard");
        expect((await wear(gold!, "srvsuit:0")).status).toBe(400);
        const worn = (await wear(gold!, suit.ref)).body.data.cosmetics.suit;
        expect(worn).toEqual({ id: suit.ref, look: suit.ref, name: "Aurore" });
        expect((await api("GET", `${V}/players/${gold!.id}`)).body.data.cosmetics.suit).toEqual({ id: suit.ref, look: suit.ref, name: "Aurore" });
        const board = await api("GET", `${V}/challenges/${id}/leaderboard`);
        const entry2 = board.body.data.entries.find((e: any) => e.player.id === gold!.id);
        expect(entry2.player.suit).toBe(suit.ref);
        expect(board.body.data.entries.find((e: any) => e.player.id === silver!.id).player.suit).toBe("standard");
        // Renamed later: every game follows (profile, the public list).
        await api("PUT", `${V}/admin/creations/${suit.id}`, { token: a.token, body: { name: "Aurore boréale", colors: GIVRE } });
        expect((await api("GET", `${V}/players/${gold!.id}`)).body.data.cosmetics.suit.name).toBe("Aurore boréale");
        // Declaring achievements again never takes it off.
        await api("PUT", `${V}/players/me/achievements`, { token: gold!.token, body: { unlocked: [] } });
        expect((await api("GET", `${V}/players/${gold!.id}`)).body.data.cosmetics.suit.id).toBe(suit.ref);
    });
});
