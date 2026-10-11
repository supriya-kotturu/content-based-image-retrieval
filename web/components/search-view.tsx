"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import { useSearchParams } from "next/navigation";
import { toast } from "sonner";
import { DatabaseIcon, ImageIcon, InfoIcon, Loader2Icon, SparklesIcon, TerminalIcon, XIcon } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Label } from "@/components/ui/label";
import { Slider } from "@/components/ui/slider";
import { Skeleton } from "@/components/ui/skeleton";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";
import { DbImagePicker } from "@/components/db-image-picker";
import { UploadDropzone } from "@/components/upload-dropzone";
import { ResultGrid } from "@/components/result-grid";
import { ReindexBanner } from "@/components/reindex-banner";
import { useReindex } from "@/hooks/use-reindex";
import { api, fetchCatalog, postJson } from "@/lib/api";
import { METRIC_LABEL, isUsable, type DbImage, type FeatureInfo, type Match, type Metric, type QueryResponse } from "@/lib/compat";

// Only which image is selected; what it's indexed in is derived from the latest image list, so it
// updates by itself when a re-index finishes
type Target = { kind: "db"; id: string; url: string } | { kind: "upload"; file: File; url: string; fileName: string };

function sliderValue(v: number | readonly number[]): number {
  return Array.isArray(v) ? v[0] : (v as number);
}

export function SearchView() {
  const params = useSearchParams();
  const [features, setFeatures] = useState<FeatureInfo[]>([]);
  const [images, setImages] = useState<DbImage[]>([]);
  const [target, setTarget] = useState<Target | null>(null);
  const [featureId, setFeatureId] = useState<string | null>(null);
  const [chosenMetric, setMetric] = useState<Metric | null>(null);
  const [top, setTop] = useState(10);
  const [worst, setWorst] = useState(0);
  const [tab, setTab] = useState<string>("db");
  const [maxUploadBytes, setMaxUploadBytes] = useState(0);
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<QueryResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  const { status } = useReindex(() => {
    fetchCatalog()
      .then((c) => {
        setFeatures(c.features);
        setMaxUploadBytes(c.maxUploadBytes);
        setImages(c.images);
      })
      .catch(() => {});
    toast.success("Re-index finished: new images are now searchable");
  });

  // Initial load, honoring ?target=pic.0535.jpg (links from the Database page)
  const initialTarget = params.get("target") ?? "pic.0535.jpg";
  useEffect(() => {
    fetchCatalog()
      .then((c) => {
        setFeatures(c.features);
        setMaxUploadBytes(c.maxUploadBytes);
        setImages(c.images);
        const img = c.images.find((i) => i.name === initialTarget);
        if (img) setTarget({ kind: "db", id: img.name, url: img.url });
      })
      .catch((e) => toast.error(`Could not load: ${e.message}`));
  }, [initialTarget]);

  // A one-off upload has no row anywhere; a database image has rows wherever the csvs say so
  const targetMissing = useMemo(() => {
    if (!target) return null;
    if (target.kind === "upload") return features.map((f) => f.id);
    return images.find((i) => i.name === target.id)?.missing ?? [];
  }, [target, images, features]);

  const usable = useCallback(
    (f: FeatureInfo) => isUsable(f, targetMissing ? { missing: targetMissing } : null),
    [targetMissing],
  );

  // Derived, not synced: the user's choice wins while it's valid for the current target, otherwise
  // fall back (e.g. a DNN feature was picked and then an upload became the target)
  const chosen = features.find((f) => f.id === featureId);
  const feature =
    (chosen && usable(chosen) ? chosen : undefined) ??
    features.find((f) => f.id === "texture_color_16" && usable(f)) ??
    features.find(usable) ??
    null;
  const metric: Metric | null =
    feature && chosenMetric && feature.metrics.includes(chosenMetric) ? chosenMetric : (feature?.metrics[0] ?? null);

  const featureItems = useMemo(
    () => features.map((f) => ({ value: f.id, label: `${f.label} · ${f.detail}` })),
    [features],
  );
  const metricItems = useMemo(
    () => (feature?.metrics ?? []).map((m) => ({ value: m, label: METRIC_LABEL[m] })),
    [feature],
  );

  const run = async (override?: Target) => {
    const t = override ?? target;
    // An override target may rule out the current feature (no embedding); the server rejects that
    // with a clear message, so no need to re-derive here
    if (!t || !feature || !metric) return;
    setRunning(true);
    setError(null);
    try {
      const common = { featureId: feature.id, metric, top, worst };
      let res: QueryResponse;
      if (t.kind === "upload") {
        // The image rides along with the search, so the server never has to keep it
        const form = new FormData();
        form.append("file", t.file);
        form.append("request", JSON.stringify({ ...common, target: { kind: "upload" } }));
        res = await api<QueryResponse>("/api/query", { method: "POST", body: form });
      } else {
        res = await postJson<QueryResponse>("/api/query", { ...common, target: { kind: "db", id: t.id } });
      }
      setResult(res);
    } catch (e) {
      const msg = (e as Error).message;
      setError(msg);
      toast.error(msg);
    } finally {
      setRunning(false);
    }
  };

  // Nothing is uploaded yet: the file is kept in the browser and sent with each search
  const uploadTarget = (files: File[]) => {
    const file = files[0];
    if (!/\.(jpe?g|png)$/i.test(file.name)) {
      toast.error(`${file.name}: only .jpg, .jpeg and .png are supported`);
      return;
    }
    if (maxUploadBytes && file.size > maxUploadBytes) {
      toast.error(`${file.name}: larger than ${Math.round(maxUploadBytes / 1024 / 1024)} MB`);
      return;
    }
    setTarget({ kind: "upload", file, url: URL.createObjectURL(file), fileName: file.name });
    setResult(null);
  };

  // Free the previous preview's object URL when the target changes
  useEffect(() => {
    const url = target?.kind === "upload" ? target.url : null;
    return () => {
      if (url) URL.revokeObjectURL(url);
    };
  }, [target]);

  const useMatchAsTarget = (m: Match) => {
    const img = images.find((i) => i.name === m.name);
    if (!img) return;
    const t: Target = { kind: "db", id: img.name, url: img.url };
    setTarget(t);
    setTab("db");
    void run(t);
    window.scrollTo({ top: 0, behavior: "smooth" });
  };

  const maxDistance = useMemo(() => {
    if (!result) return 0;
    return Math.max(...result.matches.map((m) => m.distance), ...result.worst.map((m) => m.distance), 0);
  }, [result]);

  const blockedFeatures = features.filter((f) => !usable(f));
  const pendingIndexes = target?.kind === "db" ? (targetMissing ?? []).filter((id) => {
    const f = features.find((x) => x.id === id);
    return f && !f.targetFromIndex;
  }) : [];

  return (
    <div className="space-y-6">
      <div className="flex flex-col gap-1">
        <h1 className="text-3xl font-semibold sm:text-4xl">Find similar images</h1>
        <p className="max-w-2xl text-muted-foreground">
          Pick a target, choose how images are described and compared, and rank all {images.length.toLocaleString()} database
          images by distance. Every ranking is computed by the C++ <span className="font-mono text-xs">query</span> program.
        </p>
      </div>

      <ReindexBanner status={status} />

      <div className="grid gap-6 lg:grid-cols-[400px_1fr]">
        {/* Controls */}
        <aside className="space-y-4 lg:sticky lg:top-20 lg:self-start">
          <section className="card-surface space-y-3 p-4">
            <h2 className="panel-title">1 · Target</h2>
            <Tabs value={tab} onValueChange={(v) => setTab(String(v))}>
              <TabsList className="w-full">
                <TabsTrigger value="db">
                  <DatabaseIcon /> From database
                </TabsTrigger>
                <TabsTrigger value="upload">
                  <ImageIcon /> Upload
                </TabsTrigger>
              </TabsList>
              <TabsContent value="db" className="pt-2">
                <DbImagePicker
                  images={images}
                  selected={target?.kind === "db" ? target.id : null}
                  onSelect={(img) => {
                    setTarget({ kind: "db", id: img.name, url: img.url });
                    setResult(null);
                  }}
                />
              </TabsContent>
              <TabsContent value="upload" className="pt-2">
                <UploadDropzone
                  onFiles={uploadTarget}
                  title="Drop a query image"
                  hint={`JPG or PNG, up to ${Math.round(maxUploadBytes / 1024 / 1024) || 10} MB. Sent with each search, never added to the database.`}
                />
              </TabsContent>
            </Tabs>
          </section>

          <section className="card-surface space-y-4 p-4">
            <h2 className="panel-title">2 · Feature & metric</h2>
            <div className="space-y-1.5">
              <Label>Feature</Label>
              <Select
                items={featureItems}
                value={feature?.id ?? null}
                onValueChange={(v) => {
                  const f = features.find((x) => x.id === v);
                  setFeatureId(f?.id ?? null);
                  setMetric(f?.metrics[0] ?? null);
                }}
              >
                <SelectTrigger className="w-full">
                  <SelectValue placeholder="Choose a feature" />
                </SelectTrigger>
                <SelectContent>
                  {features.map((f) => (
                    <SelectItem key={f.id} value={f.id} disabled={!usable(f)}>
                      <span className="flex flex-col">
                        <span>{f.label}</span>
                        <span className="text-xs text-muted-foreground">
                          {f.detail} · {f.rows.toLocaleString()} rows
                        </span>
                      </span>
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
              {blockedFeatures.length > 0 && (
                <p className="flex items-start gap-1.5 text-xs text-muted-foreground">
                  <InfoIcon className="mt-0.5 size-3.5 shrink-0" />
                  DNN features need a provided ResNet embedding, so they only work on original database images.
                </p>
              )}
            </div>

            <div className="space-y-1.5">
              <Label>Distance metric</Label>
              <Select items={metricItems} value={metric} onValueChange={(v) => setMetric(v as Metric)}>
                <SelectTrigger className="w-full">
                  <SelectValue placeholder="Choose a metric" />
                </SelectTrigger>
                <SelectContent>
                  {metricItems.map((m) => (
                    <SelectItem key={m.value} value={m.value}>
                      {m.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
          </section>

          <section className="card-surface space-y-4 p-4">
            <h2 className="panel-title">3 · How many</h2>
            <div className="space-y-2.5">
              <div className="flex justify-between text-sm">
                <Label>Top matches</Label>
                <span className="font-semibold tabular-nums">{top}</span>
              </div>
              <Slider value={[top]} min={1} max={40} onValueChange={(v) => setTop(sliderValue(v))} />
            </div>
            <div className="space-y-2.5">
              <div className="flex justify-between text-sm">
                <Label>Least similar</Label>
                <span className="font-semibold tabular-nums">{worst}</span>
              </div>
              <Slider value={[worst]} min={0} max={12} onValueChange={(v) => setWorst(sliderValue(v))} />
            </div>
            <Button
              size="lg"
              className="w-full bg-brand text-brand-foreground hover:bg-brand/90"
              disabled={!target || !feature || !metric || running}
              onClick={() => run()}
            >
              {running ? <Loader2Icon className="animate-spin" /> : <SparklesIcon />}
              {running ? "Ranking…" : "Find similar images"}
            </Button>
          </section>
        </aside>

        {/* Target + results */}
        <section className="min-w-0 space-y-6">
          <div className="card-surface flex flex-col gap-4 p-4 sm:flex-row">
            <div className="thumb w-full shrink-0 sm:w-64">
              {target ? (
                // eslint-disable-next-line @next/next/no-img-element -- served by our own route
                <img src={target.url} alt="target" />
              ) : (
                <Skeleton className="size-full" />
              )}
            </div>
            <div className="flex min-w-0 flex-1 flex-col gap-2">
              <span className="panel-title">Target</span>
              <div className="flex items-center gap-2">
                <span className="truncate font-mono text-sm">
                  {target ? (target.kind === "db" ? target.id : target.fileName) : "—"}
                </span>
                {target?.kind === "upload" && (
                  <>
                    <Badge variant="secondary">one-off upload</Badge>
                    <Tooltip>
                      <TooltipTrigger
                        render={
                          <Button variant="ghost" size="icon-xs" onClick={() => setTarget(null)} aria-label="Clear target" />
                        }
                      >
                        <XIcon />
                      </TooltipTrigger>
                      <TooltipContent>Clear</TooltipContent>
                    </Tooltip>
                  </>
                )}
                {target?.kind === "db" && targetMissing?.includes("dnn") && (
                  <Badge variant="outline">no embedding</Badge>
                )}
                {pendingIndexes.length > 0 && (
                  <Badge variant="outline" className="border-brand/50 text-accent-foreground">
                    indexing {pendingIndexes.length} feature{pendingIndexes.length === 1 ? "" : "s"}…
                  </Badge>
                )}
              </div>
              {feature && metric && (
                <p className="text-sm text-muted-foreground">
                  {feature.label} <span className="text-xs">({feature.detail})</span> compared with{" "}
                  <span className="text-foreground">{METRIC_LABEL[metric].toLowerCase()}</span>.
                </p>
              )}
              {result && (
                <div className="mt-auto space-y-1.5">
                  <div className="flex items-center gap-1.5 text-xs text-muted-foreground">
                    <TerminalIcon className="size-3.5" /> ran in {result.elapsedMs} ms
                  </div>
                  <code className="mono-cmd block">{result.command}</code>
                </div>
              )}
            </div>
          </div>

          {error && !running && (
            <div className="card-surface border-destructive/40 px-4 py-3 text-sm text-destructive">{error}</div>
          )}

          {running && !result && (
            <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 xl:grid-cols-4">
              {Array.from({ length: Math.min(top, 8) }, (_, i) => (
                <Skeleton key={i} className="aspect-[4/3.6] rounded-xl" />
              ))}
            </div>
          )}

          {result && (
            <div className={running ? "pointer-events-none opacity-60 transition-opacity" : "transition-opacity"}>
              <div className="mb-3 flex items-baseline justify-between">
                <h2 className="text-xl font-semibold">Top {result.matches.length} matches</h2>
                <span className="text-xs text-muted-foreground">lower distance = more similar</span>
              </div>
              {result.unindexed > 0 && (
                <p className="mb-3 flex items-start gap-1.5 text-xs text-muted-foreground">
                  <InfoIcon className="mt-0.5 size-3.5 shrink-0" />
                  {result.unindexed} database image{result.unindexed === 1 ? " isn't" : "s aren't"} in this feature&apos;s
                  index, so {result.unindexed === 1 ? "it can't" : "they can't"} appear in these results
                  {feature?.targetFromIndex ? " (no ResNet embedding)" : " yet"}.
                </p>
              )}
              <ResultGrid matches={result.matches} maxDistance={maxDistance} onUseAsTarget={useMatchAsTarget} />

              {result.worst.length > 0 && (
                <div className="mt-8">
                  <h2 className="mb-3 text-xl font-semibold">Least similar</h2>
                  <ResultGrid matches={result.worst} maxDistance={maxDistance} onUseAsTarget={useMatchAsTarget} />
                </div>
              )}
            </div>
          )}

          {!result && !running && !error && (
            <div className="grid place-items-center rounded-2xl border border-dashed py-16 text-center text-sm text-muted-foreground">
              <SparklesIcon className="mb-2 size-6 text-brand" />
              Results appear here.
            </div>
          )}
        </section>
      </div>
    </div>
  );
}
