import { listDbImages, saveToDb, UploadError } from "@/lib/images";
import { startReindex } from "@/lib/jobs";

export async function GET() {
  return Response.json({ images: await listDbImages() });
}

export async function POST(req: Request) {
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
  // New images have no feature rows until extract re-runs over the folder
  if (saved.length > 0) startReindex();

  return Response.json({ saved, errors }, { status: saved.length > 0 ? 201 : 400 });
}
