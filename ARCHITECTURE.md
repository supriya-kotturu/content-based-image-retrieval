# Architecture

How the two C++ programs and the web UI fit together, how uploads work, why it's built this way, and what
comes next. For build commands see [BUILD.md](BUILD.md); for hosting see [DEPLOYMENT.md](DEPLOYMENT.md).

![High-level architecture](docs/architecture.svg)

<sub>Editable source: [docs/architecture.excalidraw](docs/architecture.excalidraw) (open it at
[excalidraw.com](https://excalidraw.com) → Open).</sub>

- [Overview](#overview)
- [The C++ side](#the-c-side)
- [The web side](#the-web-side)
- [How uploads work](#how-uploads-work)
- [Searching](#searching)
- [Running locally vs on Netlify](#running-locally-vs-on-netlify)
- [Design decisions](#design-decisions)
- [What breaks at scale](#what-breaks-at-scale)
- [Roadmap](#roadmap)

## Overview

The system has three layers. Each one can be used without the layers above it.

| Layer | What it is | Owns |
|---|---|---|
| **C++ programs** (`src/`, built to `out/`) | `extract` turns images into feature vectors; `query` ranks the database against a target | All feature and distance math |
| **Data** (`data/`) | Plain files: images, one csv + `.meta` per feature, the provided ResNet18 embeddings | Persistent state (the file system is the database) |
| **Web app** (`web/`) | Next.js pages plus route handlers that run the C++ programs | Validation, uploads, job control, presentation |

The main rule: **the web app never computes a feature or a distance.** Every ranking is a `query` run and
every index update is an `extract` run. That's why web results match the command line exactly. The README
results table reproduces through the UI, locally and on Netlify.

## The C++ side

```
            offline, per feature                         online, per target
 data/olympus/*.jpg ──► extract ──► data/csv/<feature>.csv + .meta ──► query ──► ranked list
                                                                       ▲
                                              target image ────────────┘
```

- **Two programs, so the slow step runs once.** `extract` reads the images (a full run of `texture_color`
  takes about 15 s in Release). `query` only reads the csv and featurizes the target.
- **The `.meta` file is the contract between them.** It records the feature type, `bins` and `patch`.
  `query` computes the target with exactly those settings and refuses a csv without a `.meta`. Without it,
  a 16-bin target could silently be compared with 8-bin rows.
- **Paths in csv files are relative to `data/`**, so a feature file doesn't depend on where the project is.
- **Added for the web app:**
  - **`query gui=0`** prints the results and exits instead of opening the results window.
  - **`extract append=1`** computes only images that have no row yet ([decision 3](#3-incremental-indexing-extract-append1)).
  - **Atomic writes:** `saveFeatures` writes `.tmp` files, then renames them over the old ones ([decision 4](#4-atomic-file-swaps-instead-of-a-query-lock)).
  - **Progress:** `extract` flushes its `[i/n]` lines, because piped stdout is fully buffered.
- **Portability fixes found by the Linux build:**
  - The feature header was renamed to `image_features.h`, because `features.h` shadowed glibc's own.
  - `.meta` parsing tolerates Windows line endings.

| C++ file | Purpose |
|---|---|
| `src/extract.cpp`, `src/query.cpp` | The two programs |
| `src/features.cpp`, `include/image_features.h` | Feature extractors and their status codes |
| `src/sobel.cpp`, `include/sobel.h` | Sobel X/Y and gradient magnitude (reused from Project 1) |
| `src/metrics.cpp`, `include/metrics.h` | Distance metrics |
| `src/feature_store.cpp`, `include/feature_store.h` | Atomic saving and checked loading of feature files and their `.meta` |
| `src/display.cpp`, `include/display.h` | The scrollable results window |
| `src/common.cpp`, `include/common.h` | `key=value` argument parser |
| `src/csv_util.cpp`, `include/csv_util.h` | CSV reading and writing provided with the assignment (unchanged) |

## The web side

| Web file | Responsibility |
|---|---|
| `web/lib/cbir.ts` | **The only module that knows the programs exist.** Builds the argument arrays, spawns the programs (copied to the temp dir and made executable on Linux), parses `rank ⇥ distance ⇥ path`, and reports `extract` progress. |
| `web/lib/jobs.ts` | Runs one `extract` pass at a time: `append` after uploads, `full` from Rebuild. Requests made during a run queue exactly one more pass. |
| `web/lib/meta.ts` | Lists features from `data/csv/*.meta`. Reads which images each csv has a row for (cached by file mtime and size), which is the source of truth for availability. |
| `web/lib/images.ts` | Lists the database with each image's `missing` features. Validates uploads (extension, size, JPEG/PNG magic bytes). Holds a one-off target for the length of one search. |
| `web/lib/compat.ts` | Shared by client and server: feature → valid metrics, the availability rule, the two-line explanations shown in the UI, and the API types |
| `web/lib/paths.ts` | `CBIR_ROOT`, `CBIR_READ_ONLY`, `CBIR_IMAGE_BASE`, and the path-traversal guard |
| `web/app/api/*/route.ts` | Thin HTTP layer: `config`, `images`, `image`, `query`, `reindex`, `health` |
| `web/components/*` | Search and Database views (shadcn/ui on Base UI, Tailwind), including the info popovers |

## How uploads work

There are two kinds of upload, and they work very differently.

| | Add to the database | One-off search image |
|---|---|---|
| Where | Database page, drag and drop (several files at once) | Search page → Upload tab (one file) |
| Stored | `data/olympus/upload_<epoch-ms>_<slug>.<ext>`, permanently | Never stored; it lives for one request |
| Indexed | Yes, by `extract append=1` for every feature | No |
| Can appear in results | Yes, once indexed | No |
| Pixel features (histogram, texture, …) | ✓ | ✓ |
| ResNet features (`dnn`, `orange_dnn`) | ✗ (no provided embedding) | ✗ |
| On Netlify | Switched off (403) | ✓ (≤ 4 MB) |

### Adding images to the database

```
browser                    POST /api/images            jobs.ts                 extract (per .meta)
   │  multipart files ───────►│                           │                          │
   │                          │ check each file:          │                          │
   │                          │  .jpg/.jpeg/.png, ≤10 MB, │                          │
   │                          │  JPEG/PNG magic bytes     │                          │
   │                          │ write upload_<ts>_<slug>  │                          │
   │                          │ (flag wx: never overwrite)│                          │
   │                          ├─ startReindex("append") ─►│                          │
   │◄── 201 {saved, errors} ──┤                           │ for each .meta, in turn: │
   │                                                      ├─ extract append=1 ──────►│ load csv rows
   │  poll GET /api/reindex                               │                          │ compute missing images only
   │  (1.2 s while running) ◄── {current, progress} ──────┤                          │ drop rows of deleted images
   │                                                      │                          │ write .tmp, rename over csv
   │  GET /api/images → image.missing shrinks ◄───────────┤◄────── exit 0 ───────────┤
```

1. **Validate, then store.** Each file is checked on its own. Bad files come back in `errors`, and the rest
   are saved. The name is made unique (`upload_<epoch-ms>_<slug>`), and `wx` refuses to overwrite.
2. **Index incrementally.** `startReindex("append")` runs `extract append=1` once per `.meta`, with the
   parameters stored in that `.meta`.
   - Each run loads the existing rows, computes only images with no row, drops rows whose image is gone,
     and writes the merged result in the folder's sorted order. That makes it byte-identical to a full run.
   - All six features take about **7 s** for one new image. Most of that is process start-up and reading
     the csvs; the feature computation itself is a few milliseconds.
3. **Swap in atomically.** Each csv and `.meta` is written as `.tmp`, then renamed over the old file.
   Searches keep running throughout and see either the old index or the new one.
4. **Availability updates by itself.** `GET /api/images` reports, for each image, the features it's
   `missing`, read from the csv rows. While an upload is being indexed, the Database page shows
   **INDEXING** on it. Search results say how many images aren't indexed for the chosen feature yet.

Lifecycle of an uploaded image:

```
 saved ──► missing: all 7 features ──► (each extract pass) ──► missing: [orange_dnn, dnn]
                                                                   (final: no ResNet embedding)
```

Behaviour at the edges:

| Situation | What happens |
|---|---|
| More uploads arrive during a pass | One more `append` pass is queued. It picks up everything that arrived, however many files. |
| Rebuild is clicked during a pass | The queued pass becomes `full`. A full run wins over append. |
| One feature's `extract` fails | The other features still run. The banner shows the error, and the image stays `missing` for that feature. |
| An upload decodes in the browser but OpenCV can't read it | `extract` skips it and the image stays `missing`. It never breaks other rows. |
| The same photo is uploaded twice | Two separate images, because names are unique. There's no content-hash dedupe yet. |
| An upload file is deleted by hand | The next `append` pass drops its rows (`dropped 1`). |
| The csv and `.meta` disagree (crash, hand edit) | `append` refuses to extend it. A full run (Rebuild) rewrites it. |
| The server restarts during a pass | Job state is lost (in memory). A partially written `.tmp` is never renamed, so the csvs stay consistent. The next upload or Rebuild catches up. |

### One-off search images

`POST /api/query` as multipart (`file` plus a `request` JSON field):

1. The browser keeps the file and shows a local preview (`URL.createObjectURL`). It's sent with **every** search.
2. The server validates it like a database upload. The limit is 4 MB on Netlify and 10 MB locally.
3. It writes the file to the OS temp folder, runs `query` with it as the target, and deletes it in a
   `finally` block.
4. The echoed command shows `target=<path to your file>` instead of the throwaway server path.

**Why one request:** the first version uploaded the image (`/api/targets`) and searched in a second request.
On a serverless host, those two requests can land on different instances with different `/tmp` folders, so
the second couldn't find the file. Sending the image with the search makes the server stateless, and the
same code then works locally and on Netlify.

## Searching

`POST /api/query`, as JSON for a database target or multipart for a one-off image:

1. **Validate:**
   - `featureId` must be one listed from the `.meta` files. It ends up in a path, so free text is never accepted.
   - The metric must be valid for that feature.
   - `top` must be 1–100 and `worst` 0–50.
   - The target must exist.
   - The availability rule must allow it.
2. **Run** `query` with an argument array (never a shell string) and a 30 s timeout.
3. **Parse** stdout. Lines without tabs, such as csv_util's "Reading …", are skipped. Rows become
   `{rank, distance, name, url}`.
4. **Respond** with `{ matches, worst, elapsedMs, command, unindexed }`:
   - `command` is the exact CLI invocation, so any result can be reproduced in a terminal.
   - `unindexed` counts images missing from this feature's index, so the UI can say so.

**Availability rule** (`compat.ts`), one rule for every image: `usable(f) = !f.targetFromIndex ||
!image.missing.includes(f.id)`. Pixel features work for any target, because `query` computes the target's
vector from the file. The ResNet features read the target's vector from the index, so they need the
target's own row.

## Running locally vs on Netlify

| | Local (`npm run dev` / `start`) | Netlify ([live](https://content-based-image-retrieval.netlify.app)) |
|---|---|---|
| Programs | `out/query.exe`, `out/extract.exe` (Windows, OpenCV DLLs) | `out/linux/query` only (static OpenCV, built in `amazonlinux:2023`) |
| Data | `data/` read and written in place | Staged copy in the function bundle, read-only |
| Database images | `/api/image?p=…` | CDN: `/olympus/<name>` (`CBIR_IMAGE_BASE`) |
| Database uploads, Rebuild | ✓ | 403 (`CBIR_READ_ONLY=1`), hidden in the UI |
| One-off search | ✓ (≤ 10 MB) | ✓ (≤ 4 MB, the platform payload limit) |
| Health | `GET /api/health` | same; shows `/var/task/cbir` paths |

## Design decisions

| # | Decision | Date | Instead of | Because |
|---|---|---|---|---|
| 1 | Run the C++ programs as subprocesses | 10-10 | N-API addon, a long-lived C++ service, rewriting in TS | Results identical to the CLI; a crash only kills the child process |
| 2 | The file system is the database | 10-10 | SQLite/Postgres/vector DB | The CLI already reads and writes csv + `.meta`; 1.1k rows |
| 3 | Incremental indexing (`append=1`) | 10-10 | Re-extract the whole folder per upload | ~56 s → ~7 s per upload, byte-identical output |
| 4 | Atomic file swaps | 10-10 | A lock refusing searches (409) during indexing | Searches never block; readers never see a partial file |
| 5 | Job state in process memory | 10-10 | A queue service | One local process is enough |
| 6 | Availability derived from the csv rows | 10-10 | A separate code path or status table for uploads | Can't disagree with what `query` searches |
| 7 | Stateless one-off search | 10-10 | Upload first, then search | Works across serverless instances |
| 8 | The UI mirrors `query.cpp` validation | 10-10 | Only server-side errors | Invalid choices are disabled up front; C++ still has the final say (422) |
| 9 | ResNet features only for original images | 10-10 | Shipping a network now | Embeddings were provided; a mismatched network gives meaningless distances |
| 10 | Netlify as a read-only demo, deployed from a container on this machine | 10-10 | Uploads on serverless; data in git | Read-only functions; the photos are unlicensed and the repo is public |

### 1. Subprocesses rather than porting or embedding

| Option | Pros | Cons |
|---|---|---|
| **Subprocess per request (chosen)** | No changes to the feature code; results match the CLI exactly; a crash only kills the child | About 100–360 ms per query to start the process and reload the csv; the programs must exist for the server's OS |
| Native Node addon (N-API) | In-process, fast | node-gyp plus OpenCV on Windows is fragile; a segfault kills the server |
| Long-lived C++ HTTP service | Fastest: vectors stay in memory | A second server to write and run |
| Rewrite the features in TS/Python | One language | Two implementations that drift apart |

At 1.1k rows, a few hundred ms is not noticeable. Moving to a long-lived service only means swapping
`lib/cbir.ts`.

### 2. The file system is the database

csv + `.meta` files are what the CLI already uses, so the CLI and web app always agree. The costs: one
writer at a time, a whole-file rewrite per change, and a single machine.

### 3. Incremental indexing: `extract append=1`

`append=1` loads the existing csv, computes only images without a row, drops rows for deleted images, and
writes in folder order. The result is **byte-identical to a full run** (checked with `cmp`). The csv name
encodes `bins`/`patch`, so reused rows always share the requested parameters. The file is still rewritten
whole, which is O(N) I/O, but feature computation is O(new). Rebuild still runs a full pass, which is
needed when the feature code changes.

### 4. Atomic file swaps instead of a query lock

`saveFeatures` writes `<name>.csv.tmp` and `<name>.meta.tmp`, then renames each over the old file. A
concurrent `query` sees the old version or the new one. Two edges:

- **The pair can't be swapped in one step.** Between the two renames the row counts disagree, and
  `loadFeatures` retries a mismatch for about 0.5 s before reporting it.
- **Windows won't replace a file another process has open.** The rename retries for up to about 5 s.

Tested: 60 of 60 queries were correct while `extract` rewrote the same csv three times.

### 5. Job state lives in process memory

`jobs.ts` keeps its state on `globalThis`, which survives dev hot reloads but not a restart. It assumes
**one server process**: two instances would each run `extract` on the same files. That's fine locally.
Anything bigger needs a queue (see the [roadmap](#roadmap)).

### 6. Availability is data, not a code path

An uploads-only flag (`hasEmbedding`) and a separate UI path for uploads were both rejected in favour of
reading the index. Every image gets `missing` from the csv rows, and one rule decides availability.
Originals, fresh uploads and half-indexed uploads are all just different `missing` sets.

### 7. Stateless one-off search

See [One-off search images](#one-off-search-images). This was the one change Netlify forced that also made
the local design simpler: no `/api/targets`, no `data/tmp`, nothing to clean up.

### 8. The UI mirrors validation; the C++ decides

`compat.ts` mirrors `query.cpp`'s feature/metric checks, so invalid combinations are disabled before sending.
The duplication is deliberate. If the two ever disagree, `query`'s own message comes back as a 422.

### 9. ResNet features only cover the original images

The embeddings are a provided csv, and there is no network in the project. Adding one needs a parity check
first (see the roadmap). Otherwise new vectors would live in a different space from the provided ones, and
cross-group distances would look plausible but mean nothing.

### 10. Netlify as a read-only demo

Functions have a read-only file system, can't keep working after a response, and requests may hit
different instances. So uploads and `extract` are switched off there.
- **Data:** the photos have no license and the repo is public, so they stay out of git. They're staged into
  the bundle at deploy time, and the deploy runs from this machine.
- **Container deploy:** Netlify's Next.js adapter fails on Windows, so the deploy runs in a `node:22`
  container.

Details and the issues hit on the first deploy are in [DEPLOYMENT.md](DEPLOYMENT.md).

### Security posture

- Programs are started with argument arrays, never through a shell.
- Feature ids must match a listed `.meta`; image names must match `^[\w.-]+$`.
- `/api/image` serves image files under `data/` only. `?p=../…` returns 400.
- Uploads are checked by extension, size and magic bytes.
- **No authentication.** That's acceptable for the deployed site only because it's read-only. Uploads must
  not be exposed publicly without auth.

## What breaks at scale

| Pressure | First thing to break | Fix |
|---|---|---|
| 100k+ images | Each query re-parses csvs of hundreds of MB; `append` still rewrites whole files | A long-lived service with vectors in memory or `mmap`, a store with upsert, and an approximate index (e.g. HNSW) |
| Many uploads at once | One pass at a time, each rewriting every csv | Already batched (one queued pass covers all arrivals); beyond that, a database |
| Multiple server instances | In-memory job state; files on local disk | A job queue, plus object storage for images |
| Uploads on serverless | Read-only, per-instance storage; no background work | The event-driven pipeline below |

## Roadmap

Roughly in order of value for effort. **Status: not started** unless noted.

1. **Delete uploads from the Database page.** The `append` pass already drops rows for missing files, so
   this only needs a `DELETE /api/images/:name` that removes the file and starts an append pass.
2. **Deduplicate uploads** by content hash, so the same photo uploaded twice is one image.
3. **Your own ResNet18 embeddings** (`cv::dnn` + ONNX inside `extract`), so uploads get `dnn` and `orange_dnn` too.
   **Parity gate first:** re-embed the 1,106 originals and require cosine similarity ≈ 1.0 against the
   provided rows. That checks preprocessing (resize, crop, normalization) and the layer used. Parked until
   the current version is polished.
4. **Uploads in production on a host with a disk.** A VM or a container with a volume, a single
   instance, and authentication added first.
5. **Uploads on serverless: an event-driven pipeline.**
   ```
   upload ─► S3 (images/) ─event─► SQS ─► extract worker (Lambda) ─upsert─► vector store
                                     └─ retries, then dead-letter queue        ▲
                                                           query function ─────┘
   ```
   - **S3 and SNS deliver events at least once**, so workers must be idempotent: upsert keyed by image id
     or content hash, never append.
   - **Use SQS rather than SNS → Lambda directly** for retries, a dead-letter queue and backpressure. At
     this volume both SNS and SQS fit in the free tier; a self-hosted broker only gets cheaper around
     10M+ messages/month.
   - **One worker computing all features** (one image decode, one cold start) rather than one Lambda per
     feature, unless the isolation is needed.
   - **Check the metrics against the store.** Vector stores index L2, cosine and inner product (some also
     L1). Histogram intersection, `multi` and `custom` need a mapping to one of those, or exact re-ranking
     of an approximate shortlist.
6. **A long-lived query service.** Keep the vectors in memory and replace `lib/cbir.ts`'s spawns with HTTP
   calls. Only worth it once query latency matters, around 10k+ images.
7. **CI deploys.** Keep the data in a private bucket and download it in the build with a secret, so pushes
   to `main` deploy without this machine.
