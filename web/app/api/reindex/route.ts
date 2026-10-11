import { reindexStatus, startReindex } from "@/lib/jobs";

export async function GET() {
  return Response.json(reindexStatus());
}

// The Rebuild button: recompute every row, for when the feature code itself changed
export async function POST() {
  startReindex("full");
  return Response.json(reindexStatus(), { status: 202 });
}
