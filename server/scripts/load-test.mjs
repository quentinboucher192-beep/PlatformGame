// Load test of live editing against a LOCAL development server (wrangler dev): N members
// in one project room, each sending its presence like the game (every 0.6 s) and edits at a
// steady pace, for a given time. Prints the response times (median, p95, max), the refusals
// and checks at the end that the room's document is the replay of the operations in order.
//
//   npx wrangler dev --env dev --port 8787            (in another terminal)
//   node scripts/load-test.mjs [members=12] [seconds=20] [ops per member per second=2]
//
// It creates throwaway accounts (charge_xxxx_N) on that server. It refuses any server other
// than localhost: never run it against staging or production.

const BASE = process.env.COSMIC_LOAD_URL ?? "http://127.0.0.1:8787";
const [members = 12, seconds = 20, rate = 2] = process.argv.slice(2).map(Number);

const host = new URL(BASE).hostname;
if (!["127.0.0.1", "localhost", "::1"].includes(host)) {
    console.error(`Refusé : ${BASE} n'est pas un serveur local.`);
    process.exit(2);
}

const V = `${BASE}/api/v1`;
const run = Math.random().toString(36).slice(2, 6);
const times = { ops: [], heartbeat: [] };
const statuses = {};
let httpErrors = 0;

async function call(method, path, body, token) {
    const t0 = performance.now();
    const res = await fetch(V + path, {
        method,
        headers: {
            "content-type": "application/json",
            ...(token ? { authorization: `Bearer ${token}` } : {}),
        },
        body: body === undefined ? undefined : JSON.stringify(body),
    });
    const ms = performance.now() - t0;
    const text = await res.text();
    let json = null;
    try {
        json = JSON.parse(text);
    } catch {}
    return { status: res.status, json, ms };
}

async function account(i) {
    const username = `charge_${run}_${i}`;
    const r = await call("POST", "/auth/register", {
        username,
        password: `essai de charge ${run} ${i * 7919}`,
        accept_tos: true,
        tos_version: "2026-11",
    });
    if (r.status !== 201) throw new Error(`inscription ${username} : ${r.status} ${JSON.stringify(r.json)}`);
    return {
        id: r.json.data.player.id,
        name: username,
        token: r.json.data.session.access_token,
    };
}

const pct = (l, p) => (l.length ? l.slice().sort((a, b) => a - b)[Math.min(l.length - 1, Math.floor((l.length * p) / 100))] : 0);
const fmt = (l) => `${l.length} requêtes, médiane ${pct(l, 50).toFixed(0)} ms, p95 ${pct(l, 95).toFixed(0)} ms, max ${Math.max(0, ...l).toFixed(0)} ms`;

function apply(doc, op) {
    if (op.t === "add") doc.objects.push(structuredClone(op.o));
    else if (op.t === "del") doc.objects = doc.objects.filter((o) => o.id !== op.id);
    else if (op.t === "set") {
        const o = doc.objects.find((x) => x.id === op.id);
        for (const [k, v] of Object.entries(op.f)) v === null ? delete o[k] : (o[k] = v);
    } else if (op.t === "doc") doc[op.k] = op.v;
}

const level = {
    id: "charge",
    name: "Charge",
    planet: "sylvanox",
    layers: [{ id: "gameplay" }],
    objects: [{ id: "o1", type: "solid", rect: [0, 0, 64, 16] }],
};

console.log(`Serveur ${BASE} : ${members} membres, ${seconds} s, ${rate} modification(s) par membre et par seconde.`);
const team = [];
for (let i = 0; i < members; ++i) team.push(await account(i));
const owner = team[0];
const created = await call("POST", "/projects", { name: `Charge ${run}`, content: level }, owner.token);
const project = created.json.data.project.id;
const key = (await call("POST", `/projects/${project}/invites`, { role: "editor", max_uses: members }, owner.token)).json.data.key;
for (const m of team.slice(1)) await call("POST", "/projects/join", { key }, m.token);
for (const m of team) await call("POST", `/projects/${project}/live/join`, {}, m.token);

const end = Date.now() + seconds * 1000;
let opCounter = 0;
async function member(m, index) {
    let seq = 0,
        nextBeat = 0,
        nextOp = 0,
        mine = 0;
    while (Date.now() < end) {
        const now = Date.now();
        if (now >= nextBeat) {
            nextBeat = now + 600;
            const r = await call("POST", `/projects/${project}/live/heartbeat`, { since_seq: seq, cursor: { x: index * 10, y: mine }, selection: [] }, m.token);
            times.heartbeat.push(r.ms);
            if (r.status !== 200) httpErrors++;
            else if (!r.json.data.throttled) seq = r.json.data.seq;
        }
        if (now >= nextOp) {
            nextOp = now + 1000 / rate;
            const op =
                mine % 3 === 2
                    ? { t: "set", id: "o1", f: { rect: [index, mine, 64, 16] } }
                    : {
                          t: "add",
                          o: {
                              id: `${m.name}_${mine}`,
                              type: "coin",
                              rect: [mine * 16, index * 16, 16, 16],
                          },
                      };
            mine++;
            const r = await call("POST", `/projects/${project}/live/ops`, { ops: [{ id: `charge-${run}-${opCounter++}-op`, base_seq: seq, op }] }, m.token);
            times.ops.push(r.ms);
            if (r.status !== 200) {
                httpErrors++;
                statuses[`HTTP ${r.status}`] = (statuses[`HTTP ${r.status}`] ?? 0) + 1;
            } else for (const x of r.json.data.results) statuses[x.status] = (statuses[x.status] ?? 0) + 1;
        }
        await new Promise((r) => setTimeout(r, 20));
    }
}
await Promise.all(team.map(member));

// Consistency: replaying the operations in the room's order gives the room's document.
const all = [];
let since = 0;
for (;;) {
    const hb = await call("POST", `/projects/${project}/live/heartbeat`, { since_seq: since }, owner.token);
    if (hb.json.data.throttled) {
        await new Promise((r) => setTimeout(r, 250));
        continue;
    }
    if (hb.json.data.resync) break;
    all.push(...hb.json.data.ops);
    if (!hb.json.data.ops.length || hb.json.data.seq === since) break;
    since = hb.json.data.ops.at(-1).seq;
    if (since >= hb.json.data.seq) break;
    await new Promise((r) => setTimeout(r, 250));
}
const joined = await call("POST", `/projects/${project}/live/join`, {}, owner.token);
const roomSeq = joined.json.data.seq;
let consistent = "non vérifiable ici (plus de 1000 opérations : un client rejoint la salle au lieu de tout relire)";
if (all.length === roomSeq) {
    const doc = structuredClone(level);
    for (const o of all) apply(doc, o.op);
    consistent = JSON.stringify(doc.objects) === JSON.stringify(joined.json.data.doc.objects) ? "oui" : "NON";
}
for (const m of team) await call("POST", `/projects/${project}/live/leave`, {}, m.token);
// When the last member leaves, the room writes its document as a revision: it must be the same.
const saved = await call("GET", `/projects/${project}/content`, undefined, owner.token);
const persisted = JSON.stringify(saved.json?.objects) === JSON.stringify(joined.json.data.doc.objects) ? "oui" : "NON";
const revisions = (await call("GET", `/projects/${project}/revisions?limit=100`, undefined, owner.token)).json.data.revisions.length;

console.log(`Modifications : ${fmt(times.ops)}`);
console.log(`Présence      : ${fmt(times.heartbeat)}`);
console.log(`Réponses      : ${JSON.stringify(statuses)} ; erreurs HTTP : ${httpErrors}`);
console.log(`Ordre         : ${roomSeq} opérations numérotées, ${all.length} relues ; document identique au rejeu : ${consistent}`);
console.log(`Enregistrement: ${revisions} révisions ; dernière révision identique au document de la salle : ${persisted}`);
