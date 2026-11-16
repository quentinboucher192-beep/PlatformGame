// Backups of all the data (docs/online/BACKUP.md): server-side copies, the encrypted .cpbk
// download, its import in parts, the check that changes nothing, the apply step by step under
// maintenance, the retention, the Journal. Only administrators see these routes.

import { createExecutionContext, createScheduledController, env, waitOnExecutionContext } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import * as fmt from "../src/backupFormat";
import { applyRetention, type StoredManifest } from "../src/backups";
import worker from "../src/index";
import { api, registerPlayer, seedAll, verifiedRun, type TestPlayer, deviceOf } from "./helpers";

const V = "/api/v1";
const PASS = "une phrase secrète très longue — 42";
const CONFIRM = "REMPLACER TOUTES LES DONNÉES";
const T = 120_000;
const enc = new TextEncoder();

async function clearBackups(): Promise<void> {
    await env.DB.batch([
        env.DB.prepare("DELETE FROM backup_import_parts"),
        env.DB.prepare("DELETE FROM backup_imports"),
        env.DB.prepare("DELETE FROM backups"),
        env.DB.prepare("DELETE FROM server_state"),
    ]);
    const scratch = await env.DB.prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND name LIKE 'backup\\_%\\_\\_%' ESCAPE '\\'").all<{ name: string }>();
    for (const t of scratch.results) await env.DB.prepare(`DROP TABLE "${t.name}"`).run();
    for (;;) {
        const l = await env.STORAGE.list({ limit: 1000 });
        if (l.objects.length === 0) break;
        await env.STORAGE.delete(l.objects.map((o) => o.key));
    }
}

beforeEach(async () => {
    await seedAll();
    await clearBackups();
});

async function makeAdmin(name = "NovaRider"): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

async function sha256Hex(data: Uint8Array): Promise<string> {
    return [...new Uint8Array(await crypto.subtle.digest("SHA-256", data))].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function randomData(n: number): Uint8Array {
    const out = new Uint8Array(n);
    for (let i = 0; i < n; i += 65536) crypto.getRandomValues(out.subarray(i, Math.min(n, i + 65536)));
    return out;
}

async function createCopy(token: string): Promise<any> {
    const res = await api("POST", `${V}/admin/backups`, { token });
    expect(res.status).toBe(201);
    return res.body.data.backup;
}

async function download(token: string, id: string, passphrase = PASS): Promise<Uint8Array> {
    const res = await api("POST", `${V}/admin/backups/${id}/download`, { token, body: { passphrase } });
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("application/octet-stream");
    expect(res.headers.get("content-disposition")).toMatch(/^attachment; filename="cosmic-backup-\d{4}-\d{2}-\d{2}\.cpbk"$/);
    return new Uint8Array(res.body as ArrayBuffer);
}

async function upload(token: string, file: Uint8Array, sha256?: string): Promise<string> {
    const created = await api("POST", `${V}/admin/backups/imports`, { token, body: { size: file.byteLength, sha256: sha256 ?? (await sha256Hex(file)) } });
    expect(created.status).toBe(201);
    const { import_id, part_size, parts } = created.body.data;
    expect(parts).toBe(Math.ceil(file.byteLength / part_size));
    for (let n = 0; n < parts; ++n) {
        const res = await api("PUT", `${V}/admin/backups/imports/${import_id}/parts/${n}`, {
            token,
            raw: file.slice(n * part_size, (n + 1) * part_size),
            headers: { "content-type": "application/octet-stream" },
        });
        expect(res.status).toBe(200);
        expect(res.body.data.received).toBe(Math.min(file.byteLength, (n + 1) * part_size));
    }
    return import_id;
}

function check(token: string, id: string, passphrase = PASS) {
    return api("POST", `${V}/admin/backups/imports/${id}/check`, { token, body: { passphrase } });
}

function apply(token: string, id: string, confirm = CONFIRM, passphrase = PASS) {
    return api("POST", `${V}/admin/backups/imports/${id}/apply`, { token, body: { passphrase, confirm } });
}

async function applyUntilDone(token: string, id: string, between?: (step: any, i: number) => Promise<void>): Promise<any> {
    for (let i = 0; i < 40; ++i) {
        const res = await apply(token, id);
        expect(res.status, JSON.stringify(res.body)).toBe(200);
        if (res.body.data.done) return res.body.data;
        expect(res.body.data.progress).toBeGreaterThanOrEqual(0);
        expect(res.body.data.progress).toBeLessThan(1);
        if (between) await between(res.body.data, i);
    }
    throw new Error("apply never finished");
}

const NOT_COMPARED = new Set(["player_sessions", "rate_limits", "server_state", "backups", "backup_imports", "backup_import_parts", "d1_migrations", "sqlite_sequence"]);

/** Every application table, row by row (the Journal without the backup entries, written after the copy). */
async function dbSnapshot(): Promise<Record<string, unknown[]>> {
    const tables = await env.DB.prepare("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name").all<{ name: string }>();
    const out: Record<string, unknown[]> = {};
    for (const { name } of tables.results) {
        if (NOT_COMPARED.has(name) || name.startsWith("_cf_") || name.startsWith("backup_")) continue;
        const sql = name === "audit_log" ? "SELECT * FROM audit_log WHERE action NOT LIKE 'backup\\_%' ESCAPE '\\' ORDER BY id" : `SELECT * FROM "${name}" ORDER BY rowid`;
        out[name] = (await env.DB.prepare(sql).all()).results;
    }
    return out;
}

async function r2Snapshot(): Promise<Record<string, string>> {
    const out: Record<string, string> = {};
    let cursor: string | undefined;
    do {
        const l = await env.STORAGE.list({ cursor, include: ["httpMetadata", "customMetadata"] });
        for (const o of l.objects) {
            if (/^(backups|imports|releases)\//.test(o.key)) continue;
            const body = await env.STORAGE.get(o.key);
            const sha = await sha256Hex(new Uint8Array(await body!.arrayBuffer()));
            out[o.key] = `${sha} ${o.httpMetadata?.contentType ?? "-"} ${JSON.stringify(o.customMetadata ?? {})}`;
        }
        cursor = l.truncated ? l.cursor : undefined;
    } while (cursor);
    return out;
}

async function befriend(a: TestPlayer, b: TestPlayer): Promise<string> {
    expect((await api("POST", `${V}/friends/request`, { token: a.token, body: { player_id: b.id } })).status).toBe(201);
    expect((await api("POST", `${V}/friends/accept`, { token: b.token, body: { player_id: a.id } })).status).toBe(200);
    const conv = await api("POST", `${V}/social/direct`, { token: a.token, body: { player_id: b.id } });
    expect(conv.status).toBe(200);
    return conv.body.data.conversation.id;
}

/** Players, records with replays, a message, a custom avatar and a large file (several chunks). */
async function seedWorld() {
    const admin = await makeAdmin();
    const kes = await registerPlayer("Kestrel", { country_code: "FR", location_visibility: "country" });
    const orion = await registerPlayer("Orion");
    await verifiedRun(kes, 61000);
    await verifiedRun(orion, 64000);
    const conv = await befriend(kes, orion);
    expect((await api("POST", `${V}/social/conversations/${conv}/messages`, { token: kes.token, body: { body: "Course sur Sylvanox ?" } })).status).toBe(201);
    await env.STORAGE.put(`avatars/${kes.id}.png`, enc.encode("avatar d'origine"), { httpMetadata: { contentType: "image/png" } });
    await env.DB.prepare("UPDATE players SET avatar = 'custom', avatar_custom_key = ?1 WHERE id = ?2").bind(`avatars/${kes.id}.png`, kes.id).run();
    await env.STORAGE.put("community/niveau-test/v1.cplanet", randomData(2_600_000), {
        httpMetadata: { contentType: "application/octet-stream" },
        customMetadata: { level_id: "niveau-test", version: "1" },
    });
    // Never part of a backup.
    await env.STORAGE.put("releases/CosmicPlatformer-Setup-9.9.9.exe", enc.encode("installateur"));
    return { admin, kes, orion, conv };
}


/** The schema of a copy: the last migration applied (the newest file of migrations/). */
function lastMigration(): string {
    return [...env.TEST_MIGRATIONS].map((m) => m.name).sort().at(-1)!;
}

describe("backups: access", () => {
    it("every route is a 404 for a player and needs a session", async () => {
        const p = await registerPlayer("Kestrel");
        const id = "00000000-0000-4000-8000-000000000000";
        const routes: [string, string, unknown][] = [
            ["GET", `${V}/admin/backups`, undefined],
            ["POST", `${V}/admin/backups`, undefined],
            ["POST", `${V}/admin/backups/${id}/download`, { passphrase: PASS }],
            ["POST", `${V}/admin/backups/imports`, { size: 1000, sha256: "a".repeat(64) }],
            ["GET", `${V}/admin/backups/imports/${id}`, undefined],
            ["DELETE", `${V}/admin/backups/imports/${id}`, undefined],
            ["PUT", `${V}/admin/backups/imports/${id}/parts/0`, undefined],
            ["POST", `${V}/admin/backups/imports/${id}/check`, { passphrase: PASS }],
            ["POST", `${V}/admin/backups/imports/${id}/apply`, { passphrase: PASS, confirm: CONFIRM }],
        ];
        for (const [m, path, body] of routes) {
            const anon = await api(m, path, { body });
            expect(anon.status, `${m} ${path}`).toBe(401);
            const res = await api(m, path, { token: p.token, body });
            expect(res.status, `${m} ${path}`).toBe(404);
            expect(res.body.error.code).toBe("NOT_FOUND");
        }
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM backups").first<{ n: number }>())?.n).toBe(0);
    });
});

describe("backups: server-side copies", () => {
    it("creates a copy, lists it, and leaves out sessions, rate limits and installers", async () => {
        const { admin, kes } = await seedWorld();
        const b = await createCopy(admin.token);
        expect(b).toMatchObject({ kind: "manual", status: "ready", by: { id: admin.id, username: "NovaRider" }, schema: lastMigration() });
        expect(b.tables).toBeGreaterThan(40);
        expect(b.rows).toBeGreaterThan(10);
        expect(b.files).toBe(4); // two replays, the avatar, the level file
        expect(b.bytes).toBeGreaterThan(2_600_000);
        const list = await api("GET", `${V}/admin/backups`, { token: admin.token });
        expect(list.status).toBe(200);
        expect(list.body.data.backups).toHaveLength(1);
        expect(list.body.data.backups[0]).toEqual(b);
        expect(list.body.data.retention).toEqual({ auto: 14, manual: 10, manual_days: 90, pre_import_days: 30 });
        expect(list.body.data.maintenance).toBeNull();

        const manifest = (await (await env.STORAGE.get(`backups/${b.id}/manifest.json`))!.json()) as StoredManifest;
        const names = manifest.tables.map((t) => t.name);
        expect(names).toContain("players");
        expect(names).toContain("player_credentials");
        expect(names).toContain("audit_log");
        for (const t of ["player_sessions", "rate_limits", "backups", "backup_imports", "server_state", "d1_migrations"]) expect(names).not.toContain(t);
        expect(manifest.excluded_tables.find((t) => t.name === "player_sessions")?.reason).toMatch(/se reconnecte/);
        const files = (await (await env.STORAGE.get(`backups/${b.id}/${manifest.files.lists[0]}`))!.text()).trim().split("\n").map((l) => JSON.parse(l));
        expect(files.map((f) => f.key).sort()).toEqual(
            ["community/niveau-test/v1.cplanet", `avatars/${kes.id}.png`, ...(await replayKeys())].sort(),
        );
        for (const f of files) {
            const obj = await env.STORAGE.get(f.key);
            expect(f.sha256).toBe(await sha256Hex(new Uint8Array(await obj!.arrayBuffer())));
        }
        // Credentials are password hashes only: the password itself is nowhere.
        const creds = manifest.tables.find((t) => t.name === "player_credentials")!;
        const text = await (await env.STORAGE.get(`backups/${b.id}/${creds.parts[0]}`))!.text();
        expect(text).not.toContain(admin.password);
        expect(creds.columns).toContain("password_hash");
    }, T);

    it("the daily cron makes an automatic copy, except during an import", async () => {
        await makeAdmin();
        const exec = createExecutionContext();
        await worker.scheduled!(createScheduledController({ scheduledTime: Date.now(), cron: "17 3 * * *" }), env as never, exec);
        await waitOnExecutionContext(exec);
        const rows = await env.DB.prepare("SELECT kind, status, created_by FROM backups").all();
        expect(rows.results).toEqual([{ kind: "auto", status: "ready", created_by: null }]);

        await env.DB.prepare("INSERT INTO server_state (key, value, updated_at) VALUES ('maintenance', ?1, ?2)")
            .bind(JSON.stringify({ import_id: "x", since: new Date().toISOString(), expires_at: new Date(Date.now() + 60_000).toISOString() }), new Date().toISOString())
            .run();
        const exec2 = createExecutionContext();
        await worker.scheduled!(createScheduledController({ scheduledTime: Date.now(), cron: "17 3 * * *" }), env as never, exec2);
        await waitOnExecutionContext(exec2);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM backups").first<{ n: number }>())?.n).toBe(1);
    }, T);

    it("retention keeps 14 automatic, 10 manual and 30 days of pre_import copies", async () => {
        const now = Date.UTC(2026, 10, 16);
        const day = 86400_000;
        const rows: [string, string, string, number][] = [];
        for (let i = 0; i < 17; ++i) rows.push([crypto.randomUUID(), "auto", "ready", now - i * day]);
        for (let i = 0; i < 12; ++i) rows.push([crypto.randomUUID(), "manual", "ready", now - i * day - 3600_000]);
        rows.push([crypto.randomUUID(), "pre_import", "ready", now - 2 * day], [crypto.randomUUID(), "pre_import", "ready", now - 31 * day]);
        rows.push([crypto.randomUUID(), "manual", "failed", now - 8 * day], [crypto.randomUUID(), "auto", "failed", now - day]);
        for (const [id, kind, status, at] of rows) {
            await env.DB.prepare("INSERT INTO backups (id, kind, status, created_at) VALUES (?1, ?2, ?3, ?4)").bind(id, kind, status, new Date(at).toISOString()).run();
            await env.STORAGE.put(`backups/${id}/manifest.json`, "{}");
        }
        const deleted = await applyRetention(env, now);
        expect(deleted).toHaveLength(3 + 2 + 1 + 1);
        const left = await env.DB.prepare("SELECT kind, status, COUNT(*) AS n FROM backups GROUP BY kind, status ORDER BY kind, status").all();
        expect(left.results).toEqual([
            { kind: "auto", status: "failed", n: 1 },
            { kind: "auto", status: "ready", n: 14 },
            { kind: "manual", status: "ready", n: 10 },
            { kind: "pre_import", status: "ready", n: 1 },
        ]);
        // The oldest automatic ones went, with their files.
        const oldest = rows.filter((r) => r[1] === "auto" && r[2] === "ready").slice(14);
        for (const [id] of oldest) expect(await env.STORAGE.head(`backups/${id}/manifest.json`)).toBeNull();
        expect((await env.STORAGE.list({ prefix: "backups/" })).objects).toHaveLength(26);
    });
});

async function replayKeys(): Promise<string[]> {
    const r = await env.DB.prepare("SELECT storage_key FROM replay_metadata ORDER BY storage_key").all<{ storage_key: string }>();
    return r.results.map((x) => x.storage_key);
}

describe("backups: download and import", () => {
    it("round-trips: the downloaded file restores the data exactly, under maintenance, sessions excluded", async () => {
        const { admin, kes, orion, conv } = await seedWorld();
        const before = await dbSnapshot();
        const filesBefore = await r2Snapshot();
        expect(Object.keys(filesBefore)).toHaveLength(4);
        const b = await createCopy(admin.token);
        const file = await download(admin.token, b.id);
        expect(new TextDecoder().decode(file.subarray(0, 4))).toBe("CPBK");
        expect(file.byteLength).toBeGreaterThan(2_600_000);
        // Encrypted: none of the content shows through.
        const raw = new TextDecoder("latin1").decode(file);
        for (const s of ["Kestrel", "Course sur Sylvanox", "password_hash", "avatar d'origine"]) expect(raw).not.toContain(s);

        // Everything changes after the copy.
        expect((await api("DELETE", `${V}/players/me`, { token: orion.token, body: { password: orion.password, confirm: "DELETE" } })).status).toBe(200);
        expect((await api("PATCH", `${V}/players/me`, { token: kes.token, body: { avatar: "avatar_07", country_code: "JP" } })).status).toBe(200);
        await registerPlayer("Intrus");
        await env.STORAGE.put(`avatars/${kes.id}.png`, enc.encode("avatar plus récent"), { httpMetadata: { contentType: "image/png" } });
        await env.STORAGE.delete("community/niveau-test/v1.cplanet");
        await env.STORAGE.put("community/autre/v1.cplanet", enc.encode("ajouté après la copie"));
        expect(await dbSnapshot()).not.toEqual(before);

        const id = await upload(admin.token, file);
        const st = await api("GET", `${V}/admin/backups/imports/${id}`, { token: admin.token });
        expect(st.body.data.import).toMatchObject({ status: "uploading", parts_received: st.body.data.import.parts, received: file.byteLength });
        expect(st.body.data.import.parts).toBeGreaterThan(2);

        const checked = await check(admin.token, id);
        expect(checked.status, JSON.stringify(checked.body)).toBe(200);
        expect(checked.body.data.compatible).toBe(true);
        expect(checked.body.data.problems).toEqual([]);
        expect(checked.body.data.backup).toMatchObject({ created_at: b.created_at, schema: lastMigration(), tables: b.tables, rows: b.rows, files: 4 });
        // The check changed nothing.
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM players WHERE username = 'Intrus'").first<{ n: number }>()).toEqual({ n: 1 });
        expect((await api("GET", `${V}/admin/backups`, { token: admin.token })).body.data.maintenance).toBeNull();

        let sawMaintenance = false;
        const result = await applyUntilDone(admin.token, id, async (step) => {
            expect(step.pre_import_backup_id).toMatch(/^[0-9a-f-]{36}$/);
            // Maintenance: a player's write answers 503, a read still works, the admin goes on.
            const write = await api("POST", `${V}/presence`, { token: kes.token, body: { state: "active" } });
            expect(write.status).toBe(503);
            expect(write.body.error).toMatchObject({ code: "MAINTENANCE", message: "Maintenance en cours : réessayez dans quelques minutes." });
            expect((await api("POST", `${V}/auth/register`, { body: { username: "Pendant", password: "correct horse battery", accept_tos: true, tos_version: "2026-11" } })).status).toBe(503);
            expect((await api("GET", `${V}/players/${kes.id}`)).status).toBe(200);
            const list = await api("GET", `${V}/admin/backups`, { token: admin.token });
            expect(list.body.data.maintenance).toMatchObject({ active: true, import_id: id });
            sawMaintenance = true;
        });
        expect(sawMaintenance).toBe(true);
        expect(result.summary).toMatchObject({ rows: b.rows, sessions_reset: true, session_kept: true, files_missing: 0 });
        expect(result.summary.files_written + result.summary.files_replaced + result.summary.files_unchanged).toBe(4);
        expect(result.summary.files_replaced).toBe(1); // the avatar changed after the copy

        // Exactly the data of the copy.
        expect(await dbSnapshot()).toEqual(before);
        const filesAfter = await r2Snapshot();
        for (const [key, sig] of Object.entries(filesBefore)) expect(filesAfter[key], key).toBe(sig);
        expect(await env.STORAGE.head("releases/CosmicPlatformer-Setup-9.9.9.exe")).not.toBeNull();

        // Sessions are not restored: players sign in again; the admin who ran the import keeps theirs.
        expect((await api("GET", `${V}/auth/me`, { token: kes.token })).status).toBe(401);
        expect((await api("GET", `${V}/admin/backups`, { token: admin.token })).status).toBe(200);
        const login = await api("POST", `${V}/auth/login`, { body: { username: "Orion", password: orion.password, device: deviceOf("Orion") } });
        expect(login.status).toBe(200);
        // Maintenance is over: writes work again.
        const write = await api("POST", `${V}/presence`, { token: login.body.data.session.access_token, body: { state: "active" } });
        expect(write.status).toBe(200);
        const msgs = await api("GET", `${V}/social/conversations/${conv}/messages`, { token: login.body.data.session.access_token });
        expect(msgs.body.data.messages.map((m: { body: string }) => m.body)).toContain("Course sur Sylvanox ?");

        // The copy made just before, and the finished import.
        const list = await api("GET", `${V}/admin/backups`, { token: admin.token });
        const pre = list.body.data.backups.find((x: { kind: string }) => x.kind === "pre_import");
        expect(pre).toMatchObject({ id: result.summary.pre_import_backup_id, status: "ready", by: { id: admin.id } });
        expect(list.body.data.maintenance).toBeNull();
        expect(list.body.data.imports).toHaveLength(0);
        expect((await api("GET", `${V}/admin/backups/imports/${id}`, { token: admin.token })).body.data.import).toMatchObject({ status: "done", step: "done", progress: 1 });
        expect((await env.STORAGE.list({ prefix: `imports/${id}/` })).objects).toHaveLength(0);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM sqlite_master WHERE name LIKE 'backup\\_stage%' ESCAPE '\\'").first<{ n: number }>())?.n).toBe(0);
        // The newer avatar was kept in the pre_import copy, and its download gives it back.
        expect(await (await env.STORAGE.get(`backups/${pre.id}/objects/avatars/${kes.id}.png`))!.text()).toBe("avatar plus récent");
        const preEntries = await entriesOf(await download(admin.token, pre.id));
        const avatar = preEntries.find((e) => e.kind === fmt.ENTRY_FILE && e.name === `avatars/${kes.id}.png`)!;
        expect(new TextDecoder().decode(avatar.data)).toBe("avatar plus récent");
        expect(JSON.parse(avatar.meta!)).toEqual({ content_type: "image/png", custom_metadata: {} });
        expect(preEntries.map((e) => e.kind)).toEqual([...preEntries.map((e) => e.kind)].sort());
        const players = preEntries.find((e) => e.name.startsWith("d1/players/"))!;
        expect(new TextDecoder().decode(players.data)).toContain("Intrus");

        // The Journal: every step, never the passphrase.
        const log = await api("GET", `${V}/admin/log?kind=backup&limit=100`, { token: admin.token });
        const actions = log.body.data.entries.map((e: { action: string }) => e.action);
        for (const a of ["backup_created", "backup_downloaded", "backup_import_created", "backup_import_checked", "backup_import_started", "backup_import_finished"])
            expect(actions).toContain(a);
        const downloads = log.body.data.entries.filter((e: { action: string }) => e.action === "backup_downloaded");
        expect(downloads.map((e: { backup_id: string }) => e.backup_id)).toEqual([pre.id, b.id]);
        expect(downloads[1]).toMatchObject({ backup_kind: "manual", actor: { username: "NovaRider" } });
        const all = await env.DB.prepare("SELECT * FROM audit_log").all();
        expect(JSON.stringify(all.results)).not.toContain(PASS);
        expect(JSON.stringify(all.results)).not.toContain("phrase secrète");
    }, T);

    it("refuses a wrong passphrase, a wrong SHA-256, a tampered, reordered or truncated file", async () => {
        const { admin } = await seedWorld();
        const b = await createCopy(admin.token);
        const file = await download(admin.token, b.id);
        const rs = fmt.recordSize(fmt.CHUNK_SIZE);
        const layout = fmt.recordLayout(file.byteLength, fmt.CHUNK_SIZE);
        expect(layout.count).toBeGreaterThanOrEqual(3);
        const abandon = async (id: string) => expect((await api("DELETE", `${V}/admin/backups/imports/${id}`, { token: admin.token })).status).toBe(200);

        // Wrong passphrase (the first check reads everything to tell it from a bad upload, the next ones answer at once).
        let id = await upload(admin.token, file);
        for (let i = 0; i < 2; ++i) {
            const wrong = await check(admin.token, id, "une autre phrase secrète");
            expect(wrong.status).toBe(400);
            expect(wrong.body.error).toMatchObject({ code: "WRONG_PASSPHRASE", message: "Phrase secrète incorrecte : impossible de déchiffrer ce fichier." });
        }
        expect((await check(admin.token, id, "trop courte")).body.error.code).toBe("INVALID_PASSPHRASE");
        await abandon(id);

        // The announced SHA-256 does not match what was received.
        id = await upload(admin.token, file, "0".repeat(64));
        const mismatch = await check(admin.token, id);
        expect(mismatch.status).toBe(400);
        expect(mismatch.body.error.code).toBe("IMPORT_SHA256_MISMATCH");
        await abandon(id);

        const bad = async (data: Uint8Array, chunk: number | null) => {
            const bid = await upload(admin.token, data);
            const res = await check(admin.token, bid);
            expect(res.status).toBe(400);
            expect(res.body.error.code).toBe("BACKUP_CORRUPTED");
            if (chunk !== null) expect(res.body.error.message).toContain(`bloc ${chunk}`);
            await abandon(bid);
        };
        // One byte changed in the second chunk.
        const tampered = file.slice();
        const at = fmt.HEADER_SIZE + rs + 100;
        tampered[at] = tampered[at]! ^ 0x01;
        await bad(tampered, 1);
        // Two chunks swapped.
        const swapped = file.slice();
        swapped.set(file.subarray(fmt.HEADER_SIZE + rs, fmt.HEADER_SIZE + 2 * rs), fmt.HEADER_SIZE);
        swapped.set(file.subarray(fmt.HEADER_SIZE, fmt.HEADER_SIZE + rs), fmt.HEADER_SIZE + rs);
        await bad(swapped, 0);
        // Cut exactly after a chunk: the new last chunk was not written as the last one.
        await bad(file.slice(0, fmt.HEADER_SIZE + 2 * rs), 1);
        // Cut in the middle of the last chunk.
        await bad(file.slice(0, file.byteLength - 10), layout.count - 1);

        // Nothing was changed by any of this, and no passphrase in the Journal.
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM server_state").first<{ n: number }>())?.n).toBe(0);
        const refused = await api("GET", `${V}/admin/log?kind=refused&limit=100`, { token: admin.token });
        const reasons = refused.body.data.entries.map((e: { reason: string }) => e.reason);
        for (const r of ["WRONG_PASSPHRASE", "IMPORT_SHA256_MISMATCH", "BACKUP_CORRUPTED", "INVALID_PASSPHRASE"]) expect(reasons).toContain(r);
        const all = JSON.stringify((await env.DB.prepare("SELECT details FROM audit_log").all()).results);
        expect(all).not.toContain("une autre phrase");
        expect(all).not.toContain(PASS);
    }, T);

    it("asks for the exact confirmation and a check first; an import can be abandoned", async () => {
        const { admin } = await seedWorld();
        const b = await createCopy(admin.token);
        const file = await download(admin.token, b.id);
        const id = await upload(admin.token, file);
        const early = await apply(admin.token, id);
        expect(early.status).toBe(409);
        expect(early.body.error.code).toBe("IMPORT_NOT_CHECKED");
        expect((await check(admin.token, id)).status).toBe(200);
        for (const confirm of ["remplacer toutes les données", "REMPLACER", ""]) {
            const res = await apply(admin.token, id, confirm);
            expect(res.status).toBe(400);
            expect(res.body.error).toMatchObject({ code: "CONFIRMATION_REQUIRED", message: `Pour confirmer, tapez exactement : ${CONFIRM}` });
        }
        const wrong = await apply(admin.token, id, CONFIRM, "une autre phrase secrète");
        expect(wrong.status).toBe(400);
        expect(wrong.body.error.code).toBe("WRONG_PASSPHRASE");
        // Nothing started: no maintenance, no pre_import copy.
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM server_state").first<{ n: number }>())?.n).toBe(0);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM backups WHERE kind = 'pre_import'").first<{ n: number }>())?.n).toBe(0);
        // Abandon after the first step: maintenance lifted, parts deleted.
        const first = await apply(admin.token, id);
        expect(first.body.data).toMatchObject({ done: false, step: "tables" });
        expect((await api("POST", `${V}/presence`, { token: admin.token, body: { state: "active" } })).status).toBe(503);
        const del = await api("DELETE", `${V}/admin/backups/imports/${id}`, { token: admin.token });
        expect(del.status).toBe(200);
        expect((await api("POST", `${V}/presence`, { token: admin.token, body: { state: "active" } })).status).toBe(200);
        expect((await env.STORAGE.list({ prefix: `imports/${id}/` })).objects).toHaveLength(0);
        expect((await apply(admin.token, id)).body.error.code).toBe("IMPORT_CLOSED");
        const log = await api("GET", `${V}/admin/log?kind=backup&limit=100`, { token: admin.token });
        expect(log.body.data.entries.map((e: { action: string }) => e.action)).toContain("backup_import_abandoned");
    }, T);
});

// ------------------------------------------------------------------ crafted files

/** Every entry of a .cpbk, decrypted here with the format alone. */
async function entriesOf(file: Uint8Array, passphrase = PASS): Promise<fmt.Entry[]> {
    const header = fmt.parseHeader(file.subarray(0, fmt.HEADER_SIZE));
    const keys = await fmt.deriveKeys(passphrase, header.salt, header.iterations);
    expect(fmt.keyCheckMatches(header, keys)).toBe(true);
    const layout = fmt.recordLayout(file.byteLength, header.chunkSize);
    const reader = new fmt.EntryReader(0);
    const out: fmt.Entry[] = [];
    for (let i = 0; i < layout.count; ++i) {
        const at = fmt.recordOffset(i, header.chunkSize);
        const last = i === layout.count - 1;
        const rec = file.subarray(at, at + (last ? layout.lastSize : fmt.recordSize(header.chunkSize)));
        out.push(...reader.push(await fmt.decryptChunk(keys.key, header.bytes, i, last, rec)));
    }
    expect(reader.partial).toBe(false);
    for (const e of out) expect(await fmt.entryIntact(e)).toBe(true);
    return out;
}

async function craft(manifest: object, parts: { name: string; text: string }[]): Promise<Uint8Array> {
    const salt = randomData(16);
    const keys = await fmt.deriveKeys(PASS, salt, fmt.DEFAULT_ITERATIONS);
    const header = fmt.encodeHeader(fmt.DEFAULT_ITERATIONS, salt, fmt.CHUNK_SIZE, keys.check);
    async function* plaintext() {
        yield* fmt.entryOf(fmt.ENTRY_MANIFEST, "manifest.json", null, enc.encode(JSON.stringify(manifest)));
        for (const p of parts) yield* fmt.entryOf(fmt.ENTRY_TABLE, p.name, null, enc.encode(p.text));
        const end = { format: "cosmic-backup-end", files_included: 0, files_missing_count: 0, files_missing: [], files_changed_count: 0, files_changed: [] };
        yield* fmt.entryOf(fmt.ENTRY_END, "end.json", null, enc.encode(JSON.stringify(end)));
    }
    return new Uint8Array(await new Response(fmt.encryptStream(plaintext(), keys.key, header, fmt.CHUNK_SIZE)).arrayBuffer());
}

/** The tables of a server-side copy, as crafted entries (R2 files left out). */
async function copyAsParts(backupId: string): Promise<{ manifest: StoredManifest; parts: { name: string; text: string }[] }> {
    const manifest = (await (await env.STORAGE.get(`backups/${backupId}/manifest.json`))!.json()) as StoredManifest;
    const parts: { name: string; text: string }[] = [];
    for (const t of manifest.tables) for (const p of t.parts) parts.push({ name: p, text: await (await env.STORAGE.get(`backups/${backupId}/${p}`))!.text() });
    manifest.files = { count: 0, bytes: 0, lists: [], excluded_prefixes: [] };
    return { manifest, parts };
}

describe("backups: compatibility", () => {
    it("refuses a backup from a newer server or with an unknown table", async () => {
        const admin = await makeAdmin();
        const b = await createCopy(admin.token);
        const { manifest, parts } = await copyAsParts(b.id);
        const newer = await craft({ ...manifest, schema: "9999_future.sql", migrations: [...manifest.migrations, "9999_future.sql"] }, parts);
        let id = await upload(admin.token, newer);
        let res = await check(admin.token, id);
        expect(res.status).toBe(200);
        expect(res.body.data.compatible).toBe(false);
        expect(res.body.data.problems[0]).toMatch(/serveur plus récent \(schéma 9999_future\.sql\).*mettez d'abord ce serveur à jour/);
        expect((await apply(admin.token, id)).body.error.code).toBe("IMPORT_INCOMPATIBLE");
        await api("DELETE", `${V}/admin/backups/imports/${id}`, { token: admin.token });

        const unknown = await craft(
            { ...manifest, tables: [...manifest.tables, { name: "mystere", columns: ["id"], rows: 1, bytes: 6, parts: ["d1/mystere/0000.jsonl"] }] },
            [...parts, { name: "d1/mystere/0000.jsonl", text: '["x"]\n' }],
        );
        id = await upload(admin.token, unknown);
        res = await check(admin.token, id);
        expect(res.body.data.compatible).toBe(false);
        expect(res.body.data.problems).toContain("La table « mystere » de la sauvegarde n'existe pas sur ce serveur : la sauvegarde vient d'une autre version du serveur.");
    }, T);

    it("restores a backup of an older schema: missing columns take their default", async () => {
        const admin = await makeAdmin();
        const kes = await registerPlayer("Kestrel");
        const b = await createCopy(admin.token);
        const { manifest, parts } = await copyAsParts(b.id);
        // As if the copy predated 2.40: no players.role column.
        const players = manifest.tables.find((t) => t.name === "players")!;
        const roleAt = players.columns.indexOf("role");
        players.columns = players.columns.filter((c) => c !== "role");
        for (const p of parts)
            if (p.name.startsWith("d1/players/"))
                p.text = p.text
                    .split("\n")
                    .filter(Boolean)
                    .map((l) => JSON.stringify((JSON.parse(l) as unknown[]).filter((_v, i) => i !== roleAt)))
                    .join("\n") + "\n";
        const older = await craft({ ...manifest, schema: "0073_release_2_40_0.sql" }, parts);
        const id = await upload(admin.token, older);
        const res = await check(admin.token, id);
        expect(res.body.data.compatible).toBe(true);
        expect(res.body.data.problems[0]).toMatch(/^Attention : la sauvegarde date d'un schéma plus ancien \(0073_release_2_40_0\.sql\)/);
        expect(res.body.data.problems).toContain(
            "Attention : la sauvegarde ne contient aucun compte admin actif : après l'import, plus personne ne pourra ouvrir l'écran ADMINISTRATION (il faudra en nommer un avec wrangler).",
        );
        const done = await applyUntilDone(admin.token, id);
        expect(done.summary.session_kept).toBe(false);
        const rows = await env.DB.prepare("SELECT username, role FROM players ORDER BY username").all();
        expect(rows.results).toEqual([
            { username: "Kestrel", role: "player" },
            { username: "NovaRider", role: "player" },
        ]);
        // No admin left in the restored data: the routes are gone for this account too.
        expect((await api("GET", `${V}/admin/backups`, { token: admin.token })).status).toBe(401);
        expect(kes.id).toBeTruthy();
    }, T);
});

describe("backups: copies made while players write", () => {
    it("drops rows whose parent is missing, unlinks SET NULL ones and keeps the last of duplicates", async () => {
        const admin = await makeAdmin();
        const b = await createCopy(admin.token);
        const { manifest, parts } = await copyAsParts(b.id);
        const now = new Date().toISOString();
        const add = (table: string, rows: Record<string, unknown>[]) => {
            const t = manifest.tables.find((x) => x.name === table)!;
            const lines = rows.map((r) => JSON.stringify(t.columns.map((c) => (c in r ? r[c] : null)))).join("\n") + "\n";
            const name = `d1/${table}/${String(t.parts.length).padStart(4, "0")}.jsonl`;
            t.parts.push(name);
            t.rows += rows.length;
            parts.push({ name, text: lines });
        };
        const note = (id: string, player: string, actor: string | null, kind: string) => ({ id, player_id: player, kind, actor_id: actor, data: "{}", count: 1, created_at: now });
        add("notifications", [
            note("n1", admin.id, "compte-disparu", "follow"), // actor gone: link set to NULL
            note("n2", "compte-disparu", admin.id, "follow"), // owner gone: removed
            note("n3", admin.id, null, "level_comment"),
            note("n3", admin.id, null, "follow"), // the same row copied twice: the last one wins
        ]);
        add("player_follows", [{ follower_id: admin.id, creator_id: "compte-disparu", notify: 1, created_at: now }]);
        const id = await upload(admin.token, await craft(manifest, parts));
        expect((await check(admin.token, id)).body.data.compatible).toBe(true);
        const done = await applyUntilDone(admin.token, id);
        expect(done.summary).toMatchObject({ orphans_removed: 3, duplicates_removed: 1, session_kept: true });
        const notes = await env.DB.prepare("SELECT id, player_id, actor_id, kind FROM notifications ORDER BY id").all();
        expect(notes.results).toEqual([
            { id: "n1", player_id: admin.id, actor_id: null, kind: "follow" },
            { id: "n3", player_id: admin.id, actor_id: null, kind: "follow" },
        ]);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM player_follows").first<{ n: number }>())?.n).toBe(0);
        expect((await env.DB.prepare("PRAGMA foreign_key_check").all()).results).toEqual([]);
    }, T);
});

describe("backups: file format", () => {
    it("PBKDF2 above the Workers limit matches the standard function", async () => {
        const pw = enc.encode("phrase secrète de test ✓");
        const salt = enc.encode("0123456789abcdef");
        const hex = (b: Uint8Array) => [...b].map((x) => x.toString(16).padStart(2, "0")).join("");
        expect(hex(await fmt.pbkdf2Sha256Hmac(pw, salt, 2000))).toBe("d36092e51e3fb4a5eb6a02ade82d04adee4025ca94417c2b82f69cc88961fb93");
        expect(hex(await fmt.pbkdf2Sha256(pw, salt, 2000))).toBe("d36092e51e3fb4a5eb6a02ade82d04adee4025ca94417c2b82f69cc88961fb93");
        // Python: hashlib.pbkdf2_hmac('sha256', pw, salt, 300000)
        expect(hex(await fmt.pbkdf2Sha256(pw, salt, 300_000))).toBe("b9f898b1659bfcff6919152a580f075e9581559aa00a362415fe05625d5bb6db");
    }, T);

    it("reads entries cut anywhere by the chunks", async () => {
        const pieces: Uint8Array[] = [];
        const data = randomData(5000);
        for await (const p of fmt.entryOf(fmt.ENTRY_FILE, "replays/a/b.cprz", '{"content_type":null}', data)) pieces.push(p);
        for await (const p of fmt.entryOf(fmt.ENTRY_END, "end.json", null, enc.encode("{}"))) pieces.push(p);
        const all = new Uint8Array(pieces.reduce((n, p) => n + p.byteLength, 0));
        let at = 0;
        for (const p of pieces) {
            all.set(p, at);
            at += p.byteLength;
        }
        for (const step of [1, 7, 33, 4096, all.byteLength]) {
            const reader = new fmt.EntryReader(0);
            const got: fmt.Entry[] = [];
            for (let i = 0; i < all.byteLength; i += step) got.push(...reader.push(all.subarray(i, i + step)));
            expect(got.map((e) => [e.kind, e.name, e.meta, e.data.byteLength, e.start])).toEqual([
                [fmt.ENTRY_FILE, "replays/a/b.cprz", '{"content_type":null}', 5000, 0],
                [fmt.ENTRY_END, "end.json", null, 2, got[0]!.end],
            ]);
            expect(await fmt.entryIntact(got[0]!)).toBe(true);
            expect(got[0]!.data).toEqual(data);
            expect(reader.partial).toBe(false);
        }
    });
});
