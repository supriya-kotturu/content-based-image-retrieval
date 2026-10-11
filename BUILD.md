# Build and run

This file covers the C++ programs, every assignment task as an exact command, and the web UI.
Commands use a bash-style shell (Git Bash). In PowerShell, write `.\out\query.exe` instead of
`./out/query.exe`.

- [Prerequisites](#prerequisites)
- [Build the C++ programs](#build-the-c-programs)
- [Data layout](#data-layout)
- [`extract` reference](#extract-reference)
- [`query` reference](#query-reference)
- [Commands for each task](#commands-for-each-task)
- [Build and run the web UI](#build-and-run-the-web-ui)
- [Troubleshooting](#troubleshooting)

## Prerequisites

| For | Needs |
|---|---|
| C++ programs | Windows 11, MSVC (Visual Studio Build Tools), CMake ≥ 3.15, C++17, OpenCV 5 (found via `C:/opencv/build`, or pass `-DOpenCV_DIR=<folder with OpenCVConfig.cmake>`) |
| Web UI | Node.js ≥ 20 (developed on 24), npm, and the C++ programs built in Release |
| Dataset | `data/olympus/` with the 1,106 images (gitignored, so copy it in yourself) |

## Build the C++ programs

```
cmake -S . -B build
cmake --build build --config Release
```

This produces `out/extract.exe` and `out/query.exe`. The OpenCV DLLs are copied next to them, so no `PATH`
changes are needed.

- **Use Release.** Debug runs the pixel loops several times slower: extracting `texture_color` takes 77 s
  in Debug and 15 s in Release.
- Debug and Release libraries go to separate folders under `out/lib/<config>/`, so switching
  configurations doesn't cause link errors.
- After changing any `src/` file, rebuild before running the web UI. It calls whatever is currently in `out/`.

## Data layout

```
data/olympus/                    the 1,106 database images (pic.0001.jpg ...), plus web uploads (upload_*)
data/resNet/ResNet18_olym.csv    provided ResNet18 embeddings (512 values per image)
data/csv/                        feature files written by extract (one .csv + one .meta each)
```

Paths stored in the csv files are relative to `data/` (for example `olympus/pic.0001.jpg`).

## `extract` reference

```
./out/extract.exe db=data/olympus feature=<name> [bins=16] [patch=<odd size>] [dnn=<csv>] [out=<dir>] [root=data] [append=0|1]
```

| feature | what it is | parameters | output file |
|---|---|---|---|
| `baseline` | 7x7 pixel square at the center of the image | `patch` (default 7) | `baseline_7x7.csv` |
| `histogram` | whole-image rg chromaticity histogram | `bins` (default 16 per axis) | `histogram_16.csv` |
| `multi_histogram` | rg histogram of the top half, bg histogram of the bottom half, and an RGB histogram of the center | `bins`, `patch` (center square, default 101) | `multi_histogram_16_c101.csv` |
| `texture_color` | rg histogram + histogram of Sobel gradient magnitudes | `bins` | `texture_color_16.csv` |
| `orange_dnn` | provided embedding + histogram of strongly orange pixels | `dnn=` (required) | `orange_dnn.csv` |

- Each csv gets a `.meta` next to it, recording the feature, its parameters and the row/vector counts.
- Images that can't be read are skipped, with the reason printed. An invalid parameter stops the run.
- Unknown or misspelled arguments are reported, not ignored.
- **`append=1`** reuses the rows already in the csv and computes only images that have none. It also drops
  rows for images that were deleted. The result is byte-identical to a full run. Adding one image to
  `texture_color` takes about 1 s instead of 15 s. Without `append`, every row is recomputed, which you need
  after changing the feature code.
- Files are replaced atomically (written as `.tmp`, then renamed), so a `query` running at the same time
  reads the old version or the new one, never a partial file.

## `query` reference

```
./out/query.exe csv=<feature csv> target=<image> metric=<name> [top=10] [worst=0] [root=data] [gui=0]
./out/query.exe feature=dnn csv=data/resNet/ResNet18_olym.csv target=<name> metric=cosine [db=data/olympus] [gui=0]
```

| metric | use with |
|---|---|
| `ssd` | `baseline` (sum of squared differences) |
| `intersection` | `histogram` (1 − histogram intersection) |
| `multi` | `multi_histogram`, `texture_color` (equally weighted mean of 1 − intersection per piece) |
| `cosine` | the provided embeddings (1 − cos of the angle) |
| `custom` | `orange_dnn` (0.7 × cosine of the embedding + 0.3 × (1 − intersection of the orange shade)) |

- Prints `rank, distance, file` for the best `top` matches. The target itself is excluded.
- `worst=N` also prints the N least similar images.
- It then opens a scrollable results window; q, Esc or closing the window quits.
- `gui=0` skips the window. The web UI always passes it.

## Commands for each task

Run `extract` once per feature, then `query` as often as you like. Results are the top three matches.

### Task 1: baseline matching

```
./out/extract.exe db=data/olympus feature=baseline
./out/query.exe csv=data/csv/baseline_7x7.csv target=data/olympus/pic.1016.jpg metric=ssd top=4
```

Gives 0986, 0641, 0547 (4th: 1013), matching the reference result.

### Task 2: histogram matching

```
./out/extract.exe db=data/olympus feature=histogram bins=16
./out/query.exe csv=data/csv/histogram_16.csv target=data/olympus/pic.0164.jpg metric=intersection top=3
```

Gives 0080, 1032, 0461. `bins=8` writes `histogram_8.csv`, which is useful for comparing resolutions.

### Task 3: multi-histogram matching

```
./out/extract.exe db=data/olympus feature=multi_histogram bins=16
./out/query.exe csv=data/csv/multi_histogram_16_c101.csv target=data/olympus/pic.0274.jpg metric=multi top=3
```

Gives 0409, 1055, 0275.

### Task 4: texture and color

```
./out/extract.exe db=data/olympus feature=texture_color bins=16
./out/query.exe csv=data/csv/texture_color_16.csv target=data/olympus/pic.0535.jpg metric=multi top=3
```

Gives 0745, 0011, 0004.

### Task 5: deep network embeddings

Nothing to extract, because the embeddings are provided.

```
./out/query.exe feature=dnn csv=data/resNet/ResNet18_olym.csv target=pic.0893.jpg metric=cosine top=3
./out/query.exe feature=dnn csv=data/resNet/ResNet18_olym.csv target=pic.0164.jpg metric=cosine top=3
```

Gives 0897, 0136, 0146 and 1032, 0213, 0690.

### Task 6: DNN embeddings vs classic features

Run each of the three targets against the DNN embeddings and the two classic features:

```
for t in pic.0948.jpg pic.1072.jpg pic.0734.jpg; do
  ./out/query.exe feature=dnn csv=data/resNet/ResNet18_olym.csv target=$t metric=cosine top=5 gui=0
  ./out/query.exe csv=data/csv/histogram_16.csv target=data/olympus/$t metric=intersection top=5 gui=0
  ./out/query.exe csv=data/csv/multi_histogram_16_c101.csv target=data/olympus/$t metric=multi top=5 gui=0
done
```

### Task 7: custom design (orange object)

```
./out/extract.exe db=data/olympus feature=orange_dnn dnn=data/resNet/ResNet18_olym.csv
./out/query.exe csv=data/csv/orange_dnn.csv target=data/olympus/pic.0323.jpg metric=custom top=5 worst=3
./out/query.exe csv=data/csv/orange_dnn.csv target=data/olympus/pic.0281.jpg metric=custom top=5 worst=3
```

The first query uses the orange mask as the target, the second the basketball.

### Extensions

- **Results window:** run any query with `top=12` or more, so the grid wraps and scrolls (mouse wheel,
  arrow keys, PageUp/PageDown, Home/End).
- **Least similar:** add `worst=3` to any query.

## Build and run the web UI

The web UI lives in `web/`. It runs `out/query.exe` and `out/extract.exe`, so [build the C++](#build-the-c-programs)
first, and create the feature files (the `extract` commands above) so it has something to search.

### Development

```
cd web
npm install
npm run dev            # http://localhost:3000, reloads on save
```

### Production build (local)

```
cd web
npm run build          # type-checks and builds .next/
npm run start          # serves the production build on http://localhost:3000
```

### Lint

```
cd web
npm run lint
```

### Configuration

| Variable | Default | Meaning |
|---|---|---|
| `CBIR_ROOT` | the folder above `web/` | The project root that holds `out/` and `data/`. Every program runs with this as its working directory. |

To set it, create `web/.env.local` (gitignored) containing `CBIR_ROOT=..` or an absolute path.

### What the UI changes on disk

| Action | Writes |
|---|---|
| Search with a database image | Nothing |
| Search with a one-off upload | A temp file in the OS temp folder, deleted when the search finishes |
| Upload to the database | `data/olympus/upload_*`, then `extract append=1` rewrites every **git-tracked** `data/csv/*.csv` and `*.meta` file with one more row |
| Rebuild (Database page) | Every `data/csv` file, recomputed from scratch |

To undo database uploads:

```
rm data/olympus/upload_*
git checkout data/csv
```

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `cannot start query.exe: spawn … ENOENT` | The C++ isn't built, or `CBIR_ROOT` points to the wrong folder. Build in Release and check `out/query.exe` exists. |
| A query returns 422 with a C++ message | `query` rejected the input, for example a csv without its `.meta`. The message is `query`'s own stderr. |
| An uploaded image is missing from results | It isn't indexed for that feature yet. The results say how many images aren't indexed, and the Database page shows `INDEXING` until it's done. |
| `cannot append to it (status -4)` from `extract append=1` | The csv and its `.meta` disagree, for example after an interrupted write or a hand edit. Run the same command without `append=1` to rebuild it. |
| Exit code `0xC0000135` when running an exe | The OpenCV DLLs are missing next to it. Re-run the CMake build, which copies them. |
| The Database page shows red dots | Those feature files have fewer rows than the image folder. Click **Rebuild**, or wait for the re-index. |
