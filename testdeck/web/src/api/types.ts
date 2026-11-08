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
  /* The running continuous session, if any — just enough for the nav badge
     and the tab-title failure count. */
  soak?: { soakId: string; status: string; runs: number; fail: number } | null;
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
  /* Map.dbc's expansion id (0 classic, 1 TBC, 2 WotLK), straight from the
   * module. Optional only because an older worldserver's sidecar predates the
   * field — see expansionOfRow(). */
  expansion?: number;
  level: number;
  heroicLevel: number;   // 0 = no heroic mode
  wing: string;
  /* Raid rows only (raid-support Plan D): the launch form offers the party
   * size within [sizeMin, sizeMax], defaulting to defaultSize. Absent on
   * 5-man rows. */
  raid?: boolean;
  sizeMin?: number;
  sizeMax?: number;
  sizePresets?: number[];
  defaultSize?: number;
  gear?: GearChoice[];
  gearHeroic?: GearChoice[];
  /* SCENARIO rows (module T1): a slice of a parent dungeon — dropped at an
   * in-map point, scoped to `focus`, passing on its own `success` predicate.
   * Launched by its own token; the form locks the size to the parent's
   * default and never offers heroic. Absent on every ordinary row. */
  scenario?: boolean;
  scenarioOf?: string;
  focus?: number[];
  success?: string;          // "instanceData(9)==3", "" = all-cleared only
  successGraceS?: number;
  overallTimeoutS?: number;  // 0 = the server's global budget
  noProgressS?: number;
}

export interface Catalogue {
  limits: {
    maxConcurrent?: number;
    maxPlans?: number;
    planMaxTotal?: number;
    planPool?: boolean;      // server takes `.dc test plan start pool=…`
  };
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
  loot?: LootEntry[];           // rare-or-better drops so far
}

/* ---- dungeon map pack (generated per host, see `testdeck mappack`) ---- */

export interface MapFloor {
  floor: number;
  url: string;                 // /api/mappack/img/<mapId>/<floor>
  /* World rect this image covers. minX/maxX are the world X (north) axis and
   * drive the VERTICAL image axis; minY/maxY are world Y (west) and drive the
   * horizontal one. The DBC's own field order is misleading; the pack has
   * already normalised it. */
  minX: number;
  maxX: number;
  minY: number;
  maxY: number;
  /* px = ax*x + bx*y + cx ; py = ay*x + by*y + cy, in a w x h image. */
  ax: number; bx: number; cx: number;
  ay: number; by: number; cy: number;
  w: number;
  h: number;
  source?: string;             // "wdm" | "client"
}

export interface MapPackMap {
  mapId: number;
  name?: string;
  kind?: "dungeon" | "terrain";
  floors: MapFloor[];
  /* Blizzard's own DungeonMapChunk MinZ ladder, ascending. Only ever used to
   * break a tie between floors whose rects both contain the position. */
  floorRule?: {
    default?: number;
    zsteps: { minZ: number; floor: number }[];
  };
}

export interface MapPackIndex {
  available: boolean;
  reason?: string;
  generated?: string;
  maps: number[];
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
  /* Pool (continuous) plans. */
  endless?: boolean;
  paused?: boolean;
  nextPick?: string;
  resetHoldUntil?: number;     // unix s; 0 = no reset guard active
  pool?: { token: string; heroic?: boolean; size?: number; launched?: number; ok?: number; fail?: number }[];
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

/* schema 14: one rare-or-better item a party member received. `roll` is
 * "need" | "greed" for a won group-loot roll, "" for a direct pickup. */
export interface LootEntry {
  t?: number;
  name?: string;
  itemId?: number;
  item?: string;
  quality?: number;
  count?: number;
  roll?: string;
  source?: string;
  sourceEntry?: number;
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
  size?: number;         // party size fielded (schema 11+; 0/absent = 5-man)
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
  loot?: LootEntry[];
  pulls?: PullEntry[];
  pullsElided?: number;
  statusTimeline?: TimelineEntry[];
  pauses?: { t?: number; reason?: string }[];
  wipeOnBoss?: boolean;
  wipeOpponent?: string;
  wipeOpponentEntry?: number;
  diag?: RunDiag;
  /* schema 13: the scenario block. `extras` is the flat key/value map the
   * running event published (chess: attempts, losses, gameTimeS, …) — present
   * (possibly empty) on every schema-13 record. */
  scenario?: string;
  scenarioOf?: string;
  focus?: number[];
  successPredicate?: string;
  successBy?: string;        // "allCleared" | "predicate" | "grace"
  tailPending?: boolean;
  extras?: Record<string, string | number>;
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

/* ---- continuous mode (soak) ---- */

export interface SoakPoolEntry {
  token: string;
  heroic: boolean;
}

export interface SoakConfig {
  pool: SoakPoolEntry[];
  concurrent: number;
  pick: "bag" | "random";
  level: number;
  seed: number;
  ilvl: number;
  quality: number;
  autoResume: boolean;
  breaker: number;
}

export interface SoakLiveRun {
  runId: string;
  dungeon?: string;
  dungeonName?: string;
  heroic?: boolean;
  stage?: string;
  state?: string;
  bossName?: string;
  bossesKilled?: number;
  bossesTotal?: number;
  elapsedS?: number;
  stall?: string;
  inCombat?: boolean;
  wiped?: boolean;
}

export interface SoakPerDungeon {
  key: string;
  dungeon: string;
  dungeonName: string;
  heroic: boolean;
  runs: number;
  ok: number;
  fail: number;
  medianS: number;
  last: string[];
}

export interface SoakCluster {
  label: string;
  count: number;
  result: string;
  sample: string;
  where: { key: string; count: number }[];
}

export interface SoakStats {
  perDungeon: SoakPerDungeon[];
  clusters: SoakCluster[];
  runs: number;
  ok: number;
  fail: number;
  lost: number;
  runsPerHour: number;
  elapsedS: number;
  diskBytes: number;
}

export interface SoakView {
  soakId: string;
  owner: string;
  status: string;
  statusDetail: string;
  createdAtMs: number;
  stoppedAtMs?: number | null;
  config: SoakConfig;
  planIds: string[];
  currentPlanId: string;
  restarts: number;
  runs: number;
  ok: number;
  fail: number;
  lost: number;
  plan: LivePlan | null;
  liveRuns: SoakLiveRun[];
  stopMode: string;
  stats?: SoakStats;
}

export interface SoakIndex {
  active: SoakView | null;
  recent: SoakView[];
  supported: boolean;
  evidenceTool: boolean;
  me: string;
  admin: boolean;
  addclassPool: number | null;  // characters in the playerbots addclass pool; null = unknown
}

export interface SoakRunRow extends RunRecord {
  lost?: boolean;
  evidence?: "done" | "failed" | "pending" | "queued";
}

export interface SoakRunsPage {
  runs: SoakRunRow[];
  total: number;
  nextCursor: number | null;
}

export interface SoakEvidence {
  state: "done" | "failed" | "pending" | "queued";
  report: string;
  error: string;
  files: { name: string; size: number }[];
}

export interface SoakPreset {
  name: string;
  pool: SoakPoolEntry[];
  owner: string;
  writable: boolean;
}
