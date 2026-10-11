"use client";

import { useState } from "react";
import { CrosshairIcon } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import type { Match } from "@/lib/compat";

interface Props {
  matches: Match[];
  // Bar widths are relative to this, so top and worst rows share a scale
  maxDistance: number;
  onUseAsTarget?: (m: Match) => void;
}

function formatDistance(d: number): string {
  if (d >= 1000) return d.toLocaleString(undefined, { maximumFractionDigits: 0 });
  return d.toFixed(4);
}

export function ResultGrid({ matches, maxDistance, onUseAsTarget }: Props) {
  const [open, setOpen] = useState<Match | null>(null);

  return (
    <>
      <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 xl:grid-cols-4">
        {matches.map((m) => (
          <div key={`${m.rank}-${m.name}`} className="result-tile group">
            <button type="button" className="thumb block w-full cursor-zoom-in rounded-none ring-0" onClick={() => setOpen(m)}>
              {/* eslint-disable-next-line @next/next/no-img-element -- served by our own route */}
              <img src={m.url} alt={m.name} loading="lazy" />
              <span className="rank-chip" data-top={m.rank > 0 && m.rank <= 3}>
                {m.rank > 0 ? m.rank : `−${-m.rank}`}
              </span>
            </button>
            <div className="space-y-1.5 px-3 py-2.5">
              <div className="flex items-center justify-between gap-2">
                <span className="truncate font-mono text-xs">{m.name}</span>
                {onUseAsTarget && (
                  <Button
                    variant="ghost"
                    size="icon-xs"
                    title="Use as target"
                    className="opacity-0 transition-opacity group-hover:opacity-100"
                    onClick={() => onUseAsTarget(m)}
                  >
                    <CrosshairIcon />
                  </Button>
                )}
              </div>
              <div className="flex items-center gap-2">
                <div className="dist-bar flex-1">
                  <span style={{ width: `${maxDistance > 0 ? Math.max(4, (m.distance / maxDistance) * 100) : 0}%` }} />
                </div>
                <span className="text-[0.7rem] text-muted-foreground tabular-nums">{formatDistance(m.distance)}</span>
              </div>
            </div>
          </div>
        ))}
      </div>

      <Dialog open={open !== null} onOpenChange={(o) => !o && setOpen(null)}>
        <DialogContent className="sm:max-w-3xl">
          {open && (
            <>
              <DialogHeader>
                <DialogTitle className="font-mono">{open.name}</DialogTitle>
                <DialogDescription>
                  {open.rank > 0 ? `Rank ${open.rank}` : `Least similar #${-open.rank}`} · distance{" "}
                  {formatDistance(open.distance)}
                </DialogDescription>
              </DialogHeader>
              {/* eslint-disable-next-line @next/next/no-img-element -- served by our own route */}
              <img src={open.url} alt={open.name} className="max-h-[70vh] w-full rounded-lg object-contain" />
              {onUseAsTarget && (
                <Button
                  className="justify-self-end"
                  onClick={() => {
                    onUseAsTarget(open);
                    setOpen(null);
                  }}
                >
                  <CrosshairIcon /> Use as target
                </Button>
              )}
            </>
          )}
        </DialogContent>
      </Dialog>
    </>
  );
}
