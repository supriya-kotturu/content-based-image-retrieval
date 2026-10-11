import "server-only";
import fs from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { DB_DIR, IMAGE_EXTS, TMP_DIR, imageUrl } from "./paths";
import { embeddedNames } from "./meta";
import type { DbImage } from "./compat";

export const MAX_UPLOAD_BYTES = 10 * 1024 * 1024;
export const UPLOAD_PREFIX = "upload_";
const TMP_TTL_MS = 24 * 60 * 60 * 1000;

// Names are used in subprocess args and file paths, so only allow the plain form extract produces
const SAFE_NAME = /^[\w.-]+$/;
const TMP_ID = /^[0-9a-f-]{36}\.(jpg|jpeg|png)$/;

export async function listDbImages(): Promise<DbImage[]> {
  const [files, embedded] = await Promise.all([fs.readdir(DB_DIR), embeddedNames()]);
  return files
    .filter((f) => IMAGE_EXTS.has(path.extname(f).toLowerCase()))
    .sort()
    .map((name) => ({
      name,
      url: imageUrl(`olympus/${name}`),
      hasEmbedding: embedded.has(name),
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

export async function tmpImagePath(id: string): Promise<string | null> {
  if (!TMP_ID.test(id)) return null;
  const abs = path.join(TMP_DIR, id);
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
  if (file.size > MAX_UPLOAD_BYTES) throw new UploadError(`${file.name}: larger than 10 MB`);
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

export async function saveTemp(file: File): Promise<string> {
  const { ext, buf } = await readChecked(file);
  await fs.mkdir(TMP_DIR, { recursive: true });
  void cleanTemp();
  const id = `${randomUUID()}${ext}`;
  await fs.writeFile(path.join(TMP_DIR, id), buf);
  return id;
}

async function cleanTemp() {
  try {
    const now = Date.now();
    for (const f of await fs.readdir(TMP_DIR)) {
      const abs = path.join(TMP_DIR, f);
      const { mtimeMs } = await fs.stat(abs);
      if (now - mtimeMs > TMP_TTL_MS) await fs.rm(abs, { force: true });
    }
  } catch {
    // best effort
  }
}
