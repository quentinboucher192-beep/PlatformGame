// Database row types and their public JSON representations. Privacy rules are applied
// here, in one place: a hidden location never leaves the server.

export interface PlayerRow {
    id: string;
    username: string;
    username_normalized: string;
    avatar: string;
    avatar_custom_key: string | null;
    country_code: string | null;
    region_code: string | null;
    location_visibility: "hidden" | "country" | "globe";
    profile_visibility: "public" | "private";
    allow_friend_requests: number;
    trust_level: number;
    tos_version: string;
    tos_accepted_at: string;
    created_at: string;
    last_seen_at: string;
    account_status: "active" | "suspended";
}

export interface LevelRow {
    id: string;
    name: string;
    planet_id: string;
    planet_name: string;
    level_order: number;
    current_version: number;
    content_hash: string;
    ranking_enabled: number;
}

export interface LevelVersionRow {
    id: number;
    level_id: string;
    version: number;
    content_hash: string;
    compatibility_status: "active" | "legacy" | "disabled";
    min_game_version: string;
    total_collectibles: number;
    total_enemies: number;
    time_limit_ms: number;
    min_time_ms: number;
    max_speed_px_s: number;
    bounds_w: number;
    bounds_h: number;
    spawn_x: number;
    spawn_y: number;
    exit_x: number;
    exit_y: number;
    exit_w: number;
    exit_h: number;
    created_at: string;
}

export interface RecordRow {
    id: string;
    player_id: string;
    level_id: string;
    level_version: number;
    level_hash: string;
    mode: string;
    category: string;
    time_ms: number;
    deaths: number;
    damage_taken: number;
    collectibles: number;
    total_collectibles: number;
    kills: number;
    score: number;
    game_version: string;
    attempt_id: string;
    verification_status: "PENDING" | "VERIFIED" | "REJECTED";
    rejection_reason: string | null;
    verification_notes: string | null;
    replay_id: string | null;
    created_at: string;
    verified_at: string | null;
}

export function avatarUrl(p: Pick<PlayerRow, "id" | "avatar" | "avatar_custom_key">): string | null {
    return p.avatar === "custom" && p.avatar_custom_key ? `/api/v1/players/${p.id}/avatar` : null;
}

/** Country as other players may see it (null when the player hides their location). */
export function visibleCountry(p: Pick<PlayerRow, "country_code" | "location_visibility">): string | null {
    return p.location_visibility === "hidden" ? null : p.country_code;
}

/** Minimal public identity used in leaderboards and friend lists. */
export function playerSummary(p: Pick<PlayerRow, "id" | "username" | "avatar" | "avatar_custom_key" | "country_code" | "location_visibility">) {
    return {
        id: p.id,
        username: p.username,
        avatar: p.avatar,
        avatar_url: avatarUrl(p),
        country_code: visibleCountry(p),
    };
}

/** Everything the owner of the account may see about themselves. */
export function privateProfile(p: PlayerRow) {
    return {
        id: p.id,
        username: p.username,
        avatar: p.avatar,
        avatar_url: avatarUrl(p),
        country_code: p.country_code,
        region_code: p.region_code,
        location_visibility: p.location_visibility,
        profile_visibility: p.profile_visibility,
        allow_friend_requests: p.allow_friend_requests === 1,
        trust_level: p.trust_level,
        tos_version: p.tos_version,
        created_at: p.created_at,
        last_seen_at: p.last_seen_at,
        account_status: p.account_status,
    };
}

export function recordJson(r: RecordRow, opts: { owner: boolean }) {
    const base = {
        id: r.id,
        player_id: r.player_id,
        level_id: r.level_id,
        level_version: r.level_version,
        level_hash: r.level_hash,
        mode: r.mode,
        category: r.category,
        time_ms: r.time_ms,
        deaths: r.deaths,
        damage_taken: r.damage_taken,
        collectibles: r.collectibles,
        total_collectibles: r.total_collectibles,
        score: r.score,
        game_version: r.game_version,
        verification_status: r.verification_status,
        has_replay: r.replay_id !== null,
        created_at: r.created_at,
        verified_at: r.verified_at,
    };
    if (!opts.owner) return base;
    return { ...base, attempt_id: r.attempt_id, rejection_reason: r.rejection_reason, verification_notes: r.verification_notes };
}
