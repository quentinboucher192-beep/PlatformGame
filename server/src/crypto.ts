// Web Crypto helpers: password hashing (PBKDF2-HMAC-SHA256), opaque tokens, digests and
// constant-time comparisons. Tokens come from crypto.getRandomValues, never Math.random.

const encoder = new TextEncoder();

/** Workers caps PBKDF2 at 100 000 iterations. Each hash stores its own iteration count. */
export const DEFAULT_PBKDF2_ITERATIONS = 100_000;

export function toBase64(bytes: Uint8Array): string {
    let s = "";
    for (const b of bytes) s += String.fromCharCode(b);
    return btoa(s);
}

export function fromBase64(s: string): Uint8Array {
    const bin = atob(s);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; ++i) out[i] = bin.charCodeAt(i);
    return out;
}

export function toBase64Url(bytes: Uint8Array): string {
    return toBase64(bytes).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export function toHex(bytes: Uint8Array): string {
    let s = "";
    for (const b of bytes) s += b.toString(16).padStart(2, "0");
    return s;
}

export function randomBytes(n: number): Uint8Array {
    const b = new Uint8Array(n);
    crypto.getRandomValues(b);
    return b;
}

/** 256-bit opaque token, URL-safe. */
export function randomToken(): string {
    return toBase64Url(randomBytes(32));
}

export async function sha256(data: string | Uint8Array): Promise<Uint8Array> {
    const bytes = typeof data === "string" ? encoder.encode(data) : data;
    return new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
}

export async function sha256Hex(data: string | Uint8Array): Promise<string> {
    return toHex(await sha256(data));
}

export function timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean {
    if (a.byteLength !== b.byteLength) return false;
    return crypto.subtle.timingSafeEqual(a, b);
}

/** Constant-time comparison of two secrets of arbitrary length (compares their digests). */
export async function secretsEqual(a: string, b: string): Promise<boolean> {
    const [da, db] = await Promise.all([sha256(a), sha256(b)]);
    return timingSafeEqual(da, db);
}

async function pbkdf2(password: string, salt: Uint8Array, iterations: number): Promise<Uint8Array> {
    const key = await crypto.subtle.importKey("raw", encoder.encode(password), "PBKDF2", false, ["deriveBits"]);
    const bits = await crypto.subtle.deriveBits({ name: "PBKDF2", hash: "SHA-256", salt, iterations }, key, 256);
    return new Uint8Array(bits);
}

export interface PasswordHash {
    hash: string;
    salt: string;
    iterations: number;
}

export async function hashPassword(password: string, iterations = DEFAULT_PBKDF2_ITERATIONS): Promise<PasswordHash> {
    const salt = randomBytes(16);
    const hash = await pbkdf2(password, salt, iterations);
    return { hash: toBase64(hash), salt: toBase64(salt), iterations };
}

export async function verifyPassword(password: string, stored: PasswordHash): Promise<boolean> {
    const derived = await pbkdf2(password, fromBase64(stored.salt), stored.iterations);
    return timingSafeEqual(derived, fromBase64(stored.hash));
}

/** Burns the same CPU time as a real verification (unknown user: no timing oracle). */
export async function dummyPasswordCheck(password: string, iterations: number): Promise<void> {
    await pbkdf2(password, new Uint8Array(16), iterations);
}

/** Hash of the client IP with a server-side salt: rate limits and audit logs never store raw IPs. */
export async function hashIp(ip: string, salt: string): Promise<string> {
    return (await sha256Hex(`${salt}|${ip}`)).slice(0, 32);
}
