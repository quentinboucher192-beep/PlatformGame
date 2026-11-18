// Clan raids « Asterion », phase 6 (2.72, raidHeart.ts, docs/RAIDS.md §15): the rewards of the Cœur
// d'Asterion's fall, beside those of phase 3 (raidRewards.ts lists them all). Each one is granted by the
// server only, from validated results (the shared fall written once, the contributions each instance's
// consensus accepted), once per player, reward and event (raid_reward_grants). No import here:
// raidRewards.ts reads this table when it loads.

export const HEART_REWARDS = {
    coeur_badge: {
        id: "coeur_badge",
        name: "Badge « Cœur d'Asterion »",
        kind: "badge",
        ref: "communaute_coeur",
        why: "Présent à la chute validée du Cœur d'Asterion (votre instance était dans son arène)",
        rank: "gigaboss",
    },
    coeur_supernova: {
        id: "coeur_supernova",
        name: "Effet de victoire « Supernova d'Asterion »",
        kind: "item",
        ref: "victory_supernova_asterion",
        why: "A combattu le Cœur d'Asterion (1 minute validée dans son arène) jusqu'à sa chute validée",
        rank: "participation",
    },
    coeur_decor: {
        id: "coeur_decor",
        name: "Cadre de profil « Cœur d'Asterion »",
        kind: "item",
        ref: "frame_coeur_asterion",
        why: "Parmi les 10 meilleurs scores de contribution de l'événement (score mixte : dégâts, évités, soutien, mécanismes, objectifs, présence), le Cœur tombé",
        rank: "performance",
    },
    coeur_clan: {
        id: "coeur_clan",
        name: "Titre de clan « Briseurs d'étoile »",
        kind: "item",
        ref: "title_briseurs_etoile",
        why: "Récompense collective : votre clan était présent à la chute validée du Cœur d'Asterion",
        rank: "participation",
    },
} as const;

/** The collective title of the clans present at the heart's fall (raid_clan_titles), and its top. */
export const HEART_CLAN_TITLE = "Briseurs d'étoile";
export const HEART_TOP_PLAYERS = 10;
/** 1 minute of validated fight in the heart's arena gives its « Supernova ». */
export const HEART_PARTICIPATION_TICKS = 60 * 120;
