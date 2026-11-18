// Clan raids « Asterion », phase 5 (2.69, raidWorld.ts, docs/RAIDS.md §15): the rewards of the world
// events, beside those of phase 3 (raidRewards.ts lists them all). Exclusive and limited in time:
// each one is granted by the server only, from the validated results of the official world event
// (never a clan raid), once per player, reward and event (raid_reward_grants); the catch-up
// sessions (« sessions de rattrapage ») give the silver version only, and never a ranking reward.
// No import here: raidRewards.ts reads this table when it loads.

export const WORLD_REWARDS = {
    asterion_ecorce: {
        id: "asterion_ecorce",
        name: "Combinaison « Écorce d'Asterion »",
        kind: "item",
        ref: "suit_ecorce_asterion",
        why: "Exclusive à l'événement mondial : 5 minutes de combat validées pendant l'événement",
        rank: "participation",
    },
    asterion_ecorce_argent: {
        id: "asterion_ecorce_argent",
        name: "Combinaison « Écorce d'Asterion » argentée",
        kind: "item",
        ref: "suit_ecorce_argent",
        why: "Session de rattrapage : 5 minutes de combat validées (version argentée, sans effet sur le résultat officiel)",
        rank: "participation",
    },
    asterion_clan_top10: {
        id: "asterion_clan_top10",
        name: "Titre de clan « Dévoreurs dévorés »",
        kind: "item",
        ref: "title_devoreurs_devores",
        why: "Votre clan dans le top 10 du classement mondial officiel de l'événement",
        rank: "performance",
    },
} as const;

/** The title a clan of the top 10 wins (raid_clan_titles) and the rank it needs. */
export const CLAN_TOP_TITLE = "Dévoreurs dévorés";
export const CLAN_TOP_RANK = 10;
