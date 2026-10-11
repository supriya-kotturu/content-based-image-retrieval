// Browser-side fetch helpers: one place that turns API error bodies into thrown messages
import type { DbImage, FeatureInfo } from "./compat";

export class ApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
  ) {
    super(message);
  }
}

export async function api<T>(input: string, init?: RequestInit): Promise<T> {
  const res = await fetch(input, init);
  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    const msg = body.error ?? (Array.isArray(body.errors) ? body.errors.join("; ") : res.statusText);
    throw new ApiError(msg, res.status);
  }
  return body as T;
}

export async function fetchCatalog() {
  const [cfg, imgs] = await Promise.all([
    api<{ features: FeatureInfo[] }>("/api/config"),
    api<{ images: DbImage[] }>("/api/images"),
  ]);
  return { features: cfg.features, images: imgs.images };
}

export function postJson<T>(url: string, data: unknown): Promise<T> {
  return api<T>(url, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(data) });
}

export function postFiles<T>(url: string, field: string, files: File[]): Promise<T> {
  const form = new FormData();
  for (const f of files) form.append(field, f);
  return api<T>(url, { method: "POST", body: form });
}
