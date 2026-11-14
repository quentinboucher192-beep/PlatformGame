// Publishing community levels in tests (shared by community.test.ts and workshop.test.ts).
import { expect } from "vitest";
import { EXIT, SPAWN, api, buildReplay, type TestPlayer } from "./helpers";

export const LH = "e".repeat(64);
export const LH2 = "f".repeat(64);

async function deflateRaw(data: Uint8Array): Promise<Uint8Array> {
    const stream = new Blob([data]).stream().pipeThrough(new CompressionStream("deflate-raw"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
}

/** Minimal ZIP writer (stored or deflated entries, no CRC check on the server side). */
export async function zip(files: Record<string, string | Uint8Array>, deflated = true): Promise<Uint8Array> {
    const enc = new TextEncoder();
    const parts: Uint8Array[] = [];
    const central: Uint8Array[] = [];
    let offset = 0;
    for (const [name, content] of Object.entries(files)) {
        const raw = typeof content === "string" ? enc.encode(content) : content;
        const data = deflated ? await deflateRaw(raw) : raw;
        const n = enc.encode(name);
        const local = new Uint8Array(30 + n.length);
        const lv = new DataView(local.buffer);
        lv.setUint32(0, 0x04034b50, true);
        lv.setUint16(8, deflated ? 8 : 0, true);
        lv.setUint32(18, data.length, true);
        lv.setUint32(22, raw.length, true);
        lv.setUint16(26, n.length, true);
        local.set(n, 30);
        const cd = new Uint8Array(46 + n.length);
        const cv = new DataView(cd.buffer);
        cv.setUint32(0, 0x02014b50, true);
        cv.setUint16(10, deflated ? 8 : 0, true);
        cv.setUint32(20, data.length, true);
        cv.setUint32(24, raw.length, true);
        cv.setUint16(28, n.length, true);
        cv.setUint32(42, offset, true);
        cd.set(n, 46);
        parts.push(local, data);
        central.push(cd);
        offset += local.length + data.length;
    }
    const cdSize = central.reduce((a, c) => a + c.length, 0);
    const end = new Uint8Array(22);
    const ev = new DataView(end.buffer);
    ev.setUint32(0, 0x06054b50, true);
    ev.setUint16(8, central.length, true);
    ev.setUint16(10, central.length, true);
    ev.setUint32(12, cdSize, true);
    ev.setUint32(16, offset, true);
    const out = new Uint8Array(offset + cdSize + 22);
    let at = 0;
    for (const p of [...parts, ...central, end]) {
        out.set(p, at);
        at += p.length;
    }
    return out;
}

export function levelJson(id = "mon_niveau") {
    return JSON.stringify({
        format: "cosmic.level",
        id,
        name: "Mon niveau",
        bounds: [0, 0, 6000, 1200],
        timer: { mode: "limited", limit: 600 },
        objects: [
            { id: "o1", type: "player_start", rect: [SPAWN.x - 5, SPAWN.y - 22, 10, 22] },
            { id: "o2", type: "exit", rect: [EXIT.x, EXIT.y, EXIT.w, EXIT.h] },
            { id: "o3", type: "solid", rect: [0, 500, 6000, 100] },
        ],
    });
}

export const PNG = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3, 4]);

export async function pkg(extra: Record<string, string | Uint8Array> = {}) {
    return zip({
        "COSMIC_LEVEL/manifest.json": JSON.stringify({ format: "cosmic.package", levelId: "mon_niveau" }),
        "COSMIC_LEVEL/level.json": levelJson(),
        "COSMIC_LEVEL/metadata/thumbnail.png": PNG,
        ...extra,
    });
}

export const octet = { "content-type": "application/octet-stream" };

export async function publish(author: TestPlayer, opts: { communityId?: string; hash?: string; title?: string; difficulty?: number } = {}) {
    const start = await api("POST", "/api/v1/community/levels", {
        token: author.token,
        body: {
            ...(opts.communityId ? { community_id: opts.communityId } : {}),
            title: opts.title ?? "La Tour Ardente",
            description: "Un niveau de test",
            planet: "pyralis",
            difficulty: opts.difficulty ?? 3.5,
            level_hash: opts.hash ?? LH,
            min_game_version: "2.2.0",
        },
    });
    expect(start.status).toBe(201);
    const { id, version, upload } = start.body.data;
    const p = await api("PUT", upload.package, { token: author.token, raw: await pkg(), headers: octet });
    expect(p.status).toBe(200);
    expect(p.body.data.published).toBe(false);
    const replay = await buildReplay({ levelId: "mon_niveau", levelHash: opts.hash ?? LH, attemptId: crypto.randomUUID(), durationMs: 62000, exact: true });
    const r = await api("PUT", upload.replay, { token: author.token, raw: replay, headers: octet });
    expect(r.status).toBe(200);
    expect(r.body.data.published).toBe(true);
    return { id: id as string, version: version as number };
}

