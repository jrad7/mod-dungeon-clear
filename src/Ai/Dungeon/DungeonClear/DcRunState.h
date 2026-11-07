/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCRUNSTATE_H
#define _PLAYERBOT_DCRUNSTATE_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "G3D/Vector3.h"
#include "ObjectGuid.h"
#include "Timer.h"

#include "Ai/Dungeon/DungeonClear/Util/DcChessConductor.h"
#include "Ai/Dungeon/DungeonClear/Util/DcProgressWatchdog.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunProgress.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunWing.h"
#include "Ai/Dungeon/DungeonClear/Util/DcThrottle.h"

// The authoritative, leader-owned state of one dungeon-clear RUN — the run's
// identity/mode, its current manual-override objective, and the two cross-bot
// leader-fight signals that used to live in translation-unit-static maps. Owned
// as a single value (DungeonClearRunStateValue, "dungeon clear run state") so the
// whole run resets in lockstep through named transitions, exactly like the
// sub-feature structs DcApproachState and DcPullContext already do one level down.
//
// This is the third and last of the "one struct, one Reset()" consolidations that
// each ended a recurring "the X is flaky" family. DcApproachState fixed the
// approach FSM; DcPullContext fixed the pull FSM; DcRunState fixes the PARTY/RUN
// level — the enabled/paused/pause-cluster/selected-boss values whose resets were
// hand-replicated (as slightly different subsets) across DisableDungeonClear and
// the DcOn/Off/Skip/Go/Resume chat-action clusters, plus the leader-combat-since
// and party-engaged latches that were file-static maps each with their own mutex.
// A stale latch surviving a pause / skip / resume / boss-change was the single
// most common root cause in the whole bug log; folding these here so exactly one
// Reset() clears them makes that class structurally impossible.
//
// Add a new run-level field HERE (never as a separate value) so it can never be
// forgotten by a reset.
//
// Leader-owned. Followers read `enabled`/`paused` cross-context through the
// leader's copy of this value (DcLeaderSignal::IsInPausedDungeonClearRun and the
// party-tank / camp-hold gates), the same pattern DcPullContext uses. Each bot
// still holds its OWN DcRunState value; a follower's stays at defaults (it never
// leads a run) and reading it is harmless.
//
// NOTE on the pull PREFERENCE: the advanced-pull tri-state (`dungeon clear pull
// setting`) and its behavioral bool (`dungeon clear pull mode`) are deliberately
// NOT folded in here. Their lifetime is the odd one out — the preference is
// settable BEFORE a run and must survive the disabled window to be applied by
// `dc on`, and toggling it live is coupled to daze-immunity + camp-seed side
// effects (DungeonClearChatActions::ApplyPullSetting). They are already funneled
// through ApplyPullSetting / DisableDungeonClear and are excluded from every
// blanket reset anyway, so folding them here would add surface without any
// reset-safety gain. They stay as their own values.
// One key/value an event hands the test harness for its run record (see
// DcRunState::SetTestExtra). `numeric` values are written to the record's JSON
// as bare numbers, everything else as strings.
struct DcTestExtra
{
    std::string key;
    std::string value;
    bool numeric = false;
};

struct DcRunState
{
    // === run session — cleared by Reset() (dc on / dc off / death / all-cleared) ===
    bool        enabled = false;   // the run's master switch (leader-owned)
    bool        paused  = false;   // soft-stop layered on `enabled`; see OnResume

    // This run is on a RAID map — stamped by `dc on` from Map::IsRaid(), read
    // via DcRun::IsRaid. Raid runs change the non-interference contract: DC
    // owns everything BETWEEN fights and stands down completely during a boss
    // encounter (playerbots' raid strategies own the fight — see
    // Util/DcBossStandDown). Stamped rather than re-derived so the flag is
    // one honest fact of the run session, cleared with it by Reset().
    bool        raidRun = false;

    // Short human phrase describing WHY the run is paused, for the status panel to
    // tell a manual `dc pause` apart from a door auto-pause. Set at each pause site
    // the moment `paused` flips true; read only while paused. Empty falls back to a
    // generic "holding position".
    std::string pauseReason;

    // GUID of the closed door the tank auto-paused in front of (empty unless paused
    // specifically for an unopenable door). While set, DungeonClearDoorReopenedTrigger
    // polls this one door; the moment it reads OPEN the clear auto-resumes. Stamped
    // ONLY by the door auto-pause site — a manual `dc pause` leaves it empty so an
    // unrelated door can never auto-resume a hand-held pause.
    ObjectGuid  pausedDoor;

    // Boss entry of a manual boss override (0 = no override; normal auto progression).
    // Set by DcGoAction, cleared by dc on / dc off / dc skip.
    uint32      selectedBossEntry = 0;

    // Wait at Boss (DungeonClear.WaitAtBoss): GUID of the last boss the run
    // auto-paused at for the human's go-ahead, stamped AT PAUSE TIME by the
    // engage-boss gate so each boss waits exactly once per run. Deliberately
    // NOT part of OnResume's pause-cluster clear — the stamp is what stops the
    // gate from re-pausing the instant the human resumes. Cleared only by the
    // full Reset() (a fresh run earns a fresh heads-up at every boss).
    // See DcWaitAtBossDecision.h for the whole design.
    ObjectGuid  waitedBossGuid;

    // getMSTime() at which the SEALED-ENCOUNTER muster began holding the boss engage
    // (0 = not mustering). See SealedEncounterRegistry: for a boss whose room locks
    // on encounter start, the engage waits until no member is still outside the room,
    // and this is the clock that bounds that wait so a member who cannot path in
    // can't hold the run open. Re-armed to 0 whenever the tank leaves the boss's
    // approach range, so each attempt gets a fresh budget.
    uint32      sealedMusterSince = 0;

    // === cross-bot leader-fight signals (were the g_* file-static maps) ============
    // Both are keyed, in the old design, by the LEADER's GUID and only ever read/
    // written for the resolved leader — i.e. they are facets of the leader's run.
    // Folded here they drop their standalone mutexes: all members of one group tick
    // on the same map thread, so a follower writing the leader's DcRunState is the
    // same single-threaded cross-bot access DcPullContext already relies on.

    // getMSTime() at which the leader's CURRENT continuous combat began (0 = out of
    // combat). Maintained lazily on read by DcLeaderSignal::LeaderCombatSince so the
    // threat-lead window measures from a FRESH combat start on the Leeroy / walk-in /
    // general-assist path (which has no pull-phase transition to mark fight start).
    // Was g_leaderCombatSince.
    uint32 leaderCombatSinceMs = 0;

    // getMSTime() of the leader's FIRST damage in the current fight (0 = none
    // yet), stamped by DcLeaderSignal::NoteLeaderHit from the damage hook. The
    // threat lead runs from here: the combat flag can rise seconds before the
    // tank lands anything, and a lead measured from the flag let DPS open on a
    // mob the tank had not touched (Pull Lab O2, 20/33 scenarios).
    uint32 leaderFirstHitMs = 0;

    // getMSTime() of the last positive "some party member is in combat" observation,
    // the hysteresis latch behind IsPartyEngagedLatched (absorbs a one-tick combat
    // gap so the party doesn't snap out of "assist" mode mid-fight). 0 = never seen
    // engaged. Was g_partyEngagedLatch.
    uint32 partyEngagedLatchMs = 0;

    // === Smart Rest hysteresis latch (leader-owned, read cross-bot) ================
    // Maintained by DcSmartRest::UpdateLatch from the leader's between-pulls gate;
    // followers read it through the party tank (DcSmartRest::IsLatched). Combat does
    // NOT clear it — a patrol interrupting the rest goes inert (out-of-combat
    // triggers), then the still-set latch resumes the rest afterwards. The timeout
    // clock deliberately spans such interruptions.
    bool   smartRestLatched   = false;  // party is in a full-rest cycle
    uint32 smartRestSinceMs   = 0;      // getMSTime() when latched (timeout clock)
    uint32 smartRestRearmAtMs = 0;      // after a timeout release: no re-latch before this

    // === post-combat rez recovery (leader-owned, written cross-bot) ===============
    // Maintained by DcRezRecovery::Evaluate — called from the (alive) leader's
    // relaxed party-died trigger and from every bot's rez-party trigger, so the
    // clocks stay live even when the leader itself is the corpse (a follower
    // writes them cross-bot, the same access pattern as the latches above).
    uint32 rezPendingSinceMs = 0;  // getMSTime() recovery went pending OUT of combat;
                                   // 0 = not pending (cleared while the party fights,
                                   // so combat never burns the timeout budget)
    uint32 rezAnnounceMs     = 0;  // getMSTime() of the episode's start announcement
                                   // (dedup: one announce per recovery episode; also
                                   // marks the episode so the "party restored" resume
                                   // line fires exactly once when deaths clear)
    // The NoRezzer floor's two clocks — see the branch in DcRezDecision.h. Stamped
    // off the PREVIOUS tick's verdict (the kernel is the thing that decides whether
    // a rezzer is left, so the glue cannot know before calling it); a tick of lag is
    // immaterial against graces measured in seconds.
    uint32 noRezzerSinceMs      = 0;  // getMSTime() the party first had no rezzer
    uint32 noRezzerQuietSinceMs = 0;  // ...and first read unengaged AND unflagged
    // getMSTime() the instance was first seen refusing every resurrect
    // (InstanceScript::IsEncounterInProgress — see the rezBlocked branch in
    // DcRezDecision.h); 0 = the spell is castable. Cleared the moment the block
    // lifts, so a boss mid-reset gets a fresh wait rather than a stale verdict.
    uint32 rezBlockedSinceMs    = 0;

    // === raid boss stand-down (leader-owned, read cross-bot) ======================
    // The hysteresis state + per-tick memo behind DcBossStandDown::IsActive: while
    // a raid encounter is live every DC combat behavior and recovery ladder goes
    // inert so the playerbots raid strategy owns the fight. Evaluated on the
    // leader at most once per tick window; members read the verdict cross-bot,
    // the same access pattern as the latches above. See Util/DcBossStandDown.h.
    bool   standDownActive   = false;  // the current verdict
    uint32 standDownSignalMs = 0;      // getMSTime() an encounter signal last read true
    uint32 standDownEvalMs   = 0;      // memo stamp of the last leader evaluation

    // === raid pre-boss muster (leader-owned) ======================================
    // Phase machine state for DcRaidMusterDecision (Plan C): the full-stop that
    // stages, tops off and rebuffs the raid before every boss pull. Keyed to the
    // boss entry so a kill / skip / boss change re-arms a fresh muster; cleared
    // wholesale by Reset(). musterArmedMs stamps the whole-muster budget (the
    // hard ceiling that releases the pull from any phase), while
    // musterPhaseSinceMs stamps the per-phase ones. musterRestOverride remembers
    // that the muster itself pushed the RestHealthPct/RestManaPct per-run
    // overrides to full (so bots actually eat/drink to the bars) and must retract
    // them on release without clobbering an override the player set by hand.
    uint8  musterPhase = 0;           // DcRaidMusterDecision::Phase
    uint32 musterPhaseSinceMs = 0;    // getMSTime() the phase was entered
    uint32 musterBossEntry = 0;       // boss this muster belongs to (0 = none)
    uint32 musterArmedMs = 0;         // getMSTime() the muster armed for this boss
    uint32 musterRebuffIssuedMs = 0;  // getMSTime() the rebuff round was issued
    bool   musterRestOverride = false;

    // === the run's no-progress clocks (leader-owned) ==============================
    // Two failsafes ask "has this run stopped moving?" and both read the same
    // three signals (DcRunProgress: an encounter completed, an anchor cleared,
    // the tank closing on the next anchor). They get SEPARATE marks because the
    // detector reports an edge and consumes it — one shared mark and whichever
    // clock ticked first would eat the other's evidence.
    //
    // `progress` is the stranded-member recovery failsafe's, ticked live on the
    // leader by DcStrandedRecovery::Evaluate (its single clock-owner site). It
    // additionally re-arms on party ENGAGEMENT — a fight is progress — so a
    // legitimately slow pull or a long boss fight never trips it. When it goes
    // stale past StrandedRecoveryNoProgressSecs while a bot member is stuck out of
    // range (fell under the world / wedged), the leader teleports the strays to
    // itself. See Util/DcStrandedDecision.h + DcStrandedRecovery.
    //
    // `purgeProgress` is DcCombatPurge's, ticked on the global playerbot tick.
    // It is deliberately COMBAT-BLIND: its subject is a fight that can never end
    // (a mob stranded off the navmesh holding the party flagged forever), so a
    // clock that engagement re-armed could never fire in the one state it exists
    // for. See Util/DcCombatPurge.h.
    DcRunProgress::Mark progress;
    DcRunProgress::Mark purgeProgress;

    // === final-loot hold (leader-owned) ===========================================
    // getMSTime() the all-cleared trigger first held the run end for pending boss
    // loot (0 = not holding). Capped by DcLootPolicy::kFinalLootWaitMs.
    uint32 finalLootHoldSinceMs = 0;

    // === Blackwing Lair — Razorgore's orb and egg run (leader-owned) =============
    // The one encounter DC orchestrates from INSIDE a raid fight (see
    // DungeonEvent::encounterActive). All of it is the leader's; the runner GUID is
    // the only field read cross-bot, through DcLeaderSignal::GetRazorgoreOrbStation,
    // by the runner's own orb rung.
    //
    // Note what is NOT here: any timer for the 90s mind control or the 60s charmer
    // lockout. Both are auras on live units and are read straight off them, which
    // stays correct through a wipe, a despawn and the phase flip — a mirrored timer
    // would not.
    // getMSTime() of the last tick the Razorgore driver had work to do — which
    // starts at the tank's pull on Grethok and ends with the thirtieth egg.
    // Read cross-bot (DcLeaderSignal::IsLeaderRazorgoreDriving / …Runner) by the
    // raid's camp rung and the elected runner's own rung, so both arm and release
    // with the encounter and need no latch of their own: the driver simply stops
    // stamping when phase 1 ends.
    //
    // Nothing is stamped BEFORE the pull (Step::WaitPull). Up to then the raid's
    // position belongs to the ordinary clear — the advance is walking it to
    // Grethok's boss anchor and the muster is topping it off — and a camp rung
    // armed underneath that would fight the pipeline for every bot.
    uint32     razorDrivingMs = 0;

    ObjectGuid razorRunnerGuid;            // elected orb runner (empty = none yet)
    uint32     razorRunnerPickedMs = 0;    // getMSTime() of the last election (throttle)
    ObjectGuid razorEggGuid;               // the egg currently being driven at
    uint32     razorEggElectedMs   = 0;    // getMSTime() it was elected / last got closer
    uint32     razorMoveIssuedMs   = 0;    // getMSTime() of the last boss spline
    float      razorEggBestDist    = 0.0f; // closest approach to it (no-progress clock)
    uint8      razorEggAttempts    = 0;    // polite cast attempts before going triggered
    // Eggs parked for this pass — unreachable, or refusing every cast. Retried in
    // full once the reachable field is exhausted, so a wedge costs time, never the
    // encounter.
    std::vector<ObjectGuid> razorEggSkipped;

    // === TRANSIT state (leader-owned) ===========================================
    // Blackwing Lair's Vaelastrasz -> Broodlord crossing and Halls of Lightning's
    // Bjarngrim -> Volkhan Slag Furnace share this block, because a bot is only
    // ever on one map and the two crossings can never be live at once (see the
    // transit blocks in DungeonEventTables.h). All of it belongs to the leader;
    // the CURSOR is the only part read cross-bot, through
    // DcLeaderSignal::GetTransitAnchor, by the pack rung on every other member —
    // and that rung is gated to map 469, so on 602 the publication is telemetry.
    //
    // The cursor is a POSITION, not an index into somebody else's copy of the
    // route: a follower must be able to answer "where is the leader taking us"
    // with one struct read and no table lookup, and publishing the point rather
    // than the index means a route edit can never leave the two halves disagreeing
    // about which anchor index 7 is.
    //
    // getMSTime() of the last tick the transit driver had work to do — from the
    // leader's arrival at the staging point to its arrival at the far end (the
    // Broodlord standoff / the Slag Furnace's mid ledge). Read cross-bot (DcLeaderSignal::IsLeaderTransitDriving) by the
    // pack rung, so that rung arms and releases with the crossing and needs no
    // latch of its own: the driver simply stops stamping when the leg is over.
    uint32 transitDrivingMs = 0;
    uint32 transitCursorIndex = 0;   // authored anchor the leader is walking toward
    float  transitCursorX = 0.0f;    // ...and that anchor's position, published
    float  transitCursorY = 0.0f;
    float  transitCursorZ = 0.0f;

    // getMSTime() the crossing armed (the tick the leader first read due inside
    // the corridor). The gather gate's budget is measured from here, and
    // `transitGathered` latches it open so a raid that spreads out mid-crossing
    // is handled by the pack rung rather than by re-gathering at the staging
    // point behind them.
    uint32 transitArmedMs = 0;
    bool   transitGathered = false;

    // The current hold, if any: getMSTime() it began (0 = the driver is advancing)
    // and which of DcSuppressionTransitDecision::Hold it is. Every hold reason
    // carries its own watchdog and these are its clock — one wedged straggler must
    // cost the crossing seconds, never the run.
    uint32 transitHoldSinceMs = 0;
    uint8  transitHoldReason = 0;

    // ...and whether that hold's watchdog has already given up on it. The release
    // LATCHES (the verdict keeps reporting the hold so the leg keeps walking), so
    // this is what makes the WARN fire once per release instead of once per tick.
    bool   transitHoldTimedOut = false;

    // --- an event's pack leash (DungeonEvent::packLeash) --------------------
    // The two clocks DcPackLeash::Decide carries across ticks: when the current
    // hold began, and when its watchdog last released (the walk window). Both
    // self-clear the moment the party is formed up, so no teardown owns them.
    uint32 leashHoldSinceMs = 0;
    uint32 leashReleasedAtMs = 0;

    // The Violet Hold keeper / Black Morass rift a bot has COMMITTED to, held
    // across the portal-death boundary so a tank two yards from a corpse is not
    // instantly re-aimed at the next spawn tens of yards away. Empty = nothing
    // committed; the driver re-validates (alive, still resolvable) on every read
    // and clears it in place, so there is no staleness for a run teardown to
    // handle. Here rather than in a file-scope thread_local map for the reason in
    // Util/DcThrottle.h, and it matters more for a lock than for a log: a lapsed
    // lock re-selects, which is precisely the behaviour it exists to prevent.
    ObjectGuid vhKeeperLock;
    ObjectGuid bmRiftLock;

    // --- Pit of Saron: the Ymirjar gauntlet (map 658) ------------------------
    //
    // The two clocks DcPosGauntlet::Decide carries across ticks, plus the state
    // it last reported so the driver logs one line per transition instead of one
    // per tick.
    //
    // BOTH CLOCKS BELONG TO A PHASE, NOT TO A STATE. `posGauntletPhase` is the
    // instance's own DATA_INSTANCE_PROGRESS value (2, 3 or 4), and it is what the
    // kernel compares against to decide whether to re-stamp: Arm and Gate1 are
    // both phase 2, Wave1 and Gate2 are both phase 3, and a clock that restarted
    // on the sub-state transition would restart exactly where it is needed — the
    // wave-summon grace window opens on the gate being ACCEPTED, which is the same
    // tick the sub-state flips from Gate to Wave.
    //
    // Here rather than in a file-scope map for the Util/DcThrottle.h reason: a map
    // keyed on the bot and owned by a map-update thread is neither pruned nor
    // stably owned, and a lapsed clock here would re-open a spent grace window.
    uint8  posGauntletPhase = 0;         // DATA_INSTANCE_PROGRESS the clocks belong to
    uint32 posGauntletPhaseMs = 0;       // getMSTime() the party entered it
    uint32 posGateHoldMs = 0;            // ...and when the leader first stood in its gate
    uint32 posWaveHoldMs = 0;            // ...and when a live wave mob first stood in
                                         // reach saying nothing (the standoff clock)
    bool   posGateStallReported = false; // has the "forged and still refused" WARN fired?
    bool   posWaveArmedLatched = false;  // has the wave-1 arming hold released this
                                         // phase? ONE-WAY within a phase and reset by
                                         // the kernel on the progress bump, because
                                         // the armed COUNT alone falls back below the
                                         // quorum as the party kills the wave
    uint8  posGauntletState = 0;         // DcPosGauntlet::State last logged

    // --- Halls of Reflection: the altar and the escape (map 668) -------------
    //
    // TWO DRIVERS, TWO BLOCKS, and neither carries a decision clock the way the
    // Pit of Saron block does — both kernels derive their state fresh from the
    // instance every tick. What is stored here is what the WORLD cannot answer:
    // which state was last LOGGED (so the driver prints one line per transition
    // rather than one per tick), and the two one-shot report latches.
    //
    // horRestartLogged is scoped to one wipe episode. A leash wipe on this map is
    // both expensive (waves 1-4 replay with every dead mob respawned) and
    // INVISIBLE from every other probe — after it the counter reads 0, all 34 mobs
    // are hidden again and nothing is fightable, which is indistinguishable from a
    // healthy gap between waves. The one WARN line is the whole signal, so it must
    // fire once per episode and not once per tick.
    uint8  horWaveState = 0;             // DcHorWaves::State last logged
    uint32 horWaveStateMs = 0;           // getMSTime() it was entered
    bool   horRestartLogged = false;     // has this wipe episode been named?

    // The escape's stall watchdog is keyed on the STOP rather than on the state,
    // because the party legitimately cycles Hold -> Fight -> Hold several times at
    // one wall and a per-state latch would re-report on every cycle. horEscapeStop
    // is the stand point the latch belongs to; horEscapeStopHoldMs is when the
    // Lich King first came inside LK_STALL_DIST of the leader with that wall still
    // shut and its adds still up.
    uint8  horEscapeState = 0;           // DcHorEscape::State last logged
    uint32 horEscapeStateMs = 0;
    uint8  horEscapeStop = 0;            // the stop the stall latch belongs to
    uint32 horEscapeStopHoldMs = 0;
    bool   horEscapeStallReported = false;
    uint32 horEscapeTargetStopMs = 0;    // when horEscapeStop last CHANGED

    // HOW LONG A SUMMON HAS BEEN UP WITH NONE OF IT ON THE TANK, and whether the
    // party has ever actually made the stop it is standing at. The first arms the
    // driver's pickup (a tank with nothing on it and nothing in reach did nothing
    // at all for eighty-seven seconds on tr-20260908-225156-15); the second tells
    // an unfinished 180yd LEG, which must end on the stand point, from a chase
    // step that drifted off a stop the party already owns, which must end on the
    // near edge of the band. Both are cleared whenever the stop changes.
    uint32 horEscapeIdleMs = 0;
    bool   horEscapeReachedStand = false;

    // THE PER-FOLLOWER ADVANCE LATCH (DcHorEscape::DecideFollow). Set when this
    // bot is further from the party's stand point than STAND_LEAVE_LEASH,
    // cleared when it gets inside STAND_LEASH — the same Schmitt pair the driver
    // uses on the tank, and for the same reason: without the hysteresis the rung
    // hands the tick back to MoveChase halfway through a 136yd leg and the bot
    // is walked back onto the add it left.
    bool   horFollowAdvancing = false;

    // --- Blackrock Depths: the Iron Hall's safe-spot hop (map 230) -------------
    //
    // The hop in flight (DungeonClearIronHallSafeSpot): where this bot is going
    // and when it set off (0 = none). While it is set the rung keeps the tick and
    // the follow-tank clamp holds, so neither MoveChase nor the follow can turn
    // the bot round halfway and park it back in the fire.
    float  ironHallHopX = 0.0f;
    float  ironHallHopY = 0.0f;
    uint32 ironHallHopMs = 0;
    // When this bot was first seen standing still in the fire (0 = it is not).
    uint32 ironHallStillMs = 0;

    // --- Blackrock Depths: the Lyceum close-up (map 230) ----------------------
    //
    // When this bot started walking back inside DcLyceumCloseUp::CLOSE_TO of its
    // tank (0 = it is not closing up). The latch is the hysteresis: begun past
    // LEASH, the walk runs until CLOSE_TO, not until the bot crosses LEASH again.
    uint32 lyceumCloseUpMs = 0;

    // --- The Culling of Stratholme: the ten waves (map 595) ------------------
    //
    // ONE CLOCK AND ONE STATE ID, which is all this phase needs — and the reason
    // it needs so much less than Pit of Saron or Halls of Reflection is worth
    // recording: the counter it drives off is MONOTONIC with exactly two values in
    // the window, so there is no wipe to detect, no restart to orchestrate and no
    // phase to re-stamp clocks against.
    //
    // cosWaveStandoffMs runs ONLY while a live wave mob stands inside engage range
    // with nobody in combat. That is the one shape on this map that can deadlock
    // forever (a parked party and a parked mob, neither relocating, so neither
    // ever takes an aggro check), and the clock is what bounds it. Cleared by the
    // kernel in every other shape, so the budget is always measured from the
    // current silence.
    uint8  cosWaveState = 0;         // DcCosWaves::State last logged
    uint32 cosWaveStateMs = 0;       // getMSTime() it was entered
    uint32 cosWaveStandoffMs = 0;    // when the current silence began (0 = not silent)
    uint32 cosWaveRestMs = 0;        // ...and when the current rest window opened,
                                     // which bounds it: a party that can never top
                                     // off must not be able to hold the phase for
                                     // ever (see WAVE_REST_BUDGET_MS)
    uint32 cosWaveRestSpentMs = 0;   // futile rest banked from windows that already
                                     // CLOSED with the party still short. The budget
                                     // is measured across the phase, not per window,
                                     // so an interrupting wave fight can no longer
                                     // hand a stuck party a fresh full one; only
                                     // actually recovering clears it.

    // --- Trial of the Champion: the arena driver (map 650) -------------------
    //
    // Three "since when" clocks the kernel round-trips (DcTocDriver::Decide), and
    // the last progress value the telemetry line reported. No latch: the counter
    // rewinds on a full wipe, so everything the driver knows is re-derived from it
    // every tick and these only measure how long the current shape has held.
    uint8  tocState = 0;               // DcTocDriver::State last logged
    uint32 tocStateMs = 0;             // getMSTime() it was entered
    uint32 tocMountedSinceMs = 0;      // the tank has been on a horse since (muster timeout)
    uint32 tocUnmountedSinceMs = 0;    // ...on foot through the joust since (mount WARN)
    uint32 tocStrandSinceMs = 0;       // progress 8, no Knight and no announcer, since
    uint32 tocLastProgress = 0xFFFFFFFFu;  // the progress the telemetry last reported

    // --- The Oculus: the party plan (map 578, on the RUN OWNER) ----------------
    //
    // The driver kernel's verdict, memoised for PLAN_MEMO_MS and read by every
    // member's rider rung (DcOculusDriver::Decide). Plus the three "since when"
    // clocks it round-trips. No latch: every field is re-derived from the
    // encounter slots and the census each refresh.
    uint32 ocPlanStampMs = 0;          // getMSTime() of the last refresh (0 = never)
    uint8  ocPlanPhase = 0;            // DcOculusDriver::Phase
    uint8  ocPlanDest = 0xFF;          // DcOculus::Site
    bool   ocPlanHover = false;
    uint8  ocPlanAction = 0;           // DcOculusDriver::Action
    uint32 ocPlanLegSeq = 0;           // bumps when a new leg starts (phase or destination change)
    bool   ocPlanTankOnFootOnDest = false;
    bool   ocPlanEregosEngaged = false;
    uint8  ocDriverState = 0xFF;       // DcOculusDriver::State last logged
    uint32 ocDriverStateMs = 0;
    uint32 ocMusterSinceMs = 0;
    uint32 ocLandWaitSinceMs = 0;
    uint32 ocLastRegroupMs = 0;

    // --- The Oculus: this member's drake (map 578, on EVERY member) ------------
    ObjectGuid ocBaseGuid;             // the drake PrepareBase last set up
    uint32 ocMountIssuedMs = 0;        // the Call cast went out (the settle clock)
    uint32 ocMasterWaitSinceMs = 0;    // a master holding its own mount for its members, since
    uint32 ocEssenceWaitSinceMs = 0;   // on Drakos's ring without an essence, since (GIVER_WAIT_MS)
    uint32 ocLegSeq = 0;               // the plan leg this member's cursor belongs to
    uint8  ocLegDest = 0xFF;
    uint8  ocLegPhase = 0;
    bool   ocLegPadCentre = false;     // re-planned onto lane 0 after PAD_FALLBACK_MS
    uint8  ocLegCount = 0;
    uint8  ocLegCursor = 0;
    float  ocLegWp[18]{};              // up to six waypoints, x y z
    uint32 ocLegArrivedMs = 0;
    uint32 ocProgressMs = 0;           // when the drake last moved LEG_PROGRESS_YD
    float  ocProgressX = 0.0f, ocProgressY = 0.0f, ocProgressZ = 0.0f;
    bool   ocStallReissued = false;
    uint8  ocRiderAction = 0;          // DcOculusRider::Action last logged

    // --- Karazhan: the chess event (map 532) --------------------------------------
    //
    // The conductor's block on the RUN OWNER and each member's own half on every
    // bot — see DcChessRunState (Util/DcChessConductor.h). Cleared by ClearChess
    // when a finished game has to be played again, and by Reset().
    DcChessRunState chess;

    // === test-harness telemetry (leader-owned; SURVIVES Reset) ====================
    // The seam between a long scripted event and the `.dc test` harness
    // (Karazhan chess plan, T1). Both fields are written by the event on the RUN
    // OWNER's DcRunState (the leader; the harness's tank) and only ever read by
    // DcTestRunJob — nothing in the clear itself reacts to them, so an ordinary
    // (non-test) run pays two stores per event step and nothing else.
    //
    // WHY THEY SURVIVE Reset(): the run's own success path is DisableDungeonClear,
    // which Reset()s this struct, and the harness only samples at 1 Hz. A value
    // wiped in the same tick the event completed would never reach the record —
    // exactly the final `looted` / `gameTimeS` the record exists to carry. So
    // Reset() carries this block across, and the harness clears it explicitly
    // (ClearTestTelemetry) before it issues `dc on`, which is the one boundary a
    // test run's telemetry actually belongs to.
    //
    // eventProgressSeq — the EVENT PROGRESS SEQUENCE. A counter a running DC
    // event bumps (BumpEventProgress) whenever it makes real headway that the
    // harness cannot see for itself: chess bumps it on each accepted move, each
    // piece death and each phase change. The harness treats a CHANGE in it
    // exactly like a boss kill or an anchor clear — it resets the no-progress
    // clock — so a ten-minute game with no kills and no anchor movement is not
    // failed as a livelock. Only the change matters (the harness compares it to
    // the last value it saw), so wrap-around is harmless. Any future long event
    // can use the same counter; bump it on genuine progress only, never on a
    // timer, or it blinds the livelock net it is exempting the event from.
    //
    // testExtras — a flat key/value map the event fills for the run record
    // (schema 13 `extras`): chess writes attempts, losses, gameTimeS, restarts,
    // material, cheat counts, controllerChurn, looted. Setting an existing key
    // overwrites it in place (insertion order is kept for the record), so an
    // event can simply re-publish its counters on every change. Keep it small —
    // it is copied into the harness once a second.
    uint32 eventProgressSeq = 0;
    std::vector<DcTestExtra> testExtras;

    // PartyWide chain falls this run (DcPartyWideChain::HandleFall): every time
    // a bot ended up in an event's fall volume. Published as the `chainFalls`
    // test extra and in the DcDiag snapshot; zero across a batch is the proof
    // the stone coordinates are right.
    uint32 chainFalls = 0;

    // Blackfathom Deeps' Forgotten Pool exit (BlackfathomDeepsEvents.cpp): the
    // swim leg from where the tank fell in to the ramp, its cursor, progress
    // watchdog and wedge count. Built on the event's first tick, dropped when it
    // ends. `poolExits` counts the climbs this run, published as the `poolExits`
    // test extra.
    std::vector<G3D::Vector3> poolExitLeg;
    uint32 poolExitCursor = 0;
    uint32 poolExitWedges = 0;
    DcProgressWatchdog poolExitWatch;
    uint32 poolExits = 0;

    // Stratholme's trap-alley pit rescues this run (StratholmeEvents.cpp,
    // DcStratTrapAlley.h), published as the `stratPitRescues` test extra.
    uint32 stratPitRescues = 0;

    // Karazhan's library-pit rescues this run (KarazhanEvents.cpp,
    // DcKaraLibraryPit.h), published as the `karaPitRescues` test extra.
    uint32 karaPitRescues = 0;

    void ClearPoolExit()
    {
        poolExitLeg.clear();
        poolExitCursor = 0;
        poolExitWedges = 0;
        poolExitWatch.Reset();
    }

    // Breath watchdog stamps (DcBreathProvision::WatchdogTick): member guid ->
    // getMSTime() it was first seen under water; erased the tick it surfaces.
    std::unordered_map<uint64, uint32> underwaterSinceMs;

    void BumpEventProgress() { ++eventProgressSeq; }

    void SetTestExtra(std::string const& key, std::string value, bool numeric = false)
    {
        for (DcTestExtra& e : testExtras)
            if (e.key == key)
            {
                e.value = std::move(value);
                e.numeric = numeric;
                return;
            }
        testExtras.push_back(DcTestExtra{key, std::move(value), numeric});
    }

    void SetTestExtra(std::string const& key, char const* value)
    {
        SetTestExtra(key, std::string(value ? value : ""));
    }

    // Numeric extras. Integral values print without a fraction ("12", not
    // "12.000000"); a non-finite value is stored as a string so the record stays
    // valid JSON.
    void SetTestExtraNum(std::string const& key, double value)
    {
        if (!std::isfinite(value))
        {
            SetTestExtra(key, std::string(std::isnan(value) ? "nan" : "inf"));
            return;
        }
        char buf[48];
        if (value == std::floor(value) && std::fabs(value) < 1e15)
            std::snprintf(buf, sizeof(buf), "%.0f", value);
        else
            std::snprintf(buf, sizeof(buf), "%.6g", value);
        SetTestExtra(key, std::string(buf), true);
    }

    // Counter convenience: add `delta` to a numeric extra (absent = 0).
    void AddTestExtra(std::string const& key, double delta = 1.0)
    {
        double current = 0.0;
        for (DcTestExtra const& e : testExtras)
            if (e.key == key && e.numeric)
            {
                current = std::strtod(e.value.c_str(), nullptr);
                break;
            }
        SetTestExtraNum(key, current + delta);
    }

    void ClearTestTelemetry()
    {
        eventProgressSeq = 0;
        testExtras.clear();
    }

    // --- the RUN WING (Explicit-select split maps: Blackrock Spire) ----------
    // Which wing this run clears, latched per instance (see Util/DcRunWing.h for
    // the resolution order). Carried across Reset() like the telemetry block: it
    // is a fact of the INSTANCE, not of one `dc on` session, so `dc off` / a wipe
    // / all-cleared must not forget that the party chose UBRS. Keyed by instance
    // id, so a new instance reads it as unset without anyone clearing it.
    DcRunWing::Latch runWing;

    // --- per-bot throttles (see Util/DcThrottle.h) --------------------------

    DcThrottleSlot throttles[kDcThrottleCount]{};

    // True while `slot` is still inside its window — the caller SUPPRESSES. One
    // call both asks and arms: a false return stamps the slot, so there is no way
    // to ask without recording the answer, which is the slip the
    // `uint32& prev = map[guid]` idiom made easy.
    bool Throttled(DcThrottle slot, uint32 windowMs)
    {
        DcThrottleSlot& s = throttles[static_cast<std::size_t>(slot)];
        if (s.ms && GetMSTimeDiffToNow(s.ms) < windowMs)
            return true;
        StampThrottle(s);
        return false;
    }

    // The movement variant: true while the SAME destination (within `epsilon`)
    // was issued inside the window. A different destination always passes and
    // re-stamps, so a bot being re-aimed is never held back by a floor that only
    // exists to stop it being re-ordered to where it is already going.
    bool ThrottledIssue(DcThrottle slot, float x, float y, float z,
                        float epsilon, uint32 windowMs)
    {
        DcThrottleSlot& s = throttles[static_cast<std::size_t>(slot)];
        if (s.ms && GetMSTimeDiffToNow(s.ms) < windowMs)
        {
            float const dx = s.x - x, dy = s.y - y, dz = s.z - z;
            if (dx * dx + dy * dy + dz * dz <= epsilon * epsilon)
                return true;
        }
        s.x = x;
        s.y = y;
        s.z = z;
        StampThrottle(s);
        return false;
    }

    void ClearThrottle(DcThrottle slot)
    {
        throttles[static_cast<std::size_t>(slot)] = DcThrottleSlot{};
    }

    // getMSTime() legitimately returns 0 once per wrap, and 0 is this slot's
    // "never stamped". Bump it by a millisecond rather than let a throttle open
    // for one tick every 49 days.
    static void StampThrottle(DcThrottleSlot& s)
    {
        s.ms = getMSTime();
        if (!s.ms)
            s.ms = 1;
    }

    // Drop the whole transit block. Called when the crossing ends and from the run
    // teardown: the leg is Repeatable and a leader shoved back into the gauntlet
    // re-arms it, so coming back holding a stale cursor — or a gather gate that
    // latched open two rooms ago — is the one way this state can lie.
    // Drop the Pit of Saron gauntlet block. Called from the run teardown for the
    // ClearTransit reason: the event is Repeatable and a party that re-enters the
    // instance walks the same corridor, so coming back holding a phase clock from
    // the previous run is the one way this state can lie.
    void ClearPosGauntlet()
    {
        posGauntletPhase = 0;
        posGauntletPhaseMs = 0;
        posGateHoldMs = 0;
        posWaveHoldMs = 0;
        posGateStallReported = false;
        posWaveArmedLatched = false;
        posGauntletState = 0;
        ClearThrottle(DcThrottle::PosGauntletLog);
    }

    // Drop the Halls of Reflection block. Called from the run teardown for the
    // ClearPosGauntlet reason and one this map has on its own: both events are
    // Repeatable, and the wave event in particular re-arms after every leash wipe
    // — so a run that comes back holding a spent restart latch would replay the
    // whole first half without ever naming the wipe that caused it.
    void ClearHor()
    {
        horWaveState = 0;
        horWaveStateMs = 0;
        horRestartLogged = false;
        horEscapeState = 0;
        horEscapeStateMs = 0;
        horEscapeStop = 0;
        horEscapeStopHoldMs = 0;
        horEscapeStallReported = false;
        horEscapeTargetStopMs = 0;
        horEscapeIdleMs = 0;
        horEscapeReachedStand = false;
        horFollowAdvancing = false;
        ClearThrottle(DcThrottle::HorWaveLog);
        ClearThrottle(DcThrottle::HorEscapeLog);
        ClearThrottle(DcThrottle::HorIntroLog);
        ClearThrottle(DcThrottle::HorThroneLog);
        ClearThrottle(DcThrottle::HorEscapeGoLog);
        ClearThrottle(DcThrottle::HorStallWarn);
    }

    // Drop the Culling of Stratholme block. Called from the run teardown for the
    // reason every other driver block is: the wave event is Repeatable, so a run
    // that came back holding a spent standoff clock would open its pull budget on
    // a silence that ended ten minutes ago.
    void ClearCos()
    {
        cosWaveState = 0;
        cosWaveStateMs = 0;
        cosWaveStandoffMs = 0;
        cosWaveRestMs = 0;
        cosWaveRestSpentMs = 0;
        ClearThrottle(DcThrottle::CosWaveLog);
    }

    // Drop the Trial of the Champion block once the Knight is dead, for the
    // Culling's reason: the driver is Repeatable.
    void ClearToc()
    {
        tocState = 0;
        tocStateMs = 0;
        tocMountedSinceMs = 0;
        tocUnmountedSinceMs = 0;
        tocStrandSinceMs = 0;
        tocLastProgress = 0xFFFFFFFFu;
        ClearThrottle(DcThrottle::TocTelemetryLog);
        ClearThrottle(DcThrottle::TocMoveIssue);
        ClearThrottle(DcThrottle::TocWarn);
        ClearThrottle(DcThrottle::TocClick);
    }

    // Drop this member's drake leg: a new leg, a dismount, or a mount on a
    // different drake all start from where the drake is, never from a stale cursor.
    void ClearOcLeg()
    {
        ocLegSeq = 0;
        ocLegDest = 0xFF;
        ocLegPhase = 0;
        ocLegPadCentre = false;
        ocLegCount = 0;
        ocLegCursor = 0;
        ocLegArrivedMs = 0;
        ocProgressMs = 0;
        ocStallReissued = false;
        ClearThrottle(DcThrottle::OcMoveIssue);
    }

    void ClearTransit()
    {
        transitDrivingMs = 0;
        transitCursorIndex = 0;
        transitCursorX = 0.0f;
        transitCursorY = 0.0f;
        transitCursorZ = 0.0f;
        transitArmedMs = 0;
        transitGathered = false;
        transitHoldSinceMs = 0;
        transitHoldReason = 0;
        transitHoldTimedOut = false;
        ClearThrottle(DcThrottle::TransitIssue);
        ClearThrottle(DcThrottle::TransitLog);
    }

    // Drop the whole Razorgore block. Called when phase 1 ends and on the run
    // teardown below: the encounter soft-resets itself after a phase-1 wipe
    // (the boss respawns in 30s and the instance clears the field), so coming
    // back holding a stale runner or skip list is the one way this state can lie.
    void ClearRazorgore()
    {
        razorDrivingMs = 0;
        ClearThrottle(DcThrottle::RazorgoreOrbIssue);
        ClearThrottle(DcThrottle::RazorgoreLog);
        razorRunnerGuid.Clear();
        razorRunnerPickedMs = 0;
        razorEggGuid.Clear();
        razorEggElectedMs = 0;
        razorMoveIssuedMs = 0;
        razorEggBestDist = 0.0f;
        razorEggAttempts = 0;
        razorEggSkipped.clear();
    }

    // Drop the whole chess block. The hook calls it when it is asked to play a
    // game that already finished — an instance reload forgets the (unsaved) win,
    // so the objective comes round again and must start from Idle, not from Done.
    void ClearChess()
    {
        chess = DcChessRunState{};
        ClearThrottle(DcThrottle::ChessMoveIssue);
        ClearThrottle(DcThrottle::ChessLog);
        ClearThrottle(DcThrottle::ChessGossip);
    }

    // Full run teardown: every session + signal field. Used on dc on / dc off /
    // death / all-cleared. (The pull preference/bool are NOT here — see the header
    // note; they are reset explicitly by ApplyPullSetting / DisableDungeonClear.)
    // The test-harness telemetry block and the run wing are the exceptions,
    // carried across — see their comments for why; ClearTestTelemetry() is the
    // telemetry's own reset, and the wing is instance-keyed.
    void Reset()
    {
        uint32 const seq = eventProgressSeq;
        std::vector<DcTestExtra> extras = std::move(testExtras);
        DcRunWing::Latch wing = std::move(runWing);
        *this = DcRunState{};
        eventProgressSeq = seq;
        testExtras = std::move(extras);
        runWing = std::move(wing);
    }

    // Pause-cluster teardown — the resume path (manual `dc pause` resume AND the
    // door auto-resume) and re-arm on `dc on`. Clears the paused flag together with
    // the two fields that only mean anything while paused, so a stale reason/door
    // can never leak into the next pause. Boss progress is deliberately untouched —
    // that is the whole point of resume vs. a fresh `dc on`.
    void OnResume()
    {
        paused = false;
        pauseReason.clear();
        pausedDoor.Clear();
    }
};

#endif  // _PLAYERBOT_DCRUNSTATE_H
