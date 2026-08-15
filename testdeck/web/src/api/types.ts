/* API payload shapes.
 *
 * Run records follow the module's ADDITIVE schema (schema 9 at the time of
 * writing): older JSONL lines lack newer keys, so every field beyond identity
 * is optional and renderers show a section only when its key is present —
 * never a zero for a value that was simply not recorded yet.
 */

export interface Session {
  authenticated: boolean;
  username?: string;
  gmlevel?: number;
  accountId?: number;
  admin?: boolean;
}

export interface Status {
  realm: "ONLINE" | "OFFLINE" | "FAILED" | "UNKNOWN" | string;
  since: string;
  statusCheck: string;
  catalogue: boolean;
  liveRuns: number;
  livePlans: number;
  bridge: string;
  health: { problems: { level: string; key: string; message: string }[] };
  version: string;
}

/* ---- catalogue (dc_test_dungeons.json) ---- */

export interface GearChoice {
  ilvl: number;
  label: string;
}

export interface Dungeon {
  token: string;
  name: string;
  mapId: number;
  level: number;
  heroicLevel: number;   // 0 = no heroic mode
  wing: string;
  gear?: GearChoice[];
  gearHeroic?: GearChoice[];
}

export interface Catalogue {
  limits: { maxConcurrent?: number; maxPlans?: number; planMaxTotal?: number };
  gearDefaults?: { ilvl: number; quality: number };
  qualities?: { v: number; label: string }[];
  dungeons: Dungeon[];
}

/* ---- live heartbeat ---- */

export interface BotPos {
  role?: string;               // "tank" / "healer" / "dps"
  cls?: number;                // class id (serialized as "cls" by the module)
  name?: string;
  x?: number;
  y?: number;
  z?: number;
  alive?: boolean;
  hp?: number;
  mp?: number;        // -1 = no mana pool
  inCombat?: boolean;
}

export interface TimelineEntry {
  t?: number;
  state?: string;
  detail?: string;
}

export interface LiveRun {
  runId: string;
  planId?: string;
  dungeon?: string;
  dungeonName?: string;
  stage?: string;
  state?: string;
  level?: number;
  heroic?: boolean;
  elapsedS?: number;
  bossesKilled?: number;
  bossesTotal?: number;
  mapId?: number;
  stall?: string;              // current stall REASON, "" when none
  bossName?: string;
  sinceProgressS?: number;     // age of the no-progress watchdog (verdict ~600s)
  inCombat?: boolean;
  wiped?: boolean;
  wipeOnBoss?: boolean;
  wipeOpponent?: string;
  bots?: BotPos[];
  recent?: TimelineEntry[];
  timeline?: TimelineEntry[];   // server-accumulated full history
}

export interface LivePlan {
  planId: string;
  dungeon?: string;
  total?: number;
  launched?: number;
  succeeded?: number;
  failed?: number;
  activeNow?: number;
  active?: number;
  concurrent?: number;
  heroic?: boolean;
  state?: string;
  elapsedS?: number;
}

export interface Live {
  runs: LiveRun[];
  plans: LivePlan[];
}

/* ---- finished-run history (dc_testruns.jsonl, additive schema 9) ---- */

export interface CompEntry {
  name?: string;
  class?: string;          // lowercase class name ("warrior")
  spec?: string;
  role?: string;
  guid?: number;
  level?: number;
  detectedRole?: string;
  roleMismatch?: boolean;
  from?: { map?: number; x?: number; y?: number; z?: number; o?: number };
}

export interface BossKill {
  t?: number;
  entry?: number;
  name?: string;
  via?: string;            // "mask" | "anchor" | ...
}

export interface DeathEntry {
  t?: number;
  name?: string;
  opponent?: string;
  opponentEntry?: number;
  onBoss?: boolean;
}

export interface PullEntry {
  t?: number;
  entry?: number;
  predicted?: number;
  predictedThirds?: number;
  ceilingThirds?: number;
  observed?: number;
  observedElites?: number;
  advanced?: boolean;
  wipedHere?: boolean;
}

export interface DiagMember {
  name?: string;
  guid?: number;
  level?: number;
  bot?: boolean;
  online?: boolean;
  map?: number;
  distToTank?: number;
  alive?: boolean;
  hp?: number;
  mp?: number;
  inCombat?: boolean;
  victim?: string;
  dcStrategy?: boolean;
  dcCombatStrategy?: boolean;
}

export interface DiagRosterRow {
  entry?: number;
  order?: number;
  name?: string;
  kind?: string;           // "boss" | ...
  status?: string;         // "dead" | "alive" | ...
  doneVia?: string;        // "mask" | "anchor" | "bossState"
  isTarget?: boolean;
  isSticky?: boolean;
}

export interface RunDiag {
  valid?: boolean;
  capturedAt?: string;
  enabled?: boolean;
  paused?: boolean;
  pauseReason?: string;
  pausedAtDoor?: boolean;
  phase?: string;
  state?: string;
  detail?: string;
  stallReason?: string;
  target?: {
    sticky?: number;
    nextEntry?: number;
    nextName?: string;
    approachEntry?: number;
    distance?: number;
    mismatch?: boolean;
  };
  route?: {
    reachable?: boolean;
    failureReason?: string;
    offPathTicks?: number;
    deviation?: number;
  };
  watchdogs?: {
    routeGlide?: number;
    pursuit?: number;
    finalApproach?: number;
    resnapAttempts?: number;
    rebuildAttempts?: number;
    doorStalled?: boolean;
    doorStalledForMs?: number;
  };
  world?: {
    map?: number;
    instance?: number;
    x?: number;
    y?: number;
    z?: number;
    inCombat?: boolean;
    victim?: string;
  };
  party?: {
    size?: number;
    alive?: number;
    offline?: number;
    inCombat?: number;
    members?: DiagMember[];
  };
  roster?: DiagRosterRow[];
}

export interface RunRecord {
  schema?: number;
  runId?: string;
  planId?: string;
  dungeon?: string;
  dungeonName?: string;
  wing?: string;
  mapId?: number;
  instanceId?: number;
  level?: number;
  heroic?: boolean;
  compSeed?: number;
  gearIlvl?: number;
  gearQuality?: number;
  roster?: boolean;
  startedAtMs?: number;
  endedAtMs?: number;
  durationS?: number;
  result?: string;
  failReason?: string;
  disableReason?: string;
  bossesTotal?: number;
  bossesKilled?: number;
  setupStage?: string;
  stallAtEnd?: string;
  phaseAtEnd?: string;
  comp?: CompEntry[];
  bossRoster?: string[];
  bossTimeline?: BossKill[];
  deaths?: DeathEntry[];
  pulls?: PullEntry[];
  pullsElided?: number;
  statusTimeline?: TimelineEntry[];
  pauses?: { t?: number; reason?: string }[];
  wipeOnBoss?: boolean;
  wipeOpponent?: string;
  wipeOpponentEntry?: number;
  diag?: RunDiag;
  [k: string]: unknown;
}

/* ---- plan summaries (dc_testplans.jsonl, schema 5) ---- */

export interface PlanRecord {
  schema?: number;
  planId?: string;
  dungeon?: string;
  dungeonName?: string;
  requested?: {
    total?: number;
    concurrent?: number;
    level?: number;
    heroic?: boolean;
    seedBase?: number;
    gearIlvl?: number;
    gearQuality?: number;
  };
  startedAtMs?: number;
  endedAtMs?: number;
  durationS?: number;
  result?: string;         // "completed" | "aborted" | ...
  abortReason?: string;
  runs?: { launched?: number; succeeded?: number; failed?: number };
  verdicts?: Record<string, number>;
  failReasons?: { reason?: string; count?: number }[];
  duration?: { minS?: number; avgS?: number; medianS?: number; maxS?: number };
  bossFunnel?: { name?: string; killed?: number; wiped?: number }[];
  trashWipes?: { name?: string; count?: number }[];
  unattributedWipes?: number;
  pulls?: {
    count?: number;
    advanced?: number;
    underestimated?: number;
    observedP50?: number;
    observedP90?: number;
    observedMax?: number;
    errorP50?: number;
    errorP90?: number;
    wipePulls?: number;
    wipeObservedMax?: number;
  };
  runIds?: string[];
  [k: string]: unknown;
}

/* legacy: the plan record's heroic flag lives under requested */

/* ---- roster ---- */

export interface Account {
  id: number;
  username: string;
  chars: number;
  online: number;
  instancesLeft: number;
  mine: boolean;
}

export interface Character {
  guid: number;
  name: string;
  level: number;
  cls: number;
  race: number;
  faction: "alliance" | "horde" | "unknown";
  online: boolean;
  accountId: number;
  account: string;
  guild: string;
  instancesLeft: number;
  spec: string;
  specPoints: number[];
  /* Whether THIS tester may draft it: their own account's, or they are an
   * admin. start-roster enforces the same rule server-side. */
  owned: boolean;
}

export interface SavedRoster {
  name: string;
  members: string[];
  owner: string;
  mine: boolean;
  writable: boolean;
}

/* ---- command replies ---- */

export interface CommandReply {
  ok: boolean;
  exact: boolean;
  cmd: string;
  pending: boolean;
  reply: string[];
  roles?: Record<string, string>;
}

export interface LogFile {
  name: string;
  size: number;
  mtime: number;
}
