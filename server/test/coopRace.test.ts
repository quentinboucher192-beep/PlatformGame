// Duo / Trio boards of the « course à la vérification » (2.62, docs/COOP.md § 6): a team attempt
// is verified from its team replay (format 5: every player's trajectory and inputs), ranked only
// once every player confirmed it with their own account, by its most advanced player; Duo and
// Trio never mix, and the solo boards are untouched. A team replay never counts in solo.

import { beforeEach, describe, expect, it } from "vitest";
import { api, buildReplay, EXIT, registerPlayer, seedAll, SPAWN, type TestPlayer } from "./helpers";
import { COOP_SIMULATION_VERSION } from "../src/coopRace";
import { LH, octet, PNG, zip } from "./publish";

beforeEach(seedAll);

const V = "/api/v1";
const ROUTE: [number, number][] = Array.from({ length: 60 }, (_, i) => [SPAWN.x + ((EXIT.x + 10 - SPAWN.x) * i) / 59, SPAWN.y + ((EXIT.y + 40 - SPAWN.y) * i) / 59]);
const LAYOUT = { route: ROUTE, bosses: [{ id: "b1", name: "Gardien" }], ambushes: [{ id: "z1", waves: 2 }] };

async function publishExtreme(author: TestPlayer) {
    const start = await api("POST", `${V}/community/levels`, {
        token: author.token,
        body: { title: "Le Gouffre Final", planet: "pyralis", difficulty: 5, level_hash: LH, min_game_version: "2.16.0", race: LAYOUT },
    });
    expect(start.status).toBe(201);
    const level = JSON.stringify({
        format: "cosmic.level",
        id: "mon_niveau",
        name: "Mon niveau",
        extreme: true,
        lives: 1,
        bounds: [0, 0, 6000, 1200],
        timer: { mode: "limited", limit: 600 },
        objects: [
            { id: "o1", type: "player_start", rect: [SPAWN.x - 5, SPAWN.y - 22, 10, 22] },
            { id: "o2", type: "exit", rect: [EXIT.x, EXIT.y, EXIT.w, EXIT.h] },
        ],
    });
    const data = await zip({
        "COSMIC_LEVEL/manifest.json": JSON.stringify({ format: "cosmic.package", levelId: "mon_niveau" }),
        "COSMIC_LEVEL/level.json": level,
        "COSMIC_LEVEL/metadata/thumbnail.png": PNG,
    });
    const p = await api("PUT", start.body.data.upload.package, { token: author.token, raw: data, headers: octet });
    expect(p.status).toBeLessThan(300);
    return start.body.data.id as string;
}

async function inflate(data: Uint8Array): Promise<Uint8Array> {
    const stream = new Blob([data.subarray(8)]).stream().pipeThrough(new DecompressionStream("deflate"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
}
async function deflate(data: Uint8Array): Promise<Uint8Array> {
    const stream = new Blob([data]).stream().pipeThrough(new CompressionStream("deflate"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
}

interface Mate {
    /** How far along the route it goes (0..1). */
    upTo: number;
    events?: { frame: number; type: number; arg: number; value: number }[];
}

/** A team replay (format 5): player 1 as buildReplay writes it, then the other players. */
async function teamReplay(attemptId: string, durationMs: number, lead: Mate & { finished?: boolean; boss: number; waves: number }, mates: Mate[], o: { revives?: number; incomplete?: boolean; coopVersion?: number } = {}) {
    const extra = [...(lead.events ?? [])];
    if (lead.boss > 0) extra.push({ frame: 200, type: 12, arg: 0, value: lead.boss });
    if (lead.waves > 0) extra.push({ frame: 300, type: 13, arg: 0, value: lead.waves });
    const along = (upTo: number) => (i: number, n: number) => {
        const t = upTo * (i / (n - 1));
        return { x: SPAWN.x + (EXIT.x + 10 - SPAWN.x) * t, y: SPAWN.y + (EXIT.y + 40 - SPAWN.y) * t };
    };
    const solo = await buildReplay({
        levelId: "mon_niveau",
        levelHash: LH,
        attemptId,
        durationMs,
        collectibles: 0,
        finish: lead.finished === true,
        extraEvents: extra,
        inputFormat: 3,
        exact: true,
        path: along(lead.finished ? 1 : lead.upTo),
    });
    const raw = await inflate(solo);
    const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
    const frames = view.getUint32(8, true);
    // Its ticks: in the input section (after the header, the frames and the events).
    const levelLen = raw[68]!;
    const gvLen = raw[69 + levelLen]!;
    const eventsAt = 70 + levelLen + gvLen + frames * 12;
    const inputAt = eventsAt + view.getUint32(16, true) * 8;
    const ticks = view.getUint32(inputAt + 12, true);
    const bytes: number[] = [];
    const u8 = (v: number) => bytes.push(v & 255);
    const u16 = (v: number) => (u8(v), u8(v >>> 8));
    const u32 = (v: number) => {
        for (let i = 0; i < 4; ++i) u8(v >>> (8 * i));
    };
    const f32 = (v: number) => {
        const b = new Uint8Array(new Float32Array([v]).buffer);
        for (const x of b) u8(x);
    };
    u8(1 + mates.length);
    u16(o.coopVersion ?? COOP_SIMULATION_VERSION); // 2.75: the team rules' version (2: players pass through)
    u8(o.incomplete ? 1 : 0);
    u8(o.revives ?? 0);
    for (const m of mates) {
        u32(frames);
        const ev = m.events ?? [];
        u32(ev.length);
        const path = along(m.upTo);
        for (let i = 0; i < frames; ++i) {
            const p = path(i, frames);
            f32(p.x + 14);
            f32(p.y);
            u8(1);
            u8(2);
            u8(10);
            u8(0);
        }
        for (const e of ev) {
            u32(e.frame);
            u8(e.type);
            u8(e.arg);
            u16(e.value);
        }
        // Its inputs: runs of 1000 steps (as player 1's).
        const runs: number[] = [];
        for (let left = ticks; left > 0; left -= Math.min(left, 1000)) runs.push(Math.min(left, 1000));
        u32(runs.length);
        for (const r of runs) {
            u16(r);
            u32(2);
            u32(0);
            u32(0);
            u8(127);
            u8(0);
            u8(0);
            u8(0);
        }
    }
    const full = new Uint8Array(raw.length + bytes.length);
    full.set(raw, 0);
    full.set(bytes, raw.length);
    new DataView(full.buffer).setUint16(4, 5, true); // format 5
    const body = await deflate(full);
    const out = new Uint8Array(8 + body.length);
    out.set(new TextEncoder().encode("CPRZ"), 0);
    new DataView(out.buffer).setUint32(4, full.length, true);
    out.set(body, 8);
    return out;
}

async function teamAttempt(
    team: TestPlayer[],
    levelId: string,
    lead: Mate & { finished?: boolean; boss: number; waves: number },
    mates: Mate[],
    o: { revives?: number; incomplete?: boolean; confirm?: boolean; coopVersion?: number } = {},
) {
    const attemptId = crypto.randomUUID();
    const durationMs = 40000;
    const sub = await api("POST", `${V}/coop/runs`, {
        token: team[0]!.token,
        body: {
            kind: "community",
            level_id: levelId,
            attempt_id: attemptId,
            time_ms: durationMs,
            finished: lead.finished === true,
            bosses: [lead.boss],
            waves: [lead.waves],
            mode: "lan",
            players: team.map((p) => p.id),
        },
    });
    expect(sub.status, JSON.stringify(sub.body)).toBe(201);
    const replay = await teamReplay(attemptId, durationMs, lead, mates, o);
    const r = await api("PUT", sub.body.data.upload, { token: team[0]!.token, raw: replay, headers: octet });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    if (o.confirm !== false)
        for (const p of team.slice(1)) expect((await api("POST", `${V}/coop/attempts/${attemptId}/confirm`, { token: p.token })).status).toBe(200);
    return { attemptId, run: r.body.data.run };
}

const board = (levelId: string, size: number) => api("GET", `${V}/coop/board?kind=community&level_id=${levelId}&size=${size}`);

describe("coop: Duo and Trio boards", () => {
    it("ranks a verified team once every player confirmed, by its most advanced player", async () => {
        const author = await registerPlayer("Auteur");
        const levelId = await publishExtreme(author);
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        const { attemptId, run } = await teamAttempt([nova, lyra], levelId, { upTo: 0.2, boss: 0, waves: 0 }, [{ upTo: 0.6 }], { confirm: false });
        expect(run.status).toBe("VERIFIED");
        // Lyra went further: the team's route is hers (40 points × 0.6).
        expect(run.score).toBeGreaterThanOrEqual(23.9);
        // Not ranked before Lyra confirmed with her own account.
        expect((await board(levelId, 2)).body.data.ranking).toHaveLength(0);
        await api("POST", `${V}/coop/attempts/${attemptId}/confirm`, { token: lyra.token });
        const duo = (await board(levelId, 2)).body.data.ranking;
        expect(duo).toHaveLength(1);
        expect(duo[0].players.map((p: { username: string }) => p.username)).toEqual(["Nova", "Lyra"]);
        // Duo and Trio never mix; the solo board knows nothing of it.
        expect((await board(levelId, 3)).body.data.ranking).toHaveLength(0);
        const solo = await api("GET", `${V}/races/board?kind=community&level_id=${levelId}`);
        expect(solo.body.data.ranking).toHaveLength(0);
    });

    it("2.75: refuses a team attempt played with other team rules (version 1: solid players)", async () => {
        expect(COOP_SIMULATION_VERSION).toBe(4);
        const author = await registerPlayer("Auteur");
        const levelId = await publishExtreme(author);
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        const { run } = await teamAttempt([nova, lyra], levelId, { upTo: 0.3, boss: 0, waves: 0 }, [{ upTo: 0.3 }], { coopVersion: 1 });
        expect(run.status).toBe("REJECTED");
        expect(run.reasons.join(" ")).toContain("version des règles de coopération");
    });

    it("2.80: refuses a team attempt of version 3 (a passage closing behind the first player through)", async () => {
        const author = await registerPlayer("Auteur");
        const levelId = await publishExtreme(author);
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        const { run } = await teamAttempt([nova, lyra], levelId, { upTo: 0.3, boss: 0, waves: 0 }, [{ upTo: 0.3 }], { coopVersion: 3 });
        expect(run.status).toBe("REJECTED");
        expect(run.reasons.join(" ")).toContain("version des règles de coopération");
    });

    it("2.77: refuses a team attempt of version 2 (souls where the player fell, revivals without « Interagir »)", async () => {
        const author = await registerPlayer("Auteur");
        const levelId = await publishExtreme(author);
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        const { run } = await teamAttempt([nova, lyra], levelId, { upTo: 0.3, boss: 0, waves: 0 }, [{ upTo: 0.3 }], { coopVersion: 2 });
        expect(run.status).toBe("REJECTED");
        expect(run.reasons.join(" ")).toContain("version des règles de coopération");
    });

    it("orders the teams: score, then complete before incomplete, then fewer revivals", async () => {
        const author = await registerPlayer("Auteur");
        const levelId = await publishExtreme(author);
        const [a, b, c, d, e, f] = await Promise.all(["Aaa", "Bbb", "Ccc", "Ddd", "Eee", "Fff"].map((n) => registerPlayer(n)));
        const revived = [{ frame: 100, type: 3, arg: 0, value: 0 }, { frame: 140, type: 15, arg: 0, value: 0 }];
        await teamAttempt([a!, b!], levelId, { upTo: 0.5, boss: 0, waves: 0 }, [{ upTo: 0.5, events: revived }], { revives: 1 });
        await teamAttempt([c!, d!], levelId, { upTo: 0.5, boss: 0, waves: 0 }, [{ upTo: 0.5 }], { incomplete: true });
        await teamAttempt([e!, f!], levelId, { upTo: 0.5, boss: 0, waves: 0 }, [{ upTo: 0.5 }]);
        const duo = (await board(levelId, 2)).body.data.ranking;
        expect(duo.map((r: { players: { username: string }[] }) => r.players[0]!.username)).toEqual(["Eee", "Aaa", "Ccc"]);
        expect(duo[1].revives).toBe(1);
        expect(duo[2].incomplete).toBe(true);
    });

    it("refuses what is not a team attempt, and a team replay never counts in solo", async () => {
        const author = await registerPlayer("Auteur");
        const levelId = await publishExtreme(author);
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        const intrus = await registerPlayer("Intrus");
        // Not a member: refused.
        const attemptId = crypto.randomUUID();
        const body = { kind: "community", level_id: levelId, attempt_id: attemptId, time_ms: 40000, finished: false, bosses: [0], waves: [0], mode: "lan", players: [nova.id, lyra.id] };
        expect((await api("POST", `${V}/coop/runs`, { token: intrus.token, body })).status).toBe(403);
        // Online without the relay's session: refused.
        expect((await api("POST", `${V}/coop/runs`, { token: nova.token, body: { ...body, mode: "online" } })).status).toBe(409);
        // A solo replay for a team attempt: rejected.
        const sub = await api("POST", `${V}/coop/runs`, { token: nova.token, body });
        expect(sub.status).toBe(201);
        const solo = await buildReplay({ levelId: "mon_niveau", levelHash: LH, attemptId, durationMs: 40000, collectibles: 0, finish: false, inputFormat: 3, exact: true });
        const r = await api("PUT", sub.body.data.upload, { token: nova.token, raw: solo, headers: octet });
        expect(r.body.data.run.status).toBe("REJECTED");
        expect(r.body.data.run.reasons.join(" ")).toContain("équipe");
        // A team replay sent as a solo attempt: rejected.
        const soloAttempt = crypto.randomUUID();
        const s2 = await api("POST", `${V}/races/runs`, {
            token: nova.token,
            body: { kind: "community", level_id: levelId, attempt_id: soloAttempt, time_ms: 40000, finished: false, bosses: [0], waves: [0] },
        });
        expect(s2.status).toBe(201);
        const team = await teamReplay(soloAttempt, 40000, { upTo: 0.3, boss: 0, waves: 0 }, [{ upTo: 0.3 }]);
        const r2 = await api("PUT", s2.body.data.upload, { token: nova.token, raw: team, headers: octet });
        expect(r2.body.data.run.status).toBe("REJECTED");
    });
});
