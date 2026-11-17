// Types of challengeSpec.mjs (imported by src/adminChallenges.ts, scripts/challenge-sql.mjs and the tests).
export type SpecKind = "daily" | "weekly" | "monthly";
export type SpecObjective = "speed" | "finish";

export interface SpecRules {
    hearts: number;
    slots: number;
    powers: string[];
    stations: boolean;
    checkpoints: boolean;
}

export interface SpecLevel {
    source: string;
    id: string;
    version?: number | string | null;
}

export interface ChallengeSpec {
    kind: string;
    period: string;
    level: SpecLevel;
    hearts?: number | string | null;
    slots?: number | string | null;
    powers?: string[] | null;
    stations?: boolean | null;
    checkpoints?: boolean | null;
    objective?: string | null;
    silver_ms?: number | null;
    gold_ms?: number | null;
    gold_no_damage?: boolean | null;
    bronze_progress?: number | string | null;
    silver_progress?: number | string | null;
}

export interface SpecCatalog {
    player_max_hearts: number;
    powers: Record<string, string>;
    levels: Record<string, { slots: number; start_powers: string[]; silver_ms: number; gold_ms: number }>;
}

export interface ValidatedChallenge {
    id: string;
    kind: SpecKind;
    period: string;
    start: string;
    end: string;
    level: { source: "campaign" | "community"; id: string; version: number | null };
    rules: SpecRules;
    objective: SpecObjective;
    silver_ms: number;
    gold_ms: number;
    gold_no_damage: boolean;
    bronze_progress: number;
    silver_progress: number;
    warnings: string[];
}

export const MAX_SLOTS: number;
export const MAX_HEARTS: number;
export const KINDS: SpecKind[];
export const OBJECTIVES: SpecObjective[];
export const PROGRESS_MIN: number;
export const PROGRESS_MAX: number;
export const DEFAULT_BRONZE_PROGRESS: number;
export const DEFAULT_SILVER_PROGRESS: number;
export const MAX_TIER_MS: number;
export const FINISH_TIER_MS: number;

export class ChallengeSpecError extends Error {
    constructor(field: string, message: string);
    readonly field: string;
}

export function canonicalRules(challengeId: string, r: SpecRules): string;
export function sha256Hex(text: string): Promise<string>;
export function isDate(s: string): boolean;
export function addDays(date: string, n: number): string;
export function addMonths(month: string, n: number): string;
export function isoWeek(date: string): string;
export function periodBounds(kind: string, period: string): { start: string; end: string } | null;
export function parseTime(s: string): number;
export function formatTime(ms: number): string;
/** `options.running` (2.60): a running period is accepted too (never one that is over). */
export function validateChallengeSpec(spec: ChallengeSpec, catalog: SpecCatalog, today: string, options?: { running?: boolean }): ValidatedChallenge;
