import { reindexStatus, startReindex } from "@/lib/jobs";

export async function GET() {
  return Response.json(reindexStatus());
}

export async function POST() {
  startReindex();
  return Response.json(reindexStatus(), { status: 202 });
}
