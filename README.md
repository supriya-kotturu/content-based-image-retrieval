# Content-based Image Retrieval

Given a target image and a database of images (`data/olympus`, 1,106 photos), the programs return the
images most similar to the target. The work is split into two programs so the slow step is done once:

- `extract` computes one feature vector per database image and saves them (a csv plus a `.meta`).
- `query` loads a saved feature file, ranks every image by distance to the target, prints the best
  matches and shows them in a window.

## Environment

- Windows 11, Visual Studio Code, MSVC (Visual Studio Build Tools), CMake 3.15 or newer, C++17.
- OpenCV 5 (found through `C:/opencv/build`, or pass `-DOpenCV_DIR=<folder with OpenCVConfig.cmake>`).

## Build

From the project folder:

```
cmake -S . -B build
cmake --build build --config Release
```

This produces `out/extract.exe` and `out/query.exe`. Use **Release**: a Debug build runs the pixel
loops several times slower (extracting `texture_color` took 77 s in Debug and 15 s in Release).
Debug and Release libraries go to separate folders under `out/lib/`, so switching between them is safe.

## Data layout

```
data/olympus/            the 1,106 database images (pic.0001.jpg ...)
data/resNet/ResNet18_olym.csv   provided ResNet18 embeddings (512 values per image)
data/csv/                feature files written by extract (one .csv + one .meta each)
```

Paths stored in the csv files are relative to `data/` (for example `olympus/pic.0001.jpg`), so a feature
file does not depend on where the project folder is.

## extract

```
./out/extract.exe db=data/olympus feature=<name> [bins=16] [patch=<odd size>] [dnn=<csv>] [out=<dir>] [root=data]
```

| feature | what it is | parameters | output file |
|---|---|---|---|
| `baseline` | 7x7 pixel square at the center of the image | `patch` (default 7) | `baseline_7x7.csv` |
| `histogram` | whole-image rg chromaticity histogram | `bins` (default 16, per axis) | `histogram_16.csv` |
| `multi_histogram` | top half rg + bottom half bg histograms + a center RGB histogram | `bins`, `patch` (center square, default 101) | `multi_histogram_16_c101.csv` |
| `texture_color` | rg histogram + histogram of Sobel gradient magnitudes | `bins` | `texture_color_16.csv` |
| `orange_dnn` | custom: provided embedding + histogram of the strongly orange pixels | `dnn=` (required) | `orange_dnn.csv` |

- Each feature file has a `.meta` next to it (same name) recording the feature, its parameters, and
  the row and vector counts. `query` reads it, so the target is computed exactly like the database
  images, and a csv without its `.meta` is refused.
- `extract` skips images it cannot read, prints why, and stops if a parameter is invalid.
  Output is overwritten on every run.
- Unknown or misspelled arguments are reported instead of silently ignored.

## query

```
./out/query.exe csv=<feature csv> target=<image> metric=<name> [top=10] [worst=0] [root=data]
```

| metric | use with |
|---|---|
| `ssd` | `baseline` (sum of squared differences, written from scratch) |
| `intersection` | `histogram` (1 - histogram intersection) |
| `multi` | `multi_histogram`, `texture_color` (equally weighted mean of 1 - intersection per piece) |
| `cosine` | the provided embeddings (1 - cos of the angle) |
| `custom` | `orange_dnn` (0.7 x cosine of the embedding + 0.3 x (1 - intersection of the orange shade)) |

For the provided embeddings, which have no `.meta` and cannot be recomputed:

```
./out/query.exe feature=dnn csv=data/resNet/ResNet18_olym.csv target=pic.0893.jpg metric=cosine [db=data/olympus]
```

`query` prints `rank, distance, file` for the best `top` matches (the target itself is excluded) and, with
`worst=N`, the N least similar. It then opens a window with the target and the matches; q, Esc or closing
the window quits.

## Running each task

Build first, then extract once per feature and query as many targets as you like.

| task | extract | query (target; metric) | top three I get |
|---|---|---|---|
| 1 baseline | `feature=baseline` | `pic.1016.jpg`; `ssd` | 0986, 0641, 0547 (4th: 1013), as in the reference result |
| 2 histogram | `feature=histogram` | `pic.0164.jpg`; `intersection` | 0080, 1032, 0461 |
| 3 multi-histogram | `feature=multi_histogram` | `pic.0274.jpg`; `multi` | 0409, 1055, 0275 |
| 4 texture + color | `feature=texture_color` | `pic.0535.jpg`; `multi` | 0745, 0011, 0004 |
| 5 DNN embeddings | none (provided) | `feature=dnn` with `pic.0893.jpg` and `pic.0164.jpg`; `cosine` | 0897, 0136, 0146 and 1032, 0213, 0690 |
| 6 DNN vs classic | none | `pic.0948`, `pic.1072`, `pic.0734`: the DNN command above, and the `histogram` and `multi_histogram` queries | see the report |
| 7 custom | `feature=orange_dnn dnn=data/resNet/ResNet18_olym.csv` | `pic.0323.jpg` (mask) and `pic.0281.jpg` (basketball); `custom`, with `top=5 worst=3` | see the report |

Example, task 4:

```
./out/extract.exe db=data/olympus feature=texture_color bins=16
./out/query.exe csv=data/csv/texture_color_16.csv target=data/olympus/pic.0535.jpg metric=multi top=3
```

## Task 7: custom design

Goal: find photos that contain one specific strongly orange object (an orange mask, and a basketball as
a second test). The feature is the 512-value ResNet18 embedding followed by a 32-value histogram (8 hue
x 4 saturation bins) of only the pixels that are strongly orange (hue 4 to 35 degrees, saturation and
brightness at least 0.6), so it records the object's shade and ignores the background. The distance is
`0.7 x cosine(embedding) + 0.3 x (1 - intersection(orange shade))`.

## Extensions

- **Results window (GUI):** the target and its matches are shown in a wrapped grid, each labeled with
  rank, file name and distance, with the feature, parameters and metric in the title. The window scrolls
  vertically (mouse wheel, arrow keys, PageUp/PageDown, Home/End). To try it, run any `query` command
  with `top=12` (or more) so the grid wraps and scrolls.
- **Least similar results:** `worst=3` also prints and shows the three images farthest from the target.
- **Custom feature (task 7)** and the mask/basketball comparison described above.

## Source files

| file | purpose |
|---|---|
| `src/extract.cpp`, `src/query.cpp` | the two programs |
| `src/features.cpp`, `include/features.h` | the feature extractors and their status codes |
| `src/sobel.cpp`, `include/sobel.h` | Sobel X/Y and gradient magnitude, reused from Project 1 |
| `src/metrics.cpp`, `include/metrics.h` | the distance metrics |
| `src/feature_store.cpp`, `include/feature_store.h` | saving and loading feature files with their `.meta` |
| `src/display.cpp`, `include/display.h` | the results window |
| `src/common.cpp`, `include/common.h` | the `key=value` argument parser |
| `src/csv_util.cpp`, `include/csv_util.h` | csv reading and writing provided with the assignment (unchanged) |
