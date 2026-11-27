// Cosmic Online Ranking - Worker entry point: routing, error handling, request logs, edge
// caching of anonymous reads, and the daily maintenance cron.

import * as admin from "./admin";
import * as adminChallenges from "./adminChallenges";
import * as adminRoles from "./adminRoles";
import * as migration from "./accountMigration";
import * as backups from "./backups";
import { login, logout, me, optionalAuth, refresh, register, releaseClosedUsernames, usernameAvailable, type AuthInfo } from "./auth";
import * as friends from "./friends";
import * as gifts from "./gifts";
import * as globe from "./globe";
import { ApiError, errorResponse, json, log, type Bindings, type Ctx } from "./http";
import * as boards from "./leaderboards";
import * as creators from "./creators";
import * as achievements from "./achievements";
import * as cosmetics from "./cosmetics";
import * as badges from "./badges";
import * as campaign from "./campaign";
import * as clans from "./clans";
import * as raids from "./raids";
import * as raidZones from "./raidZones";
import * as raidRewards from "./raidRewards";
import * as raidMap from "./raidMap";
import * as raidWipe from "./raidWipe";
import * as raidWorld from "./raidWorld";
import * as raidTests from "./raidTests";
import * as raidReviews from "./raidReviews";
import * as raidRecap from "./raidRecap";
import * as campaignRevisions from "./campaignRevisions";
import * as enemyRevisions from "./enemyRevisions";
import * as tas from "./tas";
import * as challenges from "./challenges";
import * as creations from "./creations";
import * as creationAssets from "./creationAssets";
import * as devices from "./devices";
import * as notifications from "./notifications";
import * as race from "./race";
import * as workshop from "./workshop";
import * as projects from "./projects";
import { liveRoute } from "./live";
// The Durable Object classes (live editing rooms, live spectating rooms) are exported by the Worker module.
export { ProjectRoom } from "./live";
export { SpectateRoom } from "./spectate";
export { CoopRoom } from "./coop";
import * as coop from "./coop";
import * as coopRace from "./coopRace";
import * as spectate from "./spectate";
import { listLevels } from "./levels";
import type { RecordRow } from "./model";
import * as players from "./players";
import { RULES, rateLimit } from "./ratelimit";
import * as social from "./social";
import * as community from "./community";
import * as records from "./records";
import * as releases from "./releases";
import { CATEGORIES, MODES } from "./validate";

const API_VERSION = "1.0.0";

type Handler = (ctx: Ctx, viewer: AuthInfo | null) => Promise<Response>;

interface Route {
    method: string;
    pattern: RegExp;
    keys: string[];
    template: string;
    handler: Handler;
    /** Resolve an optional session before calling the handler. */
    viewer: boolean;
    /** Anonymous responses may be cached at the edge. */
    cacheable: boolean;
}

const routes: Route[] = [];

function route(method: string, template: string, handler: Handler, opts: { viewer?: boolean; cacheable?: boolean } = {}): void {
    const keys: string[] = [];
    const pattern = new RegExp(
        "^" +
            template.replace(/\{(\w+)\}/g, (_m, k: string) => {
                keys.push(k);
                return "([^/]+)";
            }) +
            "$",
    );
    routes.push({ method, pattern, keys, template, handler, viewer: opts.viewer ?? false, cacheable: opts.cacheable ?? false });
}

const V = "/api/v1";

// Health and metadata
route("GET", `${V}/health`, async (ctx) => {
    try {
        await ctx.env.DB.prepare("SELECT 1").first();
    } catch {
        // Homogeneous error envelope: clients never mistake a degraded service for a success.
        throw new ApiError(503, "SERVICE_UNAVAILABLE", "Service temporairement indisponible.", { database: "unavailable" }, { "retry-after": "30" });
    }
    return json(ctx, { status: "ok", api_version: API_VERSION, environment: ctx.env.ENVIRONMENT, database: "ok", time: new Date().toISOString() });
});
route(
    "GET",
    `${V}/meta`,
    async (ctx) =>
        json(ctx, {
            api_version: API_VERSION,
            environment: ctx.env.ENVIRONMENT,
            tos_version: ctx.env.TOS_VERSION,
            min_game_version: ctx.env.MIN_GAME_VERSION,
            categories: CATEGORIES,
            modes: MODES,
            levels: await listLevels(ctx),
        }),
    { cacheable: true },
);

// Authentication
route("GET", `${V}/auth/username-available`, (ctx) => usernameAvailable(ctx));
route("POST", `${V}/auth/register`, (ctx) => register(ctx));
route("POST", `${V}/auth/login`, (ctx) => login(ctx));
route("POST", `${V}/auth/refresh`, (ctx) => refresh(ctx));
route("POST", `${V}/auth/logout`, (ctx) => logout(ctx));
route("GET", `${V}/auth/me`, (ctx) => me(ctx));

// Players
route("GET", `${V}/players/search`, (ctx) => players.searchPlayers(ctx));
route("GET", `${V}/players/me/export`, (ctx) => players.exportMe(ctx));
route("PATCH", `${V}/players/me`, (ctx) => players.patchMe(ctx));
route("DELETE", `${V}/players/me`, (ctx) => players.deleteMe(ctx));
route("PUT", `${V}/players/me/avatar`, (ctx) => players.putAvatar(ctx));
route("POST", `${V}/players/me/playtime`, (ctx) => creators.reportPlaytime(ctx));
route("PUT", `${V}/players/me/achievements`, (ctx) => achievements.putAchievements(ctx));
// 2.47: the computer the account is bound to, and the copies of the progression (devices.ts).
route("GET", `${V}/players/me/device`, (ctx) => devices.myDevice(ctx));
route("PATCH", `${V}/players/me/device`, (ctx) => devices.renameDevice(ctx));
route("GET", `${V}/players/me/progress-copy`, (ctx) => devices.getProgressCopy(ctx));
route("PUT", `${V}/players/me/progress-copy`, (ctx) => devices.putProgressCopy(ctx));
// 2.50: « Migrer vers un nouveau compte » (accountMigration.ts): the code made on the old computer,
// the migration asked from the new one, the old computer's progression to restore.
route("POST", `${V}/players/me/migration`, (ctx) => migration.createCode(ctx));
route("GET", `${V}/players/me/migration`, (ctx) => migration.getCode(ctx));
route("DELETE", `${V}/players/me/migration`, (ctx) => migration.cancelCode(ctx));
route("POST", `${V}/players/me/migrate-from`, (ctx) => migration.migrateFrom(ctx));
route("GET", `${V}/players/me/progress-restore`, (ctx) => migration.getRestore(ctx));
route("GET", `${V}/players/me/progress-restore/file`, (ctx) => migration.getRestoreFile(ctx));
route("POST", `${V}/players/me/progress-restore`, (ctx) => migration.answerRestore(ctx));
route("PUT", `${V}/players/me/cosmetics`, (ctx) => cosmetics.putCosmetics(ctx));
route("GET", `${V}/players/me/rewards`, (ctx) => challenges.myRewards(ctx));
// Gifts (gifts.ts): mine (the content hidden until opened), and opening one, or a parcel (what grants it).
route("GET", `${V}/gifts/me`, (ctx) => gifts.myGifts(ctx));
route("POST", `${V}/gifts/{giftId}/open`, (ctx) => gifts.openGift(ctx));
route("POST", `${V}/gifts/parcels/{parcelId}/open`, (ctx) => gifts.openParcel(ctx));
route("PUT", `${V}/players/me/badges`, (ctx) => badges.putBadges(ctx));
route("GET", `${V}/players/{playerId}`, players.getPlayer, { viewer: true });
route("GET", `${V}/players/{playerId}/creations`, creators.creations, { viewer: true });
route("GET", `${V}/players/{playerId}/followers`, creators.followers, { viewer: true });
route("GET", `${V}/players/{playerId}/following`, creators.following, { viewer: true });
route("GET", `${V}/players/{playerId}/playtime`, creators.getPlaytime, { viewer: true });
route("POST", `${V}/players/{playerId}/follow`, (ctx) => creators.follow(ctx));
route("DELETE", `${V}/players/{playerId}/follow`, (ctx) => creators.unfollow(ctx));
route("GET", `${V}/creators`, (ctx) => creators.searchCreators(ctx));

// Notification centre
route("GET", `${V}/notifications`, (ctx) => notifications.listNotifications(ctx));
route("GET", `${V}/notifications/unread`, (ctx) => notifications.unreadNotifications(ctx));
route("POST", `${V}/notifications/read-all`, (ctx) => notifications.readAllNotifications(ctx));
route("GET", `${V}/notifications/settings`, (ctx) => notifications.getNotificationSettings(ctx));
route("PUT", `${V}/notifications/settings`, (ctx) => notifications.putNotificationSettings(ctx));
route("POST", `${V}/notifications/{notificationId}/read`, (ctx) => notifications.readNotification(ctx));
route("DELETE", `${V}/notifications/{notificationId}`, (ctx) => notifications.deleteNotification(ctx));
route("GET", `${V}/players/{playerId}/records`, players.getPlayerRecords, { viewer: true });
route("GET", `${V}/players/{playerId}/statistics`, players.getStatistics, { viewer: true });
route("GET", `${V}/players/{playerId}/avatar`, (ctx) => players.getAvatar(ctx));

// Leaderboards
route("GET", `${V}/leaderboards/global`, boards.routeGlobal, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/level/{levelId}`, boards.routeLevel, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/planet/{planetId}`, boards.routePlanet, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/country/{countryCode}`, boards.routeCountry, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/friends`, (ctx) => boards.routeFriends(ctx));
route("GET", `${V}/leaderboards/weekly`, boards.routeWeekly, { viewer: true, cacheable: true });

// Campaign social side (2.19): ratings of the official levels, comments of the official
// levels (ONLINE) and of the runs (their comments then use the /community/comments routes)
route("GET", `${V}/levels/ratings`, (ctx) => campaign.listRatings(ctx), { cacheable: true });
// 2.45: the campaign revisions published by an administrator (public: the games download them).
route("GET", `${V}/campaign/revisions`, (ctx) => campaignRevisions.manifest(ctx), { cacheable: true });
route("GET", `${V}/campaign/levels/{levelId}/revisions/{version}/file`, (ctx) => campaignRevisions.file(ctx), { cacheable: true });
// The creations of an administrator that every game may have to draw (public: suits' names and colours).
route("GET", `${V}/creations`, (ctx) => creations.publicCreations(ctx), { cacheable: true });
// 2.61 Studio de Créations: the images of the creations the caller may read, and each image
// (creationAssets.ts: its owner, a board, a profile or a challenge begun; cached by its hash).
route("GET", `${V}/creations/images`, (ctx, viewer) => creationAssets.publicImages(ctx, viewer), { viewer: true });
route("GET", `${V}/creations/{creationId}/image/{hash}`, (ctx, viewer) => creationAssets.getCreationImage(ctx, viewer), { viewer: true });
// 2.46: the native entities published by an administrator (public: the games download every revision).
route("GET", `${V}/enemies/revisions`, (ctx) => enemyRevisions.manifest(ctx), { cacheable: true });
route("GET", `${V}/enemies/{enemyId}/revisions/{revision}/file`, (ctx) => enemyRevisions.file(ctx), { cacheable: true });
// The machine's runs published by the administrators (tas.ts): every game reads the list now and then.
route("GET", `${V}/tas`, (ctx) => tas.list(ctx), { cacheable: true });
route("GET", `${V}/tas/{levelId}/{category}/{revision}/record`, (ctx) => tas.file(ctx, "record"), { cacheable: true });
route("GET", `${V}/tas/{levelId}/{category}/{revision}/replay`, (ctx) => tas.file(ctx, "replay"), { cacheable: true });
route("GET", `${V}/levels/{levelId}/rating`, (ctx) => campaign.getRating(ctx), { cacheable: true });
route("PUT", `${V}/levels/{levelId}/rating`, (ctx) => campaign.putRating(ctx));
route("DELETE", `${V}/levels/{levelId}/rating`, (ctx) => campaign.deleteRating(ctx));
route("GET", `${V}/levels/{levelId}/comments`, (ctx) => campaign.listLevelComments(ctx));
route("POST", `${V}/levels/{levelId}/comments`, (ctx) => campaign.postLevelComment(ctx));
route("GET", `${V}/records/{recordId}/comments`, (ctx) => campaign.listRunComments(ctx));
route("POST", `${V}/records/{recordId}/comments`, (ctx) => campaign.postRunComment(ctx));

// Records
route("POST", `${V}/records/submit`, (ctx) => records.submit(ctx));
route("GET", `${V}/records/{recordId}`, records.getRecord, { viewer: true });
route("GET", `${V}/records/{recordId}/replay`, records.downloadReplay, { viewer: true });
route("PUT", `${V}/records/{recordId}/replay`, (ctx) => records.uploadReplay(ctx));

// Globe
route("GET", `${V}/globe/records`, globe.globeRecords, { viewer: true, cacheable: true });
route("GET", `${V}/globe/countries`, (ctx) => globe.globeCountries(ctx), { cacheable: true });
route("GET", `${V}/globe/players`, (ctx) => globe.globePlayers(ctx), { cacheable: true });
route("GET", `${V}/globe/records/{countryCode}`, globe.globeCountryRecords, { viewer: true, cacheable: true });

// Friends
route("GET", `${V}/friends`, (ctx) => friends.listFriends(ctx));
route("POST", `${V}/presence`, (ctx) => friends.postPresence(ctx));
route("POST", `${V}/friends/request`, (ctx) => friends.requestFriend(ctx));
route("POST", `${V}/friends/accept`, (ctx) => friends.acceptFriend(ctx));
route("DELETE", `${V}/friends/{playerId}`, (ctx) => friends.deleteFriend(ctx));
// Live spectating between friends (2.36): the player's own run sent, a friend's run read
route("POST", `${V}/spectate/broadcast`, (ctx) => spectate.broadcast(ctx));
route("GET", `${V}/spectate/{playerId}`, (ctx) => spectate.watch(ctx));
// Cooperation on the EXTRÊME levels (2.62, docs/COOP.md): rooms, invitations, the relay's socket.
route("POST", `${V}/coop/rooms`, (ctx) => coop.createRoom(ctx));
route("POST", `${V}/coop/rooms/{roomId}/invite`, (ctx) => coop.invite(ctx));
route("GET", `${V}/coop/invitations`, (ctx) => coop.invitations(ctx));
route("GET", `${V}/coop/rooms/{roomId}/socket`, (ctx) => coop.socket(ctx));
// Duo / Trio boards of the « course à la vérification » (2.62): team attempts, confirmations.
route("POST", `${V}/coop/runs`, (ctx) => coopRace.submitRun(ctx));
route("PUT", `${V}/coop/runs/{runId}/replay`, (ctx) => coopRace.uploadRunReplay(ctx));
route("GET", `${V}/coop/runs/{runId}/replay`, (ctx) => coopRace.downloadRunReplay(ctx));
route("POST", `${V}/coop/attempts/{attemptId}/confirm`, (ctx) => coopRace.confirm(ctx));
route("GET", `${V}/coop/board`, (ctx) => coopRace.getBoard(ctx));
// 2.85: the comments of a team's attempt (then the /community/comments routes, as a run's).
route("GET", `${V}/coop/runs/{runId}/comments`, (ctx) => campaign.listCoopComments(ctx));
route("POST", `${V}/coop/runs/{runId}/comments`, (ctx) => campaign.postCoopComment(ctx));

// Social: conversations between friends, groups, blocks
route("GET", `${V}/social/conversations`, (ctx) => social.listConversations(ctx));
route("GET", `${V}/social/unread`, (ctx) => social.unread(ctx));
route("POST", `${V}/social/direct`, (ctx) => social.openDirect(ctx));
route("POST", `${V}/social/groups`, (ctx) => social.createGroup(ctx));
route("PATCH", `${V}/social/groups/{conversationId}`, (ctx) => social.renameGroup(ctx));
route("POST", `${V}/social/groups/{conversationId}/members`, (ctx) => social.addMember(ctx));
route("DELETE", `${V}/social/groups/{conversationId}/members/{playerId}`, (ctx) => social.removeMember(ctx));
route("GET", `${V}/social/conversations/{conversationId}/messages`, (ctx) => social.listMessages(ctx));
route("POST", `${V}/social/conversations/{conversationId}/messages`, (ctx) => social.sendMessage(ctx));
route("POST", `${V}/social/conversations/{conversationId}/read`, (ctx) => social.markRead(ctx));
route("DELETE", `${V}/social/messages/{messageId}`, (ctx) => social.deleteMessage(ctx));
route("POST", `${V}/social/messages/{messageId}/report`, (ctx) => social.reportMessage(ctx));
route("GET", `${V}/social/blocks`, (ctx) => social.listBlocks(ctx));
route("POST", `${V}/social/blocks`, (ctx) => social.blockPlayer(ctx));
route("DELETE", `${V}/social/blocks/{playerId}`, (ctx) => social.unblockPlayer(ctx));

// Clans (2.66, clans.ts): the literal routes come before /clans/{clanId}. Their discussion is a
// group conversation of the social routes above.
route("GET", `${V}/clans/me`, (ctx) => clans.myClan(ctx));
route("GET", `${V}/clans/ranking`, clans.clanRanking, { viewer: true, cacheable: true });
route("GET", `${V}/clans`, (ctx) => clans.searchClans(ctx), { cacheable: true });
route("POST", `${V}/clans`, (ctx) => clans.createClan(ctx));
route("GET", `${V}/clans/{clanId}`, clans.getClan, { viewer: true, cacheable: true });
route("PATCH", `${V}/clans/{clanId}`, (ctx) => clans.updateClan(ctx));
route("DELETE", `${V}/clans/{clanId}`, (ctx) => clans.dissolveClan(ctx));
route("POST", `${V}/clans/{clanId}/join`, (ctx) => clans.joinClan(ctx));
route("DELETE", `${V}/clans/{clanId}/join`, (ctx) => clans.withdrawRequest(ctx));
route("POST", `${V}/clans/{clanId}/invites`, (ctx) => clans.invitePlayer(ctx));
route("DELETE", `${V}/clans/{clanId}/invites/{playerId}`, (ctx) => clans.cancelInvite(ctx));
route("POST", `${V}/clans/{clanId}/requests/{playerId}`, (ctx) => clans.answerApplication(ctx));
route("POST", `${V}/clans/{clanId}/transfer`, (ctx) => clans.transferLeadership(ctx));
route("PATCH", `${V}/clans/{clanId}/members/{playerId}`, (ctx) => clans.setMemberRole(ctx));
route("DELETE", `${V}/clans/{clanId}/members/{playerId}`, (ctx) => clans.removeMember(ctx));
// 2.78.1 (raidRecap.ts): the « fiches récapitulatives » of the clan's finished raids (its members, the administrators).
route("GET", `${V}/clans/{clanId}/raids`, (ctx) => raidRecap.finishedRaids(ctx));
route("GET", `${V}/clans/{clanId}/raids/{eventId}/recap`, (ctx) => raidRecap.raidRecap(ctx));
// Clan raids « Asterion » (raids.ts), phase 1: events, registrations, reminders.
route("GET", `${V}/raids`, raids.overview, { viewer: true });
route("GET", `${V}/raids/prefs`, (ctx) => raids.getPrefs(ctx));
route("PUT", `${V}/raids/prefs`, (ctx) => raids.putPrefs(ctx));
route("POST", `${V}/raids/events`, (ctx) => raids.scheduleClanRaid(ctx));
route("GET", `${V}/raids/events/{eventId}`, raids.getEvent, { viewer: true });
route("DELETE", `${V}/raids/events/{eventId}`, (ctx) => raids.cancelClanRaid(ctx));
route("POST", `${V}/raids/events/{eventId}/clan`, (ctx) => raids.registerClan(ctx));
route("PATCH", `${V}/raids/events/{eventId}/clan`, (ctx) => raids.updateEntry(ctx));
route("DELETE", `${V}/raids/events/{eventId}/clan`, (ctx) => raids.withdrawClan(ctx));
route("POST", `${V}/raids/events/{eventId}/register`, (ctx) => raids.registerMe(ctx));
route("DELETE", `${V}/raids/events/{eventId}/register`, (ctx) => raids.unregisterMe(ctx));
route("POST", `${V}/raids/events/{eventId}/invites`, (ctx) => raids.inviteClan(ctx));
route("POST", `${V}/raids/events/{eventId}/invitation`, (ctx) => raids.answerInvitation(ctx));
route("POST", `${V}/raids/events/{eventId}/call`, (ctx) => raids.callMembers(ctx));
// Phase 2 (raidZones.ts, 2.65): the branch « BRAS DROIT », its instances of 8 and the consensus of their reports.
route("GET", `${V}/raids/events/{eventId}/branch`, (ctx) => raidZones.branchView(ctx));
route("POST", `${V}/raids/events/{eventId}/instance`, (ctx) => raidZones.joinInstance(ctx));
route("GET", `${V}/raids/instances/{instanceId}/socket`, (ctx) => raidZones.instanceSocket(ctx));
route("POST", `${V}/raids/instances/{instanceId}/reports`, (ctx) => raidZones.report(ctx));
// 2.75 (raidWipe.ts): a wiped group's spectators (what they can watch) and the zones taken again.
route("GET", `${V}/raids/instances/{instanceId}/watch`, (ctx) => raidWipe.watchTargets(ctx));
route("POST", `${V}/raids/instances/{instanceId}/retake`, (ctx) => raidWipe.retake(ctx));
// 2.80 (raidTests.ts): an administrator's test with invited players — the invited players' side.
route("GET", `${V}/raid-tests/invitations`, (ctx) => raidTests.invitations(ctx));
route("POST", `${V}/raid-tests/{testId}/accept`, (ctx) => raidTests.accept(ctx));
route("POST", `${V}/raid-tests/{testId}/decline`, (ctx) => raidTests.decline(ctx));
route("GET", `${V}/raid-tests/{testId}/socket`, (ctx) => raidTests.socket(ctx));
// 2.68 (phase 3): the results of an event (validated tables only) and the rewards to claim.
route("GET", `${V}/raids/events/{eventId}/results`, (ctx) => raidZones.eventResults(ctx));
route("POST", `${V}/raids/rewards/claim`, (ctx) => raidRewards.claim(ctx));
// Phase 4 (raidMap.ts, 2.68): the whole titan (seven branches, the links between them), each branch's map,
// the placement of the players (8 per instance, up to 50 per clan) and its tools for the raid leader.
route("GET", `${V}/raids/events/{eventId}/titan`, (ctx) => raidMap.titanView(ctx));
route("GET", `${V}/raids/events/{eventId}/branches/{part}`, (ctx) => raidMap.branchView(ctx));
route("POST", `${V}/raids/events/{eventId}/branches/{part}/join`, (ctx) => raidMap.join(ctx));
route("POST", `${V}/raids/events/{eventId}/join`, (ctx) => raidMap.join(ctx));
route("GET", `${V}/raids/events/{eventId}/placements`, (ctx) => raidMap.placements(ctx));
route("POST", `${V}/raids/events/{eventId}/placements`, (ctx) => raidMap.move(ctx));
route("POST", `${V}/raids/events/{eventId}/placements/rebalance`, (ctx) => raidMap.rebalance(ctx));
// Phase 5 (raidWorld.ts, 2.69): the world events: rankings of every clan, group and player (validated results only).
route("GET", `${V}/raids/events/{eventId}/world`, (ctx) => raidWorld.worldView(ctx));
// 2.77.4 (raidReviews.ts): the players' reviews of an event, only by those who played it (the server checks).
route("GET", `${V}/raids/events/{eventId}/reviews`, raidReviews.listReviews, { viewer: true });
route("PUT", `${V}/raids/events/{eventId}/review`, (ctx) => raidReviews.putReview(ctx));
route("DELETE", `${V}/raids/events/{eventId}/review`, (ctx) => raidReviews.deleteReview(ctx));
route("POST", `${V}/raids/reviews/{reviewId}/report`, (ctx) => raidReviews.reportReview(ctx));

// Community levels: publish, browse, download, rate, report, per-level leaderboards
route("GET", `${V}/community/levels`, (ctx) => community.listLevels(ctx));
route("POST", `${V}/community/levels`, (ctx) => community.startPublish(ctx));
route("GET", `${V}/community/levels/{levelId}`, (ctx) => community.getLevel(ctx));
route("DELETE", `${V}/community/levels/{levelId}`, (ctx) => community.removeLevel(ctx));
route("PUT", `${V}/community/levels/{levelId}/versions/{version}/package`, (ctx) => community.uploadPackage(ctx));
route("PUT", `${V}/community/levels/{levelId}/versions/{version}/replay`, (ctx) => community.uploadCompletion(ctx));
route("GET", `${V}/community/levels/{levelId}/package`, (ctx) => community.downloadPackage(ctx));

// Game installers (in-game "nouvelle version" notification)
route("GET", `${V}/client/latest`, (ctx) => releases.latest(ctx), { cacheable: true });
route("GET", `${V}/client/download/{version}`, (ctx) => releases.download(ctx));
route("GET", `${V}/community/levels/{levelId}/thumbnail`, (ctx) => community.thumbnail(ctx));
route("GET", `${V}/community/levels/{levelId}/leaderboard`, (ctx) => community.leaderboard(ctx));
route("POST", `${V}/community/levels/{levelId}/rate`, (ctx) => community.rate(ctx));
route("GET", `${V}/community/favorites`, (ctx) => workshop.listFavorites(ctx));
route("POST", `${V}/community/levels/{levelId}/favorite`, (ctx) => workshop.addFavorite(ctx));
route("DELETE", `${V}/community/levels/{levelId}/favorite`, (ctx) => workshop.removeFavorite(ctx));
route("GET", `${V}/community/levels/{levelId}/comments`, (ctx) => workshop.listComments(ctx));
route("POST", `${V}/community/levels/{levelId}/comments`, (ctx) => workshop.postComment(ctx));
route("GET", `${V}/community/comments/{commentId}/replies`, (ctx) => workshop.listReplies(ctx));
route("PATCH", `${V}/community/comments/{commentId}`, (ctx) => workshop.editComment(ctx));
route("DELETE", `${V}/community/comments/{commentId}`, (ctx) => workshop.deleteComment(ctx));
route("POST", `${V}/community/comments/{commentId}/pin`, (ctx) => workshop.pinComment(ctx, true));
route("DELETE", `${V}/community/comments/{commentId}/pin`, (ctx) => workshop.pinComment(ctx, false));
route("PUT", `${V}/community/comments/{commentId}/reaction`, (ctx) => workshop.reactComment(ctx));
route("DELETE", `${V}/community/comments/{commentId}/reaction`, (ctx) => workshop.unreactComment(ctx));
route("POST", `${V}/community/comments/{commentId}/report`, (ctx) => workshop.reportComment(ctx));
route("POST", `${V}/community/levels/{levelId}/report`, (ctx) => community.report(ctx));
route("POST", `${V}/community/levels/{levelId}/records`, (ctx) => community.submitRecord(ctx));
route("PUT", `${V}/community/records/{recordId}/replay`, (ctx) => community.uploadRecordReplay(ctx));
route("GET", `${V}/community/records/{recordId}/replay`, (ctx) => community.downloadRecordReplay(ctx));

// Collaborative projects (phase 2): roles checked by the server, invitations, revisions
route("POST", `${V}/projects`, (ctx) => projects.createProject(ctx));
route("GET", `${V}/projects`, (ctx) => projects.myProjects(ctx));
route("GET", `${V}/projects/discover`, (ctx) => projects.discoverProjects(ctx));
route("POST", `${V}/projects/join`, (ctx) => projects.joinWithKey(ctx));
route("GET", `${V}/projects/invitations`, (ctx) => projects.myInvitations(ctx));
route("POST", `${V}/projects/invitations/{inviteId}/accept`, (ctx) => projects.answerInvitation(ctx, true));
route("DELETE", `${V}/projects/invitations/{inviteId}`, (ctx) => projects.answerInvitation(ctx, false));
route("GET", `${V}/projects/{projectId}`, (ctx) => projects.getProject(ctx));
route("PATCH", `${V}/projects/{projectId}`, (ctx) => projects.updateProject(ctx));
route("DELETE", `${V}/projects/{projectId}`, (ctx) => projects.deleteProject(ctx));
route("GET", `${V}/projects/{projectId}/members`, (ctx) => projects.listMembers(ctx));
route("PATCH", `${V}/projects/{projectId}/members/{playerId}`, (ctx) => projects.setRole(ctx));
route("DELETE", `${V}/projects/{projectId}/members/{playerId}`, (ctx) => projects.removeMember(ctx));
route("POST", `${V}/projects/{projectId}/transfer`, (ctx) => projects.transferProject(ctx));
route("POST", `${V}/projects/{projectId}/invites`, (ctx) => projects.createInvite(ctx));
route("GET", `${V}/projects/{projectId}/invites`, (ctx) => projects.listInvites(ctx));
route("DELETE", `${V}/projects/{projectId}/invites/{inviteId}`, (ctx) => projects.revokeInvite(ctx));
route("POST", `${V}/projects/{projectId}/join`, (ctx) => projects.requestJoin(ctx));
route("DELETE", `${V}/projects/{projectId}/join`, (ctx) => projects.cancelJoinRequest(ctx));
route("GET", `${V}/projects/{projectId}/requests`, (ctx) => projects.listRequests(ctx));
route("POST", `${V}/projects/{projectId}/requests/{playerId}`, (ctx) => projects.answerRequest(ctx));
route("GET", `${V}/projects/{projectId}/content`, (ctx) => projects.getContent(ctx));
route("PUT", `${V}/projects/{projectId}/content`, (ctx) => projects.putContent(ctx));
route("GET", `${V}/projects/{projectId}/revisions`, (ctx) => projects.listRevisions(ctx));
route("GET", `${V}/projects/{projectId}/activity`, (ctx) => projects.listActivity(ctx));
// Course à la vérification (EXTRÊME levels)
route("POST", `${V}/races/runs`, (ctx) => race.submitRun(ctx));
route("PUT", `${V}/races/runs/{runId}/replay`, (ctx) => race.uploadRunReplay(ctx));
route("GET", `${V}/races/board`, (ctx) => race.getBoard(ctx));
route("GET", `${V}/races/runs/{runId}/replay`, (ctx) => race.downloadRunReplay(ctx));
// 2.85: the comments of an attempt of a solo race (then the /community/comments routes, as a run's).
route("GET", `${V}/races/runs/{runId}/comments`, (ctx) => campaign.listRaceComments(ctx));
route("POST", `${V}/races/runs/{runId}/comments`, (ctx) => campaign.postRaceComment(ctx));
route("GET", `${V}/players/{playerId}/races`, (ctx) => race.playerRaces(ctx));
// Chat of the live editor
route("GET", `${V}/projects/{projectId}/messages`, (ctx) => projects.listMessages(ctx));
route("POST", `${V}/projects/{projectId}/messages`, (ctx) => projects.postMessage(ctx));
// History and named versions (phase 5)
route("GET", `${V}/projects/{projectId}/compare`, (ctx) => projects.compareRevisions(ctx));
route("POST", `${V}/projects/{projectId}/revisions/{revision}/restore`, (ctx) => projects.restoreRevision(ctx));
route("GET", `${V}/projects/{projectId}/versions`, (ctx) => projects.listVersions(ctx));
route("POST", `${V}/projects/{projectId}/versions`, (ctx) => projects.createVersion(ctx));
route("GET", `${V}/projects/{projectId}/versions/{versionId}/content`, (ctx) => projects.versionContent(ctx));
// Live editing (phases 3-4): presence, temporary locks and operations, in the project's room
route("POST", `${V}/projects/{projectId}/live/join`, (ctx) => liveRoute(ctx, "join"));
route("POST", `${V}/projects/{projectId}/live/heartbeat`, (ctx) => liveRoute(ctx, "heartbeat"));
route("POST", `${V}/projects/{projectId}/live/ops`, (ctx) => liveRoute(ctx, "ops"));
route("POST", `${V}/projects/{projectId}/live/leave`, (ctx) => liveRoute(ctx, "leave"));

// Challenges (2.32 docs/online/DAILY_CHALLENGES.md, 2.33 docs/online/CHALLENGES_PERIODS.md):
// a future challenge is never shown. The literal routes come before /challenges/{challengeId}.
route("GET", `${V}/challenges/current`, (ctx) => challenges.currentAll(ctx));
route("GET", `${V}/challenges/calendar`, challenges.calendar, { viewer: true, cacheable: true });
route("GET", `${V}/challenges/daily/current`, (ctx) => challenges.current(ctx));
route("GET", `${V}/challenges/daily`, (ctx) => challenges.archive(ctx), { cacheable: true });
route("GET", `${V}/challenges/me`, (ctx) => challenges.mine(ctx));
route("GET", `${V}/challenges/{challengeId}/leaderboard`, challenges.leaderboard, { viewer: true, cacheable: true });
route("GET", `${V}/challenges/{challengeId}`, (ctx) => challenges.getChallenge(ctx), { cacheable: true });
route("POST", `${V}/challenges/{challengeId}/runs`, (ctx) => challenges.submitRun(ctx));
route("PUT", `${V}/challenges/runs/{runId}/replay`, (ctx) => challenges.uploadRunReplay(ctx));
// « Plus loin » (2.33): attempts that did not reach the end, scored by the server from their replay
route("POST", `${V}/challenges/{challengeId}/attempts`, (ctx) => challenges.submitAttempt(ctx));
route("PUT", `${V}/challenges/attempts/{attemptId}/replay`, (ctx) => challenges.uploadAttemptReplay(ctx));
// The kept replay of a ranked attempt (GET: {id} is the server's id, `attempt_id` of the board's entries)
route("GET", `${V}/challenges/attempts/{id}/replay`, challenges.downloadAttemptReplay, { viewer: true });
route("GET", `${V}/challenges/runs/{runId}/replay`, challenges.downloadRunReplay, { viewer: true });

// Moderation (only with the ADMIN_TOKEN secret)
route("GET", `${V}/admin/records`, (ctx) => admin.listForReview(ctx));
route("POST", `${V}/admin/records/{recordId}/review`, (ctx) => admin.review(ctx));
route("POST", `${V}/admin/levels`, (ctx) => admin.registerLevels(ctx));
route("GET", `${V}/admin/comments`, async (ctx) => {
    await admin.requireAdmin(ctx);
    return workshop.adminListComments(ctx);
});
route("POST", `${V}/admin/comments/{commentId}/moderate`, async (ctx) => {
    await admin.requireAdmin(ctx);
    return workshop.adminModerateComment(ctx);
});

// ADMINISTRATION in the game (2.40): accounts with the admin role, checked on every request.
route("GET", `${V}/admin/players`, (ctx) => adminRoles.listPlayers(ctx));
route("GET", `${V}/admin/players/{playerId}/levels`, (ctx) => adminRoles.playerLevels(ctx)); // 2.75
route("POST", `${V}/admin/players/{playerId}/role`, (ctx) => adminRoles.setRole(ctx));
route("GET", `${V}/admin/log`, (ctx) => adminRoles.listLog(ctx));
// Backups of all the data (docs/online/BACKUP.md): server-side copies, encrypted download,
// import in parts, check, apply step by step (maintenance meanwhile), abandon.
route("GET", `${V}/admin/backups`, (ctx) => backups.listBackups(ctx));
route("POST", `${V}/admin/backups`, (ctx) => backups.createBackupRoute(ctx));
route("POST", `${V}/admin/backups/imports`, (ctx) => backups.createImport(ctx));
route("GET", `${V}/admin/backups/imports/{importId}`, (ctx) => backups.getImport(ctx));
route("DELETE", `${V}/admin/backups/imports/{importId}`, (ctx) => backups.deleteImport(ctx));
route("PUT", `${V}/admin/backups/imports/{importId}/parts/{n}`, (ctx) => backups.putImportPart(ctx));
route("POST", `${V}/admin/backups/imports/{importId}/check`, (ctx) => backups.checkImport(ctx));
route("POST", `${V}/admin/backups/imports/{importId}/apply`, (ctx) => backups.applyImport(ctx));
route("POST", `${V}/admin/backups/{backupId}/download`, (ctx) => backups.downloadBackup(ctx));
// ADMINISTRATION › Calendrier des défis (2.41): the coming challenges too, set or put back to the draw.
route("GET", `${V}/admin/challenges`, (ctx) => adminChallenges.listChallenges(ctx));
// ADMINISTRATION › Campagne (2.45): campaign levels edited in the game's editor, published as revisions.
route("GET", `${V}/admin/campaign`, (ctx) => campaignRevisions.adminList(ctx));
route("GET", `${V}/admin/campaign/{levelId}/revisions`, (ctx) => campaignRevisions.adminHistory(ctx));
route("POST", `${V}/admin/campaign/{levelId}/revisions`, (ctx) => campaignRevisions.publish(ctx));
route("POST", `${V}/admin/campaign/{levelId}/revert`, (ctx) => campaignRevisions.revert(ctx));
// ADMINISTRATION › Ennemis (2.46): native entities edited in the game's enemy editor, published as revisions.
route("GET", `${V}/admin/enemies`, (ctx) => enemyRevisions.adminList(ctx));
route("GET", `${V}/admin/enemies/{enemyId}/revisions`, (ctx) => enemyRevisions.adminHistory(ctx));
route("POST", `${V}/admin/enemies/{enemyId}/revisions`, (ctx) => enemyRevisions.publish(ctx));
// ADMINISTRATION › TAS (tas.ts): the leases of the trainings run on the administrators' computers, the publications.
route("GET", `${V}/admin/tas`, (ctx) => tas.adminList(ctx));
route("POST", `${V}/admin/tas/{levelId}/{category}/lease`, (ctx) => tas.acquire(ctx));
route("POST", `${V}/admin/tas/{levelId}/{category}/lease/heartbeat`, (ctx) => tas.heartbeat(ctx));
route("POST", `${V}/admin/tas/{levelId}/{category}/lease/release`, (ctx) => tas.release(ctx));
route("POST", `${V}/admin/tas/{levelId}/{category}/publish`, (ctx) => tas.publish(ctx));
route("GET", `${V}/admin/challenges/levels`, (ctx) => adminChallenges.listLevels(ctx));
route("PUT", `${V}/admin/challenges/{challengeId}`, (ctx) => adminChallenges.setChallenge(ctx));
route("DELETE", `${V}/admin/challenges/{challengeId}`, (ctx) => adminChallenges.resetChallenge(ctx));
// 2.43: the rewards of a coming challenge, and the titles created on the server.
route("PUT", `${V}/admin/challenges/{challengeId}/rewards`, (ctx) => adminChallenges.setRewards(ctx));
route("GET", `${V}/admin/titles`, (ctx) => adminChallenges.listTitles(ctx));
route("POST", `${V}/admin/titles`, (ctx) => adminChallenges.createTitle(ctx));
// ADMINISTRATION › Créations: the suits an administrator makes (creations.ts).
// ADMINISTRATION › Joueurs (2.47): the computer an account is bound to; unbind it, or bind it to a refused attempt's.
route("GET", `${V}/admin/players/{playerId}/device`, (ctx) => devices.adminDevice(ctx));
route("POST", `${V}/admin/players/{playerId}/device`, (ctx) => devices.adminSetDevice(ctx));
// 2.50: ADMINISTRATION › Joueurs › « Migrer » : preview, then a full backup and the migration.
route("GET", `${V}/admin/players/{playerId}/migration`, (ctx) => migration.adminPreview(ctx));
route("POST", `${V}/admin/players/{playerId}/migrate`, (ctx) => migration.adminMigrate(ctx));
// ADMINISTRATION › Clans (2.66): rename or dissolve a clan (each action in the Journal).
route("GET", `${V}/admin/clans`, (ctx) => clans.adminListClans(ctx));
route("PATCH", `${V}/admin/clans/{clanId}`, (ctx) => clans.adminRenameClan(ctx));
route("DELETE", `${V}/admin/clans/{clanId}`, (ctx) => clans.adminDissolveClan(ctx));
// ADMINISTRATION › Raids: the world events (each action in the Journal).
route("GET", `${V}/admin/raids`, (ctx) => raids.adminList(ctx));
route("POST", `${V}/admin/raids`, (ctx) => raids.adminCreate(ctx));
route("GET", `${V}/admin/raids/{eventId}`, (ctx) => raids.adminGet(ctx));
route("PATCH", `${V}/admin/raids/{eventId}`, (ctx) => raids.adminUpdate(ctx));
route("DELETE", `${V}/admin/raids/{eventId}`, (ctx) => raids.adminCancel(ctx));
route("POST", `${V}/admin/raids/{eventId}/publish`, (ctx) => raids.adminPublish(ctx));
// 2.69 (raidWorld.ts): the live panel, suspend / resume, the map's check, a catch-up session (each action journaled).
route("GET", `${V}/admin/raids/{eventId}/live`, (ctx) => raidWorld.adminLive(ctx));
route("POST", `${V}/admin/raids/{eventId}/suspend`, (ctx) => raidWorld.adminSuspend(ctx));
route("POST", `${V}/admin/raids/{eventId}/resume`, (ctx) => raidWorld.adminResume(ctx));
route("POST", `${V}/admin/raids/{eventId}/validate-map`, (ctx) => raidWorld.adminValidateMap(ctx));
route("POST", `${V}/admin/raids/{eventId}/catchup`, (ctx) => raidWorld.adminCatchup(ctx));
// 2.80 (raidTests.ts): « Tester Asterion » with invited players (administrators only).
route("GET", `${V}/admin/raid-test-players`, (ctx) => raidTests.players(ctx));
route("POST", `${V}/admin/raid-tests`, (ctx) => raidTests.create(ctx));
route("GET", `${V}/admin/raid-tests/{testId}`, (ctx) => raidTests.get(ctx));
route("POST", `${V}/admin/raid-tests/{testId}/invitations`, (ctx) => raidTests.invite(ctx));
route("PATCH", `${V}/admin/raid-tests/{testId}/invitations/{playerId}`, (ctx) => raidTests.update(ctx));
route("DELETE", `${V}/admin/raid-tests/{testId}/invitations/{playerId}`, (ctx) => raidTests.cancel(ctx));
route("POST", `${V}/admin/raid-tests/{testId}/seat`, (ctx) => raidTests.seat(ctx));
route("POST", `${V}/admin/raid-tests/{testId}/end`, (ctx) => raidTests.end(ctx));
// 2.77.4 (raidReviews.ts): every review of an event, a comment masked or restored (Journal).
route("GET", `${V}/admin/raids/{eventId}/reviews`, (ctx) => raidReviews.adminListReviews(ctx));
route("POST", `${V}/admin/raids/{eventId}/reviews/{reviewId}/moderate`, (ctx) => raidReviews.adminModerateReview(ctx));
// ADMINISTRATION › Cadeaux (gifts.ts): the players to choose (with what they own), a send, the sends, a cancellation.
route("GET", `${V}/admin/gifts/players`, (ctx) => gifts.adminPlayers(ctx));
route("GET", `${V}/admin/gifts`, (ctx) => gifts.adminHistory(ctx));
route("POST", `${V}/admin/gifts`, (ctx) => gifts.adminSend(ctx));
route("POST", `${V}/admin/gifts/{sendId}/cancel`, (ctx) => gifts.adminCancel(ctx));
route("GET", `${V}/admin/creations`, (ctx) => creations.listCreations(ctx));
route("POST", `${V}/admin/creations`, (ctx) => creations.createCreation(ctx));
route("PUT", `${V}/admin/creations/{creationId}`, (ctx) => creations.updateCreation(ctx));
route("POST", `${V}/admin/creations/{creationId}/archive`, (ctx) => creations.archiveCreation(ctx));
// 2.61: the image of a creation (the Studio): sent (a PNG checked, kept in R2), and every image.
route("GET", `${V}/admin/creations/images`, (ctx) => creationAssets.adminImages(ctx));
route("PUT", `${V}/admin/creations/{creationId}/image`, (ctx) => creationAssets.putCreationImage(ctx));

function match(method: string, path: string): { route: Route; params: Record<string, string> } | "method" | null {
    let pathMatched = false;
    for (const r of routes) {
        const m = r.pattern.exec(path);
        if (!m) continue;
        pathMatched = true;
        if (r.method !== method && !(method === "HEAD" && r.method === "GET")) continue;
        const params: Record<string, string> = {};
        r.keys.forEach((k, i) => {
            params[k] = decodeURIComponent(m[i + 1] ?? "");
        });
        return { route: r, params };
    }
    return pathMatched ? "method" : null;
}

function edgeCacheSeconds(env: Bindings): number {
    const n = Number(env.EDGE_CACHE_SECONDS ?? (env.ENVIRONMENT === "development" ? "0" : "15"));
    return Number.isFinite(n) && n > 0 ? Math.min(n, 300) : 0;
}

async function handle(request: Request, env: Bindings, exec: ExecutionContext, requestId: string): Promise<Response> {
    const url = new URL(request.url);
    const m = match(request.method, url.pathname);
    if (m === null) throw new ApiError(404, "NOT_FOUND", "Route inconnue.");
    if (m === "method") throw new ApiError(405, "METHOD_NOT_ALLOWED", "Méthode non autorisée pour cette route.");
    const ctx: Ctx = {
        request,
        env,
        exec,
        url,
        params: m.params,
        requestId,
        ip: request.headers.get("cf-connecting-ip") ?? "local",
    };
    const anonymous = !request.headers.has("authorization");
    const cacheTtl = m.route.cacheable && anonymous && request.method === "GET" ? edgeCacheSeconds(env) : 0;
    const cache = cacheTtl > 0 ? await caches.open("cosmic-online") : null;
    if (cache) {
        const hit = await cache.match(request);
        if (hit) return hit;
    }
    // While an import replaces the data, the writes of the players answer 503 (reads go on).
    await backups.assertWritable(ctx);
    // The health check must work even when D1 (which stores the counters) is down.
    if (request.method === "GET" && m.route.template !== `${V}/health`) await rateLimit(ctx, RULES.reads, ctx.ip);
    const viewer = m.route.viewer ? await optionalAuth(ctx) : null;
    const response = await m.route.handler(ctx, viewer);
    if (cache && response.status === 200) {
        const cached = new Response(response.body, response);
        cached.headers.set("cache-control", `public, max-age=${cacheTtl}`);
        exec.waitUntil(cache.put(request, cached.clone()));
        return cached;
    }
    return response;
}

function maintenanceCtx(env: Bindings, exec: ExecutionContext): Ctx {
    const url = new URL("https://maintenance.invalid/");
    return { request: new Request(url), env, exec, url, params: {}, requestId: crypto.randomUUID(), ip: "maintenance" };
}

/** Records held PENDING with a verified replay are re-decided with the current rules. */
async function recheckHeldRecords(env: Bindings, exec: ExecutionContext): Promise<number> {
    const ctx = maintenanceCtx(env, exec);
    await notifications.pruneNotifications(ctx);
    const held = await env.DB.prepare(
        "SELECT * FROM records WHERE verification_status = 'PENDING' AND replay_id IS NOT NULL ORDER BY created_at LIMIT 200",
    ).all<RecordRow>();
    let changed = 0;
    for (const r of held.results) if (await records.recheckHeld(ctx, r)) ++changed;
    return changed;
}

/**
 * Daily maintenance: expired sessions, stale rate-limit windows, old audit entries, held
 * records; the challenges of the coming days, weeks and months, the featured creators. Then
 * the backups: the automatic copy and the retention (backups.ts). Nothing of it runs while an
 * import is replacing the data.
 */
async function daily(env: Bindings, exec: ExecutionContext): Promise<void> {
    if (await backups.maintenanceActive(env)) log("info", "maintenance_skipped_import_in_progress");
    else
        try {
            await maintenance(env, exec);
        } catch (e) {
            log("error", "maintenance_failed", { error: e instanceof Error ? e.message : String(e) });
        }
    await backups.scheduledBackups(env, exec);
}

async function maintenance(env: Bindings, exec: ExecutionContext): Promise<void> {
    const now = Date.now();
    const iso = (ms: number) => new Date(ms).toISOString();
    const results = await env.DB.batch([
        env.DB.prepare("DELETE FROM player_sessions WHERE refresh_expires_at < ?1 OR (revoked_at IS NOT NULL AND revoked_at < ?2)").bind(
            iso(now),
            iso(now - 30 * 86400000),
        ),
        env.DB.prepare("DELETE FROM rate_limits WHERE window_start < ?1").bind(Math.floor(now / 1000) - 86400),
        env.DB.prepare("DELETE FROM audit_log WHERE created_at < ?1").bind(iso(now - 180 * 86400000)),
    ]);
    // 2.50: expired migration codes; the usernames of accounts closed by a migration 30 days ago
    // are free again (apart: the cleanup above never depends on that migration being applied).
    let migrations: number[] | null = null;
    try {
        const r = await env.DB.batch([
            env.DB.prepare("DELETE FROM account_migration_codes WHERE expires_at < ?1").bind(iso(now)),
            releaseClosedUsernames(env.DB),
        ]);
        migrations = r.map((x) => x.meta.changes);
    } catch (e) {
        log("error", "migration_maintenance_failed", { error: e instanceof Error ? e.message : String(e) });
    }
    const rechecked = await recheckHeldRecords(env, exec);
    // 2.66: every clan's points, against the current revisions of the EXTRÊME levels.
    await clans.refreshAllClanPoints(env.DB);
    try {
        await raids.pruneRaidReminders(env.DB);
    } catch (e) {
        log("error", "raid_maintenance_failed", { error: e instanceof Error ? e.message : String(e) });
    }
    let challengeRuns: number | null = null;
    let rewardsWon: number | null = null;
    try {
        const m = await challenges.maintenance(maintenanceCtx(env, exec));
        challengeRuns = m.rechecked;
        rewardsWon = m.rewards_won;
    } catch (e) {
        log("error", "challenge_maintenance_failed", { error: e instanceof Error ? e.message : String(e) });
    }
    log("info", "maintenance", {
        deleted: results.map((r) => r.meta.changes),
        // 2.50: [expired migration codes, usernames freed]
        migrations,
        rechecked,
        challenge_runs_rechecked: challengeRuns,
        challenge_rewards_won: rewardsWon,
    });
}

/** The reminders due now (never while an import replaces the data). */
async function raidReminders(env: Bindings): Promise<void> {
    try {
        if (await backups.maintenanceActive(env)) return;
        const sent = await raids.raidTick(env.DB);
        if (sent > 0) log("info", "raid_reminders", { sent });
        // 2.68: the top of the ranking of the events just ended (raidRewards.ts, once per event).
        const tops = await raidRewards.grantEndedTops(env.DB, new Date().toISOString());
        if (tops > 0) log("info", "raid_top_rewards", { granted: tops });
        // 2.69: the world events started (journaled) and those ended: their official result frozen, rewarded, told (once).
        const closed = await raidWorld.worldTick(env.DB);
        if (closed > 0) log("info", "raid_world_closed", { events: closed });
    } catch (e) {
        log("error", "raid_reminders_failed", { error: e instanceof Error ? e.message : String(e) });
    }
}

export default {
    async fetch(request, env, exec): Promise<Response> {
        const requestId = crypto.randomUUID();
        const started = Date.now();
        let response: Response;
        try {
            response = await handle(request, env, exec, requestId);
        } catch (e) {
            if (e instanceof ApiError) response = errorResponse(requestId, e);
            else {
                // Internal details stay in the logs; the client gets a generic message.
                log("error", "unhandled_error", { request_id: requestId, error: e instanceof Error ? e.message : String(e) });
                response = errorResponse(requestId, new ApiError(500, "INTERNAL_ERROR", "Erreur interne du serveur."));
            }
        }
        const url = new URL(request.url);
        log(response.status >= 500 ? "error" : "info", "request", {
            request_id: requestId,
            method: request.method,
            path: url.pathname.replace(/[0-9a-f]{8}-[0-9a-f-]{27}/g, "{id}"),
            status: response.status,
            duration_ms: Date.now() - started,
        });
        return response;
    },
    async scheduled(controller, env, exec): Promise<void> {
        // Every minute: the reminders of the clan raids (raids.ts); else the daily maintenance.
        if (controller.cron === raids.RAID_CRON) exec.waitUntil(raidReminders(env));
        else exec.waitUntil(daily(env, exec));
    },
} satisfies ExportedHandler<Bindings>;
