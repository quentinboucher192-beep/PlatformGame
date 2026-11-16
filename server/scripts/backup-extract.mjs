// Opens a Cosmic Online backup (.cpbk) without the server: last-resort restore with wrangler
// (docs/online/BACKUP.md, « Restauration manuelle »). Node.js 20+ only, no dependency.
//
//   node scripts/backup-extract.mjs cosmic-backup-2026-11-16.cpbk dossier-de-sortie
//
// The passphrase is read from the CPBK_PASSPHRASE environment variable or typed (hidden); it is
// never written anywhere. The folder receives:
//   manifest.json, end.json          the manifest and the end of the backup
//   d1/<table>/<nnnn>.jsonl          the rows (one JSON array per line, columns in the manifest)
//   files/<nnnn>.jsonl               the list of the R2 files (key, size, SHA-256, metadata)
//   r2/<key>                         the files themselves
//   restore-d1.sql                   every table emptied then filled (wrangler d1 execute --file)
//   restore-r2.sh, restore-r2.ps1    one `wrangler r2 object put` per file
import { createDecipheriv, createHash, hkdfSync, pbkdf2Sync, timingSafeEqual } from "node:crypto";
import { closeSync, fstatSync, mkdirSync, openSync, readFileSync, readSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";

const HEADER_SIZE = 48;
const OVERHEAD = 28;
const [input, outDir] = process.argv.slice(2);
if (!input || !outDir) {
    console.error("Usage : node scripts/backup-extract.mjs <fichier.cpbk> <dossier de sortie>");
    process.exit(2);
}

function fail(message) {
    console.error(message);
    process.exit(1);
}

async function readPassphrase() {
    if (process.env.CPBK_PASSPHRASE) return process.env.CPBK_PASSPHRASE;
    if (!process.stdin.isTTY) fail("Phrase secrète : définissez CPBK_PASSPHRASE ou lancez le script dans un terminal.");
    process.stdout.write("Phrase secrète : ");
    process.stdin.setRawMode(true);
    process.stdin.resume();
    let text = "";
    return new Promise((resolve) => {
        process.stdin.on("data", (buf) => {
            for (const ch of buf.toString("utf8")) {
                if (ch === "\r" || ch === "\n") {
                    process.stdin.setRawMode(false);
                    process.stdin.pause();
                    process.stdout.write("\n");
                    resolve(text);
                    return;
                }
                if (ch === "\u0003") process.exit(130);
                if (ch === "\u007f" || ch === "\b") text = [...text].slice(0, -1).join("");
                else text += ch;
            }
        });
    });
}

const fd = openSync(input, "r");
const size = fstatSync(fd).size;
const read = (offset, length) => {
    const b = Buffer.alloc(length);
    const n = readSync(fd, b, 0, length, offset);
    if (n !== length) fail("Fichier tronqué.");
    return b;
};
if (size < HEADER_SIZE + OVERHEAD + 1) fail("Fichier trop court pour être une sauvegarde.");
const header = read(0, HEADER_SIZE);
if (header.subarray(0, 4).toString("latin1") !== "CPBK") fail("Ce fichier n'est pas une sauvegarde Cosmic Online (.cpbk).");
if (header.readUInt16LE(4) !== 1 || header[6] !== 1 || header[7] !== 1) fail("Version de format inconnue de ce script.");
const iterations = header.readUInt32LE(8);
const salt = header.subarray(12, 28);
const chunkSize = header.readUInt32LE(28);
const keyCheck = header.subarray(32, 48);

const passphrase = (await readPassphrase()).normalize("NFC");
const master = pbkdf2Sync(Buffer.from(passphrase, "utf8"), salt, iterations, 32, "sha256");
const key = Buffer.from(hkdfSync("sha256", master, Buffer.alloc(0), Buffer.from("cosmic-backup v1 encryption"), 32));
const check = Buffer.from(hkdfSync("sha256", master, Buffer.alloc(0), Buffer.from("cosmic-backup v1 key check"), 16));
if (!timingSafeEqual(check, keyCheck)) fail("Phrase secrète incorrecte.");

const recordSize = chunkSize + OVERHEAD;
const count = Math.ceil((size - HEADER_SIZE) / recordSize);
const lastSize = size - HEADER_SIZE - (count - 1) * recordSize;
if (lastSize < OVERHEAD + 1) fail("Fichier tronqué.");

function decrypt(i) {
    const last = i === count - 1;
    const rec = read(HEADER_SIZE + i * recordSize, last ? lastSize : recordSize);
    const aad = Buffer.alloc(HEADER_SIZE + 9);
    header.copy(aad, 0);
    aad.writeBigUInt64LE(BigInt(i), HEADER_SIZE);
    aad[HEADER_SIZE + 8] = last ? 1 : 0;
    const d = createDecipheriv("aes-256-gcm", key, rec.subarray(0, 12));
    d.setAAD(aad);
    d.setAuthTag(rec.subarray(rec.length - 16));
    try {
        return Buffer.concat([d.update(rec.subarray(12, rec.length - 16)), d.final()]);
    } catch {
        fail(`Bloc ${i} altéré, déplacé ou manquant : fichier inutilisable.`);
    }
}

// ------------------------------------------------------------------ entries

const SAFE_KEY = /^[A-Za-z0-9._-]+(\/[A-Za-z0-9._-]+)*$/;
const out = (rel) => {
    const p = join(outDir, rel);
    mkdirSync(dirname(p), { recursive: true });
    return p;
};
const sha256 = (b) => createHash("sha256").update(b).digest("hex");
let manifest = null;
let end = null;
const r2 = [];
const unsafe = [];
let damaged = 0;

function onEntry(kind, name, meta, data, digest) {
    if (sha256(data) !== digest.toString("hex")) {
        damaged += 1;
        console.warn(`Empreinte incorrecte : ${name}`);
    }
    switch (kind) {
        case 1:
            manifest = JSON.parse(data.toString("utf8"));
            writeFileSync(out("manifest.json"), data);
            break;
        case 2:
        case 3:
            if (!/^(d1\/[A-Za-z0-9_]+|files)\/\d{4}\.jsonl$/.test(name)) fail(`Nom d'entrée inattendu : ${name}`);
            writeFileSync(out(name), data);
            break;
        case 4: {
            const m = meta ? JSON.parse(meta) : {};
            const safe = SAFE_KEY.test(name) && !name.split("/").some((s) => s === "." || s === "..");
            const path = safe ? `r2/${name}` : `r2-autres/${sha256(Buffer.from(name))}`;
            if (!safe) unsafe.push(name);
            writeFileSync(out(path), data);
            r2.push({ key: name, path, size: data.length, sha256: digest.toString("hex"), content_type: m.content_type ?? null, custom_metadata: m.custom_metadata ?? {}, safe });
            break;
        }
        case 5:
            end = JSON.parse(data.toString("utf8"));
            writeFileSync(out("end.json"), data);
            break;
        default:
            fail(`Entrée de type ${kind} inconnue.`);
    }
}

let pending = Buffer.alloc(0);
for (let i = 0; i < count; ++i) {
    pending = pending.length ? Buffer.concat([pending, decrypt(i)]) : decrypt(i);
    for (;;) {
        if (pending.length < 7) break;
        const nameLen = pending.readUInt16LE(1);
        if (pending.length < 7 + nameLen) break;
        const metaLen = pending.readUInt32LE(3 + nameLen);
        const headLen = 7 + nameLen + metaLen + 8;
        if (pending.length < headLen) break;
        const dataLen = Number(pending.readBigUInt64LE(7 + nameLen + metaLen));
        if (pending.length < headLen + dataLen + 32) break;
        const kind = pending[0];
        const name = pending.subarray(3, 3 + nameLen).toString("utf8");
        const meta = metaLen ? pending.subarray(7 + nameLen, 7 + nameLen + metaLen).toString("utf8") : null;
        const data = pending.subarray(headLen, headLen + dataLen);
        const digest = pending.subarray(headLen + dataLen, headLen + dataLen + 32);
        onEntry(kind, name, meta, Buffer.from(data), digest);
        pending = pending.subarray(headLen + dataLen + 32);
    }
}
closeSync(fd);
if (!manifest || !end || pending.length) fail("Sauvegarde incomplète (manifeste ou fin absents).");
writeFileSync(out("r2-index.jsonl"), r2.map((f) => JSON.stringify(f)).join("\n") + "\n");

// ------------------------------------------------------------------ restore-d1.sql

const lit = (v) => {
    if (v === null) return "NULL";
    if (typeof v === "number") return Number.isFinite(v) ? String(v) : "NULL";
    return "'" + String(v).replace(/'/g, "''") + "'";
};
const qi = (id) => '"' + id.replace(/"/g, '""') + '"';
const readRows = (t) =>
    t.parts.flatMap((p) =>
        readFileSync(join(outDir, p), "utf8")
            .split("\n")
            .filter(Boolean)
            .map((l) => JSON.parse(l)),
    );

// Rows whose parent is missing (a copy runs while players play) are dropped, or their link
// set to NULL, as SQLite would have done: the import must not stop on a foreign key.
const parentsFirst = [...manifest.tables].reverse();
const keySets = new Map(); // "table|col,col" -> Set of JSON tuples
for (const t of manifest.tables) for (const fk of t.foreign_keys ?? []) keySets.set(`${fk.parent}|${fk.to.join(",")}`, new Set());
const sql = [
    "-- Cosmic Online : restauration manuelle d'une sauvegarde (scripts/backup-extract.mjs).",
    `-- Sauvegarde ${manifest.backup_id ?? "?"} du ${manifest.created_at}, schéma ${manifest.schema}.`,
    "-- À exécuter sur une base dont les migrations vont jusqu'à ce schéma : toutes les tables",
    "-- sauvegardées sont vidées puis remplies. Les sessions ne sont pas sauvegardées : chaque",
    "-- joueur se reconnecte.",
    "PRAGMA defer_foreign_keys = ON;",
    "DELETE FROM player_sessions;",
];
for (const t of manifest.tables) sql.push(`DELETE FROM ${qi(t.name)};`);
let dropped = 0;
for (const t of parentsFirst) {
    const rows = readRows(t);
    const idx = (c) => t.columns.indexOf(c);
    const fks = (t.foreign_keys ?? []).filter((fk) => {
        const parent = manifest.tables.find((x) => x.name === fk.parent);
        return parent && fk.from.every((c) => idx(c) >= 0) && fk.to.every((c) => parent.columns.includes(c));
    });
    const own = [...keySets.keys()].filter((k) => k.startsWith(t.name + "|")).map((k) => ({ set: keySets.get(k), cols: k.split("|")[1].split(",").map(idx) }));
    const accepted = [];
    for (const row of rows) {
        let keep = true;
        for (const fk of fks) {
            const vals = fk.from.map((c) => row[idx(c)]);
            if (vals.some((v) => v === null)) continue;
            const set = keySets.get(`${fk.parent}|${fk.to.join(",")}`);
            if (set.has(JSON.stringify(vals))) continue;
            if (fk.parent === t.name && rows.some((r) => JSON.stringify(fk.to.map((c) => r[idx(c)])) === JSON.stringify(vals))) continue;
            if (fk.on_delete === "SET NULL") for (const c of fk.from) row[idx(c)] = null;
            else keep = false;
        }
        if (!keep) {
            dropped += 1;
            continue;
        }
        for (const o of own) if (o.cols.every((i) => i >= 0)) o.set.add(JSON.stringify(o.cols.map((i) => row[i])));
        accepted.push(row);
    }
    const head = `INSERT INTO ${qi(t.name)} (${t.columns.map(qi).join(", ")}) VALUES\n`;
    let values = [];
    let length = head.length;
    const flush = () => {
        if (values.length) sql.push(head + values.join(",\n") + ";");
        values = [];
        length = head.length;
    };
    for (const row of accepted) {
        const v = "(" + row.map(lit).join(", ") + ")";
        // D1 refuses statements over 100 KB.
        if (values.length && length + v.length + 2 > 90_000) flush();
        values.push(v);
        length += v.length + 2;
    }
    flush();
}
writeFileSync(out("restore-d1.sql"), sql.join("\n") + "\n");

// ------------------------------------------------------------------ restore-r2.*

const restorable = r2.filter((f) => f.safe);
// Only plain media types reach the generated commands (nothing a shell could interpret).
const ct = (f) => (f.content_type && /^[\w.+-]+\/[\w.+-]+$/.test(f.content_type) ? ` --content-type "${f.content_type}"` : "");
const sh = [
    "#!/usr/bin/env bash",
    "# Remet les fichiers R2 de la sauvegarde. BUCKET et WRANGLER_ENV (ex. « --env staging ») à adapter.",
    "set -euo pipefail",
    'BUCKET="${BUCKET:-cosmic-online-prod-files}"',
    'WRANGLER_ENV="${WRANGLER_ENV:-}"',
    'cd "$(dirname "$0")"',
    ...restorable.map(
        (f) => `npx wrangler r2 object put "$BUCKET/${f.key}" --file "${f.path}"${ct(f)} --remote $WRANGLER_ENV`,
    ),
];
writeFileSync(out("restore-r2.sh"), sh.join("\n") + "\n");
const ps = [
    "# Remet les fichiers R2 de la sauvegarde. $Bucket et $WranglerEnv (ex. « --env staging ») à adapter.",
    '$Bucket = if ($env:BUCKET) { $env:BUCKET } else { "cosmic-online-prod-files" }',
    '$WranglerEnv = if ($env:WRANGLER_ENV) { $env:WRANGLER_ENV.Split(" ") } else { @() }',
    "Set-Location $PSScriptRoot",
    '$ErrorActionPreference = "Stop"',
    ...restorable.map(
        (f) => `npx wrangler r2 object put "$Bucket/${f.key}" --file "${f.path}"${ct(f)} --remote @WranglerEnv`,
    ),
];
writeFileSync(out("restore-r2.ps1"), ps.join("\r\n") + "\r\n");

const rows = manifest.tables.reduce((n, t) => n + t.rows, 0);
console.log(`Sauvegarde du ${manifest.created_at} (schéma ${manifest.schema}) : ${manifest.tables.length} tables, ${rows} lignes, ${r2.length} fichiers.`);
if (dropped) console.log(`${dropped} ligne(s) orpheline(s) écartée(s) de restore-d1.sql.`);
if (end.files_missing_count) console.log(`${end.files_missing_count} fichier(s) manquaient déjà sur le serveur lors du téléchargement (voir end.json).`);
if (unsafe.length) console.log(`${unsafe.length} fichier(s) au nom inhabituel rangés dans r2-autres/ (voir r2-index.jsonl) : à remettre à la main.`);
if (damaged) fail(`${damaged} entrée(s) à l'empreinte incorrecte : vérifiez le fichier.`);
console.log(`Fichiers écrits dans ${outDir}. Suite : docs/online/BACKUP.md, « Restauration manuelle ».`);
