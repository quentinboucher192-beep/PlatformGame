// Strict validation of client input. Every route validates its whole input with this
// helper: unknown fields are refused, types and ranges are checked, strings are bounded.

import { ApiError } from "./http";

export interface FieldError {
    field: string;
    message: string;
}

type Obj = Record<string, unknown>;

export class Validator {
    readonly errors: FieldError[] = [];

    constructor(
        private readonly obj: Obj,
        allowed: readonly string[],
    ) {
        for (const key of Object.keys(obj)) if (!allowed.includes(key)) this.fail(key, "champ inconnu");
    }

    fail(field: string, message: string): void {
        this.errors.push({ field, message });
    }

    has(field: string): boolean {
        return this.obj[field] !== undefined && this.obj[field] !== null;
    }

    str(field: string, opt: { min?: number; max: number; pattern?: RegExp; optional?: boolean; trim?: boolean }): string | undefined {
        const v = this.obj[field];
        if (v === undefined || v === null) {
            if (!opt.optional) this.fail(field, "obligatoire");
            return undefined;
        }
        if (typeof v !== "string") {
            this.fail(field, "doit être une chaîne");
            return undefined;
        }
        const s = opt.trim ? v.trim() : v;
        // Control characters are never legitimate in identifiers or names.
        if (/[\u0000-\u001f\u007f]/.test(s)) {
            this.fail(field, "caractères de contrôle interdits");
            return undefined;
        }
        if (s.length < (opt.min ?? 0) || s.length > opt.max) {
            this.fail(field, `longueur attendue entre ${opt.min ?? 0} et ${opt.max}`);
            return undefined;
        }
        if (opt.pattern && !opt.pattern.test(s)) {
            this.fail(field, "format invalide");
            return undefined;
        }
        return s;
    }

    int(field: string, opt: { min: number; max: number; optional?: boolean }): number | undefined {
        const v = this.obj[field];
        if (v === undefined || v === null) {
            if (!opt.optional) this.fail(field, "obligatoire");
            return undefined;
        }
        if (typeof v !== "number" || !Number.isSafeInteger(v)) {
            this.fail(field, "doit être un entier");
            return undefined;
        }
        if (v < opt.min || v > opt.max) {
            this.fail(field, `doit être entre ${opt.min} et ${opt.max}`);
            return undefined;
        }
        return v;
    }

    bool(field: string, opt: { optional?: boolean } = {}): boolean | undefined {
        const v = this.obj[field];
        if (v === undefined || v === null) {
            if (!opt.optional) this.fail(field, "obligatoire");
            return undefined;
        }
        if (typeof v !== "boolean") {
            this.fail(field, "doit être un booléen");
            return undefined;
        }
        return v;
    }

    oneOf<T extends string>(field: string, values: readonly T[], opt: { optional?: boolean } = {}): T | undefined {
        const v = this.obj[field];
        if (v === undefined || v === null) {
            if (!opt.optional) this.fail(field, "obligatoire");
            return undefined;
        }
        if (typeof v !== "string" || !(values as readonly string[]).includes(v)) {
            this.fail(field, `valeur attendue : ${values.join(", ")}`);
            return undefined;
        }
        return v as T;
    }

    /** Throws a 400 VALIDATION_ERROR listing every problem. */
    done(): void {
        if (this.errors.length > 0) throw new ApiError(400, "VALIDATION_ERROR", "Données invalides.", this.errors);
    }
}

// ------------------------------------------------------------------ shared rules

export const CATEGORIES = ["ANY_PERCENT", "HUNDRED_PERCENT", "NO_DEATH", "NO_DAMAGE", "SPEEDRUN"] as const;
export type Category = (typeof CATEGORIES)[number];
export const MODES = ["STANDARD"] as const;
export const LOCATION_VISIBILITY = ["hidden", "country", "globe"] as const;
export const PROFILE_VISIBILITY = ["public", "private"] as const;

export const USERNAME_RE = /^[A-Za-z0-9](?:[A-Za-z0-9_.-]{1,18})[A-Za-z0-9]$/;
export const LEVEL_ID_RE = /^[a-z0-9_]{1,64}$/;
export const PLANET_ID_RE = /^[a-z0-9_]{1,64}$/;
export const HASH_RE = /^[0-9a-f]{64}$/;
export const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
export const SEMVER_RE = /^\d{1,3}\.\d{1,3}\.\d{1,4}$/;
export const COUNTRY_RE = /^[A-Z]{2}$/;
export const REGION_RE = /^[A-Z]{2}-[A-Z0-9]{1,3}$/;
export const AVATAR_RE = /^avatar_(0[1-9]|1[0-6])$/;

const RESERVED = ["admin", "administrator", "moderator", "modo", "cosmic", "cosmicstudio", "system", "support", "root", "null", "undefined", "anonymous", "staff"];

export function normalizeUsername(name: string): string {
    return name.toLowerCase();
}

export function isReservedUsername(name: string): boolean {
    const n = normalizeUsername(name).replace(/[_.-]/g, "");
    return RESERVED.some((r) => n === r || n.startsWith(r + "team") || n.startsWith("official"));
}

/** Compares "a.b.c" versions numerically. */
export function compareVersions(a: string, b: string): number {
    const pa = a.split(".").map(Number);
    const pb = b.split(".").map(Number);
    for (let i = 0; i < 3; ++i) {
        const d = (pa[i] ?? 0) - (pb[i] ?? 0);
        if (d !== 0) return d < 0 ? -1 : 1;
    }
    return 0;
}

export interface Paging {
    limit: number;
    offset: number;
}

/** limit (1..max, default 25) and offset (0..10000) from the query string. */
export function paging(url: URL, defLimit = 25, maxLimit = 100): Paging {
    const errors: FieldError[] = [];
    const parse = (name: string, def: number, min: number, max: number): number => {
        const raw = url.searchParams.get(name);
        if (raw === null || raw === "") return def;
        if (!/^\d{1,6}$/.test(raw)) {
            errors.push({ field: name, message: "doit être un entier positif" });
            return def;
        }
        const n = Number(raw);
        if (n < min || n > max) errors.push({ field: name, message: `doit être entre ${min} et ${max}` });
        return Math.min(Math.max(n, min), max);
    };
    const limit = parse("limit", defLimit, 1, maxLimit);
    const offset = parse("offset", 0, 0, 10000);
    if (errors.length) throw new ApiError(400, "VALIDATION_ERROR", "Paramètres de pagination invalides.", errors);
    return { limit, offset };
}

/** Optional enum query parameter. */
export function queryEnum<T extends string>(url: URL, name: string, values: readonly T[], def: T): T {
    const raw = url.searchParams.get(name);
    if (raw === null || raw === "") return def;
    if (!(values as readonly string[]).includes(raw))
        throw new ApiError(400, "VALIDATION_ERROR", "Paramètre invalide.", [
            { field: name, message: `valeur attendue : ${values.join(", ")}` },
        ]);
    return raw as T;
}

/** Optional pattern-checked query parameter. */
export function queryString(url: URL, name: string, re: RegExp): string | undefined {
    const raw = url.searchParams.get(name);
    if (raw === null || raw === "") return undefined;
    if (!re.test(raw))
        throw new ApiError(400, "VALIDATION_ERROR", "Paramètre invalide.", [{ field: name, message: "format invalide" }]);
    return raw;
}

/** Escapes LIKE wildcards (used with `ESCAPE '\'`). */
export function likePrefix(s: string): string {
    return s.replace(/[\\%_]/g, (c) => "\\" + c) + "%";
}
