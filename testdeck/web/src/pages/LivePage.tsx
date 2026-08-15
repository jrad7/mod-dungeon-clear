/* Live monitoring: one card per active run (party chips, boss progress, the
 * full accumulated timeline), one per active plan. Polls /api/testruns/live
 * every 3s — the heartbeat itself rewrites every ~2s. */

import { useEffect, useRef, useState } from "react";
import { api } from "../api/client";
import { fmtDuration, usePoll } from "../api/hooks";
import type { CommandReply, Live, LivePlan, LiveRun } from "../api/types";
import { ROLE_ORDER } from "../data/wow";
import {
  Card,
  ClassChip,
  ConfirmButton,
  CopyButton,
  EmptyState,
  HpBar,
  Spinner,
  useToast,
} from "../components/ui";

export default function LivePage() {
  const { data, error } = usePoll(() => api.get<Live>("/api/testruns/live"), 3000);

  if (error)
    return (
      <EmptyState icon="⚠️" title="Cannot reach the server">
        {error}
      </EmptyState>
    );
  if (!data) return <Spinner label="loading…" />;

  const idle = !data.runs.length && !data.plans.length;

  return (
    <div>
      <div className="mb-6 flex items-end justify-between">
        <div>
          <h1 className="text-2xl font-semibold">Live</h1>
          <p className="mt-1 text-sm text-ink-400">
            {idle
              ? "Nothing running right now."
              : `${data.runs.length} run${data.runs.length === 1 ? "" : "s"}, ${data.plans.length} plan${data.plans.length === 1 ? "" : "s"} active.`}
          </p>
        </div>
        {!idle && <StopAllButton />}
      </div>

      {idle && (
        <EmptyState icon="🌙" title="No active test runs">
          Start one from the Launch page — it will appear here within a few
          seconds.
        </EmptyState>
      )}

      <div className="space-y-4">
        {data.plans.map((p) => (
          <PlanCard key={p.planId} plan={p} />
        ))}
        {data.runs.map((r) => (
          <RunCard key={r.runId} run={r} />
        ))}
      </div>
    </div>
  );
}

function StopAllButton() {
  const toast = useToast();
  return (
    <ConfirmButton
      label="Stop everything"
      confirmLabel="Stop all"
      message="Stop every active plan and run? Plans are stopped first so the scheduler cannot relaunch."
      className="rounded-lg border border-red-900/60 bg-red-950/40 px-3 py-1.5 text-sm text-red-300 hover:bg-red-900/40"
      onConfirm={() => {
        api
          .post<CommandReply>("/api/testruns/stop", { runId: "all" })
          .then(() => toast("ok", "Stop issued for everything"))
          .catch((e) => toast("error", e.message));
      }}
    />
  );
}

function PlanCard({ plan }: { plan: LivePlan }) {
  const toast = useToast();
  const done = (plan.succeeded ?? 0) + (plan.failed ?? 0);
  const total = plan.total ?? 0;
  const pct = total ? Math.round((done / total) * 100) : 0;
  return (
    <Card>
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <span className="mr-2 rounded bg-iris-500/15 px-2 py-0.5 text-xs font-medium text-iris-300">
            PLAN
          </span>
          <span className="font-medium">{plan.dungeon}</span>
          {plan.heroic && (
            <span className="ml-2 text-xs text-fuchsia-300">heroic</span>
          )}
          <span className="ml-3 font-mono text-xs text-ink-500">
            {plan.planId}
          </span>{" "}
          <CopyButton text={plan.planId} />
        </div>
        <div className="flex items-center gap-4 text-sm text-ink-400">
          <span>{fmtDuration(plan.elapsedS)}</span>
          <span className="text-ink-300">
            {done}/{total || "?"}
          </span>
          <span className="text-emerald-300">{plan.succeeded ?? 0} ok</span>
          <span className="text-red-300">{plan.failed ?? 0} failed</span>
          <ConfirmButton
            label="Stop plan"
            message={`Stop plan ${plan.planId}? Runs already in flight finish or are stopped; no new ones launch.`}
            className="rounded-lg border border-ink-700 px-2.5 py-1 text-xs text-ink-300 hover:border-red-800 hover:text-red-300"
            onConfirm={() => {
              api
                .post("/api/testplans/stop", { planId: plan.planId })
                .then(() => toast("ok", `Stop issued for ${plan.planId}`))
                .catch((e) => toast("error", e.message));
            }}
          />
        </div>
      </div>
      <div className="mt-3 h-2 overflow-hidden rounded-full bg-ink-800">
        <div
          className="h-full rounded-full bg-iris-500 transition-all"
          style={{ width: `${pct}%` }}
        />
      </div>
      {plan.state && plan.state !== "running" && (
        <div className="mt-2 text-xs text-amber-300/90">{plan.state}</div>
      )}
    </Card>
  );
}

/* The state that matters most: sinceProgressS is the module's no-progress
 * watchdog — it resets on every boss/objective completion and KILLS the run
 * with a no_progress verdict when it hits the timeout (default 600s). A run
 * quietly not clearing is exactly what these cards exist to catch, so once
 * the counter is past this threshold the whole card goes red. */
const NO_PROGRESS_ALERT_S = 120;

function RunCard({ run }: { run: LiveRun }) {
  const toast = useToast();
  const bots = [...(run.bots ?? [])].sort(
    (a, b) =>
      (ROLE_ORDER[a.role ?? "dps"] ?? 3) - (ROLE_ORDER[b.role ?? "dps"] ?? 3),
  );
  const bosses =
    run.bossesTotal !== undefined
      ? `${run.bossesKilled ?? 0}/${run.bossesTotal} bosses`
      : null;
  const noProgress = (run.sinceProgressS ?? 0) >= NO_PROGRESS_ALERT_S;

  return (
    <Card
      className={
        noProgress
          ? "border-red-600/80 ring-1 ring-red-500/30"
          : run.stall
            ? "border-amber-800/60"
            : ""
      }
    >
      {noProgress && (
        <div className="mb-3 flex items-center gap-2 rounded-lg border border-red-800/60 bg-red-950/40 px-3 py-2 text-sm text-red-200">
          <span className="h-2 w-2 animate-pulse rounded-full bg-red-400" />
          <b>NO PROGRESS</b> for {fmtDuration(run.sinceProgressS)}
          {run.bossName ? <span> — stuck on {run.bossName}</span> : null}
          {run.stall ? (
            <span className="text-red-300/80">· {run.stall}</span>
          ) : null}
        </div>
      )}
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <span className="font-medium">
            {run.dungeonName ?? run.dungeon ?? "?"}
          </span>
          {run.heroic && (
            <span className="ml-2 text-xs text-fuchsia-300">heroic</span>
          )}
          <span className="ml-3 font-mono text-xs text-ink-500">
            {run.runId}
          </span>{" "}
          <CopyButton text={run.runId} />
          {run.planId && (
            <span className="ml-2 font-mono text-xs text-iris-400/70">
              {run.planId}
            </span>
          )}
        </div>
        <div className="flex items-center gap-3 text-sm">
          {run.inCombat && (
            <span className="rounded bg-red-500/15 px-2 py-0.5 text-xs text-red-300">
              in combat
            </span>
          )}
          {run.stall && !noProgress && (
            <span
              className="rounded bg-amber-500/15 px-2 py-0.5 text-xs text-amber-300"
              title={run.stall}
            >
              stall: {run.stall}
            </span>
          )}
          <span className="text-ink-400">{fmtDuration(run.elapsedS)}</span>
          <ConfirmButton
            label="Stop"
            message={
              run.planId
                ? `Stop run ${run.runId}? Its plan keeps going and will launch a replacement.`
                : `Stop run ${run.runId}?`
            }
            className="rounded-lg border border-ink-700 px-2.5 py-1 text-xs text-ink-300 hover:border-red-800 hover:text-red-300"
            onConfirm={() => {
              api
                .post("/api/testruns/stop", { runId: run.runId })
                .then(() => toast("ok", `Stop issued for ${run.runId}`))
                .catch((e) => toast("error", e.message));
            }}
          />
        </div>
      </div>

      <div className="mt-3 flex flex-wrap items-center gap-2 text-sm text-ink-400">
        <StageBadge stage={run.stage} />

        {bosses && <span>{bosses}</span>}
        {run.bossName && (
          <span className="text-ink-300">→ {run.bossName}</span>
        )}
        {run.wiped && (
          <span className="rounded bg-red-500/15 px-2 py-0.5 text-xs text-red-300">
            wiped{run.wipeOnBoss ? ` on ${run.wipeOnBoss}` : ""}
            {run.wipeOpponent ? ` (${run.wipeOpponent})` : ""}
          </span>
        )}
      </div>

      {run.bossesTotal ? (
        <div className="mt-3 h-1.5 overflow-hidden rounded-full bg-ink-800">
          <div
            className="h-full rounded-full bg-emerald-500 transition-all"
            style={{
              width: `${Math.round(((run.bossesKilled ?? 0) / run.bossesTotal) * 100)}%`,
            }}
          />
        </div>
      ) : null}

      {bots.length > 0 && (
        <div className="mt-4 flex flex-wrap gap-x-4 gap-y-2">
          {bots.map((b, i) => (
            <div key={i} className="flex items-center gap-2">
              <ClassChip classId={b.cls} name={b.name} dead={b.alive === false} />
              <HpBar hp={b.hp} mp={b.mp} />
              {b.role && (
                <span className="text-[10px] uppercase tracking-wide text-ink-600">
                  {b.role}
                </span>
              )}
            </div>
          ))}
        </div>
      )}

      {run.timeline && run.timeline.length > 0 && (
        <TimelineFeed entries={run.timeline} startedS={run.elapsedS} />
      )}
    </Card>
  );
}

/* The job's stage. Setup stages (spawning_bots, provisioning, grouping,
 * teleporting, starting, tearing_down) are steps that visibly tick past, so
 * they read as a plain chip. "monitoring" is the whole run — everything after
 * setup until the verdict — and a static grey box there looked like a frozen
 * label rather than a party working through a dungeon. */
function StageBadge({ stage }: { stage?: string }) {
  if (!stage) return null;
  if (stage === "monitoring") return <Spinner label="Running…" />;
  return (
    <span className="rounded bg-ink-800 px-2 py-0.5 text-xs">{stage}</span>
  );
}

function TimelineFeed({
  entries,
  startedS,
}: {
  entries: { t?: number; state?: string; detail?: string }[];
  startedS?: number;
}) {
  /* Collapsed by default: with several runs up, four open logs push the party
   * chips and boss progress — the reason to glance at this page — off screen.
   * The entry count on the toggle is the at-a-glance part. */
  const [open, setOpen] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  const pinned = useRef(true);

  /* Stay pinned to the newest entry unless the reader scrolled up. Opening
   * counts: the box mounts at scrollTop 0, i.e. showing the oldest lines. */
  useEffect(() => {
    const el = box.current;
    if (el && pinned.current) el.scrollTop = el.scrollHeight;
  }, [entries.length, open]);

  /* The heartbeat's `t` is unix seconds; show mm:ss into the run when we can
   * anchor it, else the wall clock. */
  const t0 =
    startedS !== undefined && entries[entries.length - 1]?.t
      ? (entries[entries.length - 1]!.t as number) - startedS
      : null;

  return (
    <div className="mt-4">
      <button
        onClick={() => {
          pinned.current = true;   // reopen at the newest entry, not the oldest
          setOpen(!open);
        }}
        className="text-xs text-ink-500 hover:text-ink-300"
      >
        {open ? "▾" : "▸"} timeline ({entries.length})
      </button>
      {open && (
        <div
          ref={box}
          onScroll={(e) => {
            const el = e.currentTarget;
            pinned.current =
              el.scrollHeight - el.scrollTop - el.clientHeight < 24;
          }}
          className="mt-2 max-h-48 overflow-y-auto rounded-lg border border-ink-800 bg-ink-950/70 p-3 font-mono text-xs leading-relaxed"
        >
          {entries.map((e, i) => (
            <div key={i} className="flex gap-3">
              <span className="shrink-0 text-ink-600">
                {e.t
                  ? t0 !== null
                    ? fmtDuration(e.t - t0)
                    : new Date(e.t * 1000).toLocaleTimeString()
                  : "·"}
              </span>
              <span className="shrink-0 text-amber-300/80">{e.state}</span>
              <span className="text-ink-400">{e.detail}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
