import { reindexStatus, startReindex } from "@/lib/jobs";
import { READ_ONLY } from "@/lib/paths";

export async function GET() {
  return Response.json(reindexStatus());
}

// The Rebuild button: recompute every row, for when the feature code itself changed
export async function POST() {
  if (READ_ONLY) {
    return Response.json({ error: "the database is read-only in this deployment" }, { status: 403 });
  }
  startReindex("full");
  return Response.json(reindexStatus(), { status: 202 });
}
