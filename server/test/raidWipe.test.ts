// Clan raids « Asterion », 2.75 (raidWipe.ts, docs/RAIDS.md §17): a group wiped (every player down at
// once, by the consensus of its games) is out of its zone for good: its players only spectate (a join
// gives them a spectator link, never a seat; their socket watches), nothing it reports counts any more;
// the zone stays « à reprendre » until a group that finished its own zone takes it again in a fresh
// instance of that zone, whose reports alone validate it.

import { env, SELF } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { Msg, WATCHER_SEAT } from "../src/coop";
import { api, freshIp, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { raidGroups } from "./raidGroups";
import { catalogByPart } from "../src/raidMap";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";

interface Clan {
    id: string;
    tag: string;
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
    return { id, tag, players };
}

async function runningEvent(...clans: Clan[]): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-10), iso(-40), iso(50), now)
        .run();
    for (const c of clans) {
        await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
            .bind(id, c.id, c.players[0]!.id, now)
            .run();
        let k = 0;
        for (const p of c.players)
            await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
                .bind(id, p.id, c.id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
                .run();
    }
    return id;
}

/** `players` active in an instance (as its relay writes them), started 10 min ago. */
async function seat(id: string, players: TestPlayer[]) {
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(id, iso(-10), players.length).run();
    let s = 0;
    for (const p of players)
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at) VALUES (?1, ?2, ?3, 1, ?4, NULL)")
            .bind(id, p.id, s++, iso(-10))
            .run();
}

async function instanceOf(event: string, players: TestPlayer[]): Promise<string> {
    const r = await api("POST", `${V}/raids/events/${event}/instance`, { token: players[0]!.token, body: {} });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    const id = r.body.data.instance.id as string;
    await seat(id, players);
    return id;
}

async function placedIn(instance: string): Promise<number> {
    return (await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_placements WHERE instance_id = ?1").bind(instance).first<{ n: number }>())!.n;
}

const report = (p: TestPlayer, instance: string, body: Record<string, unknown>) =>
    api("POST", `${V}/raids/instances/${instance}/reports`, { token: p.token, body: { kind: "objective", zone: 1, tick: 6000, hash: H1, ...body } });
const branchView = (ev: string, p: TestPlayer) => api("GET", `${V}/raids/events/${ev}/branches/bras_droit`, { token: p.token });
const zoneOf = (view: { body: { data: { zones: { id: number; to_retake: boolean }[] } } }, id: number) => view.body.data.zones.find((z) => z.id === id)!;

describe("raids 2.75: a group wiped, its spectators, its zone taken again", () => {
    it("wipe → spectators only (no seat, nothing counts); §41: only a WAITING group of the clan replaces it, whole, from any branch", { timeout: 30000 }, async () => {
        const a = await clan("AUBE", 4);
        const b = await clan("NEBU", 1);
        const ev = await runningEvent(a, b);
        const [a1, a2, a3, a4] = a.players as [TestPlayer, TestPlayer, TestPlayer, TestPlayer];
        // §41: AUBE's seven groups: a1, a2 in the Bras droit's (group 3), a3 in the Crâne's (1), a4 in the Bras gauche's (2).
        const ga = await raidGroups(ev, a.id, { bras_droit: [a1, a2], crane: [a3], bras_gauche: [a4] });
        const gb = await raidGroups(ev, b.id, { crane: b.players });
        const ia = await instanceOf(ev, [a1, a2]);
        expect(ia).toBe(ga.bras_droit);
        // The group beats the Épaule's guard (titan 9: an ambush, the way on waits for it), reaches the Biceps (zone 2),
        // then everyone falls there: the wipe needs the majority.
        for (const p of [a1, a2]) await report(p, ia, { objective: "epaule_arene", zone: 1, tick: 3000 });
        expect((await report(a1, ia, { kind: "zone", zone: 2, tick: 7000 })).body.data.status).toBe("pending");
        expect((await report(a2, ia, { kind: "zone", zone: 2, tick: 7000 })).body.data.status).toBe("accepted");
        expect((await report(a1, ia, { kind: "wipe", zone: 2, tick: 9000 })).body.data.status).toBe("pending");
        expect((await report(a2, ia, { kind: "wipe", zone: 2, tick: 9000 })).body.data.status).toBe("accepted");
        const row = await env.DB.prepare("SELECT wiped_zone FROM raid_instances WHERE id = ?1").bind(ia).first<{ wiped_zone: number }>();
        expect(row!.wiped_zone).toBe(2);
        const spect = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_spectators WHERE event_id = ?1").bind(ev).first<{ n: number }>();
        expect(spect!.n).toBe(2);
        // Nothing the wiped group reports counts any more: its zone is not validated by it.
        const late = await report(a1, ia, { objective: "biceps_gardien", zone: 2, tick: 9500 });
        expect(late.body.data.status).toBe("rejected");
        expect((await report(a2, ia, { objective: "biceps_gardien", zone: 2, tick: 9500 })).body.data.status).toBe("rejected");
        // Back from the menu: a spectator link (never a seat), on both join routes.
        const back = await api("POST", `${V}/raids/events/${ev}/instance`, { token: a1.token, body: {} });
        expect(back.status).toBe(200);
        expect(back.body.data.spectator).toBe(true);
        expect(back.body.data.instance.id).toBe(ia);
        const join = await api("POST", `${V}/raids/events/${ev}/branches/bras_droit/join`, { token: a2.token, body: {} });
        expect(join.body.data.spectator).toBe(true);
        expect(join.body.data.instance.wiped_zone).toBe(2);
        // Helping a wiped group is refused to the others.
        const help = await api("POST", `${V}/raids/events/${ev}/instance`, { token: b.players[0]!.token, body: { instance_id: ia } });
        expect(help.status).toBe(409);
        // The zone is « à reprendre » on the branch's map (`retake_by`: the group wiped there, read by the game's
        // panel and large map), and the titan's feed tells the wipe.
        const wipedView = zoneOf(await branchView(ev, b.players[0]!), 2) as unknown as { to_retake: boolean; retake_by: string | null };
        expect(wipedView.to_retake).toBe(true);
        expect(wipedView.retake_by).toBe("[AUBE]·3");
        const feed1 = (await api("GET", `${V}/raids/events/${ev}/titan`, { token: b.players[0]!.token })).body.data.feed as { kind: string; text: string }[];
        expect(feed1.some((f) => f.kind === "wipe" && f.text.startsWith("Groupe éliminé"))).toBe(true);
        // §41: the Crâne's group (a3) replaces it only once it WAITS (Skullmaw fallen): before, refused.
        const ic = await instanceOf(ev, [a3]);
        expect(ic).toBe(ga.crane);
        const early = await api("POST", `${V}/raids/instances/${ic}/retake`, { token: a3.token, body: { zone: 2, branch: "bras_droit" } });
        expect(early.status).toBe(409);
        expect(early.body.error.code).toBe("RAID_NOT_WAITING");
        const fall = async (part: string) => {
            const c = catalogByPart(part)!;
            await env.DB.prepare("INSERT INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, NULL, ?4)")
                .bind(ev, c.id, c.objectives.find((o) => o.role === "gigaboss")!.id, new Date().toISOString())
                .run();
        };
        await fall("crane");
        const mine = (await branchView(ev, a3)).body.data.my;
        expect(mine.group_waiting).toBe(true);
        expect(mine.group_instance_id).toBe(ic);
        expect((mine.retake_offers as { zone: number }[]).map((o) => o.zone)).toEqual([2]);
        // Another clan's waiting group never replaces AUBE's (each clan its own seven groups): NEBU still has its own
        // group in the Bras droit; with it wiped too, AUBE's zone is still not NEBU's to take.
        const nb = await instanceOf(ev, b.players);
        expect(nb).toBe(gb.crane);
        const retakeNebu = () => api("POST", `${V}/raids/instances/${nb}/retake`, { token: b.players[0]!.token, body: { zone: 2, branch: "bras_droit" } });
        expect((await retakeNebu()).body.error.code).toBe("RAID_BRANCH_TAKEN");
        await env.DB.prepare("UPDATE raid_instances SET wiped_zone = 1, wiped_at = ?2 WHERE id = ?1").bind(gb.bras_droit, new Date().toISOString()).run();
        expect((await retakeNebu()).body.error.code).toBe("RAID_NOTHING_TO_RETAKE");
        const before = await placedIn(ic);
        expect(before).toBeGreaterThanOrEqual(1);
        const take = await api("POST", `${V}/raids/instances/${ic}/retake`, { token: a3.token, body: { zone: 2, branch: "bras_droit" } });
        expect(take.status, JSON.stringify(take.body)).toBe(200);
        const fresh = take.body.data.instance;
        expect(fresh.id).not.toBe(ic);
        expect(fresh.part).toBe("bras_droit");
        expect(fresh.group).toBe(1); // the Crâne's group, now in the Bras droit
        expect(fresh.start_zone).toBe(2);
        expect(fresh.relieves).toBe(ia);
        expect(take.body.data.spectator).toBe(false);
        // The whole group went (its placements follow it).
        expect(await placedIn(ic)).toBe(0);
        expect(await placedIn(fresh.id)).toBe(before);
        // Never two groups in a branch: the Bras gauche's group, waiting too, cannot go there now.
        await fall("bras_gauche");
        const ig = await instanceOf(ev, [a4]);
        const second = await api("POST", `${V}/raids/instances/${ig}/retake`, { token: a4.token, body: { zone: 2, branch: "bras_droit" } });
        expect(second.status).toBe(409);
        expect(second.body.error.code).toBe("RAID_BRANCH_TAKEN");
        // A spectator never takes a zone again.
        expect((await api("POST", `${V}/raids/instances/${ia}/retake`, { token: a1.token, body: { zone: 2 } })).status).toBe(403);
        // The group plays the zone: its report validates it, and the zone is no longer to retake.
        await seat(fresh.id, [a3]);
        expect((await report(a3, fresh.id, { objective: "biceps_gardien", zone: 2, tick: 3000 })).body.data.status).toBe("accepted");
        const done = await env.DB.prepare("SELECT instance_id FROM raid_branch_objectives WHERE event_id = ?1 AND objective_id = 'biceps_gardien'").bind(ev).first<{ instance_id: string }>();
        expect(done!.instance_id).toBe(fresh.id);
        expect(zoneOf(await branchView(ev, a3), 2).to_retake).toBe(false);
        // The feed tells the zone taken back by a reinforcement group.
        const feed2 = (await api("GET", `${V}/raids/events/${ev}/titan`, { token: a3.token })).body.data.feed as { kind: string; text: string }[];
        expect(feed2.some((f) => f.kind === "retake" && f.text.startsWith("Zone reprise par un groupe de renfort"))).toBe(true);
    });

    it("a spectator's socket watches the game and never takes a seat; a wiped relay refuses seats", async () => {
        const a = await clan("AUBE", 2);
        const ev = await runningEvent(a);
        await raidGroups(ev, a.id, { bras_droit: a.players });
        const r = await api("POST", `${V}/raids/events/${ev}/instance`, { token: a.players[0]!.token, body: {} });
        const inst = r.body.data.instance.id as string;
        const hello = Uint8Array.from([Msg.Hello, 1, 255, 4, 84, 101, 115, 116]);
        const open = async (p: TestPlayer, watch = false) => {
            const res = await SELF.fetch(`http://cosmic.test${V}/raids/instances/${inst}/socket${watch ? "?watch=1" : ""}`, {
                headers: { upgrade: "websocket", authorization: `Bearer ${p.token}`, "cf-connecting-ip": freshIp() },
            });
            expect(res.status).toBe(101);
            const ws = res.webSocket!;
            const got: Uint8Array[] = [];
            ws.accept();
            ws.binaryType = "arraybuffer";
            ws.addEventListener("message", (ev) => got.push(new Uint8Array(ev.data as ArrayBuffer)));
            ws.send(hello);
            await new Promise((x) => setTimeout(x, 30));
            return got;
        };
        const player = await open(a.players[0]!);
        expect(player.find((m) => m[0] === Msg.Welcome)![1]).toBe(0);
        // A watch asked: the seat it is told is the watcher's, never one of the eight.
        const watcher = await open(a.players[1]!, true);
        expect(watcher.find((m) => m[0] === Msg.Welcome)![1]).toBe(WATCHER_SEAT);
        // The group wiped (accepted): the relay closes its seats; every socket after it only watches.
        await seat(inst, a.players);
        for (const p of a.players) await report(p, inst, { kind: "wipe", zone: 1, tick: 4000 });
        await env.COOP_ROOMS.get(env.COOP_ROOMS.idFromName(inst)).fetch("https://coop/wiped");
        const again = await open(a.players[0]!);
        expect(again.find((m) => m[0] === Msg.Welcome)![1]).toBe(WATCHER_SEAT);
        const targets = await api("GET", `${V}/raids/instances/${inst}/watch`, { token: a.players[0]!.token });
        expect(targets.status).toBe(200);
        expect(targets.body.data.spectator).toBe(true);
        expect(targets.body.data.targets).toEqual([]); // nobody of the branch still plays
    });
});
