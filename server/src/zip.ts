// Minimal, defensive ZIP reader for the .cplanet packages uploaded by players: central
// directory only (no streaming, no ZIP64, no encryption), stored or deflated entries, hard
// limits on sizes, entry counts and compression ratio, and path checks (no absolute path,
// no "..", no backslash, no drive letter, no control character).

export class ZipError extends Error {}

export interface ZipEntry {
    name: string;
    method: number; // 0 stored, 8 deflate
    compressedSize: number;
    size: number;
    localOffset: number;
}

export function safeEntryName(name: string): boolean {
    if (name.length === 0 || name.length > 300) return false;
    if (name.startsWith("/") || name.includes("\\") || /^[A-Za-z]:/.test(name)) return false;
    // eslint-disable-next-line no-control-regex
    if (/[\u0000-\u001f\u007f]/.test(name)) return false;
    return !name.split("/").some((part) => part === ".." || part === ".");
}

export function listZip(data: Uint8Array, maxEntries: number): ZipEntry[] {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    // End of central directory: last 22 bytes + comment (max 64 KB).
    let eocd = -1;
    for (let i = data.byteLength - 22; i >= Math.max(0, data.byteLength - 22 - 65535); --i)
        if (view.getUint32(i, true) === 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) throw new ZipError("archive ZIP invalide (fin de répertoire introuvable)");
    const count = view.getUint16(eocd + 10, true);
    const cdSize = view.getUint32(eocd + 12, true);
    const cdOffset = view.getUint32(eocd + 16, true);
    if (count > maxEntries) throw new ZipError("trop de fichiers dans l'archive");
    if (cdOffset + cdSize > eocd) throw new ZipError("répertoire central incohérent");
    const entries: ZipEntry[] = [];
    let at = cdOffset;
    const decoder = new TextDecoder("utf-8", { fatal: true, ignoreBOM: false });
    for (let k = 0; k < count; ++k) {
        if (at + 46 > eocd || view.getUint32(at, true) !== 0x02014b50) throw new ZipError("entrée du répertoire central invalide");
        const flags = view.getUint16(at + 8, true);
        const method = view.getUint16(at + 10, true);
        const compressedSize = view.getUint32(at + 20, true);
        const size = view.getUint32(at + 24, true);
        const nameLen = view.getUint16(at + 28, true);
        const extraLen = view.getUint16(at + 30, true);
        const commentLen = view.getUint16(at + 32, true);
        const localOffset = view.getUint32(at + 42, true);
        if (flags & 0x1) throw new ZipError("fichier chiffré refusé");
        if (method !== 0 && method !== 8) throw new ZipError("méthode de compression non prise en charge");
        let name: string;
        try {
            name = decoder.decode(data.subarray(at + 46, at + 46 + nameLen));
        } catch {
            throw new ZipError("nom de fichier non UTF-8");
        }
        entries.push({ name, method, compressedSize, size, localOffset });
        at += 46 + nameLen + extraLen + commentLen;
    }
    return entries;
}

/** Contents of one entry, inflated with a hard cap (its declared size). */
export async function readEntry(data: Uint8Array, e: ZipEntry): Promise<Uint8Array> {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    if (e.localOffset + 30 > data.byteLength || view.getUint32(e.localOffset, true) !== 0x04034b50) throw new ZipError("en-tête local invalide");
    const nameLen = view.getUint16(e.localOffset + 26, true);
    const extraLen = view.getUint16(e.localOffset + 28, true);
    const start = e.localOffset + 30 + nameLen + extraLen;
    if (start + e.compressedSize > data.byteLength) throw new ZipError("données tronquées");
    const raw = data.subarray(start, start + e.compressedSize);
    if (e.method === 0) {
        if (e.compressedSize !== e.size) throw new ZipError("taille incohérente");
        return raw;
    }
    const reader = new Blob([raw]).stream().pipeThrough(new DecompressionStream("deflate-raw")).getReader();
    const out = new Uint8Array(e.size);
    let at = 0;
    try {
        for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            if (at + value.byteLength > e.size) {
                await reader.cancel();
                throw new ZipError("données décompressées plus longues qu'annoncé");
            }
            out.set(value, at);
            at += value.byteLength;
        }
    } catch (err) {
        if (err instanceof ZipError) throw err;
        throw new ZipError("flux compressé corrompu");
    }
    if (at !== e.size) throw new ZipError("données décompressées tronquées");
    return out;
}
