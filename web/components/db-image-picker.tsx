"use client";

import { useMemo, useState } from "react";
import { ChevronLeftIcon, ChevronRightIcon, SearchIcon } from "lucide-react";
import { Input } from "@/components/ui/input";
import { Button } from "@/components/ui/button";
import type { DbImage } from "@/lib/compat";

const PAGE = 24;

interface Props {
  images: DbImage[];
  selected: string | null;
  onSelect: (img: DbImage) => void;
}

export function DbImagePicker({ images, selected, onSelect }: Props) {
  const [filter, setFilter] = useState("");
  const [page, setPage] = useState(0);

  const filtered = useMemo(() => {
    const f = filter.trim().toLowerCase();
    return f ? images.filter((i) => i.name.toLowerCase().includes(f)) : images;
  }, [images, filter]);

  const pages = Math.max(1, Math.ceil(filtered.length / PAGE));
  const current = Math.min(page, pages - 1);
  const visible = filtered.slice(current * PAGE, (current + 1) * PAGE);

  return (
    <div className="space-y-3">
      <div className="relative">
        <SearchIcon className="pointer-events-none absolute top-1/2 left-2.5 size-4 -translate-y-1/2 text-muted-foreground" />
        <Input
          value={filter}
          onChange={(e) => {
            setFilter(e.target.value);
            setPage(0);
          }}
          placeholder="Filter by name, e.g. 0535 or upload_"
          className="pl-8"
        />
      </div>

      <div className="grid grid-cols-4 gap-2">
        {visible.map((img) => (
          <button
            key={img.name}
            type="button"
            title={img.name}
            className="thumb cursor-pointer"
            data-selected={img.name === selected}
            onClick={() => onSelect(img)}
          >
            {/* eslint-disable-next-line @next/next/no-img-element -- served by our own route, already sized */}
            <img src={img.url} alt={img.name} loading="lazy" />
            {img.uploaded && (
              <span className="absolute right-1 bottom-1 rounded bg-brand px-1 text-[0.6rem] font-semibold text-brand-foreground">
                NEW
              </span>
            )}
          </button>
        ))}
        {visible.length === 0 && (
          <p className="col-span-4 py-6 text-center text-sm text-muted-foreground">No images match.</p>
        )}
      </div>

      <div className="flex items-center justify-between text-xs text-muted-foreground">
        <span className="tabular-nums">
          {filtered.length} image{filtered.length === 1 ? "" : "s"}
        </span>
        <div className="flex items-center gap-1">
          <Button variant="ghost" size="icon-sm" disabled={current === 0} onClick={() => setPage(current - 1)}>
            <ChevronLeftIcon />
          </Button>
          <span className="tabular-nums">
            {current + 1} / {pages}
          </span>
          <Button
            variant="ghost"
            size="icon-sm"
            disabled={current >= pages - 1}
            onClick={() => setPage(current + 1)}
          >
            <ChevronRightIcon />
          </Button>
        </div>
      </div>
    </div>
  );
}
