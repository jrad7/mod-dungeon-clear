/* Starting a run is the one call with real protocol to it, so it lives here
 * rather than in a page: the worldserver may answer that the headless test
 * driver is still logging in, in which case NOTHING was started and the reply
 * carries pending=true. Retry a couple of times, then fall back to a plan of
 * one — plans wait the driver out server-side.
 *
 * Shared by the Launch drawer and History's rerun button so a run started
 * from either place behaves identically. */

import { api, ApiError } from "./client";
import type { Catalogue, CommandReply, RunRecord } from "./types";

const PENDING_RETRIES = 2;
const PENDING_DELAY_MS = 4000;

export interface RunSpec {
  dungeon: string;
  heroic: boolean;
  level: number;    // 0 = the dungeon's recommended level
  seed: number;     // 0 = roll a comp
  ilvl: number;     // 0 = server conf, -1 = no cap, >0 = that item level
  quality: number;  // 0 = server conf, else 1..5
}

/* "run" = started outright; "plan" = queued as a 1-run plan because the
 * driver was still logging in. */
export type LaunchOutcome = "run" | "plan";

function throwIfRefused(r: CommandReply) {
  if (!r.ok && r.reply.length) throw new ApiError(500, r.reply.join(" "));
}

export async function startRun(
  spec: RunSpec,
  onWait?: (attempt: number, of: number) => void,
): Promise<LaunchOutcome> {
  for (let attempt = 0; ; attempt++) {
    const r = await api.post<CommandReply>("/api/testruns/start", spec);
    if (!r.pending) {
      throwIfRefused(r);
      return "run";
    }
    if (attempt >= PENDING_RETRIES) {
      const p = await api.post<CommandReply>("/api/testplans/start", {
        ...spec,
        total: 1,
        concurrent: 1,
      });
      throwIfRefused(p);
      return "plan";
    }
    onWait?.(attempt + 1, PENDING_RETRIES);
    await new Promise((res) => setTimeout(res, PENDING_DELAY_MS));
  }
}

export async function startRosterRun(
  dungeon: string,
  members: string[],
  heroic: boolean,
): Promise<void> {
  const r = await api.post<CommandReply>("/api/testruns/start-roster", {
    dungeon,
    members,
    heroic,
  });
  /* No plan fallback here: a plan rolls its own bot comp, which is the one
   * thing a roster run exists NOT to do. */
  if (r.pending) throw new ApiError(503, r.reply.join(" "));
  throwIfRefused(r);
}

/* ---- reproducing a finished run ---- */

/* Records store RESOLVED values; the start API takes REQUESTED ones, and the
 * two spell gear differently. On a record gearIlvl 0 means "no ceiling was
 * applied", which the API writes as -1 (0 there means "inherit the conf").
 *
 * The API also only accepts an item level that is on this dungeon's curated
 * ladder, so a ceiling that came from AiPlayerbot.AutoGearScoreLimit — never
 * on the ladder — falls back to "server default" and resolves the same way
 * again. The confirm text shows whatever this returns, so a fallback is
 * visible before anything launches.
 *
 * level and compSeed are already resolved and reproduce the run's exact comp:
 * that is what the module records the seed for. */
export function rerunSpec(r: RunRecord, catalogue: Catalogue | null): RunSpec | null {
  if (!r.dungeon) return null;
  const d = catalogue?.dungeons?.find((x) => x.token === r.dungeon);
  const heroic = !!r.heroic;
  const ladder = (heroic ? d?.gearHeroic : d?.gear) ?? d?.gear ?? [];

  let ilvl = 0;
  if (r.gearIlvl === 0) ilvl = -1;
  else if (r.gearIlvl && ladder.some((g) => g.ilvl === r.gearIlvl)) ilvl = r.gearIlvl;

  return {
    dungeon: r.dungeon,
    heroic,
    level: r.level ?? 0,
    seed: r.compSeed ?? 0,
    ilvl,
    quality: r.gearQuality ?? 0,
  };
}

export function describeGear(spec: RunSpec): string {
  if (spec.ilvl === -1) return "uncapped gear";
  if (spec.ilvl === 0) return "server-default gear";
  return `gear ilvl ${spec.ilvl}`;
}
