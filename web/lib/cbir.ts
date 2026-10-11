import "server-only";
import { spawn } from "node:child_process";
import path from "node:path";
import { CBIR_ROOT, DB_DIR, EXTRACT_EXE, QUERY_EXE, RESNET_CSV, imageUrl } from "./paths";
import type { FeatureMeta } from "./meta";
import type { Match, Metric } from "./compat";

// The only module that knows the exes exist. Swapping to a long-lived C++ service later means
// rewriting this file, nothing else.

export class CbirError extends Error {
  constructor(
    message: string,
    readonly stderr: string,
  ) {
    super(message);
  }
}

interface RunResult {
  stdout: string;
  stderr: string;
}

// Args as an array, never a shell string: target names come from users
function run(
  exe: string,
  args: string[],
  timeoutMs: number,
  onStdoutLine?: (line: string) => void,
): Promise<RunResult> {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, args, { cwd: CBIR_ROOT, windowsHide: true });
    let stdout = "";
    let stderr = "";
    let partial = "";
    const timer = setTimeout(() => {
      child.kill();
      reject(new CbirError(`${path.basename(exe)} timed out after ${timeoutMs / 1000}s`, stderr));
    }, timeoutMs);

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => {
      stdout += chunk;
      if (onStdoutLine) {
        const lines = (partial + chunk).split(/\r?\n/);
        partial = lines.pop() ?? "";
        lines.forEach(onStdoutLine);
      }
    });
    child.stderr.on("data", (chunk: string) => (stderr += chunk));
    child.on("error", (err) => {
      clearTimeout(timer);
      reject(new CbirError(`cannot start ${path.basename(exe)}: ${err.message}`, stderr));
    });
    child.on("close", (code) => {
      clearTimeout(timer);
      if (code === 0) resolve({ stdout, stderr });
      else reject(new CbirError(stderr.trim() || `${path.basename(exe)} exited with ${code}`, stderr));
    });
  });
}

// Relative to cwd with forward slashes: the exes accept either, and the echoed command stays readable
const rel = (abs: string) => path.relative(CBIR_ROOT, abs).replaceAll("\\", "/");

export interface QueryParams {
  featureId: string; // csv basename or "dnn"
  metric: Metric;
  targetPath: string; // absolute path to the target image
  top: number;
  worst: number;
}

export interface QueryResult {
  matches: Match[];
  worst: Match[];
  command: string;
}

export async function runQuery(p: QueryParams): Promise<QueryResult> {
  const common = [`metric=${p.metric}`, `top=${p.top}`, `worst=${p.worst}`, "gui=0"];
  const args =
    p.featureId === "dnn"
      ? [
          "feature=dnn",
          `csv=${rel(RESNET_CSV)}`,
          // the dnn csv stores bare names, so query looks the target up by name
          `target=${path.basename(p.targetPath)}`,
          `db=${rel(DB_DIR)}`,
          ...common,
        ]
      : [
          `csv=data/csv/${p.featureId}.csv`,
          `target=${rel(p.targetPath)}`,
          "root=data",
          ...common,
        ];

  const { stdout } = await run(QUERY_EXE, args, 30_000);
  const { matches, worst } = parseQueryOutput(stdout, p.featureId === "dnn");
  return { matches, worst, command: `query ${args.join(" ")}` };
}

// query prints "rank\tdistance\tpath" rows, then "least similar:" and "-k\t..." rows.
// csv_util also prints "Reading ..." chatter, so anything not tab-separated is skipped.
export function parseQueryOutput(stdout: string, bareNames: boolean): { matches: Match[]; worst: Match[] } {
  const matches: Match[] = [];
  const worst: Match[] = [];
  for (const line of stdout.split(/\r?\n/)) {
    const parts = line.split("\t");
    if (parts.length !== 3) continue;
    const rank = Number(parts[0]);
    const distance = Number(parts[1]);
    if (!Number.isFinite(rank) || !Number.isFinite(distance)) continue;
    // Store csvs hold data-relative paths ("olympus/pic.0001.jpg"); the dnn csv holds bare names
    const rel = bareNames ? `olympus/${parts[2]}` : parts[2];
    const m: Match = { rank, distance, name: path.basename(rel), url: imageUrl(rel) };
    (rank < 0 ? worst : matches).push(m);
  }
  return { matches, worst };
}

export async function runExtract(meta: FeatureMeta, onProgress: (done: number, total: number) => void) {
  const args = [`db=${rel(DB_DIR)}`, `feature=${meta.kind}`, "root=data"];
  if (meta.bins !== undefined) args.push(`bins=${meta.bins}`);
  if (meta.patch !== undefined) args.push(`patch=${meta.patch}`);
  if (meta.kind === "orange_dnn") args.push(`dnn=${rel(RESNET_CSV)}`);

  let total = 0;
  await run(EXTRACT_EXE, args, 10 * 60_000, (line) => {
    const found = /^found (\d+) images/.exec(line);
    if (found) {
      total = Number(found[1]);
      onProgress(0, total);
    }
    const step = /^\[(\d+)\/(\d+)\]/.exec(line);
    if (step) onProgress(Number(step[1]), Number(step[2]));
  });
  onProgress(total, total);
}
