// 2.75.3: an administrator sees everything of a profile (moderation). The administrator is the role
// of the SESSION (isAdminViewer: an active account with role = 'admin'), never a parameter, a header
// or anything else the game sends. For anyone else every lock stays exactly as it was.
//
// What it lifts (players.ts, creators.ts, race.ts, records.ts, adminRoles.ts): a profile masked or an
// account that is not active, the profile's tabs (statistics, time played, follows and followers,
// creations not published, progressions), the records and the replays not verified (pending or
// refused: always marked, never counted as verified), the privacy settings, the real presence and
// activity, the friends, the clan requests, the badges and the outfit.
//
// What it never lifts (the player's consent and the secrets): a country or a region the player
// hides stays hidden (visibleCountry, administrators included), no precise position exists at all,
// no password, token or IP hash is ever sent; private messages are not part of a profile.
//
// A technical impossibility stays one with its reason: a replay never sent or whose file is gone
// (REPLAY_NOT_FOUND), another version of the level (the game says so when it plays it).
//
// Each consultation of something normally hidden is written to the Journal (audit_log), grouped:
// one entry per administrator, player and kind (profile or replay) while the consultations follow
// each other within an hour (`count`, `last_at`, the locks lifted and the levels seen merged in).

import { audit, type AuthInfo } from "./auth";
import { isAdminViewer } from "./friends";
import { nowIso, type Ctx } from "./http";
import type { PlayerRow } from "./model";

/** The locks an administrator went through, as the game names them (game/online/AdminView). */
export type AdminLock =
    | "profile_private"
    | "account_inactive"
    | "presence_hidden"
    | "follows_private"
    | "replay_pending"
    | "replay_rejected"
    // A creator's drafts and levels hidden by reports (the tab « Créations »).
    | "levels_unpublished";

/** An active administrator looking at someone else's data (never at their own: no lock to lift). */
export function adminViewing(viewer: AuthInfo | null | undefined, ownerId: string): boolean {
    return isAdminViewer(viewer) && viewer!.player.id !== ownerId;
}

/** The privacy locks another player meets on this profile (in the order the game shows them). */
export function profileLocks(p: Pick<PlayerRow, "profile_visibility" | "account_status"> & { follows_visibility?: string | null; presence_visible?: number }): AdminLock[] {
    const out: AdminLock[] = [];
    if (p.profile_visibility === "private") out.push("profile_private");
    if (p.account_status !== "active") out.push("account_inactive");
    if (p.presence_visible === 0) out.push("presence_hidden");
    if (p.follows_visibility === "private") out.push("follows_private");
    return out;
}

/** The lock of a record, a replay or an attempt that is not verified (null once verified). */
export function statusLock(status: string | null | undefined): AdminLock | null {
    if (status === "PENDING") return "replay_pending";
    if (status === "REJECTED") return "replay_rejected";
    return null;
}

/**
 * The headers of a replay an administrator downloads through a lock: `x-admin-view` keeps the 2.75
 * meaning (the privacy lock: profile_private or account_inactive, the first one), and
 * `x-admin-replay-status` (2.75.3) the status of a replay or attempt that is not verified.
 */
export function adminReplayHeaders(locks: readonly AdminLock[], status: string | null): Record<string, string> {
    const h: Record<string, string> = {};
    const privacy = locks.find((l) => l === "profile_private" || l === "account_inactive");
    if (privacy) h["x-admin-view"] = privacy;
    if (status && status !== "VERIFIED") h["x-admin-replay-status"] = status;
    if (privacy || h["x-admin-replay-status"]) h["cache-control"] = "private, no-store";
    return h;
}

/** The privacy lock among `locks` (a masked profile first, then an account not active), else null. */
function privacyLock(locks: readonly string[]): string | null {
    return locks.find((l) => l === "profile_private") ?? locks.find((l) => l === "account_inactive") ?? null;
}

const GROUP_WINDOW_MS = 3600_000;
const GROUP_MAX_SPAN_MS = 24 * 3600_000;
const MAX_ITEMS = 8;

type ViewAction = "admin_profile_viewed" | "admin_replay_viewed";

/**
 * Writes an administrator's consultation to the Journal, grouped: the entry of the same
 * administrator, player and action whose last consultation is less than an hour old (and that
 * started less than a day ago) gets one more (`count`, `last_at`, its locks and items merged);
 * otherwise a new entry. `item`: what was seen (a level's title for a replay).
 */
export async function auditAdminView(
    ctx: Ctx,
    admin: AuthInfo,
    action: ViewAction,
    target: { id: string; username: string | null },
    locks: readonly string[],
    extra: { item?: string | null; record_id?: string | null; run_id?: string | null } = {},
): Promise<void> {
    const now = Date.now();
    const db = ctx.env.DB;
    const row = await db
        .prepare(
            `SELECT id, details FROM audit_log
             WHERE player_id = ?1 AND action = ?2 AND created_at >= ?3 AND json_extract(details, '$.target') = ?4
               AND COALESCE(json_extract(details, '$.last_at'), created_at) >= ?5
             ORDER BY id DESC LIMIT 1`,
        )
        .bind(admin.player.id, action, new Date(now - GROUP_MAX_SPAN_MS).toISOString(), target.id, new Date(now - GROUP_WINDOW_MS).toISOString())
        .first<{ id: number; details: string | null }>();
    if (row) {
        let d: Record<string, unknown> = {};
        try {
            d = JSON.parse(row.details ?? "{}") as Record<string, unknown>;
        } catch {
            d = {};
        }
        const prevLocks = Array.isArray(d.locks) ? (d.locks as unknown[]).filter((x): x is string => typeof x === "string") : typeof d.lock === "string" ? [d.lock] : [];
        const prevItems = Array.isArray(d.items) ? (d.items as unknown[]).filter((x): x is string => typeof x === "string") : [];
        const merged = [...new Set([...prevLocks, ...locks])];
        const items = extra.item && !prevItems.includes(extra.item) && prevItems.length < MAX_ITEMS ? [...prevItems, extra.item] : prevItems;
        const next = {
            ...d,
            count: (typeof d.count === "number" ? d.count : 1) + 1,
            last_at: nowIso(),
            locks: merged,
            lock: typeof d.lock === "string" ? d.lock : privacyLock(merged),
            items,
            target_username: target.username ?? d.target_username ?? null,
        };
        await db.prepare("UPDATE audit_log SET details = ?2 WHERE id = ?1").bind(row.id, JSON.stringify(next)).run();
        return;
    }
    await audit(ctx, admin.player.id, action, {
        target: target.id,
        target_username: target.username,
        by_username: admin.player.username,
        count: 1,
        last_at: nowIso(),
        locks: [...new Set(locks)],
        // 2.75: the single lock the Journal read before 2.75.3 (a privacy one, else none).
        lock: privacyLock(locks),
        items: extra.item ? [extra.item] : [],
        ...(extra.item ? { level_title: extra.item } : {}),
        ...(extra.record_id ? { record_id: extra.record_id } : {}),
        ...(extra.run_id ? { run_id: extra.run_id } : {}),
    });
}

/** The username of a player (for the Journal: the one at the time). */
export async function usernameOf(ctx: Ctx, id: string): Promise<string | null> {
    return (await ctx.env.DB.prepare("SELECT username FROM players WHERE id = ?1").bind(id).first<{ username: string }>())?.username ?? null;
}
