// 2.80 (raidTests.ts, docs/RAIDS.md « Tester avec des joueurs invités »): an administrator's test of Asterion with invited players.
// Only an administrator creates a test, invites, changes « Invincible » or a group, cancels and ends;
// the invitations go to players connected now, expire, are accepted or declined; an invited game with
// another fingerprint is refused; the relay (a CoopRoom marked as a test) gives at a step where each
// real player plays (TEST_SEATS) and their invincibility; a test never reports anything.

import { env, SELF } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { EndReason, Msg, TEST_NONE } from "../src/coop";
import { INVITE_TTL_MS } from "../src/raidTests";
import { api, freshIp, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const FP = "2.80.0|s17|c3|t7";

async function admin(name: string): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

/** Connected now (as the game's presence says it every minute). */
async function online(p: TestPlayer, visible = true): Promise<void> {
    await env.DB.prepare("UPDATE players SET presence_state = 'active', presence_at = ?2, presence_visible = ?3 WHERE id = ?1")
        .bind(p.id, new Date().toISOString(), visible ? 1 : 0)
        .run();
}

const ZONE = { skill: 1, invincible: true, bots_invincible: false, branch: "asterion_bras_droit", zone: 4, players: 3, start_phase: 1, parts: 127, relics: 0 };

async function createTest(a: TestPlayer, mode: "zone" | "raid" = "zone", config: Record<string, unknown> = ZONE): Promise<string> {
    const r = await api("POST", `${V}/admin/raid-tests`, { token: a.token, body: { mode, config, fingerprint: FP } });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    return r.body.data.test.id as string;
}

// ------------------------------------------------------------------ the relay on the wire

const u32 = (v: number) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
const hello = () => Uint8Array.from([Msg.Hello, 1, 255, 4, ...new TextEncoder().encode("Test")]);
const start = () => Uint8Array.from([Msg.Start, 8]);
const catchupDone = (t: number) => Uint8Array.from([Msg.CatchupDone, ...u32(t)]);
/** INPUT: `n` empty frames from step `first` (one byte each: nothing changes). */
const idle = (first: number, n: number) => Uint8Array.from([Msg.Input, ...u32(first), n, ...Array.from({ length: n }, () => 0)]);

class Game {
    got: Uint8Array[] = [];
    closed = false;
    constructor(public ws: WebSocket) {
        ws.accept();
        ws.binaryType = "arraybuffer";
        ws.addEventListener("message", (ev) => {
            this.got.push(new Uint8Array(ev.data as ArrayBuffer));
        });
        ws.addEventListener("close", () => {
            this.closed = true;
        });
    }
    send(b: Uint8Array) {
        if (!this.closed) this.ws.send(b);
    }
    of(type: number) {
        return this.got.filter((m) => m[0] === type);
    }
    seat(): number {
        return this.of(Msg.Welcome).at(-1)?.[1] ?? -1;
    }
    count(): number {
        const m = this.of(Msg.Frames).at(-1);
        if (!m) return 0;
        const v = new DataView(m.buffer, m.byteOffset, m.byteLength);
        return v.getUint32(1, true) + v.getUint16(5, true);
    }
    /** Every TEST_SEATS received: its step and, per relay seat, [group, seat of the test, flags]. */
    tables(): { step: number; seats: [number, number, number][] }[] {
        return this.of(Msg.TestSeats).map((m) => {
            const v = new DataView(m.buffer, m.byteOffset, m.byteLength);
            const n = m[5]!;
            const seats: [number, number, number][] = [];
            for (let k = 0; k < n; ++k) seats.push([m[6 + k * 3]!, m[7 + k * 3]!, m[8 + k * 3]!]);
            return { step: v.getUint32(1, true), seats };
        });
    }
    endReason(): number {
        return this.of(Msg.End).at(-1)?.[1] ?? -1;
    }
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
async function until(cond: () => boolean | Promise<boolean>, ms = 3000): Promise<void> {
    const end = Date.now() + ms;
    while (!(await cond())) {
        if (Date.now() > end) throw new Error("condition not reached in time");
        await sleep(10);
    }
}

async function socketOf(p: TestPlayer, test: string): Promise<Response> {
    return await SELF.fetch(`http://cosmic.test${V}/raid-tests/${test}/socket`, {
        headers: { upgrade: "websocket", authorization: `Bearer ${p.token}`, "cf-connecting-ip": freshIp() },
    });
}
async function connect(p: TestPlayer, test: string): Promise<Game> {
    const res = await socketOf(p, test);
    expect(res.status).toBe(101);
    return new Game(res.webSocket!);
}

/** The administrator's game starts the test on seat 0 and plays steps [0, to) (its input delay aside). */
async function adminStarts(a: TestPlayer, test: string): Promise<Game> {
    const g = await connect(a, test);
    g.send(hello());
    await until(() => g.seat() === 0);
    g.send(start());
    await until(() => g.of(Msg.Go).length === 1);
    return g;
}
/**
 * The games play steps [from, to) (each from its own first step), in small batches, never further than
 * the relay's window ahead of the steps `watch` was given (as a real game: its input delay).
 */
async function play(games: { g: Game; from: number }[], from: number, to: number, watch: Game) {
    for (let t = from; t < to; t += 16) {
        const n = Math.min(16, to - t);
        await until(() => watch.count() >= t + n - 48, 5000);
        for (const { g, from: f } of games) {
            const a = Math.max(t, f);
            if (a < t + n) g.send(idle(a, t + n - a));
        }
    }
}

// ------------------------------------------------------------------ tests

describe("2.80: the administrator's test with invited players — rights", () => {
    it("only an administrator creates a test, lists the players, invites, changes, cancels and ends", async () => {
        const a = await admin("TestAdmin");
        const p = await registerPlayer("Joueuse");
        const q = await registerPlayer("Joueur2");
        await online(p);
        await online(q);
        // A player: none of the administrator's routes exists for them (the session's role decides).
        expect((await api("POST", `${V}/admin/raid-tests`, { token: p.token, body: { mode: "zone", config: ZONE, fingerprint: FP } })).status).toBe(404);
        expect((await api("GET", `${V}/admin/raid-test-players`, { token: p.token })).status).toBe(404);
        const test = await createTest(a);
        expect((await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: p.token, body: { player_id: q.id } })).status).toBe(404);
        // Invited by the administrator; the player cannot change their « Invincible », nor cancel, nor end.
        const inv = await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: p.id, invincible: false } });
        expect(inv.status, JSON.stringify(inv.body)).toBe(201);
        expect(inv.body.data.invitation.status).toBe("pending");
        expect((await api("PATCH", `${V}/admin/raid-tests/${test}/invitations/${p.id}`, { token: p.token, body: { invincible: true } })).status).toBe(404);
        expect((await api("DELETE", `${V}/admin/raid-tests/${test}/invitations/${p.id}`, { token: p.token })).status).toBe(404);
        expect((await api("POST", `${V}/admin/raid-tests/${test}/end`, { token: p.token, body: {} })).status).toBe(404);
        expect((await api("POST", `${V}/admin/raid-tests/${test}/seat`, { token: p.token, body: { group: -1 } })).status).toBe(404);
        // Another administrator: not their test.
        const b = await admin("AutreAdmin");
        expect((await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: b.token, body: { player_id: q.id } })).status).toBe(404);
        expect((await api("PATCH", `${V}/admin/raid-tests/${test}/invitations/${p.id}`, { token: b.token, body: { invincible: true } })).status).toBe(404);
        expect((await api("GET", `${V}/admin/raid-tests/${test}`, { token: b.token })).status).toBe(404);
        // The owner: « Invincible » on, then the list says it.
        const patch = await api("PATCH", `${V}/admin/raid-tests/${test}/invitations/${p.id}`, { token: a.token, body: { invincible: true } });
        expect(patch.status, JSON.stringify(patch.body)).toBe(200);
        const got = await api("GET", `${V}/admin/raid-tests/${test}`, { token: a.token });
        expect(got.body.data.invitations).toHaveLength(1);
        expect(got.body.data.invitations[0]).toMatchObject({ player: { id: p.id, username: "Joueuse", role: "player" }, invincible: true, status: "pending" });
    });

    it("invites only players connected now, never twice, never more than the simulated players to replace", async () => {
        const a = await admin("TestAdmin");
        const [p, q, off, hidden, other] = await Promise.all(["Alpha", "Bravo", "Absent", "Cache", "AdminB"].map((n) => registerPlayer(n)));
        await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(other!.id).run();
        for (const x of [p!, q!, other!]) await online(x);
        await online(hidden!, false);
        // players = 3: the administrator and two simulated players, so two invited players at most.
        const test = await createTest(a);
        const list = await api("GET", `${V}/admin/raid-test-players?test=${test}`, { token: a.token });
        expect(list.status).toBe(200);
        const names = list.body.data.players.map((x: { username: string }) => x.username).sort();
        expect(names).toEqual(["AdminB", "Alpha", "Bravo"]); // never me, never offline, never a hidden presence
        expect(list.body.data.players.find((x: { username: string }) => x.username === "AdminB").role).toBe("admin");
        const search = await api("GET", `${V}/admin/raid-test-players?q=rav&test=${test}`, { token: a.token });
        expect(search.body.data.players.map((x: { username: string }) => x.username)).toEqual(["Bravo"]);
        const inviteOf = (x: TestPlayer) => api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: x.id } });
        expect((await inviteOf(off!)).body.error.code).toBe("PLAYER_OFFLINE");
        expect((await inviteOf(hidden!)).body.error.code).toBe("PLAYER_OFFLINE");
        expect((await inviteOf(a)).body.error.code).toBe("RAID_TEST_SELF");
        expect((await inviteOf(p!)).status).toBe(201);
        expect((await inviteOf(p!)).body.error.code).toBe("RAID_TEST_ALREADY_INVITED");
        expect((await inviteOf(other!)).status).toBe(201); // an administrator is invited like anyone
        expect((await inviteOf(q!)).body.error.code).toBe("RAID_TEST_GROUP_FULL");
        // Those invited are no longer offered.
        const after = await api("GET", `${V}/admin/raid-test-players?test=${test}`, { token: a.token });
        expect(after.body.data.players.map((x: { username: string }) => x.username)).toEqual(["Bravo"]);
        // A declined invitation frees its seat; a group out of the test's groups is refused.
        expect((await api("POST", `${V}/raid-tests/${test}/decline`, { token: p!.token, body: {} })).status).toBe(200);
        expect((await inviteOf(q!)).status).toBe(201);
        const bad = await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: p!.id, group: 1 } });
        expect(bad.status).toBe(400);
    });

    it("an invitation expires after 3 minutes; the fingerprint must match; the invited player sees it at once", async () => {
        const a = await admin("TestAdmin");
        const p = await registerPlayer("Joueuse");
        await online(p);
        const test = await createTest(a);
        await adminStarts(a, test);
        expect((await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: p.id } })).status).toBe(201);
        // The game reads it with the unread messages (every 30 s), and in its list; a notification too.
        const unread = await api("GET", `${V}/social/unread`, { token: p.token });
        expect(unread.body.data.raid_test_invitations).toHaveLength(1);
        expect(unread.body.data.raid_test_invitations[0]).toMatchObject({ test_id: test, mode: "zone", admin: { username: "TestAdmin" }, fingerprint: FP });
        const notes = await api("GET", `${V}/notifications`, { token: p.token });
        expect(notes.body.data.notifications[0].text).toContain("vous invite à un test d'Asterion");
        // Another version of the game: refused, clearly.
        const wrong = await api("POST", `${V}/raid-tests/${test}/accept`, { token: p.token, body: { fingerprint: "2.79.0|s17|c3|t7" } });
        expect(wrong.status).toBe(409);
        expect(wrong.body.error.code).toBe("RAID_TEST_VERSION");
        // Past its 3 minutes: expired (the administrator sees it so), never accepted.
        await env.DB.prepare("UPDATE raid_test_invitations SET expires_at = ?2 WHERE test_id = ?1")
            .bind(test, new Date(Date.now() - 1000).toISOString())
            .run();
        expect(INVITE_TTL_MS).toBe(180_000);
        const late = await api("POST", `${V}/raid-tests/${test}/accept`, { token: p.token, body: { fingerprint: FP } });
        expect(late.body.error.code).toBe("RAID_TEST_INVITATION_EXPIRED");
        const got = await api("GET", `${V}/admin/raid-tests/${test}`, { token: a.token });
        expect(got.body.data.invitations[0].status).toBe("expired");
        expect((await api("GET", `${V}/raid-tests/invitations`, { token: p.token })).body.data.invitations).toHaveLength(0);
        // Not accepted: no way into the test's relay.
        expect((await socketOf(p, test)).status).toBe(404);
    });

    it("a report naming a test is refused, whoever sends it", async () => {
        const a = await admin("TestAdmin");
        const test = await createTest(a);
        const r = await api("POST", `${V}/raids/instances/${test}/reports`, {
            token: a.token,
            body: { kind: "objective", objective: "generateur_1", zone: 1, tick: 100, hash: "0123456789abcdef" },
        });
        expect(r.status).toBe(403);
        expect(r.body.error.code).toBe("RAID_TEST_REPORT");
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_reports").first<{ n: number }>())?.n).toBe(0);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_instances").first<{ n: number }>())?.n).toBe(0);
    });
});

describe("2.80: the relay of a test", () => {
    it("gives at a step where the invited player plays and their « Invincible », and frees the seat when cancelled", async () => {
        const a = await admin("TestAdmin");
        const p = await registerPlayer("Joueuse");
        const q = await registerPlayer("Curieux");
        await online(p);
        await online(q);
        const test = await createTest(a);
        const ga = await adminStarts(a, test);
        // From step 0: the administrator on seat 0 of group 0, invincible (the test's setting).
        await until(() => ga.tables().length === 1);
        expect(ga.tables()[0]).toEqual({ step: 0, seats: [[0, 0, 1], ...Array.from({ length: 7 }, () => [TEST_NONE, 0, 0])] });
        await play([{ g: ga, from: 0 }], 0, 200, ga);
        await until(() => ga.count() >= 150);
        expect((await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: p.id } })).status).toBe(201);
        const acc = await api("POST", `${V}/raid-tests/${test}/accept`, { token: p.token, body: { fingerprint: FP } });
        expect(acc.status, JSON.stringify(acc.body)).toBe(200);
        expect(acc.body.data.test.config).toMatchObject({ branch: "asterion_bras_droit", zone: 4, players: 3 });
        // Accepted: still offered (a game that lost its link comes back to the test from there).
        const again = await api("GET", `${V}/raid-tests/invitations`, { token: p.token });
        expect(again.body.data.invitations.map((x: { status: string }) => x.status)).toEqual(["accepted"]);
        // A player who did not accept never reaches the relay.
        expect((await socketOf(q, test)).status).toBe(404);
        // The invited game: the history, its catch-up, then its seat from the step the relay chose.
        const gp = await connect(p, test);
        gp.send(hello());
        await until(() => gp.seat() > 0);
        await until(() => gp.of(Msg.Go).length === 1 && gp.tables().length >= 1);
        expect(gp.tables()[0]!.step).toBe(0); // the history of the tables, before the frames
        gp.send(catchupDone(ga.count()));
        await until(() => gp.of(Msg.Rejoin).length >= 1);
        const back = new DataView(gp.of(Msg.Rejoin)[0]!.buffer).getUint32(2, true);
        await play([{ g: ga, from: 0 }, { g: gp, from: back }], 200, back + 100, ga);
        await until(() => ga.tables().length === 2);
        const joined = ga.tables()[1]!;
        expect(joined.step).toBe(back);
        expect(joined.seats[gp.seat()]).toEqual([0, 1, 0]); // group 0, the first simulated player's seat, not invincible
        // « Invincible » ticked by the administrator: at the first step not given yet, for every game.
        const given = ga.count();
        expect((await api("PATCH", `${V}/admin/raid-tests/${test}/invitations/${p.id}`, { token: a.token, body: { invincible: true } })).status).toBe(200);
        await until(() => ga.tables().length === 3 && gp.tables().length === 3);
        const inv = ga.tables()[2]!;
        expect(inv.step).toBeGreaterThanOrEqual(given);
        expect(inv.seats[gp.seat()]).toEqual([0, 1, 1]);
        expect(gp.tables()[2]).toEqual(inv);
        // The player cannot do it themselves.
        expect((await api("PATCH", `${V}/admin/raid-tests/${test}/invitations/${p.id}`, { token: p.token, body: { invincible: false } })).status).toBe(404);
        // 2.79's orders (the wheel): the administrator's, given from a step to every game; an order out of bounds closes
        // nothing here, the administrator's is checked (kind 1..6, place in 4 px units).
        const before = ga.count();
        ga.send(Uint8Array.from([Msg.TestOrder, 2, 44, 1, 100, 0]));
        await until(() => gp.of(Msg.TestOrder).length === 1 && ga.of(Msg.TestOrder).length === 1);
        const order = gp.of(Msg.TestOrder)[0]!;
        expect(new DataView(order.buffer, order.byteOffset).getUint32(1, true)).toBeGreaterThanOrEqual(before);
        expect(Array.from(order.subarray(5))).toEqual([2, 44, 1, 100, 0]);
        // Where they play, as the administrator's list shows it.
        const got = await api("GET", `${V}/admin/raid-tests/${test}`, { token: a.token });
        expect(got.body.data.invitations[0]).toMatchObject({ status: "accepted", invincible: true, playing: { group: 0, seat: 1, connected: true } });
        // Cancelled: out at once (END « removed »), the seat is its simulated player's again from a step.
        expect((await api("DELETE", `${V}/admin/raid-tests/${test}/invitations/${p.id}`, { token: a.token })).status).toBe(200);
        await until(() => gp.endReason() === EndReason.Removed);
        await play([{ g: ga, from: 0 }], back + 100, back + 200, ga);
        await until(() => ga.tables().length === 4);
        expect(ga.tables()[3]!.seats[gp.seat()]).toEqual([TEST_NONE, 0, 0]);
        expect((await socketOf(p, test)).status).toBe(404);
        // Nothing of a raid was written.
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_instance_members").first<{ n: number }>())?.n).toBe(0);
        // The administrator ends it: every game is told, the test is over.
        expect((await api("POST", `${V}/admin/raid-tests/${test}/end`, { token: a.token, body: {} })).status).toBe(200);
        await until(() => ga.endReason() === EndReason.Finished);
        const row = await env.DB.prepare("SELECT status FROM raid_tests WHERE id = ?1").bind(test).first<{ status: string }>();
        expect(row?.status).toBe("ended");
        expect((await socketOf(a, test)).status).toBe(404);
    });

    it("the whole raid's simulation: the group chosen by the administrator, their own place given at a step", async () => {
        const a = await admin("TestAdmin");
        const p = await registerPlayer("Joueuse");
        await online(p);
        // §41: 21 players over the seven groups of a real raid (3 each).
        const test = await createTest(a, "raid", { skill: 1, invincible: false, bots_invincible: false, players: 21, full_groups: true });
        const ga = await adminStarts(a, test);
        await until(() => ga.tables().length === 1);
        expect(ga.tables()[0]!.seats[0]).toEqual([0, 0, 0]);
        await play([{ g: ga, from: 0 }], 0, 100, ga);
        // Group 3 (index 2), invincible; a group past the raid's groups and reinforcements is refused.
        expect((await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: p.id, group: 30 } })).status).toBe(400);
        expect((await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: p.id, group: 2, invincible: true } })).status).toBe(201);
        expect((await api("POST", `${V}/raid-tests/${test}/accept`, { token: p.token, body: { fingerprint: FP } })).status).toBe(200);
        const gp = await connect(p, test);
        gp.send(hello());
        await until(() => gp.seat() > 0 && gp.of(Msg.Go).length === 1);
        gp.send(catchupDone(ga.count()));
        await until(() => gp.of(Msg.Rejoin).length >= 1);
        const back = new DataView(gp.of(Msg.Rejoin)[0]!.buffer).getUint32(2, true);
        await play([{ g: ga, from: 0 }, { g: gp, from: back }], 100, back + 60, ga);
        await until(() => ga.tables().length === 2);
        expect(ga.tables()[1]!.seats[gp.seat()]).toEqual([2, 1, 1]);
        // The administrator watches (leaves their place), then joins group 2.
        expect((await api("POST", `${V}/admin/raid-tests/${test}/seat`, { token: a.token, body: { group: -1 } })).status).toBe(200);
        await until(() => ga.tables().length === 3);
        expect(ga.tables()[2]!.seats[0]).toEqual([TEST_NONE, 0, 0]);
        expect((await api("POST", `${V}/admin/raid-tests/${test}/seat`, { token: a.token, body: { group: 1 } })).status).toBe(200);
        await until(() => gp.tables().length === 4);
        expect(gp.tables()[3]!.seats[0]).toEqual([1, 0, 0]);
        // An invited player never gives an order (2.79's wheel is the administrator's): their link is closed.
        gp.send(Uint8Array.from([Msg.TestOrder, 1, 0, 0, 0, 0]));
        await until(() => gp.closed);
    });
});
