import { Suspense } from "react";
import { SearchView } from "@/components/search-view";

export default function Home() {
  // useSearchParams needs a Suspense boundary
  return (
    <Suspense>
      <SearchView />
    </Suspense>
  );
}
