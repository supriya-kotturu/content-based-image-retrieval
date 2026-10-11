"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useTheme } from "next-themes";
import { MoonIcon, ScanSearchIcon, SunIcon } from "lucide-react";
import { Button } from "@/components/ui/button";

const NAV = [
  { href: "/", label: "Search" },
  { href: "/gallery", label: "Database" },
];

export function SiteHeader() {
  const pathname = usePathname();
  const { resolvedTheme, setTheme } = useTheme();

  return (
    <header className="sticky top-0 z-40 border-b bg-background/75 backdrop-blur-md">
      <div className="mx-auto flex h-14 max-w-7xl items-center gap-4 px-4 sm:px-6">
        <Link href="/" className="flex items-center gap-2">
          <span className="grid size-8 place-items-center rounded-lg bg-brand text-brand-foreground shadow-sm">
            <ScanSearchIcon className="size-4.5" />
          </span>
          <span className="font-heading text-[1.05rem] font-semibold tracking-tight whitespace-nowrap">
            CBIR<span className="text-muted-foreground font-normal"> Studio</span>
          </span>
        </Link>
        <nav className="ml-2 flex items-center gap-1">
          {NAV.map((n) => (
            <Link key={n.href} href={n.href} className="nav-link" data-active={pathname === n.href}>
              {n.label}
            </Link>
          ))}
        </nav>
        <Button
          variant="ghost"
          size="icon"
          className="ml-auto rounded-full"
          aria-label="Toggle theme"
          onClick={() => setTheme(resolvedTheme === "dark" ? "light" : "dark")}
        >
          <SunIcon className="size-4 dark:hidden" />
          <MoonIcon className="hidden size-4 dark:block" />
        </Button>
      </div>
    </header>
  );
}
