// Clan raids « Asterion », phase 1 (raids.ts): world events scheduled by the administrators
// (draft, publication to every clan), clan raids scheduled by a chief or an officer (internal,
// invitations between clans), the registration of the clans and of their members (roles, lock
// time, capacity), the reminders of the cron (each once, at its time, as each player chose),
// the « raid » notifications (grouped, read all). Every time is UTC: the game converts.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { groupsOf, parseUtc, raidState, raidTick, timesOf } from "../src/raids";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const inMin = (m: number) => new Date(Date.now() + m * 60000).toISOString();

interface Team {
    id: string;
    tag: string;
    chief: TestPlayer;
    off: TestPlayer[];
    mem: TestPlayer[];
}

/** A clan with its chief, officers and members (all through the API). */
async function team(tag: string, officers: number, members: number): Promise<Team> {
    const chief = await registerPlayer(`${tag}Chef`);
    const r = await api("POST", `${V}/clans`, {
        token: chief.token,
        body: { name: `Clan ${tag}`, tag, motto: "Une phase à la fois.", emblem: EMBLEM, join_policy: "open" },
    });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    const id = r.body.data.clan.id as string;
    const off: TestPlayer[] = [];
    const mem: TestPlayer[] = [];
    for (let i = 0; i < officers + members; ++i) {
        const p = await registerPlayer(i < officers ? `${tag}Off${i + 1}` : `${tag}Mem${i - officers + 1}`);
        expect((await api("POST", `${V}/clans/${id}/join`, { token: p.token, body: {} })).status).toBe(200);
        if (i < officers) {
            expect((await api("PATCH", `${V}/clans/${id}/members/${p.id}`, { token: chief.token, body: { role: "officer" } })).status).toBe(200);
            off.push(p);
        } else mem.push(p);
    }
    return { id, tag, chief, off, mem };
}

async function admin(): Promise<TestPlayer> {
    const p = await registerPlayer("Admin1");
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

/** A world event published by `a`, starting in `minutes`. */
async function worldEvent(a: TestPlayer, minutes = 3 * 24 * 60, extra: Record<string, unknown> = {}): Promise<string> {
    const c = await api("POST", `${V}/admin/raids`, { token: a.token, body: { format: "standard", starts_at: inMin(minutes), difficulty: 4, ...extra } });
    expect(c.status, JSON.stringify(c.body)).toBe(201);
    const id = c.body.data.event.id as string;
    const p = await api("POST", `${V}/admin/raids/${id}/publish`, { token: a.token });
    expect(p.status, JSON.stringify(p.body)).toBe(200);
    return id;
}

const register = (id: string, p: TestPlayer) => api("POST", `${V}/raids/events/${id}/register`, { token: p.token });
const registerClan = (id: string, p: TestPlayer) => api("POST", `${V}/raids/events/${id}/clan`, { token: p.token });

async function raidNotes(p: TestPlayer): Promise<{ text: string; count: number; data: Record<string, unknown>; read: boolean }[]> {
    const r = await api("GET", `${V}/notifications?filter=raids&limit=50`, { token: p.token });
    expect(r.status).toBe(200);
    return r.body.data.notifications;
}

const countWhere = async (sql: string, ...binds: unknown[]) => (await env.DB.prepare(sql).bind(...binds).first<{ n: number }>())!.n;

describe("raids: rules computed in UTC", () => {
    it("lock and end times, states, UTC only, groups of 8", () => {
        const start = Date.UTC(2026, 10, 21, 20, 0, 0);
        const t = timesOf(start, 30, "standard");
        expect(t).toEqual({ starts_at: "2026-11-21T20:00:00.000Z", lock_at: "2026-11-21T19:30:00.000Z", ends_at: "2026-11-21T21:00:00.000Z" });
        expect(timesOf(start, 30, "court").ends_at).toBe("2026-11-21T20:30:00.000Z");
        expect(timesOf(start, 30, "legendaire").ends_at).toBe("2026-11-21T21:30:00.000Z");
        const e = { ...t, registration_open: 1, capacity: 0, published_at: "2026-11-01T00:00:00.000Z", cancelled_at: null };
        expect(raidState(e, "2026-11-20T10:00:00.000Z")).toBe("inscription");
        expect(raidState({ ...e, registration_open: 0 }, "2026-11-20T10:00:00.000Z")).toBe("programme");
        expect(raidState({ ...e, capacity: 8, players_count: 8 }, "2026-11-20T10:00:00.000Z")).toBe("programme");
        expect(raidState(e, "2026-11-21T19:30:00.000Z")).toBe("imminent");
        expect(raidState(e, "2026-11-21T20:00:00.000Z")).toBe("en_cours");
        expect(raidState(e, "2026-11-21T21:00:00.000Z")).toBe("termine");
        expect(raidState({ ...e, published_at: null }, "2026-11-20T10:00:00.000Z")).toBe("brouillon");
        expect(raidState({ ...e, cancelled_at: "2026-11-20T00:00:00.000Z" }, "2026-11-20T10:00:00.000Z")).toBe("annule");
        // The game converts its local time (and daylight saving time) to UTC: an offset is refused.
        expect(parseUtc("2026-11-21T20:00Z")).toBe(start);
        expect(parseUtc("2026-11-21T20:00:00.000Z")).toBe(start);
        expect(parseUtc("2026-11-21T21:00:00+01:00")).toBeNull();
        expect(parseUtc("2026-11-21T21:00:00")).toBeNull();
        expect(parseUtc("2026-02-30T10:00:00Z")).toBeNull();
        expect(parseUtc("2026-03-29T24:00:00Z")).toBeNull();
        const roster = Array.from({ length: 10 }, (_, i) => ({ id: `p${i}` }));
        const g = groupsOf(roster, "p3");
        expect(g.map((x) => x.members.length)).toEqual([8, 2]);
        expect(g[0]!.leader.id).toBe("p3");
        expect(g[1]!.leader.id).toBe("p8");
    });
});

describe("raids: world events of the administrators", () => {
    it("a draft, then published to every clan once; the Journal keeps each action", async () => {
        const a = await admin();
        const aube = await team("AUBE", 0, 1);
        const nebu = await team("NEBU", 0, 0);
        // Not an administrator: the routes do not exist.
        expect((await api("POST", `${V}/admin/raids`, { token: aube.chief.token, body: { format: "court", starts_at: inMin(600) } })).status).toBe(404);
        // A local time with an offset is refused: the game sends UTC.
        const bad = await api("POST", `${V}/admin/raids`, { token: a.token, body: { format: "court", starts_at: "2026-12-21T21:00:00+01:00" } });
        expect(bad.status).toBe(400);
        const c = await api("POST", `${V}/admin/raids`, { token: a.token, body: { format: "standard", starts_at: inMin(3 * 1440), difficulty: 4 } });
        expect(c.status).toBe(201);
        const id = c.body.data.event.id as string;
        expect(c.body.data.event.state).toBe("brouillon");
        // A draft is nobody's business.
        expect((await api("GET", `${V}/raids`, { token: aube.mem[0]!.token })).body.data.world).toHaveLength(0);
        expect((await api("GET", `${V}/raids/events/${id}`, { token: aube.mem[0]!.token })).status).toBe(404);
        const p = await api("POST", `${V}/admin/raids/${id}/publish`, { token: a.token });
        expect(p.status).toBe(200);
        expect(p.body.data.notified).toBe(3);
        expect((await api("POST", `${V}/admin/raids/${id}/publish`, { token: a.token })).body.error.code).toBe("ALREADY_PUBLISHED");
        const o = await api("GET", `${V}/raids`, { token: aube.mem[0]!.token });
        expect(o.body.data.world[0].id).toBe(id);
        expect(o.body.data.world[0].state).toBe("inscription");
        expect(o.body.data.rewards).toEqual([]);
        expect(o.body.data.unread).toBe(1);
        // Without an account: the world events are public, nothing else.
        const anon = await api("GET", `${V}/raids`);
        expect(anon.body.data.world).toHaveLength(1);
        expect(anon.body.data.clan).toBeNull();
        const notes = await raidNotes(nebu.chief);
        expect(notes.map((n) => n.text)).toEqual(["Événement mondial annoncé : Asterion (raid standard)"]);
        // Registrations closed then opened again: every clan told once (grouped while unread).
        expect((await api("PATCH", `${V}/admin/raids/${id}`, { token: a.token, body: { registration_open: false } })).status).toBe(200);
        expect((await api("PATCH", `${V}/admin/raids/${id}`, { token: a.token, body: { registration_open: true } })).status).toBe(200);
        expect((await api("PATCH", `${V}/admin/raids/${id}`, { token: a.token, body: { registration_open: false } })).status).toBe(200);
        expect((await api("PATCH", `${V}/admin/raids/${id}`, { token: a.token, body: { registration_open: true } })).status).toBe(200);
        expect((await raidNotes(nebu.chief)).filter((n) => n.data.event === "open")).toHaveLength(1);
        const log = await api("GET", `${V}/admin/log?kind=raid`, { token: a.token });
        const actions = log.body.data.entries.map((e: { action: string }) => e.action);
        expect(actions).toContain("admin_raid_created");
        expect(actions).toContain("admin_raid_published");
        expect(actions).toContain("admin_raid_updated");
        expect(log.body.data.entries.find((e: { action: string }) => e.action === "admin_raid_published").raid.notified).toBe(3);
        // Cancelled: the event leaves the tab, the Journal keeps it.
        expect((await api("DELETE", `${V}/admin/raids/${id}`, { token: a.token, body: { reason: "test" } })).status).toBe(200);
        expect((await api("GET", `${V}/raids`, { token: aube.mem[0]!.token })).body.data.world).toHaveLength(0);
    });
});

describe("raids: the clan, then each member registers", () => {
    it("roles, two officers at once, grouped notifications, a member who leaves", async () => {
        const a = await admin();
        const aube = await team("AUBE", 2, 3);
        const loner = await registerPlayer("Solitaire");
        const id = await worldEvent(a);
        const m = aube.mem;
        expect((await register(id, m[0]!)).body.error.code).toBe("CLAN_NOT_REGISTERED");
        expect((await registerClan(id, m[0]!)).status).toBe(403);
        expect((await register(id, loner)).body.error.code).toBe("NO_CLAN");
        // Two officers register the clan at the same moment: one entry, one notification each.
        const both = await Promise.all([registerClan(id, aube.off[0]!), registerClan(id, aube.off[1]!)]);
        expect(both.map((r) => r.status).sort()).toEqual([200, 201]);
        expect(await countWhere("SELECT COUNT(*) AS n FROM raid_clan_entries WHERE event_id = ?1", id)).toBe(1);
        for (const p of [m[0]!, m[1]!, aube.chief])
            expect((await raidNotes(p)).filter((n) => n.data.event === "clan_registered")).toHaveLength(1);
        expect((await register(id, m[0]!)).status).toBe(201);
        expect((await register(id, m[0]!)).body.data.already).toBe(true);
        expect((await register(id, m[1]!)).status).toBe(201);
        // The chief: one notification for both, its counter at 2.
        const grouped = (await raidNotes(aube.chief)).filter((n) => n.data.event === "member_in");
        expect(grouped).toHaveLength(1);
        expect(grouped[0]!.count).toBe(2);
        expect(grouped[0]!.text).toBe("2 membres se sont inscrits à Asterion (raid standard)");
        const sheet = await api("GET", `${V}/raids/events/${id}`, { token: m[1]!.token });
        expect(sheet.body.data.my.registered).toBe(true);
        expect(sheet.body.data.roster.map((p: { username: string }) => p.username)).toEqual(["AUBEMem1", "AUBEMem2"]);
        expect(sheet.body.data.groups).toHaveLength(1);
        expect(sheet.body.data.event.my_clan.players).toBe(2);
        // Raid leader and instructions: chief and officers only, a member of the clan only.
        expect((await api("PATCH", `${V}/raids/events/${id}/clan`, { token: m[0]!.token, body: { instructions: "Bras droit" } })).status).toBe(403);
        expect((await api("PATCH", `${V}/raids/events/${id}/clan`, { token: aube.off[0]!.token, body: { leader_id: loner.id } })).status).toBe(400);
        const up = await api("PATCH", `${V}/raids/events/${id}/clan`, { token: aube.off[0]!.token, body: { leader_id: m[1]!.id, instructions: "Groupe 1 : bras droit." } });
        expect(up.status).toBe(200);
        expect((await raidNotes(m[1]!)).some((n) => n.data.event === "leader")).toBe(true);
        const after = await api("GET", `${V}/raids/events/${id}`, { token: m[0]!.token });
        expect(after.body.data.my.entry.instructions).toBe("Groupe 1 : bras droit.");
        expect(after.body.data.groups[0].leader.username).toBe("AUBEMem2");
        // The raid leader may call the members not registered yet, once while unread.
        const call = await api("POST", `${V}/raids/events/${id}/call`, { token: m[1]!.token });
        expect(call.body.data.notified).toBe(4); // the chief, two officers, the third member
        expect((await api("POST", `${V}/raids/events/${id}/call`, { token: m[1]!.token })).body.data.notified).toBe(0);
        expect((await api("POST", `${V}/raids/events/${id}/call`, { token: m[2]!.token })).status).toBe(403);
        // A member who leaves the clan loses their registrations to come.
        expect((await api("DELETE", `${V}/clans/${aube.id}/members/${m[0]!.id}`, { token: m[0]!.token })).status).toBe(200);
        expect(await countWhere("SELECT COUNT(*) AS n FROM raid_registrations WHERE player_id = ?1", m[0]!.id)).toBe(0);
    });

    it("lock time: neither registration nor unregistration once locked", async () => {
        const a = await admin();
        const aube = await team("AUBE", 0, 2);
        const id = await worldEvent(a, 40);
        expect((await registerClan(id, aube.chief)).status).toBe(201);
        expect((await register(id, aube.mem[0]!)).status).toBe(201);
        // The lock time passes (30 minutes before the start).
        await env.DB.prepare("UPDATE raid_events SET lock_at = ?2 WHERE id = ?1").bind(id, new Date(Date.now() - 1000).toISOString()).run();
        const late = await register(id, aube.mem[1]!);
        expect(late.status).toBe(409);
        expect(late.body.error.code).toBe("RAID_LOCKED");
        const leave = await api("DELETE", `${V}/raids/events/${id}/register`, { token: aube.mem[0]!.token });
        expect(leave.body.error.code).toBe("RAID_LOCKED");
        expect((await api("GET", `${V}/raids`, { token: aube.mem[0]!.token })).body.data.world[0].state).toBe("imminent");
    });

    it("capacity: the last place goes to one player only", async () => {
        const a = await admin();
        const aube = await team("AUBE", 0, 3);
        const id = await worldEvent(a, 600, { clan_capacity: 2 });
        expect((await registerClan(id, aube.chief)).status).toBe(201);
        expect((await register(id, aube.mem[0]!)).status).toBe(201);
        const both = await Promise.all([register(id, aube.mem[1]!), register(id, aube.mem[2]!)]);
        expect(both.map((r) => r.status).sort()).toEqual([201, 409]);
        expect(both.find((r) => r.status === 409)!.body.error.code).toBe("CLAN_FULL");
        expect(await countWhere("SELECT COUNT(*) AS n FROM raid_registrations WHERE event_id = ?1", id)).toBe(2);
        // Unregistered before the lock: the place is free again.
        const winner = both[0]!.status === 201 ? aube.mem[1]! : aube.mem[2]!;
        const other = winner === aube.mem[1] ? aube.mem[2]! : aube.mem[1]!;
        expect((await api("DELETE", `${V}/raids/events/${id}/register`, { token: winner.token })).status).toBe(200);
        expect((await register(id, other)).status).toBe(201);
    });
});

describe("raids: clan raids", () => {
    it("internal to the clan, scheduled by its chief or officers, raids in common by invitation", async () => {
        const aube = await team("AUBE", 1, 2);
        const nebu = await team("NEBU", 0, 1);
        const body = { format: "court", starts_at: inMin(24 * 60), capacity: 3 };
        expect((await api("POST", `${V}/raids/events`, { token: aube.mem[0]!.token, body })).status).toBe(403);
        expect((await api("POST", `${V}/raids/events`, { token: aube.off[0]!.token, body: { ...body, starts_at: inMin(5) } })).status).toBe(400);
        const c = await api("POST", `${V}/raids/events`, { token: aube.off[0]!.token, body });
        expect(c.status, JSON.stringify(c.body)).toBe(201);
        const id = c.body.data.event.id as string;
        expect(c.body.data.event.state).toBe("inscription");
        expect((await raidNotes(aube.mem[0]!)).map((n) => n.text)).toEqual(["AUBEOff1 a programmé un raid du clan : Asterion (raid court)"]);
        // Internal: another clan does not see it.
        expect((await api("GET", `${V}/raids/events/${id}`, { token: nebu.chief.token })).status).toBe(404);
        expect((await register(id, nebu.mem[0]!)).status).toBe(404);
        // Invitations: the organizer's chief and officers only, another clan's chief or officer answers.
        expect((await api("POST", `${V}/raids/events/${id}/invites`, { token: aube.mem[0]!.token, body: { tag: "NEBU" } })).status).toBe(403);
        expect((await api("POST", `${V}/raids/events/${id}/invites`, { token: aube.off[0]!.token, body: { tag: "ZZZZ" } })).status).toBe(404);
        expect((await api("POST", `${V}/raids/events/${id}/invites`, { token: aube.off[0]!.token, body: { tag: "nebu" } })).status).toBe(201);
        expect((await api("POST", `${V}/raids/events/${id}/invites`, { token: aube.off[0]!.token, body: { tag: "NEBU" } })).body.error.code).toBe("ALREADY_INVITED");
        const inv = await api("GET", `${V}/raids`, { token: nebu.chief.token });
        expect(inv.body.data.invitations).toHaveLength(1);
        expect(inv.body.data.invitations[0].event.organizer.tag).toBe("AUBE");
        expect((await raidNotes(nebu.chief)).map((n) => n.text)).toEqual(["[AUBE] invite [NEBU] à un raid commun : Asterion (raid court)"]);
        const answer = (p: TestPlayer, accept: boolean) => api("POST", `${V}/raids/events/${id}/invitation`, { token: p.token, body: { accept } });
        expect((await answer(nebu.mem[0]!, true)).status).toBe(403);
        const both = await Promise.all([answer(nebu.chief, true), answer(nebu.chief, true)]);
        expect(both.map((r) => r.status).sort()).toEqual([200, 409]);
        expect(await countWhere("SELECT COUNT(*) AS n FROM raid_clan_entries WHERE event_id = ?1", id)).toBe(2);
        expect((await raidNotes(aube.off[0]!)).some((n) => n.text === "[NEBU] a accepté votre raid commun : Asterion (raid court)")).toBe(true);
        // Capacity 3 for the whole raid, whatever the clan.
        expect((await register(id, nebu.mem[0]!)).status).toBe(201);
        expect((await register(id, aube.mem[0]!)).status).toBe(201);
        expect((await register(id, aube.mem[1]!)).status).toBe(201);
        const full = await register(id, aube.chief);
        expect(full.body.error.code).toBe("RAID_FULL");
        // Only the organizer cancels; everyone registered is told.
        expect((await api("DELETE", `${V}/raids/events/${id}`, { token: nebu.chief.token })).status).toBe(403);
        expect((await api("DELETE", `${V}/raids/events/${id}`, { token: aube.mem[0]!.token })).status).toBe(403);
        expect((await api("DELETE", `${V}/raids/events/${id}`, { token: aube.chief.token })).status).toBe(200);
        expect((await raidNotes(nebu.mem[0]!)).some((n) => n.text === "Raid annulé : Asterion (raid court)")).toBe(true);
        const hist = await api("GET", `${V}/raids`, { token: aube.mem[0]!.token });
        expect(hist.body.data.clan_events).toHaveLength(0);
        expect(hist.body.data.history[0].state).toBe("annule");
    });
});

describe("raids: reminders and notifications", () => {
    it("each reminder once, at its time, as each player chose; a moved event reminds again", async () => {
        const a = await admin();
        const aube = await team("AUBE", 0, 2);
        const [pa, pb] = aube.mem as [TestPlayer, TestPlayer];
        expect((await api("PUT", `${V}/raids/prefs`, { token: pb.token, body: { reminders: [30] } })).status).toBe(400);
        const prefs = await api("PUT", `${V}/raids/prefs`, { token: pb.token, body: { reminders: [10, 1440] } });
        expect(prefs.body.data.prefs).toEqual({ reminders: [1440, 10], member_updates: true });
        const id = await worldEvent(a, 60);
        expect((await registerClan(id, aube.chief)).status).toBe(201);
        expect((await register(id, pa)).status).toBe(201);
        expect((await register(id, pb)).status).toBe(201);
        const t = Date.now();
        const reminders = async (p: TestPlayer) => (await raidNotes(p)).filter((n) => n.data.event === "reminder" || n.data.event === "start");
        // One hour before: only the player who kept that reminder; never twice.
        expect(await raidTick(env.DB, t)).toBe(1);
        expect(await raidTick(env.DB, t)).toBe(0);
        expect((await reminders(pa)).map((n) => n.text)).toEqual(["Rappel : Asterion (raid standard) commence dans 1 h"]);
        expect(await reminders(pb)).toHaveLength(0);
        // Ten minutes before, then the start: both players, once each.
        expect(await raidTick(env.DB, t + 50 * 60000)).toBe(2);
        expect(await raidTick(env.DB, t + 50 * 60000)).toBe(0);
        expect(await raidTick(env.DB, t + 61 * 60000)).toBe(2);
        expect(await raidTick(env.DB, t + 62 * 60000)).toBe(0);
        expect((await reminders(pb)).map((n) => n.text).sort()).toEqual([
            "C'est l'heure : Asterion (raid standard) commence (le combat arrive dans une prochaine version)",
            "Rappel : Asterion (raid standard) commence dans 10 min",
        ]);
        // Moved: the registered players are told, and reminded again at the new time.
        expect((await api("PATCH", `${V}/admin/raids/${id}`, { token: a.token, body: { starts_at: inMin(120) } })).status).toBe(200);
        expect((await raidNotes(pa)).some((n) => n.text === "Horaire modifié : Asterion (raid standard)")).toBe(true);
        expect(await raidTick(env.DB, t + 61 * 60000)).toBe(1);
        expect(await raidTick(env.DB, t + 61 * 60000)).toBe(0);
    });

    it("« Tout marquer lu » of the raid panel leaves the other notifications unread", async () => {
        const a = await admin();
        const aube = await team("AUBE", 0, 1);
        await worldEvent(a);
        const p = aube.mem[0]!;
        await env.DB.prepare(
            "INSERT INTO notifications (id, player_id, kind, data, count, created_at) VALUES ('n-clan-1', ?1, 'clan', '{\"event\":\"accepted\"}', 1, ?2)",
        )
            .bind(p.id, new Date().toISOString())
            .run();
        expect((await api("POST", `${V}/notifications/read-all?filter=nope`, { token: p.token })).status).toBe(400);
        const r = await api("POST", `${V}/notifications/read-all?filter=raids`, { token: p.token });
        expect(r.body.data).toEqual({ read: 1, unread: 1 });
        expect((await api("GET", `${V}/raids`, { token: p.token })).body.data.unread).toBe(0);
    });
});
