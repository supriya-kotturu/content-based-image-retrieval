import { listDbImages, saveToDb, UploadError } from "@/lib/images";
import { startReindex } from "@/lib/jobs";
import { READ_ONLY } from "@/lib/paths";

export async function GET() {
  return Response.json({ images: await listDbImages() });
}

export async function POST(req: Request) {
  if (READ_ONLY) {
    return Response.json({ error: "the database is read-only in this deployment" }, { status: 403 });
  }
  const form = await req.formData();
  const files = form.getAll("files").filter((f): f is File => f instanceof File);
  if (files.length === 0) {
    return Response.json({ error: "no files" }, { status: 400 });
  }

  const saved: string[] = [];
  const errors: string[] = [];
  for (const file of files) {
    try {
      saved.push(await saveToDb(file));
    } catch (err) {
      if (!(err instanceof UploadError)) throw err;
      errors.push(err.message);
    }
  }
  // New images have no feature rows until extract appends them
  if (saved.length > 0) startReindex("append");

  return Response.json({ saved, errors }, { status: saved.length > 0 ? 201 : 400 });
}
