import "server-only";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { DB_DIR, IMAGE_EXTS, READ_ONLY, imageUrl } from "./paths";
import { indexedNames, listFeatures } from "./meta";
import type { DbImage } from "./compat";

// Netlify buffers request bodies up to 6 MB and base64-encodes binary ones (~4.5 MB of image), so
// stay under that there and report it ourselves instead of hitting the platform's 413
export const MAX_UPLOAD_BYTES = (READ_ONLY ? 4 : 10) * 1024 * 1024;
export const UPLOAD_PREFIX = "upload_";

// Names are used in subprocess args and file paths, so only allow the plain form extract produces
const SAFE_NAME = /^[\w.-]+$/;

export async function listDbImages(): Promise<DbImage[]> {
  const [files, features] = await Promise.all([fs.readdir(DB_DIR), listFeatures()]);
  const indexed = await Promise.all(features.map(async (f) => [f.id, await indexedNames(f.id)] as const));
  return files
    .filter((f) => IMAGE_EXTS.has(path.extname(f).toLowerCase()))
    .sort()
    .map((name) => ({
      name,
      url: imageUrl(`olympus/${name}`),
      missing: indexed.filter(([, names]) => !names.has(name)).map(([id]) => id),
      uploaded: name.startsWith(UPLOAD_PREFIX),
    }));
}

export async function dbImagePath(name: string): Promise<string | null> {
  if (!SAFE_NAME.test(name)) return null;
  const abs = path.join(DB_DIR, name);
  try {
    await fs.access(abs);
    return abs;
  } catch {
    return null;
  }
}

export class UploadError extends Error {}

function checkFile(file: File): string {
  const ext = path.extname(file.name).toLowerCase();
  if (!IMAGE_EXTS.has(ext)) throw new UploadError(`${file.name}: only .jpg, .jpeg and .png are supported`);
  if (file.size > MAX_UPLOAD_BYTES) {
    throw new UploadError(`${file.name}: larger than ${MAX_UPLOAD_BYTES / 1024 / 1024} MB`);
  }
  return ext;
}

// Magic bytes, so a renamed non-image doesn't reach imread and get silently skipped by extract
function looksLikeImage(buf: Buffer): boolean {
  const jpeg = buf[0] === 0xff && buf[1] === 0xd8 && buf[2] === 0xff;
  const png = buf.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]));
  return jpeg || png;
}

async function readChecked(file: File): Promise<{ ext: string; buf: Buffer }> {
  const ext = checkFile(file);
  const buf = Buffer.from(await file.arrayBuffer());
  if (!looksLikeImage(buf)) throw new UploadError(`${file.name}: not a valid JPEG or PNG`);
  return { ext, buf };
}

export async function saveToDb(file: File): Promise<string> {
  const { ext, buf } = await readChecked(file);
  const slug =
    path
      .basename(file.name, path.extname(file.name))
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-|-$/g, "")
      .slice(0, 40) || "image";
  const name = `${UPLOAD_PREFIX}${Date.now()}_${slug}${ext}`;
  await fs.writeFile(path.join(DB_DIR, name), buf, { flag: "wx" });
  return name;
}

// A one-off target lives only for the request that searches with it. Uploading it in a separate
// request would break on serverless hosts, where the next request may land on another instance
// with a different /tmp.
export async function withTempImage<T>(file: File, fn: (absPath: string) => Promise<T>): Promise<T> {
  const { ext, buf } = await readChecked(file);
  const abs = path.join(os.tmpdir(), `cbir-${randomUUID()}${ext}`);
  await fs.writeFile(abs, buf);
  try {
    return await fn(abs);
  } finally {
    await fs.rm(abs, { force: true });
  }
}
