import { listFeatures } from "@/lib/meta";

export async function GET() {
  return Response.json({ features: await listFeatures() });
}
