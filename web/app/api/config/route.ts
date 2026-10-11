import { MAX_UPLOAD_BYTES } from "@/lib/images";
import { listFeatures } from "@/lib/meta";
import { READ_ONLY } from "@/lib/paths";

export async function GET() {
  return Response.json({ features: await listFeatures(), readOnly: READ_ONLY, maxUploadBytes: MAX_UPLOAD_BYTES });
}
