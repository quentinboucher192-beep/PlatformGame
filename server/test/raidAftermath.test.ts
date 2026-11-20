// 2.81 (docs/RAIDS.md §25): « Après Asterion », the film of what the fall changes in the world, watched together.
// Its start is stamped once by the server at the VALIDATED fall of the Heart (the fall plus FILM_DELAY_MS), never
// from a game's claim (a minority report, a part below the whole health, a second call: nothing changes); the clans
// present at the fall are named on the event's sheet (`aftermath`), their members told once (« heart_film »); the
// viewer may watch it again when they took part, or once the world outcome « Asterion est tombé » is official.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { CATALOGS, HEART } from "../src/raidMap";
import { FILM_DELAY_MS, aftermathOf, checkHeartFall } from "../src/raidHeart";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";
const TICK = 150000;

async function clan(tag: string, n: number) {
    const chief = await registerPlayer(`${tag}Chef`);
    const r = await api("POST", `${V}/clans`, { token: chief.token, body: { name: `Clan ${tag}`, tag, motto: "Ensemble.", emblem: EMBLEM, join_policy: "open" } });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    const id = r.body.data.clan.id as string;
    const players = [chief];
    for (let i = 1; i < n; ++i) {
        const p = await registerPlayer(`${tag}Mem${i}`);
        expect((await api("POST", `${V}/clans/${id}/join`, { token: p.token, body: {} })).status).toBe(200);
        players.push(p);
    }
    return { id, tag, players };
}

/** A world event; `registered`: the players of each clan registered for it (the others only members of their clan). */
async function worldEvent(clans: { id: string; players: TestPlayer[] }[], registered = 99): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-30), iso(-60), iso(50), now)
        .run();
    let k = 0;
    for (const c of clans) {
        await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
            .bind(id, c.id, c.players[0]!.id, now)
            .run();
        for (const p of c.players.slice(0, registered))
            await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
                .bind(id, p.id, c.id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
                .run();
    }
    return id;
}

async function openHeart(ev: string) {
    for (const c of [...CATALOGS, HEART!])
        for (const o of c === HEART ? ["coeur_aorte"] : [c.objectives.find((x) => x.role === "gigaboss")!.id])
            await env.DB.prepare("INSERT OR IGNORE INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, NULL, ?4)")
                .bind(ev, c.id, o, new Date().toISOString())
                .run();
}

/** A clan's instance in the heart, `seated` of its players seated and active. */
async function heartInstance(ev: string, c: { players: TestPlayer[] }, seated = c.players.length) {
    const j = await api("POST", `${V}/raids/events/${ev}/branches/coeur/join`, { token: c.players[0]!.token, body: {} });
    expect(j.status, JSON.stringify(j.body)).toBe(200);
    const inst = j.body.data.instance.id as string;
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(inst, iso(-25), seated).run();
    let seat = 0;
    for (const p of c.players.slice(0, seated))
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, NULL, 0)")
            .bind(inst, p.id, seat++, iso(-25))
            .run();
    const send = (p: TestPlayer, body: Record<string, unknown>) =>
        api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { zone: 2, tick: TICK, hash: H1, ...body } });
    return { inst, send };
}

const stamp = (ev: string) =>
    env.DB.prepare("SELECT fallen_at, film_at, film_clans, notified_film FROM raid_heart_events WHERE event_id = ?1")
        .bind(ev)
        .first<{ fallen_at: string | null; film_at: string | null; film_clans: string | null; notified_film: string | null }>();

describe("raids 2.81: « Après Asterion », watched together", () => {
    it("its start is stamped only by the validated fall, once, from the fall's own time", async () => {
        const a = await clan("APRA", 3);
        const ev = await worldEvent([a]);
        await openHeart(ev);
        const x = await heartInstance(ev, a);
        // A part below the whole health, accepted: no film.
        for (const p of a.players) await x.send(p, { kind: "heart", damage: 6000 });
        expect((await stamp(ev))?.film_at ?? null).toBeNull();
        expect(await checkHeartFall(env.DB, { id: ev }, null)).toBe(false);
        // One player claiming the whole health (a minority of three): pending, never a fall, never a film.
        const claim = await x.send(a.players[0]!, { kind: "heart", damage: 10000, tick: TICK + 120 });
        expect(claim.body.data.status).toBe("pending");
        expect((await stamp(ev))?.film_at ?? null).toBeNull();
        const before = await api("GET", `${V}/raids/events/${ev}`, { token: a.players[0]!.token });
        expect(before.body.data.aftermath).toBeUndefined();
        // Unknown fields of a report never reach it.
        await x.send(a.players[1]!, { kind: "heart", damage: 6000, tick: TICK + 240, film_at: "2000-01-01T00:00:00.000Z" });
        expect((await stamp(ev))?.film_at ?? null).toBeNull();
        // The majority agrees: the fall is validated, the film stamped.
        await x.send(a.players[1]!, { kind: "heart", damage: 10000, tick: TICK + 120 });
        const s = await stamp(ev);
        expect(s?.fallen_at).toBeTruthy();
        expect(Date.parse(s!.film_at!) - Date.parse(s!.fallen_at!)).toBe(FILM_DELAY_MS);
        expect(JSON.parse(s!.film_clans!)).toEqual([a.id]);
        // Again (a replayed report, the cron): the same start.
        expect(await checkHeartFall(env.DB, { id: ev }, null, new Date(Date.now() + 3600000).toISOString())).toBe(true);
        expect((await stamp(ev))?.film_at).toBe(s!.film_at);
    });

    it("names the clans present at the fall, tells their members once, and says who may watch it again", async () => {
        const a = await clan("APRB", 3); // in the heart (two seated), its third member only in the clan
        const b = await clan("APRC", 2); // in the heart too
        const c = await clan("APRD", 2); // registered, never in the heart
        const ev = await worldEvent([a, b, c], 2);
        await openHeart(ev);
        const x = await heartInstance(ev, a, 2);
        const y = await heartInstance(ev, b);
        for (const p of a.players.slice(0, 2)) await x.send(p, { kind: "heart", damage: 6000 });
        for (const p of b.players) await y.send(p, { kind: "heart", damage: 4000 });
        const s = await stamp(ev);
        expect(s?.film_at).toBeTruthy();
        expect(new Set(JSON.parse(s!.film_clans!))).toEqual(new Set([a.id, b.id]));
        // Their members told once (the third member of A too, never registered), never the clan that was not there.
        const told = await env.DB.prepare("SELECT player_id, data FROM notifications WHERE group_key = ?1").bind(`raid:heart_film:${ev}`).all<{ player_id: string; data: string }>();
        const ids = told.results.map((r) => r.player_id).sort();
        expect(ids).toEqual([...a.players, ...b.players].map((p) => p.id).sort());
        const data = JSON.parse(told.results[0]!.data);
        expect(data.event).toBe("heart_film");
        expect(data.event_id).toBe(ev);
        expect(data.film_at).toBe(s!.film_at);
        await checkHeartFall(env.DB, { id: ev }, null);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM notifications WHERE group_key = ?1").bind(`raid:heart_film:${ev}`).first<{ n: number }>())!.n).toBe(5);
        // The notification's sentence (its English in data/i18n/en.json).
        const list = await api("GET", `${V}/notifications?filter=raids&limit=10`, { token: a.players[2]!.token });
        expect(list.status).toBe(200);
        expect(list.body.data.notifications.some((n: { text: string }) => n.text.startsWith("Asterion est tombé — regarder le film avec votre clan"))).toBe(true);
        // The sheet: its start, the clans' names (the viewer's own marked), who may watch it again.
        const sheetA = await api("GET", `${V}/raids/events/${ev}`, { token: a.players[2]!.token });
        const am = sheetA.body.data.aftermath;
        expect(am.film_at).toBe(s!.film_at);
        expect(am.clans.map((k: { tag: string }) => k.tag).sort()).toEqual(["APRB", "APRC"]);
        expect(am.clans.find((k: { tag: string }) => k.tag === "APRB").name).toBe("Clan APRB");
        expect(am.my_clan).toBe(a.id);
        expect(am.took_part).toBe(true); // a member of a clan present, even unregistered
        expect(am.unlocked).toBe(true);
        expect(typeof sheetA.body.data.server_time).toBe("string");
        const sheetC = await api("GET", `${V}/raids/events/${ev}`, { token: c.players[0]!.token });
        expect(sheetC.body.data.aftermath.my_clan).toBeNull();
        expect(sheetC.body.data.aftermath.took_part).toBe(true); // registered for the event
        const stranger = await registerPlayer("APRStranger");
        const sheetS = await api("GET", `${V}/raids/events/${ev}`, { token: stranger.token });
        expect(sheetS.body.data.aftermath.took_part).toBe(false);
        expect(sheetS.body.data.aftermath.unlocked).toBe(false);
        // Once the world outcome « Asterion est tombé » is official: for everyone.
        await env.DB.prepare("INSERT INTO raid_event_results (event_id, outcome, data, frozen_at) VALUES (?1, 'vaincu', '{}', ?2)").bind(ev, new Date().toISOString()).run();
        const after = await aftermathOf(env.DB, ev, stranger.id, null);
        expect(after?.official).toBe(true);
        expect(after?.unlocked).toBe(true);
    });

    it("no fall, no film: the sheet of an event whose heart stands has no `aftermath`", async () => {
        const a = await clan("APRE", 2);
        const ev = await worldEvent([a]);
        await openHeart(ev);
        expect(await aftermathOf(env.DB, ev, a.players[0]!.id, a.id)).toBeNull();
        const sheet = await api("GET", `${V}/raids/events/${ev}`, { token: a.players[0]!.token });
        expect(sheet.body.data.aftermath).toBeUndefined();
    });
});
