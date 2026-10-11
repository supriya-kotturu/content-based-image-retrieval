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

// Two lines each: how it works, then what that means for matching. Kept in step with
// src/features.cpp and src/metrics.cpp; the UI shows them next to the dropdowns.
export const FEATURE_INFO: Record<FeatureKind, [string, string]> = {
  baseline: [
    "The raw RGB values of the small square at the image center (7×7 → 147 numbers).",
    "Very fast, but only sees the middle of the picture: a shifted subject is a different image.",
  ],
  histogram: [
    "Each pixel becomes rg chromaticity (r = R/(R+G+B), g = G/(R+G+B)), counted into a 2-D grid of bins.",
    "Captures the overall color mix and ignores brightness and where colors are.",
  ],
  multi_histogram: [
    "Three histograms side by side: rg of the top half, bg of the bottom half, and RGB of a center square.",
    "Adds coarse layout to color, e.g. blue sky above green ground, or an object in the middle.",
  ],
  texture_color: [
    "A whole-image rg histogram plus a histogram of Sobel edge strengths (square-root binned).",
    "Matches images with similar colors and a similar amount of edges: smooth walls vs busy foliage.",
  ],
  orange_dnn: [
    "The ResNet18 embedding plus a hue × saturation histogram of only strongly orange pixels.",
    "Finds the same kind of scene that also contains a similarly shaded orange object (Task 7).",
  ],
  dnn: [
    "512 numbers per image from a ResNet18 network trained on ImageNet, provided with the assignment.",
    "Encodes what is in the picture (objects, scene type) rather than raw colors or layout.",
  ],
};

export const METRIC_INFO: Record<Metric, [string, string]> = {
  ssd: [
    "Σ (aᵢ − bᵢ)² over every value; 0 means identical.",
    "Punishes big differences heavily and depends on scale, so it suits raw pixel values best.",
  ],
  intersection: [
    "1 − Σ min(aᵢ, bᵢ) over the bins: the share of color mass the two images don't have in common.",
    "0 = identical histograms, 1 = no overlap at all.",
  ],
  multi: [
    "1 − intersection for each piece of the vector separately, then the plain average of those.",
    "Every piece counts equally, so a piece with more bins can't drown out the others.",
  ],
  cosine: [
    "1 − cos θ between the two vectors: compares their direction, not their length.",
    "The usual choice for embeddings, where overall magnitude carries little meaning.",
  ],
  custom: [
    "0.7 × cosine distance of the embeddings + 0.3 × (1 − intersection) of the orange histograms.",
    "Mostly 'same kind of scene', partly 'same shade of orange'; the weights were tuned on the mask photos.",
  ],
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
