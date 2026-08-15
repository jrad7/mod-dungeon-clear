import { useCallback, useEffect, useRef, useState } from "react";

/* Poll an endpoint while the component is mounted. Pauses when the tab is
 * hidden and refetches immediately on visibilitychange — the module's data
 * sources are files the worldserver rewrites, so polling is the honest
 * transport (nothing exists to push a notification). */
export function usePoll<T>(
  fetcher: () => Promise<T>,
  intervalMs = 5000,
): { data: T | null; error: string | null; refresh: () => void } {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<string | null>(null);
  const fetcherRef = useRef(fetcher);
  fetcherRef.current = fetcher;
  const lastJson = useRef<string>("");

  const tick = useCallback(async () => {
    try {
      const d = await fetcherRef.current();
      /* Identity dedupe: identical payloads must not re-render the tree —
       * a 5s cadence would otherwise reset scroll/selection churn. */
      const j = JSON.stringify(d);
      if (j !== lastJson.current) {
        lastJson.current = j;
        setData(d);
      }
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    let timer: ReturnType<typeof setInterval> | null = null;
    /* start() must be idempotent: browsers do fire visibilitychange with the
       document already visible, and starting a second interval over the top of
       the first leaks the first one for the life of the page. */
    const start = () => {
      if (timer) return;
      void tick();
      timer = setInterval(() => void tick(), intervalMs);
    };
    const stop = () => {
      if (timer) clearInterval(timer);
      timer = null;
    };
    const onVis = () => (document.hidden ? stop() : start());
    start();
    document.addEventListener("visibilitychange", onVis);
    return () => {
      stop();
      document.removeEventListener("visibilitychange", onVis);
    };
  }, [tick, intervalMs]);

  return { data, error, refresh: tick };
}

/* Server-Sent Events subscription for the log tail. */
export function useSSE(
  url: string | null,
  onEvent: (event: string, data: unknown) => void,
): boolean {
  const [connected, setConnected] = useState(false);
  const handler = useRef(onEvent);
  handler.current = onEvent;

  useEffect(() => {
    if (!url) return;
    const es = new EventSource(url);
    /* EventSource reconnects by itself, so the indicator has to follow BOTH
       edges. Reporting connected at construction and only ever clearing it on
       error left the Logs page reading "disconnected" for the rest of the
       session after one blip, while lines kept arriving behind the label. */
    es.onopen = () => setConnected(true);
    const forward = (name: string) => (e: MessageEvent) => {
      let data: unknown = e.data;
      try {
        data = JSON.parse(e.data);
      } catch {
        /* raw string */
      }
      handler.current(name, data);
    };
    const names = ["lines", "line", "error"];
    const listeners = names.map((n) => {
      const fn = forward(n);
      es.addEventListener(n, fn);
      return [n, fn] as const;
    });
    es.onerror = () => setConnected(false);
    return () => {
      listeners.forEach(([n, fn]) => es.removeEventListener(n, fn));
      es.close();
      setConnected(false);
    };
  }, [url]);

  return connected;
}

export function fmtDuration(s: number | undefined): string {
  if (s === undefined || s === null || isNaN(s)) return "–";
  const t = Math.max(0, Math.round(s));
  const h = Math.floor(t / 3600);
  const m = Math.floor((t % 3600) / 60);
  const sec = t % 60;
  if (h) return `${h}h ${m}m`;
  if (m) return `${m}m ${String(sec).padStart(2, "0")}s`;
  return `${sec}s`;
}

export function timeAgo(ms: number | undefined): string {
  if (!ms) return "–";
  const d = Date.now() - ms;
  if (d < 60_000) return "just now";
  if (d < 3_600_000) return `${Math.floor(d / 60_000)}m ago`;
  if (d < 86_400_000) return `${Math.floor(d / 3_600_000)}h ago`;
  return `${Math.floor(d / 86_400_000)}d ago`;
}
