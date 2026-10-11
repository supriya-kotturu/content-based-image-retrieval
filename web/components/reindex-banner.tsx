"use client";

import { Loader2Icon, TriangleAlertIcon } from "lucide-react";
import { Progress } from "@/components/ui/progress";
import type { ReindexStatus } from "@/lib/compat";

export function ReindexBanner({ status }: { status: ReindexStatus | null }) {
  if (!status) return null;

  if (status.running) {
    const pct = status.progress && status.progress.total > 0 ? (status.progress.done / status.progress.total) * 100 : 0;
    return (
      <div className="card-surface flex flex-col gap-2 border-brand/40 bg-brand-soft/50 px-4 py-3 sm:flex-row sm:items-center sm:gap-4">
        <div className="flex items-center gap-2 text-sm font-medium">
          <Loader2Icon className="size-4 animate-spin text-brand" />
          Re-indexing features
          <span className="font-mono text-xs text-muted-foreground">{status.current}</span>
          {status.pending && <span className="text-xs text-muted-foreground">(+1 queued)</span>}
        </div>
        <div className="flex flex-1 items-center gap-3">
          <Progress value={pct} className="flex-1" />
          <span className="text-xs text-muted-foreground tabular-nums">
            {status.completed.length} done
            {status.progress ? ` · ${status.progress.done}/${status.progress.total}` : ""}
          </span>
        </div>
      </div>
    );
  }

  if (status.error) {
    return (
      <div className="card-surface flex items-center gap-2 border-destructive/40 px-4 py-3 text-sm text-destructive">
        <TriangleAlertIcon className="size-4" />
        Last re-index had an error: <span className="font-mono text-xs">{status.error}</span>
      </div>
    );
  }
  return null;
}
