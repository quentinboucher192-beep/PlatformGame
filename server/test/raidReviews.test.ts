// 2.77.4 (raidReviews.ts, docs/RAIDS.md §24): the players' reviews of a raid event. Only a player who
// really played it may review it: a seat in one of its instances (written by the relay) and a
// contribution accepted by the instance's consensus, read by the server from its own tables (never
// from what the game says). 1 to 5 stars, an optional comment of 500 characters at most, one review
// per player and event (changed or deleted by its author), the average and the count on the cards and
// the sheet, the comments' moderation (filters, reports, a comment masked by an administrator).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { averageOf } from "../src/raidReviews";
import type { Contribution } from "../src/raidRewards";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { raidGroups } from "./raidGroups";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";
const TICK = 150000; // 20 min 50 s into the instance (started 25 min ago)

interface Clan {
    id: string;
    players: TestPlayer[];
}

async function clan(tag: string, n: number): Promise<Clan> {
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
    return { id, players };
}

/** A world event running for 30 min, `c` registered with all its players. */
async function runningEvent(c: Clan): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-30), iso(-60), iso(30), now)
        .run();
    await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
        .bind(id, c.id, c.players[0]!.id, now)
        .run();
    let k = 0;
    for (const p of c.players)
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(id, p.id, c.id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
            .run();
    await raidGroups(id, c.id, { bras_droit: c.players }); // §41: the clan's seven groups (14 registered at least)
    return id;
}

/** The instance of the clan's group, `seated` in its seats (what the relay writes as they take them). */
async function instanceOf(event: string, seated: TestPlayer[]): Promise<string> {
    const r = await api("POST", `${V}/raids/events/${event}/instance`, { token: seated[0]!.token, body: {} });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    const id = r.body.data.instance.id as string;
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(id, iso(-25), seated.length).run();
    let seat = 0;
    for (const p of seated)
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, NULL, 0)")
            .bind(id, p.id, seat++, iso(-25))
            .run();
    return id;
}

function row(seat: number): Contribution {
    return { seat, from: 0, damage: 4000, boss_damage: 0, avoided: 600, blocks: 4, rescues: 1, vents: 0, weak_points: 0, objectives: 2, ticks: 90000, gigaboss: 0 };
}

/**
 * A clan of 6 in a running event: `played` players held a seat and played (their table accepted by the
 * consensus of the instance), one more held a seat without playing (`idle`), one is only registered
 * (`registered`), and one is not even a member of a clan taking part (`stranger`).
 */
async function setup(played = 3) {
    const a = await clan("AUBE", played + 2);
    const ev = await runningEvent(a);
    const seated = a.players.slice(0, played + 1);
    const inst = await instanceOf(ev, seated);
    const records = seated.slice(0, played).map((_, i) => row(i));
    const statuses: string[] = [];
    for (const p of seated) {
        const r = await api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { kind: "result", zone: 1, tick: TICK, hash: H1, records } });
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        statuses.push(r.body.data.status + ":" + (r.body.data.reason ?? ""));
    }
    expect(statuses.join(" | ")).toContain("accepted");
    const n = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_contributions WHERE event_id = ?1 AND player_id IS NOT NULL AND ticks > 0").bind(ev).first<{ n: number }>();
    expect(n!.n).toBe(played);
    const stranger = await registerPlayer("Passant");
    return { ev, inst, players: seated.slice(0, played), idle: seated[played]!, registered: a.players[played + 1]!, stranger };
}

async function admin(): Promise<TestPlayer> {
    const p = await registerPlayer("Admin1");
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

const put = (p: TestPlayer, ev: string, body: unknown) => api("PUT", `${V}/raids/events/${ev}/review`, { token: p.token, body });
const list = (p: TestPlayer | null, ev: string) => api("GET", `${V}/raids/events/${ev}/reviews`, p ? { token: p.token } : {});
const reviewCount = async (ev: string) => (await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_reviews WHERE event_id = ?1").bind(ev).first<{ n: number }>())!.n;

describe("raid reviews: who may review (the server's own data)", () => {
    it("a player who did not play cannot review: registered only, seated without playing, outside the event, or saying so", async () => {
        const s = await setup();
        for (const p of [s.idle, s.registered, s.stranger]) {
            const r = await put(p, s.ev, { stars: 5, body: "Super raid !" });
            expect(r.status, p.username).toBe(403);
            expect(r.body.error.code).toBe("RAID_NOT_PLAYED");
            expect(r.body.error.message).toBe("Seuls les joueurs qui ont joué cet événement peuvent le noter.");
            const l = await list(p, s.ev);
            expect(l.status).toBe(200);
            expect(l.body.data.me).toEqual({ played: false, can_review: false, review: null });
        }
        // What the game could claim is never read: an unknown field is refused, and the rule stays the server's.
        expect((await put(s.idle, s.ev, { stars: 5, played: true })).status).toBe(400);
        expect((await put(s.idle, s.ev, { stars: 5, player_id: s.players[0]!.id })).status).toBe(400);
        // Without an account: nothing written, the list readable (a world event is public).
        expect((await api("PUT", `${V}/raids/events/${s.ev}/review`, { body: { stars: 4 } })).status).toBe(401);
        const anon = await list(null, s.ev);
        expect(anon.status).toBe(200);
        expect(anon.body.data.me).toBeNull();
        expect(await reviewCount(s.ev)).toBe(0);
        // A player of the instance: allowed.
        expect((await list(s.players[0]!, s.ev)).body.data.me).toEqual({ played: true, can_review: true, review: null });
        // An unknown or unpublished event.
        expect((await put(s.players[0]!, crypto.randomUUID(), { stars: 4 })).status).toBe(404);
        await env.DB.prepare("UPDATE raid_events SET published_at = NULL WHERE id = ?1").bind(s.ev).run();
        expect((await put(s.players[0]!, s.ev, { stars: 4 })).status).toBe(404);
    });
});

describe("raid reviews: a participant's review", () => {
    it("written, changed, deleted by its author; shown first to them with « mine »", async () => {
        const s = await setup();
        const [p1, p2] = s.players as [TestPlayer, TestPlayer];
        const w = await put(p1, s.ev, { stars: 4, body: "  Belle coordination du groupe 1.  " });
        expect(w.status, JSON.stringify(w.body)).toBe(201);
        expect(w.body.data.created).toBe(true);
        expect(w.body.data.review).toMatchObject({ stars: 4, body: "Belle coordination du groupe 1.", mine: true, hidden: false, edited_at: null });
        expect(w.body.data.summary).toEqual({ average: 4, count: 1 });
        // Changed: the same review (its id kept), edited_at set.
        const c = await put(p1, s.ev, { stars: 2, body: "Le gigaboss est trop long." });
        expect(c.status).toBe(200);
        expect(c.body.data.created).toBe(false);
        expect(c.body.data.review.id).toBe(w.body.data.review.id);
        expect(c.body.data.review.edited_at).not.toBeNull();
        // The stars only (the comment is optional), by another participant.
        expect((await put(p2, s.ev, { stars: 5 })).status).toBe(201);
        const mine = await list(p1, s.ev);
        expect(mine.body.data.me.review).toMatchObject({ stars: 2, body: "Le gigaboss est trop long.", mine: true });
        expect(mine.body.data.reviews.map((r: { author: { username: string } }) => r.author.username)).toEqual([p2.username]); // mine apart
        const other = await list(s.stranger, s.ev);
        expect(other.body.data.reviews).toHaveLength(2);
        expect(other.body.data.reviews[0]).toMatchObject({ stars: 5, body: "", has_body: false, mine: false, can_report: false });
        expect(other.body.data.reviews[1]).toMatchObject({ stars: 2, body: "Le gigaboss est trop long.", can_report: true });
        // Deleted by its author; deleting it again: nothing left.
        const d = await api("DELETE", `${V}/raids/events/${s.ev}/review`, { token: p1.token });
        expect(d.status).toBe(200);
        expect(d.body.data.summary).toEqual({ average: 5, count: 1 });
        expect((await api("DELETE", `${V}/raids/events/${s.ev}/review`, { token: p1.token })).status).toBe(404);
        expect((await list(p1, s.ev)).body.data.me.review).toBeNull();
        // Another player cannot delete it for them: DELETE is always the session's own review.
        expect((await api("DELETE", `${V}/raids/events/${s.ev}/review`, { token: s.stranger.token })).status).toBe(404);
        expect(await reviewCount(s.ev)).toBe(1);
    });

    it("one review per player and event, even for two writes at once", async () => {
        const s = await setup(1);
        const p = s.players[0]!;
        const both = await Promise.all([put(p, s.ev, { stars: 3 }), put(p, s.ev, { stars: 4 })]);
        expect(both.every((r) => r.status === 200 || r.status === 201)).toBe(true);
        expect(await reviewCount(s.ev)).toBe(1);
        expect((await put(p, s.ev, { stars: 1, body: "Encore ?" })).status).toBe(200);
        expect(await reviewCount(s.ev)).toBe(1);
        const l = await list(p, s.ev);
        expect(l.body.data.summary).toEqual({ average: 1, count: 1 });
    });

    it("stars from 1 to 5, a comment of 500 characters at most (the comments' filters)", async () => {
        const s = await setup(1);
        const p = s.players[0]!;
        for (const stars of [0, 6, "5"]) expect((await put(p, s.ev, { stars })).status, String(stars)).toBe(400);
        const long = await put(p, s.ev, { stars: 4, body: "a".repeat(501) });
        expect(long.status).toBe(400);
        expect(long.body.error.code).toBe("VALIDATION_ERROR");
        expect(long.body.error.details[0].field).toBe("body");
        // 500 characters, accented ones counted as one each.
        expect((await put(p, s.ev, { stars: 4, body: "é".repeat(500) })).status).toBe(201);
        // The comments' filters: links (spam), control characters.
        expect((await put(p, s.ev, { stars: 4, body: "https://a.example https://b.example https://c.example" })).status).toBe(400);
        expect((await put(p, s.ev, { stars: 4, body: "bip\u0007" })).status).toBe(400);
        expect(await reviewCount(s.ev)).toBe(1);
    });
});

describe("raid reviews: moderation", () => {
    it("reported three times, the comment is masked; an administrator masks or restores a comment (Journal)", async () => {
        const s = await setup();
        const [p1, p2, p3] = s.players as [TestPlayer, TestPlayer, TestPlayer];
        const a = await admin();
        const r1 = (await put(p1, s.ev, { stars: 1, body: "Texte insultant" })).body.data.review.id as string;
        const r2 = (await put(p2, s.ev, { stars: 5, body: "Un très bon raid." })).body.data.review.id as string;
        // Reports (the comments' reasons): never one's own, once per player, masked at the third.
        const report = (p: TestPlayer, id: string, reason = "harassment") => api("POST", `${V}/raids/reviews/${id}/report`, { token: p.token, body: { reason } });
        expect((await report(p1, r1)).status).toBe(403);
        expect((await report(p2, r1, "nope")).status).toBe(400);
        for (const p of [p2, p3, s.idle]) expect((await report(p, r1)).status).toBe(200);
        expect((await report(p2, r1)).status).toBe(200); // a second report by the same player counts once
        const seen = await list(s.stranger, s.ev);
        const masked = seen.body.data.reviews.find((r: { id: string }) => r.id === r1);
        expect(masked).toMatchObject({ hidden: true, body: "", stars: 1, can_report: false });
        const own = await list(p1, s.ev);
        expect(own.body.data.me.review).toMatchObject({ hidden: true, hidden_reason: "reports", body: "Texte insultant" });
        // Its stars still count: the rating comes from a real participant.
        expect(seen.body.data.summary).toEqual({ average: 3, count: 2 });

        // ADMINISTRATION › Raids › Avis: administrators only (the route does not exist for the others).
        expect((await api("GET", `${V}/admin/raids/${s.ev}/reviews`, { token: p1.token })).status).toBe(404);
        const all = await api("GET", `${V}/admin/raids/${s.ev}/reviews`, { token: a.token });
        expect(all.status).toBe(200);
        expect(all.body.data.total).toBe(2);
        expect(all.body.data.reviews[0]).toMatchObject({ id: r1, report_count: 3, hidden: true, hidden_by_reports: true, body: "Texte insultant" });
        expect(all.body.data.reviews[0].reasons).toEqual(["harassment", "harassment", "harassment"]);
        const mod = (id: string, action: string, token = a.token) =>
            api("POST", `${V}/admin/raids/${s.ev}/reviews/${id}/moderate`, { token, body: { action } });
        expect((await mod(r2, "hide", p3.token)).status).toBe(404);
        expect((await mod(r2, "delete")).status).toBe(400);
        const hidden = await mod(r2, "hide");
        expect(hidden.status).toBe(200);
        expect(hidden.body.data.review).toMatchObject({ hidden: true, hidden_by_reports: false });
        const after = await list(s.stranger, s.ev);
        expect(after.body.data.reviews.find((r: { id: string }) => r.id === r2)).toMatchObject({ hidden: true, hidden_reason: "moderation", body: "" });
        expect((await list(p2, s.ev)).body.data.me.review).toMatchObject({ hidden: true, hidden_reason: "moderation", body: "Un très bon raid." });
        // Restored: shown again, its reports cleared.
        expect((await mod(r1, "restore")).status).toBe(200);
        const back = await list(s.stranger, s.ev);
        expect(back.body.data.reviews.find((r: { id: string }) => r.id === r1)).toMatchObject({ hidden: false, body: "Texte insultant" });
        expect((await env.DB.prepare("SELECT report_count AS n FROM raid_reviews WHERE id = ?1").bind(r1).first<{ n: number }>())!.n).toBe(0);
        // The Journal keeps each action of the administrators.
        const log = await env.DB.prepare("SELECT action, details FROM audit_log WHERE action LIKE 'admin_raid_review_%' ORDER BY id").all<{ action: string; details: string }>();
        expect(log.results.map((x) => x.action)).toEqual(["admin_raid_review_hidden", "admin_raid_review_restored"]);
        expect(JSON.parse(log.results[0]!.details).raid).toMatchObject({ id: s.ev, name: "Asterion", review_author: p2.username, stars: 5 });
        // A player who blocks an author no longer sees their review.
        expect((await api("POST", `${V}/social/blocks`, { token: s.stranger.token, body: { player_id: p2.id } })).status).toBeLessThan(300);
        expect((await list(s.stranger, s.ev)).body.data.reviews.map((r: { id: string }) => r.id)).toEqual([r1]);
    });
});

describe("raid reviews: the average and the count", () => {
    it("the average of every review, to one decimal, on the cards of CLANS › RAIDS and the sheet", async () => {
        expect(averageOf(0, 0)).toBeNull();
        expect(averageOf(11, 3)).toBe(3.7);
        expect(averageOf(17, 4)).toBe(4.3); // 4.25: half up
        expect(averageOf(9, 2)).toBe(4.5);
        const s = await setup(4);
        const stars = [5, 4, 2, 4];
        for (let i = 0; i < stars.length; ++i) expect((await put(s.players[i]!, s.ev, { stars: stars[i] })).status).toBe(201);
        const l = await list(s.stranger, s.ev);
        expect(l.body.data.summary).toEqual({ average: 3.8, count: 4 }); // 15 / 4 = 3.75
        expect(l.body.data.total).toBe(4);
        // The sheet and the tab (the world event's card): the same figures.
        expect((await api("GET", `${V}/raids/events/${s.ev}`, { token: s.stranger.token })).body.data.event.reviews).toEqual({ average: 3.8, count: 4 });
        const tab = await api("GET", `${V}/raids`, { token: s.players[0]!.token });
        expect(tab.body.data.world.find((e: { id: string }) => e.id === s.ev).reviews).toEqual({ average: 3.8, count: 4 });
        expect(tab.body.data.clan_events.find((e: { id: string }) => e.id === s.ev).reviews).toEqual({ average: 3.8, count: 4 });
        // A review changed, another deleted: recomputed.
        expect((await put(s.players[2]!, s.ev, { stars: 5 })).status).toBe(200);
        expect((await api("DELETE", `${V}/raids/events/${s.ev}/review`, { token: s.players[3]!.token })).status).toBe(200);
        expect((await list(null, s.ev)).body.data.summary).toEqual({ average: 4.7, count: 3 }); // 14 / 3
        // An event without reviews.
        const a = await admin();
        const adminList = await api("GET", `${V}/admin/raids`, { token: a.token });
        expect(adminList.status).toBe(200);
    });
});
