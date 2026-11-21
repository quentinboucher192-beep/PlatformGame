// Gifts (gifts.ts, migration 0205): an administrator gives rewards of the real catalogue to players; the server
// checks the role from the session, the rewards, the wrap and the message, skips those who already own a reward,
// writes the Journal and notifies; a player sees what a gift holds only once opened, and opening it is what grants
// it (player_won_rewards « gift:<id> », a claimed raid badge), once.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { cleanGiftMessage, giftReward } from "../src/gifts";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";

async function admin(name: string): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

const SUIT = { kind: "suit", id: "suit_or" };
const BADGE = { kind: "badge", id: "communaute_coeur" };

async function send(a: TestPlayer, players: string[], rewards: unknown[] = [SUIT], extra: Record<string, unknown> = {}) {
    return api("POST", `${V}/admin/gifts`, { token: a.token, body: { rewards, players, wrap: "or", message: "Merci !", ...extra } });
}

describe("gifts: the catalogue and the message", () => {
    it("knows only the real rewards (no default, no unknown, a badge the server grants)", () => {
        expect(giftReward("suit", "suit_or")?.name).toBeTruthy();
        expect(giftReward("suit", "suit_standard")).toBeNull(); // a category's default
        expect(giftReward("trail", "suit_or")).toBeNull(); // not in its own category
        expect(giftReward("suit", "suit_nope")).toBeNull();
        expect(giftReward("badge", "communaute_coeur")).not.toBeNull();
        expect(giftReward("badge", "voyage_first")).toBeNull(); // an achievement of the game: never given
        expect(giftReward("hat", "x")).toBeNull();
    });
    it("trims the message, removes control characters, refuses more than 140", () => {
        expect(cleanGiftMessage("  Bravo\u0007 à\ttous\n ")).toBe("Bravo àtous");
        expect(cleanGiftMessage("é".repeat(140))).toBe("é".repeat(140));
        expect(cleanGiftMessage("x".repeat(141))).toBeNull();
    });
});

describe("gifts: an administrator sends", () => {
    it("exists only for an administrator (the session's role)", async () => {
        const p = await registerPlayer("Lyra");
        const q = await registerPlayer("Vega");
        for (const [m, path] of [
            ["POST", `${V}/admin/gifts`],
            ["GET", `${V}/admin/gifts`],
            ["GET", `${V}/admin/gifts/players`],
            ["POST", `${V}/admin/gifts/1/cancel`],
        ] as const) {
            const r = await api(m, path, { token: p.token, body: m === "POST" ? { rewards: [SUIT], players: [q.id], wrap: "or" } : undefined });
            expect(r.status, path).toBe(404);
        }
        expect((await api("GET", `${V}/gifts/me`)).status).toBe(401);
    });

    it("checks the rewards, the wrap, the message and the players", async () => {
        const a = await admin("NovaRider");
        const p = await registerPlayer("Lyra");
        expect((await send(a, [p.id], [{ kind: "suit", id: "suit_standard" }])).status).toBe(400);
        expect((await send(a, [p.id], [{ kind: "badge", id: "voyage_first" }])).status).toBe(400);
        expect((await send(a, [p.id], [SUIT], { wrap: "papier" })).status).toBe(400);
        expect((await send(a, [p.id], [SUIT], { message: "x".repeat(141) })).status).toBe(400);
        expect((await send(a, [p.id], [SUIT], { extra: 1 })).status).toBe(400);
        expect((await send(a, [], [SUIT])).status).toBe(400);
        // 2.85: no limit on the players nor the rewards — but they must exist (and "all" is the only word allowed).
        expect((await send(a, Array.from({ length: 101 }, (_, i) => `player-${String(i).padStart(4, "0")}`))).status).toBe(404);
        expect((await send(a, [p.id], [])).status).toBe(400);
        expect((await api("POST", `${V}/admin/gifts`, { token: a.token, body: { rewards: [SUIT], players: "everyone", wrap: "or" } })).status).toBe(400);
        const unknown = await send(a, ["ffffffff-ffff-4fff-8fff-ffffffffffff"]);
        expect(unknown.status).toBe(404);
        expect(unknown.body.error.code).toBe("PLAYER_NOT_FOUND");
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM gifts").first<{ n: number }>())?.n).toBe(0);
    });

    it("skips the players who already own a reward, notifies the others, writes the Journal", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        const vega = await registerPlayer("Vega");
        // Vega already won the suit (a challenge); Lyra gets two gifts.
        await env.DB.prepare("INSERT INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at) VALUES (?1, 'daily-2026-11-01', 'item', 'suit_or', '2026-11-02')")
            .bind(vega.id)
            .run();
        const r = await send(a, [lyra.id, vega.id], [SUIT, BADGE], { message: "  Merci d'avoir été là\u0000 !  " });
        expect(r.status, JSON.stringify(r.body)).toBe(201);
        expect(r.body.data.created).toBe(3);
        expect(r.body.data.skipped).toEqual([{ player_id: vega.id, username: "Vega", reward: "suit:suit_or", reason: "owned" }]);
        const sendView = r.body.data.send;
        expect(sendView.total).toBe(3);
        expect(sendView.opened).toBe(0);
        expect(sendView.message).toBe("Merci d'avoir été là !");
        // The same again: Lyra has both waiting, Vega the badge waiting and the suit owned.
        const again = await send(a, [lyra.id, vega.id], [SUIT, BADGE]);
        expect(again.status).toBe(409);
        expect(again.body.error.code).toBe("NOTHING_TO_SEND");
        // One notification each, merged: its count is the gifts received.
        const notes = await env.DB.prepare("SELECT player_id, count, kind FROM notifications WHERE kind = 'gift' ORDER BY count").all<{ player_id: string; count: number }>();
        expect(notes.results.map((n) => [n.player_id, n.count])).toEqual([
            [vega.id, 1],
            [lyra.id, 2],
        ]);
        const list = await api("GET", `${V}/notifications`, { token: lyra.token });
        expect(list.body.data.notifications[0].text).toBe("Vous avez reçu 2 cadeaux");
        // The Journal: who sent what to how many.
        const log = await api("GET", `${V}/admin/log?kind=gift`, { token: a.token });
        expect(log.status).toBe(200);
        const e = log.body.data.entries[0];
        expect(e.action).toBe("admin_gift_sent");
        expect(e.gift.gifts).toBe(3);
        expect(e.gift.skipped).toBe(1);
        expect(e.gift.rewards.map((x: { id: string }) => x.id)).toEqual(["suit_or", "communaute_coeur"]);
    });

    it("lists the players with what they own among the rewards chosen", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        const vega = await registerPlayer("Vega");
        await env.DB.prepare("INSERT INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at) VALUES (?1, 'x', 'item', 'suit_or', 'x')").bind(lyra.id).run();
        await send(a, [vega.id], [BADGE]);
        const r = await api("GET", `${V}/admin/gifts/players?filter=all&rewards=suit:suit_or,badge:communaute_coeur,suit:nope`, { token: a.token });
        expect(r.status).toBe(200);
        const by = new Map(r.body.data.players.map((p: { username: string }) => [p.username, p]));
        expect((by.get("Lyra") as { owned: string[] }).owned).toEqual(["suit:suit_or"]);
        expect((by.get("Vega") as { pending: string[] }).pending).toEqual(["badge:communaute_coeur"]);
        const ids = await api("GET", `${V}/admin/gifts/players?filter=ids&ids=${vega.id}`, { token: a.token });
        expect(ids.body.data.players.map((p: { id: string }) => p.id)).toEqual([vega.id]);
        const clan = await api("GET", `${V}/admin/gifts/players?filter=clan`, { token: a.token });
        expect(clan.body.data).toEqual({ players: [], has_more: false, clan: null });
    });
});

describe("gifts: the player opens them", () => {
    it("hides the content until opened; opening grants the item once (idempotent)", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        await send(a, [lyra.id], [SUIT]);
        const mine = await api("GET", `${V}/gifts/me`, { token: lyra.token });
        expect(mine.status).toBe(200);
        expect(mine.body.data.unopened).toBe(1);
        const g = mine.body.data.gifts[0];
        expect(g.reward).toBeNull();
        expect(g.opened).toBe(false);
        expect(g.from.username).toBe("NovaRider");
        expect(g.wrap).toBe("or");
        expect(JSON.stringify(g)).not.toContain("suit_or");
        // Someone else cannot open it.
        const other = await registerPlayer("Vega");
        expect((await api("POST", `${V}/gifts/${g.id}/open`, { token: other.token })).status).toBe(404);
        const o = await api("POST", `${V}/gifts/${g.id}/open`, { token: lyra.token });
        expect(o.status).toBe(200);
        expect(o.body.data.opened_now).toBe(true);
        expect(o.body.data.gift.reward).toEqual({ kind: "suit", id: "suit_or", name: giftReward("suit", "suit_or")!.name });
        const twice = await api("POST", `${V}/gifts/${g.id}/open`, { token: lyra.token });
        expect(twice.body.data.opened_now).toBe(false);
        const won = await env.DB.prepare("SELECT challenge_id, reward_ref FROM player_won_rewards WHERE player_id = ?1").bind(lyra.id).all();
        expect(won.results).toEqual([{ challenge_id: `gift:${g.id}`, reward_ref: "suit_or" }]);
        // The game's list of rewards won has it (the Collection unlocks it, offline too).
        const rewards = await api("GET", `${V}/players/me/rewards`, { token: lyra.token });
        expect(rewards.body.data.rewards.map((x: { ref: string }) => x.ref)).toContain("suit_or");
        // Worn online: the server accepts it.
        const worn = await api("PUT", `${V}/players/me/cosmetics`, { token: lyra.token, body: { suit: "suit_or", trail: "trail_none", frame: "frame_simple", title: "title_none", victory: "victory_classic" } });
        expect(worn.status, JSON.stringify(worn.body)).toBe(200);
        expect(JSON.stringify(worn.body.data)).toContain("suit_or");
        const after = await api("GET", `${V}/gifts/me`, { token: lyra.token });
        expect(after.body.data.unopened).toBe(0);
        expect(after.body.data.gifts[0].reward.id).toBe("suit_or");
    });

    it("a badge: a claimed raid reward, whose achievement the player may then declare", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        await send(a, [lyra.id], [BADGE]);
        const id = (await api("GET", `${V}/gifts/me`, { token: lyra.token })).body.data.gifts[0].id;
        const before = await api("PUT", `${V}/players/me/achievements`, { token: lyra.token, body: { unlocked: ["communaute_coeur"] } });
        expect(JSON.stringify(before.body.data)).not.toContain("communaute_coeur");
        const o = await api("POST", `${V}/gifts/${id}/open`, { token: lyra.token });
        expect(o.body.data.raid_rewards_claimed).toContain("coeur_badge");
        await api("PUT", `${V}/players/me/achievements`, { token: lyra.token, body: { unlocked: ["communaute_coeur"] } });
        const row = await env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(lyra.id).first<{ ids: string }>();
        expect(JSON.parse(row!.ids)).toContain("communaute_coeur");
    });

    it("the history counts what was opened; cancelling withdraws only the unopened gifts", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        const vega = await registerPlayer("Vega");
        const s = await send(a, [lyra.id, vega.id], [SUIT]);
        const sendId = s.body.data.send.id;
        const lyraGift = (await api("GET", `${V}/gifts/me`, { token: lyra.token })).body.data.gifts[0].id;
        await api("POST", `${V}/gifts/${lyraGift}/open`, { token: lyra.token });
        const h = await api("GET", `${V}/admin/gifts`, { token: a.token });
        expect(h.status).toBe(200);
        expect(h.body.data.sends[0]).toMatchObject({ id: sendId, total: 2, opened: 1, cancelled: 0, waiting: 1, recipients: 2, names: ["Lyra", "Vega"] });
        const c = await api("POST", `${V}/admin/gifts/${sendId}/cancel`, { token: a.token });
        expect(c.status).toBe(200);
        expect(c.body.data.cancelled).toBe(1);
        expect(c.body.data.send).toMatchObject({ opened: 1, cancelled: 1, waiting: 0 });
        // Vega's gift is gone; Lyra keeps hers.
        const vegaList = await api("GET", `${V}/gifts/me`, { token: vega.token });
        expect(vegaList.body.data.gifts).toEqual([]);
        const vegaGift = await env.DB.prepare("SELECT id FROM gifts WHERE recipient_id = ?1").bind(vega.id).first<{ id: number }>();
        expect((await api("POST", `${V}/gifts/${vegaGift!.id}/open`, { token: vega.token })).status).toBe(404);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM player_won_rewards WHERE player_id = ?1").bind(vega.id).first<{ n: number }>())?.n).toBe(0);
        // Nothing more to cancel; the Journal has the cancellation.
        expect((await api("POST", `${V}/admin/gifts/${sendId}/cancel`, { token: a.token })).body.data.cancelled).toBe(0);
        const log = await api("GET", `${V}/admin/log?kind=gift`, { token: a.token });
        expect(log.body.data.entries.map((e: { action: string }) => e.action)).toEqual(["admin_gift_cancelled", "admin_gift_sent"]);
        expect((await api("POST", `${V}/admin/gifts/999/cancel`, { token: a.token })).status).toBe(404);
    });
});

// « Colis » (migration 0210): `bundle: true` gives each player ONE parcel with the rewards they do not own yet; it
// shows as one wrapped entry that tells neither what it holds nor how many, and opening it grants everything once.
describe("gifts: parcels (« Colis »)", () => {
    const TRAIL = { kind: "trail", id: "trail_comet" };

    it("one parcel per player with what each does not own; skipped when everything is owned; the notification counts one", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        const vega = await registerPlayer("Vega");
        const orion = await registerPlayer("Orion");
        // Vega owns the suit (two rewards left); Orion owns the suit and the trail, and waits for the badge.
        for (const [p, ref] of [
            [vega.id, "suit_or"],
            [orion.id, "suit_or"],
            [orion.id, "trail_comet"],
        ] as const)
            await env.DB.prepare("INSERT INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at) VALUES (?1, 'x', 'item', ?2, 'x')")
                .bind(p, ref)
                .run();
        await send(a, [orion.id], [BADGE]);
        expect((await send(a, [lyra.id], [SUIT, TRAIL], { bundle: "yes" })).status).toBe(400);
        const r = await send(a, [lyra.id, vega.id, orion.id], [SUIT, TRAIL, BADGE], { bundle: true });
        expect(r.status, JSON.stringify(r.body)).toBe(201);
        expect(r.body.data.parcels).toBe(2);
        expect(r.body.data.created).toBe(5); // Lyra 3, Vega 2
        expect(r.body.data.skipped.filter((s: { player_id: string }) => s.player_id === orion.id).length).toBe(3);
        expect(r.body.data.send).toMatchObject({ bundle: true, total: 2, opened: 0, waiting: 2, recipients: 2 });
        const rows = await env.DB.prepare("SELECT recipient_id, rewards FROM gift_parcels ORDER BY id").all<{ recipient_id: string; rewards: string }>();
        expect(rows.results.map((x) => [x.recipient_id, JSON.parse(x.rewards).map((y: { id: string }) => y.id)])).toEqual([
            [lyra.id, ["suit_or", "trail_comet", "communaute_coeur"]],
            [vega.id, ["trail_comet", "communaute_coeur"]],
        ]);
        // No gift is written before a parcel is opened (no id spent).
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM gifts WHERE recipient_id IN (?1, ?2)").bind(lyra.id, vega.id).first<{ n: number }>())?.n).toBe(0);
        // The notification counts the parcel as one.
        const note = await env.DB.prepare("SELECT count FROM notifications WHERE kind = 'gift' AND player_id = ?1").bind(lyra.id).first<{ count: number }>();
        expect(note?.count).toBe(1);
        // One reward left for a player: still a parcel.
        const single = await send(a, [vega.id], [SUIT, { kind: "frame", id: "frame_gold" }], { bundle: true });
        expect(single.status, JSON.stringify(single.body)).toBe(201);
        expect(single.body.data).toMatchObject({ parcels: 1, created: 1 });
        // What waits in a parcel is « pending » for the tab, and a second parcel of the same skips it.
        const tags = await api("GET", `${V}/admin/gifts/players?filter=ids&ids=${lyra.id}&rewards=trail:trail_comet`, { token: a.token });
        expect(tags.body.data.players[0].pending).toEqual(["trail:trail_comet"]);
        expect((await send(a, [lyra.id], [SUIT, TRAIL], { bundle: true })).status).toBe(409);
        // The Journal, the history.
        const log = await api("GET", `${V}/admin/log?kind=gift`, { token: a.token });
        expect(log.body.data.entries[1].gift).toMatchObject({ parcels: 2, gifts: 5, recipients: 2 });
    });

    it("never tells what a wrapped parcel holds nor how many; opening grants all, in order, once", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        await send(a, [lyra.id], [SUIT, TRAIL, BADGE], { bundle: true });
        const mine = await api("GET", `${V}/gifts/me`, { token: lyra.token });
        expect(mine.body.data.unopened).toBe(1);
        expect(mine.body.data.total).toBe(1);
        const p = mine.body.data.gifts[0];
        expect(p).toMatchObject({ parcel: true, opened: false, reward: null, contents: null, wrap: "or" });
        const text = JSON.stringify(mine.body.data);
        for (const secret of ["suit_or", "trail_comet", "communaute_coeur", "count", "items"]) expect(text).not.toContain(secret);
        expect(Object.keys(p).sort()).toEqual(["contents", "created_at", "from", "id", "message", "opened", "opened_at", "parcel", "reward", "wrap"]);
        // Not someone else's; not a number.
        const vega = await registerPlayer("Vega");
        expect((await api("POST", `${V}/gifts/parcels/${p.id}/open`, { token: vega.token })).status).toBe(404);
        expect((await api("POST", `${V}/gifts/parcels/abc/open`, { token: lyra.token })).status).toBe(404);
        const o = await api("POST", `${V}/gifts/parcels/${p.id}/open`, { token: lyra.token });
        expect(o.status, JSON.stringify(o.body)).toBe(200);
        expect(o.body.data.opened_now).toBe(true);
        expect(o.body.data.items.map((x: { id: string }) => x.id)).toEqual(["suit_or", "trail_comet", "communaute_coeur"]);
        expect(o.body.data.gift).toMatchObject({ parcel: true, opened: true });
        expect(o.body.data.gift.contents).toEqual(o.body.data.items);
        expect(o.body.data.raid_rewards_claimed).toContain("coeur_badge");
        const twice = await api("POST", `${V}/gifts/parcels/${p.id}/open`, { token: lyra.token });
        expect(twice.body.data.opened_now).toBe(false);
        expect(twice.body.data.items).toEqual(o.body.data.items);
        // Granted through the usual ownership, once: one gift per reward, in order.
        const gifts = await env.DB.prepare("SELECT id, reward_id, opened_at FROM gifts WHERE parcel_id = ?1 ORDER BY id")
            .bind(p.id)
            .all<{ id: number; reward_id: string; opened_at: string | null }>();
        expect(gifts.results.map((g) => g.reward_id)).toEqual(["suit_or", "trail_comet", "communaute_coeur"]);
        expect(gifts.results.every((g) => g.opened_at !== null)).toBe(true);
        const won = await env.DB.prepare("SELECT challenge_id, reward_ref FROM player_won_rewards WHERE player_id = ?1 ORDER BY reward_ref").bind(lyra.id).all();
        expect(won.results).toEqual([
            { challenge_id: `gift:${gifts.results[0]!.id}`, reward_ref: "suit_or" },
            { challenge_id: `gift:${gifts.results[1]!.id}`, reward_ref: "trail_comet" },
        ]);
        const badge = await env.DB.prepare("SELECT event_id FROM raid_reward_grants WHERE player_id = ?1 AND claimed_at IS NOT NULL").bind(lyra.id).all();
        expect(badge.results).toEqual([{ event_id: `gift:${gifts.results[2]!.id}` }]);
        // The gifts it held are neither listed nor opened apart; the list shows it opened with its contents.
        expect((await api("POST", `${V}/gifts/${gifts.results[0]!.id}/open`, { token: lyra.token })).status).toBe(404);
        const after = await api("GET", `${V}/gifts/me`, { token: lyra.token });
        expect(after.body.data.total).toBe(1);
        expect(after.body.data.unopened).toBe(0);
        expect(after.body.data.gifts[0].contents.map((x: { id: string }) => x.id)).toEqual(["suit_or", "trail_comet", "communaute_coeur"]);
    });

    it("lists parcels with gifts, newest first; the history says « bundle »; cancelling withdraws the unopened parcels", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        const vega = await registerPlayer("Vega");
        await send(a, [lyra.id], [{ kind: "frame", id: "frame_gold" }]);
        const s = await send(a, [lyra.id, vega.id], [SUIT, TRAIL], { bundle: true });
        const sendId = s.body.data.send.id;
        const mine = await api("GET", `${V}/gifts/me`, { token: lyra.token });
        expect(mine.body.data.gifts.map((g: { parcel: boolean }) => g.parcel)).toEqual([true, false]);
        await api("POST", `${V}/gifts/parcels/${mine.body.data.gifts[0].id}/open`, { token: lyra.token });
        const h = await api("GET", `${V}/admin/gifts`, { token: a.token });
        expect(h.body.data.sends[0]).toMatchObject({ id: sendId, bundle: true, total: 2, opened: 1, waiting: 1, recipients: 2, names: ["Lyra", "Vega"] });
        expect(h.body.data.sends[1]).toMatchObject({ bundle: false, total: 1 });
        const c = await api("POST", `${V}/admin/gifts/${sendId}/cancel`, { token: a.token });
        expect(c.body.data.cancelled).toBe(1);
        expect(c.body.data.send).toMatchObject({ opened: 1, cancelled: 1, waiting: 0 });
        expect((await api("GET", `${V}/gifts/me`, { token: vega.token })).body.data.gifts).toEqual([]);
        const vegaParcel = await env.DB.prepare("SELECT id FROM gift_parcels WHERE recipient_id = ?1").bind(vega.id).first<{ id: number }>();
        expect((await api("POST", `${V}/gifts/parcels/${vegaParcel!.id}/open`, { token: vega.token })).status).toBe(404);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM player_won_rewards WHERE player_id = ?1").bind(vega.id).first<{ n: number }>())?.n).toBe(0);
        const log = await api("GET", `${V}/admin/log?kind=gift`, { token: a.token });
        expect(log.body.data.entries[0]).toMatchObject({ action: "admin_gift_cancelled", gift: { cancelled: 1, parcels: 2 } });
    });
});
