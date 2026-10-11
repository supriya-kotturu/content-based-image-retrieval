import path from "node:path";
import { CbirError, runQuery } from "@/lib/cbir";
import { isMetric, type QueryRequest, type QueryResponse } from "@/lib/compat";
import { dbImagePath, tmpImagePath } from "@/lib/images";
import { isReindexing } from "@/lib/jobs";
import { embeddedNames, listFeatures } from "@/lib/meta";

const bad = (error: string, status = 400) => Response.json({ error }, { status });

export async function POST(req: Request) {
  if (isReindexing()) {
    return bad("The database is being re-indexed; try again when it finishes.", 409);
  }

  let body: Partial<QueryRequest>;
  try {
    body = await req.json();
  } catch {
    return bad("invalid JSON");
  }
  const { featureId, metric, target } = body;
  const top = Number(body.top ?? 10);
  const worst = Number(body.worst ?? 0);

  // featureId ends up in a file path, so only accept ids we listed ourselves
  const feature = (await listFeatures()).find((f) => f.id === featureId);
  if (!feature) return bad(`unknown feature: ${featureId}`);
  if (!isMetric(metric) || !feature.metrics.includes(metric)) {
    return bad(`metric ${metric} is not valid for ${feature.label}`);
  }
  if (!Number.isInteger(top) || top < 1 || top > 100) return bad("top must be 1-100");
  if (!Number.isInteger(worst) || worst < 0 || worst > 50) return bad("worst must be 0-50");

  let targetPath: string | null = null;
  if (target?.kind === "db") targetPath = await dbImagePath(target.id);
  else if (target?.kind === "temp") targetPath = await tmpImagePath(target.id);
  if (!targetPath) return bad("target image not found");

  if (feature.needsEmbedding) {
    const embedded = await embeddedNames();
    if (target?.kind === "temp" || !embedded.has(path.basename(targetPath))) {
      return bad(`${feature.label} only works on original database images: it needs their provided ResNet embedding`);
    }
  }

  const started = performance.now();
  try {
    const result = await runQuery({ featureId: feature.id, metric, targetPath, top, worst });
    const res: QueryResponse = { ...result, elapsedMs: Math.round(performance.now() - started) };
    return Response.json(res);
  } catch (err) {
    if (err instanceof CbirError) return bad(err.message, 422);
    throw err;
  }
}
