import { imageUrl } from "@/lib/paths";
import { saveTemp, UploadError } from "@/lib/images";

// A one-off query image: saved under data/tmp (outside the db folder, so never indexed)
export async function POST(req: Request) {
  const form = await req.formData();
  const file = form.get("file");
  if (!(file instanceof File)) {
    return Response.json({ error: "no file" }, { status: 400 });
  }
  try {
    const id = await saveTemp(file);
    return Response.json({ id, url: imageUrl(`tmp/${id}`) }, { status: 201 });
  } catch (err) {
    if (err instanceof UploadError) return Response.json({ error: err.message }, { status: 400 });
    throw err;
  }
}
