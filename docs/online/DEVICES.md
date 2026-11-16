# Un compte, un ordinateur (2.47)

Un compte Cosmic Online est **privé à un seul ordinateur**. Code : `server/src/devices.ts`,
`server/migrations/0091_device_binding.sql`, `game/online/DeviceIdentity.*`,
`ui/screens/OnlineComputer.*` (EN LIGNE › PARAMÈTRES › Cet ordinateur, « De retour en ligne »).

## Règles

- Le jeu tire au hasard un **identifiant d'ordinateur** la première fois (32 caractères
  hexadécimaux, aucune empreinte du matériel), gardé dans `<dossier utilisateur>/online/device.json`
  (une réinstallation le garde), avec un nom modifiable (« Mon ordinateur » par défaut). Il est
  envoyé à l'inscription et à chaque connexion. Un jeu plus ancien qui n'en envoie pas reçoit
  `426` : « Mettez le jeu à jour ».
- Le compte est **lié** à l'ordinateur où il est créé ; un compte d'avant la 2.47 est lié au
  premier ordinateur qui s'y connecte avec la 2.47 (les sessions ouvertes ont été fermées par la
  migration pour cela).
- **Depuis un autre ordinateur**, le bon mot de passe est refusé (`403 ACCOUNT_BOUND_ELSEWHERE`) :
  la tentative est notée (pays approximatif seulement) et le propriétaire reçoit une notification
  **Sécurité** (regroupée tant qu'elle n'est pas lue). Après 5 refus en 10 minutes, cet
  ordinateur attend (`429`). Un mauvais mot de passe reste un simple `INVALID_CREDENTIALS`.
- **Un seul compte par ordinateur** : un ordinateur déjà lié refuse l'inscription d'un autre
  compte et la connexion d'un autre compte (`409 DEVICE_BOUND`). Le jeu ne propose sur l'écran de
  connexion que le compte lié, et masque « Créer un compte ».
- Supprimer le compte libère l'ordinateur.
- Les notifications « Sécurité » ne se désactivent pas.

L'identifiant n'est jamais un secret (un fichier copié passerait) : la session, le mot de passe
et la liaison restent ceux du serveur.

## Local et en ligne

Une seule progression sur l'ordinateur, connecté ou non. Se déconnecter (« Se déconnecter (jouer
en local) ») note l'état de la progression ; à la connexion suivante, ce qui a été fait en local
s'affiche dans **« De retour en ligne »** (niveaux finis, temps à envoyer, succès, temps de jeu)
et part au compte : succès déclarés, temps en attente (vérifiés comme d'habitude), tenue, et une
copie de la progression.

## Copies de la progression

Le jeu envoie une copie de ses sauvegardes (`progress`, `records`, `boss_stats`, `achievements`,
`collection`) avec un résumé : après les parties (au plus toutes les 10 minutes), à chaque
connexion, et sur demande. Le serveur en garde 5 par compte, dans le bucket privé
(`progress/<joueur>/…`), supprimées avec le compte. Elles servent à migrer vers un nouveau compte
(2.48) et aux admins.

## Administration

ADMINISTRATION › Joueurs › « Ordinateur » : l'ordinateur lié, les tentatives refusées. **Délier**
(la prochaine connexion, depuis n'importe quel ordinateur, le lie de nouveau) ou **Lier à cet
ordinateur** (celui d'une tentative refusée ; les sessions de l'ancien se ferment). Journal
(filtre « account ») et notification « Sécurité » au joueur.

## Routes

| Méthode | Route | Corps / réponse |
|---|---|---|
| POST | `/auth/register`, `/auth/login` | + `device: { id, name }` ; `403 ACCOUNT_BOUND_ELSEWHERE`, `409 DEVICE_BOUND`, `426` sans `device` |
| GET | `/players/me/device` | `{ device: { name, bound_at } \| null, events: [{ kind: "refused"\|"bound", country?, device_name?, at }], copy }` |
| PATCH | `/players/me/device` | `{ name }` |
| PUT | `/players/me/progress-copy` | `{ format: "cosmic.progress_copy", game_version, summary: { levels, achievements, stars, play_seconds, bosses }, files }` (≤ 4 Mio) → `201 { copy }` |
| GET | `/players/me/progress-copy` | `{ copy: { id, size, game_version, created_at, summary } \| null }` |
| GET | `/admin/players/{id}/device` | `{ player, device, rebind_allowed, attempts: [{ id, country, created_at }], copies }` |
| POST | `/admin/players/{id}/device` | `{ action: "unbind" }` ou `{ action: "bind", attempt_id }` |
