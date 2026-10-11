"use client";

import { InfoIcon } from "lucide-react";
import { Popover, PopoverContent, PopoverDescription, PopoverTitle, PopoverTrigger } from "@/components/ui/popover";

interface Props {
  // What the icon explains, for screen readers ("About Texture + color")
  label: string;
  title: string;
  lines: readonly string[];
}

// A popover rather than a tooltip: tooltips only open on hover/focus, so touch screens could never
// read them. openOnHover keeps the quick hover behaviour on desktop.
export function InfoHint({ label, title, lines }: Props) {
  return (
    <Popover>
      <PopoverTrigger
        openOnHover
        delay={150}
        aria-label={label}
        className="inline-grid size-5 cursor-help place-items-center rounded-full text-muted-foreground transition-colors hover:bg-muted hover:text-foreground focus-visible:ring-2 focus-visible:ring-ring/50 focus-visible:outline-none"
      >
        <InfoIcon className="size-3.5" />
      </PopoverTrigger>
      <PopoverContent side="right" align="start" className="w-80 space-y-1.5">
        <PopoverTitle className="text-sm font-semibold">{title}</PopoverTitle>
        {lines.map((line) => (
          <PopoverDescription key={line} className="text-xs leading-relaxed">
            {line}
          </PopoverDescription>
        ))}
      </PopoverContent>
    </Popover>
  );
}
