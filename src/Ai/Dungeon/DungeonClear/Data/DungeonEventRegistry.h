/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONEVENTREGISTRY_H
#define _PLAYERBOT_DUNGEONEVENTREGISTRY_H

#include <functional>
#include <string>
#include <vector>

#include "Common.h"
#include "G3D/Vector3.h"
#include "SharedDefines.h"  // TeamId

#include "Ai/Dungeon/DungeonClear/Data/DcDifficultyGate.h"

class Player;
class AiObjectContext;

// A Conditional event's activation predicate: a pure-ish read-only test over the
// live world, evaluated each tick. When it returns true (and the event is not yet
// latched) the conditional-event trigger drives the event's steps. The predicate
// lives next to the event that uses it (one file per dungeon under Data/Events/,
// cross-dungeon shared predicates in SharedConditions.cpp) and is handed to the
// builder directly by pointer — there is no global id space to keep collision-
// free (a typo is now a compile error, not a silently-never-firing event).
using EventCondition = std::function<bool(Player*, AiObjectContext*)>;

// Declarative framework for scripted dungeon EVENTS the party must perform to
// progress — pull a lever, click an altar, talk to an NPC and pick a gossip
// option, walk to a spot to trigger a spawn, free a prisoner, etc. An event is
// PURE DATA: an ordered list of typed steps the DungeonEventExecutor drives one
// at a time. This replaces hand-written C++ in ObjectiveHookRegistry with a
// reusable step vocabulary (the old freeform hook survives as the `Custom` step).
//
// Authoring mirrors the other DungeonClear registries: a hardcoded table in a
// GLOB'd .cpp (link-safe in the static-lib build). See DungeonEventRegistry.cpp.
//
// Milestone 1 ships the anchored model + the low-risk steps. An anchored event
// is referenced by a travel-objective anchor (DungeonBossInfo::eventId, kind
// Objective): when the tank reaches the anchor the executor runs the event's
// steps to completion before the clear advances. Gossip and the conditional
// activation model land in later milestones (the enum carries their slots now).

enum class EventStepKind : uint8
{
    MoveTo,                  // walk the leader to (x,y,z) within `radius` — a SHORT
                             // intra-room hop only (plain MovePoint). A far haul
                             // must be done by a BOSS/OBJECTIVE anchor so the boss
                             // navigation + dynamic-pull machinery drives it (an
                             // event step driving a long move fights the pull camp).
    Jump,                    // ballistic MoveJump to (x,y,z) within `radius` — bridge
                             // a navmesh gap a ground move can't (a drop-down ledge
                             // onto a disconnected mesh island). Same SHORT-hop rule
                             // as MoveTo: the anchor navigation gets the bot to the
                             // lip; this clears the one off-mesh leg. Idempotent —
                             // Done once landed (within radius), so a Drive restart
                             // after a combat tick-gap re-runs it safely.
    UseGameObject,           // approach + GameObject::Use(bot) the nearest goEntry
    Gossip,                  // talk creatureEntry + select gossipOption (milestone 2)
    WaitForSpawn,            // hold until creatureEntry is alive (wantAlive) / gone
    WaitForGameObjectState,  // hold until goEntry reaches GOState `wantState`
    KillCreature,            // gate: done when no alive creatureEntry remain in range
    ClearRadius,             // engage + gate: clear every reachable hostile within
                             // `radius` (2D) and a floor `zBand` of (x,y,z); done
                             // when none remain. POSITION-based (any entry), unlike
                             // KillCreature — a point-anchored room pre-clear.
    CastSpell,               // leader casts spellId on self (milestone 2+)
    UseItem,                 // leader uses itemId (milestone 2+)
    Wait,                    // dwell `durationMs` then continue
    Custom,                  // escape hatch -> ObjectiveHookRegistry hookId
    EscortCreature,          // PROTECT a moving NPC (Wailing Caverns' Disciple of
                             // Naralex) the whole way to its destination: start its
                             // scripted escort via gossip, then each tick FOLLOW it
                             // and actively ENGAGE whatever attacks it (mod-playerbots
                             // gives a bot no threat event when only a non-party
                             // escortee is hit, so aggro propagation alone never puts
                             // the tank into the fight). Done only when the final
                             // boss (`escortDoneEntry` / `escortDoneBit`) exists —
                             // never on "reached the end". DRIVEN by the action
                             // (DcObjectiveArriveAction::DriveEscortCreature, which
                             // owns the follow + threat-engage + self-heal gossip +
                             // watchdog); RunStep here is the pure completion gate.
                             // Anchored+Persistent only (the escort spans several
                             // combat gaps that would rewind a non-persistent event).
    DropInHole,              // DROP the party down a narrow vertical hole into the
                             // water below (Wailing Caverns' return-fall off Verdan's
                             // shelf to reach the Disciple). A ground move clamps to
                             // the navmesh edge and a ballistic Jump (Jump kind) clips
                             // the shaft wall — so this glides the leader a few yards
                             // OUT over the open hole-mouth (raw spline, off-mesh),
                             // then MoveFall()s it PURE-VERTICAL into the water (no
                             // horizontal travel => no wall clip; clears the mid-shaft
                             // ledge a lip-MoveFall would catch). Followers can't
                             // reproduce the off-mesh nudge, so they teleport to the
                             // landing (the sanctioned one-way-drop pattern). `x,y,z`
                             // is the over-hole nudge target; `landX,landY,landZ` the
                             // deep-floor landing.
                             // DRIVEN by the action (DriveDropInHole owns the leader's
                             // glide+fall so the at-objective Hold can't cancel the
                             // off-mesh spline); RunStep here pulls the followers and
                             // gates Done once the leader is on the deep floor.
                             // Anchored+Persistent (the drop is one-way — like the
                             // Serpentis Jump, the bot can't path back up).
    TeleportParty,           // TELEPORT the whole party (leader + every bot) across a
                             // navmesh break to a fixed landing, once the leader has
                             // reached the checkpoint the anchor navigation walked it
                             // to. For a one-way DIAGONAL drop where the checkpoint and
                             // landing sit on disconnected mesh islands and the
                             // horizontal offset rules out both a pure-vertical
                             // DropInHole (it would fall in the wrong column) and a
                             // ballistic Jump (big diagonal drops clip/overshoot — the
                             // source of past drop-down grief). User-sanctioned blunt
                             // instrument: nobody glides or falls, everyone is simply
                             // relocated. `x,y,z` is the checkpoint the leader must
                             // reach; `landX,landY,landZ` the landing. Fully synchronous
                             // — one RunStep teleports the leader, pulls every follower
                             // across (PullStrandedFollowersAcross), and returns Done, so
                             // no action driver is needed (unlike DropInHole). Idempotent:
                             // Done immediately if the leader is already on the landing,
                             // so a tick-gap restart never re-teleports. Anchored only
                             // (one-way — the bot can't path back up).
    UseItemOnGO,             // USE a quest item ON a positioned GameObject: grant the
                             // item (itemId) if the bags lack it, then USE it (the full
                             // CastItemUseSpell path) AT the GO of goEntry nearest
                             // (x,y,z), so the GO's SmartAI SMART_EVENT_SPELLHIT
                             // fires. This is the "plant a bomb on a barrel" mechanic
                             // (Old Hillsbrad's Durnholde barrels — item 25853, spell
                             // 32744, GO 182589). The use-spell is OPEN_LOCK against
                             // the GO's ITEM lock, so it only hits when cast FROM the
                             // key item — the Deadmines-cannon gotcha; a bare or
                             // triggered CastSpell never registers. Distinct from
                             // UseGameObject (Use()s the GO — never delivers a
                             // SPELLHIT) and UseItem (self-cast — no GO target). The
                             // (x,y,z) anchor picks a SPECIFIC GO, so five same-entry
                             // barrels are hit as five DISTINCT GOs. Done once the GO
                             // leaves GO_READY (the landed plant Uses the goober — a
                             // stable per-GO success latch, idempotent across restarts).
    UseItemAt,               // USE a quest item AT A PLACE: walk to (x,y,z), then USE
                             // `itemId` (granting it first if the bags lack it) via the
                             // full item-use cast path, and latch Done once the GO
                             // `goEntry` — the mechanic's own RECEIPT object — exists
                             // within DC_EVENT_USEITEM_LATCH of the anchor.
                             //
                             // The Culling of Stratholme's five plagued grain crates
                             // (item 37888 Arcane Disruptor, spell 49590 Arcane
                             // Disruption, receipt GO 190095 Plagued Grain Crate) are
                             // what this exists for, and each of its three differences
                             // from UseItemOnGO is load-bearing there:
                             //
                             //   * THE SPELL HITS A CREATURE, NOT THE GO. 49590's
                             //     effect-0 implicit target is TARGET_UNIT_NEARBY_ENTRY
                             //     (38) with an 8yd range, narrowed by a
                             //     CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET row to
                             //     an ALIVE creature 27827 (Grain Crate Helper) — the
                             //     invisible NOT_SELECTABLE trigger standing on each
                             //     crate, whose SpellHit does the counting. So there is
                             //     no GO to target and no lock to open: the cast is a
                             //     plain self-targeted item use and the engine finds the
                             //     helper. UseItemOnGO's SetGOTarget would be wrong.
                             //   * THE RECEIPT IS A DIFFERENT OBJECT FROM THE THING
                             //     USED. The helper's SpellHit deletes the Suspicious
                             //     Grain Crate (190094) and summons a Plagued Grain
                             //     Crate (190095) in its place for a DAY, so "190095
                             //     stands here" is a stable per-crate "this one is
                             //     done" — idempotent across an event restart, a rewind
                             //     and a re-entered instance. UseItemOnGO's lootState
                             //     latch has nothing to read, because the GO it would
                             //     have latched on no longer exists.
                             //   * THE ITEM HAS A COOLDOWN. 37888 carries a 10s
                             //     spellcooldown, so the step has to WAIT it out
                             //     between crates rather than spam-cast through it.
                             //     Walking the 48-81yd road legs usually covers it,
                             //     but the step never assumes so.
                             //
                             // `spellId` is the item's use-spell, kept (as in
                             // UseItemOnGO) for the cooldown probe and the log line.
    RawPath,                 // Drive an AUTHORED POLYLINE verbatim — a raw
                             // MoveSplinePath through `path`, no per-point navmesh
                             // pathing, no UpdateAllowedPositionZ — from wherever the
                             // bot stands to the last point. The off-mesh primitive
                             // for a leg the navmesh does not carry: a 1yd-wide rock
                             // rim the mesh never baked (Blackfathom Deeps' entrance
                             // stones), or a submerged passage between two surface
                             // sheets (.Swim(): the same step through water, where the
                             // breath watchdog and the druid aquatic-form veto apply).
                             // The preceding MoveTo/anchor must place the bot ON the
                             // polyline's start; the points are then consumed by 3D
                             // proximity (DcRawPathDrive) and the step is Done within
                             // `radius` of the last point — idempotent across a
                             // restart. DRIVEN by the action (DriveChainStep owns the
                             // tick so the at-objective Hold cannot cancel the raw
                             // spline, exactly as DropInHole); RunStep is the pure
                             // completion gate. A watchdog re-issues the spline from
                             // the cursor after 6s without 3D progress, and a second
                             // wedge stalls the run (swim) or hands the bot to the
                             // event's fall handler (stones). Anchored+Persistent
                             // only: the leg is one-way and a rewind would re-run
                             // the MoveTo toward a start the bot can no longer reach.
    EnsureWaterBreathing,    // GATE: every BOT in the party carries a water-breathing
                             // aura (SPELL_AURA_WATER_BREATHING — what the core's
                             // HandleDrowning checks). The leader provisions it per
                             // member from the first source that applies — a shaman's
                             // Water Breathing (131), a warlock's Unending Breath
                             // (5697), else an Elixir of Water Breathing (item 5996,
                             // granted to the member's bags and used by it) — and the
                             // step is Done once everyone is covered, or after the
                             // DC_BREATH_PROVISION_TIMEOUT_MS budget with the uncovered
                             // members logged. Druids are shifted OUT of Aquatic Form
                             // (a bear tank or resto healer cannot do its job in it)
                             // and buffed like everyone else. Humans are never buffed
                             // or shapeshifted; pets do not drown. See DcBreathSource.
};

// Pure classification of a step kind, shared by the chain driver, the follower
// release rule and the registry lint.
//   Movement: the bot physically travels (MoveTo / RawPath / Jump) — the steps a
//             PartyWide event has every bot execute from its own position.
//   Off-mesh: the leg leaves the navmesh (RawPath / Jump) — one-way by nature.
inline bool IsMovementStepKind(EventStepKind k)
{
    return k == EventStepKind::MoveTo || k == EventStepKind::RawPath || k == EventStepKind::Jump;
}

inline bool IsOffMeshStepKind(EventStepKind k)
{
    return k == EventStepKind::RawPath || k == EventStepKind::Jump;
}

// A FALL VOLUME for a PartyWide off-mesh chain (DungeonEvent::fall): the water
// (or pit) a bot ends up in when a hop misses. A bot found inside it — within
// the x/y box and BELOW `maxZ` — is treated as a FAILURE OF THE CHAIN: logged at
// INFO with the step it fell from, teleported to the RETRY point and rewound to
// step 0, so the run re-attempts the crossing the way a player would and the
// log shows the fall. After `maxFalls` by one bot it is teleported to the FINAL
// point and its chain marked done, so a bad coordinate never wedges a run. The
// run's fall count is published to the test record (`chainFalls`) and DcDiag.
struct DcFallVolume
{
    bool  enabled{false};
    float minX{0.0f}, maxX{0.0f};
    float minY{0.0f}, maxY{0.0f};
    float maxZ{0.0f};              // in the volume when z < maxZ (the water line)
    float retryX{0.0f}, retryY{0.0f}, retryZ{0.0f};  // back to the start of the chain
    float finalX{0.0f}, finalY{0.0f}, finalZ{0.0f};  // the far landing, after maxFalls
    uint32 maxFalls{3};

    // Pure: is (x,y,z) inside the volume? False when the volume is disabled.
    bool Contains(float x, float y, float z) const
    {
        return enabled && x >= minX && x <= maxX && y >= minY && y <= maxY && z < maxZ;
    }
};

// One typed primitive. Fields are a shared bag — only those relevant to `kind`
// are read (see DungeonEventExecutor::RunStep). Authoring goes through the
// fluent EventBuilder below, not by populating this directly.
struct EventStep
{
    EventStepKind kind{EventStepKind::Wait};

    // Geometry: MoveTo target, and the search anchor for GO/creature steps when
    // (x,y,z) is set; (0,0,0) means "search around the bot".
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float radius{0.0f};      // arrival / search radius; 0 => a per-kind default
    float zBand{0.0f};       // ClearRadius: vertical half-band around z (keeps a
                             // multi-level chamber's balconies/pit out of a clear)

    uint32 goEntry{0};       // UseGameObject / WaitForGameObjectState
    uint32 creatureEntry{0}; // Gossip target / WaitForSpawn / KillCreature
    uint32 spellId{0};       // CastSpell
    uint32 itemId{0};        // UseItem; UseGameObject: carried item (CarryItem)

    int32  gossipOption{-1}; // Gossip: option index to select (-1 = none)
    uint32 count{1};         // KillCreature: alive-count that still blocks
    uint32 wantState{0};     // WaitForGameObjectState: target GOState value
    bool   wantAlive{true};  // WaitForSpawn: wait for alive (true) vs gone (false)

    // KillCreature only. When set, the DRIVING action (DcObjectiveArriveAction /
    // DcRunEventAction) actively walks the leader into the named creature via the
    // engage pipeline (EngageDirect — long-range path + combat) instead of merely
    // GATING on its death while the bot holds. Use it for a kill the bot must seek
    // out (a boss across the room / down the stairs), where waiting for the mob to
    // come to a held bot would deadlock. The step is still Done once no live
    // creature of the entry remains. (Distinct from the room-aggro entry-0 mode,
    // which targets the room-trash value rather than a fixed entry.)
    bool   engage{false};

    // KillCreature(engage) only. Bar this step from the COMBAT-side stealth-breaker
    // (DungeonEventExecutor::ActiveEngageStep's `anyStep` fallback, which arms
    // DungeonClearObjectiveEngageCombatTrigger from the event's FIRST engage step
    // even while an earlier step is still active).
    //
    // That fallback exists for a stealthed sapper that flags the party into combat
    // before the tank has reached the anchor: the signature it keys on is "a live
    // creature of the entry is nearby and reachable but this bot cannot see or
    // detect it". A boss that is DESIGNED to be undetectable until a gate opens
    // matches that signature perfectly and permanently — so the rung, which sits
    // above the stock combat movers, hijacks every combat tick and walks the tank
    // at the boss, abandoning whatever the party is actually fighting.
    //
    // Pathaleon the Calculator is exactly that: greater-invisible with
    // CanAIAttack()==false until four bridge deaths. Set this on any engage step
    // whose target is invisible/untargetable by script until the steps before it
    // have run. The step still drives the engage normally once it IS the active
    // step (DcObjectiveArriveAction), which is the only time engaging is correct.
    bool   engageOnlyWhenActive{false};

    // Gossip only. When set, a Gossip step whose target creature is not alive
    // anywhere nearby is SKIPPED (Done) instead of waited on — for an OPTIONAL
    // interaction the run must not deadlock on if the NPC died (a freed ZulFarrak
    // helper the party let die: a dead Weegli leaves the boss unreachable but we
    // still talk to Bly, and if every helper is dead the event simply completes
    // and the clear continues). A wide rescan separates "dead/gone" (skip) from
    // "still walking in / just outside the gossip search radius" (keep approaching).
    bool   skipIfMissing{false};

    // UseGameObject only. Deliver the click as a REPORT-USE
    // (CMSG_GAMEOBJ_REPORT_USE -> GameObjectAI::GossipHello(player, /*reportUse*/
    // true)) instead of GameObject::Use().
    //
    // The two are not interchangeable, and for a scripted lever the difference is
    // the whole mechanic. GameObject::Use() calls AI()->GossipHello(player,
    // FALSE); only the report-use opcode passes true. A GameObjectAI that keys its
    // work off `reportUse` therefore does NOTHING on a plain Use() — and a real
    // client always sends both opcodes, so scripts are written assuming it.
    //
    // Blackwing Lair's Chromaggus lever (GO 179148, go_chromaggus_lever) is the
    // case that forced this, and it is worse than a no-op there: the door-opening
    // half of its GossipHello is inside `if (reportUse)`, while the "this lever is
    // spent" half (GO_FLAG_NOT_SELECTABLE | GO_FLAG_IN_USE, GO_STATE_ACTIVE) is
    // OUTSIDE it. A plain Use() would burn the lever without opening the cage —
    // Chromaggus stays immune behind a shut portcullis with nothing left to click,
    // i.e. an unrecoverable run.
    //
    // So this is not "also send report-use": it REPLACES the Use() call. Set it
    // only for a GO whose script reads the flag; everything else stays on Use(),
    // which is the path that fires loot, traps, summons and spell targets.
    bool   reportUse{false};

    // Gossip only. When set, the step WAITS (Running) while the target creature is
    // still moving, so the bot never talks to an NPC mid-walk. Used when a scripted
    // NPC walks to a final position before its gossip is meaningful (ZulFarrak's
    // freed crew descending to the temple floor — their gossip is already offered
    // while they are still walking down). Combined with skipIfMissing, a dead NPC
    // still skips rather than waiting forever.
    bool   waitForStill{false};

    uint32 durationMs{0};    // Wait: dwell length
    uint32 timeoutMs{0};     // 0 => EventStepTimeout config default; else per-step
    uint32 hookId{0};        // Custom -> ObjectiveHookRegistry; on a garrison MoveTo,
                             // the hook to re-run each tick WHILE it holds (see
                             // EventBuilder::WhileHolding). 0 => none.

    // --- EscortCreature only ----------------------------------------------
    // The escortee (creatureEntry) is the NPC to protect; `radius` is the grid
    // scan radius used to (re)resolve it live each tick. `gossipOption` is the
    // menu option that STARTS its scripted escort (re-run for self-heal when it
    // resets to idle). The remaining knobs:
    float  escortStandoff{0.0f};    // follow slot distance to hold BEHIND the
                                    // escortee (offset so the tank never blocks
                                    // its movement spline). 0 => per-kind default.
    float  escortThreatRadius{0.0f};// TIGHT scan radius around the escortee for an
                                    // attacker to engage (the ambushes spawn ON it,
                                    // so a tight radius catches them without
                                    // cross-pulling the cavern's ambient fauna).
                                    // `zBand` reuses the shared field for vertical
                                    // tolerance (the chamber drops ~z -96 -> -102).
    uint32 escortDoneEntry{0};      // completion creature (Mutanus 3654): the step
                                    // is Done once it exists (grid scan) — never on
                                    // "reached the destination".
    int32  escortDoneBit{-1};       // completion encounter bit, as a backstop to the
                                    // grid scan (set once the boss is killed even
                                    // after its corpse despawns). -1 => bit unused.

    // --- DropInHole only --------------------------------------------------
    // The deep-floor LANDING the leader falls onto and the followers teleport to.
    // (`x,y,z` is the over-hole nudge target the leader glides to before MoveFall.)
    float  landX{0.0f};
    float  landY{0.0f};
    float  landZ{0.0f};

    // --- RawPath only -----------------------------------------------------
    // The authored polyline, driven verbatim (see EventStepKind::RawPath). The
    // single-point x/y/z above mirror the LAST point so every x/y/z consumer
    // (lint distance checks, the panel, the diag) sees the step's destination.
    std::vector<G3D::Vector3> path;
    // The leg is SUBMERGED (.Swim()): the breath watchdog treats a bot on it as
    // "driving a swim leg", the druid aquatic-form veto applies, and the stuck
    // thresholds are the 3D swim ones.
    bool   swim{false};

    // --- Jump only --------------------------------------------------------
    // Vertical launch speed for MoveJump; 0 => the bot's run speed (the arc apex
    // at run speed is ~1.3yd). Lower it per hop where a capture shows a low roof
    // over the landing (the Blackfathom stones have 2.9-4.2yd of headroom).
    float  jumpSpeedZ{0.0f};

    // --- UseGameObject only -----------------------------------------------
    // SKIP the click (report Done without Use()) when the map's InstanceScript
    // GetData(skipIfDataId) == skipIfDataValue. For a GO whose script has no
    // once-only guard of its own and whose re-click is destructive: Blackfathom
    // Deeps' Fires of Aku'mai summon a SECOND wave on a second click, and the
    // fire's GO state (a button that auto-resets) cannot be trusted as the
    // "already lit" latch — the script's TYPE_FIREn == DONE can. -1 => unused.
    int32  skipIfDataId{-1};
    uint32 skipIfDataValue{0};

    // --- stamped by EventBuilder::Build(), never authored ---------------------
    // The event is PartyWide (DungeonEvent::partyWide), copied onto every step
    // so RunStep — which sees one step, not the event — can tell a chain hop
    // from a leader-only Jump; and whether this is the event's LAST movement
    // step, where the leader waits for the followers' chains (then pulls the
    // stragglers) before reporting Done.
    bool   partyWide{false};
    bool   lastMovementStep{false};

    // MoveTo garrison gate, instance-data variant. When instanceDataId >= 0 the
    // step holds at (x,y,z) until the map's InstanceScript GetData(instanceDataId)
    // reaches instanceDataMin. Preferred over the creatureEntry "until alive" gate
    // for a value that the party kills DURING continuous combat (the event engine
    // is dormant in combat, so a transient "boss alive" window can be missed and
    // never recovers once the boss dies): a scripted phase counter only ever
    // climbs, so a >= gate is safe to observe late. -1 => no instance-data gate.
    int32  instanceDataId{-1};
    uint32 instanceDataMin{0};

    // MoveTo garrison gate, instance PERSISTENT-data variant. Same contract as
    // instanceDataId (monotonic counter, safe to observe late), but read from
    // InstanceScript::GetPersistentData instead of GetData.
    //
    // The two stores are unrelated: GetData is a virtual an instance script must
    // OVERRIDE, while the persistent-data vector is a plain SetPersistentDataCount
    // /StorePersistentData slot that every InstanceScript exposes for free. Several
    // scripts keep their wave counters only in the persistent store and never
    // override GetData at all (instance_mechanar is one: DATA_BRIDGE_MOB_DEATH_COUNT
    // lives in the persistent vector, and GetData returns the base-class 0 forever),
    // so an instanceDataId gate on such a map would hold until it times out.
    // -1 => no persistent-data gate.
    int32  persistentDataId{-1};
    uint32 persistentDataMin{0};

    // MoveTo garrison gate, instance BOSS-STATE variant. When bossStateId >= 0 the
    // step holds until InstanceScript::GetBossState(bossStateId) is one of the
    // states in bossStateClearMask (a DcBossStateBit mask). Unlike the two data
    // gates above, a boss state is NOT monotonic — an evade puts it back to
    // NOT_STARTED, a wipe to FAIL — so the gate takes a SET of states, and
    // restartOnBossStateMask names the states that mean "the encounter reset
    // under us": on one of those the event REWINDS to step 0 and runs again.
    //
    // For an encounter the event itself starts and whose retry is the event's
    // job. Karazhan's Opera (a wipe FAILs it and respawns Barnes for another
    // gossip) and Nightbane (an evade flies him home and re-arms the urn) are
    // why it exists. Their GetData overrides do not expose the boss state
    // (Opera's returns the play picked), so the data gates cannot express it.
    //
    // The slot is authored from the instance header's DATA_* enum, which is NOT
    // the DungeonEncounter bit index (Karazhan's Opera is slot 4, bit 3) — the
    // same rule as DungeonBossInfo::doneBossStateIndex. -1 => no boss-state gate.
    int32  bossStateId{-1};
    uint32 bossStateClearMask{0};
    uint32 restartOnBossStateMask{0};

    // ClearRadius only. When non-empty, the volume clears ONLY creatures whose
    // entry is in this allow-list — both the RunStep completion gate and the
    // EngageDirect the driving action issues. Empty (the default) keeps the
    // plain position-based behaviour: everything hostile in the volume.
    //
    // For a set-piece fought inside ambient wildlife this is the difference
    // between the encounter and a safari. Black Morass is the case that forced
    // it: the wave volumes sit in open swamp (the arena catch-all is r=160),
    // and unfiltered they sent the party chasing crocolisks and tarantulas
    // across the map while the rift's infinites walked unopposed into Medivh.
    // Ambient fauna is still fought when it aggros the party — that is the
    // normal combat engine's job, not the clear's.
    std::vector<uint32> entryFilter;

    // WaitForSpawn / garrison MoveTo (MoveToHoldUntilSpawn) only: ALTERNATIVE
    // gate entries, OR'd with creatureEntry. wantAlive: the gate clears once ANY
    // of them is alive. !wantAlive: once NONE of them is. Empty (the default)
    // gates on creatureEntry alone.
    //
    // For a spawn whose entry is rolled by the script. Karazhan's Opera plays one
    // of three shows per instance (OperaEvent = urand(1,3)), and the only way to
    // tell which is running is which cast member the curtain summons: Dorothee,
    // Grandmother or Julianne. See EventStepGateEntries.
    std::vector<uint32> orEntries;
};

// One bit per EncounterState value (InstanceScript.h: NOT_STARTED 0,
// IN_PROGRESS 1, FAIL 2, DONE 3, SPECIAL 4), for the boss-state gate's masks.
inline constexpr uint32 DcBossStateBit(uint32 state) { return 1u << state; }

// What a boss-state garrison gate does this tick (pure). Clear wins over Restart
// if a state is in both masks.
enum class BossStateGateVerdict : uint8
{
    Hold,     // keep holding
    Clear,    // the gate is satisfied — the step is Done
    Restart,  // the encounter reset — rewind the event to step 0
};

inline BossStateGateVerdict DecideBossStateGate(uint32 state, uint32 clearMask,
                                                uint32 restartMask)
{
    if (state < 32 && (clearMask & DcBossStateBit(state)))
        return BossStateGateVerdict::Clear;
    if (state < 32 && (restartMask & DcBossStateBit(state)))
        return BossStateGateVerdict::Restart;
    return BossStateGateVerdict::Hold;
}

// Every creature entry an EventStep's spawn gate accepts: creatureEntry followed
// by orEntries, zeros dropped. Pure, so the any-of contract is unit-testable.
inline std::vector<uint32> EventStepGateEntries(EventStep const& step)
{
    std::vector<uint32> out;
    if (step.creatureEntry)
        out.push_back(step.creatureEntry);
    for (uint32 e : step.orEntries)
        if (e)
            out.push_back(e);
    return out;
}

// How an event enters the clear. Milestone 1 only uses Anchored; Conditional is
// reserved for off-path levers / pre-boss gates (milestone 2).
enum class EventActivation : uint8
{
    Anchored,     // referenced by a boss-list objective anchor (orderIndex)
    Conditional,  // fired by a predicate (condition) each tick
};

struct DungeonEvent
{
    uint32 mapId{0};
    uint32 id{0};            // stable per-map id (objective eventId / latch key)
    std::string name;

    EventActivation activation{EventActivation::Anchored};
    uint32 orderIndex{0};    // Anchored: the objective's encounter slot (doc only)
    EventCondition condition{};  // Conditional: the per-tick activation predicate

    // Difficulty gate. A HeroicOnly event never fires (and never surfaces in the
    // panel) on a normal run, and vice versa. For an ANCHORED event the primary
    // gate is the roster anchor that references it (gate the BossRosterPatch);
    // this field additionally hard-stops Drive as a belt-and-suspenders, and is
    // what the conditional-event paths filter on directly.
    DcDifficultyGate gate{DcDifficultyGate::Any};

    std::vector<EventStep> steps;

    // Required => a Blocked/timed-out step stalls the run for the human;
    // Optional => such a step is skipped and the clear advances anyway (used for
    // best-effort events whose scripted trigger may not fire for bots).
    bool required{true};

    // Conditional events only. A normal conditional event latches DONE the moment
    // its step list completes (DcRunEventAction inserts its ConditionalLatchKey),
    // so it never fires again that run. A Repeatable event is NOT latched on
    // completion — it re-fires every time its condition reads true again. Use it
    // for an action the party must repeat an unknown number of times, where some
    // OTHER live signal (not a one-shot latch) gates each repeat and ends the
    // loop. Example: Razorfen Downs' gong, which must be rung once per wave until
    // the boss spawns — the gong's own selectable flag gates each ring and the
    // boss going live ends the loop, so a fixed latch would stop it after ring 1.
    bool repeatable{false};

    // Conditional events only. A conditional event is normally driven ONLY out of
    // combat (DungeonClearEventDueTrigger stands down on bot->IsInCombat(), so the
    // stock combat engine owns every fight uncontested). That is right for the
    // usual shape — a lever, a gossip, a gate — where the event's work happens
    // between pulls.
    //
    // It is WRONG for a continuous WAVE encounter, where the party is in combat
    // essentially from the first pull to the last and the event IS the encounter.
    // There the non-combat-only driver runs only in the shrinking gaps between
    // waves, and stops running altogether the moment the party falls behind and
    // combat stops dropping — exactly when the encounter most needs steering.
    // Black Morass is the case that forced this flag: with two rifts open the
    // party never left combat, so nothing ever walked it to a portal to kill the
    // rift keeper, so the rifts never closed — a spiral with no floor. (Sunken
    // Temple's Avatar of Hakkar hit the same wall and was fixed the same way, by
    // hand, with a second copy of its handlers in DungeonClearCombatStrategy; this
    // is that fix generalised onto the events framework.)
    //
    // When set, the event is ALSO driven by the combat-engine copy of the
    // conditional-event rung (DungeonClearEventDueCombatTrigger ->
    // DcRunEventCombatAction, DcRel::EventDueCombat), which sits above the stock
    // combat movers so the driver can actually reposition the tank mid-fight.
    // Reserve it for encounters that genuinely need steering under fire: it takes
    // ticks away from the stock combat engine, which is the right owner everywhere
    // else.
    bool drivesInCombat{false};

    // RAID runs only: this event is part of a boss ENCOUNTER and keeps driving
    // while the raid boss stand-down holds (the exemption in the DC combat
    // multiplier and FindDueConditionalEvent — see Util/DcBossStandDown.h).
    // The canonical case is BWL's Razorgore orb: the fight is the playerbots
    // strategy's, but the orb click and the possessed-Razorgore egg run are
    // DC's orchestration inside it. An event WITHOUT this flag is refused
    // outright during stand-down, whatever its other flags say. Usually paired
    // with drivesInCombat (an encounter is a fight); meaningless on 5-man maps
    // where stand-down never arms.
    bool encounterActive{false};

    // This event's STEPS are the sole driver: they issue their own movement, and
    // they decide per tick whether they need the tick at all.
    //
    // Two consequences, both of which a continuous driver event needs:
    //
    //  (a) the driving action does NOT hold position for it (below); and
    //  (b) when a step reports Done — "nothing to steer this tick" — the action
    //      YIELDS the tick instead of claiming it. Engine::DoNextAction runs
    //      exactly ONE action per tick (it breaks out of its loop on the first
    //      Execute returning true), so a driver registered above the stock combat
    //      movers that returns true every tick starves the combat engine outright:
    //      the bot never picks a target, never swings, never casts, never builds
    //      threat. Black Morass wiped parties that way — the tank drove to the
    //      portal, the keeper engaged it, and the tank stood there taking hits
    //      with no rotation while the DPS pulled aggro and died. The step still
    //      RUNS every tick (the trigger is unchanged), so its side effects stay
    //      responsive; it just stops claiming ticks it does not need.
    //
    // DcRunEventAction normally calls DcMovement::ResolveEscortConflict before
    // every Drive, to park the tank while a step does its work — right for a
    // lever, a gossip, a wait. But ResolveEscortConflict CANCELS any escort glide
    // in flight, and it runs BEFORE the step does, so a Custom hook that delivers
    // the tank across the map with its own long-range spline has that spline
    // killed on the very next tick, re-issues it, has it killed again... and the
    // bot creeps roughly one tick's worth of movement per cycle while logging a
    // perfectly healthy "spline issued" each time.
    //
    // Black Morass is where this was found. Its rift camp was moved onto
    // LongRangePathfinder precisely because the portals are 80-102yd out and a
    // bare MovePoint silently truncates — and it STILL never arrived: 151 camp
    // attempts in a 10-run batch, none within 20yd of the rift, 111 logging
    // moving=false. The route was fine; it was being cancelled from under the
    // hook. Set this only for an event whose steps genuinely own the tank's
    // movement, or it will fight whatever else is trying to hold position.
    bool stepsOwnMovement{false};

    // ANCHORED events only. A normal anchored event is driven only while the tank
    // sits at its objective anchor, and DungeonEventExecutor::Drive RESTARTS it
    // from step 0 whenever Drive hasn't run for >1s (treated as a fresh
    // activation). That is right for a quick one-room event, but wrong for a long
    // multi-combat event that spans the dungeon (ZulFarrak's temple): every wave /
    // boss fight is a >1s gap on the combat engine, which would rewind the step
    // list each time combat ends. A Persistent event keeps its progress across
    // those gaps — Drive only (re)initialises it on a real eventId mismatch (new
    // run / different event), never on a tick gap — and its "at objective" trigger
    // stays sticky once started so the tank can roam far from the anchor (down the
    // stairs to the bosses) while the event drives, instead of being held at it.
    bool persistent{false};

    // Event-level completion gate: when completeGoEntry is set, the event is
    // Completed the moment the nearest such GO (within completeGoRadius) reads
    // completeGoState — whatever step it is on. For a chain whose phases the
    // party can fight through out of band (reactively, before the anchor's
    // arrival radius, or while the event engine is dormant in combat): the
    // linear steps then wait on a wave that already came and went, and a
    // required event turns that timeout into a stall. A monotonic "it's over"
    // signal makes every such ordering converge. Stratholme's Slaughterhouse is
    // the case — the Baron door opens once the Black Guards die. 0 => unused.
    uint32 completeGoEntry{0};
    uint32 completeGoState{0};
    float  completeGoRadius{0.0f};

    // This event OWNS THE PULL while it drives: the whole dynamic/advanced pull
    // system stands down (DungeonClearPullModeCurrentValue) and the scout-lag
    // drops with it (DcLeaderSignal::IsLeaderDynamicScouting), exactly as they do
    // for a persistent ANCHORED event. The tank face-pulls whatever it meets and
    // fights it where it stands; the party follows close and the leader-fight
    // assist collapses onto it.
    //
    // The anchored path infers this from `persistent` alone, but it cannot serve a
    // CONDITIONAL event: IsPersistentAnchoredEventActive reads NextDungeonBoss and
    // requires an Objective anchor, and a conditional event drives BETWEEN anchors
    // (FindDueConditionalEvent) with the next boss still sitting in that value. So
    // the conditional half is opted in per row rather than inferred — persistence
    // says "keep my progress across combat gaps", which is not the same claim.
    //
    // BWL's Suppression Rooms is why this exists. The crossing is a TRANSIT, and
    // the advanced pull's Idle branch answers every unplanned aggro by walking a
    // fresh camp BACK along the route until it finds ground clear of hostiles —
    // which, among 160 whelps on a 30s respawn, means all the way out to maxDrag.
    // Live (tr-20260828-142623-4): nine drag legs in four minutes, 16-71yd each,
    // one sibling run at 102yd, with the transit cursor falling 10/19 -> 6/19 and
    // re-walking the same four anchors. The tank ran the gauntlet backwards, over
    // and over. See DcSuppressionTransitDecision.h.
    bool ownsThePull{false};

    // PACK LEASH, yd (0 = off). Between fights, the objective action holds the
    // leader while any living member on the map is further than this from it, so
    // the party crosses as ONE body instead of a column (DcPackLeashDecision.h,
    // which also bounds the hold with a duty-cycle watchdog). For a room that
    // refills behind the party, where a straggler is an add magnet — Blackrock
    // Depths' Lyceum. The hold never walks the leader back and never runs in
    // combat: the followers' own follow closes the gap.
    float packLeash{0.0f};

    // Conditional ownsThePull events only. While a pull-owning event drives, the
    // Advance ladder's off-line route rejoin stands down: an event that stepped
    // its own movement has left the route cursor somewhere the party no longer
    // is, and rejoining it walks the party backwards (Halls of Reflection's
    // escape, see DcAdvanceAction's OffLineRejoin rung). Set this for a driver
    // that CLAIMS every tick in which it moves the party, and yields only once
    // the party stands where the ordinary approach should take over — then a
    // yield means the approach is Advance's to finish, rejoin included.
    //
    // The Oculus ascent is the case. Live tr-20260913-003200-10: the tank stepped
    // off its drake under fire 12.5yd from the south pad's 8yd arrival, fought to
    // 11yd, and the driver said "on site" and yielded while the rejoin stood down
    // for it — 1372 ticks, ten minutes, nobody walked the last three yards.
    bool yieldsTheApproach{false};

    // PARTY-WIDE movement. Every BOT in the group executes the event's movement
    // steps (MoveTo / RawPath / Jump) from its own position, the leader first and
    // the followers released one at a time behind it (DcChainRelease: a follower
    // starts once the bot ahead of it has cleared the first off-mesh step, so
    // two bots never share a stone top), each driving its own copy of the chain
    // (DcChainState, DcPartyWideChain). Non-movement steps stay leader-only.
    // The follow-tank action yields to the chain driver while a follower's chain
    // is active, exactly as it yields to the leader's DropInHole today.
    //
    // For an off-mesh leg the followers can physically cross themselves — a
    // stepping-stone chain, a swim through a submerged passage — where the
    // leader-hops / followers-teleported pattern (Jump, DropInHole) would yank a
    // bot off its stone mid-chain. The teleport survives in ONE place: a follower
    // still short of the last point DC_CHAIN_STRAGGLER_MS after the leader
    // arrived is pulled across to it (PullStrandedFollowersAcross) — the
    // sanctioned one-way fallback, now a straggler net instead of the mechanism.
    // Humans are never driven and never teleported. Anchored + Persistent only.
    bool partyWide{false};

    // PartyWide events only: where a bot that misses a hop ends up, and what to
    // do about it. See DcFallVolume. Disabled unless FallVolume() is authored.
    DcFallVolume fall;

    // Conditional events only, panel cosmetics. By default an off-path
    // conditional event renders last in the `dc bosses` panel (index 99). When
    // this names a boss entry, the event instead sorts just BEFORE that boss —
    // for a prerequisite the party performs before the boss is reachable (e.g.
    // the gong before Tuten'kash). 0 => default (sort last). Does not affect
    // engine ordering, only the status panel.
    uint32 panelGatesBossEntry{0};

    // Conditional events with panelGatesBossEntry only. How close to the gated
    // boss's anchor, or to the boss once it is up, DcTargeting::
    // IsHoldingForSummonEvent stands the dynamic pull down. 0 => the default
    // 80yd, sized for the RFD gong waves. Shrink it where trash the party still
    // has to clear stands inside 80yd of the anchor (Zul'Gurub's gong).
    float summonHoldRadius{0.0f};

    // Conditional events only, panel cosmetics. Like panelGatesBossEntry but
    // sorts the event just AFTER the named boss and keeps it as its OWN numbered
    // row (panelGatesBossEntry instead FOLDS the event into the boss's row as a
    // sub-line). Use for a standalone progression gate that follows a specific
    // boss but isn't owned by the next one — e.g. Shadowfang Keep's courtyard
    // door, which the party opens right after Rethilgore. 0 => not used.
    uint32 panelSortAfterBossEntry{0};

    // Conditional events only, panel cosmetics. When set to a real team the event
    // is shown in the panel ONLY for that team — a faction-specific gate whose
    // mechanic only one side ever performs (Shadowfang Keep frees Ashcrombe for
    // Alliance, Adamant for Horde). The activation predicate already team-gates
    // execution; this hides the irrelevant faction's row from the status panel.
    // TEAM_NEUTRAL => shown to both factions.
    TeamId panelTeam{TEAM_NEUTRAL};
};

// Fluent builder for readable registry rows, mirroring MakeBoss/MakeObjective.
//
//   Event(mapId, id, "Free the prisoner")
//       .MoveTo(x, y, z, 12)
//       .UseGO(GO_LEVER, 10)
//       .WaitForGOState(GO_GATE, GO_STATE_ACTIVE, 8000)
//       .Build();
class EventBuilder
{
public:
    EventBuilder(uint32 mapId, uint32 id, std::string name);

    EventBuilder& Anchored(uint32 orderIndex);
    EventBuilder& Conditional(EventCondition condition);
    EventBuilder& Optional();
    EventBuilder& Repeatable();
    EventBuilder& Persistent();
    // Complete the whole event as soon as `goEntry` reads `wantState` (see
    // DungeonEvent::completeGoEntry).
    EventBuilder& CompleteWhenGOState(uint32 goEntry, uint32 wantState,
                                      float searchRadius = 0.0f);
    // Stand the whole pull system (and the scout-lag) down for as long as this
    // event drives — see DungeonEvent::ownsThePull. Conditional events only; the
    // anchored path already infers it from Persistent().
    EventBuilder& OwnsThePull();
    // Hold the leader between fights until the party is within `radius` of it
    // (see DungeonEvent::packLeash).
    EventBuilder& PackLeash(float radius);
    // A pull-owning driver whose yield hands the approach — off-line rejoin
    // included — back to Advance (see DungeonEvent::yieldsTheApproach).
    EventBuilder& YieldsTheApproach();
    // Keep driving this conditional event while the party is IN COMBAT (see
    // DungeonEvent::drivesInCombat). For continuous wave encounters only.
    EventBuilder& DrivesInCombat();
    // Do not hold position for this event's steps — they issue their own movement
    // (see DungeonEvent::stepsOwnMovement).
    EventBuilder& StepsOwnMovement();
    // RAID maps: keep driving this event while the raid boss stand-down holds,
    // because it IS part of the encounter (see DungeonEvent::encounterActive).
    // Without it the executor refuses the event outright during a fight.
    EventBuilder& EncounterActive();
    // Difficulty gates (see DungeonEvent::gate). Default is both difficulties.
    EventBuilder& HeroicOnly();
    EventBuilder& NormalOnly();
    EventBuilder& PanelBeforeBoss(uint32 bossEntry);
    // Pull stand-down radius around the boss PanelBeforeBoss names. See
    // DungeonEvent::summonHoldRadius.
    EventBuilder& SummonHoldRadius(float yards);
    // Sort this standalone event just AFTER `bossEntry` in the panel, keeping it
    // as its own numbered row (unlike PanelBeforeBoss, which folds the event into
    // the boss's row). See DungeonEvent::panelSortAfterBossEntry.
    EventBuilder& PanelAfterBoss(uint32 bossEntry);
    // Show this event in the panel only for `team` (a faction-specific gate). See
    // DungeonEvent::panelTeam.
    EventBuilder& PanelTeam(TeamId team);

    // Override the LAST-added step's timeout (0 on the step => the
    // EventStepTimeout config default). Chain it right after the step it tunes:
    //   .WaitForSpawn(entry).Timeout(900000)
    EventBuilder& Timeout(uint32 ms);

    // Mark the LAST-added step's target as optional: a Gossip step whose creature
    // is dead/gone is skipped rather than waited on. Chain after the step:
    //   .Gossip(npc, 0).SkipIfTargetMissing()
    EventBuilder& SkipIfTargetMissing();

    // Make the LAST-added Gossip step wait until its target NPC stops moving (has
    // finished a scripted walk) before talking to it. Chain after the step:
    //   .Gossip(npc, 0).WaitTargetStill()
    EventBuilder& WaitTargetStill();

    // Every bot in the group runs the movement steps (see DungeonEvent::partyWide).
    EventBuilder& PartyWide();
    // The fall volume for a PartyWide chain (see DcFallVolume): the box
    // [minX,maxX]x[minY,maxY] below `maxZ` is the water; a bot found in it is
    // sent back to (retryX,Y,Z) and rewound, and after `maxFalls` falls on to
    // (finalX,Y,Z) with its chain marked done.
    EventBuilder& FallVolume(float minX, float maxX, float minY, float maxY, float maxZ,
                             float retryX, float retryY, float retryZ,
                             float finalX, float finalY, float finalZ, uint32 maxFalls = 3);

    EventBuilder& MoveTo(float x, float y, float z, float radius = 0.0f);
    // Ballistic jump to (x,y,z): MotionMaster::MoveJump at run speed. Bridges a
    // navmesh gap a ground move can't (drop-down ledge / off-mesh island). The
    // preceding anchor/MoveTo must place the bot on the jump LIP; this clears the
    // gap. Done once the bot is within `radius` of the landing.
    EventBuilder& Jump(float x, float y, float z, float radius = 4.0f);
    // Override the LAST-added Jump's vertical launch speed (see
    // EventStep::jumpSpeedZ). Chain it right after the hop it tunes:
    //   .Jump(x, y, z, 1.5f).JumpSpeedZ(5.0f)
    EventBuilder& JumpSpeedZ(float speedZ);
    // Drive an authored polyline verbatim (see EventStepKind::RawPath): a raw
    // spline through `pts` from wherever the bot stands, Done within
    // `arriveRadius` (3D) of the last point. Chain .Swim() for a submerged leg.
    EventBuilder& RawPath(std::vector<G3D::Vector3> pts, float arriveRadius = 3.0f);
    // Mark the LAST-added RawPath as a SUBMERGED leg (see EventStep::swim):
    //   .RawPath({S1, S2, S3}, 4.0f).Swim()
    EventBuilder& Swim();
    // Provision a water-breathing aura on every bot in the party before the leg
    // that needs it (see EventStepKind::EnsureWaterBreathing).
    EventBuilder& EnsureWaterBreathing();
    // MoveTo that GARRISONS: walk to (x,y,z) and HOLD there — re-moving back if a
    // later tick finds the bot displaced (combat pushed it off the spot) — until
    // `creatureEntry` matches `wantAlive`. Keeps the tank anchored at a staging
    // point between waves (ZulFarrak's ramp head) instead of holding wherever the
    // last fight ended (down on the wave spawn). Internally a MoveTo step with a
    // spawn gate; creatureEntry 0 would be a plain one-shot MoveTo.
    EventBuilder& MoveToHoldUntilSpawn(float x, float y, float z, float radius,
                                       uint32 creatureEntry, bool wantAlive = true);
    // Garrison variant gated on a monotonic InstanceScript phase counter: hold at
    // (x,y,z) until GetData(dataId) >= minValue. Robust where the party fights
    // through the gate during continuous combat (ZulFarrak's temple — hold the
    // ramp until DATA_PYRAMID reaches WAVE_3, then descend; works even if the
    // bosses are already dead by the time the party drops combat).
    EventBuilder& MoveToHoldUntilInstanceData(float x, float y, float z, float radius,
                                              uint32 dataId, uint32 minValue);
    // Garrison variant gated on the InstanceScript PERSISTENT-data store: hold at
    // (x,y,z) until GetPersistentData(dataId) >= minValue. Use it when the map's
    // counter lives in the persistent vector and the script never overrides GetData
    // — the Mechanar bridge gauntlet's DATA_BRIDGE_MOB_DEATH_COUNT is the case it
    // exists for. See EventStep::persistentDataId.
    EventBuilder& MoveToHoldUntilPersistentData(float x, float y, float z, float radius,
                                                uint32 dataId, uint32 minValue);
    // Garrison variant gated on the instance BOSS STATE: hold at (x,y,z) until
    // GetBossState(bossStateId) is in `clearMask` (DcBossStateBit values OR'd).
    // Chain .RestartOnBossState(mask) to rewind the whole event when the state
    // shows the encounter reset. See EventStep::bossStateId.
    EventBuilder& MoveToHoldUntilBossState(float x, float y, float z, float radius,
                                           uint32 bossStateId, uint32 clearMask);
    // Rewind the event to step 0 when the PRECEDING boss-state garrison reads a
    // state in `mask`. Chain it right after the step:
    //   .MoveToHoldUntilBossState(x, y, z, r, SLOT, DcBossStateBit(DONE))
    //       .RestartOnBossState(DcBossStateBit(FAIL))
    EventBuilder& RestartOnBossState(uint32 mask);
    // Run ObjectiveHookRegistry `hookId` every tick while the PRECEDING garrison
    // step holds, in addition to its gate. The hook's result is ignored — the gate
    // alone still ends the step. Chain it right after the step it arms:
    //   .MoveToHoldUntilInstanceData(x, y, z, r, dataId, min).WhileHolding(hook)
    //
    // For a garrison whose gate can regress BEHIND THE PARTY'S BACK, where the
    // usual "start it with a Custom step, then hold for the finish" pair silently
    // stops being true. The Ring of Law is the case it exists for: npc_grimstone's
    // own 30s "no summon has a victim" watchdog SetData(FAIL)s the arena, which
    // despawns Grimstone and every wave and puts TYPE_RING_OF_LAW back to
    // NOT_STARTED. Nothing then restarts it — the Custom step that fired the
    // areatrigger latched Done minutes ago, and the test-run areatrigger relay is
    // EDGE-triggered on a volume the party is already standing in — so the hold
    // burns its whole timeout on an encounter that is no longer running. Re-running
    // the start hook from inside the hold closes that: it is a no-op while the
    // encounter is live and re-fires the real trigger once it isn't.
    EventBuilder& WhileHolding(uint32 hookId);
    EventBuilder& UseGO(uint32 goEntry, float searchRadius = 0.0f,
                        float x = 0.0f, float y = 0.0f, float z = 0.0f);
    // Skip the LAST-added UseGO's click when the map's InstanceScript already
    // reads GetData(dataId) == value (see EventStep::skipIfDataId). Chain it
    // right after the step:
    //   .UseGO(GO_FIRE_1, 20).SkipIfInstanceData(TYPE_FIRE1, DONE)
    EventBuilder& SkipIfInstanceData(uint32 dataId, uint32 value);
    // Deliver the LAST-added UseGO step's click as a report-use rather than a
    // GameObject::Use() (see EventStep::reportUse). Chain after the step:
    //   .UseGO(GO_LEVER, 80).ReportUse()
    EventBuilder& ReportUse();
    // Put `itemId` in the clicker's bags (if missing) before the LAST-added UseGO
    // step's click: for a GO whose script checks that the clicker carries a key
    // item. Karazhan's Blackened Urn needs item 24140 once mod-individual-progression
    // rebinds go_blackened_urn; without it the click is silently refused.
    //   .UseGO(GO_URN, 30).CarryItem(ITEM_URN)
    EventBuilder& CarryItem(uint32 itemId);
    // Leader casts `spellId` on itself (triggered: no cost/cooldown/reagent/cast
    // time). For a scripted "use a quest item" spell whose effect a bot cannot
    // otherwise reach — e.g. Sunken Temple's "Awaken the Soulflayer" (12346),
    // which a player normally fires via Yeh'kinya's Scroll / Egg of Hakkar to
    // summon the Shade of Hakkar. The spell's SEND_EVENT script only checks the
    // instance + event-not-started, so a direct triggered cast summons the Shade
    // without the quest item (and no-ops if the event already started).
    EventBuilder& CastSpell(uint32 spellId);
    // Leader USES `itemId` (granted first if not in bags), via the full item-use
    // cast path. For a scripted "use a quest item" the bot never earned — Sunken
    // Temple's Egg of Hakkar (10465), which fires Awaken the Soulflayer to summon
    // the Shade ONLY when used as an item (a bare CastSpell is rejected).
    EventBuilder& UseItem(uint32 itemId);
    EventBuilder& Gossip(uint32 creatureEntry, int32 option, float searchRadius = 0.0f);
    EventBuilder& WaitForSpawn(uint32 creatureEntry, bool wantAlive = true,
                               uint32 timeoutMs = 0);
    EventBuilder& WaitForGOState(uint32 goEntry, uint32 wantState,
                                 uint32 timeoutMs = 0, float searchRadius = 0.0f);
    EventBuilder& KillCreature(uint32 creatureEntry, uint32 count = 1,
                               float searchRadius = 0.0f);
    // KillCreature variant whose driving action actively seeks out and engages
    // the creature (EngageDirect) rather than only gating on its death. See
    // EventStep::engage.
    EventBuilder& KillCreatureEngage(uint32 creatureEntry, uint32 count = 1,
                                     float searchRadius = 0.0f);
    // Bar the LAST-added engage step from the combat-side stealth-breaker's
    // "arm from any engage step" fallback. Chain it right after the step:
    //   .KillCreatureEngage(boss, 1, 130).EngageOnlyWhenActive()
    // For a target that is invisible/untargetable by script until the earlier
    // steps have run. See EventStep::engageOnlyWhenActive.
    EventBuilder& EngageOnlyWhenActive();
    // Point-anchored room pre-clear: the driving action engages every reachable
    // hostile within `radius` (2D) and `zBand` (vertical, floor-keeping) of the
    // centre (x,y,z); the step is Done once none remain. Use on an OBJECTIVE
    // anchor placed at the centre so boss-nav travels the tank in first.
    EventBuilder& ClearRadius(float x, float y, float z, float radius,
                              float zBand = 20.0f);
    // Restrict the PRECEDING ClearRadius to the given creature entries (see
    // EventStep::entryFilter). Chain it right after the step it narrows:
    //   .ClearRadius(x, y, z, r).OnlyEntries({ENTRY_A, ENTRY_B})
    EventBuilder& OnlyEntries(std::vector<uint32> entries);
    // Widen the PRECEDING WaitForSpawn / MoveToHoldUntilSpawn gate to also accept
    // any of `entries` (see EventStep::orEntries). Chain right after the step:
    //   .MoveToHoldUntilSpawn(x, y, z, r, NPC_A).OrSpawnOf({NPC_B, NPC_C})
    EventBuilder& OrSpawnOf(std::vector<uint32> entries);
    EventBuilder& Wait(uint32 durationMs);
    EventBuilder& Custom(uint32 hookId);
    // Protect a moving escortee (see EventStepKind::EscortCreature). `escortee` is
    // the NPC entry, `startGossipOption` the menu option that starts (and, when the
    // escortee re-offers a gossip mid-route, RESUMES) its scripted escort, and
    // `doneEntry`/`doneBit` the final boss whose existence/kill completes the step.
    //
    // `doneDataId`/`doneDataMin` are an ALTERNATIVE completion gate for an escort
    // whose end is not marked by a boss going live but by the map's monotonic
    // progress counter reaching a value (Old Hillsbrad's whole Thrall escort — one
    // step from freeing him through Epoch Hunter's death — completes when
    // DATA_ESCORT_PROGRESS reaches FINISHED). -1 => unused (boss-entry gate only).
    // The two gates are OR'd: either satisfies completion.
    //
    // The geometry knobs carry sensible defaults; `searchRadius` is the live-
    // escortee grid scan radius (widen it for a mounted escort that outpaces the
    // party so the escortee is never lost off-grid).
    EventBuilder& EscortCreature(uint32 escortee, int32 startGossipOption,
                                 uint32 doneEntry, int32 doneBit,
                                 float standoff = 5.0f, float threatRadius = 18.0f,
                                 float threatZBand = 20.0f, float searchRadius = 80.0f,
                                 int32 doneDataId = -1, uint32 doneDataMin = 0);
    // Use a quest item ON a positioned GameObject (see EventStepKind::UseItemOnGO):
    // approach the `goEntry` GO nearest (x,y,z), then USE `itemId` on it via the
    // full item-use cast path (granting the item first if the bags lack it;
    // `spellId` is the item's use-spell, kept for validation and logging), so its
    // SmartAI SPELLHIT fires (the Durnholde barrel-bomb). `radius` is the CAST
    // REACH (0 => 3.5yd STRICT world distance — the landed OPEN_LOCK range-checks
    // the GO's interact box, live-measured failing at 6yd; arrival additionally
    // requires vmap LINE OF SIGHT so a barrel across a thin wall never reads as
    // reachable). The approach INTO the house is driven tick-owning
    // (DriveUseItemOnGO), which threads the doorway, detours to the anchor when
    // the barrel is walled off, and force-walks the final un-meshed yards.
    EventBuilder& UseItemOnGO(uint32 itemId, uint32 spellId, uint32 goEntry,
                              float x, float y, float z, float radius = 0.0f);
    // Use a quest item AT a place (see EventStepKind::UseItemAt): walk to (x,y,z),
    // wait out the item's own cooldown, then USE `itemId` there (granting it first
    // if the bags lack it; `spellId` is its use-spell, used for the cooldown probe
    // and the log). `receiptGoEntry` is the GO the mechanic leaves BEHIND on
    // success — the step latches Done once one stands within
    // DC_EVENT_USEITEM_LATCH of the anchor, which makes it idempotent across
    // rewinds and a re-entered instance.
    //
    // Argument order deliberately mirrors UseItemOnGO, but the GO means the
    // opposite thing: there it is the TARGET of the cast, here it is the RECEIPT
    // for a cast that targets a creature the engine picks itself.
    //
    // `radius` is the ARRIVAL radius (0 => DC_EVENT_USEITEM_REACH). It is not a
    // cast range — the spell's own range does that job — so it only has to be
    // tight enough that the intended trigger is the nearest candidate.
    EventBuilder& UseItemAt(uint32 itemId, uint32 spellId, uint32 receiptGoEntry,
                            float x, float y, float z, float radius = 0.0f);
    // Drop the party down a narrow vertical hole (see EventStepKind::DropInHole).
    // (overX,overY,overZ) is the over-hole nudge target the leader glides to (a
    // point whose column is open straight to the deep floor — NOT the lip, whose
    // column catches a mid-shaft ledge); (landX,landY,landZ) the deep-floor landing
    // the leader falls onto and the followers teleport to. The preceding step must
    // settle the leader on the lip (a short MoveTo), exactly like the Serpentis
    // Jump's MoveTo→Jump pair.
    EventBuilder& DropInHole(float overX, float overY, float overZ,
                             float landX, float landY, float landZ);
    // Teleport the whole party across a navmesh break (see
    // EventStepKind::TeleportParty). (checkpointX,Y,Z) is the spot the leader must
    // reach — the objective anchor's navigation drives it there up the ramp; once
    // there (within `radius`), the leader and every party bot are relocated to
    // (landX,Y,Z). `radius` defaults generous so the objective arrival always
    // satisfies the gate (no mid-ramp teleport, no stutter-step closing move).
    EventBuilder& TeleportParty(float checkpointX, float checkpointY, float checkpointZ,
                                float landX, float landY, float landZ,
                                float radius = 12.0f);

    // Returns the event with the per-step stamps applied (EventStep::partyWide,
    // EventStep::lastMovementStep) — the one place the step list learns what
    // the event around it is.
    DungeonEvent Build() const;

private:
    EventStep& Add(EventStepKind kind);
    DungeonEvent _ev;
};

class DungeonEventRegistry
{
public:
    // The full event table, in registration order. Exposed as a seam for the
    // registry-integrity gtests (t/TestEventRegistry.cpp) — cross-reference and
    // persistence lint over the authored data. Not used by runtime callers, which
    // key on Find/Conditional.
    static std::vector<DungeonEvent> const& AllEvents();

    // The event with `id` on `mapId`, or nullptr if none is registered.
    static DungeonEvent const* Find(uint32 mapId, uint32 id);

    // True if the map has any registered event (cheap gate for callers).
    static bool HasEvents(uint32 mapId);

    // Every Conditional-activation event registered for `mapId`, in table order.
    // Empty for maps with only Anchored events. UNFILTERED by difficulty — an
    // authoring/test seam like AllEvents. Runtime callers use the difficulty
    // overload below so a heroic-only event never surfaces on a normal run.
    static std::vector<DungeonEvent const*> Conditional(uint32 mapId);

    // The Conditional events whose difficulty gate matches `key`. This is
    // the runtime lookup — every live consumer (executor, panel, targeting) has
    // the bot's map in hand and must pass its difficulty key (DcDifficulty::Of).
    static std::vector<DungeonEvent const*> Conditional(uint32 mapId, DcDiffKey key);

    // --- Room-aggro pre-clear (milestone 3) ------------------------------

    // True if `ev` is a room-aggro PRE-CLEAR event: a Conditional gate whose lone
    // KillCreature step uses creatureEntry 0 ("room-trash" mode — the target set
    // is resolved live through DungeonClearRoomTrashValue / the room-trash
    // targeting seam, not a fixed entry). These re-express the RoomAggroRegistry
    // bosses on the events framework: the condition (room trash remains) gates the
    // boss pull and DcRunEventAction drives the engage (nearest room trash first).
    // They are NEVER latched — the gate re-fires for each room-aggro boss on the
    // map (the condition simply reads false once a room is clear).
    static bool IsRoomAggroPreClear(DungeonEvent const& ev);

    // True if `mapId` has a room-aggro pre-clear event authored. The legacy
    // standalone room-clear trigger (relevance 26) stands down for such maps so
    // the conditional-event path (relevance 31) is the single room-clear driver.
    static bool HasRoomAggroEvent(uint32 mapId);
};

#endif
