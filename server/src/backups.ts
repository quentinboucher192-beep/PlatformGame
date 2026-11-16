// Full backups of the online service (ADMINISTRATION › Sauvegardes, docs/online/BACKUP.md).
//
// * Server-side copies (`auto` by the daily cron, `manual`, `pre_import`): every application
//   table of D1 as JSON lines in R2 under backups/<id>/, plus the list of the players' R2 files
//   (key, size, SHA-256), which stay where they are.
// * Download: the copy and the bytes of its files, streamed as one .cpbk file encrypted with a
//   passphrase chosen by the administrator (never stored, never logged).
// * Import: the file is sent in parts, checked without changing anything, then applied step by
//   step: copy of the current state (`pre_import`), maintenance (player writes answer 503),
//   rows staged in temporary tables, files put back, then every table replaced in ONE D1
//   transaction. Every action and refusal is written to the Journal.
//
// Every route checks the administrator role of the session on each request (requireAdminRole:
// 404 for any other account).

import { requireAdminRole } from "./adminRoles";
import { audit, type AuthInfo } from "./auth";
import * as fmt from "./backupFormat";
import { randomBytes, toHex } from "./crypto";
import { ApiError, Errors, json, log, nowIso, readJsonObject, readLimited, type Bindings, type Ctx } from "./http";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";

// ------------------------------------------------------------------ constants

export type BackupKind = "auto" | "manual" | "pre_import";

/** Kept by the cron: the latest automatic and manual copies, the copies made before an import for some days. */
export const RETENTION = { auto: 14, manual: 10, pre_import_days: 30 } as const;
const FAILED_KEEP_DAYS = 7;
const STALE_RUNNING_MS = 6 * 3600_000;

/** Largest .cpbk accepted by the import routes (bigger files: manual procedure of BACKUP.md). */
export const MAX_IMPORT_BYTES = 1024 * 1024 * 1024;
export const DEFAULT_PART_SIZE = 8 * 1024 * 1024;
const MAX_OPEN_IMPORTS = 3;
const IMPORT_EXPIRY_MS = 48 * 3600_000;
export const CONFIRM_PHRASE = "REMPLACER TOUTES LES DONNÉES";

/** Maintenance expires if no apply request renews it for this long: a forgotten import cannot block the service. */
const MAINTENANCE_TTL_MS = 15 * 60_000;
/** The router re-reads the maintenance flag at most this often per isolate. */
const MAINTENANCE_CACHE_MS = 2000;
/** Wait after setting maintenance before copying the current state (other isolates' cache, requests in flight). */
const DEFAULT_SETTLE_MS = 5000;
const LEASE_MS = 10 * 60_000;

/** Work done by one apply request before it answers `done: false` (Workers limits per request). */
const APPLY_BUDGET_MS = 20_000;
const APPLY_MAX_R2_OPS = 800;
const APPLY_MAX_D1_OPS = 150;
const FILE_GROUP = 16;

const DUMP_PAGE_ROWS = 5000;
const PART_TARGET_BYTES = 4 * 1024 * 1024;
const STAGE_PARAM_BYTES = 900 * 1024;
const STAGE_STATEMENTS_PER_BATCH = 4;

/** R2 prefixes that are never part of a backup: game installers, the backups themselves, uploaded imports. */
const EXCLUDED_R2_PREFIXES = ["releases/", "backups/", "imports/"];
const STAGE_PREFIX = "backup_stage__";
const KEEP_AUDIT = "backup_keep__audit";
const KEEP_SESSION = "backup_keep__session";

/** Tables never saved (and never replaced by an import), with the reason written in the manifest. */
const EXCLUDED_TABLES: Record<string, string> = {
    player_sessions:
        "Sessions de connexion : jamais sauvegardées. Après une restauration, chaque joueur se reconnecte avec son mot de passe.",
    rate_limits: "Compteurs temporaires de limitation des requêtes.",
    server_state: "État du serveur (maintenance).",
    backups: "Liste des copies de sauvegarde elle-même.",
    backup_imports: "Imports de sauvegarde en cours.",
    backup_import_parts: "Imports de sauvegarde en cours.",
};

const encoder = new TextEncoder();
const strictUtf8 = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });
const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

/** An error whose French message can be shown and stored as is. */
class BackupError extends Error {}

function q(id: string): string {
    return '"' + id.replace(/"/g, '""') + '"';
}

function partName(n: number): string {
    return String(n).padStart(4, "0");
}

// ------------------------------------------------------------------ schema

interface Column {
    name: string;
    type: string;
    notnull: boolean;
    dflt: string | null;
    pk: number;
    hidden: number;
}

interface ForeignKey {
    parent: string;
    from: string[];
    to: (string | null)[];
    onDelete: string;
}

interface TableSchema {
    name: string;
    columns: Column[];
    fks: ForeignKey[];
    withoutRowid: boolean;
}

interface Schema {
    tables: Map<string, TableSchema>;
    /** Saved and replaced, parents before children (foreign keys). */
    restorable: string[];
    excluded: { name: string; reason: string }[];
    migrations: string[];
    version: string | null;
}

function isInternalTable(name: string): boolean {
    return /^sqlite_/i.test(name) || /^_cf_/i.test(name) || name.toLowerCase() === "d1_migrations";
}

function isScratchTable(name: string): boolean {
    return name.startsWith(STAGE_PREFIX) || name.startsWith("backup_keep__");
}

/** Columns a row is written with (generated columns are computed by SQLite). */
function storedColumns(t: TableSchema): Column[] {
    return t.columns.filter((c) => c.hidden === 0);
}

function findCi<T>(map: Map<string, T>, name: string): T | undefined {
    const direct = map.get(name);
    if (direct !== undefined) return direct;
    const lower = name.toLowerCase();
    for (const [k, v] of map) if (k.toLowerCase() === lower) return v;
    return undefined;
}

async function readSchema(db: D1Database): Promise<Schema> {
    const master = await db.prepare("SELECT name, sql FROM sqlite_master WHERE type = 'table' ORDER BY name").all<{ name: string; sql: string | null }>();
    const all = master.results.filter((t) => !isInternalTable(t.name) && !isScratchTable(t.name));
    const virtual = new Set(all.filter((t) => /^\s*CREATE\s+VIRTUAL\s+TABLE/i.test(t.sql ?? "")).map((t) => t.name));
    const real = all.filter((t) => !virtual.has(t.name));
    const stmts: D1PreparedStatement[] = [];
    for (const t of real)
        stmts.push(db.prepare("SELECT * FROM pragma_table_xinfo(?1)").bind(t.name), db.prepare("SELECT * FROM pragma_foreign_key_list(?1)").bind(t.name));
    const res = stmts.length ? await db.batch(stmts) : [];
    let migrations: string[] = [];
    try {
        migrations = (await db.prepare("SELECT name FROM d1_migrations ORDER BY id").all<{ name: string }>()).results.map((r) => r.name);
    } catch {
        migrations = [];
    }
    const tables = new Map<string, TableSchema>();
    real.forEach((t, i) => {
        const cols = (res[2 * i]?.results ?? []) as { name: string; type: string; notnull: number; dflt_value: string | null; pk: number; hidden: number }[];
        const fkRows = (res[2 * i + 1]?.results ?? []) as { id: number; seq: number; table: string; from: string; to: string | null; on_delete: string }[];
        const fks = new Map<number, ForeignKey>();
        for (const f of [...fkRows].sort((a, b) => a.id - b.id || a.seq - b.seq)) {
            let fk = fks.get(f.id);
            if (!fk) fks.set(f.id, (fk = { parent: f.table, from: [], to: [], onDelete: (f.on_delete ?? "NO ACTION").toUpperCase() }));
            fk.from.push(f.from);
            fk.to.push(f.to);
        }
        tables.set(t.name, {
            name: t.name,
            columns: cols.map((c) => ({ name: c.name, type: c.type ?? "", notnull: !!c.notnull, dflt: c.dflt_value, pk: c.pk, hidden: c.hidden ?? 0 })),
            fks: [...fks.values()],
            withoutRowid: /WITHOUT\s+ROWID/i.test(t.sql ?? ""),
        });
    });
    const excluded: { name: string; reason: string }[] = [];
    const candidates: string[] = [];
    for (const t of all) {
        const reason = virtual.has(t.name) ? "Table virtuelle : reconstruite par le serveur." : EXCLUDED_TABLES[t.name];
        if (reason) excluded.push({ name: t.name, reason });
        else candidates.push(t.name);
    }
    return { tables, restorable: topoOrder(candidates, tables), excluded, migrations, version: migrations.length ? migrations[migrations.length - 1]! : null };
}

/** Parents before children; a cycle (none today) keeps its tables in name order. */
function topoOrder(names: string[], tables: Map<string, TableSchema>): string[] {
    const byLower = new Map(names.map((n) => [n.toLowerCase(), n]));
    const deps = new Map<string, Set<string>>();
    for (const n of names) {
        const s = new Set<string>();
        for (const fk of tables.get(n)?.fks ?? []) {
            const p = byLower.get(fk.parent.toLowerCase());
            if (p && p !== n) s.add(p);
        }
        deps.set(n, s);
    }
    const out: string[] = [];
    const done = new Set<string>();
    while (out.length < names.length) {
        const ready = names.filter((n) => !done.has(n) && [...deps.get(n)!].every((p) => done.has(p)));
        if (ready.length === 0) {
            for (const n of names) if (!done.has(n)) out.push(n);
            break;
        }
        for (const r of ready) {
            out.push(r);
            done.add(r);
        }
    }
    return out;
}

function pkColumns(t: TableSchema): string[] {
    return t.columns.filter((c) => c.pk > 0).sort((a, b) => a.pk - b.pk).map((c) => c.name);
}

/** INTEGER PRIMARY KEY: an alias of the rowid, assigned by SQLite when absent. */
function isRowidAlias(t: TableSchema, c: Column): boolean {
    return !t.withoutRowid && c.pk === 1 && pkColumns(t).length === 1 && c.type.toUpperCase() === "INTEGER";
}

// ------------------------------------------------------------------ maintenance flag

interface MaintenanceValue {
    import_id: string;
    since: string;
    expires_at: string;
}

let maintenanceCache: { at: number; value: MaintenanceValue | null } | null = null;

async function readMaintenance(db: D1Database): Promise<MaintenanceValue | null> {
    const row = await db.prepare("SELECT value FROM server_state WHERE key = 'maintenance'").first<{ value: string }>();
    if (!row) return null;
    try {
        const v = JSON.parse(row.value) as MaintenanceValue;
        return Date.parse(v.expires_at) > Date.now() ? v : null;
    } catch {
        return null;
    }
}

export async function maintenanceActive(env: Bindings): Promise<boolean> {
    try {
        return (await readMaintenance(env.DB)) !== null;
    } catch {
        return false;
    }
}

const WRITE_METHODS = new Set(["POST", "PUT", "PATCH", "DELETE"]);
/** Still answered during maintenance: the administration, and signing in (an admin must be able to finish or check a restore). */
const MAINTENANCE_EXEMPT = [/^\/api\/v1\/admin\//, /^\/api\/v1\/auth\/(login|refresh|logout)$/];

/** Router check: while an import replaces the data, the other write requests answer 503 MAINTENANCE. */
export async function assertWritable(ctx: Ctx): Promise<void> {
    if (!WRITE_METHODS.has(ctx.request.method)) return;
    if (MAINTENANCE_EXEMPT.some((r) => r.test(ctx.url.pathname))) return;
    const now = Date.now();
    if (!maintenanceCache || now - maintenanceCache.at > MAINTENANCE_CACHE_MS) {
        let value: MaintenanceValue | null = null;
        try {
            value = await readMaintenance(ctx.env.DB);
        } catch (e) {
            // A missing table (migration not applied yet) must never block the service.
            log("warn", "maintenance_check_failed", { error: e instanceof Error ? e.message : String(e) });
        }
        maintenanceCache = { at: now, value };
    }
    const v = maintenanceCache.value;
    if (v && Date.parse(v.expires_at) > now)
        throw new ApiError(503, "MAINTENANCE", "Maintenance en cours : réessayez dans quelques minutes.", undefined, { "retry-after": "120" });
}

/** Takes the maintenance for an import (or renews it); null when another import holds it. */
async function acquireMaintenance(db: D1Database, importId: string): Promise<MaintenanceValue | null> {
    const now = nowIso();
    const value: MaintenanceValue = { import_id: importId, since: now, expires_at: new Date(Date.now() + MAINTENANCE_TTL_MS).toISOString() };
    const res = await db
        .prepare(
            `INSERT INTO server_state (key, value, updated_at) VALUES ('maintenance', ?1, ?2)
             ON CONFLICT (key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at
             WHERE json_extract(server_state.value, '$.import_id') = ?3 OR json_extract(server_state.value, '$.expires_at') <= ?2`,
        )
        .bind(JSON.stringify(value), now, importId)
        .run();
    maintenanceCache = null;
    return res.meta.changes ? value : null;
}

/** Pushes the expiry back; false when the maintenance lapsed or belongs to another import. */
async function renewMaintenance(db: D1Database, importId: string): Promise<boolean> {
    const now = nowIso();
    const res = await db
        .prepare(
            `UPDATE server_state SET value = json_set(value, '$.expires_at', ?3), updated_at = ?2
             WHERE key = 'maintenance' AND json_extract(value, '$.import_id') = ?1 AND json_extract(value, '$.expires_at') > ?2`,
        )
        .bind(importId, now, new Date(Date.now() + MAINTENANCE_TTL_MS).toISOString())
        .run();
    return res.meta.changes > 0;
}

async function liftMaintenance(db: D1Database, importId: string): Promise<void> {
    await db.prepare("DELETE FROM server_state WHERE key = 'maintenance' AND json_extract(value, '$.import_id') = ?1").bind(importId).run();
    maintenanceCache = null;
}

// ------------------------------------------------------------------ backup rows

interface BackupRow {
    id: string;
    kind: BackupKind;
    status: "running" | "ready" | "failed";
    created_by: string | null;
    created_by_username: string | null;
    created_at: string;
    finished_at: string | null;
    schema_version: string | null;
    tables_count: number;
    rows_count: number;
    files_count: number;
    bytes: number;
    error: string | null;
    current_username?: string | null;
}

function backupJson(b: BackupRow) {
    return {
        id: b.id,
        created_at: b.created_at,
        finished_at: b.finished_at,
        kind: b.kind,
        by: b.created_by ? { id: b.created_by, username: b.current_username ?? b.created_by_username } : null,
        schema: b.schema_version,
        tables: b.tables_count,
        rows: b.rows_count,
        files: b.files_count,
        bytes: b.bytes,
        status: b.status,
        error: b.error,
    };
}

interface Actor {
    id: string;
    username: string;
}

function actorOf(auth: AuthInfo): Actor {
    return { id: auth.player.id, username: auth.player.username };
}

/** What a server-side copy says about itself (backups/<id>/manifest.json, first entry of a .cpbk). */
export interface StoredManifest {
    format: "cosmic-backup";
    format_version: 1;
    backup_id: string;
    kind: BackupKind;
    created_at: string;
    finished_at: string;
    created_by: Actor | null;
    environment: string | null;
    schema: string | null;
    migrations: string[];
    tables: { name: string; columns: string[]; rows: number; bytes: number; parts: string[] }[];
    excluded_tables: { name: string; reason: string }[];
    files: { count: number; bytes: number; lists: string[]; excluded_prefixes: string[] };
    totals: { tables: number; rows: number; files: number; bytes: number };
    notes: string[];
}

export interface FileRecord {
    key: string;
    size: number;
    sha256: string;
    etag: string;
    uploaded: string;
    content_type: string | null;
    custom_metadata: Record<string, string>;
}

const MANIFEST_NOTES = [
    "Les sessions de connexion (player_sessions) ne sont pas sauvegardées : après une restauration, chaque joueur se reconnecte.",
    "Les mots de passe ne figurent que sous forme d'empreintes PBKDF2 (player_credentials), jamais en clair.",
    "Les fichiers R2 listés (replays, avatars, niveaux de la communauté, projets…) sont dans le fichier .cpbk téléchargé ; les installateurs du jeu (releases/) n'en font pas partie.",
];

// ------------------------------------------------------------------ creating a copy

async function dumpTable(env: Bindings, t: TableSchema, prefix: string): Promise<StoredManifest["tables"][number]> {
    const cols = storedColumns(t).map((c) => c.name);
    const colSql = cols.map(q).join(", ");
    const parts: string[] = [];
    let lines: string[] = [];
    let pending = 0;
    let rows = 0;
    let bytes = 0;
    const flush = async () => {
        if (lines.length === 0) return;
        const data = encoder.encode(lines.join("\n") + "\n");
        const name = `d1/${t.name}/${partName(parts.length)}.jsonl`;
        await env.STORAGE.put(prefix + name, data, { httpMetadata: { contentType: "application/x-ndjson" } });
        parts.push(name);
        bytes += data.byteLength;
        lines = [];
        pending = 0;
    };
    const order = t.withoutRowid ? pkColumns(t).map(q).join(", ") || colSql : "rowid";
    let lastRowid: number | null = null;
    let offset = 0;
    for (;;) {
        let page: unknown[][];
        if (t.withoutRowid) {
            page = await env.DB.prepare(`SELECT ${colSql} FROM ${q(t.name)} ORDER BY ${order} LIMIT ?1 OFFSET ?2`).bind(DUMP_PAGE_ROWS, offset).raw();
            offset += page.length;
        } else {
            page =
                lastRowid === null
                    ? await env.DB.prepare(`SELECT rowid, ${colSql} FROM ${q(t.name)} ORDER BY rowid LIMIT ?1`).bind(DUMP_PAGE_ROWS).raw()
                    : await env.DB.prepare(`SELECT rowid, ${colSql} FROM ${q(t.name)} WHERE rowid > ?1 ORDER BY rowid LIMIT ?2`)
                          .bind(lastRowid, DUMP_PAGE_ROWS)
                          .raw();
        }
        for (const r of page) {
            const values = t.withoutRowid ? r : r.slice(1);
            for (const v of values)
                if (!(v === null || typeof v === "string" || (typeof v === "number" && Number.isFinite(v))))
                    throw new BackupError(`La table ${t.name} contient une valeur binaire ou non finie, que ce format ne sait pas encore sauvegarder.`);
            const line = JSON.stringify(values);
            lines.push(line);
            pending += line.length + 1;
            rows += 1;
            if (pending >= PART_TARGET_BYTES) await flush();
        }
        if (page.length < DUMP_PAGE_ROWS) break;
        if (!t.withoutRowid) lastRowid = page[page.length - 1]![0] as number;
    }
    await flush();
    return { name: t.name, columns: cols, rows, bytes, parts };
}

/** The players' R2 objects, page by page: every top-level prefix except the excluded ones. */
async function* listPlayerObjects(env: Bindings): AsyncGenerator<R2Object[]> {
    const include: ("httpMetadata" | "customMetadata")[] = ["httpMetadata", "customMetadata"];
    const prefixes: string[] = [];
    const root: R2Object[] = [];
    let cursor: string | undefined;
    do {
        const l = await env.STORAGE.list({ delimiter: "/", cursor, include });
        prefixes.push(...l.delimitedPrefixes);
        root.push(...l.objects);
        cursor = l.truncated ? l.cursor : undefined;
    } while (cursor);
    if (root.length) yield root;
    for (const p of prefixes.sort()) {
        if (EXCLUDED_R2_PREFIXES.includes(p)) continue;
        let c: string | undefined;
        do {
            const l = await env.STORAGE.list({ prefix: p, cursor: c, include });
            if (l.objects.length) yield l.objects;
            c = l.truncated ? l.cursor : undefined;
        } while (c);
    }
}

function hexOf(buf: ArrayBuffer | undefined): string | null {
    return buf && buf.byteLength === 32 ? toHex(new Uint8Array(buf)) : null;
}

async function hashObject(env: Bindings, key: string): Promise<{ sha256: string; size: number } | null> {
    const obj = await env.STORAGE.get(key);
    if (!obj) return null;
    const ds = new crypto.DigestStream("SHA-256");
    await obj.body.pipeTo(ds);
    return { sha256: toHex(new Uint8Array(await ds.digest)), size: obj.size };
}

/** SHA-256 of the files of the latest ready copy: unchanged objects are not read again. */
async function previousHashes(env: Bindings): Promise<Map<string, FileRecord>> {
    const map = new Map<string, FileRecord>();
    const last = await env.DB.prepare("SELECT id FROM backups WHERE status = 'ready' ORDER BY created_at DESC LIMIT 1").first<{ id: string }>();
    if (!last) return map;
    try {
        const m = await env.STORAGE.get(`backups/${last.id}/manifest.json`);
        if (!m) return map;
        const manifest = (await m.json()) as StoredManifest;
        for (const list of manifest.files.lists) {
            const o = await env.STORAGE.get(`backups/${last.id}/${list}`);
            if (!o) continue;
            for (const line of (await o.text()).split("\n")) if (line) {
                const r = JSON.parse(line) as FileRecord;
                map.set(r.key, r);
            }
        }
    } catch (e) {
        log("warn", "backup_previous_hashes_unreadable", { error: e instanceof Error ? e.message : String(e) });
    }
    return map;
}

async function inventoryFiles(env: Bindings, prefix: string): Promise<StoredManifest["files"] & { listBytes: number }> {
    const previous = await previousHashes(env);
    const lists: string[] = [];
    let lines: string[] = [];
    let pending = 0;
    let count = 0;
    let bytes = 0;
    let listBytes = 0;
    const flush = async () => {
        if (lines.length === 0) return;
        const data = encoder.encode(lines.join("\n") + "\n");
        const name = `files/${partName(lists.length)}.jsonl`;
        await env.STORAGE.put(prefix + name, data, { httpMetadata: { contentType: "application/x-ndjson" } });
        lists.push(name);
        listBytes += data.byteLength;
        lines = [];
        pending = 0;
    };
    for await (const page of listPlayerObjects(env)) {
        const records: (FileRecord | null)[] = page.map((o) => {
            const uploaded = o.uploaded.toISOString();
            const prev = previous.get(o.key);
            const custom = o.customMetadata ?? {};
            const known =
                hexOf(o.checksums?.sha256) ??
                (prev && prev.etag === o.etag && prev.size === o.size && prev.uploaded === uploaded ? prev.sha256 : null) ??
                (typeof custom.sha256 === "string" && /^[0-9a-f]{64}$/.test(custom.sha256) ? custom.sha256 : null);
            return {
                key: o.key,
                size: o.size,
                sha256: known ?? "",
                etag: o.etag,
                uploaded,
                content_type: o.httpMetadata?.contentType ?? null,
                custom_metadata: custom,
            };
        });
        // Objects never hashed before are read (8 at a time); one deleted meanwhile is left out.
        const todo = records.filter((r): r is FileRecord => r !== null && r.sha256 === "");
        for (let i = 0; i < todo.length; i += 8) {
            await Promise.all(
                todo.slice(i, i + 8).map(async (r) => {
                    const h = await hashObject(env, r.key);
                    if (!h) {
                        records[records.indexOf(r)] = null;
                        return;
                    }
                    r.sha256 = h.sha256;
                    r.size = h.size;
                }),
            );
        }
        for (const r of records) {
            if (!r) continue;
            const line = JSON.stringify(r);
            lines.push(line);
            pending += line.length + 1;
            count += 1;
            bytes += r.size;
            if (pending >= PART_TARGET_BYTES) await flush();
        }
    }
    await flush();
    return { count, bytes, lists, excluded_prefixes: EXCLUDED_R2_PREFIXES, listBytes };
}

async function deletePrefix(env: Bindings, prefix: string): Promise<number> {
    let deleted = 0;
    for (;;) {
        const l = await env.STORAGE.list({ prefix, limit: 1000 });
        if (l.objects.length === 0) break;
        await env.STORAGE.delete(l.objects.map((o) => o.key));
        deleted += l.objects.length;
        if (!l.truncated) break;
    }
    return deleted;
}

/** Makes a server-side copy now. Throws ApiError 500 BACKUP_FAILED (the row stays, status `failed`). */
export async function createBackup(env: Bindings, kind: BackupKind, by: Actor | null): Promise<BackupRow> {
    const id = crypto.randomUUID();
    const createdAt = nowIso();
    await env.DB.prepare("INSERT INTO backups (id, kind, status, created_by, created_by_username, created_at) VALUES (?1, ?2, 'running', ?3, ?4, ?5)")
        .bind(id, kind, by?.id ?? null, by?.username ?? null, createdAt)
        .run();
    const prefix = `backups/${id}/`;
    try {
        const schema = await readSchema(env.DB);
        const tables: StoredManifest["tables"] = [];
        let rows = 0;
        let bytes = 0;
        // Children before parents: a row added while the copy runs is more often a child of a
        // row already copied than the opposite (any orphan is removed when importing).
        for (const name of [...schema.restorable].reverse()) {
            const t = await dumpTable(env, schema.tables.get(name)!, prefix);
            tables.push(t);
            rows += t.rows;
            bytes += t.bytes;
        }
        const { listBytes, ...files } = await inventoryFiles(env, prefix);
        const finishedAt = nowIso();
        const manifest: StoredManifest = {
            format: "cosmic-backup",
            format_version: 1,
            backup_id: id,
            kind,
            created_at: createdAt,
            finished_at: finishedAt,
            created_by: by,
            environment: env.ENVIRONMENT ?? null,
            schema: schema.version,
            migrations: schema.migrations,
            tables,
            excluded_tables: schema.excluded,
            files,
            totals: { tables: tables.length, rows, files: files.count, bytes: bytes + files.bytes + listBytes },
            notes: MANIFEST_NOTES,
        };
        const text = encoder.encode(JSON.stringify(manifest));
        await env.STORAGE.put(prefix + "manifest.json", text, { httpMetadata: { contentType: "application/json" } });
        const total = manifest.totals.bytes + text.byteLength;
        await env.DB.prepare(
            `UPDATE backups SET status = 'ready', finished_at = ?2, schema_version = ?3, tables_count = ?4, rows_count = ?5, files_count = ?6, bytes = ?7
             WHERE id = ?1`,
        )
            .bind(id, finishedAt, schema.version, tables.length, rows, files.count, total)
            .run();
        return (await env.DB.prepare("SELECT * FROM backups WHERE id = ?1").bind(id).first<BackupRow>())!;
    } catch (e) {
        const message = e instanceof BackupError ? e.message : "La copie n'a pas pu être terminée (détails dans les journaux du serveur).";
        log("error", "backup_failed", { backup_id: id, kind, error: e instanceof Error ? e.message : String(e) });
        try {
            await env.DB.prepare("UPDATE backups SET status = 'failed', finished_at = ?2, error = ?3 WHERE id = ?1").bind(id, nowIso(), message).run();
            await deletePrefix(env, prefix);
        } catch {
            // the cron cleans failed copies up
        }
        throw new ApiError(500, "BACKUP_FAILED", `La sauvegarde a échoué : ${message}`, { backup_id: id });
    }
}

async function deleteBackup(env: Bindings, id: string): Promise<void> {
    await deletePrefix(env, `backups/${id}/`);
    await env.DB.prepare("DELETE FROM backups WHERE id = ?1").bind(id).run();
}

/** Keeps the 14 latest automatic copies, the 10 latest manual ones, pre_import copies for 30 days. */
export async function applyRetention(env: Bindings, now = Date.now()): Promise<string[]> {
    const rows = await env.DB.prepare("SELECT id, kind, status, created_at FROM backups ORDER BY created_at DESC, id DESC").all<{
        id: string;
        kind: BackupKind;
        status: BackupRow["status"];
        created_at: string;
    }>();
    const seen = { auto: 0, manual: 0 };
    const doomed: string[] = [];
    for (const b of rows.results) {
        const age = now - Date.parse(b.created_at);
        if (b.status === "running") {
            if (age > STALE_RUNNING_MS) {
                await env.DB.prepare("UPDATE backups SET status = 'failed', finished_at = ?2, error = ?3 WHERE id = ?1 AND status = 'running'")
                    .bind(b.id, nowIso(), "Copie interrompue.")
                    .run();
                await deletePrefix(env, `backups/${b.id}/`);
            }
            continue;
        }
        if (b.status === "failed") {
            if (age > FAILED_KEEP_DAYS * 86400_000) doomed.push(b.id);
            continue;
        }
        if (b.kind === "pre_import") {
            if (age > RETENTION.pre_import_days * 86400_000) doomed.push(b.id);
            continue;
        }
        seen[b.kind] += 1;
        if (seen[b.kind] > RETENTION[b.kind]) doomed.push(b.id);
    }
    for (const id of doomed) await deleteBackup(env, id);
    return doomed;
}

// ------------------------------------------------------------------ download

async function* fileRecords(env: Bindings, prefix: string, lists: string[]): AsyncGenerator<FileRecord> {
    for (const list of lists) {
        const o = await env.STORAGE.get(prefix + list);
        if (!o) throw new Error(`file list ${list} missing`);
        for (const line of (await o.text()).split("\n")) if (line) yield JSON.parse(line) as FileRecord;
    }
}

async function listKeys(env: Bindings, prefix: string): Promise<Set<string>> {
    const keys = new Set<string>();
    let cursor: string | undefined;
    do {
        const l = await env.STORAGE.list({ prefix, cursor });
        for (const o of l.objects) keys.add(o.key.slice(prefix.length));
        cursor = l.truncated ? l.cursor : undefined;
    } while (cursor);
    return keys;
}

/** One entry whose data is an R2 object, streamed; returns the SHA-256 (hex) of what was sent. */
async function* objectEntry(kind: number, name: string, meta: string | null, obj: R2ObjectBody): AsyncGenerator<Uint8Array, string, undefined> {
    yield fmt.encodeEntryHead(kind, name, meta, obj.size);
    const ds = new crypto.DigestStream("SHA-256");
    const writer = ds.getWriter();
    const reader = (obj.body as ReadableStream<Uint8Array>).getReader();
    let n = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        n += value.byteLength;
        await writer.write(value);
        yield value;
    }
    await writer.close();
    if (n !== obj.size) throw new Error(`object ${name}: ${n} bytes read, ${obj.size} expected`);
    const digest = new Uint8Array(await ds.digest);
    yield digest;
    return toHex(digest);
}

/** The plaintext of a .cpbk: manifest, D1 parts, file lists, files, end. */
async function* backupPlaintext(env: Bindings, b: BackupRow): AsyncGenerator<Uint8Array> {
    const prefix = `backups/${b.id}/`;
    const mobj = await env.STORAGE.get(prefix + "manifest.json");
    if (!mobj) throw new Error("manifest missing");
    const mbytes = new Uint8Array(await mobj.arrayBuffer());
    const manifest = JSON.parse(strictUtf8.decode(mbytes)) as StoredManifest;
    yield* fmt.entryOf(fmt.ENTRY_MANIFEST, "manifest.json", null, mbytes);
    for (const t of manifest.tables)
        for (const part of t.parts) {
            const o = await env.STORAGE.get(prefix + part);
            if (!o) throw new Error(`part ${part} missing`);
            yield* objectEntry(fmt.ENTRY_TABLE, part, null, o);
        }
    for (const list of manifest.files.lists) {
        const o = await env.STORAGE.get(prefix + list);
        if (!o) throw new Error(`list ${list} missing`);
        yield* objectEntry(fmt.ENTRY_FILE_LIST, list, null, o);
    }
    // Objects replaced by an import after this copy was made were saved under objects/.
    const saved = await listKeys(env, prefix + "objects/");
    const source = (key: string) => (saved.has(key) ? prefix + "objects/" + key : key);
    const missing: string[] = [];
    const changed: string[] = [];
    let included = 0;
    type Fetched = { rec: FileRecord; got: Promise<{ obj: R2ObjectBody | null } | { error: unknown }> };
    const records = fileRecords(env, prefix, manifest.files.lists);
    const queue: Fetched[] = [];
    const fill = async () => {
        while (queue.length < 4) {
            const r = await records.next();
            if (r.done) break;
            queue.push({
                rec: r.value,
                got: env.STORAGE.get(source(r.value.key)).then(
                    (obj) => ({ obj }),
                    (error: unknown) => ({ error }),
                ),
            });
        }
    };
    await fill();
    while (queue.length) {
        const { rec, got } = queue.shift()!;
        const res = await got;
        if ("error" in res) throw res.error;
        await fill();
        if (!res.obj) {
            missing.push(rec.key);
            continue;
        }
        const meta = JSON.stringify({ content_type: res.obj.httpMetadata?.contentType ?? null, custom_metadata: res.obj.customMetadata ?? {} });
        const sha = yield* objectEntry(fmt.ENTRY_FILE, rec.key, meta, res.obj);
        included += 1;
        if (sha !== rec.sha256) changed.push(rec.key);
    }
    const end = {
        format: "cosmic-backup-end",
        backup_id: b.id,
        files_included: included,
        files_missing_count: missing.length,
        files_missing: missing.slice(0, 10000),
        files_changed_count: changed.length,
        files_changed: changed.slice(0, 10000),
        completed_at: nowIso(),
    };
    yield* fmt.entryOf(fmt.ENTRY_END, "end.json", null, encoder.encode(JSON.stringify(end)));
}

// ------------------------------------------------------------------ request helpers

function passphraseOf(body: Record<string, unknown>): string {
    const raw = body.passphrase;
    const bad = (message: string) => new ApiError(400, "INVALID_PASSPHRASE", message);
    if (typeof raw !== "string") throw bad("La phrase secrète est obligatoire.");
    const p = fmt.normalizePassphrase(raw);
    const n = [...p].length;
    if (n < 12 || n > 200) throw bad("La phrase secrète doit contenir de 12 à 200 caractères.");
    // eslint-disable-next-line no-control-regex
    if (/[\u0000-\u001f\u007f]/.test(p)) throw bad("La phrase secrète ne doit pas contenir de caractère de contrôle.");
    return p;
}

/** Audits a refusal (never the passphrase) and throws it. */
async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "backup_refused", { ...details, reason: error.code, by_username: auth.player.username });
    throw error;
}

function systemCtx(env: Bindings, exec: ExecutionContext): Ctx {
    const url = new URL("https://maintenance.invalid/");
    return { request: new Request(url), env, exec, url, params: {}, requestId: crypto.randomUUID(), ip: "cron" };
}

// ------------------------------------------------------------------ routes: copies

/** GET /admin/backups — the copies (newest first), the retention, the maintenance and the open imports. */
export async function listBackups(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const db = ctx.env.DB;
    const [rows, imports, maintenance] = await Promise.all([
        db
            .prepare(
                `SELECT b.*, p.username AS current_username FROM backups b LEFT JOIN players p ON p.id = b.created_by
                 ORDER BY b.created_at DESC, b.id DESC LIMIT 200`,
            )
            .all<BackupRow>(),
        db
            .prepare(
                `SELECT i.*, p.username AS current_username,
                   (SELECT COUNT(*) FROM backup_import_parts x WHERE x.import_id = i.id) AS parts_received,
                   (SELECT COALESCE(SUM(size), 0) FROM backup_import_parts x WHERE x.import_id = i.id) AS received
                 FROM backup_imports i LEFT JOIN players p ON p.id = i.created_by
                 WHERE i.status IN ('uploading', 'checked', 'applying', 'failed') ORDER BY i.created_at DESC LIMIT 20`,
            )
            .all<ImportRow>(),
        readMaintenance(db),
    ]);
    return json(ctx, {
        backups: rows.results.map(backupJson),
        retention: RETENTION,
        maintenance: maintenance ? { active: true, import_id: maintenance.import_id, since: maintenance.since, expires_at: maintenance.expires_at } : null,
        imports: imports.results.map(importJson),
    });
}

/** POST /admin/backups — a server-side copy now (kind `manual`). */
export async function createBackupRoute(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.backupCreate, "player:" + auth.player.id);
    if (await readMaintenance(ctx.env.DB))
        await refuse(ctx, auth, Errors.conflict("IMPORT_IN_PROGRESS", "Un import est en train de remplacer les données : attendez la fin."), { action: "create" });
    let b: BackupRow;
    try {
        b = await createBackup(ctx.env, "manual", actorOf(auth));
    } catch (e) {
        if (e instanceof ApiError)
            await audit(ctx, auth.player.id, "backup_failed", {
                backup_id: (e.details as { backup_id?: string } | undefined)?.backup_id ?? null,
                backup_kind: "manual",
                by_username: auth.player.username,
            });
        throw e;
    }
    await audit(ctx, auth.player.id, "backup_created", {
        backup_id: b.id,
        backup_kind: "manual",
        rows: b.rows_count,
        files: b.files_count,
        by_username: auth.player.username,
    });
    return json(ctx, { backup: backupJson({ ...b, current_username: auth.player.username }) }, { status: 201 });
}

/** POST /admin/backups/{backupId}/download {"passphrase"} — the encrypted .cpbk, streamed. */
export async function downloadBackup(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.backupDownload, "player:" + auth.player.id);
    const id = ctx.params.backupId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("BACKUP_NOT_FOUND", "Sauvegarde introuvable.");
    const body = await readJsonObject(ctx);
    new Validator(body, ["passphrase"]).done();
    const where = { action: "download", backup_id: id };
    let passphrase: string;
    try {
        passphrase = passphraseOf(body);
    } catch (e) {
        return refuse(ctx, auth, e as ApiError, where);
    }
    const b = await ctx.env.DB.prepare("SELECT * FROM backups WHERE id = ?1").bind(id).first<BackupRow>();
    if (!b) throw Errors.notFound("BACKUP_NOT_FOUND", "Sauvegarde introuvable.");
    if (b.status !== "ready")
        await refuse(ctx, auth, Errors.conflict("BACKUP_NOT_READY", b.status === "failed" ? "Cette sauvegarde a échoué : elle ne peut pas être téléchargée." : "Cette sauvegarde est encore en cours."), where);
    const salt = randomBytes(16);
    const keys = await fmt.deriveKeys(passphrase, salt, fmt.DEFAULT_ITERATIONS);
    const header = fmt.encodeHeader(fmt.DEFAULT_ITERATIONS, salt, fmt.CHUNK_SIZE, keys.check);
    await audit(ctx, auth.player.id, "backup_downloaded", { backup_id: b.id, backup_kind: b.kind, by_username: auth.player.username });
    const plaintext = backupPlaintext(ctx.env, b);
    const logged: AsyncIterator<Uint8Array> = {
        next: async () => {
            try {
                return await plaintext.next();
            } catch (e) {
                // The stream stops: the file is incomplete, which an import detects (no last chunk).
                log("error", "backup_download_failed", { backup_id: b.id, error: e instanceof Error ? e.message : String(e) });
                throw e;
            }
        },
        return: async () => plaintext.return(undefined),
    };
    const stream = fmt.encryptStream(logged, keys.key, header, fmt.CHUNK_SIZE);
    return new Response(stream, {
        status: 200,
        headers: {
            "content-type": "application/octet-stream",
            "content-disposition": `attachment; filename="cosmic-backup-${b.created_at.slice(0, 10)}.cpbk"`,
            "cache-control": "no-store",
            "x-content-type-options": "nosniff",
            "referrer-policy": "no-referrer",
            "x-backup-id": b.id,
            // Size of the content before encryption (the file is about 0.003 % larger), for a progress bar.
            "x-backup-plaintext-bytes": String(b.bytes),
        },
    });
}

// ------------------------------------------------------------------ imports

interface ImportRow {
    id: string;
    created_by: string;
    created_by_username: string | null;
    created_at: string;
    updated_at: string;
    size: number;
    sha256: string;
    part_size: number;
    parts: number;
    status: "uploading" | "checked" | "applying" | "done" | "failed" | "abandoned" | "expired";
    sha256_verified: number;
    checked: string | null;
    compatible: number;
    state: string | null;
    pre_import_id: string | null;
    lease_until: string | null;
    finished_at: string | null;
    error: string | null;
    current_username?: string | null;
    parts_received?: number;
    received?: number;
}

interface CheckSummary {
    id: string | null;
    kind: string | null;
    created_at: string;
    schema: string | null;
    environment: string | null;
    tables: number;
    rows: number;
    files: number;
    bytes: number;
}

interface CheckedInfo {
    summary: CheckSummary;
    /** Tables of the file and their columns, in the order of each JSON line. */
    tables: Record<string, string[]>;
    compatible: boolean;
    problems: string[];
    /** Plaintext offset of the first entry after the manifest. */
    entries_start: number;
    plaintext_size: number;
    files_missing: number;
}

type Step = "pre_import" | "stage" | "stream" | "swap" | "finalize" | "cleanup" | "done";

interface Plan {
    table: string;
    cols: string[];
    idx: number[];
}

interface ApplyState {
    step: Step;
    since: string;
    restarts: number;
    pos: number;
    row_skip: number;
    phase: "tables" | "files";
    swapped: boolean;
    pre_import_id: string | null;
    plans: Record<string, Plan>;
    counters: {
        rows_staged: number;
        files_written: number;
        files_unchanged: number;
        files_deferred: number;
        files_replaced: number;
        orphans_removed: number;
        duplicates_removed: number;
        rows_restored: number;
        tables_restored: number;
        tables_emptied: number;
    };
    session_kept: boolean;
}

function newState(since: string, restarts = 0): ApplyState {
    return {
        step: "pre_import",
        since,
        restarts,
        pos: 0,
        row_skip: 0,
        phase: "tables",
        swapped: false,
        pre_import_id: null,
        plans: {},
        counters: {
            rows_staged: 0,
            files_written: 0,
            files_unchanged: 0,
            files_deferred: 0,
            files_replaced: 0,
            orphans_removed: 0,
            duplicates_removed: 0,
            rows_restored: 0,
            tables_restored: 0,
            tables_emptied: 0,
        },
        session_kept: false,
    };
}

const STEP_MESSAGES: Record<string, string> = {
    pre_import: "Copie de sécurité de l'état actuel…",
    tables: "Restauration des tables…",
    files: "Restauration des fichiers…",
    swap: "Remplacement des données…",
    finalize: "Remplacement des fichiers modifiés…",
    cleanup: "Finalisation…",
    done: "Restauration terminée.",
};

function publicStep(state: ApplyState): string {
    if (state.step === "stage" || state.step === "stream") return state.phase;
    return state.step;
}

function progressOf(state: ApplyState, checked: CheckedInfo | null): number {
    switch (state.step) {
        case "pre_import":
            return 0;
        case "stage":
            return 0.05;
        case "stream": {
            const total = Math.max(1, (checked?.plaintext_size ?? 1) - (checked?.entries_start ?? 0));
            const done = Math.max(0, state.pos - (checked?.entries_start ?? 0));
            return Math.round((0.05 + 0.85 * Math.min(1, done / total)) * 1000) / 1000;
        }
        case "swap":
            return 0.9;
        case "finalize":
            return 0.95;
        case "cleanup":
            return 0.98;
        default:
            return 1;
    }
}

function importJson(r: ImportRow) {
    let checked: CheckedInfo | null = null;
    let state: ApplyState | null = null;
    try {
        checked = r.checked ? (JSON.parse(r.checked) as CheckedInfo) : null;
        state = r.state ? (JSON.parse(r.state) as ApplyState) : null;
    } catch {
        // shown without them
    }
    return {
        id: r.id,
        status: r.status,
        created_at: r.created_at,
        updated_at: r.updated_at,
        by: { id: r.created_by, username: r.current_username ?? r.created_by_username },
        size: r.size,
        sha256: r.sha256,
        part_size: r.part_size,
        parts: r.parts,
        parts_received: r.parts_received ?? 0,
        received: r.received ?? 0,
        backup: checked?.summary ?? null,
        compatible: checked ? checked.compatible : null,
        problems: checked?.problems ?? [],
        step: state ? publicStep(state) : null,
        progress: state ? progressOf(state, checked) : null,
        pre_import_backup_id: r.pre_import_id,
        error: r.error,
    };
}

function importId(ctx: Ctx): string {
    const id = ctx.params.importId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("IMPORT_NOT_FOUND", "Import introuvable.");
    return id;
}

async function loadImport(db: D1Database, id: string): Promise<ImportRow> {
    const row = await db
        .prepare(
            `SELECT i.*, p.username AS current_username,
               (SELECT COUNT(*) FROM backup_import_parts x WHERE x.import_id = i.id) AS parts_received,
               (SELECT COALESCE(SUM(size), 0) FROM backup_import_parts x WHERE x.import_id = i.id) AS received
             FROM backup_imports i LEFT JOIN players p ON p.id = i.created_by WHERE i.id = ?1`,
        )
        .bind(id)
        .first<ImportRow>();
    if (!row) throw Errors.notFound("IMPORT_NOT_FOUND", "Import introuvable.");
    return row;
}

function partSize(env: Bindings): number {
    const n = Number(env.BACKUP_IMPORT_PART_SIZE ?? DEFAULT_PART_SIZE);
    return Number.isInteger(n) && n >= 64 * 1024 && n <= DEFAULT_PART_SIZE ? n : DEFAULT_PART_SIZE;
}

function partKey(id: string, n: number): string {
    return `imports/${id}/part-${String(n).padStart(5, "0")}`;
}

const CLOSED = new Set(["done", "abandoned", "expired"]);

/** POST /admin/backups/imports {"size", "sha256"} — announces a .cpbk file to send in parts. */
export async function createImport(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["size", "sha256"]);
    const size = v.int("size", { min: 1, max: Number.MAX_SAFE_INTEGER });
    const sha = v.str("sha256", { min: 64, max: 64, pattern: /^[0-9a-fA-F]{64}$/ });
    v.done();
    if (size! > MAX_IMPORT_BYTES)
        await refuse(ctx, auth, new ApiError(413, "PAYLOAD_TOO_LARGE", "Fichier trop volumineux : 1 Gio au maximum par l'API (voir la procédure manuelle de BACKUP.md)."), {
            action: "import",
            size,
        });
    if (size! < fmt.HEADER_SIZE + fmt.RECORD_OVERHEAD + 1)
        await refuse(ctx, auth, new ApiError(400, "BACKUP_INVALID", "Fichier trop petit pour être une sauvegarde Cosmic Online."), { action: "import", size });
    const open = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM backup_imports WHERE status IN ('uploading', 'checked', 'applying', 'failed')").first<{ n: number }>();
    if ((open?.n ?? 0) >= MAX_OPEN_IMPORTS)
        await refuse(ctx, auth, Errors.conflict("TOO_MANY_IMPORTS", "Trop d'imports en attente : abandonnez-en un d'abord."), { action: "import" });
    const id = crypto.randomUUID();
    const ps = partSize(ctx.env);
    const parts = Math.ceil(size! / ps);
    const now = nowIso();
    await ctx.env.DB.prepare(
        `INSERT INTO backup_imports (id, created_by, created_by_username, created_at, updated_at, size, sha256, part_size, parts, status)
         VALUES (?1, ?2, ?3, ?4, ?4, ?5, ?6, ?7, ?8, 'uploading')`,
    )
        .bind(id, auth.player.id, auth.player.username, now, size!, sha!.toLowerCase(), ps, parts)
        .run();
    await audit(ctx, auth.player.id, "backup_import_created", { import_id: id, size, by_username: auth.player.username });
    return json(ctx, { import_id: id, part_size: ps, parts, max_size: MAX_IMPORT_BYTES }, { status: 201 });
}

/** PUT /admin/backups/imports/{importId}/parts/{n} — raw bytes of part n (from 0). */
export async function putImportPart(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.backupUpload, "player:" + auth.player.id);
    const id = importId(ctx);
    const imp = await loadImport(ctx.env.DB, id);
    if (imp.status !== "uploading") throw Errors.conflict("IMPORT_NOT_UPLOADING", "Cet import ne reçoit plus de parties.");
    const raw = ctx.params.n ?? "";
    const n = /^\d{1,6}$/.test(raw) ? Number(raw) : -1;
    if (n < 0 || n >= imp.parts) throw new ApiError(400, "INVALID_PART", `Numéro de partie invalide (de 0 à ${imp.parts - 1}).`);
    if (!(ctx.request.headers.get("content-type") ?? "").toLowerCase().startsWith("application/octet-stream"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Les parties s'envoient en application/octet-stream.");
    const expected = n < imp.parts - 1 ? imp.part_size : imp.size - imp.part_size * (imp.parts - 1);
    const data = await readLimited(ctx.request, expected);
    if (data.byteLength !== expected) throw new ApiError(400, "PART_SIZE", `La partie ${n} doit faire ${expected} octets (reçu ${data.byteLength}).`);
    await ctx.env.STORAGE.put(partKey(id, n), data, { httpMetadata: { contentType: "application/octet-stream" } });
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("INSERT INTO backup_import_parts (import_id, n, size) VALUES (?1, ?2, ?3) ON CONFLICT (import_id, n) DO UPDATE SET size = excluded.size").bind(
            id,
            n,
            data.byteLength,
        ),
        ctx.env.DB.prepare("UPDATE backup_imports SET updated_at = ?2, sha256_verified = 0 WHERE id = ?1").bind(id, nowIso()),
    ]);
    const got = await ctx.env.DB.prepare("SELECT COUNT(*) AS n, COALESCE(SUM(size), 0) AS b FROM backup_import_parts WHERE import_id = ?1")
        .bind(id)
        .first<{ n: number; b: number }>();
    return json(ctx, { received: got?.b ?? 0, parts_received: got?.n ?? 0, parts: imp.parts, complete: (got?.n ?? 0) === imp.parts });
}

/** GET /admin/backups/imports/{importId} — where an import stands. */
export async function getImport(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    return json(ctx, { import: importJson(await loadImport(ctx.env.DB, importId(ctx))) });
}

/** The uploaded file in R2, read record by record. */
class ImportFile {
    constructor(
        private readonly env: Bindings,
        private readonly imp: ImportRow,
    ) {}

    async readHeader(): Promise<fmt.CpbkHeader> {
        const o = await this.env.STORAGE.get(partKey(this.imp.id, 0), { range: { offset: 0, length: fmt.HEADER_SIZE } });
        if (!o) throw new Error("part 0 missing");
        return fmt.parseHeader(new Uint8Array(await o.arrayBuffer()));
    }

    /**
     * Records from `from` on. `fullParts`: every part is read whole and given to `onPart` in
     * order (whole-file SHA-256); otherwise reading starts at the first record needed.
     */
    async *records(
        from: number,
        layout: fmt.RecordLayout,
        chunkSize: number,
        opts: { fullParts: boolean; onPart?: (bytes: Uint8Array) => Promise<void> },
    ): AsyncGenerator<{ index: number; last: boolean; record: Uint8Array }> {
        const ps = this.imp.part_size;
        const firstOffset = fmt.recordOffset(from, chunkSize);
        let nextPart = opts.fullParts ? 0 : Math.floor(firstOffset / ps);
        let bufStart = opts.fullParts ? 0 : firstOffset;
        let partialFirst = !opts.fullParts;
        let buf = new Uint8Array(0);
        const load = async () => {
            if (nextPart >= this.imp.parts) throw new fmt.CpbkError("corrupted", "fin de fichier inattendue", layout.count - 1);
            const key = partKey(this.imp.id, nextPart);
            const o = partialFirst ? await this.env.STORAGE.get(key, { range: { offset: bufStart - nextPart * ps } }) : await this.env.STORAGE.get(key);
            if (!o) throw new Error(`part ${nextPart} missing`);
            partialFirst = false;
            const bytes = new Uint8Array(await o.arrayBuffer());
            if (opts.onPart) await opts.onPart(bytes);
            nextPart += 1;
            const merged = new Uint8Array(buf.byteLength + bytes.byteLength);
            merged.set(buf, 0);
            merged.set(bytes, buf.byteLength);
            buf = merged;
        };
        for (let i = from; i < layout.count; ++i) {
            const off = fmt.recordOffset(i, chunkSize);
            const last = i === layout.count - 1;
            const len = last ? layout.lastSize : fmt.recordSize(chunkSize);
            while (bufStart + buf.byteLength < off + len) await load();
            yield { index: i, last, record: buf.subarray(off - bufStart, off - bufStart + len) };
            buf = buf.subarray(off + len - bufStart);
            bufStart = off + len;
        }
        if (opts.fullParts) while (nextPart < this.imp.parts) await load();
    }
}

const TABLE_PART_RE = /^d1\/([A-Za-z0-9_]{1,64})\/(\d{4})\.jsonl$/;
const FILE_LIST_RE = /^files\/\d{4}\.jsonl$/;
const NAME_RE = /^[A-Za-z0-9_]{1,64}$/;

function validFileKey(key: string): boolean {
    if (key.length === 0 || encoder.encode(key).byteLength > 1024 || key.startsWith("/")) return false;
    // eslint-disable-next-line no-control-regex
    if (/[\u0000-\u001f\u007f]/.test(key)) return false;
    return !EXCLUDED_R2_PREFIXES.some((p) => key.startsWith(p));
}

function invalid(message: string): fmt.CpbkError {
    return new fmt.CpbkError("format", message);
}

function parseManifest(data: Uint8Array): StoredManifest {
    let m: StoredManifest;
    try {
        m = JSON.parse(strictUtf8.decode(data)) as StoredManifest;
    } catch {
        throw invalid("manifeste illisible");
    }
    if (!m || typeof m !== "object" || m.format !== "cosmic-backup") throw invalid("manifeste inconnu");
    if (m.format_version !== 1) throw invalid(`version de manifeste ${String(m.format_version)} inconnue`);
    if (typeof m.created_at !== "string" || (m.schema !== null && typeof m.schema !== "string")) throw invalid("manifeste incomplet");
    if (!Array.isArray(m.tables) || !m.files || !Array.isArray(m.files.lists)) throw invalid("manifeste incomplet");
    const names = new Set<string>();
    for (const t of m.tables) {
        if (!t || typeof t.name !== "string" || !NAME_RE.test(t.name) || names.has(t.name.toLowerCase())) throw invalid("table invalide dans le manifeste");
        names.add(t.name.toLowerCase());
        if (!Array.isArray(t.columns) || t.columns.length === 0 || !t.columns.every((c) => typeof c === "string" && NAME_RE.test(c)))
            throw invalid(`colonnes invalides pour la table ${t.name}`);
        if (new Set(t.columns.map((c) => c.toLowerCase())).size !== t.columns.length) throw invalid(`colonnes en double dans la table ${t.name}`);
        if (!Number.isSafeInteger(t.rows) || t.rows < 0 || !Array.isArray(t.parts)) throw invalid(`table ${t.name} incomplète`);
        for (const p of t.parts) {
            const pm = typeof p === "string" ? TABLE_PART_RE.exec(p) : null;
            if (!pm || pm[1] !== t.name) throw invalid(`partie invalide pour la table ${t.name}`);
        }
    }
    if (!m.files.lists.every((l) => typeof l === "string" && FILE_LIST_RE.test(l))) throw invalid("liste de fichiers invalide");
    if (!Number.isSafeInteger(m.files.count) || m.files.count < 0) throw invalid("nombre de fichiers invalide");
    if (m.migrations !== undefined && !(Array.isArray(m.migrations) && m.migrations.every((x) => typeof x === "string"))) throw invalid("migrations invalides");
    return m;
}

/** Compatibility of a backup with the current schema: blocking problems, then warnings (« Attention : »). */
function compatibility(m: StoredManifest, schema: Schema): { problems: string[]; warnings: string[]; plans: Record<string, Plan> } {
    const problems: string[] = [];
    const warnings: string[] = [];
    const plans: Record<string, Plan> = {};
    const cur = schema.version;
    if (!m.schema) problems.push("La sauvegarde n'indique pas la version de son schéma : elle ne peut pas être importée.");
    else if (m.schema !== cur) {
        const idx = schema.migrations.indexOf(m.schema);
        if (idx < 0) {
            if (cur && (m.migrations ?? []).includes(cur))
                problems.push(
                    `La sauvegarde vient d'un serveur plus récent (schéma ${m.schema}) que celui-ci (schéma ${cur}) : mettez d'abord ce serveur à jour.`,
                );
            else problems.push(`Le schéma de la sauvegarde (${m.schema}) est inconnu de ce serveur (schéma ${cur ?? "inconnu"}) : elle ne peut pas être importée.`);
        } else {
            const later = schema.migrations.slice(idx + 1);
            warnings.push(
                `Attention : la sauvegarde date d'un schéma plus ancien (${m.schema}) ; les ${later.length} migration(s) suivante(s) ne seront pas rejouées sur ses données (${later.slice(0, 4).join(", ")}${later.length > 4 ? "…" : ""}) : les colonnes ajoutées depuis prendront leur valeur par défaut.`,
            );
        }
    }
    const restorable = new Map(schema.restorable.map((n) => [n, schema.tables.get(n)!]));
    const inBackup = new Set<string>();
    for (const t of m.tables) {
        const table = findCi(restorable, t.name);
        if (!table) {
            if (findCi(schema.tables, t.name)) problems.push(`La table « ${t.name} » ne fait jamais partie d'une sauvegarde : ce fichier n'a pas été produit par ce serveur.`);
            else problems.push(`La table « ${t.name} » de la sauvegarde n'existe pas sur ce serveur : la sauvegarde vient d'une autre version du serveur.`);
            continue;
        }
        inBackup.add(table.name);
        const curCols = new Map(storedColumns(table).map((c) => [c.name, c]));
        const cols: string[] = [];
        const idx: number[] = [];
        t.columns.forEach((c, i) => {
            const col = findCi(curCols, c);
            if (col) {
                cols.push(col.name);
                idx.push(i);
            } else warnings.push(`Attention : la colonne « ${t.name}.${c} » n'existe plus sur ce serveur : ses valeurs seront ignorées.`);
        });
        const have = new Set(cols.map((c) => c.toLowerCase()));
        for (const c of storedColumns(table))
            if (!have.has(c.name.toLowerCase()) && c.notnull && c.dflt === null && !isRowidAlias(table, c))
                problems.push(`La colonne obligatoire « ${table.name}.${c.name} » manque dans la sauvegarde et n'a pas de valeur par défaut.`);
        if (cols.length === 0) problems.push(`La table « ${t.name} » n'a aucune colonne commune avec ce serveur.`);
        else plans[t.name] = { table: table.name, cols, idx };
    }
    for (const name of schema.restorable)
        if (!inBackup.has(name)) warnings.push(`Attention : la table « ${name} » n'existe pas dans la sauvegarde : elle sera vidée.`);
    return { problems, warnings, plans };
}

/** Streaming validation of the decrypted content (check). */
class CheckValidator {
    private reader = new fmt.EntryReader(0);
    private stage = 0; // 0 manifest, 1 tables, 2 lists, 3 files, 4 after the end
    manifest: StoredManifest | null = null;
    entriesStart = 0;
    plaintextSize = 0;
    problems: string[] = [];
    warnings: string[] = [];
    private rows = new Map<string, number>();
    private seenParts = new Set<string>();
    private expected = new Map<string, string>();
    private seenLists = 0;
    private seenFiles = new Set<string>();
    private filesBytes = 0;
    private end: { files_missing_count: number; files_changed_count: number; files_included: number; files_missing: string[] } | null = null;
    private admins: string[] = [];

    constructor(
        private readonly schema: Schema,
        private readonly requester: string,
    ) {}

    async push(plaintext: Uint8Array, last: boolean): Promise<void> {
        this.plaintextSize += plaintext.byteLength;
        for (const e of this.reader.push(plaintext)) await this.entry(e);
        if (last) {
            if (this.reader.partial) throw invalid("dernière entrée incomplète");
            if (this.stage !== 4) throw invalid("fin de sauvegarde absente");
        }
    }

    private async entry(e: fmt.Entry): Promise<void> {
        if (this.stage === 4) throw invalid("données après la fin de la sauvegarde");
        if (!(await fmt.entryIntact(e))) throw invalid(`empreinte incorrecte pour « ${e.name} »`);
        switch (e.kind) {
            case fmt.ENTRY_MANIFEST: {
                if (this.stage !== 0 || e.name !== "manifest.json") throw invalid("manifeste mal placé");
                this.manifest = parseManifest(e.data);
                this.entriesStart = e.end;
                const c = compatibility(this.manifest, this.schema);
                this.problems.push(...c.problems);
                this.warnings.push(...c.warnings);
                this.stage = 1;
                return;
            }
            case fmt.ENTRY_TABLE: {
                if (this.stage !== 1) throw invalid("table mal placée");
                const pm = TABLE_PART_RE.exec(e.name);
                const t = pm ? this.manifest!.tables.find((x) => x.name === pm[1]) : undefined;
                if (!t || !t.parts.includes(e.name) || this.seenParts.has(e.name)) throw invalid(`partie de table inattendue : ${e.name}`);
                this.seenParts.add(e.name);
                let text: string;
                try {
                    text = strictUtf8.decode(e.data);
                } catch {
                    throw invalid(`texte invalide dans ${e.name}`);
                }
                const isPlayers = t.name.toLowerCase() === "players";
                const iId = t.columns.indexOf("id");
                const iRole = t.columns.indexOf("role");
                const iStatus = t.columns.indexOf("account_status");
                let n = 0;
                for (const line of text.split("\n")) {
                    if (line.length === 0) continue;
                    let row: unknown;
                    try {
                        row = JSON.parse(line);
                    } catch {
                        throw invalid(`ligne illisible dans ${e.name}`);
                    }
                    if (!Array.isArray(row) || row.length !== t.columns.length) throw invalid(`ligne de ${row instanceof Array ? row.length : 0} valeurs dans ${e.name}`);
                    for (const v of row)
                        if (!(v === null || typeof v === "string" || (typeof v === "number" && Number.isFinite(v)))) throw invalid(`valeur invalide dans ${e.name}`);
                    if (isPlayers && iId >= 0 && iRole >= 0 && row[iRole] === "admin" && (iStatus < 0 || row[iStatus] === "active")) this.admins.push(String(row[iId]));
                    n += 1;
                }
                this.rows.set(t.name, (this.rows.get(t.name) ?? 0) + n);
                return;
            }
            case fmt.ENTRY_FILE_LIST: {
                if (this.stage > 2 || this.stage < 1) throw invalid("liste de fichiers mal placée");
                this.stage = 2;
                if (!this.manifest!.files.lists.includes(e.name)) throw invalid(`liste de fichiers inattendue : ${e.name}`);
                this.seenLists += 1;
                let text: string;
                try {
                    text = strictUtf8.decode(e.data);
                } catch {
                    throw invalid(`texte invalide dans ${e.name}`);
                }
                for (const line of text.split("\n")) {
                    if (!line) continue;
                    let r: FileRecord;
                    try {
                        r = JSON.parse(line) as FileRecord;
                    } catch {
                        throw invalid(`ligne illisible dans ${e.name}`);
                    }
                    if (!r || typeof r.key !== "string" || !validFileKey(r.key) || typeof r.sha256 !== "string")
                        throw invalid(`fichier invalide dans la liste ${e.name}`);
                    this.expected.set(r.key, r.sha256);
                }
                return;
            }
            case fmt.ENTRY_FILE: {
                if (this.stage < 1 || this.stage > 3) throw invalid("fichier mal placé");
                this.stage = 3;
                if (!this.expected.has(e.name) || this.seenFiles.has(e.name)) throw invalid(`fichier inattendu : ${e.name}`);
                if (e.meta !== null) {
                    try {
                        const meta = JSON.parse(e.meta) as { content_type?: unknown; custom_metadata?: unknown };
                        if (meta.content_type !== null && meta.content_type !== undefined && typeof meta.content_type !== "string") throw new Error();
                        if (meta.custom_metadata !== undefined && (typeof meta.custom_metadata !== "object" || meta.custom_metadata === null)) throw new Error();
                    } catch {
                        throw invalid(`métadonnées invalides pour ${e.name}`);
                    }
                }
                this.seenFiles.add(e.name);
                this.filesBytes += e.data.byteLength;
                return;
            }
            case fmt.ENTRY_END: {
                if (this.stage < 1 || e.name !== "end.json") throw invalid("fin mal placée");
                try {
                    this.end = JSON.parse(strictUtf8.decode(e.data)) as CheckValidator["end"];
                } catch {
                    throw invalid("fin de sauvegarde illisible");
                }
                if (!this.end || !Number.isSafeInteger(this.end.files_missing_count) || !Number.isSafeInteger(this.end.files_changed_count))
                    throw invalid("fin de sauvegarde incomplète");
                this.stage = 4;
                return;
            }
            default:
                throw invalid("entrée de type inconnu");
        }
    }

    finish(): CheckedInfo {
        const m = this.manifest!;
        for (const t of m.tables) {
            if (t.parts.some((p) => !this.seenParts.has(p))) throw invalid(`des parties de la table ${t.name} manquent`);
            if ((this.rows.get(t.name) ?? 0) !== t.rows) throw invalid(`la table ${t.name} contient ${this.rows.get(t.name) ?? 0} lignes au lieu de ${t.rows}`);
        }
        if (this.seenLists !== m.files.lists.length || this.expected.size !== m.files.count) throw invalid("liste des fichiers incomplète");
        const end = this.end!;
        if (this.seenFiles.size + end.files_missing_count !== this.expected.size) throw invalid("des fichiers manquent dans la sauvegarde");
        if (end.files_missing_count > 0)
            this.warnings.push(
                `Attention : ${end.files_missing_count} fichier(s) (replays, avatars, niveaux…) n'existaient plus sur le serveur au moment du téléchargement : ils ne seront pas restaurés.`,
            );
        if (end.files_changed_count > 0)
            this.warnings.push(
                `Attention : ${end.files_changed_count} fichier(s) ont changé entre la copie et son téléchargement : leur version la plus récente sera restaurée.`,
            );
        if (this.manifest!.tables.some((t) => t.name.toLowerCase() === "players")) {
            if (this.admins.length === 0)
                this.warnings.push(
                    "Attention : la sauvegarde ne contient aucun compte admin actif : après l'import, plus personne ne pourra ouvrir l'écran ADMINISTRATION (il faudra en nommer un avec wrangler).",
                );
            else if (!this.admins.includes(this.requester))
                this.warnings.push("Attention : votre compte n'est pas admin dans cette sauvegarde : vous serez déconnecté à la fin de l'import.");
        }
        const tables: Record<string, string[]> = {};
        for (const t of m.tables) tables[t.name] = t.columns;
        const rows = m.tables.reduce((n, t) => n + t.rows, 0);
        return {
            summary: {
                id: typeof m.backup_id === "string" ? m.backup_id : null,
                kind: typeof m.kind === "string" ? m.kind : null,
                created_at: m.created_at,
                schema: m.schema,
                environment: typeof m.environment === "string" ? m.environment : null,
                tables: m.tables.length,
                rows,
                files: this.seenFiles.size,
                bytes: this.plaintextSize,
            },
            tables,
            compatible: this.problems.length === 0,
            problems: [...this.problems, ...this.warnings],
            entries_start: this.entriesStart,
            plaintext_size: this.plaintextSize,
            files_missing: end.files_missing_count,
        };
    }
}

function cryptoError(e: fmt.CpbkError): ApiError {
    if (e.kind === "corrupted")
        return new ApiError(400, "BACKUP_CORRUPTED", `Fichier de sauvegarde altéré ou tronqué (bloc ${e.chunk ?? 0}) : il ne peut pas être importé.`);
    return new ApiError(400, "BACKUP_INVALID", `Ce fichier n'est pas une sauvegarde Cosmic Online valide : ${e.message}.`);
}

const WRONG_PASSPHRASE = () => new ApiError(400, "WRONG_PASSPHRASE", "Phrase secrète incorrecte : impossible de déchiffrer ce fichier.");

/** Reads the whole file once: SHA-256 of what was received, decryption and validation of every entry. */
async function runCheck(env: Bindings, imp: ImportRow, passphrase: string, requester: string): Promise<CheckedInfo> {
    const file = new ImportFile(env, imp);
    let header: fmt.CpbkHeader;
    let layout: fmt.RecordLayout;
    try {
        header = await file.readHeader();
        layout = fmt.recordLayout(imp.size, header.chunkSize);
    } catch (e) {
        if (e instanceof fmt.CpbkError) throw cryptoError(e);
        throw e;
    }
    const keys = await fmt.deriveKeys(passphrase, header.salt, header.iterations);
    const keyOk = fmt.keyCheckMatches(header, keys);
    if (!keyOk && imp.sha256_verified) throw WRONG_PASSPHRASE();
    const validator = keyOk ? new CheckValidator(await readSchema(env.DB), requester) : null;
    const ds = new crypto.DigestStream("SHA-256");
    const writer = ds.getWriter();
    let failure: fmt.CpbkError | null = null;
    for await (const rec of file.records(0, layout, header.chunkSize, { fullParts: true, onPart: (b) => writer.write(b) })) {
        if (failure || !validator) continue; // keep reading: the SHA-256 tells a bad upload from a bad file
        try {
            const pt = await fmt.decryptChunk(keys.key, header.bytes, rec.index, rec.last, rec.record);
            await validator.push(pt, rec.last);
        } catch (e) {
            if (e instanceof fmt.CpbkError) failure = e;
            else throw e;
        }
    }
    await writer.close();
    const sha = toHex(new Uint8Array(await ds.digest));
    if (sha !== imp.sha256)
        throw new ApiError(400, "IMPORT_SHA256_MISMATCH", "Le fichier reçu ne correspond pas à l'empreinte SHA-256 annoncée : envoyez-le de nouveau.");
    await env.DB.prepare("UPDATE backup_imports SET sha256_verified = 1 WHERE id = ?1").bind(imp.id).run();
    if (!keyOk) throw WRONG_PASSPHRASE();
    if (failure) throw cryptoError(failure);
    try {
        return validator!.finish();
    } catch (e) {
        if (e instanceof fmt.CpbkError) throw cryptoError(e);
        throw e;
    }
}

/** POST /admin/backups/imports/{importId}/check {"passphrase"} — decrypts and validates, changes nothing. */
export async function checkImport(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.backupCheck, "player:" + auth.player.id);
    const id = importId(ctx);
    const body = await readJsonObject(ctx);
    new Validator(body, ["passphrase"]).done();
    const where = { action: "check", import_id: id };
    let passphrase: string;
    try {
        passphrase = passphraseOf(body);
    } catch (e) {
        return refuse(ctx, auth, e as ApiError, where);
    }
    const imp = await loadImport(ctx.env.DB, id);
    if (CLOSED.has(imp.status)) await refuse(ctx, auth, Errors.conflict("IMPORT_CLOSED", "Cet import est terminé ou abandonné."), where);
    if (imp.status === "applying") await refuse(ctx, auth, Errors.conflict("IMPORT_BUSY", "Cet import est en cours d'application."), where);
    if ((imp.parts_received ?? 0) !== imp.parts || (imp.received ?? 0) !== imp.size)
        await refuse(ctx, auth, Errors.conflict("IMPORT_INCOMPLETE", `Envoi incomplet : ${imp.parts_received ?? 0} partie(s) reçue(s) sur ${imp.parts}.`), where);
    let info: CheckedInfo;
    try {
        info = await runCheck(ctx.env, imp, passphrase, auth.player.id);
    } catch (e) {
        if (e instanceof ApiError && e.status < 500) return refuse(ctx, auth, e, where);
        throw e;
    }
    await ctx.env.DB.prepare(
        "UPDATE backup_imports SET status = CASE WHEN status = 'applying' THEN status ELSE 'checked' END, checked = ?2, compatible = ?3, updated_at = ?4, error = NULL WHERE id = ?1",
    )
        .bind(id, JSON.stringify(info), info.compatible ? 1 : 0, nowIso())
        .run();
    await audit(ctx, auth.player.id, "backup_import_checked", {
        import_id: id,
        compatible: info.compatible,
        schema: info.summary.schema,
        rows: info.summary.rows,
        files: info.summary.files,
        problems: info.problems.length,
        by_username: auth.player.username,
    });
    return json(ctx, { backup: info.summary, compatible: info.compatible, problems: info.problems });
}

// ------------------------------------------------------------------ apply

interface ApplyContext {
    ctx: Ctx;
    env: Bindings;
    db: D1Database;
    imp: ImportRow;
    checked: CheckedInfo;
    state: ApplyState;
    actor: Actor | null;
    sessionId: string | null;
    deadline: number;
    ops: { r2: number; d1: number };
}

function stateStatement(a: ApplyContext): D1PreparedStatement {
    return a.db.prepare("UPDATE backup_imports SET state = ?2, pre_import_id = ?3, updated_at = ?4 WHERE id = ?1").bind(
        a.imp.id,
        JSON.stringify(a.state),
        a.state.pre_import_id,
        nowIso(),
    );
}

async function saveState(a: ApplyContext): Promise<void> {
    await stateStatement(a).run();
    a.ops.d1 += 1;
}

function overBudget(a: ApplyContext): boolean {
    return Date.now() > a.deadline || a.ops.r2 > APPLY_MAX_R2_OPS || a.ops.d1 > APPLY_MAX_D1_OPS;
}

async function scratchTables(db: D1Database): Promise<string[]> {
    const rows = await db
        .prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND (name LIKE 'backup\\_stage\\_\\_%' ESCAPE '\\' OR name LIKE 'backup\\_keep\\_\\_%' ESCAPE '\\')")
        .all<{ name: string }>();
    return rows.results.map((r) => r.name);
}

async function dropScratchTables(db: D1Database): Promise<void> {
    const names = await scratchTables(db);
    if (names.length) await db.batch(names.map((n) => db.prepare(`DROP TABLE IF EXISTS ${q(n)}`)));
}

/** Temporary tables holding the rows of the file until the swap (current columns that the file has). */
async function prepareStage(a: ApplyContext, plans: Record<string, Plan>): Promise<void> {
    await dropScratchTables(a.db);
    // Objects kept aside by an earlier attempt of this import.
    await deletePrefix(a.env, `imports/${a.imp.id}/swap/`);
    const schema = await readSchema(a.db);
    const stmts: D1PreparedStatement[] = [];
    for (const plan of Object.values(plans)) stmts.push(a.db.prepare(`CREATE TABLE ${q(STAGE_PREFIX + plan.table)} (${plan.cols.map(q).join(", ")})`));
    stmts.push(a.db.prepare(`CREATE TABLE ${q(KEEP_AUDIT)} (player_id, action, ip_hash, details, created_at)`));
    if (schema.tables.has("player_sessions")) stmts.push(a.db.prepare(`CREATE TABLE ${q(KEEP_SESSION)} AS SELECT * FROM player_sessions WHERE 0`));
    await a.db.batch(stmts);
    a.ops.d1 += 3;
    a.state.plans = plans;
    a.state.pos = a.checked.entries_start;
    a.state.row_skip = 0;
    a.state.step = "stream";
    await saveState(a);
}

function stageSql(plan: Plan): string {
    return `INSERT INTO ${q(STAGE_PREFIX + plan.table)} (${plan.cols.map(q).join(", ")}) SELECT ${plan.idx
        .map((i) => `json_extract(value, '$[${i}]')`)
        .join(", ")} FROM json_each(?1)`;
}

/** Rows of one table part into its stage table; the position in the file is saved in the same transaction. */
async function stageEntry(a: ApplyContext, e: fmt.Entry): Promise<void> {
    const pm = TABLE_PART_RE.exec(e.name);
    const plan = pm ? a.state.plans[pm[1]!] : undefined;
    if (!plan) {
        a.state.pos = e.end;
        a.state.row_skip = 0;
        return;
    }
    const lines = strictUtf8.decode(e.data).split("\n").filter((l) => l.length > 0);
    const sql = stageSql(plan);
    let i = a.state.pos === e.start ? a.state.row_skip : 0;
    if (i >= lines.length) {
        a.state.pos = e.end;
        a.state.row_skip = 0;
        await saveState(a);
        return;
    }
    while (i < lines.length) {
        const stmts: D1PreparedStatement[] = [];
        let rows = 0;
        while (i < lines.length && stmts.length < STAGE_STATEMENTS_PER_BATCH) {
            const chunk: string[] = [];
            let size = 2;
            while (i < lines.length && (chunk.length === 0 || size + lines[i]!.length + 1 <= STAGE_PARAM_BYTES)) {
                chunk.push(lines[i]!);
                size += lines[i]!.length + 1;
                i += 1;
            }
            stmts.push(a.db.prepare(sql).bind("[" + chunk.join(",") + "]"));
            rows += chunk.length;
        }
        const finished = i >= lines.length;
        a.state.pos = finished ? e.end : e.start;
        a.state.row_skip = finished ? 0 : i;
        a.state.counters.rows_staged += rows;
        stmts.push(stateStatement(a));
        await a.db.batch(stmts);
        a.ops.d1 += 1;
    }
}

interface FileMeta {
    content_type?: string | null;
    custom_metadata?: Record<string, string>;
}

function putOptions(meta: FileMeta): R2PutOptions {
    const custom: Record<string, string> = {};
    for (const [k, v] of Object.entries(meta.custom_metadata ?? {})) if (typeof v === "string") custom[k] = v;
    return { httpMetadata: meta.content_type ? { contentType: meta.content_type } : undefined, customMetadata: custom };
}

/**
 * One file of the backup: written when absent, left alone when identical; an existing object
 * with other content is only replaced after the swap (kept aside until then).
 */
async function restoreFile(a: ApplyContext, e: fmt.Entry): Promise<void> {
    const key = e.name;
    if (!validFileKey(key)) throw new Error("invalid file key");
    if (!(await fmt.entryIntact(e))) throw new fmt.CpbkError("corrupted", "fichier altéré", 0);
    const meta = (e.meta ? JSON.parse(e.meta) : {}) as FileMeta;
    const head = await a.env.STORAGE.head(key);
    a.ops.r2 += 1;
    if (!head) {
        await a.env.STORAGE.put(key, e.data, putOptions(meta));
        a.ops.r2 += 1;
        a.state.counters.files_written += 1;
        return;
    }
    if (head.size === e.data.byteLength) {
        const want = toHex(e.sha256);
        let same = hexOf(head.checksums?.sha256) === want;
        if (!same) {
            const h = await hashObject(a.env, key);
            a.ops.r2 += 1;
            same = h?.sha256 === want;
        }
        if (same) {
            a.state.counters.files_unchanged += 1;
            return;
        }
    }
    await a.env.STORAGE.put(`imports/${a.imp.id}/swap/${key}`, e.data, putOptions(meta));
    a.ops.r2 += 1;
    a.state.counters.files_deferred += 1;
}

/** Applies the entries from the saved position until the end entry or the budget. True at the end. */
async function streamEntries(a: ApplyContext, file: ImportFile, header: fmt.CpbkHeader, keys: fmt.CpbkKeys): Promise<boolean> {
    const chunk = header.chunkSize;
    const layout = fmt.recordLayout(a.imp.size, chunk);
    const startRecord = Math.floor(a.state.pos / chunk);
    let skip = a.state.pos - startRecord * chunk;
    const reader = new fmt.EntryReader(a.state.pos);
    let group: fmt.Entry[] = [];
    let groupsSinceSave = 0;
    const flush = async () => {
        if (group.length === 0) return;
        const g = group;
        group = [];
        await Promise.all(g.map((e) => restoreFile(a, e)));
        a.state.pos = g[g.length - 1]!.end;
        a.state.row_skip = 0;
        groupsSinceSave += 1;
        if (groupsSinceSave >= 8) {
            groupsSinceSave = 0;
            await saveState(a);
        }
    };
    for await (const rec of file.records(startRecord, layout, chunk, { fullParts: false })) {
        a.ops.r2 += 1;
        let pt = await fmt.decryptChunk(keys.key, header.bytes, rec.index, rec.last, rec.record);
        if (skip) {
            pt = pt.subarray(skip);
            skip = 0;
        }
        for (const e of reader.push(pt)) {
            if (overBudget(a)) {
                await flush();
                await saveState(a);
                return false;
            }
            switch (e.kind) {
                case fmt.ENTRY_TABLE:
                    await flush();
                    await stageEntry(a, e);
                    break;
                case fmt.ENTRY_FILE:
                    a.state.phase = "files";
                    group.push(e);
                    if (group.length >= FILE_GROUP) await flush();
                    break;
                case fmt.ENTRY_END:
                    await flush();
                    a.state.pos = e.end;
                    a.state.step = "swap";
                    await saveState(a);
                    return true;
                default:
                    // the manifest (already read by the check) and the file lists
                    await flush();
                    if (e.kind === fmt.ENTRY_FILE_LIST) a.state.phase = "files";
                    a.state.pos = e.end;
                    a.state.row_skip = 0;
            }
        }
    }
    await flush();
    throw new Error("end entry not reached");
}

/** Orphan rows (a parent missing from the file) are removed or their link set to NULL, as SQLite would do. */
async function cleanStage(a: ApplyContext, schema: Schema): Promise<void> {
    const staged = new Map<string, Set<string>>();
    for (const p of Object.values(a.state.plans)) staged.set(p.table, new Set(p.cols.map((c) => c.toLowerCase())));
    const restorable = new Set(schema.restorable);
    // Duplicate primary keys (a row moved while the copy ran): the last one wins.
    const dedupe: D1PreparedStatement[] = [];
    for (const [table, cols] of staged) {
        const pk = pkColumns(schema.tables.get(table)!);
        if (pk.length && pk.every((c) => cols.has(c.toLowerCase())))
            dedupe.push(
                a.db.prepare(
                    `DELETE FROM ${q(STAGE_PREFIX + table)} WHERE rowid NOT IN (SELECT MAX(rowid) FROM ${q(STAGE_PREFIX + table)} GROUP BY ${pk.map(q).join(", ")})`,
                ),
            );
    }
    if (dedupe.length) {
        const res = await a.db.batch(dedupe);
        a.ops.d1 += 1;
        a.state.counters.duplicates_removed += res.reduce((n, r) => n + (r.meta.changes ?? 0), 0);
    }
    const stmts: D1PreparedStatement[] = [];
    for (const table of schema.restorable) {
        const cols = staged.get(table);
        if (!cols) continue;
        const t = schema.tables.get(table)!;
        const stage = q(STAGE_PREFIX + table);
        for (const fk of t.fks) {
            const parentName = [...restorable].find((n) => n.toLowerCase() === fk.parent.toLowerCase());
            if (!parentName) continue; // a table the import does not replace: checked by SQLite at the swap
            if (!fk.from.every((c) => cols.has(c.toLowerCase()))) continue;
            const parent = schema.tables.get(parentName)!;
            const refs = fk.to.map((r, i) => r ?? pkColumns(parent)[i] ?? "rowid");
            const parentCols = staged.get(parentName);
            const nonNull = fk.from.map((c) => `${stage}.${q(c)} IS NOT NULL`).join(" AND ");
            const orphan =
                parentCols && refs.every((r) => parentCols.has(r.toLowerCase()))
                    ? `NOT EXISTS (SELECT 1 FROM ${q(STAGE_PREFIX + parentName)} AS par WHERE ${refs.map((r, i) => `par.${q(r)} = ${stage}.${q(fk.from[i]!)}`).join(" AND ")})`
                    : "1"; // the parent table will be empty
            if (fk.onDelete === "SET NULL")
                stmts.push(a.db.prepare(`UPDATE ${stage} SET ${fk.from.map((c) => `${q(c)} = NULL`).join(", ")} WHERE ${nonNull} AND ${orphan}`));
            else stmts.push(a.db.prepare(`DELETE FROM ${stage} WHERE ${nonNull} AND ${orphan}`));
        }
    }
    // A removed row can orphan its own children (and replies of replies): until nothing changes.
    for (let pass = 0; pass < 25 && stmts.length; ++pass) {
        let changed = 0;
        for (let i = 0; i < stmts.length; i += 50) {
            const res = await a.db.batch(stmts.slice(i, i + 50));
            a.ops.d1 += 1;
            changed += res.reduce((n, r) => n + (r.meta.changes ?? 0), 0);
        }
        a.state.counters.orphans_removed += changed;
        if (changed === 0) break;
    }
}

/** Every table replaced in ONE D1 transaction (nothing changes if it fails). */
async function swap(a: ApplyContext): Promise<void> {
    const schema = await readSchema(a.db);
    await cleanStage(a, schema);
    const staged = new Map(Object.values(a.state.plans).map((p) => [p.table, p]));
    const counts = staged.size
        ? await a.db.batch([...staged.keys()].map((t) => a.db.prepare(`SELECT COUNT(*) AS n FROM ${q(STAGE_PREFIX + t)}`)))
        : [];
    a.ops.d1 += 1;
    a.state.counters.rows_restored = counts.reduce((n, r) => n + (((r.results[0] as { n?: number } | undefined)?.n) ?? 0), 0);
    a.state.counters.tables_restored = staged.size;
    a.state.counters.tables_emptied = schema.restorable.filter((t) => !staged.has(t)).length;
    const db = a.db;
    const hasAudit = schema.restorable.includes("audit_log");
    const players = schema.tables.get("players");
    const keepSession =
        a.sessionId !== null &&
        schema.tables.has("player_sessions") &&
        !!players &&
        ["role", "account_status"].every((c) => players.columns.some((x) => x.name === c)) &&
        (await scratchTables(db)).includes(KEEP_SESSION);
    const stmts: D1PreparedStatement[] = [db.prepare("PRAGMA defer_foreign_keys = ON")];
    if (hasAudit) {
        // The Journal keeps its backup entries (copies, downloads, this import) that the file does not have.
        const auditPlan = staged.get("audit_log");
        const known =
            auditPlan && ["action", "created_at", "details"].every((c) => auditPlan.cols.includes(c))
                ? `AND NOT EXISTS (SELECT 1 FROM ${q(STAGE_PREFIX + "audit_log")} s WHERE s.action = a.action AND s.created_at = a.created_at AND s.details IS a.details)`
                : "";
        stmts.push(
            db.prepare(`DELETE FROM ${q(KEEP_AUDIT)}`),
            db.prepare(
                `INSERT INTO ${q(KEEP_AUDIT)} (player_id, action, ip_hash, details, created_at)
                 SELECT a.player_id, a.action, a.ip_hash, a.details, a.created_at FROM audit_log a
                 WHERE a.action LIKE 'backup\\_%' ESCAPE '\\' ${known} ORDER BY a.created_at, a.id`,
            ),
        );
    }
    if (keepSession)
        stmts.push(
            db.prepare(`DELETE FROM ${q(KEEP_SESSION)}`),
            db.prepare(`INSERT INTO ${q(KEEP_SESSION)} SELECT * FROM player_sessions WHERE id = ?1`).bind(a.sessionId),
        );
    if (schema.tables.has("player_sessions")) stmts.push(db.prepare("DELETE FROM player_sessions"));
    for (const t of [...schema.restorable].reverse()) stmts.push(db.prepare(`DELETE FROM ${q(t)}`));
    for (const t of schema.restorable) {
        const p = staged.get(t);
        if (!p) continue;
        const cols = p.cols.map(q).join(", ");
        stmts.push(db.prepare(`INSERT INTO ${q(t)} (${cols}) SELECT ${cols} FROM ${q(STAGE_PREFIX + t)} ORDER BY rowid`));
    }
    if (hasAudit)
        stmts.push(
            db.prepare(
                `INSERT INTO audit_log (player_id, action, ip_hash, details, created_at)
                 SELECT player_id, action, ip_hash, details, created_at FROM ${q(KEEP_AUDIT)} ORDER BY rowid`,
            ),
        );
    // The administrator running the import keeps their session if the restored data still makes them an active admin.
    if (keepSession)
        stmts.push(
            db.prepare(
                `INSERT INTO player_sessions SELECT * FROM ${q(KEEP_SESSION)}
                 WHERE player_id IN (SELECT id FROM players WHERE role = 'admin' AND account_status = 'active')`,
            ),
        );
    a.state.swapped = true;
    a.state.step = "finalize";
    stmts.push(stateStatement(a));
    try {
        await db.batch(stmts);
        a.ops.d1 += 1;
    } catch (e) {
        a.state.swapped = false;
        a.state.step = "swap";
        throw e;
    }
    if (keepSession && a.sessionId) {
        const kept = await db.prepare("SELECT 1 AS x FROM player_sessions WHERE id = ?1").bind(a.sessionId).first();
        a.state.session_kept = kept !== null;
    }
}

/** Objects that existed with other content: the current one is saved in the pre_import copy, then replaced. */
async function finalizeFiles(a: ApplyContext): Promise<boolean> {
    const prefix = `imports/${a.imp.id}/swap/`;
    for (;;) {
        const l = await a.env.STORAGE.list({ prefix, limit: 100 });
        a.ops.r2 += 1;
        if (l.objects.length === 0) return true;
        for (const o of l.objects) {
            if (overBudget(a)) return false;
            const key = o.key.slice(prefix.length);
            if (a.state.pre_import_id) {
                const savedKey = `backups/${a.state.pre_import_id}/objects/${key}`;
                if (!(await a.env.STORAGE.head(savedKey))) {
                    const live = await a.env.STORAGE.get(key);
                    if (live)
                        await a.env.STORAGE.put(savedKey, await live.arrayBuffer(), { httpMetadata: live.httpMetadata, customMetadata: live.customMetadata });
                    a.ops.r2 += 2;
                }
                a.ops.r2 += 1;
            }
            const staged = await a.env.STORAGE.get(o.key);
            if (staged) await a.env.STORAGE.put(key, await staged.arrayBuffer(), { httpMetadata: staged.httpMetadata, customMetadata: staged.customMetadata });
            await a.env.STORAGE.delete(o.key);
            a.ops.r2 += 3;
            a.state.counters.files_replaced += 1;
        }
    }
}

function summaryOf(a: ApplyContext) {
    const c = a.state.counters;
    return {
        pre_import_backup_id: a.state.pre_import_id,
        backup: a.checked.summary,
        tables: c.tables_restored,
        tables_emptied: c.tables_emptied,
        rows: c.rows_restored,
        orphans_removed: c.orphans_removed,
        duplicates_removed: c.duplicates_removed,
        files_written: c.files_written,
        files_unchanged: c.files_unchanged,
        files_replaced: c.files_replaced,
        files_missing: a.checked.files_missing,
        sessions_reset: true,
        session_kept: a.state.session_kept,
        finished_at: nowIso(),
        message: a.state.session_kept
            ? "Restauration terminée. Les joueurs doivent se reconnecter."
            : "Restauration terminée. Les joueurs doivent se reconnecter (vous aussi).",
    };
}

async function cleanup(a: ApplyContext) {
    await dropScratchTables(a.db);
    await liftMaintenance(a.db, a.imp.id);
    a.state.step = "done";
    const summary = summaryOf(a);
    await a.db
        .prepare("UPDATE backup_imports SET status = 'done', state = ?2, finished_at = ?3, updated_at = ?3, lease_until = NULL, error = NULL WHERE id = ?1")
        .bind(a.imp.id, JSON.stringify(a.state), summary.finished_at)
        .run();
    await audit(a.ctx, a.actor?.id ?? null, "backup_import_finished", {
        import_id: a.imp.id,
        pre_import_id: a.state.pre_import_id,
        rows: summary.rows,
        files: summary.files_written + summary.files_replaced,
        by_username: a.actor?.username ?? null,
        by: a.actor ? undefined : "cron",
    });
    try {
        await deletePrefix(a.env, `imports/${a.imp.id}/`);
        await a.db.prepare("DELETE FROM backup_import_parts WHERE import_id = ?1").bind(a.imp.id).run();
    } catch (e) {
        log("warn", "import_parts_cleanup_failed", { import_id: a.imp.id, error: e instanceof Error ? e.message : String(e) });
    }
    return summary;
}

async function settle(since: string, env: Bindings): Promise<void> {
    const n = Number(env.BACKUP_SETTLE_MS ?? DEFAULT_SETTLE_MS);
    const settleMs = Number.isFinite(n) && n >= 0 ? Math.min(n, 30_000) : DEFAULT_SETTLE_MS;
    const wait = Date.parse(since) + settleMs - Date.now();
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
}

/** A step failed: before the swap nothing was replaced (the import can be applied again); after it, apply again to finish. */
async function failApply(a: ApplyContext, e: unknown): Promise<never> {
    const step = publicStep(a.state);
    log("error", "backup_import_failed", { import_id: a.imp.id, step, error: e instanceof Error ? e.message : String(e) });
    const pre = a.state.pre_import_id;
    const preText = pre ? ` La copie de sécurité faite juste avant (${pre}) permet de revenir à l'état précédent.` : "";
    if (!a.state.swapped) {
        try {
            await dropScratchTables(a.db);
        } catch {
            // dropped again by the next attempt
        }
        await liftMaintenance(a.db, a.imp.id);
        await a.db
            .prepare("UPDATE backup_imports SET status = 'failed', state = ?2, error = ?3, updated_at = ?4, lease_until = NULL WHERE id = ?1")
            .bind(a.imp.id, JSON.stringify(a.state), `Échec à l'étape « ${step} » : rien n'a été remplacé.`, nowIso())
            .run();
    } else {
        await liftMaintenance(a.db, a.imp.id);
        await a.db
            .prepare("UPDATE backup_imports SET state = ?2, error = ?3, updated_at = ?4 WHERE id = ?1")
            .bind(a.imp.id, JSON.stringify(a.state), `Échec à l'étape « ${step} » après le remplacement des données.`, nowIso())
            .run();
    }
    await audit(a.ctx, a.actor?.id ?? null, "backup_import_failed", {
        import_id: a.imp.id,
        step,
        pre_import_id: pre,
        data_replaced: a.state.swapped,
        by_username: a.actor?.username ?? null,
    });
    throw new ApiError(
        500,
        "IMPORT_FAILED",
        a.state.swapped
            ? `Les données ont été remplacées, mais l'étape « ${step} » a échoué : relancez « Appliquer » pour la terminer.${preText}`
            : `L'import a échoué à l'étape « ${step} » : rien n'a été remplacé.${preText}`,
        { step, pre_import_backup_id: pre, data_replaced: a.state.swapped },
    );
}

type ApplyResult =
    | { done: false; step: string; progress: number; message: string; pre_import_backup_id: string | null }
    | { done: true; step: "done"; progress: 1; summary: ReturnType<typeof summaryOf> };

function pending(a: ApplyContext): ApplyResult {
    const step = publicStep(a.state);
    return { done: false, step, progress: progressOf(a.state, a.checked), message: STEP_MESSAGES[step] ?? "", pre_import_backup_id: a.state.pre_import_id };
}

/**
 * Runs the next steps of an applying import. Each request does one major phase at most:
 * the pre_import copy, then the rows and files of the file (as many as its budget allows),
 * then the swap and the end.
 */
async function runApply(a: ApplyContext, file: ImportFile, header: fmt.CpbkHeader, keys: fmt.CpbkKeys): Promise<ApplyResult> {
    try {
        if (a.state.step === "pre_import") {
            await settle(a.state.since, a.env);
            const pre = await createBackup(a.env, "pre_import", a.actor);
            a.state.pre_import_id = pre.id;
            a.state.step = "stage";
            await saveState(a);
            await audit(a.ctx, a.actor?.id ?? null, "backup_created", {
                backup_id: pre.id,
                backup_kind: "pre_import",
                import_id: a.imp.id,
                rows: pre.rows_count,
                files: pre.files_count,
                by_username: a.actor?.username ?? null,
            });
            return pending(a);
        }
        if (a.state.step === "stage") await prepareStage(a, planFor(a));
        if (a.state.step === "stream") {
            await streamEntries(a, file, header, keys);
            return pending(a);
        }
        if (a.state.step === "swap") await swap(a);
        if (a.state.step === "finalize") {
            if (!(await finalizeFiles(a))) {
                await saveState(a);
                return pending(a);
            }
            a.state.step = "cleanup";
            await saveState(a);
        }
        if (a.state.step === "cleanup") return { done: true, step: "done", progress: 1, summary: await cleanup(a) };
        return { done: true, step: "done", progress: 1, summary: summaryOf(a) };
    } catch (e) {
        if (e instanceof ApiError && e.status < 500) throw e;
        return failApply(a, e);
    }
}

function planFor(a: ApplyContext): Record<string, Plan> {
    if (Object.keys(a.state.plans).length) return a.state.plans;
    throw new Error("no table plan");
}

async function takeLease(db: D1Database, id: string): Promise<boolean> {
    const now = nowIso();
    const res = await db
        .prepare("UPDATE backup_imports SET lease_until = ?3 WHERE id = ?1 AND (lease_until IS NULL OR lease_until < ?2)")
        .bind(id, now, new Date(Date.now() + LEASE_MS).toISOString())
        .run();
    return res.meta.changes > 0;
}

async function releaseLease(db: D1Database, id: string): Promise<void> {
    await db.prepare("UPDATE backup_imports SET lease_until = NULL WHERE id = ?1").bind(id).run();
}

/**
 * POST /admin/backups/imports/{importId}/apply {"passphrase", "confirm": "REMPLACER TOUTES LES DONNÉES"}
 * Call again while the answer is `done: false`.
 */
export async function applyImport(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.backupApply, "player:" + auth.player.id);
    const id = importId(ctx);
    const body = await readJsonObject(ctx);
    new Validator(body, ["passphrase", "confirm"]).done();
    const where = { action: "apply", import_id: id };
    let passphrase: string;
    try {
        passphrase = passphraseOf(body);
    } catch (e) {
        return refuse(ctx, auth, e as ApiError, where);
    }
    if (typeof body.confirm !== "string" || body.confirm.normalize("NFC").trim() !== CONFIRM_PHRASE)
        await refuse(ctx, auth, new ApiError(400, "CONFIRMATION_REQUIRED", `Pour confirmer, tapez exactement : ${CONFIRM_PHRASE}`), where);
    const db = ctx.env.DB;
    let imp = await loadImport(db, id);
    if (CLOSED.has(imp.status)) await refuse(ctx, auth, Errors.conflict("IMPORT_CLOSED", "Cet import est terminé ou abandonné."), where);
    if (!imp.checked || imp.status === "uploading")
        await refuse(ctx, auth, Errors.conflict("IMPORT_NOT_CHECKED", "Vérifiez d'abord le fichier (étape « Vérifier »)."), where);
    const checked = JSON.parse(imp.checked!) as CheckedInfo;
    if (!checked.compatible)
        await refuse(ctx, auth, Errors.conflict("IMPORT_INCOMPATIBLE", `Cette sauvegarde n'est pas compatible avec ce serveur : ${checked.problems[0] ?? ""}`), where);
    const file = new ImportFile(ctx.env, imp);
    const header = await file.readHeader();
    const keys = await fmt.deriveKeys(passphrase, header.salt, header.iterations);
    if (!fmt.keyCheckMatches(header, keys)) await refuse(ctx, auth, WRONG_PASSPHRASE(), where);
    if (!(await takeLease(db, id)))
        throw new ApiError(409, "IMPORT_BUSY", "Une étape de cet import est déjà en cours : réessayez dans un instant.", undefined, { "retry-after": "5" });
    try {
        imp = await loadImport(db, id);
        let state = imp.state ? (JSON.parse(imp.state) as ApplyState) : null;
        const actor = actorOf(auth);
        if (imp.status !== "applying" || !state || state.step === "done") {
            // Start, or start again after a failure (nothing was replaced then).
            const m = await acquireMaintenance(db, id);
            if (!m) await refuse(ctx, auth, Errors.conflict("IMPORT_IN_PROGRESS", "Un autre import est en train de remplacer les données."), where);
            state = newState(m!.since, state ? state.restarts + 1 : 0);
            await db.prepare("UPDATE backup_imports SET status = 'applying', state = ?2, pre_import_id = NULL, error = NULL, updated_at = ?3 WHERE id = ?1")
                .bind(id, JSON.stringify(state), nowIso())
                .run();
            await audit(ctx, auth.player.id, "backup_import_started", {
                import_id: id,
                source_backup_id: checked.summary.id,
                schema: checked.summary.schema,
                by_username: auth.player.username,
            });
        } else if (!state.swapped && !(await renewMaintenance(db, id))) {
            // The maintenance lapsed: players may have written since the pre_import copy, which
            // must be the state just before the swap. Everything starts again.
            const m = await acquireMaintenance(db, id);
            if (!m) await refuse(ctx, auth, Errors.conflict("IMPORT_IN_PROGRESS", "Un autre import est en train de remplacer les données."), where);
            state = newState(m!.since, state.restarts + 1);
            await db.prepare("UPDATE backup_imports SET state = ?2, updated_at = ?3 WHERE id = ?1").bind(id, JSON.stringify(state), nowIso()).run();
        }
        if (state.step === "pre_import" || state.step === "stage") {
            // Table plans from the check, against the schema of now.
            const schema = await readSchema(db);
            const manifestTables = Object.entries(checked.tables).map(([name, columns]) => ({ name, columns, rows: 0, bytes: 0, parts: [] }));
            const c = compatibility({ ...stubManifest(checked), tables: manifestTables }, schema);
            if (c.problems.length) throw Errors.conflict("IMPORT_INCOMPATIBLE", `Cette sauvegarde n'est plus compatible avec ce serveur : ${c.problems[0]}`);
            state.plans = c.plans;
        }
        const a: ApplyContext = {
            ctx,
            env: ctx.env,
            db,
            imp,
            checked,
            state,
            actor,
            sessionId: auth.sessionId,
            deadline: Date.now() + APPLY_BUDGET_MS,
            ops: { r2: 0, d1: 0 },
        };
        const result = await runApply(a, file, header, keys);
        return json(ctx, result);
    } catch (e) {
        if (e instanceof ApiError && e.code === "IMPORT_INCOMPATIBLE") {
            await liftMaintenance(db, id);
            await db.prepare("UPDATE backup_imports SET status = 'failed', error = ?2, updated_at = ?3 WHERE id = ?1").bind(id, e.message, nowIso()).run();
            await audit(ctx, auth.player.id, "backup_refused", { ...where, reason: e.code, by_username: auth.player.username });
        }
        throw e;
    } finally {
        await releaseLease(db, id);
    }
}

function stubManifest(checked: CheckedInfo): StoredManifest {
    return {
        format: "cosmic-backup",
        format_version: 1,
        backup_id: checked.summary.id ?? "",
        kind: "manual",
        created_at: checked.summary.created_at,
        finished_at: checked.summary.created_at,
        created_by: null,
        environment: checked.summary.environment,
        schema: checked.summary.schema,
        migrations: [],
        tables: [],
        excluded_tables: [],
        files: { count: 0, bytes: 0, lists: [], excluded_prefixes: [] },
        totals: { tables: 0, rows: 0, files: 0, bytes: 0 },
        notes: [],
    };
}

/** DELETE /admin/backups/imports/{importId} — abandons an import and deletes its parts. */
export async function deleteImport(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = importId(ctx);
    const db = ctx.env.DB;
    const imp = await loadImport(db, id);
    const where = { action: "abandon", import_id: id };
    if (imp.status === "abandoned" || imp.status === "expired") return json(ctx, { abandoned: true, import_id: id });
    if (imp.status === "done") await refuse(ctx, auth, Errors.conflict("IMPORT_CLOSED", "Cet import est terminé."), where);
    if (imp.status === "applying") {
        const state = imp.state ? (JSON.parse(imp.state) as ApplyState) : null;
        if (state?.swapped)
            await refuse(ctx, auth, Errors.conflict("IMPORT_ALREADY_APPLIED", "Les données ont déjà été remplacées : terminez l'import avec « Appliquer »."), where);
        if (!(await takeLease(db, id)))
            throw new ApiError(409, "IMPORT_BUSY", "Une étape de cet import est en cours : réessayez dans un instant.", undefined, { "retry-after": "5" });
        await dropScratchTables(db);
        await liftMaintenance(db, id);
    }
    await deletePrefix(ctx.env, `imports/${id}/`);
    await db.batch([
        db.prepare("DELETE FROM backup_import_parts WHERE import_id = ?1").bind(id),
        db.prepare("UPDATE backup_imports SET status = 'abandoned', updated_at = ?2, finished_at = ?2, lease_until = NULL WHERE id = ?1").bind(id, nowIso()),
    ]);
    await audit(ctx, auth.player.id, "backup_import_abandoned", { import_id: id, by_username: auth.player.username });
    return json(ctx, { abandoned: true, import_id: id });
}

// ------------------------------------------------------------------ cron

/** Imports left behind: uploads never finished, applies nobody continued (maintenance lapsed). */
async function housekeepImports(env: Bindings, exec: ExecutionContext): Promise<void> {
    const db = env.DB;
    const rows = await db.prepare("SELECT * FROM backup_imports WHERE status IN ('uploading', 'checked', 'failed', 'applying')").all<ImportRow>();
    const now = Date.now();
    for (const imp of rows.results) {
        const idle = now - Date.parse(imp.updated_at);
        if (imp.status !== "applying") {
            if (idle > IMPORT_EXPIRY_MS) {
                await deletePrefix(env, `imports/${imp.id}/`);
                await db.batch([
                    db.prepare("DELETE FROM backup_import_parts WHERE import_id = ?1").bind(imp.id),
                    db.prepare("UPDATE backup_imports SET status = 'expired', updated_at = ?2, finished_at = ?2 WHERE id = ?1").bind(imp.id, nowIso()),
                ]);
            }
            continue;
        }
        if (idle < MAINTENANCE_TTL_MS || !(await takeLease(db, imp.id))) continue;
        const ctx = systemCtx(env, exec);
        try {
            const state = imp.state ? (JSON.parse(imp.state) as ApplyState) : newState(nowIso());
            if (state.swapped) {
                // The data were replaced: finish without anyone (the swapped files, the temporary tables).
                const a: ApplyContext = {
                    ctx,
                    env,
                    db,
                    imp,
                    checked: JSON.parse(imp.checked ?? "null") as CheckedInfo,
                    state,
                    actor: null,
                    sessionId: null,
                    deadline: Date.now() + 5 * 60_000,
                    ops: { r2: -1_000_000, d1: -1_000_000 },
                };
                if (state.step === "finalize" && (await finalizeFiles(a))) a.state.step = "cleanup";
                if (a.state.step === "cleanup") await cleanup(a);
                else await saveState(a);
            } else {
                await dropScratchTables(db);
                await liftMaintenance(db, imp.id);
                await db
                    .prepare("UPDATE backup_imports SET status = 'failed', error = ?2, updated_at = ?3 WHERE id = ?1")
                    .bind(imp.id, "Import interrompu (aucune nouvelle depuis 15 minutes) : rien n'a été remplacé.", nowIso())
                    .run();
                await audit(ctx, null, "backup_import_failed", { import_id: imp.id, step: publicStep(state), reason: "TIMEOUT", data_replaced: false, by: "cron" });
            }
        } finally {
            await releaseLease(db, imp.id);
        }
    }
}

/** Daily: imports left behind, the automatic copy (not during an import), the retention. */
export async function scheduledBackups(env: Bindings, exec: ExecutionContext): Promise<void> {
    try {
        await housekeepImports(env, exec);
    } catch (e) {
        log("error", "backup_import_housekeeping_failed", { error: e instanceof Error ? e.message : String(e) });
    }
    if (await maintenanceActive(env)) {
        log("info", "backup_skipped_maintenance");
        return;
    }
    try {
        const b = await createBackup(env, "auto", null);
        log("info", "backup_created", { backup_id: b.id, rows: b.rows_count, files: b.files_count, bytes: b.bytes });
    } catch (e) {
        log("error", "backup_auto_failed", { error: e instanceof Error ? e.message : String(e) });
        try {
            const details = e instanceof ApiError ? (e.details as { backup_id?: string } | undefined) : undefined;
            await audit(systemCtx(env, exec), null, "backup_failed", { backup_id: details?.backup_id ?? null, backup_kind: "auto", by: "cron" });
        } catch {
            // logged above
        }
    }
    try {
        const deleted = await applyRetention(env);
        if (deleted.length) log("info", "backup_retention", { deleted: deleted.length });
    } catch (e) {
        log("error", "backup_retention_failed", { error: e instanceof Error ? e.message : String(e) });
    }
}
