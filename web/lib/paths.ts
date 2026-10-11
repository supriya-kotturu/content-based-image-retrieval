import "server-only";
import path from "node:path";

// The C++ project root: the exes live in out/, and the csv files store paths relative to data/,
// so every subprocess runs with this as cwd. A relative CBIR_ROOT resolves against the server's
// cwd (on Netlify the staged copy lives at <function cwd>/cbir).
export const CBIR_ROOT = path.resolve(process.env.CBIR_ROOT ?? path.join(process.cwd(), ".."));
export const DATA_DIR = path.join(CBIR_ROOT, "data");
export const DB_DIR = path.join(DATA_DIR, "olympus");
export const CSV_DIR = path.join(DATA_DIR, "csv");
export const RESNET_CSV = path.join(DATA_DIR, "resNet", "ResNet18_olym.csv");
export const QUERY_EXE = path.join(CBIR_ROOT, "out", process.platform === "win32" ? "query.exe" : "query");
export const EXTRACT_EXE = path.join(CBIR_ROOT, "out", process.platform === "win32" ? "extract.exe" : "extract");

// Serverless hosts have a read-only, per-instance file system: uploads and re-indexing can't work
// there, so they're switched off rather than failing halfway
export const READ_ONLY = process.env.CBIR_READ_ONLY === "1";

// When set (e.g. "/olympus"), database images are served as static files from the CDN instead of
// one function call per thumbnail
const IMAGE_BASE = process.env.CBIR_IMAGE_BASE?.replace(/\/$/, "");

// Same extensions extract's isImageFile accepts, so anything we save gets indexed
export const IMAGE_EXTS = new Set([".jpg", ".jpeg", ".png"]);

// Resolves a data-relative path, or null if it escapes data/ (?p=../CMakeLists.txt)
export function safeDataPath(rel: string): string | null {
  const abs = path.resolve(DATA_DIR, rel);
  return abs.startsWith(DATA_DIR + path.sep) ? abs : null;
}

export function imageUrl(dataRelative: string): string {
  if (IMAGE_BASE && dataRelative.startsWith("olympus/")) {
    return `${IMAGE_BASE}/${encodeURIComponent(dataRelative.slice("olympus/".length))}`;
  }
  return `/api/image?p=${encodeURIComponent(dataRelative)}`;
}
