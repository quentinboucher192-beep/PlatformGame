// 2.85: « Cadeaux » without limits — any number of rewards of the real catalogue, any number of players, or every
// player (`players: "all"`). A send is written in steps (vitest.config.ts: 100 players per step, 2 steps per
// request), invisible until it is whole; the same request (same `request_key`) goes on where it stopped, and once
// sent answers the same send again: nothing is ever given twice.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import cosmeticCatalog from "../src/cosmeticCatalog.json";
import { unlocked, type CosmeticItem } from "../src/cosmetics";
import { giftReward } from "../src/gifts";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";

/** `n` rewards of the catalogue a new player does not own yet. */
function rewards(n: number): { kind: string; id: string }[] {
    const fresh = { stars: 0, ids: new Set<string>(), won: { items: new Set<string>(), titles: new Set<string>(), suits: new Set<string>(), looks: new Set<string>() } };
    const out = (cosmeticCatalog as unknown as { items: CosmeticItem[] }).items
        .filter((i) => giftReward(i.category, i.id) !== null && !unlocked(i, fresh as never))
        .map((i) => ({ kind: i.category, id: i.id }));
    expect(out.length).toBeGreaterThanOrEqual(n);
    return out.slice(0, n);
}

async function admin(name: string): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

/** `n` active players written straight into the database (as many as a real server has). */
async function manyPlayers(n: number): Promise<string[]> {
    const now = new Date().toISOString();
    const ids: string[] = [];
    for (let i = 0; i < n; i += 50) {
        const stmts = [];
        for (let k = i; k < Math.min(n, i + 50); ++k) {
            const id = `00000000-0000-4000-8000-${String(k).padStart(12, "0")}`;
            ids.push(id);
            stmts.push(
                env.DB.prepare(
                    "INSERT INTO players (id, username, username_normalized, tos_version, tos_accepted_at, created_at, last_seen_at) VALUES (?1, ?2, ?3, '2026-11', ?4, ?4, ?4)",
                ).bind(id, `Joueur${k}`, `joueur${k}`, now),
            );
        }
        await env.DB.batch(stmts);
    }
    return ids;
}

const count = async (sql: string, ...binds: unknown[]) => (await env.DB.prepare(sql).bind(...binds).first<{ n: number }>())?.n ?? 0;

/** POSTs the send, then again with its key while it answers 202 (as the game does). */
async function sendAll(a: TestPlayer, body: Record<string, unknown>) {
    const notes = await count("SELECT COUNT(*) AS n FROM notifications WHERE kind = 'gift'");
    const sends = (await api("GET", `${V}/admin/gifts`, { token: a.token })).body.data.sends.length;
    let r = await api("POST", `${V}/admin/gifts`, { token: a.token, body: { wrap: "or", message: "Pour tous", ...body } });
    let rounds = 1;
    while (r.status === 202) {
        // Nothing is shown before the send is whole.
        expect(await count("SELECT COUNT(*) AS n FROM notifications WHERE kind = 'gift'")).toBe(notes);
        expect((await api("GET", `${V}/admin/gifts`, { token: a.token })).body.data.sends).toHaveLength(sends);
        r = await api("POST", `${V}/admin/gifts`, { token: a.token, body: { wrap: "or", message: "Pour tous", ...body, request_key: r.body.data.request_key } });
        ++rounds;
    }
    return { r, rounds };
}

describe("gifts without limits (2.85)", () => {
    it("300 players and 30 rewards in one send: written in steps, whole, notified once", async () => {
        const a = await admin("NovaRider");
        const players = await manyPlayers(300);
        const list = rewards(30);
        const { r, rounds } = await sendAll(a, { players, rewards: list });
        expect(r.status, JSON.stringify(r.body).slice(0, 300)).toBe(201);
        expect(rounds).toBe(2); // 100 players per step, 2 steps per request
        expect(r.body.data.created).toBe(9000);
        expect(r.body.data.skipped_count).toBe(0);
        expect(r.body.data.send).toMatchObject({ recipients: 300, total: 9000, waiting: 9000 });
        expect(r.body.data.send.rewards).toHaveLength(30);
        expect(await count("SELECT COUNT(*) AS n FROM gifts")).toBe(9000);
        expect(await count("SELECT COUNT(*) AS n FROM notifications WHERE kind = 'gift' AND count = 30")).toBe(300);
        expect(await count("SELECT COUNT(*) AS n FROM audit_log WHERE action = 'admin_gift_sent'")).toBe(1);
    });

    it("a parcel holds any number of rewards", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        const list = rewards(30);
        const r = await api("POST", `${V}/admin/gifts`, { token: a.token, body: { players: [lyra.id], rewards: list, wrap: "givre", bundle: true } });
        expect(r.status).toBe(201);
        expect(r.body.data).toMatchObject({ created: 30, parcels: 1 });
        const mine = await api("GET", `${V}/gifts/me`, { token: lyra.token });
        const parcel = mine.body.data.gifts.find((g: { parcel?: boolean }) => g.parcel);
        const opened = await api("POST", `${V}/gifts/parcels/${parcel.id}/open`, { token: lyra.token });
        expect(opened.status).toBe(200);
        expect(opened.body.data.gift.contents).toHaveLength(30);
    });

    it("« all »: every active player, the inactive ones left out, the rest skipped as usual", async () => {
        const a = await admin("NovaRider");
        const lyra = await registerPlayer("Lyra");
        const players = await manyPlayers(250);
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(players[0]).run();
        const [suit] = rewards(1);
        // Lyra already has it waiting: skipped.
        expect((await api("POST", `${V}/admin/gifts`, { token: a.token, body: { players: [lyra.id], rewards: [suit], wrap: "or" } })).status).toBe(201);
        const listed = await api("GET", `${V}/admin/gifts/players?filter=all&limit=1`, { token: a.token });
        const active = listed.body.data.active_players;
        expect(active).toBe(251); // the administrator, Lyra, 249 of the 250 players
        const { r, rounds } = await sendAll(a, { players: "all", rewards: [suit] });
        expect(r.status).toBe(201);
        expect(rounds).toBe(2);
        expect(r.body.data.created).toBe(active - 1);
        expect(r.body.data.skipped_count).toBe(1);
        expect(r.body.data.skipped).toMatchObject([{ player_id: lyra.id, reason: "pending" }]);
        expect(await count("SELECT COUNT(*) AS n FROM gifts WHERE recipient_id = ?1", players[0])).toBe(0);
        const audit = await env.DB.prepare("SELECT details FROM audit_log WHERE action = 'admin_gift_sent' ORDER BY rowid DESC LIMIT 1").first<{ details: string }>();
        expect(JSON.parse(audit!.details).gift).toMatchObject({ all_players: true, recipients: active - 1 });
    });

    it("is idempotent with its request key: sent again, the same send (nothing twice); another request with it: refused", async () => {
        const a = await admin("NovaRider");
        const players = await manyPlayers(150);
        const list = rewards(3);
        const key = "gift-send-0001";
        const first = await api("POST", `${V}/admin/gifts`, { token: a.token, body: { players, rewards: list, wrap: "or", request_key: key } });
        expect(first.status).toBe(201); // 2 steps: one request
        const again = await api("POST", `${V}/admin/gifts`, { token: a.token, body: { players, rewards: list, wrap: "or", request_key: key } });
        expect(again.status).toBe(200);
        expect(again.body.data).toMatchObject({ replayed: true, created: 450, request_key: key });
        expect(again.body.data.send.id).toBe(first.body.data.send.id);
        expect(await count("SELECT COUNT(*) AS n FROM gifts")).toBe(450);
        expect(await count("SELECT COUNT(*) AS n FROM gift_sends")).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM notifications WHERE kind = 'gift' AND count = 3")).toBe(150);
        expect(await count("SELECT COUNT(*) AS n FROM audit_log WHERE action = 'admin_gift_sent'")).toBe(1);
        const other = await api("POST", `${V}/admin/gifts`, { token: a.token, body: { players, rewards: list.slice(0, 1), wrap: "or", request_key: key } });
        expect(other.status).toBe(409);
        expect(other.body.error.code).toBe("REQUEST_KEY_REUSED");
    });

    it("a send stopped half-way is never seen, goes on where it stopped, and an abandoned one goes away", async () => {
        const a = await admin("NovaRider");
        const players = await manyPlayers(450); // 5 steps: 3 requests
        const [suit] = rewards(1);
        const body = { players, rewards: [suit], wrap: "or", request_key: "gift-send-half" };
        const r1 = await api("POST", `${V}/admin/gifts`, { token: a.token, body });
        expect(r1.status).toBe(202);
        expect(r1.body.data).toMatchObject({ state: "writing", done: 200, total: 450 });
        // Written but invisible: not in a recipient's gifts, not in the history, not cancellable.
        expect(await count("SELECT COUNT(*) AS n FROM gifts")).toBe(200);
        const sendId = (await env.DB.prepare("SELECT id FROM gift_sends").first<{ id: number }>())!.id;
        expect((await api("POST", `${V}/admin/gifts/${sendId}/cancel`, { token: a.token })).status).toBe(404);
        // The same request again (a lost answer, the same step twice at worst): it goes on, nothing doubled.
        await env.DB.prepare("UPDATE gift_sends SET progress = '100'").run(); // as if the step's answer were lost
        const r2 = await api("POST", `${V}/admin/gifts`, { token: a.token, body });
        expect(r2.status).toBe(202);
        const r3 = await api("POST", `${V}/admin/gifts`, { token: a.token, body });
        expect(r3.status).toBe(201);
        expect(r3.body.data.created).toBe(450);
        expect(await count("SELECT COUNT(*) AS n FROM gifts")).toBe(450);
        expect(await count("SELECT COUNT(*) AS n FROM notifications WHERE kind = 'gift' AND count = 1")).toBe(450);
        // Another send left 'writing' for more than an hour: removed (with its gifts) by the next send.
        const r4 = await api("POST", `${V}/admin/gifts`, { token: a.token, body: { ...body, rewards: rewards(2).slice(1), request_key: "gift-send-left" } });
        expect(r4.status).toBe(202);
        await env.DB.prepare("UPDATE gift_sends SET touched_at = '2020-01-01T00:00:00.000Z' WHERE state = 'writing'").run();
        const lyra = await registerPlayer("Lyra");
        expect((await api("POST", `${V}/admin/gifts`, { token: a.token, body: { players: [lyra.id], rewards: [suit], wrap: "or" } })).status).toBe(201);
        expect(await count("SELECT COUNT(*) AS n FROM gift_sends WHERE state = 'writing'")).toBe(0);
        expect(await count("SELECT COUNT(*) AS n FROM gifts")).toBe(451);
    });
});
