// Clans (2.66): one clan per player, 50 members at most, a chief, officers and members, joining
// by invitation, application or freely, the clan's discussion (a group conversation that follows
// its members), its points (the best verified result per official EXTRÊME level), the tag next
// to the username (boards, profile, SOCIAL), the moderation of the administrators (Journal).
// Every rule two requests could race on is checked with requests sent at the same time.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { refreshAllClanPoints } from "../src/clans";
import { api, buildReplay, EXIT, LEVEL2_HASH, LEVEL_HASH, registerPlayer, seedAll, SPAWN, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };

async function createClan(p: TestPlayer, extra: Record<string, unknown> = {}) {
    const r = await api("POST", `${V}/clans`, {
        token: p.token,
        body: { name: "Les Gardiens de l'Aube", tag: "AUBE", motto: "Une phase à la fois.", emblem: EMBLEM, join_policy: "open", ...extra },
    });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    return r.body.data as { clan: { id: string; tag: string }; conversation_id: string };
}

const page = (id: string, p?: TestPlayer) => api("GET", `${V}/clans/${id}`, { token: p?.token });
const join = (id: string, p: TestPlayer, message?: string) => api("POST", `${V}/clans/${id}/join`, { token: p.token, body: message === undefined ? {} : { message } });
const leave = (id: string, p: TestPlayer) => api("DELETE", `${V}/clans/${id}/members/${p.id}`, { token: p.token });
const kick = (id: string, by: TestPlayer, target: TestPlayer) => api("DELETE", `${V}/clans/${id}/members/${target.id}`, { token: by.token });
const setRole = (id: string, by: TestPlayer, target: TestPlayer, role: string) =>
    api("PATCH", `${V}/clans/${id}/members/${target.id}`, { token: by.token, body: { role } });
const transfer = (id: string, by: TestPlayer, target: TestPlayer) => api("POST", `${V}/clans/${id}/transfer`, { token: by.token, body: { player_id: target.id } });
const invite = (id: string, by: TestPlayer, target: TestPlayer) => api("POST", `${V}/clans/${id}/invites`, { token: by.token, body: { username: target.username } });
const answer = (id: string, by: TestPlayer, target: TestPlayer, accept: boolean) =>
    api("POST", `${V}/clans/${id}/requests/${target.id}`, { token: by.token, body: { accept } });

async function rolesOf(clanId: string): Promise<Record<string, string>> {
    const r = await env.DB.prepare("SELECT player_id, role FROM clan_members WHERE clan_id = ?1").bind(clanId).all<{ player_id: string; role: string }>();
    return Object.fromEntries(r.results.map((x) => [x.player_id, x.role]));
}

async function leaders(clanId: string): Promise<number> {
    const r = await env.DB.prepare("SELECT COUNT(*) AS n FROM clan_members WHERE clan_id = ?1 AND role = 'leader'").bind(clanId).first<{ n: number }>();
    return r!.n;
}

/** A clan with its chief, `officers` officers and `members` members (all through the API). */
async function team(officers: number, members: number) {
    const chief = await registerPlayer("Chef");
    const { clan } = await createClan(chief);
    const off: TestPlayer[] = [];
    const mem: TestPlayer[] = [];
    for (let i = 0; i < officers; ++i) {
        const p = await registerPlayer(`Officier${i + 1}`);
        expect((await join(clan.id, p)).status).toBe(200);
        expect((await setRole(clan.id, chief, p, "officer")).status).toBe(200);
        off.push(p);
    }
    for (let i = 0; i < members; ++i) {
        const p = await registerPlayer(`Membre${i + 1}`);
        expect((await join(clan.id, p)).status).toBe(200);
        mem.push(p);
    }
    return { id: clan.id, chief, off, mem };
}

/** `n` extra members written directly (the 50-member tests). */
async function fill(clanId: string, n: number): Promise<void> {
    const now = new Date().toISOString();
    const stmts = [];
    for (let i = 0; i < n; ++i) {
        const id = crypto.randomUUID();
        stmts.push(
            env.DB.prepare(
                "INSERT INTO players (id, username, username_normalized, tos_version, tos_accepted_at, created_at, last_seen_at) VALUES (?1, ?2, ?3, '2026-11', ?4, ?4, ?4)",
            ).bind(id, `Rempli${i}`, `rempli${i}`, now),
            env.DB.prepare("INSERT INTO clan_members (player_id, clan_id, role, joined_at) VALUES (?1, ?2, 'member', ?3)").bind(id, clanId, now),
        );
    }
    await env.DB.batch(stmts);
}

async function count(clanId: string): Promise<number> {
    return (await env.DB.prepare("SELECT COUNT(*) AS n FROM clan_members WHERE clan_id = ?1").bind(clanId).first<{ n: number }>())!.n;
}

describe("clans: creation, discussion, validation", () => {
    it("creates a clan with its discussion; one clan per player; a tag unique whatever the case", async () => {
        const nova = await registerPlayer("Nova");
        expect((await api("POST", `${V}/clans`, { body: { name: "Sans compte", tag: "SANS", emblem: EMBLEM, join_policy: "open" } })).status).toBe(401);
        const { clan, conversation_id } = await createClan(nova);
        expect(clan).toMatchObject({ name: "Les Gardiens de l'Aube", tag: "AUBE", members: 1, max_members: 50, join_policy: "open", role: "leader" });
        expect((clan as any).emblem).toEqual({ shape: "star", primary: "#ffd27a", secondary: "#a66bff" });

        const again = await api("POST", `${V}/clans`, { token: nova.token, body: { name: "Un autre clan", tag: "AUTRE", emblem: EMBLEM, join_policy: "open" } });
        expect(again.status).toBe(409);
        expect(again.body.error.code).toBe("ALREADY_IN_CLAN");
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM clans").first<{ n: number }>()).toEqual({ n: 1 });

        const orion = await registerPlayer("Orion");
        const taken = await api("POST", `${V}/clans`, { token: orion.token, body: { name: "Copie", tag: "aube", emblem: EMBLEM, join_policy: "open" } });
        expect(taken.status).toBe(409);
        expect(taken.body.error.code).toBe("TAG_TAKEN");

        // The discussion is a group conversation of SOCIAL, with the clan's tag.
        const convs = await api("GET", `${V}/social/conversations`, { token: nova.token });
        expect(convs.body.data.conversations[0]).toMatchObject({ id: conversation_id, kind: "group", name: "Les Gardiens de l'Aube", clan: { id: clan.id, tag: "AUBE" } });
        expect((await api("POST", `${V}/social/conversations/${conversation_id}/messages`, { token: nova.token, body: { body: "Bienvenue !" } })).status).toBe(201);
        // It is managed from the clan, never as a plain group.
        const rename = await api("PATCH", `${V}/social/groups/${conversation_id}`, { token: nova.token, body: { name: "Autre" } });
        expect(rename.body.error.code).toBe("CLAN_CONVERSATION");
        expect((await api("DELETE", `${V}/social/groups/${conversation_id}/members/${nova.id}`, { token: nova.token })).body.error.code).toBe("CLAN_CONVERSATION");
        expect((await api("POST", `${V}/social/groups/${conversation_id}/members`, { token: nova.token, body: { player_id: orion.id } })).body.error.code).toBe(
            "CLAN_CONVERSATION",
        );
        // An outsider does not see it.
        expect((await api("GET", `${V}/social/conversations/${conversation_id}/messages`, { token: orion.token })).status).toBe(404);
        expect((await page(clan.id, orion)).body.data.conversation_id).toBeNull();
        expect((await page(clan.id, nova)).body.data.conversation_id).toBe(conversation_id);
    });

    it("validates every field", async () => {
        const p = await registerPlayer("Nova");
        const bad = async (body: Record<string, unknown>, field: string) => {
            // The creations are limited (5 an hour, invalid ones included): only the validation is tested here.
            await env.DB.prepare("DELETE FROM rate_limits").run();
            const r = await api("POST", `${V}/clans`, { token: p.token, body: { name: "Clan valide", tag: "OK", emblem: EMBLEM, join_policy: "open", ...body } });
            expect(r.status, JSON.stringify(body)).toBe(400);
            expect(r.body.error.details.map((d: { field: string }) => d.field)).toContain(field);
        };
        await bad({ name: "ab" }, "name");
        await bad({ name: "x".repeat(33) }, "name");
        await bad({ name: "-commence mal" }, "name");
        await bad({ name: "Clan <script>" }, "name");
        await bad({ tag: "A" }, "tag");
        await bad({ tag: "TROPLG" }, "tag");
        await bad({ tag: "A-B" }, "tag");
        await bad({ tag: "admin" }, "tag");
        await bad({ emblem: { shape: "cube", primary: "#000000", secondary: "#ffffff" } }, "emblem.shape");
        await bad({ emblem: { shape: "orb", primary: "rouge", secondary: "#ffffff" } }, "emblem.primary");
        await bad({ emblem: "étoile" }, "emblem");
        await bad({ join_policy: "libre" }, "join_policy");
        await bad({ motto: "x".repeat(81) }, "motto");
        await bad({ player_id: p.id }, "player_id");
        // Spaces collapsed, control characters dropped, accents allowed.
        const ok = await api("POST", `${V}/clans`, {
            token: p.token,
            body: { name: "  Étoiles   du\u0007 Nord ", tag: "Nord", motto: "Ensemble‮ jusqu'au bout", emblem: EMBLEM, join_policy: "request" },
        });
        expect(ok.status).toBe(201);
        expect(ok.body.data.clan).toMatchObject({ name: "Étoiles du Nord", tag: "Nord", motto: "Ensemble jusqu'au bout" });
        expect((await page("pas-un-id")).status).toBe(404);
        expect((await page("00000000-0000-4000-8000-000000000000")).status).toBe(404);
    });
});

describe("clans: joining", () => {
    it("on application (with its message), answered by the chief or an officer only", async () => {
        const { id, chief, off, mem } = await team(1, 1);
        expect((await api("PATCH", `${V}/clans/${id}`, { token: chief.token, body: { join_policy: "request" } })).status).toBe(200);
        const zephyr = await registerPlayer("Zephyr");
        const applied = await join(id, zephyr, "  dispo le soir  ");
        expect(applied.status).toBe(201);
        expect(applied.body.data).toMatchObject({ joined: false, application: { message: "dispo le soir" } });
        const mine = await api("GET", `${V}/clans/me`, { token: zephyr.token });
        expect(mine.body.data).toMatchObject({ clan: null, applications: [{ clan: { id, tag: "AUBE" }, message: "dispo le soir" }], invitations: [] });
        // The requests: for the chief and the officers only.
        expect((await page(id, chief)).body.data.requests).toMatchObject([{ kind: "apply", player: { id: zephyr.id }, message: "dispo le soir" }]);
        expect((await page(id, off[0]!)).body.data.requests).toHaveLength(1);
        expect((await page(id, mem[0]!)).body.data.requests).toEqual([]);
        expect((await page(id)).body.data.requests).toEqual([]);
        expect((await page(id, zephyr)).body.data.me).toMatchObject({ role: null, request: { kind: "apply", message: "dispo le soir" } });
        // The chief and the officers were told.
        const told = await api("GET", `${V}/notifications`, { token: chief.token });
        expect(told.body.data.notifications[0]).toMatchObject({ kind: "clan" });
        expect(JSON.stringify(told.body.data.notifications[0])).toContain("demande à rejoindre");
        // A member cannot answer; an officer can.
        expect((await answer(id, mem[0]!, zephyr, true)).body.error.code).toBe("NOT_CLAN_OFFICER");
        const ok = await answer(id, off[0]!, zephyr, true);
        expect(ok.status, JSON.stringify(ok.body)).toBe(200);
        expect((await rolesOf(id))[zephyr.id]).toBe("member");
        expect((await api("GET", `${V}/clans/me`, { token: zephyr.token })).body.data).toMatchObject({ clan: { id, role: "member" }, applications: [] });
        // Now in the discussion.
        const conv = (await page(id, zephyr)).body.data.conversation_id;
        expect((await api("POST", `${V}/social/conversations/${conv}/messages`, { token: zephyr.token, body: { body: "Merci !" } })).status).toBe(201);

        // A refused application disappears; a withdrawn one too.
        const kael = await registerPlayer("Kael");
        expect((await join(id, kael)).status).toBe(201);
        expect((await answer(id, chief, kael, false)).body.data).toEqual({ accepted: false });
        expect((await answer(id, chief, kael, false)).status).toBe(404);
        expect((await join(id, kael, "encore")).status).toBe(201);
        expect((await api("DELETE", `${V}/clans/${id}/join`, { token: kael.token })).status).toBe(200);
        expect((await api("DELETE", `${V}/clans/${id}/join`, { token: kael.token })).status).toBe(404);
        // A message too long is refused.
        expect((await join(id, kael, "x".repeat(121))).status).toBe(400);
    });

    it("on invitation only; an invitation answers an application; at most 5 applications", async () => {
        const { id, chief, off, mem } = await team(1, 1);
        expect((await api("PATCH", `${V}/clans/${id}`, { token: chief.token, body: { join_policy: "invite" } })).status).toBe(200);
        const comete = await registerPlayer("Comete");
        const refused = await join(id, comete);
        expect(refused.status).toBe(403);
        expect(refused.body.error.code).toBe("INVITE_ONLY");
        expect((await invite(id, mem[0]!, comete)).body.error.code).toBe("NOT_CLAN_OFFICER");
        const inv = await invite(id, off[0]!, comete);
        expect(inv.status).toBe(201);
        expect((await invite(id, off[0]!, comete)).body.data.already).toBe(true);
        const mine = await api("GET", `${V}/clans/me`, { token: comete.token });
        expect(mine.body.data.invitations).toMatchObject([{ clan: { id }, by: { id: off[0]!.id, username: off[0]!.username } }]);
        expect((await join(id, comete)).body.data).toMatchObject({ joined: true, clan: { id, role: "member" } });
        // An invitation can be withdrawn by an officer, or declined.
        const lyra = await registerPlayer("Lyra");
        await invite(id, chief, lyra);
        expect((await api("DELETE", `${V}/clans/${id}/invites/${lyra.id}`, { token: mem[0]!.token })).body.error.code).toBe("NOT_CLAN_OFFICER");
        expect((await api("DELETE", `${V}/clans/${id}/invites/${lyra.id}`, { token: off[0]!.token })).status).toBe(200);
        expect((await join(id, lyra)).status).toBe(403);
        // Inviting someone already in a clan, or oneself.
        expect((await invite(id, chief, comete)).body.error.code).toBe("ALREADY_IN_CLAN");
        expect((await invite(id, chief, chief)).status).toBe(400);

        // A clan « sur candidature »: inviting a player who applied makes them a member.
        const other = await registerPlayer("Autre");
        const second = await createClan(other, { tag: "VEGA", name: "Vega", join_policy: "request" });
        expect((await join(second.clan.id, lyra, "je veux venir")).status).toBe(201);
        const accepted = await invite(second.clan.id, other, lyra);
        expect(accepted.body.data).toMatchObject({ joined: true });
        expect((await rolesOf(second.clan.id))[lyra.id]).toBe("member");

        // At most 5 applications at once.
        const solo = await registerPlayer("Solo");
        for (let i = 0; i < 5; ++i) {
            const owner = await registerPlayer(`Fondateur${i}`);
            const c = await createClan(owner, { tag: `C${i}`, name: `Clan numéro ${i}`, join_policy: "request" });
            expect((await join(c.clan.id, solo)).status).toBe(201);
        }
        const owner = await registerPlayer("Fondateur9");
        const sixth = await createClan(owner, { tag: "C9", name: "Clan numéro 9", join_policy: "request" });
        expect((await join(sixth.clan.id, solo)).body.error.code).toBe("TOO_MANY_APPLICATIONS");
        // Joining one clan ends the other applications.
        expect((await api("PATCH", `${V}/clans/${sixth.clan.id}`, { token: owner.token, body: { join_policy: "open" } })).status).toBe(200);
        expect((await join(sixth.clan.id, solo)).body.data.joined).toBe(true);
        expect((await api("GET", `${V}/clans/me`, { token: solo.token })).body.data.applications).toEqual([]);
        expect((await join(second.clan.id, solo)).body.error.code).toBe("ALREADY_IN_CLAN");
    });
});

describe("clans: the rights of each role", () => {
    it("a member reads; an officer invites and excludes members; the chief does everything", async () => {
        const { id, chief, off, mem } = await team(2, 2);
        const [o1, o2] = off as [TestPlayer, TestPlayer];
        const [m1, m2] = mem as [TestPlayer, TestPlayer];
        const outsider = await registerPlayer("Dehors");

        // A member: no change at all.
        expect((await kick(id, m1, m2)).body.error.code).toBe("NOT_CLAN_OFFICER");
        expect((await setRole(id, m1, m2, "officer")).body.error.code).toBe("NOT_CLAN_LEADER");
        expect((await api("PATCH", `${V}/clans/${id}`, { token: m1.token, body: { motto: "pirate" } })).body.error.code).toBe("NOT_CLAN_LEADER");
        expect((await api("DELETE", `${V}/clans/${id}`, { token: m1.token, body: { confirm: true } })).body.error.code).toBe("NOT_CLAN_LEADER");
        expect((await transfer(id, m1, m2)).body.error.code).toBe("NOT_CLAN_LEADER");
        // An outsider neither.
        expect((await kick(id, outsider, m2)).status).toBe(403);
        expect((await invite(id, outsider, await registerPlayer("Ami"))).status).toBe(403);

        // An officer: excludes a member, never an officer nor the chief, never changes roles.
        expect((await kick(id, o1, o2)).body.error.code).toBe("CANNOT_EXCLUDE_OFFICER");
        expect((await kick(id, o1, chief)).body.error.code).toBe("CANNOT_EXCLUDE_LEADER");
        expect((await setRole(id, o1, m1, "officer")).body.error.code).toBe("NOT_CLAN_LEADER");
        expect((await api("PATCH", `${V}/clans/${id}`, { token: o1.token, body: { motto: "officier" } })).body.error.code).toBe("NOT_CLAN_LEADER");
        expect((await transfer(id, o1, o2)).body.error.code).toBe("NOT_CLAN_LEADER");
        const kicked = await kick(id, o1, m2);
        expect(kicked.status).toBe(200);
        expect(kicked.body.data).toMatchObject({ removed: true, left: false, dissolved: false, leader: { id: chief.id } });
        expect((await rolesOf(id))[m2.id]).toBeUndefined();
        const told = await api("GET", `${V}/notifications`, { token: m2.token });
        expect(JSON.stringify(told.body.data.notifications[0])).toContain("exclu du clan [AUBE]");
        // The excluded member left the discussion too.
        const conv = (await page(id, chief)).body.data.conversation_id;
        expect((await api("GET", `${V}/social/conversations/${conv}/messages`, { token: m2.token })).status).toBe(404);
        expect((await kick(id, o1, m2)).status).toBe(404);

        // The chief: names and removes officers, excludes officers, edits and hands over.
        expect((await setRole(id, chief, m1, "officer")).body.data).toEqual({ player_id: m1.id, role: "officer" });
        expect((await setRole(id, chief, m1, "member")).status).toBe(200);
        expect((await setRole(id, chief, chief, "member")).body.error.code).toBe("IS_LEADER");
        expect((await setRole(id, chief, m1, "leader")).status).toBe(400);
        expect((await kick(id, chief, o2)).status).toBe(200);
        const edit = await api("PATCH", `${V}/clans/${id}`, {
            token: chief.token,
            body: { name: "Gardiens du Crépuscule", tag: "CREP", motto: "Toujours plus loin", emblem: { shape: "shield", primary: "#3ee6d6", secondary: "#12304a" } },
        });
        expect(edit.status, JSON.stringify(edit.body)).toBe(200);
        expect(edit.body.data.clan).toMatchObject({ name: "Gardiens du Crépuscule", tag: "CREP", emblem: { shape: "shield" } });
        const convs = await api("GET", `${V}/social/conversations`, { token: chief.token });
        expect(convs.body.data.conversations[0]).toMatchObject({ name: "Gardiens du Crépuscule", clan: { tag: "CREP" } });
        expect((await api("PATCH", `${V}/clans/${id}`, { token: chief.token, body: {} })).status).toBe(400);

        const t = await transfer(id, chief, o1);
        expect(t.status).toBe(200);
        expect(await rolesOf(id)).toMatchObject({ [o1.id]: "leader", [chief.id]: "officer" });
        // The former chief is an officer now: nobody excludes the new chief.
        expect((await kick(id, chief, o1)).body.error.code).toBe("CANNOT_EXCLUDE_LEADER");
        expect((await api("PATCH", `${V}/clans/${id}`, { token: chief.token, body: { motto: "ancien chef" } })).body.error.code).toBe("NOT_CLAN_LEADER");
        // The dissolution needs its confirmation, and the chief.
        expect((await api("DELETE", `${V}/clans/${id}`, { token: o1.token, body: {} })).status).toBe(400);
        expect((await api("DELETE", `${V}/clans/${id}`, { token: o1.token, body: { confirm: false } })).status).toBe(400);
        expect((await api("DELETE", `${V}/clans/${id}`, { token: chief.token, body: { confirm: true } })).body.error.code).toBe("NOT_CLAN_LEADER");
        const gone = await api("DELETE", `${V}/clans/${id}`, { token: o1.token, body: { confirm: true } });
        expect(gone.status).toBe(200);
        expect((await page(id)).status).toBe(404);
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM conversations WHERE id = ?1").bind(conv).first<{ n: number }>()).toEqual({ n: 0 });
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM clan_members").first<{ n: number }>()).toEqual({ n: 0 });
        const dissolved = await api("GET", `${V}/notifications`, { token: m1.token });
        expect(JSON.stringify(dissolved.body.data.notifications[0])).toContain("dissous par son chef");
    });

    it("a chief who leaves hands the clan to the oldest officer; the last member dissolves it", async () => {
        const { id, chief, off, mem } = await team(1, 1);
        const left = await leave(id, chief);
        expect(left.body.data).toMatchObject({ left: true, dissolved: false, leader: { id: off[0]!.id } });
        expect(await rolesOf(id)).toEqual({ [off[0]!.id]: "leader", [mem[0]!.id]: "member" });
        expect((await leave(id, off[0]!)).body.data.leader).toMatchObject({ id: mem[0]!.id });
        const last = await leave(id, mem[0]!);
        expect(last.body.data).toMatchObject({ dissolved: true, leader: null });
        expect((await page(id)).status).toBe(404);
    });

    it("account deletion: a new chief, or the clan gone with its last member", async () => {
        const { id, chief, off } = await team(1, 0);
        expect((await api("DELETE", `${V}/players/me`, { token: chief.token, body: { password: chief.password, confirm: "DELETE" } })).status).toBe(200);
        expect(await rolesOf(id)).toEqual({ [off[0]!.id]: "leader" });
        // The discussion stays the clan's (never handed to someone as a plain group).
        expect(await env.DB.prepare("SELECT role FROM conversation_members WHERE player_id = ?1").bind(off[0]!.id).first()).toEqual({ role: "member" });
        expect((await api("DELETE", `${V}/players/me`, { token: off[0]!.token, body: { password: off[0]!.password, confirm: "DELETE" } })).status).toBe(200);
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM clans").first<{ n: number }>()).toEqual({ n: 0 });
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM conversations").first<{ n: number }>()).toEqual({ n: 0 });
    });
});

describe("clans: two actions at the same time", () => {
    it("the 51st member is refused atomically", async () => {
        const chief = await registerPlayer("Chef");
        const { clan } = await createClan(chief);
        await fill(clan.id, 48);
        expect(await count(clan.id)).toBe(49);
        const [a, b] = await Promise.all([registerPlayer("Premier"), registerPlayer("Second")]);
        const results = await Promise.all([join(clan.id, a!), join(clan.id, b!)]);
        expect(results.map((r) => r.status).sort()).toEqual([200, 409]);
        expect(results.find((r) => r.status === 409)!.body.error.code).toBe("CLAN_FULL");
        expect(await count(clan.id)).toBe(50);
        // The refused one is not in the discussion either.
        const loser = results[0]!.status === 409 ? a! : b!;
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM conversation_members WHERE player_id = ?1").bind(loser.id).first<{ n: number }>()).toEqual({ n: 0 });
        expect((await page(clan.id)).body.data.clan.members).toBe(50);

        // Full: an application cannot be accepted, an invitation not sent, an invited player cannot join.
        const c = await registerPlayer("Candidat");
        await env.DB.prepare("INSERT INTO clan_requests (clan_id, player_id, kind, message, created_at) VALUES (?1, ?2, 'apply', '', ?3)")
            .bind(clan.id, c.id, new Date().toISOString())
            .run();
        expect((await answer(clan.id, chief, c, true)).body.error.code).toBe("CLAN_FULL");
        expect((await invite(clan.id, chief, await registerPlayer("Invite"))).body.error.code).toBe("CLAN_FULL");
        await env.DB.prepare("UPDATE clan_requests SET kind = 'invite' WHERE player_id = ?1").bind(c.id).run();
        expect((await join(clan.id, c)).body.error.code).toBe("CLAN_FULL");
        expect(await count(clan.id)).toBe(50);
    });

    it("two officers accepting the last place for two applicants: one gets it", async () => {
        const { id, chief, off } = await team(1, 0);
        expect((await api("PATCH", `${V}/clans/${id}`, { token: chief.token, body: { join_policy: "request" } })).status).toBe(200);
        await fill(id, 47);
        const [x, y] = await Promise.all([registerPlayer("Postulant1"), registerPlayer("Postulant2")]);
        expect((await join(id, x!)).status).toBe(201);
        expect((await join(id, y!)).status).toBe(201);
        const results = await Promise.all([answer(id, chief, x!, true), answer(id, off[0]!, y!, true)]);
        expect(results.map((r) => r.status).sort()).toEqual([200, 409]);
        expect(await count(id)).toBe(50);
    });

    it("one applicant accepted by two clans at once joins only one", async () => {
        const [c1, c2] = await Promise.all([registerPlayer("ChefA"), registerPlayer("ChefB")]);
        const a = await createClan(c1!, { tag: "AAA", name: "Clan A", join_policy: "request" });
        const b = await createClan(c2!, { tag: "BBB", name: "Clan B", join_policy: "request" });
        const p = await registerPlayer("Convoite");
        expect((await join(a.clan.id, p)).status).toBe(201);
        expect((await join(b.clan.id, p)).status).toBe(201);
        const results = await Promise.all([answer(a.clan.id, c1!, p, true), answer(b.clan.id, c2!, p, true)]);
        // The other: already in a clan, or (read after the first one) no application any more.
        expect(results.filter((r) => r.status === 200)).toHaveLength(1);
        expect([404, 409]).toContain(results.find((r) => r.status !== 200)!.status);
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM clan_members WHERE player_id = ?1").bind(p.id).first<{ n: number }>()).toEqual({ n: 1 });
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM conversation_members WHERE player_id = ?1").bind(p.id).first<{ n: number }>()).toEqual({ n: 1 });
    });

    it("two transfers of the chief at once: one chief", async () => {
        const { id, chief, off, mem } = await team(1, 1);
        const results = await Promise.all([transfer(id, chief, off[0]!), transfer(id, chief, mem[0]!)]);
        // The other: refused in its transaction (409), or seen after the first one (no longer chief).
        expect(results.filter((r) => r.status === 200)).toHaveLength(1);
        expect([403, 409]).toContain(results.find((r) => r.status !== 200)!.status);
        expect(await leaders(id)).toBe(1);
        const roles = await rolesOf(id);
        expect(roles[chief.id]).toBe("officer");
        expect(Object.values(roles).filter((r) => r === "leader")).toHaveLength(1);
    });

    it("the chief hands over to the last officer while that officer leaves: still one chief", async () => {
        for (let round = 0; round < 3; ++round) {
            await seedAll();
            const { id, chief, off, mem } = await team(1, 1);
            const [t, l] = await Promise.all([transfer(id, chief, off[0]!), leave(id, off[0]!)]);
            expect(l.status).toBe(200);
            expect([200, 404, 409]).toContain(t.status);
            expect(await leaders(id)).toBe(1);
            expect(await count(id)).toBe(2);
            const roles = await rolesOf(id);
            expect(roles[off[0]!.id]).toBeUndefined();
            // Either the transfer came first (the former chief, now officer, is chief again) or it failed.
            expect(roles[chief.id]).toBe("leader");
            expect(roles[mem[0]!.id]).toBe("member");
        }
    });

    it("the chief and the last officer leave at the same time: the member becomes chief", async () => {
        const { id, chief, off, mem } = await team(1, 1);
        const results = await Promise.all([leave(id, chief), leave(id, off[0]!)]);
        expect(results.map((r) => r.status)).toEqual([200, 200]);
        expect(await rolesOf(id)).toEqual({ [mem[0]!.id]: "leader" });
    });

    it("an officer excludes a member while the chief names them officer: never an officer excluded by an officer", async () => {
        const { id, chief, off, mem } = await team(1, 1);
        const [k, p] = await Promise.all([kick(id, off[0]!, mem[0]!), setRole(id, chief, mem[0]!, "officer")]);
        const role = (await rolesOf(id))[mem[0]!.id];
        if (k.status === 200) {
            expect(role).toBeUndefined();
            expect(p.status).toBe(404);
        } else {
            expect(k.body.error.code).toBe("CANNOT_EXCLUDE_OFFICER");
            expect(role).toBe("officer");
        }
    });
});

// ---------------------------------------------------------------------- points

const ROUTE: [number, number][] = Array.from({ length: 60 }, (_, i) => [SPAWN.x + ((EXIT.x + 10 - SPAWN.x) * i) / 59, SPAWN.y + ((EXIT.y + 40 - SPAWN.y) * i) / 59]);
const LAYOUT = JSON.stringify({ route: ROUTE, bosses: [], ambushes: [] });

async function makeExtreme(hash: string): Promise<void> {
    await env.DB.prepare("UPDATE level_versions SET race = ?2 WHERE content_hash = ?1").bind(hash, LAYOUT).run();
}

let runCounter = 0;
async function verifiedRun(p: TestPlayer, board: string, score: number, at: string, status = "VERIFIED", finished = false): Promise<void> {
    runCounter += 1;
    await env.DB.prepare(
        `INSERT INTO race_runs (id, board, player_id, attempt_id, claim, time_ms, score, route, bosses_beaten, waves, finished, status, created_at, verified_at)
         VALUES (?1, ?2, ?3, ?4, '{}', 40000, ?5, 0.5, 0, 0, ?6, ?7, ?8, ?8)`,
    )
        .bind(crypto.randomUUID(), board, p.id, `attempt-${runCounter}`, score, finished ? 1 : 0, status, at)
        .run();
}

/** A real attempt of an official race, sent and verified by the server (the clan's points follow). */
async function officialAttempt(p: TestPlayer, upTo: number) {
    const attemptId = crypto.randomUUID();
    const durationMs = 40000;
    const sub = await api("POST", `${V}/races/runs`, {
        token: p.token,
        body: { kind: "official", level_id: "test_01", level_hash: LEVEL_HASH, attempt_id: attemptId, time_ms: durationMs, finished: false, bosses: [], waves: [] },
    });
    expect(sub.status, JSON.stringify(sub.body)).toBe(201);
    const replay = await buildReplay({
        levelId: "test_01",
        levelHash: LEVEL_HASH,
        attemptId,
        durationMs,
        collectibles: 0,
        finish: false,
        extraEvents: [{ frame: 100000, type: 3, arg: 0, value: 0 }],
        path: (i, n) => {
            const t = upTo * (i / (n - 1));
            return { x: SPAWN.x + (EXIT.x + 10 - SPAWN.x) * t, y: SPAWN.y + (EXIT.y + 40 - SPAWN.y) * t };
        },
    });
    const r = await api("PUT", sub.body.data.upload, { token: p.token, raw: replay, headers: { "content-type": "application/octet-stream" } });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    expect(r.body.data.run.status).toBe("VERIFIED");
    return r.body.data.run as { score: number };
}

describe("clans: points and ranking", () => {
    it("the clan's best verified result on each EXTRÊME level, summed; the members who leave take theirs", async () => {
        await makeExtreme(LEVEL_HASH);
        await makeExtreme(LEVEL2_HASH);
        const B1 = `o:test_01:${LEVEL_HASH}`;
        const B2 = `o:test_02:${LEVEL2_HASH}`;
        const [a1, a2, b1] = await Promise.all(["Lyra", "Orion", "Vega"].map((n) => registerPlayer(n)));
        const A = await createClan(a1!, { tag: "AUBE" });
        expect((await join(A.clan.id, a2!)).status).toBe(200);
        const B = await createClan(b1!, { tag: "VEGA", name: "Vega" });
        await verifiedRun(a1!, B1, 50, "2026-11-10T10:00:00.000Z");
        await verifiedRun(a2!, B1, 70, "2026-11-11T10:00:00.000Z");
        await verifiedRun(a2!, B2, 100, "2026-11-12T10:00:00.000Z", "VERIFIED", true);
        await verifiedRun(a1!, B2, 99.9, "2026-11-12T11:00:00.000Z", "REJECTED");
        await verifiedRun(a1!, B2, 99.9, "2026-11-12T11:00:00.000Z", "PENDING");
        await verifiedRun(b1!, B1, 80, "2026-11-09T10:00:00.000Z");
        // A community EXTRÊME level never counts (anyone could make an easy one).
        await verifiedRun(b1!, "c:00000000-0000-4000-8000-000000000000:1", 100, "2026-11-09T10:00:00.000Z", "VERIFIED", true);
        await refreshAllClanPoints(env.DB);

        let rank = await api("GET", `${V}/clans/ranking`, { token: a1!.token });
        expect(rank.body.data.ranking.map((r: any) => [r.rank, r.clan.tag, r.clan.points])).toEqual([
            [1, "AUBE", 170],
            [2, "VEGA", 80],
        ]);
        expect(rank.body.data.total).toBe(2);
        expect(rank.body.data.me).toMatchObject({ rank: 1, clan: { tag: "AUBE", points_levels: 2 } });

        // A real attempt verified by the server: the clan's best on test_01 follows at once.
        const run = await officialAttempt(a1!, 0.9);
        expect(run.score).toBeGreaterThan(85);
        const view = await page(A.clan.id, a1!);
        const levels = view.body.data.levels as any[];
        expect(levels.map((l) => l.level_id)).toEqual(["test_01", "test_02"]);
        expect(levels[0].best).toMatchObject({ mode: "solo", team_size: 1, players: [{ id: a1!.id, username: "Lyra", clan_tag: "AUBE" }], rank: 1, finished: false });
        expect(levels[0].best.score).toBe(run.score);
        expect(levels[1].best).toMatchObject({ mode: "solo", score: 100, finished: true, players: [{ id: a2!.id }], rank: 1 });
        expect(view.body.data.clan.points).toBeCloseTo(run.score + 100, 5);
        expect(view.body.data.clan.rank).toBe(1);
        // Each member's best EXTRÊME result.
        const members = view.body.data.members as any[];
        expect(members.find((m) => m.player.id === a2!.id).best).toMatchObject({ level_id: "test_02", title: "Niveau test 2", score: 100, finished: true });

        // Orion leaves: his results leave with him.
        expect((await leave(A.clan.id, a2!)).status).toBe(200);
        const after = await page(A.clan.id);
        expect(after.body.data.clan.points).toBe(run.score);
        expect(after.body.data.levels[1].best).toBeNull();
        rank = await api("GET", `${V}/clans/ranking`);
        expect(rank.body.data.ranking.map((r: any) => r.clan.tag)).toEqual(["AUBE", "VEGA"]);
        expect(rank.body.data.me).toBeNull();
        // Joining Vega, he brings them along.
        expect((await join(B.clan.id, a2!)).status).toBe(200);
        rank = await api("GET", `${V}/clans/ranking?limit=1`);
        expect(rank.body.data.ranking).toMatchObject([{ rank: 1, clan: { tag: "VEGA", points: 180 } }]);
        expect(rank.body.data.has_more).toBe(true);
        expect((await api("GET", `${V}/clans/ranking?limit=1&offset=1`)).body.data.ranking).toMatchObject([{ rank: 2, clan: { tag: "AUBE" } }]);
    });

    it("an earlier revision kept with « Garder les records » counts; a reset revision does not", async () => {
        await makeExtreme(LEVEL_HASH);
        const p = await registerPlayer("Lyra");
        const c = await createClan(p);
        await verifiedRun(p, `o:test_01:${LEVEL_HASH}`, 60, "2026-11-10T10:00:00.000Z");
        // A new revision of test_01 (same layout) becomes current.
        const NEW = "e".repeat(64);
        await env.DB.prepare(
            `INSERT INTO level_versions (level_id, version, content_hash, compatibility_status, min_game_version, total_collectibles, total_enemies,
               time_limit_ms, min_time_ms, bounds_w, bounds_h, spawn_x, spawn_y, exit_x, exit_y, exit_w, exit_h, created_at, race)
             SELECT level_id, 2, ?2, 'active', min_game_version, total_collectibles, total_enemies, time_limit_ms, min_time_ms, bounds_w, bounds_h,
               spawn_x, spawn_y, exit_x, exit_y, exit_w, exit_h, created_at, race FROM level_versions WHERE content_hash = ?1`,
        )
            .bind(LEVEL_HASH, NEW)
            .run();
        await env.DB.prepare("UPDATE levels SET current_version = 2, content_hash = ?1 WHERE id = 'test_01'").bind(NEW).run();
        await refreshAllClanPoints(env.DB);
        expect((await page(c.clan.id)).body.data.clan.points).toBe(0);
        await env.DB.prepare("INSERT INTO campaign_board_links (level_id, to_hash, from_hash, created_at) VALUES ('test_01', ?1, ?2, ?3)")
            .bind(NEW, LEVEL_HASH, new Date().toISOString())
            .run();
        await refreshAllClanPoints(env.DB);
        expect((await page(c.clan.id)).body.data.clan.points).toBe(60);
        // A clan without points is not ranked.
        const other = await registerPlayer("Vide");
        await createClan(other, { tag: "VIDE", name: "Clan vide" });
        expect((await api("GET", `${V}/clans/ranking`)).body.data.ranking).toHaveLength(1);
    });
});

describe("clans: the tag next to the username, presence, search", () => {
    it("on the boards, in the profile and in SOCIAL; presence only to members, only as shared", async () => {
        await makeExtreme(LEVEL_HASH);
        const [nova, orion, lyra] = await Promise.all(["Nova", "Orion", "Lyra"].map((n) => registerPlayer(n)));
        const c = await createClan(nova!, { tag: "Aube" });
        expect((await join(c.clan.id, orion!)).status).toBe(200);
        await verifiedRun(nova!, `o:test_01:${LEVEL_HASH}`, 40, "2026-11-10T10:00:00.000Z");
        // A private profile still shows its clan tag (the clan's page is public), not its cosmetics.
        await env.DB.prepare("UPDATE players SET profile_visibility = 'private' WHERE id = ?1").bind(nova!.id).run();
        const board = await api("GET", `${V}/races/board?kind=official&level_id=test_01`);
        expect(board.body.data.ranking[0].player).toEqual({ id: nova!.id, username: "Nova", avatar: "avatar_01", clan_tag: "Aube" });
        // The profile.
        const profile = await api("GET", `${V}/players/${orion!.id}`, { token: lyra!.token });
        expect(profile.body.data.clan).toMatchObject({ id: c.clan.id, tag: "Aube", name: "Les Gardiens de l'Aube", role: "member", emblem: { shape: "star" } });
        expect((await api("GET", `${V}/players/${lyra!.id}`)).body.data.clan).toBeNull();
        expect((await api("GET", `${V}/players/${nova!.id}`)).body.data.clan).toMatchObject({ tag: "Aube", role: "leader" });
        // SOCIAL: the friends list, the discussion's members and its messages.
        expect((await api("POST", `${V}/friends/request`, { token: lyra!.token, body: { player_id: orion!.id } })).status).toBe(201);
        expect((await api("POST", `${V}/friends/accept`, { token: orion!.token, body: { player_id: lyra!.id } })).status).toBe(200);
        const friends = await api("GET", `${V}/friends`, { token: lyra!.token });
        expect(friends.body.data.friends[0].player).toMatchObject({ username: "Orion", clan_tag: "Aube" });
        const friendsBack = await api("GET", `${V}/friends`, { token: orion!.token });
        expect(friendsBack.body.data.friends[0].player.clan_tag).toBeUndefined();
        await api("POST", `${V}/social/conversations/${c.conversation_id}/messages`, { token: orion!.token, body: { body: "Salut le clan" } });
        const msgs = await api("GET", `${V}/social/conversations/${c.conversation_id}/messages`, { token: nova!.token });
        expect(msgs.body.data.messages[0].sender).toMatchObject({ username: "Orion", clan_tag: "Aube" });
        const convs = await api("GET", `${V}/social/conversations`, { token: nova!.token });
        expect(convs.body.data.conversations[0].members.every((m: any) => m.clan_tag === "Aube")).toBe(true);

        // Presence: the members see it (as each player shares it), the others never.
        expect((await api("POST", `${V}/presence`, { token: orion!.token, body: { state: "active" } })).status).toBe(200);
        const inside = await page(c.clan.id, nova!);
        expect(inside.body.data.members.find((m: any) => m.player.id === orion!.id).presence).toBe("online");
        const outside = await page(c.clan.id, lyra!);
        expect(outside.body.data.members.every((m: any) => m.presence === undefined)).toBe(true);
        await env.DB.prepare("UPDATE players SET presence_visible = 0 WHERE id = ?1").bind(orion!.id).run();
        expect((await page(c.clan.id, nova!)).body.data.members.find((m: any) => m.player.id === orion!.id).presence).toBe("hidden");
        // Only a country, as the player shares it (never more precise).
        expect(inside.body.data.members[0].player).not.toHaveProperty("region_code");
    });

    it("searches a tag (whatever the case) or a part of a name (accents ignored), page by page", async () => {
        const names = ["Les Gardiens de l'Aube", "Étoiles du Nord", "Nébuleuse Rouge"];
        const tags = ["AUBE", "NORD", "NEB"];
        for (let i = 0; i < 3; ++i) await createClan(await registerPlayer(`Fondateur${i}`), { name: names[i], tag: tags[i] });
        const q = async (s: string) => (await api("GET", `${V}/clans?q=${encodeURIComponent(s)}`)).body.data.clans.map((c: any) => c.tag);
        expect(await q("nord")).toEqual(["NORD"]);
        expect(await q("etoiles")).toEqual(["NORD"]);
        expect(await q("NEBULEUSE")).toEqual(["NEB"]);
        expect(await q("aube")).toEqual(["AUBE"]);
        expect(await q("zzz")).toEqual([]);
        const first = await api("GET", `${V}/clans?limit=2`);
        expect(first.body.data.clans).toHaveLength(2);
        expect(first.body.data.has_more).toBe(true);
        const next = await api("GET", `${V}/clans?limit=2&offset=2`);
        expect(next.body.data.clans).toHaveLength(1);
        expect(next.body.data.has_more).toBe(false);
        expect((await api("GET", `${V}/clans?q=${"x".repeat(33)}`)).status).toBe(400);
        expect((await api("GET", `${V}/clans?limit=500`)).status).toBe(400);
    });
});

describe("clans: moderation (ADMINISTRATION › Clans)", () => {
    it("an administrator renames or dissolves a clan; each action goes to the Journal", async () => {
        const admin = await registerPlayer("Admin1");
        await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(admin.id).run();
        const [nova, orion] = await Promise.all([registerPlayer("Nova"), registerPlayer("Orion")]);
        const c = await createClan(nova!, { tag: "AUBE" });
        expect((await join(c.clan.id, orion!)).status).toBe(200);
        await createClan(await registerPlayer("Autre"), { tag: "VEGA", name: "Vega" });

        // For a player, these routes do not exist.
        expect((await api("GET", `${V}/admin/clans`, { token: nova!.token })).status).toBe(404);
        expect((await api("PATCH", `${V}/admin/clans/${c.clan.id}`, { token: nova!.token, body: { name: "Piraté" } })).status).toBe(404);
        expect((await api("DELETE", `${V}/admin/clans/${c.clan.id}`, { token: nova!.token, body: {} })).status).toBe(404);

        const list = await api("GET", `${V}/admin/clans?q=aube`, { token: admin.token });
        expect(list.body.data.clans).toMatchObject([{ id: c.clan.id, tag: "AUBE", members: 2, leader: { id: nova!.id, username: "Nova" } }]);
        expect(list.body.data.total).toBe(2);

        const taken = await api("PATCH", `${V}/admin/clans/${c.clan.id}`, { token: admin.token, body: { tag: "vega" } });
        expect(taken.body.error.code).toBe("TAG_TAKEN");
        expect((await api("PATCH", `${V}/admin/clans/${c.clan.id}`, { token: admin.token, body: { motto: "non" } })).status).toBe(400);
        const renamed = await api("PATCH", `${V}/admin/clans/${c.clan.id}`, { token: admin.token, body: { name: "Clan renommé", tag: "RNM", reason: "nom injurieux" } });
        expect(renamed.status, JSON.stringify(renamed.body)).toBe(200);
        expect(renamed.body.data.clan).toMatchObject({ name: "Clan renommé", tag: "RNM" });
        const convs = await api("GET", `${V}/social/conversations`, { token: orion!.token });
        expect(convs.body.data.conversations[0]).toMatchObject({ name: "Clan renommé", clan: { tag: "RNM" } });

        const dissolved = await api("DELETE", `${V}/admin/clans/${c.clan.id}`, { token: admin.token, body: { reason: "triche" } });
        expect(dissolved.status).toBe(200);
        expect((await page(c.clan.id)).status).toBe(404);
        expect((await api("DELETE", `${V}/admin/clans/${c.clan.id}`, { token: admin.token, body: {} })).status).toBe(404);
        const told = await api("GET", `${V}/notifications`, { token: orion!.token });
        expect(JSON.stringify(told.body.data.notifications[0])).toContain("dissous par la modération");

        const journal = await api("GET", `${V}/admin/log?kind=clan`, { token: admin.token });
        expect(journal.status).toBe(200);
        expect(journal.body.data.entries.map((e: any) => e.action)).toEqual(["admin_clan_dissolved", "admin_clan_renamed"]);
        expect(journal.body.data.entries[1]).toMatchObject({
            actor: { id: admin.id, username: "Admin1" },
            reason: "nom injurieux",
            clan: { id: c.clan.id, name: "Clan renommé", tag: "RNM", previous: { name: "Les Gardiens de l'Aube", tag: "AUBE" } },
        });
        expect(journal.body.data.entries[0]).toMatchObject({ reason: "triche", clan: { tag: "RNM", members: 2 } });
        const refused = await api("GET", `${V}/admin/log?kind=refused`, { token: admin.token });
        expect(refused.body.data.entries[0]).toMatchObject({ action: "admin_refused", reason: "TAG_TAKEN", clan: { previous: { tag: "AUBE" } } });
    });
});
