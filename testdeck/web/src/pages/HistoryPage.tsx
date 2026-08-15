/* Run and plan history from the JSONL tails, at parity with the Command
 * Deck's post-mortems. The record schema is ADDITIVE: every section renders
 * only when its key is present — an old row must never show a fabricated
 * zero. */

import { useMemo, useState, type ReactNode } from "react";
import { api } from "../api/client";
import { fmtDuration, timeAgo, usePoll } from "../api/hooks";
import { describeGear, rerunSpec, startRosterRun, startRun } from "../api/launch";
import type {
  Catalogue,
  PlanRecord,
  PullEntry,
  RunDiag,
  RunRecord,
} from "../api/types";
import { classColorFor, QUALITY_CHOICES } from "../data/wow";
import {
  Card,
  ConfirmButton,
  CopyButton,
  EmptyState,
  ExpandToggle,
  ResultPill,
  Spinner,
  useToast,
} from "../components/ui";
import { useSession } from "../auth/SessionContext";

const QUALITY_NAME: Record<number, string> = Object.fromEntries(
  QUALITY_CHOICES.map((q) => [q.v, q.label]),
);

/* The record's `t` values are seconds into the run. */
const fmtT = (t?: number) =>
  t === undefined ? "·" : fmtDuration(t).replace(" ", "");

export default function HistoryPage() {
  const [tab, setTab] = useState<"runs" | "plans">("runs");
  return (
    <div>
      <div className="mb-6 flex items-end justify-between">
        <div>
          <h1 className="text-2xl font-semibold">History</h1>
          <p className="mt-1 text-sm text-ink-400">
            Finished runs and campaign summaries.
          </p>
        </div>
        {/* Same segmented treatment as the launch drawer's mode tabs. */}
        <div className="flex gap-1 rounded-xl border border-ink-800 bg-ink-950/70 p-1">
          {(["runs", "plans"] as const).map((t) => (
            <button
              key={t}
              onClick={() => setTab(t)}
              className={`rounded-lg px-4 py-1.5 text-sm capitalize transition ${
                tab === t
                  ? "bg-iris-500/20 text-iris-100 ring-1 ring-inset ring-iris-500/40"
                  : "text-ink-400 hover:text-ink-200"
              }`}
            >
              {t}
            </button>
          ))}
        </div>
      </div>
      {tab === "runs" ? <RunsTab /> : <PlansTab />}
    </div>
  );
}

/* ---- shared row plumbing ---- */

function Section({
  title,
  note,
  scroll,
  children,
}: {
  title: string;
  note?: string;
  scroll?: boolean;
  children: ReactNode;
}) {
  return (
    <div>
      <div className="mb-1 text-xs uppercase tracking-wide text-ink-600">
        {title}
        {note && <span className="ml-2 normal-case text-ink-700">{note}</span>}
      </div>
      <div
        className={
          scroll
            ? "max-h-56 overflow-y-auto rounded-lg border border-ink-800 bg-ink-950/70 p-3"
            : ""
        }
      >
        {children}
      </div>
    </div>
  );
}

function Line({
  tone = "",
  children,
}: {
  tone?: "" | "warn" | "fail" | "dim";
  children: ReactNode;
}) {
  const cls =
    tone === "warn"
      ? "text-amber-300/90"
      : tone === "fail"
        ? "text-red-300"
        : tone === "dim"
          ? "text-ink-500"
          : "text-ink-300";
  return <div className={`text-sm leading-relaxed ${cls}`}>{children}</div>;
}

function PlayerName({
  name,
  comp,
}: {
  name?: string;
  comp?: RunRecord["comp"];
}) {
  const color = classColorFor(name, comp);
  return (
    <span className="font-medium" style={color ? { color } : undefined}>
      {name ?? "?"}
    </span>
  );
}

/* ---- runs ---- */

function RunsTab() {
  const { data, error } = usePoll(
    () => api.get<{ runs: RunRecord[] }>("/api/testruns?limit=100"),
    15000,
  );
  /* Rerun needs the catalogue to map a record's resolved gear ceiling back
   * onto this dungeon's curated ladder — see rerunSpec. */
  const { data: catalogue } = usePoll(
    () => api.get<Catalogue>("/api/testdungeons"),
    60000,
  );
  const { session } = useSession();
  const toast = useToast();
  const [dungeonFilter, setDungeonFilter] = useState("");
  const [resultFilter, setResultFilter] = useState("");
  const [open, setOpen] = useState<Set<string>>(new Set());
  const toggle = (id: string) =>
    setOpen((s) => {
      const next = new Set(s);
      if (!next.delete(id)) next.add(id);
      return next;
    });

  const runs = data?.runs ?? [];
  const dungeons = useMemo(
    () =>
      [...new Set(runs.map((r) => r.dungeonName ?? r.dungeon ?? ""))]
        .filter(Boolean)
        .sort(),
    [runs],
  );
  const results = useMemo(
    () => [...new Set(runs.map((r) => r.result ?? ""))].filter(Boolean).sort(),
    [runs],
  );
  const filtered = runs.filter(
    (r) =>
      (!dungeonFilter || (r.dungeonName ?? r.dungeon) === dungeonFilter) &&
      (!resultFilter || r.result === resultFilter),
  );

  if (error)
    return (
      <EmptyState icon="⚠️" title="Cannot reach the server">
        {error}
      </EmptyState>
    );
  if (!data) return <Spinner label="loading history…" />;
  if (!runs.length)
    return (
      <EmptyState icon="📜" title="No finished runs yet">
        Results land here the moment a run ends.
      </EmptyState>
    );

  const sel =
    "rounded-lg border border-ink-700 bg-ink-900 px-2.5 py-1.5 text-sm outline-none";

  return (
    <div>
      <div className="mb-3 flex flex-wrap items-center gap-2">
        <select
          className={sel}
          value={dungeonFilter}
          onChange={(e) => setDungeonFilter(e.target.value)}
        >
          <option value="">all dungeons</option>
          {dungeons.map((d) => (
            <option key={d}>{d}</option>
          ))}
        </select>
        <select
          className={sel}
          value={resultFilter}
          onChange={(e) => setResultFilter(e.target.value)}
        >
          <option value="">all results</option>
          {results.map((r) => (
            <option key={r}>{r}</option>
          ))}
        </select>
        <span className="text-sm text-ink-500">
          {filtered.length} of {runs.length}
        </span>
        {session?.admin && (
          <ConfirmButton
            label="Clear history"
            message="Wipe the entire run history? A one-level .bak backup is kept on the server."
            className="ml-auto rounded-lg border border-ink-800 px-3 py-1.5 text-xs text-ink-500 hover:border-red-900 hover:text-red-300"
            onConfirm={() => {
              api
                .post<{ cleared?: number }>("/api/testruns/clear")
                .then((r) => toast("ok", `Cleared ${r.cleared ?? 0} records`))
                .catch((e) => toast("error", e.message));
            }}
          />
        )}
      </div>

      <div className="space-y-2">
        {filtered.map((r, i) => {
          const id = r.runId ?? String(i);
          const expanded = open.has(id);
          return (
            <Card key={id} className="!p-0">
              <div
                className="flex w-full cursor-pointer flex-wrap items-center gap-x-3 gap-y-1 px-4 py-3 text-left"
                onClick={() => toggle(id)}
              >
                <ResultPill result={r.result} />
                <span className="font-medium">
                  {r.dungeonName ?? r.dungeon ?? "?"}
                </span>
                {r.heroic && (
                  <span className="text-xs text-fuchsia-300">heroic</span>
                )}
                {r.roster && (
                  <span className="rounded bg-sky-500/15 px-1.5 py-0.5 text-xs text-sky-300">
                    roster
                  </span>
                )}
                <WipeBadge r={r} />
                {r.level !== undefined && (
                  <span className="text-xs text-ink-600">lv {r.level}</span>
                )}
                <GearChip r={r} />
                {r.bossesTotal !== undefined && (
                  <span className="text-sm text-ink-400">
                    {r.bossesKilled ?? 0}/{r.bossesTotal} bosses
                  </span>
                )}
                {r.durationS !== undefined && (
                  <span className="text-sm text-ink-500">
                    {fmtDuration(r.durationS)}
                  </span>
                )}
                {r.failReason && (
                  <span className="max-w-xs truncate text-xs text-red-300/80">
                    {r.failReason}
                  </span>
                )}
                <span className="ml-auto flex items-center gap-2">
                  <span className="text-xs text-ink-600">
                    {timeAgo(r.endedAtMs)}
                  </span>
                  <RerunButton r={r} catalogue={catalogue} />
                  <CopyButton text={r.runId} />
                  <ExpandToggle
                    expanded={expanded}
                    onToggle={() => toggle(id)}
                    label={`run ${r.dungeonName ?? r.dungeon ?? id}`}
                  />
                </span>
              </div>
              {expanded && <RunDetail r={r} />}
            </Card>
          );
        })}
      </div>
    </div>
  );
}

/* Relaunch a finished run with its own parameters — the same dungeon,
 * difficulty, level, gear ceiling and comp seed, which together reproduce the
 * exact party. This is the loop the deck exists for: read a failure, change
 * something in the module, run the identical setup again.
 *
 * It gets a NEW run id; nothing about the old record is touched. The confirm
 * spells out what will actually be launched, because a record's gear ceiling
 * does not always map back onto a ladder choice (see rerunSpec). */
function RerunButton({
  r,
  catalogue,
}: {
  r: RunRecord;
  catalogue: Catalogue | null;
}) {
  const toast = useToast();
  const [busy, setBusy] = useState(false);
  const spec = rerunSpec(r, catalogue);
  if (!spec) return null;

  const name = r.dungeonName ?? r.dungeon ?? "?";
  const where = `${name}${spec.heroic ? " (heroic)" : ""}`;
  /* A roster run's party is the point of it — relaunch those characters, not
   * a rolled comp. Older records without a comp cannot be reproduced. */
  const members = r.roster
    ? ((r.comp ?? []).map((m) => m.name).filter(Boolean) as string[])
    : [];
  if (r.roster && members.length === 0) return null;

  const message = r.roster
    ? `Relaunch the same party at ${where}: ${members.join(", ")}. ` +
      "Every one of them must be offline right now."
    : `Start a new run at ${where} with this run's setup — level ` +
      `${spec.level || "default"}, comp seed ${spec.seed || "random"}, ` +
      `${describeGear(spec)}. It gets its own run id.`;

  const rerun = async () => {
    setBusy(true);
    try {
      if (r.roster) {
        await startRosterRun(spec.dungeon, members, spec.heroic);
        toast("ok", `Roster rerun started at ${name} — see Live`);
      } else {
        const outcome = await startRun(spec);
        toast(
          "ok",
          outcome === "plan"
            ? `Driver was busy — queued as a 1-run plan at ${name}`
            : `Rerun started at ${name} — see Live`,
        );
      }
    } catch (e) {
      toast("error", e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <ConfirmButton
      label={busy ? "starting…" : "rerun"}
      confirmLabel="Start it"
      tone="go"
      message={message}
      className="rounded border border-ink-700 px-1.5 py-0.5 text-[10px] text-ink-500 transition hover:border-iris-600 hover:text-iris-300"
      onConfirm={() => void rerun()}
    />
  );
}

/* The gear ceiling the party was rolled to (schema 9+). Absent on older
 * records and roster runs, which are never geared. */
function GearChip({ r }: { r: RunRecord }) {
  if (r.roster || (!r.gearIlvl && !r.gearQuality)) return null;
  const cap = r.gearIlvl ? `i${r.gearIlvl}` : "i∞";
  const q = QUALITY_NAME[r.gearQuality ?? 0] ?? "";
  return (
    <span
      className="font-mono text-xs text-ink-600"
      title={`gear ceiling: ${r.gearIlvl ? `item level ${r.gearIlvl}` : "no item-level limit"}${q ? `, ${q} quality` : ""}`}
    >
      {cap}
      {q ? ` ${q[0]}` : ""}
    </span>
  );
}

/* What killed the party, on the collapsed row — red for a boss, amber for
 * trash. Scanning a plan's failures for "which boss keeps eating us?" must
 * not need every row expanded. */
function WipeBadge({ r }: { r: RunRecord }) {
  if (!r.wipeOpponent) return null;
  return (
    <span
      className={`rounded px-1.5 py-0.5 text-xs ${
        r.wipeOnBoss
          ? "bg-red-500/15 text-red-300"
          : "bg-amber-500/15 text-amber-300"
      }`}
    >
      💀 {r.wipeOnBoss ? r.wipeOpponent : `trash: ${r.wipeOpponent}`}
    </span>
  );
}

function RunDetail({ r }: { r: RunRecord }) {
  return (
    <div className="space-y-4 border-t border-ink-800/70 px-4 py-4">
      <Line tone="dim">
        <span className="font-mono">{r.runId}</span>{" "}
        <CopyButton text={r.runId} />
        {r.planId && (
          <>
            {" "}
            · <span className="font-mono">{r.planId}</span>{" "}
            <CopyButton text={r.planId} />
          </>
        )}
        {r.mapId !== undefined && <> · map {r.mapId}</>}
        {r.instanceId !== undefined && <> · instance {r.instanceId}</>}
        {r.compSeed ? <> · seed {r.compSeed}</> : null}
        {r.setupStage && (
          <span className="text-amber-300/90"> · failed in {r.setupStage}</span>
        )}
      </Line>

      {r.comp && r.comp.length > 0 && (
        <div className="flex flex-wrap gap-2 text-sm">
          {r.comp.map((m, i) => (
            <span
              key={i}
              className="rounded-lg border border-ink-800 bg-ink-950 px-2 py-1"
            >
              <PlayerName name={m.name} comp={r.comp} />
              <span className="ml-1.5 text-xs text-ink-500">
                {[m.spec, m.level !== undefined ? `lvl ${m.level}` : ""]
                  .filter(Boolean)
                  .join(", ")}
                {m.roleMismatch && (
                  <span className="ml-1 text-amber-400">
                    (detected {m.detectedRole})
                  </span>
                )}
              </span>
            </span>
          ))}
        </div>
      )}

      {r.wipeOpponent && (
        <Section title="Wiped on">
          <Line tone={r.wipeOnBoss ? "warn" : ""}>
            {r.wipeOpponent} — {r.wipeOnBoss ? "boss" : "trash"}
            {r.wipeOpponentEntry ? ` · entry ${r.wipeOpponentEntry}` : ""}
          </Line>
        </Section>
      )}

      <DiagSection r={r} />

      {r.deaths && r.deaths.length > 0 && (
        <Section title={`Deaths (${r.deaths.length})`}>
          {r.deaths.map((d, i) => (
            <Line key={i} tone={d.onBoss ? "warn" : ""}>
              <span className="font-mono text-ink-600">{fmtT(d.t)}</span> ☠{" "}
              <PlayerName name={d.name} comp={r.comp} /> —{" "}
              {d.opponent
                ? `${d.onBoss ? "" : "trash: "}${d.opponent}`
                : "out of combat"}
            </Line>
          ))}
        </Section>
      )}

      {r.pulls && r.pulls.length > 0 && (
        <Section
          title="Pulls"
          note="predicted vs fought; flagged at +2 or where the run ended"
          scroll
        >
          {r.pulls.map((p, i) => (
            <PullLine key={i} p={p} />
          ))}
          {r.pullsElided ? (
            <Line tone="dim">{r.pullsElided} further pulls not logged (cap)</Line>
          ) : null}
        </Section>
      )}

      {r.bossTimeline && r.bossTimeline.length > 0 && (
        <Section title="Kills">
          {r.bossTimeline.map((b, i) => (
            <Line key={i}>
              <span className="font-mono text-ink-600">{fmtT(b.t)}</span> ✦{" "}
              {b.name}
              {b.via === "anchor" ? (
                <span className="text-ink-500"> (objective)</span>
              ) : null}
            </Line>
          ))}
        </Section>
      )}

      {r.pauses && r.pauses.length > 0 && (
        <Section title="Pauses">
          {r.pauses.map((p, i) => (
            <Line key={i} tone="warn">
              <span className="font-mono text-ink-600">{fmtT(p.t)}</span> ⏸{" "}
              {p.reason ?? ""}
            </Line>
          ))}
        </Section>
      )}

      {r.statusTimeline && r.statusTimeline.length > 0 && (
        <Section title={`Status timeline (${r.statusTimeline.length})`} scroll>
          {r.statusTimeline.map((e, i) => (
            <div key={i} className="flex gap-3 font-mono text-xs leading-relaxed">
              <span className="shrink-0 text-ink-600">{fmtT(e.t)}</span>
              <span className="shrink-0 text-amber-300/70">{e.state}</span>
              <span className="text-ink-400">{e.detail}</span>
            </div>
          ))}
        </Section>
      )}
    </div>
  );
}

/* Every pull the Dynamic governor took a verdict on (schema 7+), predicted
 * size against what turned up. Flagged when the two disagree by 2+ bodies —
 * that row, not the total, is where an over-pull actually happened. */
function PullLine({ p }: { p: PullEntry }) {
  const over = (p.observed ?? 0) - (p.predicted ?? 0);
  const tone = p.wipedHere || over >= 2 ? "warn" : "dim";
  return (
    <Line tone={tone}>
      <span className="font-mono text-ink-600">{fmtT(p.t)}</span>{" "}
      {p.advanced ? "⚑" : "→"} entry {p.entry} — predicted {p.predicted ?? "?"},
      fought {p.observed ?? "?"}
      {over > 0 ? ` (+${over})` : ""}
      {p.wipedHere ? " · run ended here" : ""}
    </Line>
  );
}

const fmtPos = (w?: { map?: number; x?: number; y?: number; z?: number }) =>
  w
    ? `${w.map ?? "?"}:${Math.round(w.x ?? 0)},${Math.round(w.y ?? 0)},${Math.round(w.z ?? 0)}`
    : "?";

/* End-of-run diagnostics (schema 3+). Absent on older records and on setup
 * failures, where no party ever existed to describe. */
function DiagSection({ r }: { r: RunRecord }) {
  const d: RunDiag | undefined = r.diag;
  if (!d || !d.valid) return null;
  const t = d.target ?? {};
  const route = d.route ?? {};
  const w = d.watchdogs ?? {};
  const party = d.party ?? {};

  /* The tell for a no_progress verdict on a run that was actually clearing:
   * only mask/anchor completions reset the watchdog. */
  const invisible = (d.roster ?? []).filter((b) => b.doneVia === "bossState");

  return (
    <>
      <Section title="Diagnostics" note={`at ${d.capturedAt ?? "end"}`}>
        <Line>
          <b>state</b> {d.state ?? "?"}
          {d.phase ? ` (${d.phase})` : ""}
          {t.nextName && (
            <>
              {" "}
              · <b>on</b> {t.nextName} @ {Math.round(t.distance ?? 0)}yd
            </>
          )}{" "}
          · <b>at</b> {fmtPos(d.world)} · <b>party</b> {party.alive ?? "?"}/
          {party.size ?? "?"} alive, {party.inCombat ?? 0} in combat
        </Line>
        {d.stallReason && <Line tone="warn">stall: {d.stallReason}</Line>}
        {d.paused && <Line tone="warn">paused: {d.pauseReason ?? "?"}</Line>}
        {w.doorStalled && (
          <Line tone="warn">
            door-stalled {fmtDuration((w.doorStalledForMs ?? 0) / 1000)}
          </Line>
        )}
        {route.reachable === false && (
          <Line tone="warn">
            route unreachable: {route.failureReason || "?"}
          </Line>
        )}
        {t.mismatch && (
          <Line tone="warn">
            target mismatch — next {t.nextEntry}, sticky {t.sticky}, approach{" "}
            {t.approachEntry}
          </Line>
        )}
        {invisible.length > 0 && (
          <Line tone="warn">
            {invisible.length} kill(s) invisible to the no-progress watchdog (
            {invisible.map((b) => b.name).join(", ")})
          </Line>
        )}
        <Line tone="dim">
          route-glide {w.routeGlide ?? 0} · pursuit {w.pursuit ?? 0} ·
          final-approach {w.finalApproach ?? 0} · resnaps{" "}
          {w.resnapAttempts ?? 0} · rebuilds {w.rebuildAttempts ?? 0} · off-path{" "}
          {route.offPathTicks ?? 0} · deviation{" "}
          {route.deviation !== undefined && route.deviation >= 0
            ? route.deviation.toFixed(1)
            : "–"}
        </Line>
      </Section>

      {party.members && party.members.length > 0 && (
        <Section title="Party at end">
          {party.members.map((m, i) => {
            const tone = !m.online || !m.alive ? "fail" : (m.hp ?? 100) < 40 ? "warn" : "dim";
            const state = !m.online
              ? "offline"
              : !m.alive
                ? "dead"
                : `${m.hp}%${m.mp ? `/${m.mp}mp` : ""}`;
            const where =
              (m.distToTank ?? 0) < 0
                ? `map ${m.map}`
                : `${Math.round(m.distToTank ?? 0)}yd`;
            return (
              <Line key={i} tone={tone}>
                <PlayerName name={m.name} comp={r.comp} /> — {state} · {where}
                {m.inCombat ? ` · fighting ${m.victim || "?"}` : ""}
                {m.online && m.dcStrategy === false ? " · NO DC STRATEGY" : ""}
              </Line>
            );
          })}
        </Section>
      )}

      {d.roster && d.roster.length > 0 && (
        <Section title="Boss roster" scroll>
          {d.roster.map((b, i) => (
            <Line key={i} tone={b.status === "dead" ? "dim" : b.isTarget ? "warn" : ""}>
              {b.isTarget ? "▶ " : "　"}
              {b.name ?? "?"} — {b.status}
              {b.doneVia ? ` (${b.doneVia})` : ""}
            </Line>
          ))}
        </Section>
      )}
    </>
  );
}

/* ---- plans ---- */

const pctOf = (n: number | undefined, total: number) => {
  const c = n ?? 0;
  if (!total) return `${c}`;
  return `${c} (${Math.round((100 * c) / total)}%)`;
};

function PlansTab() {
  const { data, error } = usePoll(
    () => api.get<{ plans: PlanRecord[] }>("/api/testplans?limit=50"),
    15000,
  );
  const { session } = useSession();
  const toast = useToast();
  const [open, setOpen] = useState<Set<string>>(new Set());
  const toggle = (id: string) =>
    setOpen((s) => {
      const next = new Set(s);
      if (!next.delete(id)) next.add(id);
      return next;
    });

  if (error)
    return (
      <EmptyState icon="⚠️" title="Cannot reach the server">
        {error}
      </EmptyState>
    );
  if (!data) return <Spinner label="loading plans…" />;
  if (!data.plans.length)
    return (
      <EmptyState icon="🗂️" title="No finished plans yet">
        Campaign summaries land here when a plan completes.
      </EmptyState>
    );

  return (
    <div>
      {session?.admin && (
        <div className="mb-3 flex justify-end">
          <ConfirmButton
            label="Clear history"
            message="Wipe the entire plan history? A one-level .bak backup is kept on the server."
            className="rounded-lg border border-ink-800 px-3 py-1.5 text-xs text-ink-500 hover:border-red-900 hover:text-red-300"
            onConfirm={() => {
              api
                .post<{ cleared?: number }>("/api/testplans/clear")
                .then((r) => toast("ok", `Cleared ${r.cleared ?? 0} records`))
                .catch((e) => toast("error", e.message));
            }}
          />
        </div>
      )}
      <div className="space-y-2">
        {data.plans.map((p, i) => {
          const id = p.planId ?? String(i);
          const expanded = open.has(id);
          const runs = p.runs ?? {};
          const clean = p.result === "completed" && (runs.failed ?? 0) === 0;
          return (
            <Card key={id} className="!p-0">
              <div
                className="flex w-full cursor-pointer flex-wrap items-center gap-x-3 gap-y-1 px-4 py-3 text-left"
                onClick={() => toggle(id)}
              >
                <span
                  className={`inline-block rounded-full border px-2.5 py-0.5 text-xs font-medium ${
                    clean
                      ? "border-emerald-700/50 bg-emerald-500/15 text-emerald-300"
                      : p.result === "completed"
                        ? "border-amber-800/50 bg-amber-500/15 text-amber-300"
                        : "border-red-800/50 bg-red-500/15 text-red-300"
                  }`}
                >
                  {p.result ?? "?"}
                </span>
                <span className="font-medium">
                  {p.dungeonName ?? p.dungeon ?? "?"}
                </span>
                {p.requested?.heroic && (
                  <span className="text-xs text-fuchsia-300">heroic</span>
                )}
                <span className="text-sm text-ink-400">
                  {runs.succeeded ?? 0}/{runs.launched ?? 0} ✓
                </span>
                {p.durationS !== undefined && (
                  <span className="text-sm text-ink-500">
                    {fmtDuration(p.durationS)}
                  </span>
                )}
                {p.abortReason && (
                  <span className="max-w-xs truncate text-xs text-red-300/80">
                    {p.abortReason}
                  </span>
                )}
                <span className="ml-auto flex items-center gap-2">
                  <span className="text-xs text-ink-600">
                    {timeAgo(p.endedAtMs)}
                  </span>
                  <CopyButton text={p.planId} />
                  <ExpandToggle
                    expanded={expanded}
                    onToggle={() => toggle(id)}
                    label={`plan ${p.dungeonName ?? p.dungeon ?? id}`}
                  />
                </span>
              </div>
              {expanded && <PlanDetail p={p} />}
            </Card>
          );
        })}
      </div>
    </div>
  );
}

function PlanDetail({ p }: { p: PlanRecord }) {
  const launched = p.runs?.launched ?? 0;
  const dur = p.duration ?? {};
  const pulls = p.pulls;
  const blind = (pulls?.errorP90 ?? 0) >= 2;
  const sign = (n?: number) => ((n ?? 0) > 0 ? `+${n}` : `${n ?? 0}`);
  return (
    <div className="space-y-4 border-t border-ink-800/70 px-4 py-4">
      <Line tone="dim">
        <span className="font-mono">{p.planId}</span>{" "}
        <CopyButton text={p.planId} /> · requested total{" "}
        {p.requested?.total ?? "?"} · concurrent {p.requested?.concurrent ?? "?"}
        {p.abortReason ? ` · ${p.abortReason}` : ""}
      </Line>

      {p.verdicts && Object.keys(p.verdicts).length > 0 && (
        <Section title="Verdicts">
          {Object.entries(p.verdicts).map(([k, v]) => (
            <Line key={k} tone={k === "success" ? "" : "warn"}>
              {k} × {v}
            </Line>
          ))}
        </Section>
      )}

      {p.bossFunnel && p.bossFunnel.length > 0 && (
        <Section
          title="Bosses"
          note={`of ${launched} run${launched === 1 ? "" : "s"} — killed / wiped`}
        >
          <div className="space-y-1.5">
            {p.bossFunnel.map((f, i) => {
              const killPct = launched ? (100 * (f.killed ?? 0)) / launched : 0;
              const wipePct = launched ? (100 * (f.wiped ?? 0)) / launched : 0;
              return (
                <div key={i} className="flex items-center gap-3 text-sm">
                  <span
                    className="w-48 truncate text-ink-300 sm:w-64"
                    title={f.name}
                  >
                    {f.name}
                  </span>
                  <span className="flex h-2 flex-1 overflow-hidden rounded-full bg-ink-800">
                    <span
                      className="h-full bg-emerald-500"
                      style={{ width: `${killPct}%` }}
                    />
                    <span
                      className="h-full bg-red-500"
                      style={{ width: `${wipePct}%` }}
                    />
                  </span>
                  <span className="w-16 text-right font-mono text-xs text-emerald-300">
                    {pctOf(f.killed, launched)}
                  </span>
                  {f.wiped !== undefined && (
                    <span className="w-14 text-right font-mono text-xs text-red-300">
                      {pctOf(f.wiped, launched)}
                    </span>
                  )}
                </div>
              );
            })}
          </div>
        </Section>
      )}

      {((p.trashWipes && p.trashWipes.length > 0) || p.unattributedWipes) && (
        <Section title="Wiped to trash">
          {(p.trashWipes ?? []).map((t, i) => (
            <Line key={i} tone="warn">
              {t.count} × {t.name}
            </Line>
          ))}
          {p.unattributedWipes ? (
            <Line tone="dim">
              {p.unattributedWipes} × out of combat (fall, hazard, or unengaged)
            </Line>
          ) : null}
        </Section>
      )}

      {pulls && pulls.count ? (
        <Section
          title="Pulls"
          note={`${pulls.count} sampled · ${Math.round((100 * (pulls.advanced ?? 0)) / pulls.count)}% advanced`}
        >
          <Line>
            mobs engaged — median {pulls.observedP50} · p90 {pulls.observedP90}{" "}
            · max {pulls.observedMax}
          </Line>
          <Line>
            vs predicted — median {sign(pulls.errorP50)} · p90{" "}
            {sign(pulls.errorP90)} ·{" "}
            {Math.round((100 * (pulls.underestimated ?? 0)) / pulls.count)}%
            brought more than expected
          </Line>
          {blind ? (
            <Line tone="warn">
              estimate misses mobs — p90 is {sign(pulls.errorP90)} over
              prediction; lowering the ceiling would only mask it
            </Line>
          ) : (
            <Line tone="dim">
              estimate tracks reality — size is set by the ceiling, not by
              blind spots
            </Line>
          )}
          {pulls.wipePulls ? (
            <Line tone="warn">
              {pulls.wipePulls} run{pulls.wipePulls === 1 ? "" : "s"} ended
              mid-pull · biggest was {pulls.wipeObservedMax} mob
              {pulls.wipeObservedMax === 1 ? "" : "s"}
            </Line>
          ) : null}
        </Section>
      ) : null}

      {p.failReasons && p.failReasons.length > 0 && (
        <Section title="Fail reasons">
          {p.failReasons.map((f, i) => (
            <Line key={i} tone="warn">
              {f.count} × {f.reason}
            </Line>
          ))}
        </Section>
      )}

      {p.runs?.succeeded ? (
        <Section title="Duration (successes)">
          <Line>
            min {fmtDuration(dur.minS)} · median {fmtDuration(dur.medianS)} ·
            avg {fmtDuration(dur.avgS)} · max {fmtDuration(dur.maxS)}
          </Line>
        </Section>
      ) : null}

      {p.runIds && p.runIds.length > 0 && (
        <Section title="Runs" scroll>
          <div className="flex flex-wrap gap-x-3 gap-y-1">
            {p.runIds.map((id) => (
              <span key={id} className="font-mono text-xs text-ink-500">
                {id}
              </span>
            ))}
          </div>
        </Section>
      )}
    </div>
  );
}
