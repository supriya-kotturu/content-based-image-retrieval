"use client";

import { useRef, useState } from "react";
import { ImageUpIcon, Loader2Icon } from "lucide-react";
import { cn } from "@/lib/utils";

interface Props {
  multiple?: boolean;
  busy?: boolean;
  onFiles: (files: File[]) => void;
  title: string;
  hint?: string;
  className?: string;
}

export function UploadDropzone({ multiple, busy, onFiles, title, hint, className }: Props) {
  const input = useRef<HTMLInputElement>(null);
  const [dragging, setDragging] = useState(false);

  const take = (list: FileList | null) => {
    if (!list || list.length === 0 || busy) return;
    const files = Array.from(list);
    onFiles(multiple ? files : files.slice(0, 1));
  };

  return (
    <div
      role="button"
      tabIndex={0}
      className={cn("dropzone", busy && "pointer-events-none opacity-70", className)}
      data-dragging={dragging}
      onClick={() => input.current?.click()}
      onKeyDown={(e) => (e.key === "Enter" || e.key === " ") && input.current?.click()}
      onDragOver={(e) => {
        e.preventDefault();
        setDragging(true);
      }}
      onDragLeave={() => setDragging(false)}
      onDrop={(e) => {
        e.preventDefault();
        setDragging(false);
        take(e.dataTransfer.files);
      }}
    >
      <span className="grid size-10 place-items-center rounded-full bg-brand-soft text-accent-foreground">
        {busy ? <Loader2Icon className="size-5 animate-spin" /> : <ImageUpIcon className="size-5" />}
      </span>
      <span className="font-medium text-foreground">{title}</span>
      {hint && <span className="text-xs">{hint}</span>}
      <input
        ref={input}
        type="file"
        accept=".jpg,.jpeg,.png,image/jpeg,image/png"
        multiple={multiple}
        hidden
        onChange={(e) => {
          take(e.target.files);
          e.target.value = "";
        }}
      />
    </div>
  );
}
