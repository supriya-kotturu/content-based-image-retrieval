import "server-only";
import { listMetas } from "./meta";
import { runExtract } from "./cbir";
import type { ReindexStatus } from "./compat";

// One reindex at a time per server process. extract rewrites csvs in place, so queries are refused
// while it runs (see isReindexing) instead of reading a half-written file.
// Kept on globalThis so dev-mode HMR doesn't orphan a running job.
const g = globalThis as unknown as { __cbirJob?: ReindexStatus };
const state: ReindexStatus = (g.__cbirJob ??= {
  running: false,
  pending: false,
  current: null,
  progress: null,
  completed: [],
  error: null,
  finishedAt: null,
});

export function reindexStatus(): ReindexStatus {
  return { ...state, completed: [...state.completed] };
}

export function isReindexing(): boolean {
  return state.running;
}

// Uploads during a run set pending: the running pass may have listed the folder before the new
// file landed, so one more pass picks it up
export function startReindex(): void {
  if (state.running) {
    state.pending = true;
    return;
  }
  void loop();
}

async function loop() {
  state.running = true;
  try {
    do {
      state.pending = false;
      state.completed = [];
      state.error = null;
      const metas = await listMetas();
      for (const meta of metas) {
        state.current = meta.id;
        state.progress = null;
        try {
          await runExtract(meta, (done, total) => (state.progress = { done, total }));
          state.completed.push(meta.id);
        } catch (err) {
          // Keep going: one bad feature shouldn't leave the others stale
          state.error = `${meta.id}: ${(err as Error).message}`;
        }
      }
    } while (state.pending);
  } finally {
    state.running = false;
    state.current = null;
    state.progress = null;
    state.finishedAt = Date.now();
  }
}
