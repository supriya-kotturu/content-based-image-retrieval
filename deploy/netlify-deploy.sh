#!/usr/bin/env bash
# Deploys web/ to Netlify from a Linux container (DEPLOYMENT.md, step 6).
#   bash deploy/netlify-deploy.sh          draft deploy (prints a preview URL)
#   bash deploy/netlify-deploy.sh --prod   production deploy
# Why a container: the Next.js adapter's "publish static content" step fails when run on Windows,
# and a clean Linux node_modules avoids shipping Windows-only native packages.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

: "${NETLIFY_SITE_ID:=$(node -e "process.stdout.write(require('./web/.netlify/state.json').siteId)")}"
if [ -z "${NETLIFY_AUTH_TOKEN:-}" ]; then
  # The token `netlify login` stored; read here so it's never typed or printed
  NETLIFY_AUTH_TOKEN="$(node -e "
    const os = require('os'), path = require('path');
    const dir = process.env.APPDATA ? path.join(process.env.APPDATA, 'netlify', 'Config')
                                    : path.join(os.homedir(), '.config', 'netlify');
    const c = require(path.join(dir, 'config.json'));
    process.stdout.write(c.users[c.userId].auth.token);")"
fi
export NETLIFY_SITE_ID NETLIFY_AUTH_TOKEN

src="$(pwd -W 2>/dev/null || pwd)"
MSYS_NO_PATHCONV=1 docker run --rm -e NETLIFY_AUTH_TOKEN -e NETLIFY_SITE_ID -v "$src:/repo:ro" node:22 bash -c "
  set -e
  mkdir /w && cd /repo/web
  # .env.local points CBIR_ROOT at the local checkout; the site's own env vars must win
  tar --exclude=./node_modules --exclude=./.next --exclude=./.netlify --exclude=./.env.local -cf - . | (cd /w && tar xf -)
  cd /w && npm ci --no-audit --no-fund
  npx -y netlify-cli@18 deploy --build --site \"\$NETLIFY_SITE_ID\" $*"
