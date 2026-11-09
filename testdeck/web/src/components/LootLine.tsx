/* One rare-or-better drop: the History page's finished record and the Live
 * page's in-flight heartbeat carry the same entry shape. */

import { fmtDuration } from "../api/hooks";
import type { LootEntry } from "../api/types";
import { QUALITY_CHOICES, QUALITY_COLOR } from "../data/wow";

const QUALITY_NAME: Record<number, string> = Object.fromEntries(
  QUALITY_CHOICES.map((q) => [q.v, q.label]),
);

export default function LootLine({
  l,
  nameColor,
}: {
  l: LootEntry;
  nameColor?: string;
}) {
  return (
    <div className="text-sm leading-relaxed text-ink-300">
      <span className="font-mono text-ink-600">
        {l.t === undefined ? "·" : fmtDuration(l.t).replace(" ", "")}
      </span>{" "}
      <span
        style={{ color: QUALITY_COLOR[l.quality ?? 0] }}
        title={`${QUALITY_NAME[l.quality ?? 0] ?? "quality " + l.quality} · item ${l.itemId}`}
      >
        [{l.item}]
      </span>
      {(l.count ?? 1) > 1 ? ` x${l.count}` : ""} →{" "}
      <span className="font-medium" style={nameColor ? { color: nameColor } : undefined}>
        {l.name ?? "?"}
      </span>{" "}
      <span
        className={`rounded px-1.5 py-0.5 text-xs ${
          l.roll === "need"
            ? "bg-emerald-900/50 text-emerald-300"
            : l.roll === "greed"
              ? "bg-amber-900/40 text-amber-300"
              : "bg-ink-800 text-ink-400"
        }`}
      >
        {l.roll || "looted"}
      </span>
      {l.source ? <span className="text-ink-500"> · from {l.source}</span> : null}
    </div>
  );
}
