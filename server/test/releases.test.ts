import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { englishNotes, releasesOfMigrations } from "../scripts/release-notes.mjs";
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

    it("since: the notes of every release after the player's version, newest first (2.62)", async () => {
        await publish("2.30.0", 2030000, new Uint8Array([1]));
        await publish("2.32.1", 2032001, new Uint8Array([2]));
        await publish("2.60.2", 2060002, new Uint8Array([3]));
        await publish("2.61.1", 2061001, new Uint8Array([4]));
        const r = await api("GET", "/api/v1/client/latest?platform=windows&since=2.32.0");
        expect(r.status).toBe(200);
        expect(r.body.data.release.version).toBe("2.61.1");
        expect(r.body.data.changes.map((c: { version: string }) => c.version)).toEqual(["2.61.1", "2.60.2", "2.32.1"]);
        expect(r.body.data.changes[0].notes).toBe("notes");
        const none = await api("GET", "/api/v1/client/latest?platform=windows");
        expect(none.body.data.changes).toBeUndefined();
        expect((await api("GET", "/api/v1/client/latest?platform=windows&since=2.x")).status).toBe(400);
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

    it("notes_en: the same notes in English, both given, the game shows those of its language (2.76.1)", async () => {
        await publish("2.75.3", 2075003, new Uint8Array([1]));
        await publish("2.75.4", 2075004, new Uint8Array([2]));
        await env.DB.prepare("UPDATE client_releases SET notes = ?1, notes_en = ?2 WHERE version = '2.75.4'")
            .bind("Version 2.75.4 :\n• Tout le jeu en anglais.", "Version 2.75.4:\n• The whole game in English.")
            .run();
        const r = await api("GET", "/api/v1/client/latest?platform=windows&since=2.75.2");
        expect(r.status).toBe(200);
        expect(r.body.data.release.notes).toBe("Version 2.75.4 :\n• Tout le jeu en anglais.");
        expect(r.body.data.release.notes_en).toBe("Version 2.75.4:\n• The whole game in English.");
        expect(r.body.data.changes[0].notes_en).toBe("Version 2.75.4:\n• The whole game in English.");
        // An installer registered without its English notes: "" (the game shows the French ones, translated as it can).
        expect(r.body.data.changes[1].version).toBe("2.75.3");
        expect(r.body.data.changes[1].notes_en).toBe("");
    });
});

describe("English notes of the installers (scripts/release-notes.mjs, 2.76.1)", () => {
    const strings = { "Tout le jeu en anglais.": "The whole game in English.", "Raids : le titan tombe.": "Raids: the titan falls." };

    it("each line by the dictionary, the version line in English typography", () => {
        expect(englishNotes("Version 2.75.4 :\n• Tout le jeu en anglais.\n• Raids : le titan tombe.\n\nVersion 2.75.3 :\n• Tout le jeu en anglais.", strings)).toBe(
            "Version 2.75.4:\n• The whole game in English.\n• Raids: the titan falls.\n\nVersion 2.75.3:\n• The whole game in English.",
        );
        expect(englishNotes("Tout le jeu en anglais.", strings)).toBe("The whole game in English.");
        expect(englishNotes("", strings)).toBe("");
    });

    it("a line without translation: no English notes at all (never French among English)", () => {
        expect(englishNotes("Version 2.75.4 :\n• Tout le jeu en anglais.\n• Une ligne inconnue.", strings)).toBeNull();
        expect(englishNotes("toString", strings)).toBeNull();
    });

    it("the installers of the migrations: version and notes read from their INSERT", () => {
        const sql =
            "-- Installer 2.4.0\nINSERT INTO client_releases (version, version_key, platform, file_key, file_name, size, sha256, notes, published_at) VALUES ('2.4.0', 2004000, 'windows', 'releases/a.exe', 'a.exe', 31397133, 'ab', 'L''aspect des boss.', '2026-11-14T09:34:01.493Z') ON CONFLICT (version) DO NOTHING;";
        expect(releasesOfMigrations({ "0008_release_2_4_0.sql": sql, "0009_other.sql": sql })).toEqual([{ version: "2.4.0", notes: "L'aspect des boss." }]);
    });
});
