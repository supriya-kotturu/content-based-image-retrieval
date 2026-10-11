import fs from "node:fs/promises";
import { listDbImages } from "@/lib/images";
import { listFeatures } from "@/lib/meta";
import { CBIR_ROOT, QUERY_EXE, READ_ONLY } from "@/lib/paths";

// What the server actually sees, so a deployment's path or bundling problem shows up here instead
// of as a vague spawn error
export async function GET() {
  const exists = await fs
    .access(QUERY_EXE)
    .then(() => true)
    .catch(() => false);
  const [features, images] = await Promise.all([
    listFeatures().catch(() => []),
    listDbImages().catch(() => []),
  ]);
  return Response.json({
    cwd: process.cwd(),
    cbirRoot: CBIR_ROOT,
    queryExe: { path: QUERY_EXE, exists },
    features: features.length,
    dbImages: images.length,
    readOnly: READ_ONLY,
    platform: `${process.platform}-${process.arch}`,
  });
}
