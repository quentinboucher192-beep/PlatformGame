#!/usr/bin/env bash
# Local Cosmic Online server for development: no Cloudflare account needed.
#   ./scripts/dev-setup.sh            install, create .dev.vars, apply migrations, start the server
#   ./scripts/dev-setup.sh --levels   ...then register the game's levels (needs the built game)
# The game connects to it with: CosmicPlatformer --online-server http://127.0.0.1:8787
# (or COSMIC_ONLINE_ENV=development).
set -euo pipefail
cd "$(dirname "$0")/.."

npm ci
if [ ! -f .dev.vars ]; then
    # Random local-only values; never reuse them in staging or production.
    printf 'IP_HASH_SALT=%s\nADMIN_TOKEN=%s\n' "$(openssl rand -hex 24)" "$(openssl rand -hex 32)" > .dev.vars
    chmod 600 .dev.vars
    echo ".dev.vars créé (secrets locaux aléatoires)."
fi
npx wrangler d1 migrations apply cosmic-online-dev --local --env dev

if [ "${1:-}" = "--levels" ]; then
    GAME="${COSMIC_GAME_BIN:-../build/bin/CosmicPlatformer}"
    MANIFEST="$(mktemp -d)/manifest.json"
    "$GAME" --online-manifest "$MANIFEST"
    npx wrangler dev --env dev --port 8787 --ip 127.0.0.1 &
    SERVER=$!
    trap 'kill $SERVER 2>/dev/null || true' EXIT
    for _ in $(seq 1 60); do curl -sf http://127.0.0.1:8787/api/v1/health >/dev/null && break; sleep 1; done
    node scripts/register-levels.mjs "$MANIFEST" http://127.0.0.1:8787
    echo "Niveaux enregistrés. Serveur local : http://127.0.0.1:8787 (Ctrl+C pour arrêter)"
    wait $SERVER
else
    exec npx wrangler dev --env dev --port 8787 --ip 127.0.0.1
fi
