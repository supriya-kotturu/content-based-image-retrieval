import path from "node:path";
import { CbirError, runQuery } from "@/lib/cbir";
import { isMetric, type QueryRequest, type QueryResponse } from "@/lib/compat";
import { UploadError, dbImagePath, listDbImages, withTempImage } from "@/lib/images";
import { indexedNames, listFeatures } from "@/lib/meta";

const bad = (error: string, status = 400) => Response.json({ error }, { status });

// Two shapes:
//   JSON       { featureId, metric, target: { kind: "db", id }, top, worst }
//   multipart  file=<image>, request=<the same JSON with target { kind: "upload" }>
// The one-off image travels with the search itself, so nothing has to survive between requests.
// No lock against a running re-index: extract swaps csv files in atomically.
export async function POST(req: Request) {
  let body: Partial<QueryRequest>;
  let file: File | null = null;
  try {
    if (req.headers.get("content-type")?.startsWith("multipart/form-data")) {
      const form = await req.formData();
      const f = form.get("file");
      file = f instanceof File ? f : null;
      body = JSON.parse(String(form.get("request") ?? "{}"));
    } else {
      body = await req.json();
    }
  } catch {
    return bad("invalid request body");
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

  const [indexed, images] = await Promise.all([indexedNames(feature.id), listDbImages()]);
  const fromIndexError = () =>
    bad(
      `${feature.label} reads the target's vector from the index, and this image has no row in it ` +
        `(only the original database images have a ResNet embedding)`,
    );

  const search = async (targetPath: string, targetLabel?: string) => {
    const started = performance.now();
    const result = await runQuery({ featureId: feature.id, metric, targetPath, targetLabel, top, worst });
    const res: QueryResponse = {
      ...result,
      elapsedMs: Math.round(performance.now() - started),
      unindexed: images.filter((i) => !indexed.has(i.name)).length,
    };
    return Response.json(res);
  };

  try {
    if (target?.kind === "upload") {
      if (!file) return bad("no image in the request");
      if (feature.targetFromIndex) return fromIndexError();
      return await withTempImage(file, (abs) => search(abs, `<path to ${file.name.replace(/\s+/g, "_")}>`));
    }
    if (target?.kind !== "db") return bad("target must be a database image or an uploaded file");
    const targetPath = await dbImagePath(target.id);
    if (!targetPath) return bad("target image not found");
    if (feature.targetFromIndex && !indexed.has(path.basename(targetPath))) return fromIndexError();
    return await search(targetPath);
  } catch (err) {
    if (err instanceof UploadError) return bad(err.message);
    if (err instanceof CbirError) return bad(err.message, 422);
    throw err;
  }
}
