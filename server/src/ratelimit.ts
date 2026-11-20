// Fixed-window rate limiting stored in D1 (one atomic UPSERT per check). Keys are hashed,
// so neither IPs nor usernames are stored in clear.

import { hashIp } from "./crypto";
import { ApiError, type Ctx } from "./http";

export interface RateRule {
    name: string;
    limit: number;
    windowSeconds: number;
}

export const RULES = {
    register: { name: "register", limit: 5, windowSeconds: 3600 },
    usernameCheck: { name: "username_check", limit: 60, windowSeconds: 300 },
    loginIp: { name: "login_ip", limit: 30, windowSeconds: 900 },
    loginUser: { name: "login_user", limit: 20, windowSeconds: 900 },
    refresh: { name: "refresh", limit: 60, windowSeconds: 60 },
    submit: { name: "submit", limit: 30, windowSeconds: 60 },
    replayUpload: { name: "replay_upload", limit: 20, windowSeconds: 60 },
    profileWrite: { name: "profile_write", limit: 20, windowSeconds: 300 },
    friendRequest: { name: "friend_request", limit: 30, windowSeconds: 3600 },
    presence: { name: "presence", limit: 6, windowSeconds: 60 },
    // ADMINISTRATION (2.40): role changes and, later, the calendar of the challenges.
    adminWrite: { name: "admin_write", limit: 60, windowSeconds: 300 },
    // Backups (ADMINISTRATION › Sauvegardes): copies, downloads, the parts of an imported file
    // (1 Gio = 128 parts of 8 Mio), its checks and the steps of its apply.
    backupCreate: { name: "backup_create", limit: 6, windowSeconds: 3600 },
    backupDownload: { name: "backup_download", limit: 10, windowSeconds: 3600 },
    backupUpload: { name: "backup_upload", limit: 400, windowSeconds: 3600 },
    backupCheck: { name: "backup_check", limit: 20, windowSeconds: 3600 },
    backupApply: { name: "backup_apply", limit: 600, windowSeconds: 3600 },
    reads: { name: "reads", limit: 300, windowSeconds: 60 },
    accountDelete: { name: "account_delete", limit: 5, windowSeconds: 3600 },
    socialMessage: { name: "social_message", limit: 30, windowSeconds: 60 },
    socialGroup: { name: "social_group", limit: 10, windowSeconds: 3600 },
    socialReport: { name: "social_report", limit: 20, windowSeconds: 3600 },
    socialBlock: { name: "social_block", limit: 30, windowSeconds: 3600 },
    communityPublish: { name: "community_publish", limit: 10, windowSeconds: 3600 },
    communityUpload: { name: "community_upload", limit: 40, windowSeconds: 3600 },
    communityDownload: { name: "community_download", limit: 120, windowSeconds: 3600 },
    clientDownload: { name: "client_download", limit: 6, windowSeconds: 3600 },
    communityRate: { name: "community_rate", limit: 60, windowSeconds: 3600 },
    communityReport: { name: "community_report", limit: 20, windowSeconds: 3600 },
    communityRecord: { name: "community_record", limit: 30, windowSeconds: 60 },
    communityFavorite: { name: "community_favorite", limit: 120, windowSeconds: 3600 },
    communityComment: { name: "community_comment", limit: 8, windowSeconds: 600 },
    communityReaction: { name: "community_reaction", limit: 120, windowSeconds: 600 },
    // Ratings of the official levels (2.19); their comments share community_comment.
    levelRate: { name: "level_rate", limit: 60, windowSeconds: 3600 },
    follow: { name: "follow", limit: 60, windowSeconds: 3600 },
    playtime: { name: "playtime", limit: 40, windowSeconds: 3600 },
    achievements: { name: "achievements", limit: 30, windowSeconds: 3600 },
    // 2.47: the copy of the progression (after the runs, at most every 10 minutes, and on demand).
    progressCopy: { name: "progress_copy", limit: 12, windowSeconds: 3600 },
    // 2.47: the right password typed on another computer than the account's: refused; after 5, that computer waits.
    loginRefused: { name: "login_refused", limit: 5, windowSeconds: 600 },
    deviceRename: { name: "device_rename", limit: 10, windowSeconds: 3600 },
    // 2.50: « Migrer vers un nouveau compte ». The code made on the old computer; the failed
    // attempts with it (wrong username, password or code: one generic answer), counted per new
    // account and per old account (assertUnderLimit / countFailure: only failures count); the
    // choice about the progression to restore.
    migrationCode: { name: "migration_code", limit: 10, windowSeconds: 3600 },
    migrationFailTarget: { name: "migration_fail_target", limit: 5, windowSeconds: 600 },
    migrationFailSource: { name: "migration_fail_source", limit: 5, windowSeconds: 600 },
    progressRestore: { name: "progress_restore", limit: 30, windowSeconds: 3600 },
    // What the player wears online (2.23): sent after each « Équiper » of the Collection.
    cosmetics: { name: "cosmetics", limit: 60, windowSeconds: 3600 },
    // The badges of the profile (2.41): sent after each choice of the Collection, like the items worn.
    badges: { name: "badges", limit: 60, windowSeconds: 3600 },
    projectCreate: { name: "project_create", limit: 10, windowSeconds: 3600 },
    projectInvite: { name: "project_invite", limit: 30, windowSeconds: 3600 },
    projectJoin: { name: "project_join", limit: 20, windowSeconds: 3600 },
    projectSave: { name: "project_save", limit: 120, windowSeconds: 3600 },
    projectChat: { name: "project_chat", limit: 30, windowSeconds: 60 },
    raceSubmit: { name: "race_submit", limit: 240, windowSeconds: 3600 },
    // Runs of the daily challenges (2.31), like the records; their replays share replay_upload.
    challengeSubmit: { name: "challenge_submit", limit: 30, windowSeconds: 60 },
    // Clans (2.66): creations; applications and invitations sent; the other changes (settings,
    // roles, answers, exclusions, leaving). Their discussion shares social_message.
    clanCreate: { name: "clan_create", limit: 5, windowSeconds: 3600 },
    clanApply: { name: "clan_apply", limit: 30, windowSeconds: 3600 },
    clanWrite: { name: "clan_write", limit: 120, windowSeconds: 3600 },
    // Clan raids (raids.ts): registrations and answers, the calls to its members. 2.77.4: a clan no longer
    // schedules raids (raid_schedule removed); the reviews of the raids share community_comment / community_report.
    raidWrite: { name: "raid_write", limit: 120, windowSeconds: 3600 },
    // 2.65: the reports of a raid instance (objectives, zones): a few per minute in a real game.
    raidReport: { name: "raid_report", limit: 600, windowSeconds: 600 },
    raidCall: { name: "raid_call", limit: 6, windowSeconds: 3600 },
    // 2.68: claiming the rewards of the raids (idempotent; a few per event).
    raidClaim: { name: "raid_claim", limit: 60, windowSeconds: 600 },
} as const satisfies Record<string, RateRule>;

/** RATE_LIMIT_SCALE multiplies every limit (development only: repeated local test runs). */
function scale(ctx: Ctx): number {
    const n = Number((ctx.env as { RATE_LIMIT_SCALE?: string }).RATE_LIMIT_SCALE ?? "1");
    return Number.isFinite(n) && n >= 1 ? Math.min(n, 1000) : 1;
}

function windowOf(rule: RateRule): { now: number; windowStart: number } {
    const now = Math.floor(Date.now() / 1000);
    return { now, windowStart: now - (now % rule.windowSeconds) };
}

async function bucketOf(ctx: Ctx, rule: RateRule, key: string): Promise<string> {
    return `${rule.name}:${await hashIp(key, ctx.env.IP_HASH_SALT ?? "")}`;
}

function limited(rule: RateRule, now: number, windowStart: number): ApiError {
    const retry = Math.max(1, windowStart + rule.windowSeconds - now);
    return new ApiError(429, "RATE_LIMITED", "Trop de requêtes, réessayez plus tard.", { retry_after_seconds: retry }, {
        "retry-after": String(retry),
    });
}

/** Counts one use of `key` in the current window; the new count. */
async function bump(ctx: Ctx, rule: RateRule, key: string, windowStart: number): Promise<number> {
    const row = await ctx.env.DB.prepare(
        `INSERT INTO rate_limits (bucket, window_start, count) VALUES (?1, ?2, 1)
         ON CONFLICT (bucket) DO UPDATE SET
           count = CASE WHEN rate_limits.window_start = ?2 THEN rate_limits.count + 1 ELSE 1 END,
           window_start = ?2
         RETURNING count`,
    )
        .bind(await bucketOf(ctx, rule, key), windowStart)
        .first<{ count: number }>();
    return row?.count ?? 0;
}

export async function rateLimit(ctx: Ctx, rule: RateRule, key: string): Promise<void> {
    const { now, windowStart } = windowOf(rule);
    if ((await bump(ctx, rule, key, windowStart)) > rule.limit * scale(ctx)) throw limited(rule, now, windowStart);
}

/**
 * Rules that count FAILURES only (2.50, the migration code): 429 when `key` already failed
 * `rule.limit` times in this window, without counting this request (countFailure does).
 */
export async function assertUnderLimit(ctx: Ctx, rule: RateRule, key: string): Promise<void> {
    const { now, windowStart } = windowOf(rule);
    const row = await ctx.env.DB.prepare("SELECT count FROM rate_limits WHERE bucket = ?1 AND window_start = ?2")
        .bind(await bucketOf(ctx, rule, key), windowStart)
        .first<{ count: number }>();
    if (row && row.count >= rule.limit * scale(ctx)) throw limited(rule, now, windowStart);
}

/** One failure of `key` for a rule checked with assertUnderLimit. */
export async function countFailure(ctx: Ctx, rule: RateRule, key: string): Promise<void> {
    await bump(ctx, rule, key, windowOf(rule).windowStart);
}
