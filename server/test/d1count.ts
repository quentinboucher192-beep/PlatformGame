// Phase 7 of the raids (2.74): a D1 database that counts its round trips and records its statements,
// with hooks that run something just before a given statement (a concurrent request, a close: the
// races of the load and anti-cheat tests, made deterministic), and a request context to call a route
// directly with it.

import { env } from "cloudflare:test";
import type { Ctx } from "../src/http";
import { freshIp } from "./helpers";

export interface Recorded {
    sql: string;
    binds: unknown[];
}
export interface D1Stats {
    /** Round trips: each first / all / run / raw, each batch. */
    calls: number;
    /** Statements, those of the batches included. */
    statements: number;
    queries: Recorded[];
}
export interface Hook {
    match: RegExp;
    run: () => Promise<void>;
    done?: boolean;
}

export function countingDb(db: D1Database, hooks: Hook[] = []): { db: D1Database; stats: D1Stats } {
    const stats: D1Stats = { calls: 0, statements: 0, queries: [] };
    const real = new WeakMap<object, D1PreparedStatement>();
    const meta = new WeakMap<object, Recorded>();
    const fire = async (sql: string) => {
        for (const h of hooks)
            if (!h.done && h.match.test(sql)) {
                h.done = true;
                await h.run();
            }
    };
    const wrap = (s: D1PreparedStatement, rec: Recorded): D1PreparedStatement => {
        const one = async <T>(f: () => Promise<T>): Promise<T> => {
            stats.calls++;
            stats.statements++;
            stats.queries.push(rec);
            await fire(rec.sql);
            return f();
        };
        const w = {
            bind: (...a: unknown[]) => wrap(s.bind(...a), { sql: rec.sql, binds: a }),
            first: (col?: string) => one(() => (col === undefined ? s.first() : s.first(col))),
            all: () => one(() => s.all()),
            run: () => one(() => s.run()),
            raw: (o?: { columnNames?: boolean }) => one(() => s.raw(o as never)),
        };
        real.set(w, s);
        meta.set(w, rec);
        return w as unknown as D1PreparedStatement;
    };
    const proxy = {
        prepare: (sql: string) => wrap(db.prepare(sql), { sql, binds: [] }),
        batch: async (list: D1PreparedStatement[]) => {
            stats.calls++;
            stats.statements += list.length;
            for (const x of list) {
                const rec = meta.get(x);
                if (rec) {
                    stats.queries.push(rec);
                    await fire(rec.sql);
                }
            }
            return db.batch(list.map((x) => real.get(x) ?? x));
        },
        exec: (q: string) => {
            stats.calls++;
            return db.exec(q);
        },
        dump: () => db.dump(),
        withSession: () => proxy,
    };
    return { db: proxy as unknown as D1Database, stats };
}

/** A request context for a route called directly (its D1 given), and what it left to waitUntil. */
export function directCtx(opts: { method: string; path: string; params: Record<string, string>; token?: string; body?: unknown; db: D1Database }) {
    const url = new URL("http://cosmic.test" + opts.path);
    const ip = freshIp();
    const headers = new Headers({ "cf-connecting-ip": ip });
    if (opts.body !== undefined) headers.set("content-type", "application/json");
    if (opts.token) headers.set("authorization", "Bearer " + opts.token);
    const request = new Request(url, { method: opts.method, headers, body: opts.body === undefined ? undefined : JSON.stringify(opts.body) });
    const waits: Promise<unknown>[] = [];
    const exec = { waitUntil: (p: Promise<unknown>) => void waits.push(p), passThroughOnException() {}, props: {} } as unknown as ExecutionContext;
    const ctx: Ctx = { request, env: { ...env, DB: opts.db } as unknown as Ctx["env"], exec, url, params: opts.params, requestId: crypto.randomUUID(), ip };
    return { ctx, waits };
}

/** The body of a route's JSON answer. */
export async function bodyOf(res: Response): Promise<any> {
    return ((await res.json()) as { data: unknown }).data;
}

/**
 * The plan of each distinct statement recorded (EXPLAIN QUERY PLAN with its own values): the
 * statements that read a whole raid table (« SCAN raid_… » without an index).
 */
export async function fullScans(queries: Recorded[], tables: RegExp): Promise<string[]> {
    const seen = new Set<string>();
    const out: string[] = [];
    for (const q of queries) {
        const sql = q.sql.trim();
        if (seen.has(sql) || !/^(SELECT|INSERT|UPDATE|DELETE|WITH)/i.test(sql)) continue;
        seen.add(sql);
        let plan: { detail: string }[] = [];
        try {
            plan = (await env.DB.prepare("EXPLAIN QUERY PLAN " + sql).bind(...q.binds).all<{ detail: string }>()).results;
        } catch {
            continue; // a statement D1 does not explain (none in the raids)
        }
        // The plan names a table by its alias (« SCAN c »): the aliases of the statement, to their tables.
        const alias = new Map<string, string>();
        const KEYWORDS = /^(WHERE|ON|LEFT|INNER|JOIN|GROUP|ORDER|LIMIT|USING|SET|VALUES|SELECT|AND|OR|UNION|CROSS)$/i;
        for (const m of sql.matchAll(/\b(?:FROM|JOIN|INTO|UPDATE)\s+(\w+)(?:\s+(?:AS\s+)?(\w+))?/gi)) {
            alias.set(m[1]!, m[1]!);
            if (m[2] && !KEYWORDS.test(m[2])) alias.set(m[2], m[1]!);
        }
        for (const p of plan) {
            const m = /^SCAN (\w+)/.exec(p.detail);
            const table = m ? (alias.get(m[1]!) ?? m[1]!) : "";
            if (m && tables.test(table)) out.push(`${table} (${p.detail}) ← ${sql.replace(/\s+/g, " ").slice(0, 200)}`);
        }
    }
    return out;
}
