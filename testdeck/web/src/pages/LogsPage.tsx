/* Live log tail over SSE, with stick-to-bottom, pause, and a client-side
 * filter. Only *.log files inside the server's log_dir are offered. */

import { useEffect, useRef, useState } from "react";
import { api } from "../api/client";
import { useSSE } from "../api/hooks";
import type { LogFile } from "../api/types";
import { EmptyState, Spinner } from "../components/ui";

const MAX_LINES = 3000;

export default function LogsPage() {
  const [files, setFiles] = useState<LogFile[] | null>(null);
  const [file, setFile] = useState<string | null>(null);
  const [lines, setLines] = useState<string[]>([]);
  const [filter, setFilter] = useState("");
  const [paused, setPaused] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  const pinned = useRef(true);
  const pausedRef = useRef(false);
  pausedRef.current = paused;

  useEffect(() => {
    api
      .get<{ logs: LogFile[] }>("/api/logs")
      .then((r) => {
        setFiles(r.logs);
        const dc = r.logs.find((l) => l.name.includes("DungeonClear"));
        if (dc) setFile(dc.name);
        else if (r.logs.length) setFile(r.logs[0].name);
      })
      .catch(() => setFiles([]));
  }, []);

  useEffect(() => setLines([]), [file]);

  const connected = useSSE(
    file ? `/api/logs/stream?file=${encodeURIComponent(file)}` : null,
    (event, data) => {
      if (pausedRef.current) return;
      if (event === "lines" && Array.isArray(data)) {
        setLines((l) => [...l, ...(data as string[])].slice(-MAX_LINES));
      } else if (event === "line") {
        setLines((l) => [...l, String(data)].slice(-MAX_LINES));
      } else if (event === "error") {
        setLines((l) => [...l, `--- error: ${String(data)} ---`]);
      }
    },
  );

  useEffect(() => {
    const el = box.current;
    if (el && pinned.current) el.scrollTop = el.scrollHeight;
  }, [lines]);

  if (files === null) return <Spinner label="loading log list…" />;
  if (!files.length)
    return (
      <EmptyState icon="🧾" title="No log files found">
        The server offers *.log files from its configured log directory.
      </EmptyState>
    );

  const shown = filter
    ? lines.filter((l) => l.toLowerCase().includes(filter.toLowerCase()))
    : lines;

  return (
    <div className="flex h-[calc(100dvh-9rem)] flex-col lg:h-[calc(100dvh-6rem)]">
      <div className="mb-3 flex flex-wrap items-center gap-2">
        <h1 className="mr-2 text-2xl font-semibold">Logs</h1>
        <select
          className="rounded-lg border border-ink-700 bg-ink-900 px-2.5 py-1.5 text-sm outline-none"
          value={file ?? ""}
          onChange={(e) => setFile(e.target.value)}
        >
          {files.map((f) => (
            <option key={f.name} value={f.name}>
              {f.name}
            </option>
          ))}
        </select>
        <input
          placeholder="Filter lines…"
          className="w-48 rounded-lg border border-ink-700 bg-ink-900 px-2.5 py-1.5 text-sm outline-none focus:border-iris-400/60"
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
        />
        <button
          onClick={() => setPaused(!paused)}
          className={`rounded-lg border px-2.5 py-1.5 text-sm ${
            paused
              ? "border-amber-700 text-amber-300"
              : "border-ink-700 text-ink-400"
          }`}
        >
          {paused ? "▶ resume" : "⏸ pause"}
        </button>
        <span
          className={`text-xs ${connected ? "text-emerald-400" : "text-red-400"}`}
        >
          {connected ? "● streaming" : "○ disconnected"}
        </span>
      </div>

      <div
        ref={box}
        onScroll={(e) => {
          const el = e.currentTarget;
          pinned.current = el.scrollHeight - el.scrollTop - el.clientHeight < 24;
        }}
        className="flex-1 overflow-y-auto rounded-xl border border-ink-800 bg-ink-950 p-3 font-mono text-xs leading-relaxed text-ink-400"
      >
        {shown.map((l, i) => (
          <div key={i} className="whitespace-pre-wrap break-all">
            {l}
          </div>
        ))}
        {!shown.length && (
          <div className="p-4 text-ink-600">
            {filter ? "no lines match the filter" : "waiting for output…"}
          </div>
        )}
      </div>
    </div>
  );
}
