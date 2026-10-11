// Shared by client and server. Mirrors the checks in query.cpp (prepareFromStore / prepareDnn) so
// the UI can disable invalid combos up front; query.exe still has the final say.

export type FeatureKind =
  | "baseline"
  | "histogram"
  | "multi_histogram"
  | "texture_color"
  | "orange_dnn"
  | "dnn";

export type Metric = "ssd" | "intersection" | "multi" | "cosine" | "custom";

export const METRICS_FOR: Record<FeatureKind, Metric[]> = {
  baseline: ["ssd", "cosine"],
  histogram: ["intersection", "ssd", "cosine"],
  multi_histogram: ["multi", "ssd", "cosine"],
  texture_color: ["multi", "ssd", "cosine"],
  orange_dnn: ["custom"],
  dnn: ["cosine", "ssd"],
};

export const METRIC_LABEL: Record<Metric, string> = {
  ssd: "Sum of squared differences",
  intersection: "Histogram intersection",
  multi: "Multi-piece intersection",
  cosine: "Cosine distance",
  custom: "Custom (0.7 cosine + 0.3 orange)",
};

export const FEATURE_LABEL: Record<FeatureKind, string> = {
  baseline: "Baseline (center patch)",
  histogram: "rg chromaticity histogram",
  multi_histogram: "Multi-histogram",
  texture_color: "Texture + color",
  orange_dnn: "Orange object + DNN",
  dnn: "ResNet18 embedding",
};

// query computes most targets from pixels, but these read the target's vector from the index (the
// embedding half comes from the provided ResNet csv, and there's no network to compute it), so the
// target must already have a row
export function targetFromIndex(kind: FeatureKind): boolean {
  return kind === "dnn" || kind === "orange_dnn";
}

// One availability rule for every image, original or uploaded: a feature is usable unless it
// needs the target's own row and the image doesn't have one yet
export function isUsable(feature: FeatureInfo, target: { missing: string[] } | null): boolean {
  if (!feature.targetFromIndex) return true;
  return target !== null && !target.missing.includes(feature.id);
}

export function isMetric(x: unknown): x is Metric {
  return typeof x === "string" && x in METRIC_LABEL;
}

// API contracts
export interface FeatureInfo {
  id: string; // csv basename, or "dnn"
  kind: FeatureKind;
  label: string;
  detail: string; // e.g. "bins=16"
  rows: number;
  metrics: Metric[];
  targetFromIndex: boolean;
}

export interface DbImage {
  name: string;
  url: string;
  // Feature ids with no row for this image, read from the csvs themselves so it can't disagree
  // with what query searches. Usually empty; a fresh upload lists everything not yet extracted.
  missing: string[];
  uploaded: boolean;
}

// "upload": a one-off image sent in the same multipart request
export type QueryTarget = { kind: "db"; id: string } | { kind: "upload" };

export interface QueryRequest {
  featureId: string;
  metric: Metric;
  target: QueryTarget;
  top: number;
  worst: number;
}

export interface Match {
  rank: number; // negative = least similar
  distance: number;
  name: string;
  url: string;
}

export interface QueryResponse {
  matches: Match[];
  worst: Match[];
  elapsedMs: number;
  command: string;
  // Database images this feature has no row for, so they can't appear in the ranking
  unindexed: number;
}

export interface ReindexStatus {
  running: boolean;
  pending: boolean;
  current: string | null;
  progress: { done: number; total: number } | null;
  completed: string[];
  error: string | null;
  finishedAt: number | null;
}
