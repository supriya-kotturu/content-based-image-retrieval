#!/usr/bin/env bash
# Copies what the deployed app reads into web/ (the Netlify base directory), since a function can
# only bundle files under its base. See DEPLOYMENT.md, step 4. Re-run after re-extracting features.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
web="$root/web"

if [ "$(head -c 4 "$root/out/linux/query" 2>/dev/null | od -An -c | tr -d " ")" != "177ELF" ]; then
  echo "out/linux/query (the Linux build) is missing: build it first (DEPLOYMENT.md step 3)" >&2
  exit 1
fi

rm -rf "$web/cbir" "$web/public/olympus"
mkdir -p "$web/cbir/out" "$web/cbir/data" "$web/public"

cp "$root/out/linux/query" "$web/cbir/out/query"
# query reads the csvs and, for a database target, the target jpg itself
cp -r "$root/data/csv" "$root/data/resNet" "$root/data/olympus" "$web/cbir/data/"
# A second copy for the CDN, so thumbnails aren't one function call each
cp -r "$root/data/olympus" "$web/public/olympus"

du -sh "$web/cbir" "$web/public/olympus"
