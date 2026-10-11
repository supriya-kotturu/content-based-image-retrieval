"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "@/lib/api";
import type { ReindexStatus } from "@/lib/compat";

// Polls fast while a job runs, slowly otherwise. onFinish fires once per completed run so pages can
// refetch the feature list (row counts) and image list.
export function useReindex(onFinish?: () => void) {
  const [status, setStatus] = useState<ReindexStatus | null>(null);
  const finishRef = useRef(onFinish);
  const wasRunning = useRef(false);

  useEffect(() => {
    finishRef.current = onFinish;
  });

  const refresh = useCallback(async () => {
    try {
      const s = await api<ReindexStatus>("/api/reindex");
      setStatus(s);
      if (wasRunning.current && !s.running) finishRef.current?.();
      wasRunning.current = s.running;
    } catch {
      // the next tick retries
    }
  }, []);

  useEffect(() => {
    let alive = true;
    let timer: ReturnType<typeof setTimeout>;
    const tick = async () => {
      await refresh();
      if (alive) timer = setTimeout(tick, wasRunning.current ? 1200 : 8000);
    };
    void tick();
    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, [refresh]);

  return { status, refresh };
}
