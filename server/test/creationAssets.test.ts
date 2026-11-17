// 2.61 Studio de Créations: the image of a creation. Sent by an administrator only (the role of
// the session), checked (a PNG: its signature, its IHDR, its chunks; its size in bytes and in
// pixels for its kind; its emitter), kept in R2 under a key of its own, every sending and every
// refusal in the Journal. Read by a player only when the creation is theirs, worn by someone or
// given by a challenge that has begun — never a creation nobody was given — and cached by its hash.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { addDays, periodBounds, periodOf, utcDate } from "../src/challenges";
import { IMAGE_MAX_BYTES, layoutProblem, readPng } from "../src/creationAssets";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

const V = "/api/v1";
const GIVRE = { suit: "#c8e1ff", accent: "#78c8ff", scarf: "#ebf5ff" };
const EMITTER = "rate=40&life_ms=650&size=150&speed=30&spread=25&gravity=-10&spin=90&fade=1&sparkles=1";

beforeEach(seedAll);

// ------------------------------------------------------------------ PNG files made here

let table: Uint32Array | null = null;
function crc32(d: Uint8Array): number {
    if (!table) {
        table = new Uint32Array(256);
        for (let i = 0; i < 256; i++) {
            let c = i;
            for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
            table[i] = c >>> 0;
        }
    }
    let c = 0xffffffff;
    for (const b of d) c = table[(c ^ b) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

async function deflate(data: Uint8Array): Promise<Uint8Array> {
    const stream = new Blob([data]).stream().pipeThrough(new CompressionStream("deflate"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
}

function chunk(type: string, data: Uint8Array): Uint8Array {
    const out = new Uint8Array(12 + data.length);
    const v = new DataView(out.buffer);
    v.setUint32(0, data.length);
    for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
    out.set(data, 8);
    v.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
    return out;
}

/** An RGBA PNG of w × h pixels (`seed` changes its colours, so its hash). */
async function png(w: number, h: number, seed = 1): Promise<Uint8Array> {
    const raw = new Uint8Array(h * (1 + w * 4));
    for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++) {
            const at = y * (1 + w * 4) + 1 + x * 4;
            raw.set([((x >> 4) * 16 + seed * 31) & 255, ((y >> 5) * 8 + seed) & 255, (seed * 40) & 255, ((x >> 3) + (y >> 3)) % 3 === 0 ? 0 : 255], at);
        }
    const ihdr = new Uint8Array(13);
    const v = new DataView(ihdr.buffer);
    v.setUint32(0, w);
    v.setUint32(4, h);
    ihdr.set([8, 6, 0, 0, 0], 8);
    const parts = [new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk("IHDR", ihdr), chunk("IDAT", await deflate(raw)), chunk("IEND", new Uint8Array(0))];
    const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
    let at = 0;
    for (const p of parts) {
        out.set(p, at);
        at += p.length;
    }
    return out;
}

async function sha256(d: Uint8Array): Promise<string> {
    return [...new Uint8Array(await crypto.subtle.digest("SHA-256", d))].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// ------------------------------------------------------------------ helpers

async function admin(name = "NovaRider"): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

async function creation(a: TestPlayer, kind: string, name: string): Promise<{ id: number; ref: string }> {
    const body =
        kind === "suit"
            ? { kind, name, colors: GIVRE }
            : kind === "trail"
              ? { kind, name, colors: { start: "#ffd27a", end: "#a66bff" }, style: "comet", width: 100, life_ms: 450, particles: true }
              : kind === "frame"
                ? { kind, name, colors: { border: "#ffd27a", glow: "#a66bff" }, style: "metal" }
                : { kind, name, colors: { main: "#ffd27a", second: "#a66bff" }, style: "fireworks" };
    const r = await api("POST", `${V}/admin/creations`, { token: a.token, body });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    return r.body.data.creation;
}

const put = (who: TestPlayer | null, id: number, bytes: Uint8Array, query = "", type = "image/png") =>
    api("PUT", `${V}/admin/creations/${id}/image${query ? "?" + query : ""}`, { token: who?.token, raw: bytes, headers: { "content-type": type } });

async function journal(a: TestPlayer, kind: string): Promise<any[]> {
    const r = await api("GET", `${V}/admin/log?kind=${kind}`, { token: a.token });
    expect(r.status).toBe(200);
    return r.body.data.entries;
}

/** A challenge that began yesterday (or begins tomorrow) giving `ref`. */
async function giveInChallenge(ref: string, kind: string, begun = true): Promise<void> {
    const date = begun ? addDays(utcDate(), -1) : addDays(utcDate(), 3);
    const p = periodBounds("daily", periodOf("daily", date))!;
    const id = `daily-${p.period}`;
    await env.DB.prepare(
        `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title,
           hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, created_at, updated_at)
         VALUES (?1, 'daily', ?2, ?3, ?4, 'admin', 'campaign', 'glacior_02', 1, ?5, 'Glacior', 3, 4, '[]', 1, 0, ?6, 90000, 70000, 1, 'speed', ?7, ?7)`,
    )
        .bind(id, p.period, p.start, p.end, "2".repeat(64), "a".repeat(64), new Date().toISOString())
        .run();
    await env.DB.prepare("INSERT INTO challenge_reward_rules (challenge_id, position, reward_kind, reward_ref, condition, condition_value) VALUES (?1, 0, ?2, ?3, 'tier', 3)")
        .bind(id, kind, ref)
        .run();
}

// ------------------------------------------------------------------ tests

describe("creation images: the checks of the file", () => {
    it("reads a PNG's size and refuses anything else, as the game does", async () => {
        const ok = await png(48, 16);
        expect(readPng(ok)).toEqual({ width: 48, height: 16 });
        expect(readPng(new TextEncoder().encode("GIF89a, pas une image PNG"))).toEqual({ problem: "Pas un fichier PNG." });
        const broken = ok.slice();
        broken[18] = broken[18]! ^ 1;  // the width: the CRC of IHDR no longer matches
        expect(readPng(broken)).toEqual({ problem: "Fichier PNG abîmé (en-tête IHDR)." });
        expect(readPng(ok.slice(0, ok.length - 20))).toEqual({ problem: "Fichier PNG tronqué." });
        expect(layoutProblem("suit", 320, 864, 32, 32, 1, 1)).toBe("");
        expect(layoutProblem("trail", 64, 16, 16, 16, 4, 12)).toBe("");
        expect(layoutProblem("trail", 64, 16, 16, 16, 3, 12)).not.toBe("");
        expect(layoutProblem("frame", 64, 32, 64, 32, 1, 8)).not.toBe("");
    });
});

describe("creation images: sending", () => {
    it("an administrator sends a suit's sheet: checked, hashed, kept in R2, written to the Journal", async () => {
        const a = await admin();
        const c = await creation(a, "suit", "Aube de Sylvanox");
        const sheet = await png(320, 864, 3);
        const r = await put(a, c.id, sheet);
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        const hash = await sha256(sheet);
        expect(r.body.data).toMatchObject({ changed: true, image: { ref: c.ref, kind: "suit", hash, width: 320, height: 864, bytes: sheet.length } });
        const row = await env.DB.prepare("SELECT * FROM creation_images WHERE creation_id = ?1").bind(c.id).first<any>();
        expect(row.r2_key).toBe(`creation-images/${c.id}/${hash}.png`);
        const obj = await env.STORAGE.get(row.r2_key);
        expect(new Uint8Array(await obj!.arrayBuffer())).toEqual(sheet);
        // The same again: nothing changes, nothing written.
        const same = await put(a, c.id, sheet);
        expect(same.body.data.changed).toBe(false);
        const done = (await journal(a, "creation")).filter((e) => e.action === "admin_creation_image");
        expect(done).toHaveLength(1);
        expect(done[0].creation).toMatchObject({ id: c.id, ref: c.ref, name: "Aube de Sylvanox" });
        expect(done[0].image).toMatchObject({ hash, width: 320, height: 864 });
        // The administration lists it.
        const list = await api("GET", `${V}/admin/creations/images`, { token: a.token });
        expect(list.body.data.images.map((i: any) => i.ref)).toEqual([c.ref]);
    });

    it("a trail's particle and its emitter; a frame's ring; a new image replaces the previous one", async () => {
        const a = await admin();
        const t = await creation(a, "trail", "Comète d'aurore");
        const strip = await png(64, 16, 4);
        const r = await put(a, t.id, strip, `frame_w=16&frame_h=16&frames=4&fps=12&${EMITTER}`, "application/octet-stream");
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        expect(r.body.data.image).toMatchObject({ frame_w: 16, frame_h: 16, frames: 4, fps: 12 });
        expect(r.body.data.image.emitter).toEqual({ rate: 40, life_ms: 650, size: 150, speed: 30, spread: 25, gravity: -10, spin: 90, fade: 1, sparkles: true });
        const f = await creation(a, "frame", "Lauriers");
        expect((await put(a, f.id, await png(128, 64, 5), "frame_w=64&frame_h=64&frames=2&fps=8")).status).toBe(200);
        // Replaced: the old file goes from R2.
        const oldKey = `creation-images/${t.id}/${await sha256(strip)}.png`;
        const next = await png(48, 16, 6);
        expect((await put(a, t.id, next, `frame_w=16&frame_h=16&frames=3&fps=10&${EMITTER}`)).status).toBe(200);
        expect(await env.STORAGE.get(oldKey)).toBeNull();
        expect((await env.DB.prepare("SELECT hash FROM creation_images WHERE creation_id = ?1").bind(t.id).first<any>()).hash).toBe(await sha256(next));
    });

    it("refuses: no session, not an administrator, not a PNG, too large, the wrong sizes, a bad setting, an archived creation", async () => {
        const a = await admin();
        const p = await registerPlayer("Kestrel");
        const suit = await creation(a, "suit", "Givre");
        const trail = await creation(a, "trail", "Braise");
        const sheet = await png(320, 864);
        expect((await put(null, suit.id, sheet)).status).toBe(401);
        const notAdmin = await put(p, suit.id, sheet);
        expect(notAdmin.status).toBe(404);
        expect(notAdmin.body.error.code).toBe("NOT_FOUND");
        const notPng = await put(a, suit.id, new TextEncoder().encode("ce n'est pas une image"));
        expect(notPng.status).toBe(400);
        expect(notPng.body.error.code).toBe("NOT_PNG");
        const huge = new Uint8Array(IMAGE_MAX_BYTES + 1);
        huge.set(sheet.subarray(0, 64));
        const tooLarge = await put(a, suit.id, huge);
        expect(tooLarge.status).toBe(413);
        expect(tooLarge.body.error.code).toBe("IMAGE_TOO_LARGE");
        const wrongSize = await put(a, suit.id, await png(320, 832));
        expect(wrongSize.status).toBe(400);
        expect(wrongSize.body.error.code).toBe("IMAGE_BAD_SIZE");
        expect((await put(a, trail.id, await png(64, 16), `frame_w=16&frame_h=16&frames=3&fps=12&${EMITTER}`)).body.error.code).toBe("IMAGE_BAD_SIZE");
        expect((await put(a, trail.id, await png(64, 16), `frame_w=16&frame_h=16&frames=4&fps=12&${EMITTER.replace("rate=40", "rate=500")}`)).body.error.code).toBe(
            "VALIDATION_ERROR",
        );
        expect((await put(a, trail.id, await png(64, 16), `frame_w=16&frame_h=16&frames=4&fps=12`)).body.error.code).toBe("VALIDATION_ERROR");
        expect((await put(a, suit.id, sheet, "frames=4")).body.error.code).toBe("VALIDATION_ERROR");
        expect((await put(a, suit.id, sheet, "", "text/plain")).status).toBe(415);
        expect((await put(a, 999999, sheet)).status).toBe(404);
        await api("POST", `${V}/admin/creations/${suit.id}/archive`, { token: a.token, body: { archived: true } });
        const archived = await put(a, suit.id, sheet);
        expect(archived.status).toBe(409);
        expect(archived.body.error.code).toBe("CREATION_ARCHIVED");
        // Nothing kept; every refusal of the administrator in the Journal, with its reason.
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM creation_images").first<any>()).n).toBe(0);
        const refused = (await journal(a, "refused")).filter((e) => e.creation?.action === "image");
        const reasons = refused.map((e) => e.reason);
        for (const code of ["NOT_PNG", "IMAGE_TOO_LARGE", "IMAGE_BAD_SIZE", "VALIDATION_ERROR", "UNSUPPORTED_MEDIA_TYPE", "CREATION_NOT_FOUND", "CREATION_ARCHIVED"])
            expect(reasons, code).toContain(code);
        expect(refused.filter((e) => e.reason === "IMAGE_BAD_SIZE").map((e) => e.image)).toContainEqual(expect.objectContaining({ width: 320, height: 832 }));
    });
});

describe("creation images: reading", () => {
    it("only a creation one owns, worn by a player or given by a challenge begun; never the reserve nobody was given", async () => {
        const a = await admin();
        const p = await registerPlayer("Kestrel");
        const other = await registerPlayer("Vega");
        const c = await creation(a, "trail", "Comète d'aurore");
        const strip = await png(64, 16, 9);
        await put(a, c.id, strip, `frame_w=16&frame_h=16&frames=4&fps=12&${EMITTER}`);
        const hash = await sha256(strip);
        const path = `${V}/creations/${c.id}/image/${hash}`;
        const listed = async (who: TestPlayer | null) => (await api("GET", `${V}/creations/images`, { token: who?.token })).body.data.images.map((i: any) => i.ref);
        // Given to nobody yet: neither listed nor read (the same answer as an unknown image).
        expect(await listed(null)).toEqual([]);
        expect(await listed(p)).toEqual([]);
        const refused = await api("GET", path, { token: p.token });
        expect(refused.status).toBe(404);
        expect(refused.body.error.code).toBe("CREATION_IMAGE_NOT_FOUND");
        expect((await api("GET", path)).status).toBe(404);
        // A challenge that has not begun yet: still nothing.
        await giveInChallenge(c.ref, "trail", false);
        expect(await listed(p)).toEqual([]);
        // The administrators read every image.
        expect((await api("GET", path, { token: a.token })).status).toBe(200);
        expect(await listed(a)).toEqual([c.ref]);
        // Won by a player: that player only.
        await env.DB.prepare("INSERT INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at) VALUES (?1, 'daily-2026-01-01', 'trail', ?2, ?3)")
            .bind(p.id, c.ref, new Date().toISOString())
            .run();
        expect(await listed(p)).toEqual([c.ref]);
        expect(await listed(other)).toEqual([]);
        const mine = await api("GET", path, { token: p.token });
        expect(mine.status).toBe(200);
        expect(mine.headers.get("content-type")).toBe("image/png");
        expect(new Uint8Array(mine.body)).toEqual(strip);
        expect((await api("GET", path, { token: other.token })).status).toBe(404);
        // Worn by that player: on the boards and profiles, so everyone may read it.
        await env.DB.prepare(
            "INSERT INTO player_cosmetics (player_id, suit, trail, frame, title, victory, updated_at) VALUES (?1, 'suit_standard', ?2, 'frame_simple', 'title_none', 'victory_classic', ?3)",
        )
            .bind(p.id, c.ref, new Date().toISOString())
            .run();
        expect(await listed(null)).toEqual([c.ref]);
        expect((await api("GET", path)).status).toBe(200);
        // Another creation, given by a challenge that began: everyone.
        const v = await creation(a, "victory", "Couronne solaire");
        const vs = await png(32, 16, 2);
        await put(a, v.id, vs, `frame_w=16&frame_h=16&frames=2&fps=12&${EMITTER}`);
        await giveInChallenge(v.ref, "victory", true);
        expect(await listed(other)).toEqual([c.ref, v.ref]);
        expect((await api("GET", `${V}/creations/${v.id}/image/${await sha256(vs)}`)).status).toBe(200);
        // A wrong hash, a malformed address: unknown.
        expect((await api("GET", `${V}/creations/${c.id}/image/${"0".repeat(64)}`)).status).toBe(404);
        expect((await api("GET", `${V}/creations/abc/image/${hash}`)).status).toBe(404);
    });

    it("is cached by its hash: ETag, If-None-Match, immutable; a new image has a new address", async () => {
        const a = await admin();
        const c = await creation(a, "frame", "Lauriers");
        const ring = await png(64, 64, 7);
        await put(a, c.id, ring, "frame_w=64&frame_h=64&frames=1&fps=8");
        await giveInChallenge(c.ref, "frame", true);
        const hash = await sha256(ring);
        const path = `${V}/creations/${c.id}/image/${hash}`;
        const first = await api("GET", path);
        expect(first.status).toBe(200);
        expect(first.headers.get("etag")).toBe(`"${hash}"`);
        expect(first.headers.get("cache-control")).toContain("immutable");
        expect(first.headers.get("cache-control")).toContain("max-age=31536000");
        const again = await api("GET", path, { headers: { "if-none-match": `"${hash}"` } });
        expect(again.status).toBe(304);
        expect((await api("GET", path, { headers: { "if-none-match": `"${"1".repeat(64)}"` } })).status).toBe(200);
        // Replaced: the old address is gone, the new one is listed.
        const ring2 = await png(64, 64, 8);
        await put(a, c.id, ring2, "frame_w=64&frame_h=64&frames=1&fps=8");
        expect((await api("GET", path)).status).toBe(404);
        const listed = (await api("GET", `${V}/creations/images`)).body.data.images;
        expect(listed[0].hash).toBe(await sha256(ring2));
        expect((await api("GET", `${V}/creations/${c.id}/image/${listed[0].hash}`)).status).toBe(200);
    });
});
