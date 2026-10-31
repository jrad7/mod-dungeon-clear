# mod-dungeon-clear — AzerothCore module build registration.
#
# Sources are auto-collected from src/ (recursively) by AzerothCore's module
# system; AC_ADD_SCRIPT registers the script-loader path. The module compiles
# into the shared `modules` static library alongside mod-playerbots, so its
# subclasses of the playerbots engine classes link directly.
#
# REQUIRES mod-playerbots to be present and enabled — this is a playerbots
# extension, not a standalone module.
#
# NOTE: sources are GLOBed at configure time (no CONFIGURE_DEPENDS), so adding
# or removing a .cpp requires CMake to re-run. Editing this file bumps its
# mtime, which CMake treats as a configure dependency and reconfigures on the
# next build — the reliable way to force a re-glob after the file set changes.

AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src")

# New files added: Data/HealLeashRegistry.{h,cpp}
#   (per-map camp leash for the healer LOS reposition — Karazhan's Banquet Hall,
#    where healers chasing DPS onto the ballroom floor aggroed idle packs.)

# New files added: Overrides/KarazhanChessDriver.cpp, Action/DcChessPieceAction.cpp,
#   Util/DcChessBoard.h, Util/DcChessDecision.h, Util/DcChessConductor.h,
#   Util/DcAnchorDone.h
#   (Karazhan's chess event, map 532 — a conductor on the run owner that starts
#    the game, hands out the ten pieces, plays them through a pure policy and
#    loots the chest; a per-member rung that takes and holds each bot's piece.)

# New files added: Data/Events/OculusEvents.cpp, Overrides/OculusDriver.cpp,
#   Action/DcOculusRiderAction.cpp, Util/DcFlightLeg.{h,cpp}, Util/DcOculusPlan.h,
#   Util/DcOculusDriverDecision.h, Util/DcOculusFlightDecision.h
#   (The Oculus, map 578 — four rings of navmesh islands in open air, reached by
#    player-owned flying drakes. A portal objective, six island clears and an
#    Eregos objective; a conditional flight driver, hook 38, that musters, flies
#    and lands the party; and a per-member rider rung that gets each bot its
#    essence, mounts it, flies its drake on a lane and steps off over mesh. The
#    first thing in the module that moves anything through the air.)

# New files added: Data/Events/TrialOfTheChampionEvents.cpp,
#   Overrides/TrialOfTheChampionDriver.cpp, Util/DcTocDriverDecision.h
#   (Trial of the Champion, map 650 — greenfield before this: every run failed at
#    setup with "no boss roster for this map", because all three encounters are
#    cast-spell credits on script summons and two of the bosses never die. Three
#    objectives held on the rewinding DATA_INSTANCE_PROGRESS counter, and one
#    conditional driver, hook 37, that musters the mounted party, clicks the
#    announcer from horseback and pulls the Argent side packs.)

# New files added: Data/Events/CullingOfStratholmeEvents.cpp,
#   Overrides/CullingOfStratholmeDriver.cpp, Util/DcCosWaveDecision.h
#   (The Culling of Stratholme, map 595 — greenfield before this: no roster, no
#    events, and every run failing at setup with "no boss roster for this map",
#    because all four encounters are script TempSummons with no `creature` row for
#    BossSpawnIndex's join to find. Seven objectives plus a HEROIC eighth for the
#    Infinite Corruptor; three Arthas escort legs driven entirely off the monotonic
#    DATA_ARTHAS_EVENT counter and his gossip flag; and one conditional controller
#    for the ten waves, whose four spawn clusters are up to 264yd apart with no
#    movement script, so the party must be walked to each of them. Also adds the
#    UseItemAt event step for the five plagued grain crates — see
#    Data/DungeonEventRegistry.h.)

AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/mod_dungeon_clear.conf.dist")

# New files added: Action/DcLyceumCloseUpAction.cpp, Util/DcLyceumCloseUpDecision.h
#   (BRD Lyceum: in a fight inside the room every member stands within 15yd of
#    the tank — the combat-engine rung that walks a member back, plus the
#    multiplier's veto on stock chase toward a target outside that radius.)
# New files added: Util/DcDungeonAccess.{h,cpp}
#   (making a throwaway pool bot ELIGIBLE for the destination. Player::TeleportTo
#    refuses a far teleport silently in two places — the
#    dungeon_access_requirements attunement rows (Pit of Saron and Halls of
#    Reflection chain off each other, every TBC heroic wants its key
#    achievement) and the death-knight Acherus lock — and both refusals surface
#    only as a setup stage timing out. This satisfies each gate (quest marked
#    rewarded, achievement/item granted, Death Gate learned) instead of
#    bypassing PlayerCannotEnter wholesale.)
# New files added: Util/DcBotProvisioning.{h,cpp}
#   (the order-sensitive PlayerbotFactory sequence — strip, Randomize at the
#    wanted level, force the spec and re-gear/re-glyph/re-enchant for it, pet,
#    ammo, ResetStrategies — lifted verbatim out of DcTestRunJob so the RDF
#    queue fill rolls its bots through exactly the same pass. Three orderings
#    in it each cost a live run to find; a second copy would drift.)
# New files added: DungeonQueueFill/DcDungeonQueueFillPlanner.{h,cpp},
#   DungeonQueueFill/DcDungeonQueueFillJob.{h,cpp},
#   DungeonQueueFill/DcDungeonQueueFillManager.{h,cpp},
#   DungeonQueueFill/DcDungeonQueueFillHooks.cpp
#   (RDF INSTANT QUEUE FILL. A real player clicks Find Group; bots with the
#    roles the party is missing queue for the same dungeon through the same
#    queue, and the STOCK matchmaker pairs them — the player gets the ordinary
#    Dungeon Found popup. No core change: PLAYERHOOK_CAN_JOIN_LFG is a pure
#    observer that always returns true, and the job reads the AUTHORITATIVE
#    post-validation dungeon set and roles back out of LFGMgr only once the
#    queue has settled into LFG_STATE_QUEUED, so RBAC/deserter/cooldown/lock
#    checks have all already run. The planner half is engine-free and gtested.)
# New files added: BgQueueFill/DcBgQueueFillPlanner.{h,cpp},
#   BgQueueFill/DcBgQueueFillJob.{h,cpp},
#   BgQueueFill/DcBgQueueFillManager.{h,cpp},
#   BgQueueFill/DcBgQueueFillHooks.cpp
#   (BATTLEGROUND INSTANT QUEUE FILL. A real player queues for a battleground;
#    Alliance AND Horde pool bots in the player's bracket queue for the same
#    battleground solo, and the STOCK matchmaker pops the match. Sides fill to
#    the template max, start-critical bots first so the match pops at its
#    minimum; Random Battleground is topped up once the core rolls the map; a
#    human premade is moved to the normal queue in OnAddGroup so it can match
#    against bots. The planner half is engine-free and gtested.)
# New files added: Util/DcPoolBots.{h,cpp}
#   (the pool-bot plumbing both queue fills share — claim an offline addclass
#    character, evict it to its capital, sanitise it, release it — lifted out
#    of DcDungeonQueueFillJob so the dungeon and battleground fills draw
#    through one reservation check.)
# New files added: Util/DcProvisionBudget.{h,cpp}
#   (module-root src/Util/ — the realm-wide "one PlayerbotFactory::Randomize per
#    world tick" ration, hoisted out of DcTestRunManager::Tick's local bool so
#    the `.dc test` harness and the RDF queue fill share one budget instead of
#    each rationing itself and rolling two factories in a tick),
# New files added: Value/DungeonClearGroundHazardsValue.{h,cpp} (live
#   persistent-area-aura ground pools — DynamicObjects, not creatures — near the
#   bot; the DcHazard half that covers Scholomance's Cloud of Disease),
# Value/DungeonClearTrapHazardsValue.{h,cpp} (live GAMEOBJECT_TYPE_TRAP hazards
#   near the bot — the third resolver, for objects that are neither units nor
#   dynamic objects; covers the Shattered Halls flame-gauntlet Blaze 181915),
# Util/DcFirstContact.{h,cpp} (records the enemy that STARTED
#   each fight, off the OnPlayerEnterCombat `enemy` parameter DC had been
#   discarding — the pull record only covers fights DC pulled),
#   DungeonClearDispatch.h, DungeonClearAddonHook.cpp,
# Action/BetterLootRollAction.{h,cpp} (self-bot no-auto-roll override),
# Data/DungeonWingRegistry.{h,cpp}, Action/StayDeadAction.{h,cpp},
# Util/CorridorCenter.{h,cpp}, Value/DungeonClearLiveBossValue.{h,cpp},
# Util/DcPathWorker.{h,cpp} (async pathfinding worker),
# Util/DungeonClearGeometry.{h,cpp} (shared LOS/distance primitives),
# Settings/DcSettingsRegistry.h + Settings/DcSettings.{h,cpp}
#   (per-run settings registry + override accessor)
# Value/DungeonClearPullModeCurrentValue.{h,cpp}
#   (order-independent live pull-mode / Dynamic-verdict accessor)
# Util/SwimPathfinder.{h,cpp}
#   (Tier A greedy 3D swim navigator for submerged tunnels; driven via a raw
#    3D escort spline off the new DungeonClearSwimState value)
# Util/DungeonClearTuning.h
#   (single home for shared DC_* tuning constants — constant-centralization)
# Util/DcEngageGeometry.{h,cpp}
#   (DungeonClearUtil decomposition: ranges/doors/reachability/corridor unit)
# Util/DcPartyState.{h,cpp}
#   (DungeonClearUtil decomposition: readiness/rest gate/status text unit)
# Util/DcLootPolicy.{h,cpp}
#   (DungeonClearUtil decomposition: loot quality floor/skip/give-up unit)
# Util/DcLeaderSignal.{h,cpp}
#   (DungeonClearUtil decomposition: cross-bot leader/pull broadcast unit)
# Util/DcFollowerLifecycle.{h,cpp}
#   (DungeonClearUtil decomposition: follow/passive marks + reapers; owns g_dc* statics)
# Util/DcStatusPublisher.{h,cpp}
#   (DungeonClearUtil decomposition: addon comms + event-driven status push)
# Util/DcPullPlanner.{h,cpp}
#   (DungeonClearUtil decomposition: camp placement + dynamic-pull classification)
# Util/DcTargeting.{h,cpp}
#   (DungeonClearUtil decomposition: find-the-thing-to-fight unit; owns the
#    corridor trash-scan helpers. Completes the split — DungeonClearUtil class
#    is now empty; DungeonClearUtil.h is the umbrella + DC_PULL_* macros.)
# Util/DungeonClearApproach.{h,cpp}
#   (advance phase-machine §C: pure, engine-free decision core for the boss-
#    approach tail ladder — DecideApproach(Observation) -> Verdict, gtested at
#    every threshold edge; mirrors DungeonClearAdvanceAction's tail order.)
# Util/DungeonClearApproachIo.{h,cpp}
#   (orchestration replay harness: engine-free JSONL serialize/parse of the
#    DecideApproach (observation -> verdict) record + the live capture hook,
#    gated by DcSettings RecordDecisions. Replayed offline by t/replay_decisions
#    against t/fixtures/*.jsonl to lock every historical freeze as a regression.)
# src/Util/DcSpectator.{h,cpp}
#   (module-root src/Util/ — session plumbing, not bot AI: spectator
#    free-camera via WORLD_TRIGGER possession, `.dc spectate` / addon button)
# src/Util/DcWatchHop.{h,cpp}
#   (pure instance-bind planner for `.dc test watch`: which binds to release,
#    whether to re-bind, and whether the teleport must be forced far so a
#    second watch lands in the LIVE copy instead of the finished one)
# Data/RoomAggroRegistry.{h,cpp}
#   (room-wide-aggro boss table: bosses that force the whole room into combat on
#    engage; the tank clears the room before the pull — ClearRoomBeforeBoss)
# Value/DungeonClearRoomTrashValue.{h,cpp}
#   ("dungeon clear room trash remaining" set + the no-progress give-up valve)
# Data/DungeonEventRegistry.{h,cpp} + Util/DungeonEventExecutor.{h,cpp}
#   (declarative dungeon-events framework: typed step list + state-machine
#    executor; milestone 1 anchored events, milestone 2 Gossip + conditional)
# Data/EventConditionRegistry.{h,cpp}
#   (milestone 2: conditionId predicate table for off-path Conditional events —
#    e.g. SFK "courtyard door shut & prisoner alive"; mirrors ObjectiveHookRegistry)
# Data/Events/DungeonEventTables.h + Data/Events/*.cpp
#   (the event ROWS + activation conditions split one-file-per-dungeon:
#    SunkenTemple / ZulFarrak / ShadowfangKeep / ScarletMonastery /
#    RazorfenDowns / Stratholme Events.cpp + SharedConditions.cpp. Stratholme =
#    the dead-side "Baron run": 3 conditional ziggurat acolyte clears (conditions
#    5/6/7) + a persistent Slaughterhouse chain (abominations -> Ramstein -> 2
#    add waves -> Baron). DungeonEventRegistry.cpp /
#    EventConditionRegistry.cpp keep only the builder/lookup machinery and call
#    each file's Register<Dungeon>Events/Conditions appender — explicit calls so
#    every per-dungeon TU stays linked into the module static lib. Add a dungeon
#    = one new file + one call.)
#   (fable2 T2.3: the per-dungeon file is now the dungeon's whole "definition
#    unit" — its BossRosterPatch (Register<Dungeon>Roster, aggregated by
#    BossRosterRegistry) and any wing layout (Register<Dungeon>Wings, aggregated
#    by DungeonWingRegistry) moved in alongside the event rows, built with the
#    shared DcRoster helpers in Data/Events/DungeonRosterBuilders.h. NEW file:
#    Data/Events/MaraudonEvents.cpp — wings-only definition unit for map 349.)
# Action/DcActionShared.{h,cpp}, Action/DcAdvanceAction.cpp,
# Action/DcEngageActions.cpp, Action/DcPullActions.cpp,
# Action/DcFollowerActions.cpp
#   (nav/pull review Phase C Part 2 — the 4,608-line Action/DungeonClearActions.cpp
#    split along its four family seams: advance navigation / engage family /
#    pull FSM + maneuver / follower behaviors, with the cross-family
#    anon-namespace helpers + tuning constants promoted into namespace
#    DcActionShared (DcActionShared.{h,cpp}). DungeonClearActions.cpp is REMOVED;
#    DungeonClearActions.h stays as the umbrella class-declaration header.)
# Util/DcDecisionJson.h
#   (headless-sim Tier 1: shared flat-JSONL writer/parser primitives for the
#    decision capture/replay IO — header-only, used by the pull-decision IO)
# Util/DcPullDecision.{h,cpp} + Util/DcPullDecisionIo.{h,cpp}
#   (headless-sim Tier 1: pure Dynamic-pull governor decision core
#    DecidePull(PullObservation) -> PullVerdict, extracted from
#    DcPullPlanner::UpdateDynamicPullMode + its JSONL capture/replay IO, gated by
#    DcSettings RecordDecisions. Replayed offline by t/replay_pull against
#    t/fixtures/pull/*.jsonl; edge-pinned by t/TestPullDecisions; driven through
#    a scripted pull by t/TestScenarioDriver. Tier 2 nav harness (t/NavHarness,
#    t/TestNavGeometry, tools/slice_mapdata.py) is test-only, not GLOBed here.)
# Value/DungeonClearHealTargetValue.{h,cpp}
#   (LOS-blind, tank-biased most-hurt heal target — feeds the healer LOS
#    reposition fix: DungeonClearHealReposition{Trigger,Action})
# Data/Events/DireMaulEvents.cpp
#   (Dire Maul East: Ironbark the Redeemed anchored objective/event — gossip him
#    to open the Conservatory Door to Alzzin; ordered just before Alzzin via the
#    BossRosterRegistry map-429 patch + East-wing objective entry)
# src/DcModuleEnable.{h,cpp}
#   (module master switch: `DungeonClear.Enable` (conf, default 1) latched once
#    on the first world tick. With it off the registrar appends NO context into
#    mod-playerbots, DcStrategyGate installs nothing, and every hook, `.dc`
#    command and addon message is inert. Latch semantics are headless-tested by
#    t/TestModuleEnable.cpp.)
# src/DcStrategyGate.{h,cpp}
#   (dungeon-gated strategy install: the DC strategies are installed on a bot iff
#    it is on a dungeon/raid map — Map::IsDungeon() — instead of on every bot in
#    the realm. Removes the realm-wide DC trigger-ladder idle cost. Driven by
#    OnPlayerLogin + OnPlayerMapChanged (responsive) and a throttled world-tick
#    sweep in DungeonClearReaperScript (correctness net across ResetStrategies).
#    Pure decision kernel DcStrategyGate::Decide is headless-tested by
#    t/TestStrategyGate.cpp.)
# Data/DcNavPenaltyRegistry.{h,cpp} + Util/DcRouteFilter.{h,cpp}
#   (no-go route volumes: DcRouteFilter extends dtQueryFilterExt with a per-map
#    hand-authored no-go AABB table (DcNavPenaltyRegistry), so the long-range A*
#    corridor avoids navmesh shortcuts a real player can't follow — e.g. the LBRS
#    chasm wall-climb. Wired into LongRangePathfinder::BuildCoreFromMesh (now takes
#    a mapId) and screened in the StridedPathfinder fallback too.)
# Action/DcHakkar.cpp (Sunken Temple Avatar of Hakkar orchestration split out of
#   DcEngageActions.cpp — fable2 T2.5)
# REMOVED Data/EventConditionRegistry.{h,cpp}: conditional-event activation is now
#   a function pointer on DungeonEvent (.Conditional(&Predicate)), so the global
#   condition-id registry + id ledger are gone (fable2 T2.3 / events F7)
# Util/DcRegroupDecision.{h,cpp}
#   (combat-regroup Option B: pure contribution-gate kernel DecideCombatRegroup(
#    RegroupInputs) -> RegroupVerdict — "reconnect only when a follower truly can't
#    contribute". Engine-free, edge-pinned by t/TestCombatRegroup. Drives the
#    rewritten DungeonClearRegroupCombat{Trigger,Action}.)
# Data/Events/OldHillsbradEvents.cpp
#   (Escape from Durnholde, map 560: Brazen ride -> bomb 5 barrels + Lt. Drake ->
#    ONE EscortCreature step freeing Thrall and assisting him through Skarloc, the
#    mounted Tarren Mill ride, the barn/church/inn waves and Epoch Hunter, gated on
#    the instance progress counter. New step kind UseItemOnGO (cast a quest-item
#    spell AT a positioned GO — the barrel-bomb SPELLHIT); EscortCreature gains an
#    instance-data completion gate, a gossip-flag resume, and party mount speed-match.)
# Util/DcSmartRestDecision.{h,cpp} + Util/DcSmartRest.{h,cpp}
#   (Smart Rest hysteresis: pure latch kernel Decide(Inputs, members) -> Result
#    — enter on role-based low triggers, release at full hp/mana — edge-pinned
#    by t/TestSmartRest; DcSmartRest is the engine glue owning the DcRunState
#    latch fields + the cross-bot IsLatched read.)
# Util/DcWaitAtBossDecision.{h,cpp}
#   (Wait at Boss: pure pre-pull hold kernel — auto-pause the run right before
#    a boss pull and hold for the human's resume, once per boss per run via the
#    DcRunState::waitedBossGuid stamp; edge-pinned by t/TestWaitAtBoss. The gate
#    lives in DungeonClearEngageBossAction just before EngageDirect.)
# Value/DungeonClearPartyMemberToHealValue.{h,cpp}
#   (decorator over the stock `party member to heal` value, registered under the
#    same name so it fronts it: delegates to stock, then folds the DC escortee
#    into the heal candidate set while an escort is live — so the healer heals
#    Thrall / the Disciple / any escortee exactly like a party member. Paired with
#    DcLeaderSignal::GetLeaderEscortee (generic active-EscortCreature-step lookup)
#    and the escortee fold-in on DungeonClearHealTargetValue (reposition mover).)
# Util/DcBreadcrumb.h
#   (RecordBreadcrumb hoisted out of DcAdvanceAction.cpp so the item-on-GO
#    objective drive records the centered trail too — followers inherit it during
#    the Old Hillsbrad barrel run. Paired with a party-cohesion spread gate in
#    DriveUseItemOnGO so the tank waits for the party between houses instead of
#    sprinting off while the panel reports "waiting".)
# TestRun/DcTestRunVerdict.{h,cpp}, TestRun/DcTestDungeonRegistry.{h,cpp},
# TestRun/DcTestComp.{h,cpp}, TestRun/DcTestRunRecord.{h,cpp},
# TestRun/DcTestRunManager.{h,cpp}
#   (`.dc test` automated dungeon test harness: pure verdict kernel + supported-
#    dungeon/entrance registry + randomised party comp (DcTestComp::BuildComp
#    rolls a distinct-class tank/heal/3-dps party from a recorded seed; pinned
#    by t/TestTestComp) + JSONL run-result sink + the state-machine manager that
#    spawns/gears/groups/teleports a 5-bot party, starts the run, watches it,
#    and records the outcome.)
# TestRun/DcTestRunSelect.{h,cpp}, TestRun/DcTestRunLiveJson.{h,cpp}
#   (concurrent-run refactor pure helpers: DcTestRunSelect resolves the
#    `.dc test stop <selector>` precedence (bare/all/runId/dungeon-token) to a
#    set of run indices, and NextWatchIndex holds the `.dc test watch next`
#    cycle rule (wrap, cold start, stay put when there is nowhere to hop);
#    DcTestRunLiveJson builds the multi-run live-heartbeat
#    JSON ({"active","ts","runs":[…]}) the dashboard polls. Both engine-free and
#    edge-pinned by t/TestTestRunSelect + t/TestTestRunLiveJson. Paired with the
#    DcTestRunJob per-run FSM extraction + DcTestRunManager registry rewrite.)
# TestRun/DcTestRunJob.{h,cpp}
#   (per-run FSM extracted verbatim from the old singleton DcTestRunManager so N
#    runs can execute at once; DcTestRunManager is now a run registry.)
# Util/DcFormGate.h
#   (header-only shapeshift gate for the item-use casts: a feral-form druid tank
#    fails Spell::CheckCast's CheckShapeshift, so every quest-item interaction
#    — Old Hillsbrad's barrel bombs, the Deadmines cannon, the Mechanar cache —
#    silently no-ops until the form is dropped. Called just before each
#    CastItemUseSpell.)
# TestRun/DcDiagSnapshot.{h,cpp}
#   (end-of-run failure diagnostics: one read-only Snapshot of the whole run —
#    party positions/liveness/combat, the current boss target, route + wedge
#    watchdog counters, the stall reason, and the roster with WHICH completion
#    path fired per boss. Captured in DcTestRunJob::Teardown before the party is
#    disbanded, serialized under "diag" in dc_testruns.jsonl (schema 3).)
# Data/DcHazardRegistry.{h,cpp}
#   (per-map table of creatures emitting a persistent damaging aura bots must
#    stand outside of — the Arcatraz Sentinel's 15yd/1s Energy Discharge is the
#    forcing case. Split in two halves because they run on different threads:
#    the ROUTE half is hand-authored DcNavPenaltyRegistry boxes (the emitters are
#    rooted, and DcRouteFilter runs on the worker thread where no Map* exists);
#    the LIVE half is DcHazard::PointIsHot/LegIsHot over DungeonClearHazardsValue,
#    consumed by the camp/standoff solvers on the map thread.)
# Value/DungeonClearHazardsValue.{h,cpp}
#   (500ms-cached NearestUnitsValue of live hazard emitters near the bot. Needs a
#    PERMISSIVE AcceptUnit — the Arcatraz corpse bombs carry UNIT_DYNFLAG_DEAD and
#    are filtered out by AttackersValue::IsPossibleTarget, so DcKey::FarTargets
#    cannot be reused for this.)
# Util/DcHazard.{h,cpp}
#   (the live predicates the camp/standoff solvers call: PointIsHot/SegmentIsHot
#    for the persistent pulse, plus NearestVacate for the active retreat, and
#    PointIsInVacateBand — the "will the retreat trigger re-fire if I land here"
#    test the retreat validates its destination with. That is a DIFFERENT question
#    from PointIsHot, which asks about placement; conflating them thrashed the
#    Maraudon sludge packs.)
# Trigger/DungeonClearHazardVacateTrigger + Action/DungeonClearHazardVacateAction
#   (survival move: walk every party bot out of an active-vacate emitter's pulse.
#    Registered in BOTH strategies at DcRel::HazardVacate = 55 — the Arcatraz
#    Destroyed Sentinel pulses after the kill, once combat has usually dropped.
#    Per-row hysteresis decides what happens next: a thin holdBand means leave and
#    move on (the unattackable summon), a wide one means stay out while it lives
#    (Maraudon's Creeping Sludge, which melee are meant to abandon rather than
#    trade with). The action COMMITS to one validated point rather than recomputing
#    per tick — recomputing reverses the bearing whenever a nearer emitter is
#    elected, which is how a sludge pack turned the retreat into a treadmill.)
# Data/Events/ArcatrazEvents.cpp
#   (map 552: Warden Mellichar's stasis-pod wave finale + roster patch.)
# Data/Events/SethekkHallsEvents.cpp
#   (map 556: force-summon & kill Anzu — send-event 14797 via ObjectiveHook 7 —
#    + Objective-anchor roster patch. Bonus boss, works on normal difficulty.)
# Util/DcRezDecision.h + Util/DcRezRecovery.{h,cpp}
#   (post-combat party resurrection: a death HOLDS the run while a living
#    Priest/Paladin/Shaman/Druid can recover it instead of disabling on the
#    spot. Pure header-only election/timeout kernel Decide(Inputs, members) ->
#    Result, edge-pinned by t/TestPostCombatRez; DcRezRecovery is the engine
#    glue — dead-tolerant run-owner resolution, same-map snapshot KEEPING dead
#    members, the DcRunState rezPendingSinceMs/rezAnnounceMs clocks, and the
#    IsPending hold-gate read used by the between-pulls + event-rest gates.
#    Paired with the relaxed party-died trigger and the all-bots
#    DungeonClearRezParty{Trigger,Action} rung at DcRel::RezParty = 31.5.)
# Util/DcStrandedDecision.h + Util/DcStrandedRecovery.{h,cpp}
#   (stranded-member recovery failsafe: when the run has shown no progress for
#    DungeonClear.StrandedRecoveryNoProgressSecs while a bot member is stuck out
#    of range — fell under the world / wedged — the leader teleports the strays to
#    itself. Pure header-only Decide(Inputs, members) -> Result kernel, edge-pinned
#    by t/TestStrandedRecovery; DcStrandedRecovery is the engine glue — leader-only
#    clock ownership, the DcRunState progress* fields, a harness-mirroring progress
#    detector (encounter mask / cleared anchors / closing distance) and the group
#    teleport. Paired with the leader-only DungeonClearRecoverStranded{Trigger,
#    Action} rung at DcRel::StrandedRecovery = 42.)
# Data/Events/BlackMorassEvents.cpp
#   (map 269: the fully event-driven CoT Opening of the Dark Portal — Sa'at
#    gossip, portal-grounds pre-clear, Medivh proximity start + garrison via
#    ObjectiveHook 8, and the repeatable conditional wave-kill event that
#    closes all 18 time rifts. Objectives-only roster patch — every boss is a
#    rift TempSummon.)
# TestRun/DcWipeContext.{h,cpp}
#   (wipe post-mortem: the pure latch rule that answers "did we lose the boss
#    fight or did a trash pack eat us?". Combat ends when the last member dies,
#    so the harness samples the party's opponent every monitor step and only a
#    clean disengage clears it — a wipe never does. Edge-pinned by
#    t/TestWipeContext; DcTestRunJob::TrackEngagement is the engine glue, and
#    the result rides out on the run record (wipeOnBoss/wipeOpponent) and the
#    live heartbeat.)
# Overrides/BlackMorassDriver.cpp
#   (map 269's CONTROLLER half, lifted out of ObjectiveHookRegistry.cpp. Hooks 8
#    and 12 are the only on-arrival behaviour in the module that re-decides from
#    live world state every tick instead of doing one thing on arrival, so they
#    carry their own movement (StepsOwnMovement + repath/reissue throttling),
#    engagement (REACT_DEFENSIVE rift adds the normal pipeline cannot see) and
#    rift/keeper/drainer selection layers. Registered via RegisterBlackMorassHooks,
#    called explicitly from Hooks() — a static-lib TU with only constructor side
#    effects gets dropped by the linker. The DECLARATIVE half of 269 stays in
#    Data/Events/BlackMorassEvents.cpp.)
# TestRun/DcTestRoster.{h,cpp}
#   (`.dc test start <d> party=Tank,Heal,D1,D2,D3` — hand-picked parties of REAL
#    player characters, from any account, launchable from the Command Deck. This
#    is only the pure grammar (split, trim, positional roles, refuse a bad list),
#    edge-pinned by t/TestTestRoster; the engine half is
#    DcTestRunJob::CreateFromRoster + DcTestRunManager::StartRoster. Roster runs
#    log in MASTERLESS (AddPlayerBot with masterAccountId 0 — the ownership gate
#    refuses a hand-picked party on any other path), never roll the factory (no
#    respec/regear/relevel: the characters fight as they are), and always unbind
#    both difficulties first so every run gets a fresh instance.)
# TestRun/DcTestGearTiers.{h,cpp}
#   (per-run gear ceiling: `.dc test start <d> ilvl=N quality=epic`, the same two
#    options on `.dc test plan start`, and the ladder of ceilings worth offering
#    for a given dungeon — named raid tiers from the AutoGearScoreLimit conf
#    table at the level cap, three steps around the dungeon's own gear below it.
#    Pure kernel, edge-pinned by t/TestTestGearTiers; the engine half is
#    DcTestRunJob::TickProvisioning building PlayerbotFactory with the resolved
#    pair, and DcTestDungeonRegistry::WriteSidecar publishing the ladders for the
#    Command Deck's start form.)
# Util/DcCombatFlag.{h,cpp}
#   (flag-vs-fight: the one shared answer for every rung that used to stand down
#    on a bare bot->IsInCombat(). A 45yd hostile area aura flags the whole party
#    in with nothing aggroed, and each rung that keyed on the raw flag froze on
#    it in turn — the leader's driving ladder (fixed S1356), then the follower's
#    follow-tank rung, the between-pulls rest floors, the stranded failsafe and
#    the post-combat rez clock. MayDrive / IsPhantomFlag / AnyPartyEngagement,
#    all over DungeonClearMath::MayDriveWhileFlagged's grace.)
# (this comment bumps mtime to force CMake re-glob on next build)
# Util/DcTankForm.{h,cpp}
#   (pre-pull shapeshift gate — the mirror of DcFormGate. A feral druid is only
#    ever shifted by BearDruidStrategy's "bear form" trigger, which is
#    combat-engine only, while every DC pull is opened on the NON-combat engine;
#    so the druid tank tagged in caster form, ate the pack's opener at caster
#    armour, and shifted a beat late. EnsureBearForm is called fire-and-forget
#    from DungeonClearPullAction (whenever a pull is live) and from EngageDirect
#    (inside DC_PULL_START_RANGE), so nothing can wait on a form that won't go
#    up.)
# Data/ScriptedPullRegistry.{h,cpp}
#   (hand-authored "peel this pack out of that room, in this order, from this
#    exact spot" plans — the third member of the positional-override family, and
#    the middle ground between FightInPlaceRegistry, which FORBIDS a pull, and
#    BossPullbackRegistry, which MANDATES one for a boss. A row names the party
#    camp, the tank's stand spot (the one place with line of sight to exactly one
#    pack), the pack's cylinder + entry filter, and its position in the pull
#    sequence. Selin Fireheart's room is the case it exists for: every pack in it
#    shares sight-lines with the boss, so the safe spots are facts about the
#    room's walls that no aggro-radius or navmesh probe can derive.)
# Data/SealedEncounterRegistry.{h,cpp}
#    (SEALED ENCOUNTERS: bosses whose room LOCKS on encounter start, via an
#    InstanceScript DOOR_TYPE_ROOM door held `open &= (state != IN_PROGRESS)`.
#    Magisters' Terrace registers one for Selin Fireheart — the Assembly Chamber
#    Door 188065 at (215.1, 0.4) — so engaging him shuts the only way in and any
#    follower still in the corridor spends the whole fight at a closed door. A row
#    names the sealed VOLUME, because the tank crosses the threshold before it
#    engages and so a radius around the tank cannot express "will be locked out";
#    plus a tank-anchored clump radius for the final approach, which is what makes
#    the muster satisfiable without a long wait inside the boss's aggro range.)
# Util/DcPlayerbotCompat.h
#   (header-only: IsSelfBot / IsRealPlayer / IsHumanControlled written against
#    only the mod-playerbots API that is identical on master and test-staging,
#    so this module builds on both across PR #2592. No .cpp, so no re-glob is
#    needed for it — listed here to keep the file inventory complete.)
# Data/DcSocialQuarantineRegistry.{h,cpp} + Util/DcSocialQuarantine.{h,cpp}
#   (SOCIAL QUARANTINE — the fourth positional override, and the only one that
#    changes the MOBS rather than the party. While a named boss is the run's
#    objective, every pack the plan is not currently pulling is held at
#    REACT_DEFENSIVE, which is the first thing Creature::CanAssistTo tests and
#    the gate CreatureAI/SmartAI::MoveInLineOfSight passes before AttackStart —
#    so a quarantined pack can be neither RECRUITED by a call for help nor woken
#    by walking past it, while CreatureGroup::MemberEngagingTarget (which does
#    not test react state) keeps it fighting as a formation the instant anything
#    actually attacks it. Volumes come from the scripted-pull plan's own pack
#    cylinders, minus the due stage, plus hand-authored zones for packs with no
#    pull row. Built for the Magisters' Terrace rotunda, whose east/north-east
#    and centre/north-east pairs are inside CreatureFamilyAssistanceRadius AT
#    SPAWN — a T=0 bond no stand spot, opener or ordering can break.)
# TestRun/DcTestAreaTriggers.{h,cpp}
#   (AREATRIGGER RELAY for headless runs. An areatrigger is a client-side
#    volume: the only thing that ever runs an at_* script is
#    WorldSession::HandleAreaTriggerOpcode fed by a client's CMSG_AREATRIGGER,
#    and stock playerbots only relays a HUMAN MASTER's packet
#    (masterIncomingPacketHandlers) — its position-based WithinAreaTrigger
#    returns false for anything without an areatrigger_teleport row. So a
#    5-bot test party fires NO scripted areatriggers at all. The Underbog's
#    Ghaz'an is the proof: at_underbog_ghazan (4302) is the only caller of his
#    ACTION_MOVE_TO_PLATFORM, so without it he swims his underwater DB patrol
#    (path 1383920, z 32-47) all run instead of climbing to his platform (path
#    1383921, z 81.4). The job sends the packet for whichever member is
#    standing in the volume, edge-triggered like a client. Scoped hard: test
#    runs only, bot-only groups only (a human-controlled member's own client is
#    already sending it — relaying would double-fire), and scripted
#    NON-teleport triggers only (the instance exit is an areatrigger).)
# Util/DcZoneLine.{h,cpp}
#   (ZONE-LINE KEEP-OUT for camp placement. An advanced pull drags the pack back
#    onto cleared ground, and at a dungeon's FIRST pull the only cleared ground
#    behind the tank is the entrance it walked in through — so ComputeSafeCamp /
#    ComputeTrailCamp / GetLeaderScoutTrailPoint would plant the camp on, or
#    past, the instance exit areatrigger. Nothing downstream catches it: the
#    navmesh has no idea areatriggers exist, so IsNavReachable says yes. The cost
#    is asymmetric — a headless bot sends no CMSG_AREATRIGGER and shrugs it off,
#    but a SELF-BOT is a real client, reports the trigger, and the player is
#    teleported out of the instance mid-run. This indexes every map-changing
#    areatrigger by the map it stands on (ObjectMgr, snapshot by value, built
#    once) and offers a point test padded by CampMargin plus a segment test
#    padded by the much thinner WalkMargin — a camp can sit cleanly BEYOND a zone
#    line with both endpoints clear while the leg to it goes straight through.
#    Margins pad in XY only, never Z: a fall-catcher slab's ceiling sits a couple
#    of yards UNDER the floor beside it, and padding its height would make a
#    third of Shadowfang un-campable.)
# Data/Events/NexusEvents.cpp
#   (The Nexus, map 576. Three anchored objectives — one per Containment Sphere
#    (188526/188527/188528) — that FREE KERISTRASZA. She spawns
#    UNIT_FLAG_NON_ATTACKABLE inside a frozen prison and boss_keristrasza only
#    drops the flag once all three DATA_*_ORB boss states are DONE, which nothing
#    but a click on each sphere ever sets. Each objective sits at its sphere so
#    boss-nav walks the tank in; the event is a one-step UseGameObject, the
#    Sunken Temple statue idiom. The roster patch reorders the four bosses onto a
#    2..8 scale so the spheres have somewhere to sit between Ormorok and
#    Keristrasza — the DBC packs those two into adjacent bits on both
#    difficulties.)
# Data/Events/AzjolNerubEvents.cpp
#   (Azjol-Nerub, map 601. Two structural events plus a roster patch.
#    (1) HADRONOX. Her add swarm is INFINITE — three world triggers (23472)
#    summoned in her Reset() carry periodic summon auras that fire for as long as
#    the encounter is not done, and every add she kills while it carries her Leech
#    Poison heals her 10% of max HP, so she is not killable until the swarm stops.
#    The only off-switch is EVENT_HADRONOX_MOVE3, gated on all three Anub'ar
#    Crushers (28922) being dead, which walks her onto the z~733 platform and
#    casts Web Front Doors. The event garrisons the party on that platform until
#    the crushers are dead AND the doors are actually webbed (hook 13 reads
#    boss_hadronox::GetData(28921), the 'Hadronox Denied' probe — the only
#    exposure _doorsWebbed has), and the roster patch moves her anchor off her
#    spawn ledge (522.5, 544.9, 674.7 — 10yd from where every add-swarm waypoint
#    path ends) onto her own MOVE3 destination.
#    (2) THE DROP. Flood-filling the four 601 mmtiles splits the map into two
#    components that do not touch: z 645-833 (everything down to the pit floor)
#    and z 195-310 (the lower kingdom out to Anub'arak). The join is an ~11x13yd
#    HOLE in the pit floor at x 528-539 / y 543-556 with a single surface 360yd
#    below it. TeleportParty, not DropInHole — nothing makes a fall that long
#    survivable. Checkpoint (522, 548, 648.9), landing (533.5, 549.5, 288.7),
#    both column-probed against the live mmaps.
#    NO CLIENT PACKET IS INVOLVED: AreaTrigger.dbc has three rows on map 601 and
#    none of them carries a script, teleport, involvedrelation or smart_scripts
#    row, and the map has no gossip and no clickable gameobject at all.)
# Data/DcNeverTargetRegistry.{h,cpp}
#   (Creatures the CLEAR must never pick as a target — the "is killing this
#    progress" half of target selection, which AttackersValue::IsPossibleTarget
#    ("may this bot legally attack it") cannot answer. First row: The Nexus's
#    Crystalline Frayer (26793), which npc_crystalline_frayer makes unkillable
#    until Ormorok dies — a lethal hit is discarded, the mob spends 90s in a
#    NOT_SELECTABLE seed pod regenerating, then returns at full health — and
#    which instance_nexus::KillAllFrayers then kills outright, so an ALIVE frayer
#    is never worth fighting. Wired into the five clear-side candidate gates
#    (FarTargets AcceptUnit, PickBlockingTrash, the en-route sweep spheres,
#    NearestHostileNearPoint, room-trash); the stock combat engine is untouched,
#    so bots still fight back when a frayer attacks them.)
# Data/Events/AhnkahetEvents.cpp
#   (Ahn'kahet: The Old Kingdom, map 619. Three anchored objectives + a roster
#    patch, covering the map's two GATED encounters. NEW file; the map was
#    previously unauthored apart from its `ok` test-run key.
#    (1) PRINCE TALDARAM is imprisoned — SetImmuneToAll + hover + beam visual,
#    hanging at z 42 over his platform — until BOTH Ancient Nerubian Devices are
#    clicked. There is no C++ gameobject script: 193093/193094 carry
#    smart_scripts source_type 1 SMART_EVENT_GOSSIP_HELLO -> SET_INST_DATA 0/1,
#    which GameObject::Use reaches through SmartGameObjectAI::GossipHello (it
#    returns FALSE, so Use also falls through to UseDoorOrButton and the device
#    leaves GO_STATE_READY — which is what makes the step idempotent).
#    instance_ahnkahet then StorePersistentData(index, DONE) and, on the second,
#    DoAction(ACTION_REMOVE_PRISON). Authored as two objectives, one per device
#    (Nexus Containment Sphere idiom — they are 74.5yd apart and an event step's
#    own movement is a short HopTo). Visit order east-then-west from a Dijkstra
#    over the live 619 navmesh (692 vs 720yd). Each click is followed by a
#    persistent-data hold on the index it is supposed to set, because
#    UseGameObject reports Done without checking that anything happened and a
#    swallowed click would otherwise surface as an unexplained stall at Taldaram.
#    (2) JEDOGA SHADOWSEEKER hovers NOT_SELECTABLE|NON_ATTACKABLE and immune
#    until all FIFTEEN Twilight Initiates (30114, creature_summon_groups group 0)
#    are dead — SummonedCreatureDies emptying `oocSummons` is the only thing that
#    MovePoints her down. An entry-filtered ClearRadius (26yd, z-band 12) plus a
#    by-entry KillCreatureEngage backstop. Deliberately NO leading MoveTo: the
#    step is a pure gate, so the ordinary engage pipeline works the pack from the
#    volume's edge instead of the tank marching into the middle of fifteen mobs.
#    (3) ROSTER. Both bosses' `creature` rows are their PRE-FIGHT poses — 42.04
#    and -0.62, against probed floors of 11.43 and -15.98 — and
#    DungeonBossesValue::SnapAll cannot fix either: BOSS_SNAP_RADIUS is 40 but
#    NavmeshSnap's query box has a fixed 10yd VERTICAL half-extent, so
#    findNearestPoly never sees a floor 15 or 30yd down. Both are remove+re-add
#    with hand-probed coordinates; the other three bosses are only reordered onto
#    the same 1..8 scale so the objectives have slots.
#    NOT covered here: Elder Nadox's guardian aura, Amanitar's mushrooms, Herald
#    Volazj's Insanity phasing, and the NAVMESH BREAK that leaves Volazj's whole
#    chamber on a component the party cannot reach. See
#    deployment-files/docs/mod-dungeon-clear_ahnkahet_plan.md.)
# Data/DcNeverTargetRegistry.{h,cpp}
#   (Two map-619 rows for Jedoga Shadowseeker's ritual STAGING — Twilight
#    Worshipper 30111 and Twilight Volunteer 30385. At 55% HP she goes
#    REACT_PASSIVE + NOT_SELECTABLE | NON_ATTACKABLE with damage=0 and takes off
#    to sacrifice a volunteer; with no boss to hit the clear's NON-COMBAT ladder
#    takes over and the corridor scan finds the ring of staging mobs the
#    encounter has just placed around the arena. Live (tr-20260825-224456-8, tank
#    Wieron): "pull target vetoed — Jedoga Shadowseeker" then 25 consecutive
#    "blocking-trash: 3 candidate(s) in band -> Entry: 30111 at 52.6yd", plus
#    three members' "regroup: moving to standoff (36.3yd, anchor=Entry: 30385)"
#    (LeaderFightAnchor resolves off the leader's victim, so the tank's pick
#    moves the whole party). She has combat movement off and is passive, so her
#    threat list empties behind the departing party and she
#    EnterEvadeMode(NO_HOSTILES)s -> DespawnAll + Reset, re-summoning the fifteen
#    Twilight Initiates — which the clear can never re-clear, because event 3
#    latched Done and is not Repeatable. The header's criterion is widened to name
#    this second class (encounter staging the clear must not travel to) alongside
#    the original (mobs that cannot be killed at all). The combat engine is
#    untouched, so mod-playerbots' own wotlk-ok volunteer kill — which denies
#    Jedoga Gift of the Herald — still works; only the CLEAR's decision to walk
#    out to the arena edge is removed.)
# Data/DcEventDoorRegistry.h
#   (Four map-619 rows. IsNavigationIgnored: 193564 Doodad_Azjol_Platform_FX_01,
#    Taldaram's prison effect, which sits ON his objective anchor (under a yard
#    away) and would park the run on its own destination and auto-pause — the
#    Steamvault access-panel failure, one step worse; and 193093/193094, the two
#    devices, which DO have to be clicked but by their events, in a measured
#    order and with verification, not opportunistically by the door-blocked
#    watchdog. IsScriptOnly: 192236 Taldaram Door, a DOOR_TYPE_PASSAGE that only
#    his death opens but which is lock-free, so a force-open would unlock Jedoga
#    / Amanitar / Volazj with him still immune behind it.)
# Data/Events/DrakTharonKeepEvents.cpp
#   (map 600 — Drak'Tharon Keep. THE fix is the roster patch: The Prophet
#    Tharon'ja's instance_encounters rows are creditType 1 (cast-spell, spell
#    61863) and BossSpawnIndex::Build skips every encounter that is not
#    ENCOUNTER_CREDIT_KILL_CREATURE — correctly, since a spell credit carries no
#    creature entry to derive coordinates from — so he never enters the derived
#    roster, the candidate list empties after King Dred and every run declares
#    itself finished at 3/4. Fixed with the new DcRoster::MakeBossWithBit, which
#    adds a boss with an EXPLICIT DungeonEncounter bit (3) and hand-authored
#    coords; completion then rides the same GetCompletedEncounterMask the other
#    three use. The same defect hides a boss on CoT Stratholme (595), Halls of
#    Stone (599) and Trial of the Champion (650) — each is one patch of its own
#    and is deliberately not bundled here.
#    Also: conditional event 1, the Novos camp-and-hold. Phase 1 is a 70s+ gate
#    that only four Crystal Handler deaths open, run under a 3-second add stream
#    with an 11yd / 1665-per-second pool on the boss, so the party never leaves
#    combat — DrivesInCombat + StepsOwnMovement + Repeatable, driven by
#    ObjectiveHookRegistry hook 14 (HoldNovosCamp), which is the Black Morass
#    wave-driver shape scoped to one fixed camp. And heroic-only anchored event
#    2, an entry-filtered sweep of King Dred's raptor pen so Raptor Call (59416,
#    heroic only) has nothing left to summon.
#    Paired rows elsewhere: DcNeverTargetRegistry {600,27583} (Novos Summon
#    Target — killing one permanently stops a Crystal Handler spawning and
#    softlocks the gate for the life of the instance), FightInPlaceRegistry
#    (Trollgore's arena — his own CheckInRoom is a 72yd Y band and failing it
#    EnterEvadeMode(BOUNDARY)s him every tick), DcEventDoorRegistry::
#    IsNavigationIgnored 189299-189302 (the four Ritual Crystals, inverted-state
#    DOOR_TYPE_ROOMs on a key-item lock lying across the route into the Novos
#    chamber — the Utgarde Keep forge-fire shape), and three DcGroundHazard rows
#    (47346 Arcane Field 14/11, 49034 Blizzard and 49548 Poison Cloud both 10/8).
# Data/Events/VioletHoldEvents.cpp + Overrides/VioletHoldDriver.cpp
#   (map 608 — The Violet Hold, the Black Morass model one size down: a fully
#    event-driven dungeon whose objectives ARE the clear. The stock pipeline has
#    nothing to work with here — instance_encounters names Erekem (29315) and
#    Moragg (29316) as the First/Second Prisoner credits, both are world spawns,
#    so BossSpawnIndex anchors the clear at their CELL coordinates behind sealed
#    lock-0 doors; and the pair actually released is rolled per instance
#    (urand(BOSS_MORAGG, BOSS_ZURAMAT)), so those credit rows name the wrong
#    bosses besides. Cyanigosa is a TempSummon and never derives at all.
#    The patch REMOVES both cell anchors and adds four objectives: enter (gossip
#    Sinclari 30658, menu 9997 -> 9998, which starts the 18-wave siege) and one
#    per instance boss-state slot. Completion rides GetBossState(DATA_1ST_BOSS /
#    DATA_2ND_BOSS / DATA_CYANIGOSA), never the completed-encounter mask.
#    Five hooks, ids 15-19. Three defend hooks and not one because a Custom step
#    is handed a default-constructed DungeonBossInfo, so a shared hook cannot
#    tell which objective invoked it. Hook 19 (VhDriveWave) is the controller:
#    Conditional + Repeatable + Optional + DrivesInCombat + StepsOwnMovement,
#    one Custom step, SEVEN rungs re-decided per tick — force-pull, released
#    prisoner, clean the door seal, kill the portal keeper, finish whatever is
#    already within 30yd, STATION ON THE LIVE PORTAL, hold the staging point.
#    Rungs 4-6 are the 2026-08-26 strategy correction. The first cut read the
#    geometry as a chokepoint camp — all six trash waypoint paths converge on two
#    points a yard apart at the door, so a party on the landing intercepts the
#    whole siege for free — and that is neither how the Violet Hold is played nor
#    how it is won: a keeper portal pumps 3-4 adds every 20s FOREVER and its only
#    off-switch stands 52-86yd away at the rim, so a party at the door fights the
#    pump's output on the pump's schedule while the gate bleeds. The party now
#    stations ON the live portal (adds are summoned within 2yd of it and the
#    keeper spawns in melee range, so the wave is met before it takes a step) and
#    waits at STAGE (1892.29, 805.70, 38.44 — the core's own MiddleRoomLocation,
#    45.5yd from the average rim portal against the door camp's 69.0) in between.
#    VhLivePortal tells the two wave kinds apart by SERVER-SIDE VISIBILITY: an
#    elite portal calls SetVisible(false) the instant it summons its elites, so
#    an invisible portal reads as spent and the driver fights the elites where
#    they are instead of parking on an empty rim spot. The camp (1855.0, 803.5,
#    44.05) survives as the EMERGENCY position — rung 2, the only one that goes
#    back to the door.
#    And the loss condition is readable without a core change — _gateHealth has
#    no GetData case, but every add at the door holds a 58040 aura on the Prison
#    Door Seal (30896), so walking that creature's applied-aura list names
#    exactly which mobs are draining the gate. 58040 ticks every 3s per caster
#    against a 100-point gate: one add loses the run in 300s, three in 100s.
#    Paired rows elsewhere: DcNeverTargetRegistry {608, 31079} (Azure Saboteur —
#    IMMUNE_TO_PC bait that walks the whole room on waves 6 and 12),
#    DcEventDoorRegistry::IsScriptOnly for the Prison Seal 191723 and all twelve
#    Cells (lock 0, so BotCanOpenDoorLikePlayer reads them as clickable, and
#    opening one does NOT release its boss), ::IsNavigationIgnored 193611/193615
#    (the six Activation Crystals — GAMEOBJECT_TYPE_DOOR wall controls sitting
#    permanently READY around the arena rim, the Steamvault access-panel shape),
#    and one DcGroundHazard row {608, 58693} (Cyanigosa's Blizzard, 12/10 — a
#    10yd PERSISTENT_AREA_AURA at 1500 frost per 2s. Lavanthor's Cauterizing
#    Flames 59466 was checked and is NOT one: effect 2 + effect 6, no
#    DynamicObject).)
# t/TestVioletHold.cpp
#   (the map-608 authored-data lints: the wave event's exact shape, the four
#    objectives' distinct hooks, the roster removals, the camp-on-the-funnel
#    geometry, the staging point being closer to every one of the six rim portals
#    than the door camp is (the whole point of the station), the three entry
#    lists' completeness/exclusivity/disjointness, and the four safety-rail
#    registries. Suite name starts DungeonEvent so the `DungeonEvent*` gtest
#    filter in t/run_tests.sh and CI picks it up.)
# Data/Events/BlackwingLairEvents.cpp
#   (Blackwing Lair / Razorgore the Untamed, phase 1 — the encounter the module
#    orchestrates from INSIDE a raid boss fight. One conditional event:
#    Conditional + Repeatable + Optional + DrivesInCombat + ENCOUNTERACTIVE +
#    StepsOwnMovement, one Custom step (hook 20). encounterActive is the flag
#    that makes it possible at all: on a raid map every DC combat behaviour goes
#    inert the moment an encounter starts (DcBossStandDown) and this is the one
#    sanctioned exception, the seam DungeonEvent::encounterActive was written
#    for. Phase 1 cannot be done by the stock bots at any skill level: a player
#    must click the Orb of Domination (GO 177808) to mind-control Razorgore
#    (19832, NINETY SECONDS, and it leaves the clicker locked out for SIXTY by
#    Mind Exhaustion 23958), then drive the possessed boss to each of THIRTY
#    Black Dragon Eggs (GO 177807) and cast Destroy Egg (19873 — 3s, 10yd,
#    interrupt-on-move). Measured budget: a 262yd greedy tour at his 10 yd/s plus
#    90s of casting = ~135s, i.e. TWO OR THREE mind-control windows and a
#    rotation of runners, exactly like a human raid. Meanwhile the instance
#    spawns 2-5 adds every 15s from eight floor positions until the last egg
#    pops, and Razorgore DYING in phase 1 casts 20038 and instakills the raid —
#    which is why the run's job is to make phase 1 end, and why the DPS guard
#    (RaidBwlStrategy::AppendTargetExclusions) lives upstream in mod-playerbots
#    where phase and target selection actually are. The other seven BWL bosses
#    need nothing: all eight carry kill-credit rows (instance_encounters
#    610-617) so BossSpawnIndex derives them. The file also owns the map's ROSTER
#    PATCH (RegisterBlackwingLairRoster): Grethok the Controller as boss #0 —
#    trash by the DBC, but the mob a human raid pulls to start this fight, and
#    therefore the anchor the pull pipeline needs — with the eight real bosses
#    shifted to order 1..8 and their kill-bits untouched.)
# Overrides/BlackwingLairDriver.cpp
#   (the imperative half, hook 20. Unlike every other controller in this module
#    it NEVER MOVES THE BOT THAT RUNS IT — the leader is the raid's main tank
#    holding an add wave, so every branch ends in Done (yield) and the work is
#    side effects on other units: walking the POSSESSED BOSS egg to egg and
#    casting from it. Its second actor is delegated, not driven: the orb refuses
#    a user with a pet and roots the charmer for 90s, so the driver ELECTS a
#    runner (DcRazorgore::SelectRunner — ranged dps first, healers last, never
#    the tank) and publishes it in DcRunState; that bot walks itself over on its
#    own tick. Per-egg no-progress clock retires an unreachable egg for the pass
#    rather than wedging the encounter on it, and a cast the caster keeps
#    refusing falls back to the same spell TRIGGERED — same script, same credit,
#    minus the channel.)
# Util/DcRazorgoreDecision.h
#   (the pure kernel behind both: Decide() over a snapshot View, and the runner
#    election. No world state, so t/TestBlackwingLair.cpp exercises the whole FSM
#    — completion, the possessed branch, the runner rotation — without a map.)
# Action/DcRazorgoreActions.cpp
#   (the two member-side rungs. The RUNNER's half — DungeonClearRazorgoreOrb
#    {Trigger,Action}, both engines,
#    DcRel::RazorgoreOrb 62 — it has to outrank mod-playerbots' `bwl` nodes at
#    ACTION_RAID 60/61, one of which walks bots out of Razorgore's frontal cone
#    and would walk the runner off the orb ledge every tick. The 78yd walk to the
#    ledge goes through LongRangePathfinder + one escort spline because the
#    engine PathGenerator truncates a >74-poly path silently. Yields the tick the
#    moment the runner is parked, so a possessing DPS still runs its rotation.
#    While the possession is up it does the OPPOSITE of yielding: 19832 is a
#    channel and any cast, swing or step from the runner's own body ends it, so
#    the rung owns every tick of the window, drops the bot's victim (autoattack
#    runs off Unit::Update, not the action engine) and spends 90s mute. Handing
#    the tick back "so the rotation runs" was the first shape and it broke its own
#    channel mid-run. The click interrupts any cast in flight first, because
#    go_orb_of_domination's own CastSpell is non-triggered and a cast in progress
#    refuses it silently.
#    Exempted by name from the stand-down zero in DungeonClearCombatMultiplier,
#    alongside the combat event driver.
#    The PULL is no longer a rung at all. Grethok the Controller (12557) and two
#    Blackwing Guardsmen (14456) are world spawns 6-10yd from the orb, on the
#    ledge, belonging to no encounter, and the runner's 78yd walk ENDS inside
#    them: the first live run sent one elected DPS up alone and it died without
#    reaching the orb. The fix that followed — a rung that glided the WHOLE raid
#    to a staging point on the platform the moment the leader came within 100yd —
#    delivered bodies but was not a pull: no muster, no formation, no standoff,
#    forty bots racing up a ramp with the tank in front and the stragglers
#    wherever the last trash pack left them. Grethok is now a BOSS ANCHOR
#    (RegisterBlackwingLairRoster, order 0, borrowing Razorgore's kill-bit), so
#    the ordinary pipeline does it: advance walks the raid to (-7618.29,
#    -1021.42, 413.56) as one body, the raid muster tops it off and rebuffs it,
#    the boss standoff parks the tank just outside Grethok's aggro bubble, and the
#    engage makes the pull. `creature_formations` puts Razorgore in Grethok's
#    formation with groupAI 7, so that pull IS the boss pull — which is exactly
#    why the kernel's FIRST test is now "has anybody pulled the platform": before
#    the tag nothing on the ledge moves (no election, no staging, no click), and
#    from the tag onward the runner takes over immediately, because every second
#    without the mind control is a second the raid spends damaging a boss whose
#    phase-1 death wipes it. The driver latches Grethok's anchor cleared once the
#    platform reads empty — the wipe path, where his corpse has decayed and
#    Razorgore's bit will not be set for another attempt.
#    The RAID's half — DungeonClearRazorgoreCamp{Trigger,Action}, DcRel::
#    RazorgoreCamp 61.5, COMBAT ENGINE ONLY — is the fix from the first live run,
#    where the orb and the egg run worked and the raid simply fought wherever the
#    pull had left it: the runner is ROOTED on the ledge for 90s at a time and
#    cannot defend itself, so every add that picked it arrived unopposed. This
#    walks everyone but the runner to a camp at the FOOT of the platform —
#    (-7608.30, -1036.00, 408.87), column-probed to one walkable surface, 12.3yd
#    from the orb (inside every healer's range of the runner) and 46-81yd from all
#    eight add-spawn positions — so the tank, the heals and the AoE sit on the
#    path everything takes to reach the ledge. Combat-only because out of combat
#    the walk-in and the pre-boss muster own the raid's position and a 61.5 rung
#    in the non-combat engine would hijack the approach. It goes INERT inside the
#    leash (30yd for the whole raid, wide enough that anyone can step onto an add
#    that reached the healers) rather than owning the tick and yielding, so a bot in
#    position never contends with its own rotation and the fine positioning stays
#    the raid strategy's. Armed by DcRunState::razorDrivingMs, which the driver
#    stamps on every step but completion AND the pre-pull wait — so the camp arms
#    with the tank's pull on Grethok, releases with the thirtieth egg, and leaves
#    the approach to the ordinary walk-in and muster, with no latch to reset after
#    a wipe. That same stamp now
#    also gates IsLeaderRazorgoreRunner: the election is only cleared on a tick
#    that reaches Step::Done, and after the thirtieth egg the event stops being
#    DUE, so no such tick ever arrives — read as a bare GUID compare the runner
#    stayed elected for the rest of the raid and held its station at the orb while
#    the party walked to Vaelastrasz.)
# Data/DcTargetExclusionRegistry.{h,cpp}
#   (the third and last member of the "do not attack that" family, and the only
#    one that reaches the STOCK combat engine's target selection. FightInPlace /
#    BossPullback answer "where do we fight it"; DcNeverTargetRegistry answers
#    "is killing this progress" as a permanent (mapId, entry) fact filtering only
#    DC's own scans; this answers "will killing it right now LOSE THE RUN", and
#    it is WINDOWED — the same creature is the correct target ten seconds later.
#    Razorgore is the row it exists for: killing him before the thirtieth egg
#    breaks casts 20038 and instakills the raid, and in phase 2 he is the whole
#    objective. It reaches the engine through DungeonClearCombatStrategy's
#    override of Strategy::AppendTargetExclusions/HasTargetExclusions —
#    GatherStrategyTargetExclusions walks EVERY strategy on the bot's combat
#    engine, not a hardcoded raid list, so DC has the same authority over what
#    the raid shoots that `moltencore` and `karazhan` have with NO change to
#    mod-playerbots. That is the point: raid content DC drives keeps its combat
#    guards next to the encounter logic, in a module we control. The gate is
#    map-keyed rather than a flat true so a bot on an unlisted map never rebuilds
#    its strategy-name list per target pick; it stays fresh because the cache is
#    recomputed on every strategy add/remove and ApplyInstanceStrategies does
#    both on every map change. Tanks are never excluded — somebody still has to
#    HOLD the thing nobody may kill.)
# Value/DungeonClearDpsTargetValue.{h,cpp}
#   (decorators over the stock `dps target` / `dps aoe target` values, registered
#    under the STOCK names so they front them — the same last-wins seam
#    DungeonClearPartyMemberToHealValue uses. They exist because
#    DcTargetExclusionRegistry alone was NOT a complete guard: both stock pickers
#    return RtiTargetValue's answer BEFORE the exclusion pass runs, and
#    `bwl razorgore mark boss` paints the moon icon on Razorgore for as long as an
#    egg stands — so every DPS in the raid was handed the one creature whose
#    phase-1 death casts 20038 on all of them. Measured, tr-20260827-233058-1:
#    pull 23:31:36, boss dead 23:31:44, 17 of 25 bots dead with him. The mark is
#    deliberate and untouched (the off-tank needs it); what changes is that a
#    barred pick is re-run through the SAME stock picker with the icon disarmed
#    (a second instance constructed against DcKey::NoRti, a string value that
#    always reads "" so GetRtiIndex resolves -1), which lands the DPS on the adds
#    with the exclusion pass applied. Off a map with rows the stock answer is
#    returned verbatim, for one map-keyed table scan per pick.)
# Trigger/DungeonClearHoldFireTrigger + Action/DungeonClearHoldFireAction
#   (in DungeonClearTriggers.h / DungeonClearActions.h, implemented in
#    Action/DcRazorgoreActions.cpp; relevance DcRel::HoldFire 61.25, registered in
#    BOTH engines and exempted from the raid stand-down. The case no target picker
#    can reach: a bot that acquired the barred creature a tick before the bar came
#    up keeps auto-attacking it off GetVictim() with no further pick involved. The
#    rung drops the victim, clears `current target`, and interrupts a cast already
#    aimed at it — then goes inert. Tanks are exempt, as in the registry: somebody
#    must still HOLD the thing nobody may kill.)
# DcBlackwingLair::HoldsThePossession (DungeonEventTables.h / BlackwingLairEvents.cpp)
#   (the orb runner's channel guard, read off the bot's OWN UNIT_FIELD_CHARM rather
#    than the leader's razorDrivingMs stamp — because every reason that stamp goes
#    stale (dead leader, leader out of EVENT_DUE_RANGE, one not-due tick, `dc
#    pause`) is a reason the possession is STILL UP and still needs guarding. It
#    gates both the orb rung's trigger and a clamp in BOTH DC multipliers that
#    zeroes every action except the rung itself: 19832 is a channel the runner's
#    own rotation ends, and the rung's relevance band (62) does not outrank
#    ACTION_EMERGENCY (90) or stock `drop target` (99), which is what a rooted bot
#    with adds on it reaches for.)
# t/TestBlackwingLair.cpp
#   (the map-469 authored-data lints plus the kernel: the event's exact flag set
#    (encounterActive is the one that must never be dropped), hook 20's
#    registration, the FSM's ordering guarantees, and the runner election's hard
#    gates and preference order. Suite name starts DungeonEvent so the
#    `DungeonEvent*` gtest filter in t/run_tests.sh picks it up.)
# Util/DcRunProgress.{h,cpp}
#   (the module's one definition of PROGRESS — encounter mask grew, cleared-anchor
#    set grew, tank closed on the next anchor — hoisted out of DcStrandedRecovery's
#    anonymous namespace so the two no-progress failsafes cannot drift apart. Each
#    caller owns its own Mark, because the detector reports an EDGE and consumes
#    it: one shared mark and whichever clock ticked first would eat the other's
#    evidence. Combat is deliberately NOT one of the signals — the stranded
#    failsafe adds engagement back on top of it, DcCombatPurge must not.)
# Data/DcCombatPurgeRegistry.{h,cpp} + Util/DcCombatPurge.{h,cpp}
#   (THE UNENDABLE FIGHT. A creature alive and hostile where the party cannot
#    reach it, holding a CombatReference: instanced creatures never leash and a
#    re-homed one has nowhere to evade back to, so nothing in the core ends that
#    fight and the party stays flagged for the rest of the run — pull FSM stuck,
#    rest gates shut, clear stopped. Gundrak's Drakkari Raider (29982) is the row
#    it was written for: it has NO spawn row at all, it rides the Drakkari Rhino
#    (29931) as a vehicle accessory, and the rhino's SmartAI ejects all three off
#    waypoint path 1272070's sixteen-yard drop into the water and has them set
#    their home position where they land.
#    Two halves, and NEITHER is a reachability test — that guard was tried twice
#    and is what made the old stuck-combat hatch go inert, since `IsReachable` is
#    the chunked pathfinder and calls a 347yd holder through rock "reachable".
#    WHICH creature is a hand-vetted table; WHEN is a COMBAT-BLIND no-progress
#    clock on the global playerbot tick (a trigger cannot serve — the deadlock
#    parks bots on the combat engine, where the non-combat rungs never tick).
#    A purge also drops the stale `current target` and arms a windowed bar that
#    DcTargetExclusionRegistry carries into stock target selection, or the pickers
#    re-open the same fight next tick and the purge becomes a revolving door.
#    Settings: DungeonClear.UnreachableCombatPurgeSecs. Tests: t/TestCombatPurge.cpp.)
# t/TestCombatPurge.cpp
#   (the registry lint and the shared progress kernel's clock arithmetic —
#    unarmed/off/rollover. Suite names start Dc so t/run_tests.sh's filter picks
#    them up.)
#
# New files added: Data/Events/HallsOfStoneEvents.cpp,
#   Overrides/HallsOfStoneDriver.cpp, t/TestHallsOfStone.cpp
#   (Halls of Stone, map 599 — the Tribunal of Ages. The dungeon's bit-2
#    encounter is creditType 1 (ENCOUNTER_CREDIT_CAST_SPELL, spell 59046), so
#    BossSpawnIndex cannot derive it and the roster reads 0, 1, 3. On this map
#    that missing encounter is not a scoreboard row: GO 191296 is the only
#    functional door and the ONLY thing that opens it is Brann arriving there
#    after four gossip selections, the second of which starts a fixed 300-second
#    defend event. Five runs of tp-20260831-205458-3 finished 2/3 with zero
#    deaths, all five ending "paused for over 60s: a closed door".
#    Three objectives (escort / Tribunal / door) plus a Conditional +
#    DrivesInCombat wave driver; hooks 22-23. Unlike CoT Stratholme's Mal'ganis
#    and ToC's champions — the other creditType-1 dungeons MakeBossWithBit was
#    written for — the Tribunal has NO creature at all to anchor or target, so it
#    is authored as an objective whose event owns completion.)

# New files added: Data/Events/UtgardePinnacleEvents.cpp,
#   Overrides/UtgardePinnacleDriver.cpp, t/TestUtgardePinnacle.cpp
#   (Utgarde Pinnacle, map 575 — an entirely unauthored dungeon, and the first
#    one in the module whose ROSTER, not its geometry, is the defect. Svala's
#    instance_encounters credit entry (26668) has no `creature` spawn row
#    anywhere in the DB, because boss_svala.cpp UpdateEntry()s into it ~34s into
#    the intro cinematic — so BossSpawnIndex::Build's creditEntry -> spawn join
#    matches nothing and drops the FIRST boss silently, leaving a 3-row roster
#    for a 4-boss dungeon. tr-20260902-083808-1 then targeted Palehoof, took the
#    696yd door-blind shortcut A* offers, and ended `paused_timeout` against two
#    portcullises that only open on the deaths of bosses three and four.
#    One MakeBossWithBit row, three objectives (areatrigger 5140 / the Stasis
#    Generator / areatrigger 4991), six authored legs, two IsScriptOnly doors,
#    and hooks 25-28 — of which 27 is the only genuinely new mechanism the module
#    has needed for a while: Skadi's phase 1 cannot be damaged at all (Grauf is
#    IMMUNE_TO_PC and she is NOT_SELECTABLE), and the encounter is won by clicking
#    a Harpoon Launcher three times, each shot landing 35% of the drake's maximum
#    health from a World Trigger creature rather than from a player.)

# New files added: Data/Events/PitOfSaronEvents.cpp,
#   Overrides/PitOfSaronDriver.cpp, Util/DcPosGauntletDecision.h,
#   t/TestPitOfSaron.cpp, t/TestPitOfSaronRouteProbe.cpp
#   (Pit of Saron, map 658 — the Utgarde Pinnacle roster defect one step worse,
#    because here it does not stall the run, it makes the run SUCCEED. Scourgelord
#    Tyrannus (36658) has kill-credit DungeonEncounter rows on both difficulties
#    and NO `creature` spawn anywhere: he is created at runtime as Rimefang's
#    vehicle accessory (vehicle_template_accessory 36661 -> 36658), so
#    BossSpawnIndex::Build's spawn join drops him and the derived roster is two
#    bosses for a three-boss dungeon — against which encounterMask 0x3 reads
#    "all cleared". tr-20260907-100813-1 is that run: reported success, 2/2, 8m01s,
#    with a third of the dungeon unwalked.
#    One MakeBossWithBit row is the fix; everything else here is what is on the
#    other side of it. Krick and Tyrannus are separated by a FOUR-AREATRIGGER,
#    TWO-WAVE, STRICTLY-ORDERED GAUNTLET whose every gate is refused SILENTLY out
#    of turn — gate 1 additionally requires the orchestrator (36794) to have flown
#    85-100 seconds of Krick outro into position, which DC's own advance beats to
#    the trigger by a wide margin. So: one conditional in-combat driver (hook 29,
#    with its FSM as the pure kernel Util/DcPosGauntletDecision.h), one anchored
#    ledge gate (hook 30) whose objective anchor sits deliberately 22yd OUTSIDE
#    areatrigger 5633's sphere so arriving cannot fire it, two authored legs with
#    NO_STOP across the gauntlet and the icicle tunnel, and one IsScriptOnly door.
#    Both hooks forge their own CMSG_AREATRIGGER, which is level-triggered where
#    the test harness's relay is edge-triggered — the property that makes an early
#    crossing cost nothing.
#    NOT fixed here, and it is the difference between reaching Tyrannus and
#    beating him: mod-playerbots' `wotlk-pos` TyrannusAction spreads ranged 10yd
#    and does nothing about Overlord's Brand (69172), whose branded healer heals
#    the boss for 5.5x. That is combat rotation and belongs on a mod-playerbots
#    branch; DC has no seam that suppresses a bot's own heals.)

# New files added: Data/Events/HallsOfReflectionEvents.cpp,
#   Overrides/HallsOfReflectionDriver.cpp, Util/DcHorWaveDecision.h,
#   Util/DcHorEscapeDecision.h, Action/DcHorStayAheadAction.cpp,
#   t/TestHallsOfReflection.cpp, t/TestHallsOfReflectionRouteProbe.cpp
#   (Halls of Reflection, map 668 — the first dungeon in this module where DC's
#    pull/advance pipeline is the WRONG TOOL rather than a tool that needs help.
#    There is no trash to clear and no boss to pull: Falric, Marwyn and all 34
#    wave mobs pre-spawn invisible, NOT_SELECTABLE and SetImmuneToAll, and the
#    instance activates them a wave at a time, on the FARTHEST player. Left to
#    itself the clear walks the tank onto Marwyn's invisible, immune spawn and
#    stands there — tr-20260907-100815-2, 5m38s, encounterMask 0x0, aborted by
#    hand. Thirteen earlier attempts never got through the door at all (the
#    24710/24712 attunement gate, fixed by 10fc340).
#    So: TWO conditional in-combat drivers, which is one more than any other map
#    has, and between them they own roughly three quarters of the run. Hook 32
#    holds the altar from wave 1 to Marwyn's death — mostly by REFUSING, because
#    the map's 70.5yd leash wipes the whole event and respawns every dead mob,
#    and a bot that releases is locked out until the survivors wipe. Hook 35
#    drives the escape, whose only real difficulty is geometry: four ice walls on
#    an absolute clock, a Lich King who never pauses, a 10yd 7068-per-second ring
#    and a 10 000-damage zap plus knockback for anyone who falls behind him along
#    a path that runs -x -y. Both FSMs are pure kernels (Util/DcHorWaveDecision.h,
#    Util/DcHorEscapeDecision.h).
#    The roster row is the Nexus Frozen Commander shape, not the Pit of Saron
#    one: "Escaped from Arthas" has DungeonEncounter.dbc rows 843/844 and NO
#    instance_encounters row, so there is no credit entry AND no kill bit
#    anywhere — completion is the instance's own boss-state slot, and
#    encounterMask 0x3 is the EXPECTED final mask on a cleared run.
#    Two module-wide changes come out of this map. The hold-fire rung's blanket
#    tank exemption is now per-row, keyed on DcTargetExclusionRow::alsoTank,
#    because the escape's Lich King (36954, unit_flags 0, no immunities) must
#    have NO holder at all: he heals to 75% below 70, and one bot holding him as
#    a victim makes DcCombatFlag::IsEngaged true for the party and freezes every
#    MayDrive rung while he walks at them. And DungeonClearHorStayAheadAction
#    (DcRel::HorStayAhead, 56) is a new per-bot survival rung that moves FORWARD
#    rather than radially away — the generic hazard vacate's away-from-emitter
#    bearing is the one direction that makes the zap rule worse.)

# New files added: Data/Events/BlackrockSpireEvents.cpp, Util/DcRunWing.{h,cpp},
#   t/TestRunWing.cpp
#   (Blackrock Spire, map 229, split into LBRS and UBRS. The wing is a per-RUN
#    choice latched on the run owner — test row, `.dc on <wing>` / `.dc wing`,
#    the LFG dungeon, else the wing region the party stands in, else LBRS —
#    because the two wings share one portal and proximity picks UBRS there. The
#    wing's terminal boss completes the run, the test harness counts only the
#    wing's encounter bits, and wing-tagged DcNavPenaltyRegistry rows keep each
#    wing's routes out of the other.)

# New files added: t/TestBlackrockDepthsRouteProbe.cpp
#   (Blackrock Depths, map 230, split into the Detention Block and the Upper
#    City as a second Explicit wing layout. The probe routes the Upper City legs
#    from the portal and checks none crosses the Ring of Law arena floor, which
#    is why map 230 needs no wing-tagged nav fence.)

# New files added: Util/DcRawPathDrive.h, Util/DcPartyWideChain.{h,cpp},
#   Util/DcBreathSource.h, Util/DcBreathProvision.{h,cpp},
#   Data/Events/BlackfathomDeepsEvents.cpp
#   (Blackfathom Deeps, map 48: party-wide off-mesh chains — the authored
#    RawPath / Jump steps every bot runs itself, with a fall volume — and water
#    breathing for the Serra'kis dive: the shaman / warlock / elixir planner,
#    the EnsureWaterBreathing step and the breath watchdog.)

# New files added: Data/Events/ZulGurubEvents.cpp,
#   Data/Events/TempleOfAhnQirajEvents.cpp, t/TestClassicRaids.cpp
#   (The two classic raid bosses with no creature spawn row, so BossSpawnIndex
#    dropped them: Zul'Gurub's High Priestess Arlokk, summoned by the Gong of
#    Bethekk, and Temple of Ahn'Qiraj's Ouro, summoned when a player reaches his
#    mound. Each gets a boss anchor on its trigger plus a repeatable event.)
