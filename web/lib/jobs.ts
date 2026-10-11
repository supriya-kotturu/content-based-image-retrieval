import "server-only";
import { listMetas } from "./meta";
import { runExtract } from "./cbir";
import type { ReindexStatus } from "./compat";

// One extract run at a time per server process, so two runs never write the same csv. Queries
// are not blocked: extract swaps files in atomically.
// Kept on globalThis so dev-mode HMR doesn't orphan a running job.
type Mode = "append" | "full";
const g = globalThis as unknown as { __cbirJob?: ReindexStatus & { pendingMode: Mode | null } };
const state = (g.__cbirJob ??= {
  running: false,
  pending: false,
  pendingMode: null,
  current: null,
  progress: null,
  completed: [],
  error: null,
  finishedAt: null,
});

export function reindexStatus(): ReindexStatus {
  const { running, pending, current, progress, completed, error, finishedAt } = state;
  return { running, pending, current, progress, completed: [...completed], error, finishedAt };
}

// Uploads ask for "append"; the Rebuild button asks for "full". A request during a run queues one
// more pass (the running one may have listed the folder before the new file landed); full wins
// when both were asked for.
export function startReindex(mode: Mode = "append"): void {
  if (state.running) {
    state.pendingMode = state.pendingMode === "full" || mode === "full" ? "full" : "append";
    state.pending = true;
    return;
  }
  void loop(mode);
}

async function loop(first: Mode) {
  state.running = true;
  let mode: Mode | null = first;
  try {
    while (mode) {
      state.pending = false;
      state.pendingMode = null;
      state.completed = [];
      state.error = null;
      const metas = await listMetas();
      for (const meta of metas) {
        state.current = meta.id;
        state.progress = null;
        try {
          await runExtract(meta, mode === "append", (done, total) => (state.progress = { done, total }));
          state.completed.push(meta.id);
        } catch (err) {
          // Keep going: one bad feature shouldn't leave the others stale
          state.error = `${meta.id}: ${(err as Error).message}`;
        }
      }
      mode = state.pendingMode;
    }
  } finally {
    state.running = false;
    state.pending = false;
    state.current = null;
    state.progress = null;
    state.finishedAt = Date.now();
  }
}
