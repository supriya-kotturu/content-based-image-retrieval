import fs from "node:fs/promises";
import path from "node:path";
import type { NextRequest } from "next/server";
import { IMAGE_EXTS, safeDataPath } from "@/lib/paths";

const TYPES: Record<string, string> = { ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png" };

export async function GET(req: NextRequest) {
  const rel = req.nextUrl.searchParams.get("p") ?? "";
  const abs = safeDataPath(rel);
  const ext = path.extname(rel).toLowerCase();
  // Only images under data/: blocks traversal and reading csvs through this route
  if (!abs || !IMAGE_EXTS.has(ext)) {
    return Response.json({ error: "invalid path" }, { status: 400 });
  }
  try {
    const buf = await fs.readFile(abs);
    return new Response(new Uint8Array(buf), {
      headers: {
        "Content-Type": TYPES[ext],
        // Uploads get unique names and DB images never change, so a long cache is safe
        "Cache-Control": "public, max-age=86400, immutable",
      },
    });
  } catch {
    return Response.json({ error: "not found" }, { status: 404 });
  }
}
