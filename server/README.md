# Cosmic Online — serveur (Cloudflare Workers + D1 + R2)

API REST de Cosmic Online, indépendante du jeu C++ : TypeScript, aucune dépendance
d'exécution (routeur, validation et cryptographie maison sur les API Web standard).

```
npm ci
./scripts/dev-setup.sh --levels   # serveur local http://127.0.0.1:8787 + niveaux du jeu
npm test                          # 55 tests (D1 / R2 locaux via Miniflare)
```

| Dossier / fichier | Rôle |
|---|---|
| `src/index.ts` | routes, gestion d'erreurs, journaux, cache, tâche planifiée |
| `src/auth.ts` | inscription, connexion, sessions (rotation, révocation) |
| `src/records.ts`, `src/replay.ts` | soumission, validation, analyse des replays, décision |
| `src/leaderboards.ts`, `src/globe.ts` | classements et données du globe |
| `src/players.ts`, `src/friends.ts`, `src/admin.ts` | profils, RGPD, amis, modération, niveaux |
| `migrations/` | schéma D1 versionné (ne jamais modifier une migration appliquée) |
| `scripts/` | lancement local, enregistrement des niveaux, génération de la table des pays |
| `openapi.yaml` | spécification OpenAPI 3.1 |
| `wrangler.jsonc` | environnements `dev`, `staging`, production (aucun secret) |

Documentation : [../docs/online/](../docs/online/README.md) — installation, déploiement,
secrets, API, règles, dépannage.
