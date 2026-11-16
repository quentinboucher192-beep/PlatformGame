// The encrypted backup file of Cosmic Online (.cpbk, docs/online/BACKUP.md): key derivation
// from the administrator's passphrase, the header, the independently encrypted chunks and the
// plaintext entry stream they carry. Web Crypto only.
//
//   header (48 bytes) | record 0 | record 1 | ... | record n-1
//   record i = IV (12 random bytes) | AES-256-GCM(plaintext chunk i) with its 16-byte tag
//   additional data of record i = header | i (u64 LE) | 1 if it is the last record else 0
//
// Every chunk holds `chunk_size` plaintext bytes except the last one. Binding the index and the
// "last" flag means no chunk can be swapped, dropped or cut off without the decryption failing.

export const CPBK_VERSION = 1;
export const KDF_PBKDF2_SHA256 = 1;
export const CIPHER_AES_256_GCM = 1;
export const HEADER_SIZE = 48;
export const CHUNK_SIZE = 1024 * 1024;
export const IV_SIZE = 12;
export const TAG_SIZE = 16;
export const RECORD_OVERHEAD = IV_SIZE + TAG_SIZE;
/** PBKDF2-HMAC-SHA256 iterations of the files written by this server (stored in each header). */
// The most Web Crypto derives natively in Workers (no second of CPU per request), with
// passphrases of 12 characters at least. Files of other servers may carry more (HMAC path).
export const DEFAULT_ITERATIONS = 100_000;
export const MIN_ITERATIONS = 100_000;
export const MAX_ITERATIONS = 2_000_000;
const MIN_CHUNK = 64 * 1024;
const MAX_CHUNK = 8 * 1024 * 1024;
const MAGIC = [0x43, 0x50, 0x42, 0x4b]; // "CPBK"

const encoder = new TextEncoder();

export class CpbkError extends Error {
    constructor(
        readonly kind: "format" | "passphrase" | "corrupted",
        message: string,
        readonly chunk?: number,
    ) {
        super(message);
    }
}

// ------------------------------------------------------------------ keys

/** Web Crypto refuses more than 100 000 PBKDF2 iterations in Workers: above, the same function from HMAC. */
const NATIVE_PBKDF2_MAX = 100_000;

/** PBKDF2-HMAC-SHA256 with a 32-byte output (RFC 8018), any iteration count. */
export async function pbkdf2Sha256(password: Uint8Array, salt: Uint8Array, iterations: number): Promise<Uint8Array> {
    if (iterations <= NATIVE_PBKDF2_MAX) {
        const key = await crypto.subtle.importKey("raw", password, "PBKDF2", false, ["deriveBits"]);
        return new Uint8Array(await crypto.subtle.deriveBits({ name: "PBKDF2", hash: "SHA-256", salt, iterations }, key, 256));
    }
    return pbkdf2Sha256Hmac(password, salt, iterations);
}

/** The same PBKDF2, first block only (32 bytes), computed with one HMAC-SHA256 per iteration. */
export async function pbkdf2Sha256Hmac(password: Uint8Array, salt: Uint8Array, iterations: number): Promise<Uint8Array> {
    const key = await crypto.subtle.importKey("raw", password, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
    const first = new Uint8Array(salt.length + 4);
    first.set(salt, 0);
    first[salt.length + 3] = 1; // block index 1, big-endian
    let u = new Uint8Array(await crypto.subtle.sign("HMAC", key, first));
    const t = u.slice();
    for (let i = 1; i < iterations; ++i) {
        u = new Uint8Array(await crypto.subtle.sign("HMAC", key, u));
        for (let j = 0; j < t.length; ++j) t[j] = t[j]! ^ u[j]!;
    }
    return t;
}

export interface CpbkKeys {
    key: CryptoKey;
    /** 16 bytes written in the header: a wrong passphrase is told apart from a damaged file. */
    check: Uint8Array;
}

/** The passphrase as the key derivation sees it: Unicode NFC, then UTF-8. */
export function normalizePassphrase(passphrase: string): string {
    return passphrase.normalize("NFC");
}

/** master = PBKDF2(passphrase, salt, iterations); key and check = HKDF-SHA256(master, empty salt, info). */
export async function deriveKeys(passphrase: string, salt: Uint8Array, iterations: number): Promise<CpbkKeys> {
    const master = await pbkdf2Sha256(encoder.encode(normalizePassphrase(passphrase)), salt, iterations);
    const hk = await crypto.subtle.importKey("raw", master, "HKDF", false, ["deriveBits"]);
    const hkdf = (info: string, bits: number) =>
        crypto.subtle.deriveBits({ name: "HKDF", hash: "SHA-256", salt: new Uint8Array(0), info: encoder.encode(info) }, hk, bits);
    const [keyBits, check] = await Promise.all([hkdf("cosmic-backup v1 encryption", 256), hkdf("cosmic-backup v1 key check", 128)]);
    const key = await crypto.subtle.importKey("raw", keyBits, { name: "AES-GCM" }, false, ["encrypt", "decrypt"]);
    return { key, check: new Uint8Array(check) };
}

// ------------------------------------------------------------------ header

export interface CpbkHeader {
    version: number;
    iterations: number;
    salt: Uint8Array;
    chunkSize: number;
    keyCheck: Uint8Array;
    /** The 48 bytes as written (part of every chunk's additional data). */
    bytes: Uint8Array;
}

export function encodeHeader(iterations: number, salt: Uint8Array, chunkSize: number, keyCheck: Uint8Array): Uint8Array {
    const b = new Uint8Array(HEADER_SIZE);
    const v = new DataView(b.buffer);
    b.set(MAGIC, 0);
    v.setUint16(4, CPBK_VERSION, true);
    b[6] = KDF_PBKDF2_SHA256;
    b[7] = CIPHER_AES_256_GCM;
    v.setUint32(8, iterations, true);
    b.set(salt.subarray(0, 16), 12);
    v.setUint32(28, chunkSize, true);
    b.set(keyCheck.subarray(0, 16), 32);
    return b;
}

export function parseHeader(bytes: Uint8Array): CpbkHeader {
    if (bytes.byteLength < HEADER_SIZE || MAGIC.some((m, i) => bytes[i] !== m))
        throw new CpbkError("format", "ce fichier n'est pas une sauvegarde Cosmic Online (.cpbk)");
    const v = new DataView(bytes.buffer, bytes.byteOffset, HEADER_SIZE);
    const version = v.getUint16(4, true);
    if (version !== CPBK_VERSION) throw new CpbkError("format", `version de format ${version} inconnue de ce serveur`);
    if (bytes[6] !== KDF_PBKDF2_SHA256 || bytes[7] !== CIPHER_AES_256_GCM) throw new CpbkError("format", "méthode de chiffrement inconnue");
    const iterations = v.getUint32(8, true);
    if (iterations < MIN_ITERATIONS || iterations > MAX_ITERATIONS) throw new CpbkError("format", "paramètres de dérivation de clé refusés");
    const chunkSize = v.getUint32(28, true);
    if (chunkSize < MIN_CHUNK || chunkSize > MAX_CHUNK) throw new CpbkError("format", "taille de bloc invalide");
    return {
        version,
        iterations,
        salt: bytes.slice(12, 28),
        chunkSize,
        keyCheck: bytes.slice(32, 48),
        bytes: bytes.slice(0, HEADER_SIZE),
    };
}

/** Same 16 bytes, compared in constant time. */
export function keyCheckMatches(header: CpbkHeader, keys: CpbkKeys): boolean {
    return keys.check.byteLength === 16 && crypto.subtle.timingSafeEqual(header.keyCheck, keys.check);
}

// ------------------------------------------------------------------ records

export function recordSize(chunkSize: number): number {
    return chunkSize + RECORD_OVERHEAD;
}

export function recordOffset(index: number, chunkSize: number): number {
    return HEADER_SIZE + index * recordSize(chunkSize);
}

export interface RecordLayout {
    count: number;
    /** Size of the last record (IV + ciphertext + tag). */
    lastSize: number;
    /** Plaintext bytes of the whole file. */
    plaintextSize: number;
}

/** Number of records of a file of `fileSize` bytes; a cut that leaves no valid last record is refused. */
export function recordLayout(fileSize: number, chunkSize: number): RecordLayout {
    const body = fileSize - HEADER_SIZE;
    if (body < RECORD_OVERHEAD + 1) throw new CpbkError("corrupted", "fichier tronqué", 0);
    const rs = recordSize(chunkSize);
    const count = Math.ceil(body / rs);
    const lastSize = body - (count - 1) * rs;
    if (lastSize < RECORD_OVERHEAD + 1) throw new CpbkError("corrupted", "fichier tronqué", count - 1);
    return { count, lastSize, plaintextSize: (count - 1) * chunkSize + (lastSize - RECORD_OVERHEAD) };
}

function additionalData(header: Uint8Array, index: number, last: boolean): Uint8Array {
    const ad = new Uint8Array(HEADER_SIZE + 9);
    ad.set(header.subarray(0, HEADER_SIZE), 0);
    new DataView(ad.buffer).setBigUint64(HEADER_SIZE, BigInt(index), true);
    ad[HEADER_SIZE + 8] = last ? 1 : 0;
    return ad;
}

export async function encryptChunk(key: CryptoKey, header: Uint8Array, index: number, last: boolean, plaintext: Uint8Array): Promise<Uint8Array> {
    const iv = new Uint8Array(IV_SIZE);
    crypto.getRandomValues(iv);
    const ct = new Uint8Array(
        await crypto.subtle.encrypt({ name: "AES-GCM", iv, additionalData: additionalData(header, index, last), tagLength: 128 }, key, plaintext),
    );
    const out = new Uint8Array(IV_SIZE + ct.byteLength);
    out.set(iv, 0);
    out.set(ct, IV_SIZE);
    return out;
}

export async function decryptChunk(key: CryptoKey, header: Uint8Array, index: number, last: boolean, record: Uint8Array): Promise<Uint8Array> {
    if (record.byteLength < RECORD_OVERHEAD + 1) throw new CpbkError("corrupted", "bloc tronqué", index);
    try {
        return new Uint8Array(
            await crypto.subtle.decrypt(
                { name: "AES-GCM", iv: record.subarray(0, IV_SIZE), additionalData: additionalData(header, index, last), tagLength: 128 },
                key,
                record.subarray(IV_SIZE),
            ),
        );
    } catch {
        throw new CpbkError("corrupted", `bloc ${index} altéré, déplacé ou manquant`, index);
    }
}

/**
 * Encrypts a plaintext byte stream into a .cpbk stream: the header, then one record per chunk.
 * Memory stays at about one chunk; nothing is read from the source before the reader asks.
 */
export function encryptStream(source: AsyncIterator<Uint8Array>, key: CryptoKey, header: Uint8Array, chunkSize: number): ReadableStream<Uint8Array> {
    const buf = new Uint8Array(chunkSize);
    let pending: Uint8Array | null = null;
    let pendingAt = 0;
    let exhausted = false;
    let headerSent = false;
    let finished = false;
    let index = 0;
    const more = async (): Promise<boolean> => {
        while (!exhausted && (pending === null || pendingAt >= pending.byteLength)) {
            const r = await source.next();
            if (r.done) exhausted = true;
            else {
                pending = r.value;
                pendingAt = 0;
            }
        }
        return !exhausted || (pending !== null && pendingAt < pending.byteLength);
    };
    return new ReadableStream<Uint8Array>({
        async pull(controller) {
            if (!headerSent) {
                headerSent = true;
                controller.enqueue(header.slice());
                return;
            }
            if (finished) {
                controller.close();
                return;
            }
            let fill = 0;
            while (fill < chunkSize && (await more())) {
                const p = pending!;
                const n = Math.min(chunkSize - fill, p.byteLength - pendingAt);
                buf.set(p.subarray(pendingAt, pendingAt + n), fill);
                fill += n;
                pendingAt += n;
            }
            const last = fill < chunkSize || !(await more());
            controller.enqueue(await encryptChunk(key, header, index++, last, buf.subarray(0, fill)));
            if (last) {
                finished = true;
                controller.close();
            }
        },
        async cancel() {
            await source.return?.();
        },
    });
}

// ------------------------------------------------------------------ entries

/** Kinds of the plaintext entries, in the order they appear. */
export const ENTRY_MANIFEST = 1;
export const ENTRY_TABLE = 2;
export const ENTRY_FILE_LIST = 3;
export const ENTRY_FILE = 4;
export const ENTRY_END = 5;
export const MAX_NAME_BYTES = 1024;
export const MAX_META_BYTES = 64 * 1024;
/** Largest entry held in memory (R2 files of the game are 16 MiB at most). */
export const MAX_ENTRY_BYTES = 64 * 1024 * 1024;
export const ENTRY_TRAILER = 32;

/** kind u8 | name length u16 | name | meta length u32 | meta (JSON) | data size u64; then the data and its SHA-256. */
export function encodeEntryHead(kind: number, name: string, meta: string | null, size: number): Uint8Array {
    const n = encoder.encode(name);
    const m = meta === null ? new Uint8Array(0) : encoder.encode(meta);
    if (n.byteLength === 0 || n.byteLength > MAX_NAME_BYTES || m.byteLength > MAX_META_BYTES) throw new Error("entry name or meta too long");
    const out = new Uint8Array(3 + n.byteLength + 4 + m.byteLength + 8);
    const v = new DataView(out.buffer);
    out[0] = kind;
    v.setUint16(1, n.byteLength, true);
    out.set(n, 3);
    v.setUint32(3 + n.byteLength, m.byteLength, true);
    out.set(m, 7 + n.byteLength);
    v.setBigUint64(7 + n.byteLength + m.byteLength, BigInt(size), true);
    return out;
}

/** A whole entry held in memory: head, data, SHA-256. */
export async function* entryOf(kind: number, name: string, meta: string | null, data: Uint8Array): AsyncGenerator<Uint8Array> {
    yield encodeEntryHead(kind, name, meta, data.byteLength);
    yield data;
    yield new Uint8Array(await crypto.subtle.digest("SHA-256", data));
}

export interface Entry {
    kind: number;
    name: string;
    meta: string | null;
    data: Uint8Array;
    sha256: Uint8Array;
    /** Plaintext offsets: where the entry starts and where the next one starts. */
    start: number;
    end: number;
}

const strictUtf8 = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });

/** Incremental parser of the plaintext entry stream (chunks may cut an entry anywhere). */
export class EntryReader {
    private queue: Uint8Array[] = [];
    private queued = 0;
    private head: { kind: number; name: string; meta: string | null; size: number; start: number } | null = null;
    private data: Uint8Array | null = null;
    private dataAt = 0;

    /** `pos`: plaintext offset of the next byte pushed. */
    constructor(public pos = 0) {}

    /** True while bytes of an unfinished entry are held. */
    get partial(): boolean {
        return this.head !== null || this.queued > 0;
    }

    private peek(n: number): Uint8Array | null {
        if (this.queued < n) return null;
        const first = this.queue[0]!;
        if (first.byteLength >= n) return first.subarray(0, n);
        const out = new Uint8Array(n);
        let at = 0;
        for (const q of this.queue) {
            const k = Math.min(q.byteLength, n - at);
            out.set(q.subarray(0, k), at);
            at += k;
            if (at === n) break;
        }
        return out;
    }

    private consume(n: number, into?: Uint8Array, intoAt = 0): void {
        let left = n;
        while (left > 0) {
            const first = this.queue[0]!;
            const k = Math.min(first.byteLength, left);
            if (into) into.set(first.subarray(0, k), intoAt + (n - left));
            if (k === first.byteLength) this.queue.shift();
            else this.queue[0] = first.subarray(k);
            left -= k;
        }
        this.queued -= n;
        this.pos += n;
    }

    push(bytes: Uint8Array): Entry[] {
        if (bytes.byteLength > 0) {
            this.queue.push(bytes);
            this.queued += bytes.byteLength;
        }
        const out: Entry[] = [];
        for (;;) {
            if (!this.head) {
                const h = this.peek(3);
                if (!h) break;
                const kind = h[0]!;
                const nameLen = h[1]! | (h[2]! << 8);
                if (kind < ENTRY_MANIFEST || kind > ENTRY_END || nameLen === 0 || nameLen > MAX_NAME_BYTES)
                    throw new CpbkError("format", "entrée invalide dans le contenu déchiffré");
                const h2 = this.peek(7 + nameLen);
                if (!h2) break;
                const metaLen = new DataView(h2.buffer, h2.byteOffset, h2.byteLength).getUint32(3 + nameLen, true);
                if (metaLen > MAX_META_BYTES) throw new CpbkError("format", "métadonnées d'entrée trop longues");
                const headLen = 7 + nameLen + metaLen + 8;
                const full = this.peek(headLen);
                if (!full) break;
                const fv = new DataView(full.buffer, full.byteOffset, full.byteLength);
                const size64 = fv.getBigUint64(7 + nameLen + metaLen, true);
                if (size64 > BigInt(MAX_ENTRY_BYTES)) throw new CpbkError("format", "entrée trop volumineuse");
                let name: string;
                let meta: string | null = null;
                try {
                    name = strictUtf8.decode(full.subarray(3, 3 + nameLen));
                    if (metaLen > 0) meta = strictUtf8.decode(full.subarray(7 + nameLen, 7 + nameLen + metaLen));
                } catch {
                    throw new CpbkError("format", "nom d'entrée invalide");
                }
                const start = this.pos;
                this.consume(headLen);
                this.head = { kind, name, meta, size: Number(size64), start };
                this.data = new Uint8Array(this.head.size);
                this.dataAt = 0;
            }
            const head = this.head;
            if (this.dataAt < head.size) {
                const n = Math.min(this.queued, head.size - this.dataAt);
                if (n === 0) break;
                this.consume(n, this.data!, this.dataAt);
                this.dataAt += n;
                if (this.dataAt < head.size) break;
            }
            const trailer = this.peek(ENTRY_TRAILER);
            if (!trailer) break;
            const sha256 = trailer.slice();
            this.consume(ENTRY_TRAILER);
            out.push({ kind: head.kind, name: head.name, meta: head.meta, data: this.data!, sha256, start: head.start, end: this.pos });
            this.head = null;
            this.data = null;
        }
        return out;
    }
}

export async function entryIntact(e: Entry): Promise<boolean> {
    const d = new Uint8Array(await crypto.subtle.digest("SHA-256", e.data));
    return crypto.subtle.timingSafeEqual(d, e.sha256);
}
