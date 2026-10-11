# Deploying to Netlify

The web UI runs on Netlify as a **read-only search demo**. Every feature and metric works on the 1,106
database images, and so does searching with a one-off uploaded image. Adding images to the database is
switched off. Site: `cbir-studio-skotturu` → <https://cbir-studio-skotturu.netlify.app>.

Each step below names the script that does it and the check that proves it worked. Three scripts in
`deploy/` do the work. Run them in Git Bash from the project root.

| Step | Command | Time |
|---|---|---|
| 3. Linux `query` | `bash deploy/build-linux.sh` | ~10 min the first time (OpenCV), ~30 s after |
| 4. Stage files | `bash deploy/stage-netlify.sh` | seconds |
| 6. Deploy | `bash deploy/netlify-deploy.sh` (draft) / `--prod` | ~3 min |

- [1. What Netlify changes](#1-what-netlify-changes)
- [2. How the app adapts (read-only mode)](#2-how-the-app-adapts-read-only-mode)
- [3. Build the C++ `query` program for Linux](#3-build-the-c-query-program-for-linux)
- [4. Stage the runtime files](#4-stage-the-runtime-files)
- [5. Configure the Netlify site (once)](#5-configure-the-netlify-site-once)
- [6. Deploy](#6-deploy)
- [7. Verify](#7-verify)
- [What broke on the first deploy](#what-broke-on-the-first-deploy)
- [Troubleshooting](#troubleshooting)
- [If you need uploads in production](#if-you-need-uploads-in-production)

## 1. What Netlify changes

On Netlify, the Next.js route handlers run inside one serverless function (AWS Lambda, Amazon Linux 2023,
x86-64), and static files are served from the CDN.

| The local app assumes | On Netlify | Consequence |
|---|---|---|
| `out/query.exe` with OpenCV **Windows** DLLs | Functions run on **Linux** | Build a Linux `query` with OpenCV linked statically (step 3) |
| `data/` is writable | The function's files are **read-only**. Only `/tmp` is writable, per instance. | Database uploads and `extract` are switched off |
| Background re-indexing after an upload | No work is guaranteed to finish after the response | Same: switched off |
| Two requests can share server state | Each request may land on a **different instance** | A one-off image is sent in the same request as its search |
| Uploads up to 10 MB | Request payload limit 6 MB, about **4.5 MB for binary uploads** | 4 MB limit in read-only mode |
| `/api/image` streams each thumbnail | One function call per image | Database images are served from the CDN |
| `data/olympus` is gitignored (unlicensed photos, public repo) | A Git-based build has no images | Deploy from this machine with the CLI |

Limits from the [functions configuration reference](https://docs.netlify.com/build/functions/configuration/):
- 60 s synchronous execution
- 1024 MB memory
- 6 MB buffered request payload
- 250 MB unzipped handler bundle (this app uses about 110 MB)

## 2. How the app adapts (read-only mode)

Everything is switched by environment variables, so local behaviour is unchanged.

| Variable | Effect |
|---|---|
| `CBIR_READ_ONLY=1` | `POST /api/images` and `POST /api/reindex` return 403. `/api/config` reports `readOnly`, so the Database page hides the upload area and the Rebuild button. The upload limit drops to 4 MB. |
| `CBIR_IMAGE_BASE=/olympus` | `imageUrl()` returns `/olympus/<name>`, served by the CDN from `web/public/olympus`. |
| `CBIR_ROOT=cbir` | A relative path, resolved against the function's working directory (`/var/task`). |

These parts are always on, locally too:

- **One-off search is one request.** `POST /api/query` accepts multipart (`file` plus `request` JSON). The
  image is written to `os.tmpdir()`, searched, and deleted in a `finally` block.
- **The binary is copied to `/tmp` and made executable** on first use (`runnableExe` in `web/lib/cbir.ts`),
  because the bundle is read-only and may not keep the exec bit.
- **`GET /api/health`** reports the function's working directory, the resolved paths, whether `query` exists,
  and the feature and image counts.

## 3. Build the C++ `query` program for Linux

```
bash deploy/build-linux.sh
```

This builds `deploy/Dockerfile.linux`, which is `amazonlinux:2023` (the Lambda OS, so glibc matches) with a
static, headless OpenCV **5.0.0**, the same version as the Windows build. It then compiles `query` into
**`out/linux/query`**. That's its own folder because Git Bash resolves `out/query` to `out/query.exe`,
which would hide a missing Linux build. `libstdc++` and `libgcc` are linked statically.

**Check:** the script ends with `ldd` output listing only `libc`, `libm` and the loader. Then run the task
queries inside a clean Lambda-like container. They must match the README table:

```
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src amazonlinux:2023 bash -c '
  cp out/linux/query /tmp/q && chmod +x /tmp/q &&
  /tmp/q csv=data/csv/texture_color_16.csv target=data/olympus/pic.0535.jpg metric=multi top=3 gui=0'
```

The expected Task 4 result is 0745, 0011, 0004.

## 4. Stage the runtime files

```
bash deploy/stage-netlify.sh
```

This copies, into `web/` (the Netlify base directory; the function can only bundle files under it):

- `web/cbir/`: `out/query` (the Linux binary) and `data/csv`, `data/resNet`, `data/olympus`. `query` needs
  the target jpg for database searches.
- `web/public/olympus/`: the images again, for the CDN.

Both folders are gitignored build output. Re-run the script after re-extracting any feature.

## 5. Configure the Netlify site (once)

Already done for `cbir-studio-skotturu`. For a new site:

```
cd web
netlify login
netlify sites:create --name <site-name> --account-slug <team-slug>     # links web/ to it
netlify env:set CBIR_ROOT cbir
netlify env:set CBIR_READ_ONLY 1
MSYS_NO_PATHCONV=1 netlify env:set CBIR_IMAGE_BASE /olympus
```

`MSYS_NO_PATHCONV=1` matters: without it, Git Bash turns `/olympus` into `C:/Program Files/Git/olympus`
before the CLI sees it.

`web/netlify.toml` holds the rest:
- the build command
- the Next.js adapter (`@netlify/plugin-nextjs`, pinned in `package.json`)
- `included_files` for the four `cbir/` paths

## 6. Deploy

```
bash deploy/netlify-deploy.sh            # draft: prints a preview URL
bash deploy/netlify-deploy.sh --prod     # production, once the draft passes step 7
```

The script runs `netlify deploy --build` inside a `node:22` container on a clean copy of `web/`, without
Windows `node_modules`, `.next`, or `.env.local` (whose `CBIR_ROOT=..` must not reach the site). It reads
the token that `netlify login` stored, so the token is never typed or printed.

## 7. Verify

Run these against the draft URL before `--prod`. The results below are from the first deploy:

| Check | Expected | First deploy |
|---|---|---|
| `GET /api/health` | `queryExe.exists: true`, 7 features, 1106 images, `readOnly: true` | ✓ (cwd `/var/task`, root `/var/task/cbir`) |
| Task 4 (texture_color, multi, `pic.0535`) | 0745, 0011, 0004 | ✓ (~120 ms in `query`, ~0.9 s end to end) |
| Tasks 1, 5, 7 | as in the README | ✓ |
| One-off upload (histogram, intersection) | results, about 1 s | ✓ |
| One-off upload over 6 MB | rejected | ✓ 413 from the platform. The UI blocks anything over 4 MB first. |
| `POST /api/images`, `POST /api/reindex` | 403 | ✓ |
| `/api/image?p=../../etc/passwd` | 400 | ✓ |
| Database page | images load from `/olympus/...`; no upload area | ✓ |

## What broke on the first deploy

| Symptom | Cause | Fix (now in the repo) |
|---|---|---|
| The Linux compile failed inside glibc headers ("missing binary operator before token") | The project's `include/features.h` shadowed glibc's internal `<features.h>`, because `include/` is on the `-I` path. MSVC has no glibc, so it never showed up on Windows. | Renamed to `include/image_features.h` |
| `the .meta names an unknown feature` on Linux only | `git checkout` gave `.meta` files CRLF endings (`core.autocrlf`). Linux `getline` keeps the `\r`, so `histogram\r` didn't match. | `readMeta` strips a trailing `\r` |
| `cannot find -lstdc++` when linking statically | Amazon Linux doesn't ship the static `libstdc++` by default | `libstdc++-static` in the Dockerfile, as a separate layer so the OpenCV cache is kept |
| "Failed publishing static content" from the Next.js adapter | The adapter's post-build step fails when run on Windows | Deploy from a Linux container (`netlify-deploy.sh`) |
| `CBIR_IMAGE_BASE` became `C:/Program Files/Git/olympus` | Git Bash path conversion | `MSYS_NO_PATHCONV=1` |
| The Docker build died with `EOF` mid-install | Docker Desktop auto-updated and restarted its engine | Re-run once the engine is back (`docker info`) |

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `spawn … ENOENT`, or `/api/health` shows `exists: false` | `web/cbir` wasn't staged before the deploy, or `CBIR_ROOT` is wrong. Re-run step 4, then step 6. |
| `spawn … EACCES` | The copy to `/tmp` failed. Check the function logs for `cannot prepare query`. |
| `GLIBC_2.xx not found` | The binary was built somewhere other than `amazonlinux:2023`. Use `build-linux.sh`. |
| Results differ from the README | A stale `web/cbir` from an older extract. Re-run step 4. |
| Thumbnails broken | `CBIR_IMAGE_BASE` wrong (`netlify env:list`), or `public/olympus` missing before the build |
| Function size error | More than the four `included_files` patterns, or `web/cbir` holds extra files |

## If you need uploads in production

Uploads need a persistent disk and work that keeps running after a response is sent, which serverless
functions don't provide. Options, from least to most change:

- **One machine with a disk:** a VM, or a container host with a volume such as Fly.io or Render.
  - Build both `query` and `extract` for Linux (`build-linux.sh` without `--target query`).
  - Mount `data/` on the volume and run `npm run build && npm run start`.
  - Keep a **single** instance, because the job lock lives in memory.
  - Add authentication.
- **Serverless with managed storage:**
  - Store images in S3 or R2.
  - Have upload events trigger `extract` through a queue (SQS, with a dead-letter queue).
  - Make writes idempotent upserts into a vector store rather than csv appends.

  See the trade-off notes in [ARCHITECTURE.md](ARCHITECTURE.md#what-breaks-at-scale).
