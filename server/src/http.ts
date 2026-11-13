// HTTP plumbing shared by every route: request context, homogeneous JSON envelopes,
// errors that never leak internal details, and size-limited body readers.

/** Bindings plus the optional secrets that `wrangler types` does not list as required. */
export type Bindings = Env & {
    ADMIN_TOKEN?: string;
    /** Seconds anonymous GET responses may be cached at the edge ("0" disables it). */
    EDGE_CACHE_SECONDS?: string;
    PBKDF2_ITERATIONS?: string;
};

export interface Ctx {
    request: Request;
    env: Bindings;
    exec: ExecutionContext;
    url: URL;
    params: Record<string, string>;
    requestId: string;
    /** Client IP as seen by Cloudflare. Only ever stored hashed. */
    ip: string;
}

export class ApiError extends Error {
    constructor(
        public readonly status: number,
        public readonly code: string,
        message: string,
        public readonly details?: unknown,
        public readonly headers?: Record<string, string>,
    ) {
        super(message);
    }
}

export const Errors = {
    validation: (details: unknown) => new ApiError(400, "VALIDATION_ERROR", "Données invalides.", details),
    unauthorized: (code = "UNAUTHORIZED", message = "Authentification requise.") => new ApiError(401, code, message),
    forbidden: (code = "FORBIDDEN", message = "Action non autorisée.") => new ApiError(403, code, message),
    notFound: (code = "NOT_FOUND", message = "Ressource introuvable.") => new ApiError(404, code, message),
    conflict: (code: string, message: string, details?: unknown) => new ApiError(409, code, message, details),
};

export interface PageMeta {
    limit: number;
    offset: number;
    total: number;
    next_offset: number | null;
}

const BASE_HEADERS: Record<string, string> = {
    "content-type": "application/json; charset=utf-8",
    "x-content-type-options": "nosniff",
    "referrer-policy": "no-referrer",
};

export function json(
    ctx: Pick<Ctx, "requestId">,
    data: unknown,
    init: { status?: number; meta?: unknown; headers?: Record<string, string> } = {},
): Response {
    const body: Record<string, unknown> = { ok: true, data };
    if (init.meta !== undefined) body.meta = init.meta;
    body.request_id = ctx.requestId;
    return new Response(JSON.stringify(body), {
        status: init.status ?? 200,
        headers: { ...BASE_HEADERS, "cache-control": "no-store", ...init.headers },
    });
}

export function errorResponse(requestId: string, err: ApiError): Response {
    const error: Record<string, unknown> = { code: err.code, message: err.message };
    if (err.details !== undefined) error.details = err.details;
    return new Response(JSON.stringify({ ok: false, error, request_id: requestId }), {
        status: err.status,
        headers: { ...BASE_HEADERS, "cache-control": "no-store", ...err.headers },
    });
}

/** Reads at most `maxBytes` of the body, failing early on a declared or actual overflow. */
export async function readLimited(request: Request, maxBytes: number): Promise<Uint8Array> {
    const declared = request.headers.get("content-length");
    if (declared !== null) {
        const n = Number(declared);
        if (!Number.isFinite(n) || n < 0) throw new ApiError(400, "BAD_REQUEST", "En-tête Content-Length invalide.");
        if (n > maxBytes) throw tooLarge(maxBytes);
    }
    if (!request.body) return new Uint8Array(0);
    const reader = request.body.getReader();
    const chunks: Uint8Array[] = [];
    let total = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        total += value.byteLength;
        if (total > maxBytes) {
            await reader.cancel();
            throw tooLarge(maxBytes);
        }
        chunks.push(value);
    }
    const out = new Uint8Array(total);
    let at = 0;
    for (const c of chunks) {
        out.set(c, at);
        at += c.byteLength;
    }
    return out;
}

function tooLarge(maxBytes: number): ApiError {
    return new ApiError(413, "PAYLOAD_TOO_LARGE", `Requête trop volumineuse (maximum ${maxBytes} octets).`);
}

/** Parses a JSON object body (Content-Type application/json, size-limited). */
export async function readJsonObject(ctx: Ctx, maxBytes = 8 * 1024): Promise<Record<string, unknown>> {
    const type = ctx.request.headers.get("content-type") ?? "";
    if (!type.toLowerCase().startsWith("application/json"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le corps doit être du JSON (application/json).");
    const bytes = await readLimited(ctx.request, maxBytes);
    let value: unknown;
    try {
        value = JSON.parse(new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(bytes));
    } catch {
        throw new ApiError(400, "INVALID_JSON", "Le corps de la requête n'est pas un JSON valide.");
    }
    if (value === null || typeof value !== "object" || Array.isArray(value))
        throw new ApiError(400, "INVALID_JSON", "Un objet JSON est attendu.");
    return value as Record<string, unknown>;
}

export function nowIso(): string {
    return new Date().toISOString();
}

/** Monday 00:00 UTC of the week containing `d` (weekly leaderboards reset logically). */
export function weekStart(d: Date = new Date()): Date {
    const day = (d.getUTCDay() + 6) % 7; // 0 = Monday
    return new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate() - day));
}

/** Structured JSON log line (searchable in Workers Logs). Never pass tokens or passwords. */
export function log(level: "info" | "warn" | "error", message: string, fields: Record<string, unknown> = {}): void {
    const line = JSON.stringify({ level, message, ...fields });
    if (level === "error") console.error(line);
    else if (level === "warn") console.warn(line);
    else console.log(line);
}
