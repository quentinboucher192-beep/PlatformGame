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
    follow: { name: "follow", limit: 60, windowSeconds: 3600 },
    playtime: { name: "playtime", limit: 40, windowSeconds: 3600 },
    projectCreate: { name: "project_create", limit: 10, windowSeconds: 3600 },
    projectInvite: { name: "project_invite", limit: 30, windowSeconds: 3600 },
    projectJoin: { name: "project_join", limit: 20, windowSeconds: 3600 },
    projectSave: { name: "project_save", limit: 120, windowSeconds: 3600 },
} as const satisfies Record<string, RateRule>;

/** RATE_LIMIT_SCALE multiplies every limit (development only: repeated local test runs). */
function scale(ctx: Ctx): number {
    const n = Number((ctx.env as { RATE_LIMIT_SCALE?: string }).RATE_LIMIT_SCALE ?? "1");
    return Number.isFinite(n) && n >= 1 ? Math.min(n, 1000) : 1;
}

export async function rateLimit(ctx: Ctx, rule: RateRule, key: string): Promise<void> {
    const limit = rule.limit * scale(ctx);
    const hashed = await hashIp(key, ctx.env.IP_HASH_SALT ?? "");
    const now = Math.floor(Date.now() / 1000);
    const windowStart = now - (now % rule.windowSeconds);
    const row = await ctx.env.DB.prepare(
        `INSERT INTO rate_limits (bucket, window_start, count) VALUES (?1, ?2, 1)
         ON CONFLICT (bucket) DO UPDATE SET
           count = CASE WHEN rate_limits.window_start = ?2 THEN rate_limits.count + 1 ELSE 1 END,
           window_start = ?2
         RETURNING count`,
    )
        .bind(`${rule.name}:${hashed}`, windowStart)
        .first<{ count: number }>();
    if (row && row.count > limit) {
        const retry = Math.max(1, windowStart + rule.windowSeconds - now);
        throw new ApiError(429, "RATE_LIMITED", "Trop de requêtes, réessayez plus tard.", { retry_after_seconds: retry }, {
            "retry-after": String(retry),
        });
    }
}
