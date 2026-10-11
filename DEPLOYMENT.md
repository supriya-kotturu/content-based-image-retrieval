# Deploying to Netlify

> **Status: a plan, not yet carried out.** The app currently runs only as a local tool. Netlify runs
> server code as short-lived Linux functions with a read-only file system, which breaks several things
> the local app relies on. This guide lists the code changes needed first ([step 2](#2-code-changes-required-before-deploying)),
> then the build and deploy steps. Each step ends with a check, so problems surface early.

- [1. What Netlify changes](#1-what-netlify-changes)
- [2. Code changes required before deploying](#2-code-changes-required-before-deploying)
- [3. Build the C++ `query` program for Linux](#3-build-the-c-query-program-for-linux)
- [4. Stage the runtime files](#4-stage-the-runtime-files)
- [5. Configure Netlify](#5-configure-netlify)
- [6. Deploy with the Netlify CLI](#6-deploy-with-the-netlify-cli)
- [7. Verify the deployment](#7-verify-the-deployment)
- [Troubleshooting](#troubleshooting)
- [If you need uploads in production](#if-you-need-uploads-in-production)

## 1. What Netlify changes

On Netlify, the Next.js route handlers run inside one serverless function (AWS Lambda, Linux x86-64),
managed by Netlify's Next.js adapter. Static assets are served from the CDN. This table compares what the
app assumes with how Netlify works:

| The local app assumes | On Netlify | Consequence |
|---|---|---|
| `out/query.exe` and `extract.exe` with OpenCV **Windows** DLLs | Functions run on **Linux** | Rebuild `query` as a Linux binary with OpenCV linked statically (step 3) |
| `data/` is a writable folder | The function's files are **read-only**. Only `/tmp` is writable, and it belongs to one instance and is wiped when that instance stops. | **Database uploads can't be stored**, and `extract` can't rewrite csv files |
| A re-index keeps running after the upload response is sent | Work after the response is not guaranteed to finish, and the synchronous limit is 60 s | **Background re-indexing doesn't work** |
| A one-off target is uploaded, then queried in a second request | Two requests may land on **different instances** with different `/tmp` folders | The two requests must become one (step 2.2) |
| Uploads up to 10 MB | The request payload limit is 6 MB, about **4.5 MB for binary uploads** (base64 overhead) | Lower the limit (step 2.4) |
| `/api/image` streams every thumbnail | Every image would be one function call | Serve database images from the CDN (step 2.3) |
| `data/olympus` is gitignored | A deploy from Git has no images | Deploy from your machine with the CLI (step 6) |

**What you get: a read-only search demo.** It supports every feature and metric on the 1,106
database images, plus searching with a one-off uploaded image. Adding images to the database is turned off.
Each limit is a property of the platform, not a bug in the app.

Netlify limits used in this plan (from the [functions configuration reference](https://docs.netlify.com/build/functions/configuration/)):
- 60 s synchronous execution
- 1024 MB memory by default
- 6 MB buffered request payload
- 250 MB unzipped handler bundle
- `included_files` resolved relative to the base directory

## 2. Code changes required before deploying

None of these exist in the repo yet. Each one is controlled by an environment variable, so local behaviour
stays the same.

### 2.1 Read-only mode: `CBIR_READ_ONLY=1`

- `POST /api/images` and `POST /api/reindex` return **403** with the message "database is read-only in
  this deployment".
- `GET /api/config` reports `readOnly: true`. The Database page then hides the upload area and the
  Rebuild button, and shows a short note instead.
- `startReindex()` does nothing.

### 2.2 One-request one-off search

Change `POST /api/query` to also accept `multipart/form-data`, with the image as `file` and the other
fields as before:

1. Save the file to `os.tmpdir()/<uuid>.<ext>`. On Lambda that is `/tmp`, and `data/tmp` would be read-only.
2. Run `query` with that file as the target.
3. Delete the file in a `finally` block.

The browser shows the preview with `URL.createObjectURL(file)`, so nothing has to be served back.
`/api/targets` then isn't needed. Keep it for local use, or remove it.

### 2.3 Database images from the CDN: `CBIR_IMAGE_BASE=/olympus`

Change `imageUrl()` in `web/lib/paths.ts` to return `${CBIR_IMAGE_BASE}/${name}` when the variable is set.
Step 4 copies the images into `web/public/olympus/`, so Netlify's CDN serves them and no function call is needed.

### 2.4 Upload size limit

Lower `MAX_UPLOAD_BYTES` in `web/lib/images.ts` to `4 * 1024 * 1024` when `CBIR_READ_ONLY` is set, so the
UI's error appears before Netlify's own 413.

### 2.5 Make sure the binary can run

Lambda's code directory is read-only, and the deploy might not keep the file's executable bit. On the first
spawn, copy the binary to `/tmp` and mark it executable:

```ts
// web/lib/cbir.ts: the Lambda bundle is read-only and may lose the exec bit
import os from "node:os";
import fs from "node:fs/promises";

let runnable: Promise<string> | undefined;
function runnableExe(src: string): Promise<string> {
  return (runnable ??= (async () => {
    if (process.platform === "win32") return src;
    const dst = path.join(os.tmpdir(), path.basename(src));
    await fs.copyFile(src, dst);
    await fs.chmod(dst, 0o755);
    return dst;
  })());
}
```

The working directory stays `CBIR_ROOT`. `query` only reads from it.

### 2.6 Health check route: `GET /api/health`

Return what the function actually sees, so a path problem can be diagnosed without guessing:

```json
{ "cwd": "...", "cbirRoot": "...", "queryExe": { "path": "...", "exists": true },
  "features": 6, "dbImages": 1106, "readOnly": true }
```

**Check for step 2:** run the app locally with `CBIR_READ_ONLY=1 CBIR_IMAGE_BASE=/olympus npm run dev`.
Uploads are hidden, a one-off search still works, and `/api/health` reports correct paths.

## 3. Build the C++ `query` program for Linux

The binary must run on the Lambda image (Amazon Linux 2023 for current Node runtimes). Building inside
`amazonlinux:2023` matches its glibc. Linking OpenCV statically means there are no `.so` files to ship. Only
`query` is needed, because `extract` never runs in read-only mode.

Save as `deploy/Dockerfile.linux`:

```dockerfile
FROM amazonlinux:2023

# Use the same OpenCV major version as the local build (C:/opencv is OpenCV 5)
ARG OPENCV_REF=5.x

RUN dnf install -y gcc-c++ make cmake git tar gzip && dnf clean all

# A static, headless OpenCV with only what query needs. highgui is built
# without a GUI backend: display.cpp links against it, and gui=0 never opens a window.
RUN git clone --depth 1 --branch ${OPENCV_REF} https://github.com/opencv/opencv.git /opencv-src \
 && cmake -S /opencv-src -B /opencv-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/opencv \
      -DBUILD_SHARED_LIBS=OFF -DBUILD_LIST=core,imgproc,imgcodecs,highgui \
      -DBUILD_ZLIB=ON -DBUILD_JPEG=ON -DBUILD_PNG=ON \
      -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF -DWITH_V4L=OFF \
      -DWITH_TIFF=OFF -DWITH_WEBP=OFF -DWITH_OPENJPEG=OFF -DWITH_JASPER=OFF -DWITH_OPENEXR=OFF \
      -DWITH_IPP=OFF -DWITH_ITT=OFF -DWITH_OPENCL=OFF \
      -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF -DBUILD_DOCS=OFF \
 && cmake --build /opencv-build -j"$(nproc)" --target install \
 && rm -rf /opencv-src /opencv-build

WORKDIR /src
```

Build the image and compile `query` into `out/query`. It sits next to the Windows `.exe` without a name clash.

```
docker build -t cbir-linux -f deploy/Dockerfile.linux deploy
docker run --rm -v "$PWD:/src" cbir-linux bash -c '
  cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/opencv \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" &&
  cmake --build build-linux --target query -j"$(nproc)"'
```

`build-linux/` is already gitignored (`build-*/`). If the `5.x` branch doesn't build, set
`--build-arg OPENCV_REF=<tag>` to the exact version under `C:/opencv`.

**Check for step 3:** the binary depends only on system libraries and returns the Task 2 result.

```
docker run --rm -v "$PWD:/src" -w /src amazonlinux:2023 bash -c '
  ldd out/query;
  ./out/query csv=data/csv/histogram_16.csv target=data/olympus/pic.0164.jpg metric=intersection top=3 gui=0'
```

Expect `ldd` to list only `libc`, `libm`, `libpthread`/`libdl` and similar, and the ranking to be
0080, 1032, 0461.

## 4. Stage the runtime files

The function bundle can only include files under the base directory (`web/`), so copy what `query`
reads into `web/cbir/`. The images are copied into `public/` for the CDN as well. Run this from the
project root:

```
rm -rf web/cbir web/public/olympus
mkdir -p web/cbir/out web/cbir/data web/public
cp out/query web/cbir/out/query
cp -r data/csv data/resNet data/olympus web/cbir/data/
cp -r data/olympus web/public/olympus
```

`query` needs `data/olympus` inside the bundle as well, to read the target image of a database search. The
copy in `public/` only serves thumbnails.

Add the staged folders to `web/.gitignore`. They are build output, not source.

```
/cbir/
/public/olympus/
```

**Bundle size check:** about 71 MB of images, 18 MB of csv files, 5 MB of embeddings, the static `query`
binary and the Next.js server. That's well under the 250 MB unzipped limit. Check with
`du -sh web/cbir web/.next` after a build.

## 5. Configure Netlify

Create `web/netlify.toml`, with `web/` as the base directory:

```toml
[build]
  command = "npm run build"
  publish = ".next"

[build.environment]
  NODE_VERSION = "22"

[functions]
  # Bundled into the Next.js server function next to the route handlers
  included_files = [
    "cbir/out/query",
    "cbir/data/csv/**",
    "cbir/data/resNet/**",
    "cbir/data/olympus/**",
  ]
```

Netlify detects Next.js and applies its adapter automatically (Next.js 13.5+ is supported). Set these as
**site environment variables**, in the UI or with `netlify env:set`:

| Variable | Value | Why |
|---|---|---|
| `CBIR_ROOT` | `cbir` | A relative path, resolved against the function's working directory. Confirm it with `/api/health`. |
| `CBIR_READ_ONLY` | `1` | Step 2.1 |
| `CBIR_IMAGE_BASE` | `/olympus` | Step 2.3 |

## 6. Deploy with the Netlify CLI

Deploy from your machine, not from Git, because `data/olympus` isn't in the repository.

```
npm install -g netlify-cli
cd web
netlify login
netlify init                    # create the site; base directory = web, no Git-based builds needed
netlify env:set CBIR_ROOT cbir
netlify env:set CBIR_READ_ONLY 1
netlify env:set CBIR_IMAGE_BASE /olympus

netlify deploy --build          # draft URL first
netlify deploy --build --prod   # once the draft passes step 7
```

If the build has trouble with Windows paths, run the same commands from WSL. The output is Linux-bound
either way.

## 7. Verify the deployment

Run these against the draft URL before promoting it to production:

1. `GET /api/health`: `queryExe.exists` is true, there are 6 features, and `dbImages` is 1106.
2. Search page: texture_color + multi on `pic.0535` returns 0745, 0011, 0004.
3. ResNet18 + cosine on `pic.0893` returns 0897, 0136, 0146.
4. A one-off upload under 4 MB returns results. A 6 MB file gets a clear error, not a crash.
5. The Database page browses images, and they load from `/olympus/...` with no function calls. It shows no
   upload area. `POST /api/images` returns 403.
6. Netlify UI → **Logs → Functions**: a query runs in well under a second on a warm instance. The first
   request after a cold start is slower, because it copies the binary to `/tmp`.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `spawn … ENOENT` | The binary isn't in the bundle, or `CBIR_ROOT` is wrong. Compare `/api/health` with `included_files` and the staged paths. |
| `spawn … EACCES` | The exec bit was lost. Make sure the step 2.5 copy-and-chmod is in place. |
| `query` exits with `GLIBC_2.xx not found` | The binary was built on a newer distro than the runtime. Rebuild inside `amazonlinux:2023` (step 3). |
| `error while loading shared libraries: libopencv_*.so` | OpenCV wasn't linked statically. Check `-DBUILD_SHARED_LIBS=OFF`, and `ldd out/query`. |
| 413 or a 502 on a one-off upload | The file is over about 4.5 MB. Lower the client-side limit (step 2.4). |
| Deploy fails with a function size error | The bundle is over 250 MB. Make sure only the four `included_files` patterns are listed and `web/.next/cache` isn't included. |
| Search works but thumbnails are broken | `CBIR_IMAGE_BASE` isn't set, or `public/olympus` wasn't staged before the build. |

## If you need uploads in production

Uploads need a persistent disk and long-running work, which is exactly what serverless functions don't
provide. To keep the full feature set, run the app on a host that gives you one machine with a disk, such
as a VM or a container host with a volume (for example Fly.io or Render):

- Build both `query` and `extract` for Linux (step 3, with no `--target`).
- Mount `data/` on a volume and run `npm run build && npm run start`.
- Keep a **single** instance, because the re-index lock lives in memory (see [ARCHITECTURE.md](ARCHITECTURE.md#5-job-state-lives-in-process-memory)).
- Add authentication before exposing uploads publicly.
