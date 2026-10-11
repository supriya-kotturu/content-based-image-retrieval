"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import Link from "next/link";
import { toast } from "sonner";
import { ChevronLeftIcon, ChevronRightIcon, RefreshCwIcon, SearchIcon } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { UploadDropzone } from "@/components/upload-dropzone";
import { ReindexBanner } from "@/components/reindex-banner";
import { useReindex } from "@/hooks/use-reindex";
import { fetchCatalog, postFiles, postJson } from "@/lib/api";
import type { DbImage, FeatureInfo, ReindexStatus } from "@/lib/compat";

const PAGE = 48;

export function GalleryView() {
  const [images, setImages] = useState<DbImage[]>([]);
  const [features, setFeatures] = useState<FeatureInfo[]>([]);
  const [filter, setFilter] = useState("");
  const [scope, setScope] = useState<string>("all");
  const [page, setPage] = useState(0);
  const [uploading, setUploading] = useState(false);

  const load = useCallback(
    () =>
      fetchCatalog().then((c) => {
        setImages(c.images);
        setFeatures(c.features);
      }),
    [],
  );

  const { status, refresh } = useReindex(() => {
    load().catch(() => {});
    toast.success("Re-index finished");
  });

  useEffect(() => {
    fetchCatalog()
      .then((c) => {
        setImages(c.images);
        setFeatures(c.features);
      })
      .catch((e) => toast.error(e.message));
  }, []);

  const upload = async (files: File[]) => {
    setUploading(true);
    try {
      const res = await postFiles<{ saved: string[]; errors: string[] }>("/api/images", "files", files);
      toast.success(`Added ${res.saved.length} image${res.saved.length === 1 ? "" : "s"}; re-indexing features`);
      res.errors.forEach((e) => toast.error(e));
      await load();
      setScope("uploaded");
      setPage(0);
      void refresh();
    } catch (e) {
      toast.error((e as Error).message);
    } finally {
      setUploading(false);
    }
  };

  const filtered = useMemo(() => {
    const f = filter.trim().toLowerCase();
    return images.filter((i) => (scope === "all" || i.uploaded) && (!f || i.name.toLowerCase().includes(f)));
  }, [images, filter, scope]);

  const pages = Math.max(1, Math.ceil(filtered.length / PAGE));
  const current = Math.min(page, pages - 1);
  const visible = filtered.slice(current * PAGE, (current + 1) * PAGE);
  const uploadedCount = images.filter((i) => i.uploaded).length;
  const store = features.filter((f) => f.id !== "dnn");
  // A feature is stale when its row count lags the folder (images uploaded but not yet extracted)
  const expected = images.length;

  return (
    <div className="space-y-6">
      <div className="flex flex-col gap-1">
        <h1 className="text-3xl font-semibold sm:text-4xl">Database</h1>
        <p className="max-w-2xl text-muted-foreground">
          {images.length.toLocaleString()} images in <span className="font-mono text-sm">data/olympus</span>
          {uploadedCount > 0 && <> · {uploadedCount} uploaded</>}. New images are indexed by re-running the C++{" "}
          <span className="font-mono text-xs">extract</span> program for every feature.
        </p>
      </div>

      <ReindexBanner status={status} />

      <div className="grid gap-6 lg:grid-cols-[1fr_340px]">
        <UploadDropzone
          multiple
          busy={uploading}
          onFiles={upload}
          title="Add images to the database"
          hint="Drop JPG/PNG files (≤ 10 MB each). Features are re-extracted automatically; DNN features can't be computed for new images."
          className="py-10"
        />

        <section className="card-surface space-y-3 p-4">
          <div className="flex items-center justify-between">
            <h2 className="panel-title">Feature index</h2>
            <Button
              variant="ghost"
              size="xs"
              disabled={status?.running}
              onClick={async () => {
                await postJson<ReindexStatus>("/api/reindex", {});
                void refresh();
              }}
            >
              <RefreshCwIcon /> Rebuild
            </Button>
          </div>
          <ul className="space-y-1.5 text-sm">
            {store.map((f) => {
              const fresh = f.kind === "orange_dnn" || f.rows >= expected;
              const active = status?.running && status.current === f.id;
              return (
                <li key={f.id} className="flex items-center gap-2">
                  <span
                    className={`size-2 rounded-full ${active ? "animate-pulse bg-brand" : fresh ? "bg-success" : "bg-destructive/70"}`}
                  />
                  <span className="truncate font-mono text-xs">{f.id}</span>
                  <span className="ml-auto text-xs text-muted-foreground tabular-nums">{f.rows.toLocaleString()} rows</span>
                </li>
              );
            })}
          </ul>
          <p className="text-xs text-muted-foreground">
            orange_dnn only covers images with a provided ResNet embedding, so it stays at the original count.
          </p>
        </section>
      </div>

      <div className="flex flex-col gap-3 sm:flex-row sm:items-center">
        <Tabs
          value={scope}
          onValueChange={(v) => {
            setScope(String(v));
            setPage(0);
          }}
        >
          <TabsList>
            <TabsTrigger value="all">All</TabsTrigger>
            <TabsTrigger value="uploaded">Uploaded ({uploadedCount})</TabsTrigger>
          </TabsList>
        </Tabs>
        <div className="relative sm:ml-auto sm:w-72">
          <SearchIcon className="pointer-events-none absolute top-1/2 left-2.5 size-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            value={filter}
            onChange={(e) => {
              setFilter(e.target.value);
              setPage(0);
            }}
            placeholder="Filter by name"
            className="pl-8"
          />
        </div>
      </div>

      <div className="grid grid-cols-2 gap-3 sm:grid-cols-4 lg:grid-cols-6">
        {visible.map((img) => (
          <Link
            key={img.name}
            href={`/?target=${encodeURIComponent(img.name)}`}
            className="group space-y-1.5"
            title="Search with this image"
          >
            <div className="thumb">
              {/* eslint-disable-next-line @next/next/no-img-element -- served by our own route */}
              <img src={img.url} alt={img.name} loading="lazy" />
              <span className="absolute inset-0 grid place-items-center bg-foreground/0 opacity-0 transition-all group-hover:bg-foreground/35 group-hover:opacity-100">
                <span className="rounded-full bg-background/90 px-2.5 py-1 text-xs font-medium">Search</span>
              </span>
              {img.uploaded && (
                <span className="absolute top-1.5 left-1.5 rounded bg-brand px-1.5 text-[0.65rem] font-semibold text-brand-foreground">
                  NEW
                </span>
              )}
            </div>
            <span className="block truncate font-mono text-[0.7rem] text-muted-foreground">{img.name}</span>
          </Link>
        ))}
      </div>
      {visible.length === 0 && (
        <p className="py-12 text-center text-sm text-muted-foreground">
          {scope === "uploaded" ? "No uploaded images yet." : "No images match."}
        </p>
      )}

      <div className="flex items-center justify-center gap-2">
        <Button variant="outline" size="sm" disabled={current === 0} onClick={() => setPage(current - 1)}>
          <ChevronLeftIcon /> Prev
        </Button>
        <span className="text-sm text-muted-foreground tabular-nums">
          {current + 1} / {pages}
        </span>
        <Button variant="outline" size="sm" disabled={current >= pages - 1} onClick={() => setPage(current + 1)}>
          Next <ChevronRightIcon />
        </Button>
      </div>
    </div>
  );
}
