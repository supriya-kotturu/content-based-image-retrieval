#!/usr/bin/env bash
# Builds the Linux `query` for deployment into out/linux/query (DEPLOYMENT.md, step 3).
# Its own folder because Git Bash resolves out/query to out/query.exe, which would hide a missing
# Linux build.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

docker build -t cbir-linux -f deploy/Dockerfile.linux deploy

# pwd -W gives a Windows path for Docker Desktop; MSYS_NO_PATHCONV stops Git Bash rewriting /src
src="$(pwd -W 2>/dev/null || pwd)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src" cbir-linux bash -c '
  set -e
  cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/opencv \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
  cmake --build build-linux --target query -j"$(nproc)"
  mkdir -p out/linux
  mv -f out/query out/linux/query
  echo "--- dynamic dependencies (expect only glibc pieces):"
  ldd out/linux/query'
