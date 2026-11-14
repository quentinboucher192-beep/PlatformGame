import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, seedAll } from "./helpers";

beforeEach(seedAll);

async function publish(version: string, key: number, bytes: Uint8Array) {
    const sha = [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))].map((b) => b.toString(16).padStart(2, "0")).join("");
    const fileKey = `releases/CosmicPlatformer-Setup-${version}.exe`;
    await env.STORAGE.put(fileKey, bytes);
    await env.DB.prepare(
        "INSERT INTO client_releases (version, version_key, platform, file_key, file_name, size, sha256, notes, published_at) VALUES (?1, ?2, 'windows', ?3, ?4, ?5, ?6, 'notes', '2026-11-14T00:00:00Z')",
    )
        .bind(version, key, fileKey, `CosmicPlatformer-Setup-${version}.exe`, bytes.length, sha)
        .run();
    return sha;
}

describe("installers", () => {
    it("no release: release is null", async () => {
        const r = await api("GET", "/api/v1/client/latest");
        expect(r.status).toBe(200);
        expect(r.body.data.release).toBeNull();
    });

    it("latest is the highest version, the download is the file with its SHA-256", async () => {
        await publish("2.3.0", 2003000, new Uint8Array([1, 2, 3]));
        const data = new Uint8Array([77, 90, 9, 8, 7, 6]);
        const sha = await publish("2.10.0", 2010000, data);
        await publish("2.4.0", 2004000, new Uint8Array([4, 5]));
        const r = await api("GET", "/api/v1/client/latest?platform=windows");
        expect(r.status).toBe(200);
        expect(r.body.data.release.version).toBe("2.10.0");
        expect(r.body.data.release.sha256).toBe(sha);
        expect(r.body.data.release.size).toBe(6);
        const d = await api("GET", r.body.data.release.download);
        expect(d.status).toBe(200);
        expect(d.headers.get("x-content-sha256")).toBe(sha);
        expect(d.headers.get("content-disposition")).toContain("CosmicPlatformer-Setup-2.10.0.exe");
        expect([...new Uint8Array(d.body as ArrayBuffer)]).toEqual([...data]);
    });

    it("unknown or malformed versions are not found, downloads are rate limited", async () => {
        await publish("2.4.0", 2004000, new Uint8Array([4, 5]));
        expect((await api("GET", "/api/v1/client/download/9.9.9")).status).toBe(404);
        expect((await api("GET", "/api/v1/client/download/..%2Fsecret")).status).toBe(404);
        expect((await api("GET", "/api/v1/client/latest?platform=Win$")).status).toBe(400);
        const ip = "203.0.113.77";
        for (let i = 0; i < 6; ++i) expect((await api("GET", "/api/v1/client/download/2.4.0", { ip })).status).toBe(200);
        expect((await api("GET", "/api/v1/client/download/2.4.0", { ip })).status).toBe(429);
    });
});
