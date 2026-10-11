import "server-only";
import fs from "node:fs/promises";
import path from "node:path";
import { CSV_DIR, RESNET_CSV } from "./paths";
import { FEATURE_LABEL, METRICS_FOR, targetFromIndex, type FeatureInfo, type FeatureKind } from "./compat";

export interface FeatureMeta {
  id: string;
  kind: FeatureKind;
  bins?: number;
  patch?: number;
  rows: number;
  vectorLength: number;
}

// .meta is the key=value file feature_store.cpp writes next to each csv
function parseMeta(id: string, text: string): FeatureMeta | null {
  const kv = new Map<string, string>();
  for (const line of text.split(/\r?\n/)) {
    const eq = line.indexOf("=");
    if (eq > 0) kv.set(line.slice(0, eq).trim(), line.slice(eq + 1).trim());
  }
  const kind = kv.get("feature") as FeatureKind | undefined;
  if (!kind || !(kind in METRICS_FOR) || kind === "dnn") return null;
  const num = (k: string) => (kv.has(k) ? Number(kv.get(k)) : undefined);
  return {
    id,
    kind,
    bins: num("bins"),
    patch: num("patch"),
    rows: num("rows") ?? 0,
    vectorLength: num("vector_length") ?? 0,
  };
}

export async function listMetas(): Promise<FeatureMeta[]> {
  let files: string[] = [];
  try {
    files = await fs.readdir(CSV_DIR);
  } catch {
    return [];
  }
  const metas = await Promise.all(
    files
      .filter((f) => f.endsWith(".meta"))
      .map(async (f) => {
        const id = path.basename(f, ".meta");
        const text = await fs.readFile(path.join(CSV_DIR, f), "utf8");
        return parseMeta(id, text);
      }),
  );
  return metas.filter((m): m is FeatureMeta => m !== null).sort((a, b) => a.id.localeCompare(b.id));
}

function detailOf(m: FeatureMeta): string {
  switch (m.kind) {
    case "baseline":
      return `${m.patch}x${m.patch} patch`;
    case "histogram":
    case "texture_color":
      return `bins=${m.bins}`;
    case "multi_histogram":
      return `bins=${m.bins}, center=${m.patch}`;
    default:
      return `${m.vectorLength}-d`;
  }
}

export async function listFeatures(): Promise<FeatureInfo[]> {
  const metas = await listMetas();
  const features: FeatureInfo[] = metas.map((m) => ({
    id: m.id,
    kind: m.kind,
    label: FEATURE_LABEL[m.kind],
    detail: detailOf(m),
    rows: m.rows,
    metrics: METRICS_FOR[m.kind],
    targetFromIndex: targetFromIndex(m.kind),
  }));
  const names = await embeddedNames();
  if (names.size > 0) {
    features.push({
      id: "dnn",
      kind: "dnn",
      label: FEATURE_LABEL.dnn,
      detail: "512-d, provided",
      rows: names.size,
      metrics: METRICS_FOR.dnn,
      targetFromIndex: true,
    });
  }
  return features;
}

// Image names with a row in a csv: the first column, reduced to the basename. The csvs are the
// source of truth for "is this image indexed", so read them rather than tracking status on the
// side. Cached per file and invalidated by mtime/size, since extract replaces files atomically.
interface CachedNames {
  stamp: string;
  names: Promise<Set<string>>;
}
const g = globalThis as unknown as { __cbirNames?: Map<string, CachedNames> };
const nameCache = (g.__cbirNames ??= new Map());

async function namesInCsv(csvPath: string): Promise<Set<string>> {
  let stamp: string;
  try {
    const st = await fs.stat(csvPath);
    stamp = `${st.mtimeMs}:${st.size}`;
  } catch {
    return new Set();
  }
  const hit = nameCache.get(csvPath);
  if (hit && hit.stamp === stamp) return hit.names;

  const names = fs
    .readFile(csvPath, "utf8")
    .then((text) => {
      const set = new Set<string>();
      for (const line of text.split(/\r?\n/)) {
        const comma = line.indexOf(",");
        if (comma > 0) set.add(path.basename(line.slice(0, comma)));
      }
      return set;
    })
    .catch(() => new Set<string>());
  nameCache.set(csvPath, { stamp, names });
  return names;
}

export function embeddedNames(): Promise<Set<string>> {
  return namesInCsv(RESNET_CSV);
}

// Which images a feature can rank (and use as a target, for targetFromIndex features)
export function indexedNames(featureId: string): Promise<Set<string>> {
  return featureId === "dnn" ? embeddedNames() : namesInCsv(path.join(CSV_DIR, `${featureId}.csv`));
}
