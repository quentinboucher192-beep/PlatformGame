// Types of challenge-sql.mjs (imported by the server tests).
export interface ChallengeSqlRules {
    hearts: number;
    slots: number;
    powers: string[];
    stations: boolean;
    checkpoints: boolean;
}
export interface ChallengeSqlOptions {
    kind?: string;
    period?: string;
    date?: string;
    level?: string;
    hearts?: string | number;
    slots?: string | number;
    powers?: string;
    silver?: string;
    gold?: string;
    stations?: boolean;
    checkpoints?: boolean;
    goldNoDamage?: boolean;
}
export interface ChallengeSqlCatalog {
    player_max_hearts: number;
    powers: Record<string, string>;
    levels: Record<string, { slots: number; start_powers: string[]; silver_ms: number; gold_ms: number }>;
}
export const KINDS: string[];
export const MAX_SLOTS: number;
export const MAX_HEARTS: number;
export function canonicalRules(challengeId: string, r: ChallengeSqlRules): string;
export function sha256Hex(text: string): Promise<string>;
export function parseTime(s: string): number;
export function isoWeek(date: string): string;
export function periodBounds(kind: string, period: string): { start: string; end: string } | null;
export function resolvePeriod(opts: ChallengeSqlOptions): { kind: string; period: string; start: string; end: string };
export function parseArgs(argv: string[]): ChallengeSqlOptions;
export function buildChallengeSql(
    opts: ChallengeSqlOptions,
    catalog: ChallengeSqlCatalog,
    today: string,
): Promise<{
    sql: string;
    id: string;
    kind: string;
    period: string;
    start: string;
    end: string;
    rules: ChallengeSqlRules;
    rulesHash: string;
    canonical: string;
    warnings: string[];
}>;
