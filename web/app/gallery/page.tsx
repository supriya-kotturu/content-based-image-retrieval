import type { Metadata } from "next";
import { GalleryView } from "@/components/gallery-view";

export const metadata: Metadata = { title: "Database · CBIR Studio" };

export default function GalleryPage() {
  return <GalleryView />;
}
