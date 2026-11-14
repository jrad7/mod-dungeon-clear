/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARMATH_H
#define _PLAYERBOT_DUNGEONCLEARMATH_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "G3D/Vector3.h"
#include "Position.h"

namespace DungeonClearMath
{
    // Sentinel "no rejoin crumb" returned by FindTrailRejoin.
    inline constexpr std::size_t TrailRejoinNone = static_cast<std::size_t>(-1);

    // Sentinel "no member needs healing" returned by SelectHealTarget.
    inline constexpr std::size_t HealTargetNone = static_cast<std::size_t>(-1);

    // One candidate heal target for SelectHealTarget. `healthPct` is the member's
    // current health (0..100); `isLeaderTank` marks the elected dungeon-clear
    // leader tank so it can be favoured. Deliberately carries NO line-of-sight or
    // range flag: heal-reposition selection must survive LOS loss (the stock
    // `party member to heal` value drops out-of-LOS members entirely, which is the
    // root of the "healer stops healing when the tank is dragged out of sight"
    // bug). The caller resolves members to candidates from live game state.
    struct HealCandidate
    {
        float healthPct;
        bool  isLeaderTank;
    };

    // Pure heal-target selector (LOS-blind). Among members BELOW `hpFloor` (the
    // "actually needs healing" gate), pick the one with the lowest tank-biased
    // score: `healthPct - (isLeaderTank ? tankBias : 0)`. The bias only breaks the
    // choice toward the tank among members that already need healing — a healthy
    // tank above the floor is never selected — so a hurt tank being kited out of
    // sight is preferred over an equally/slightly-more-hurt DPS, while a healthy
    // tank never steals the pick from a hurt DPS. Returns the chosen index, or
    // HealTargetNone when nobody is below the floor. Separated from the group
    // iteration so the choice is unit-testable.
    std::size_t SelectHealTarget(std::vector<HealCandidate> const& members,
                                 float hpFloor, float tankBias);

    // Pure standoff-point generator. Produces candidate 2D points on a circle of
    // radius `standoffRadius` around `center`, ORDERED so the first is on the side
    // the bot already stands (shortest move / most likely to round the same corner)
    // and the rest fan out alternately to either side. The caller snaps each to the
    // navmesh and keeps the first that has line of sight and a clear path to the
    // center. `ringPoints` extra points are generated past the first (so the result
    // has `ringPoints + 1` entries). Z is left at the center's Z; the caller
    // re-grounds via navmesh snap. With a degenerate bot-on-center input the bias
    // direction falls back to +X. Nothing here is heal-specific: the healer LOS
    // reposition and the contribution-gated combat regroup both sample a role-range
    // ring around a fight anchor with it (see DcActionShared::FindStandoffPoint).
    std::vector<Position> StandoffCandidates(Position const& center,
                                             Position const& bot,
                                             float standoffRadius,
                                             std::uint32_t ringPoints);

    // Deprecated heal-specific alias for StandoffCandidates, kept so the existing
    // heal-reposition call site and t/TestDungeonClearMath / t/TestHealReposition
    // compile unchanged. Prefer StandoffCandidates in new code.
    inline std::vector<Position> HealStandoffCandidates(Position const& target,
                                                        Position const& bot,
                                                        float standoffRadius,
                                                        std::uint32_t ringPoints)
    {
        return StandoffCandidates(target, bot, standoffRadius, ringPoints);
    }

    // The point `stopShort` yards before the END of a route polyline, measured
    // ALONG the route — the close-on-a-unit fallback for the heal reposition and
    // the combat regroup ("walk at them and let the corner give you sight").
    //
    // It used to be a point on the straight bot->target line, and a straight line
    // is not a walk. Across a wall that point is inside the wall or on its far
    // side: Karazhan's Maiden hairpin, where the two legs of the corridor sit a few
    // yards apart through a wall with no navmesh in it, had healers and DPS
    // walking into that wall toward a groupmate on the other leg and dropping to
    // the Servants' Quarters below (tr-20260927-184653-9, -190943-12). A point on
    // the route is on the mesh by construction, and walking to it rounds the
    // hairpin the way a player would.
    //
    // A route no longer than `stopShort` returns its first point (already close
    // enough — nothing to walk); an empty route returns the origin.
    inline G3D::Vector3 PointShortOfPathEnd(std::vector<G3D::Vector3> const& path,
                                            float stopShort)
    {
        if (path.empty())
            return G3D::Vector3(0.0f, 0.0f, 0.0f);
        float remaining = std::max(0.0f, stopShort);
        for (std::size_t i = path.size() - 1; i > 0; --i)
        {
            G3D::Vector3 const& a = path[i - 1];
            G3D::Vector3 const& b = path[i];
            float const seg = (b - a).length();
            if (seg >= remaining)
            {
                if (seg <= 0.0f)
                    return b;
                return b + (a - b) * (remaining / seg);
            }
            remaining -= seg;
        }
        return path.front();
    }

    // A point `distFromAnchor` yards out from `anchor` along the 2D bearing to
    // `toward`, with z carried along the SAME fraction of the way.
    //
    // Interpolating z is the whole reason this is a shared helper and not two
    // lines at the call site: leaving it at the anchor's height describes a point
    // that exists nowhere — the anchor's floor over the hold point's x/y. That is
    // flat-leg-only reasoning, and the legs this is used on are not all flat (the
    // Suppression Rooms ramp climbs 5yd over one 20yd leg).
    //
    // Clamped to `toward` when it is nearer than `distFromAnchor`, and returns
    // `anchor` unchanged when the two are stacked within `bearingFloor` (no
    // meaningful bearing to walk out along).
    inline Position PointTowardFrom(Position const& anchor, Position const& toward,
                                    float distFromAnchor, float bearingFloor)
    {
        float const dx = toward.GetPositionX() - anchor.GetPositionX();
        float const dy = toward.GetPositionY() - anchor.GetPositionY();
        float const bearing = std::sqrt(dx * dx + dy * dy);
        if (!(bearing >= bearingFloor))
            return anchor;

        float const out = std::min(std::max(distFromAnchor, 0.0f), bearing);
        float const frac = out / bearing;
        return Position(anchor.GetPositionX() + dx * frac,
                        anchor.GetPositionY() + dy * frac,
                        anchor.GetPositionZ() +
                            (toward.GetPositionZ() - anchor.GetPositionZ()) * frac,
                        anchor.GetOrientation());
    }

    // Should follow-tank's centered breadcrumb trail own this follower's tick?
    //
    // Two independent conditions, and the second is the one that was missing.
    //
    // `toTank > trailEngage` is the ORIGINAL leash: only trail once a real
    // corridor traversal is involved, not a fan-out shuffle inside the follow
    // bubble.
    //
    // `toTank > lag` is the guard the rung had no equivalent of. The crumb this
    // branch aims at is `lag` yards behind the tank, and the stagger makes `lag`
    // grow per slot while `trailEngage` does not — so a follower INSIDE its own
    // slot but outside the leash was glided BACKWARD, down the trail, away from
    // the party. That immediately re-armed the stock Follow() fan (radius `dist`)
    // which walked it forward again, and ownership then flipped on every crossing
    // between two aim points up to `lag + dist` apart. On a ramp the backward leg
    // is a walk down the incline and the forward leg climbs it again, so the slope
    // sets the amplitude: the reported "bots ping-pong on ramps". Live at the
    // shipped Playerbots.FollowDistance of 1.5 the leash is 3.5 and the crumbs
    // sit at 1.5 / 4.5 / 7.5 / 10.5 — three of the four slots retreat.
    //
    // The invariant, stated generally: a rung must not move the bot AWAY from the
    // condition that armed it. cf. the scout-lag branch, which has always held
    // whenever `toTank <= lag` and only trails past it, and the follower cursor's
    // own HopIsBehind ("a hop behind the bot is never worth walking to at ANY
    // distance — the route is one-way").
    //
    // `toTank` must be a 3D distance: the crumb spacing is walked 3D distance and
    // the arrival hold is GetExactDist, so a 2D read here shrinks with the cosine
    // of the slope and arms the leash at a larger true separation on every ramp.
    inline bool TrailFollowShouldEngage(float toTank, float trailEngage, float lag)
    {
        return toTank > trailEngage && toTank > lag;
    }

    // One forward hostile for the Dynamic-pull aggro estimate. `chainEligible` is
    // pre-resolved by the caller from game state: true only when this mob is
    // navmesh-reachable from the pull target with a clear line of sight and no
    // closed door between (the far-targets scan ignores LOS, so this gate keeps a
    // mob through a wall / a floor away / behind a door from counting as one that
    // would aggro). It is ignored for mobs that share the target's own pack. `z`
    // carries the mob's world height so the estimate can reject mobs on another
    // floor (a ledge/ramp directly above or below) instead of counting them by
    // plan-view distance alone — see `zTolerance` on EstimateAggroCount.
    //
    // `aggroReach` is this mob's real aggro radius (Creature::GetAggroRange +
    // GetCombatReach, level-diff scaled — see DungeonClearUtil::ClassifyPull-
    // Advanced) measured against the squishiest party member. It is the distance
    // at which the mob proximity-aggros the party fighting at the camp spot, so it
    // self-tunes per zone/level instead of a single hand-tuned chain radius.
    //
    // `packId` is an OPTIONAL engine-pack identity (0 = none) the caller resolves
    // from what actually pulls together: a creature formation group, or a
    // creature_linked_respawn link. Mobs sharing a non-zero `packId` pull as one
    // atomic unit regardless of spacing or height (you cannot pull half a
    // formation), so a counted member drags its whole pack into the estimate.
    // `patroller` marks a DB-authored waypoint mover (GetDefaultMovementType() ==
    // WAYPOINT_MOTION_TYPE). A human tank waits a patrol out instead of committing
    // the heavier pull for a pack that would be a clean small Leeroy moments later;
    // the patrol-wait gate re-runs the estimate with lone patrollers excluded (see
    // `excludeLonePatrollers` on EstimateAggroCount) to detect that case. Random
    // wanderers are deliberately NOT patrollers — their return is unpredictable.
    // `elite` marks an elite-tier body (Creature::isElite(): elite / rare-elite /
    // worldboss rank). It tunes only the WEIGHT a counted mob adds to the pull-
    // weight tally (see EstimateAggroCount's weightThirdsOut), never seeding,
    // assist, or pack membership — a normal mob is a third of an elite because the
    // MaxLeeroyMobs ceiling was set thinking in elites, and a big room of weak
    // trash should not force cautious one-cluster-at-a-time Advanced pulls.
    struct DynPullMob
    {
        float         x;
        float         y;
        float         z;
        bool          chainEligible;
        std::uint32_t packId = 0;
        float         aggroReach = 0.0f;
        bool          patroller = false;
        bool          elite = false;
    };

    // Pure Dynamic-pull estimate: how many mobs aggro if the party Leeroys on top
    // of `mobs[targetIdx]`? The camp spot is the target's position (where a Leeroy
    // fight happens). Returns the estimated body count; the caller compares it to
    // its Leeroy ceiling (count > ceiling => Advanced pull, else Leeroy).
    //   - Seed = the target, its atomic pack (shared non-zero `packId`), and every
    //     other mob that proximity-aggros the camp: within `aggroReach + combat-
    //     Spread` (2D), on the same level (`zTolerance`), and `chainEligible`.
    //     `combatSpread` widens the camp from a point to a disc to model players
    //     drifting to flank/kite during the fight.
    //   - One assist hop: a `chainEligible` mob within `assistRadius` (2D, same
    //     level) of a SEED mob joins via CallForHelp. Proximity aggro from a fixed
    //     camp does not chain, so assisted mobs do NOT seed further proximity or
    //     assist — exactly one ring.
    //   - Formation closure: any pack touched by the set is counted in full.
    // `zTolerance` keeps the estimate honest in multi-level rooms: WotLK inter-
    // floor gaps exceed it, so a mob a ramp above/below never counts. Separated
    // from the game-state resolution in DcPullPlanner::ClassifyPullAdvanced so
    // the logic is unit-testable.
    // `excludeLonePatrollers` (patrol-wait gate): when true, a mob marked
    // `patroller` that is NOT part of an atomic pack (`packId == 0`) is treated as
    // not `chainEligible` — it neither seeds proximity nor assists nor is assisted
    // — modelling "if we let this patrol pass". Formation/linked patrollers
    // (`packId != 0`) are unaffected: you cannot wait out half a formation.
    // `countedOut` (optional) receives the indices of every mob the estimate
    // counted, for diagnostic logging at the call site.
    // `weightThirdsOut` (optional) receives the counted set's PULL WEIGHT, in
    // thirds of an elite: each counted elite adds 3, each counted normal adds 1.
    // This is the value the verdict compares to a x3-scaled ceiling so a normal mob
    // weighs a third of an elite (the ceiling was tuned in elites). The return value
    // stays the raw body count (what `countedOut` enumerates) for diagnostics.
    std::uint32_t EstimateAggroCount(std::vector<DynPullMob> const& mobs,
                                     std::size_t targetIdx, float combatSpread,
                                     float assistRadius, float zTolerance,
                                     bool excludeLonePatrollers = false,
                                     std::vector<std::size_t>* countedOut = nullptr,
                                     std::uint32_t* weightThirdsOut = nullptr);

    // Pull CC-assist grace gate (pure). Decides whether a CC-impaired drag-back
    // should be ABORTED so the party drops passive and piles in to help the tank.
    // `impaired` is the caller's verdict that the leader tank is currently under a
    // pull-ruining control effect (stun / fear / confuse / root / heavy slow).
    // `ccSince` is the timestamp the CURRENT continuous impairment began (0 = not
    // impaired right now). Each tick: while impaired, arm/keep the latch and abort
    // once it has persisted for `graceMs`; while clear, disarm it. A brief micro-CC
    // that clears within the grace therefore never throws the pull away on a
    // flicker, while sustained CC (the pull is failing) releases the party. With
    // `graceMs` == 0 the very first impaired tick aborts. Returns true to abort and
    // writes the updated latch to `ccSinceOut` (which the caller persists in the
    // pull context). Separated from the Unit-state read at the call site so the
    // timing logic is unit-testable.
    bool ShouldAbortPullForCc(bool impaired, std::uint32_t ccSince,
                              std::uint32_t now, std::uint32_t graceMs,
                              std::uint32_t& ccSinceOut);

    // Pack-cannot-follow gate (pure). Decides whether a drag-back should be
    // ABANDONED because the mob being dragged has no combat movement — the caller's
    // `planted` verdict, read off UNIT_STATE_NO_COMBAT_MOVEMENT at the call site.
    // Such a mob has had its chase generator removed, so it holds the ground it was
    // tagged on however far the tank retreats: the drag is not slow, it is
    // impossible, and the tank should turn around and fight it where it stands.
    //
    // Shares the latch/grace contract of ShouldAbortPullForCc exactly (including the
    // now==0 corner and `confirmMs` == 0 firing on the first tick), and is delegated
    // to it — kept as a separate name because the QUESTION is different and the two
    // are tuned independently. The grace is a debounce, not a confidence threshold:
    // the unit state is exact, but some creatures toggle it transiently (planting
    // only for the duration of a cast), and those must not lose their drag.
    bool ShouldAbandonPlantedDrag(bool planted, std::uint32_t plantedSince,
                                  std::uint32_t now, std::uint32_t confirmMs,
                                  std::uint32_t& plantedSinceOut);

    // Camp-safety valve (pure). A held passive follower should trigger the valve
    // when it is in combat below `safetyHpPct` AND that has persisted for
    // `graceMs`. `attackerIsPullTarget` is the caller's verdict that everything
    // currently on the follower belongs to the pack being dragged — in which case
    // this is an ordinary drag taking splash, not a failed pull, and the valve
    // must not fire (raw HP% alone is the wrong question: a follower at 60% being
    // splashed by the dragged pack is a normal drag; at 60% from something ELSE, a
    // second pack found us and the maneuver is genuinely compromised).
    // `safetyHpPct` <= 0 disables the valve outright. `since` is the timestamp the
    // CURRENT continuous qualifying spell began (0 = fine); the latch/grace
    // contract mirrors ShouldAbortPullForCc exactly (including the now==0 corner).
    // `graceMs` == 0 fires on the first qualifying tick. Returns true to trip the
    // valve and writes the updated latch to `sinceOut` (persisted in the
    // follower's DcPullContext::campSafetySince by the caller).
    bool ShouldTripCampSafety(bool inCombat, float healthPct, float safetyHpPct,
                              bool attackerIsPullTarget,
                              std::uint32_t since, std::uint32_t now,
                              std::uint32_t graceMs, std::uint32_t& sinceOut);

    // Pull-mode blocking-trash stand-down gate (pure). In advanced-pull mode the
    // pull pipeline owns the pack it is working, so the blocking-trash trigger
    // stands down for it (engage-trash outranks the pull's deliberate wait band
    // and would otherwise preempt every pull). But a BYSTANDER — a pack the pull
    // never selected — appearing inside the aggro-shaped scan while the pull is
    // IDLE is exactly the case the scan exists for, and silently handing it to a
    // pipeline that is not looking at it left the tank walking into it under
    // Advance. Returns true to STAND DOWN (the pull pipeline owns the tick):
    // always for the pull's own pack, and for anyone once a maneuver is in
    // flight (non-Idle — never thrash it). Returns false only for a bystander
    // while the pull is Idle: the blocking-trash walk-in owns that tick. The
    // game-state read (pack identity vs decision/pull target, phase) stays at
    // the trigger; this carries only the decision so it is unit-testable.
    bool ShouldStandDownForPull(bool packIsPullsOwn, bool pullPhaseIdle);

    // Advance's yield to a live engage-trash walk-in (pure). Engage-trash (rel 25)
    // and Advance (rel 15) share the tank's movement: when the blocking-trash
    // trigger only holds on alternate ticks, each tick the winner replaces the
    // other's move — engage's DcMoveTo cancels the escort glide, Advance reads the
    // resulting POINT generator as "no glide running" and re-issues its spline —
    // and every replacement starts with a stop, so the tank stands still
    // (tr-20260924-081931-3, Karazhan Opera balcony stair). Returns true while a
    // walk-in engage-trash stamped at `stampMs` is at most `holdMs` old, its
    // target is still alive and the bot is still moving on it. Bounded both
    // ways: engage going quiet for `holdMs`, or the walk no longer moving (a
    // real wedge for Advance's stuck ladder), hands the tick back.
    bool ShouldYieldToEngageWalk(bool walkStamped, bool targetAlive, bool botMoving,
                                 std::uint32_t stampMs, std::uint32_t now,
                                 std::uint32_t holdMs);

    // The boss engage's last resort (pure): tag the boss from the standoff because
    // the walk-in is refused. The at-boss trigger fires the engage once the party is
    // ready, and the engage walks the tank at the boss; when stock finds no route
    // there that walk is refused every tick, the engage returns false, Advance's
    // engage hold parks the tank and clears the stall — and nothing ever pulls
    // (sk-20261019-003633 tr-…-12: 4063 refusals in 16 minutes at 30.5yd from
    // Emperor Dagran Thaurissan on his raised throne, party ready throughout, until
    // the 900s watchdog). True once the walk has been refused for `graceMs` with the
    // tank standing still, the boss in line of sight and within `maxRange`.
    bool ShouldTagBossFromStandoff(bool walkRefused, bool botMoving, bool inLos, float dist,
                                   float maxRange, std::uint32_t refusedSinceMs,
                                   std::uint32_t now, std::uint32_t graceMs);

    // Engage-trash sticky staleness (pure). The sticky pins a walk-in target so the
    // pick does not bounce between two equidistant corridor mobs, and it never
    // releases on distance. But the trigger fires on its OWN fresh pick: once the
    // sticky is a pack an earlier scan found far down the route, the trigger keeps
    // firing for the mob in front of the tank while the action walks back to the
    // sticky, the walk carries that mob out of the band, the trigger drops, Advance
    // climbs back, and it repeats (tr-20260927-201144-5, Karazhan ramp to
    // Terestian: a Mana Feeder 90yd back vs an Arcane Protector patrolling ahead).
    // Returns true to drop the sticky for the fresh pick when a different fresh
    // pick exists and the sticky is out of combat, and either:
    //  - the sticky is outside the corridor band the fresh pick came from
    //    (`stickyInBand` false). Distance cannot see this case: on the Karazhan
    //    opera balcony ramp (tr-20261114-113223-1) the sticky was a Skeletal Usher on
    //    the floor below, NEARER than the balcony Patron the trigger named, so the
    //    margin never fired, and each walk-in dragged the tank down the ramp
    //    until the off-line rejoin struck out. Or
    //  - the sticky is more than `margin` farther away than the fresh pick (the
    //    fallback when band membership is unknown: pass `stickyInBand` true).
    // Two in-band corridor mobs at comparable range keep the sticky: no flip-flop.
    bool ShouldDropTrashSticky(bool haveFresh, bool freshIsSticky, bool stickyInCombat,
                               bool stickyInBand, float stickyDist, float freshDist,
                               float margin);

    // Orphaned-pull release gate (pure). The effective pull mode can be forced
    // OFF while a pull is still standing — a PERSISTENT anchored event takes the
    // tank (DungeonClearPullModeCurrentValue), or a Dynamic verdict drops. The
    // pull FSM is then unreachable: its non-combat driver (DungeonClearPullTrigger)
    // gates on the EFFECTIVE mode, so nothing ever runs the Engage->Idle cleanup,
    // while the followers' camp hold and the party-spread gate read the LATCHED
    // mode and keep obeying a camp nobody can move. Live in heroic Old Hillsbrad
    // (runs tr-20260801-174432-3/-7): an unplanned camp pull during the barrel
    // event left phase=Engage, the party pinned ~100yd back at the frozen camp,
    // the barrel drive holding for a party that had been told to stand there, and
    // stranded-recovery teleporting everyone forward once a minute only for
    // hold-at-camp to walk them straight back. Returns true to dismantle the
    // standing pull (phase -> Idle, camp cleared) so the party reverts to plain
    // follow. `effectiveOn` is the current effective pull mode; `standing` is
    // "there is something to release" (non-Idle phase or a marked camp). Never
    // releases mid-maneuver: `partyInCombat`, a holding phase (Forming/Advancing/
    // Returning — the drag must finish) and `bossPullback` (a pull-back drag runs
    // with pull mode off BY DESIGN) all hold it off.
    //
    // `partyInCombat` IS PARTY-WIDE, and used to be the leader's own flag. That was
    // wrong in the one arrangement this release exists inside: a camp fight. The
    // pack is dragged to the camp and handed to the followers, and the tank in
    // Engage is routinely flag-clear for stretches of it — a scripted stage tags at
    // range, so the pack arrives late and strung out, and threat lands on whoever it
    // reaches first. Reading only the tank there dismantles the camp WHILE the
    // followers are fighting at it: the hold releases, the party reverts to follow,
    // and the tank walks off to form the next pull with the last pack still up. In
    // the MgT rotunda that is the difference between a five-elite fight and a
    // ten-elite one, and 8 of the 9 rotunda losses in tp-20260805-191829-1 died
    // within a few yards of an authored camp.
    bool ShouldReleaseStandingPull(bool effectiveOn, bool standing, bool partyInCombat,
                                   bool holdingPhase, bool bossPullback);

    // Unclassified-aggro gate for the Dynamic governor (pure). Between packs the
    // governor answers "no target" with the bool OFF, and with the bool off the pull
    // action is not live — including its Idle-branch "unplanned aggro while scouting
    // -> fresh camp" drag-back. An aggro the scan never sized therefore became a
    // walk-in fought where it bit. On a sweep map (RouteSweepRegistry) that spot is
    // inside a neighbour's reach by definition, so the answer is the safe direction:
    // flip to ADVANCED and let the maneuver drag it home. `hasVerdict` is the whole
    // discriminator — a standing LEEROY is a pack the classifier sized and chose to
    // walk in on, and that choice stands; only an aggro nobody sized is answered.
    // A boss (or his summoned add) among the attackers is never dragged: that is
    // the at-boss path's engagement, and scripted bosses misbehave when camp-dragged.
    // True only for: in combat, phase Idle, bool off, no verdict, sweep map, no boss.
    bool ShouldAdvanceUnclassifiedAggro(bool inCombat, bool phaseIdle, bool modeOn,
                                        bool hasVerdict, bool sweepMap, bool bossInFight);

    // Dynamic-verdict drop grace gate (pure). A standing Leeroy/Advanced verdict
    // must survive a TRANSIENT no-target read (door veto flicker, long-path cache
    // mid-rebuild, far-targets poll boundary): dropping it instantly flips the
    // pull-mode bool, releasing the camp hold and stripping daze immunity for a
    // single bad tick, then re-deriving everything — the party lurch. `targetPresent`
    // is the caller's verdict that the pull target resolved this tick. `lostSince`
    // is the timestamp the target first resolved null while the verdict was
    // standing (0 = present). Each tick: while lost, arm/keep the latch and drop
    // once it has persisted for `graceMs`; while present, disarm it. Mirrors
    // ShouldAbortPullForCc's latch/clear contract (including the now==0 corner
    // and graceMs==0 => drop on the first lost tick). Returns true to drop the
    // verdict and writes the updated latch to `lostSinceOut` (persisted in
    // DcPullContext::targetLostSince by the caller).
    bool ShouldDropPullVerdict(bool targetPresent, std::uint32_t lostSince,
                               std::uint32_t now, std::uint32_t graceMs,
                               std::uint32_t& lostSinceOut);

    // Leeroy roll-in gate (pure). True when the party's scout-lag hold should
    // release because the tank is committing to a Leeroy on the verdicted pack:
    // the standing decision is Leeroy (1) and the tank is within
    // `commitRange + lead` (2D) of the live target. `decision` 0 (none, still
    // scouting) and 2 (Advanced — the camp machinery owns the party) never roll
    // in, nor does a dead/unresolvable target. The game-state resolution
    // (decision, live target, distances) stays in
    // DcLeaderSignal::IsLeaderDynamicScouting; this carries only the decision
    // logic so it is unit-testable.
    bool ShouldRollInForLeeroy(std::uint32_t decision, bool targetAlive,
                               float tankToTarget2d, float commitRange, float lead);

    // Patrol-wait gate (pure). A pull is "patrol-contended" when the ONLY thing
    // pushing the aggro estimate over the Leeroy ceiling is one or more lone
    // patrollers: `fullCount > ceiling` but `reducedCount <= ceiling` (the reduced
    // pass excluded them — see EstimateAggroCount's excludeLonePatrollers). For
    // such a pack a human waits the patrol out rather than committing the heavier
    // Advanced maneuver. Returns true to KEEP WAITING (hold the pull), false to
    // proceed (commit the standing verdict). Contended: arm/keep the `waitSince`
    // latch and wait until `waitMs` has elapsed, then proceed with the heavy
    // verdict (don't stall the run for a stationary/slow patrol) — the latch stays
    // armed past the timeout so the wait does not re-fire on the same contention.
    // Not contended (patrol left chain range, or never the cause): clear the latch
    // and proceed at once. `waitMs` == 0 proceeds immediately. Mirrors
    // ShouldAbortPullForCc's by-reference latch/clear contract. The game-state read
    // (the two estimates, the live target/commit distance that decides WHEN to arm)
    // stays in DcPullPlanner::UpdateDynamicPullMode.
    bool ShouldWaitForPatrol(std::uint32_t fullCount, std::uint32_t reducedCount,
                             std::uint32_t ceiling, std::uint32_t waitSince,
                             std::uint32_t now, std::uint32_t waitMs,
                             std::uint32_t& waitSinceOut);

    // Scripted-stage MUSTER gate (pure). A ScriptedPullRegistry stage is a planned
    // fight against a hand-counted pack, and the party should arrive at it topped
    // up — the way it arrives at a boss — rather than merely "not resting".
    //
    // The ordinary between-pulls floors do not deliver that. They are
    // min(90, Playerbots.AlmostFullHealth) HP and min(75, Playerbots.HighMana)
    // mana, which on stock config is 85/65, and 65% healer mana is thin for a
    // five-elite heroic pack that contains its own healer. Live
    // (tr-20260805-191834-3): the party reported "Shannon (low mana), Erinerice
    // (low HP)" at 12:25, the gate released at 12:30, the stage armed, and the tank
    // was dead 22 seconds into the fight with the whole party following inside 22
    // more.
    //
    // Returns true to KEEP HOLDING (do not arm the stage), false to proceed.
    // BOUNDED, and that bound is the point: the muster floors sit ABOVE what stock
    // bots eat/drink back up to, so an unbounded wait on a bot with no water in
    // its bags is a run that never continues. Arm/keep the `waitSince` latch while
    // the party is short, proceed once `waitMs` elapses, and leave the latch armed
    // so the same muster cannot re-fire. `toppedUp` (the caller's party read
    // against the muster floors) or `!stagePending` clears the latch and proceeds
    // at once; `waitMs` == 0 proceeds immediately. Same by-reference latch/clear
    // contract as ShouldWaitForPatrol. The party read stays in DcPartyState.
    //
    // `minMs` is the substance floor: once the muster has ARMED (the party was
    // genuinely below the floors for at least one tick), it holds for at least
    // min(minMs, waitMs) even if the percentages close sooner. An instantaneous
    // percentage test releases the tick one AoE heal crosses the line — live
    // musters ran 1-5s and no one ever drank (tp-20260806-212646-1). A party
    // already at the floors when the stage comes due still arms nothing.
    bool ShouldMusterForScriptedStage(bool stagePending, bool toppedUp,
                                      std::uint32_t waitSince, std::uint32_t now,
                                      std::uint32_t waitMs, std::uint32_t minMs,
                                      std::uint32_t& waitSinceOut);

    // Turn-and-plant gate (pure). A human tank dragging a pack back to camp turns
    // and fights the moment the pack is glued to it, rather than sprinting the
    // whole leg back-turned (the single biggest visual bot-tell, and why the
    // daze-immunity cheat exists). True when the drag-back should stop early and
    // the tank turn to fight: the pack is gathered (EVERY live attacker distance
    // <= `glueRadius`) for >= `glueTicksNeeded` consecutive maneuver ticks, this
    // is not an LOS-break pull (`losPull` — those must reach the corner), there is
    // something chasing (empty `attackerDists` => evade/fizzle, never a plant),
    // and at least the first HALF of the return leg is covered
    // (`distToCamp <= legStartDist / 2`, keeping the neighbour-pack clearance the
    // camp was measured for). `plantTicks` is the per-pull debounce latch: it is
    // incremented while the gather condition holds and reset to 0 the moment it
    // breaks, mirroring ShouldAbortPullForCc's by-reference latch contract — so a
    // single-tick noise spike can never trip an early plant. The game-state read
    // (attacker distances, leg progress) stays in DungeonClearPullManeuverAction;
    // this carries only the decision so it is unit-testable.
    bool ShouldPlantEarly(std::vector<float> const& attackerDists, float glueRadius,
                          std::uint32_t glueTicksNeeded, bool losPull,
                          float distToCamp, float legStartDist,
                          std::uint32_t& plantTicks);

    // Tag walk-in stop distance (pure), in yards from the target.
    //
    // The core only re-checks a pack's aggro on MOVEMENT, and its notice test is the
    // plain centre-to-centre distance vs Creature::GetAggroRange. So the tank must
    // GLIDE to a point strictly inside `aggroRange` — arriving is what crosses the
    // threshold — and it stops 2yd in.
    //
    // `closingMs` is HOW LONG THE TANK HAS BEEN CLOSING, and the creep it drives is a
    // backstop: a tank parked exactly at the edge is never re-evaluated (no relocation
    // -> no MoveInLineOfSight), so after `graceMs` the stop point steps inward at
    // `creepYardsPerSec` until something notices. It rarely runs at all.
    //
    // `creepFloor` bounds it — the closest the creep may bring the stop point. 0 lets
    // it run to body contact, which is right for an ordinary pull: the pack it ends up
    // touching is the pack it came for. A SCRIPTED stage passes a real floor, because
    // it is standing in a room full of formations the plan has deliberately left up
    // and the yards between the aggro edge and the pack's feet are the ones that reach
    // them.
    //
    // `forceTagOut` comes back true when even the edge is inside melee reach — a
    // much-higher-level tank against the core's 5yd minimum aggro, where closing can
    // never cross the threshold and the caller must swing to start the fight instead.
    //
    // The game-state read (GetAggroRange, both combat reaches, and WHICH event
    // `closingMs` is measured from — phase start for an ordinary pull, stand-spot
    // arrival for a scripted stage) stays in DungeonClearPullAction.
    float PullTagStopDistance(float aggroRange, float meleeReach,
                              std::uint32_t closingMs, std::uint32_t graceMs,
                              float creepYardsPerSec, float creepFloor,
                              bool& forceTagOut);

    // Threat-lead follower-release gate (pure). After the leader tank enters
    // combat a real group gives it a beat to gather and establish AoE threat
    // before DPS pile in; this holds a follower's fight assist for `leadMs` after
    // the leader's CURRENT combat began (`combatSinceMs`; 0 = leader not in
    // combat). Healers release immediately (`isHealer` — a withheld heal is a
    // wipe and heals don't rip threat the way DPS openers do). DPS release once
    // the lead has elapsed, with THREE bypasses: `alreadyInCombat` (this follower
    // is itself already flagged in combat — it can't over-aggro by moving into
    // sight and MUST be driven onto the tank's fight rather than left with nothing
    // to do, where reviving stock follow-master would drift it to the human), the
    // tank's HP below `panicHpPct` (it is LOSING the fight — pile in; <= 0 disables
    // the bypass), and `leadMs` == 0 (feature off). The game-state read (leader
    // combat stamp, healer role, tank HP, this bot's combat flag) stays in
    // DcLeaderSignal::IsLeaderFightAssistWanted.
    // Threat-lead DAMAGE hold (pure — gtested). True when a DPS follower must not
    // deal damage yet: the leader's fight is live (`combatSinceMs` != 0) and less
    // than `leadMs` has passed since the leader's FIRST HIT (`firstHitMs`). If the
    // leader has not landed a hit, the hold lasts at most `leadMs + noHitCapMs`
    // from the combat flag, so a tank that pulls with a taunt or is stunned on the
    // opener cannot freeze the party. Never holds an `exempt` bot (the tank), a
    // follower something is attacking (`beingAttacked` — self-defence), a tank
    // below `panicHpPct` (<= 0 disables that bypass), or with `leadMs` == 0.
    // ShouldReleaseFollower below gates DC's own assist MOVEMENT; this gates the
    // damage every action can do, stock ones included.
    bool ShouldHoldThreatLead(bool exempt, bool beingAttacked, std::uint32_t combatSinceMs,
                              std::uint32_t firstHitMs, std::uint32_t now, std::uint32_t leadMs,
                              std::uint32_t noHitCapMs, float tankHealthPct, float panicHpPct);

    bool ShouldReleaseFollower(bool isHealer, bool alreadyInCombat,
                               std::uint32_t combatSinceMs,
                               std::uint32_t now, std::uint32_t leadMs,
                               float tankHealthPct, float panicHpPct);

    // Phantom-combat classifier (pure). True when a bot is FLAGGED in combat but has
    // nothing it can actually fight from where it stands: nothing is meleeing it
    // (`hasAttacker`), it has no victim of its own (`hasVictim`), and its combat is
    // not explained by any LEGITIMATE holder (`hasLegitimateHolder`). The caller
    // computes the last from the bot's CombatManager PvE refs: a holder counts as
    // legitimate only if it is alive, non-evading, and PATH-REACHABLE from the bot,
    // and an opaque combat with NO unit references at all (a script-forced state) is
    // also treated as legitimate so it is never cleared. This is the signature of the
    // "a mob spawned across the map / behind a gate and tagged me" deadlock: the core
    // combat flag never drops because the holder is unreachable, and every DC gate
    // that keys off "someone is in combat" (the fight-assist arm, the party-engaged
    // latch) then spins forever. Because legitimacy keys on REACHABILITY rather than
    // distance, a fleeing/kiting party (pursuers always reachable) trips
    // `hasLegitimateHolder` and is never phantom — as does any real fight (which
    // trips hasAttacker/hasVictim). The game-state reads stay in
    // DungeonClearBreakStuckCombatTrigger.
    inline bool IsPhantomCombat(bool inCombat, bool hasAttacker, bool hasVictim,
                                bool hasLegitimateHolder)
    {
        return inCombat && !hasAttacker && !hasVictim && !hasLegitimateHolder;
    }

    // Is a holder that PASSED the legitimacy test actually prosecuting the fight?
    //
    // Reachability alone says the holder COULD come; it does not say it IS coming. In
    // an instance a creature never leashes (the dungeon short-circuit in
    // CanCreatureAttack), so a mob that tagged the party and then stopped keeps its
    // combat reference alive forever from wherever it stands — alive, non-evading,
    // path-reachable, allowed to attack, and completely inert. That reads as a REAL
    // fight to the legitimacy test, so the phantom hatch stands down, while every DC
    // gate that keys off "someone is in combat" spins. Live in tr-20260804-153254-2:
    // Ushkuk and Olanne held by a Sunblade Mage Guard 68-69yd back, 100% HP,
    // attackers=0 victim=-, for the eight minutes until the run was stopped by hand.
    //
    // A holder is prosecuting the fight if it is either already INSIDE engage range
    // (that is a fight whatever the numbers say) or CLOSING on us. `closing` comes
    // from the caller's DcProgressWatchdog::TickClosing over the nearest holder's
    // distance, so this cannot be fooled by the party moving: a chaser or a kited mob
    // improves the closest-ever distance and keeps the hatch inert, while a party that
    // walks away from a stationary holder only makes the distance WORSE. The first
    // sample arms the watchdog and counts as closing, so the streak clock in
    // ShouldBreakStuckCombat only starts once the holder has demonstrably stopped.
    //
    // `holderDist` is the distance to the nearest holder the bot can SEE (+inf when
    // none is in LOS), not to the nearest holder outright. "In engage range is a
    // fight" assumes the two sides can trade blows; a holder 14yd away on the floor
    // below cannot be targeted by any playerbots action (AttackersValue requires
    // LOS), so it is only a fight while it is CLOSING (tr-20261018-101829-1).
    inline bool IsHolderProsecutingFight(bool haveHolder, float holderDist,
                                         float engageRange, bool closing)
    {
        return haveHolder && (holderDist <= engageRange || closing);
    }

    // A holder the party can never TOUCH can never end the fight, so it is not a
    // fight — it is a flag with no way out. IsHolderProsecutingFight above asks
    // whether the holder is COMING; this asks the prior question of whether
    // resolving it is possible at all, and a NO here outranks any answer to that
    // one: a trigger standing on top of the party reads as toe-to-toe (dist 0.0)
    // and therefore "prosecuting" forever, which is exactly how it hides.
    //
    // TRIGGER creatures are the airtight case and the only one taken here.
    // CREATURE_FLAG_EXTRA_TRIGGER units are invisible script helpers: never a
    // pack, never loot, never selectable. No bot can target one, so nothing the
    // party does can make the reference go away — the fight has no end state.
    // Utgarde Pinnacle's Skadi gauntlet parks 75 of them (28351) down the hall; a
    // hunter PET tags one, it chases the party 80yd out of the hall (measured:
    // "79.7yd from its spawn"), and every member stays PvE-flagged for the rest
    // of the run. tr-20260902-121659-12 and -13 both died at the 600s no-progress
    // watchdog with the whole party standing on the tank at full health, teardown
    // reading `Flame Breath Trigger (Skadi) 0.0yd 100% reach LEGIT fighting
    // Scorpid` — a perfect legitimate-holder verdict about a unit that cannot be
    // fought. With the flag never dropping, the Engage-phase camp teardown (gated
    // on !IsInCombat) never ran, the stale camp kept anchoring the spread gate,
    // and Advance yielded 2314 and 2617 times respectively.
    //
    // NOT-SELECTABLE ALONE IS DELIBERATELY *NOT* THE TEST, and this is the half
    // worth not re-deriving. UNIT_FLAG_NOT_SELECTABLE is worn TEMPORARILY by real
    // encounters — boss_ymironAI::Reset() adds it and only SetData(DATA_SKADI,
    // DONE) takes it off — so keying on it would let the phantom hatch
    // force-clear combat in the middle of a legitimate boss fight, which is the
    // one outcome the breaker's raid stand-down exists to prevent. TRIGGER is a
    // permanent property of the creature template; selectable is encounter state.
    //
    // UNFLAGGED SCRIPT HELPERS are listed by entry instead. Each row is a creature
    // that is unselectable by template, never by encounter state, but carries no
    // TRIGGER flag — so the airtight test above misses it. Keep every row justified.
    //   17161 Blizzard (Shade of Aran): a STATIC spawn the Aran fight only switches
    //         on, so it outlives Aran and keeps flagging the raid. Teardown of
    //         tr-20261022-104155-559 read `Blizzard (Shade of Aran) 17161 7.5yd
    //         reach LEGIT` over three members for 199s; the leader-assist fallback
    //         walked the tank at them until the Netherspite leg stalled.
    inline bool IsUnflaggedScriptHelper(uint32 entry)
    {
        switch (entry)
        {
            case 17161:  // Blizzard (Shade of Aran), Karazhan
                return true;
            default:
                return false;
        }
    }

    // Leader-assist groupmate fallback, with hysteresis (DC_LEADER_ASSIST_COLOCATED_
    // RELEASE): yield inside `colocated`, and keep yielding out to `release` for the
    // member the rung last yielded beside. True = yield the tick to the ladder.
    inline bool LeaderAssistFallbackYields(float memberDist, bool latchedOnMember,
                                           float colocated, float release)
    {
        return memberDist <= colocated || (latchedOnMember && memberDist <= release);
    }

    inline bool IsUnresolvableCombatHolder(bool isCreature, bool isTrigger, uint32 entry = 0)
    {
        return isCreature && (isTrigger || IsUnflaggedScriptHelper(entry));
    }

    // "Can this follower attack from where it stands?" for the camp-assist handoff,
    // expressed in the SAME metric the stock reach action enforces.
    //
    // This exists because the two halves of that handoff used different metrics and
    // opened a dead band. Stock ReachSpellAction is constructed with
    // GetRange("spell") but tests it as `!IsWithinCombatRange(target, spellRange)`,
    // and Unit::IsWithinCombatRange adds GetCombatReach(bot) + GetCombatReach(target)
    // to the threshold — so stock stops closing at spellRange + combatReachSum. DC
    // previously required `dist <= spellRange - CONTACT_DISTANCE`, roughly
    // combatReachSum + 0.5 yards TIGHTER than stock would ever walk. A ranged bot
    // left in that window is simultaneously "in range, stop moving" (stock) and "out
    // of range, yield to stock" (DC): neither side acts, the bot never engages, and
    // the tank's spread gate waits on it until the run deadlocks.
    //
    // Ranged therefore mirrors IsWithinCombatRange exactly, making DC's engage window
    // the precise complement of stock's keep-closing window — no gap by construction,
    // and self-syncing if SpellDistance is retuned. Melee passes its own
    // reach-inclusive threshold (reachSum + 1.0), which is already WIDER than stock
    // reach-melee's stop point (reachSum + MeleeDistance, default 0.75) and so
    // overlaps rather than gaps — which is why only ranged ever hung.
    inline bool IsWithinAssistAttackRange(bool isMelee, float dist, float meleeRange,
                                          float spellRange, float combatReachSum)
    {
        return isMelee ? dist <= meleeRange : dist <= spellRange + combatReachSum;
    }

    // Stuck-combat fire gate (pure, streak clock by reference). Given whether the bot
    // is CURRENTLY in phantom combat (IsPhantomCombat above), arm/hold/reset a streak
    // clock and report when the phantom state has persisted continuously for
    // `timeoutMs` — the point at which the caller force-clears combat. `sinceMs` is
    // the caller-owned latch (0 = not streaking): armed to `now` on the first phantom
    // tick, cleared to 0 the instant the phantom state breaks (a real target
    // reappeared), so a one-tick target gap can never trip it. The timeout is
    // deliberately LONG (default 15s) so an ENCOUNTER that intentionally holds the
    // party in combat with no reachable enemy (a scripted wave gap, a gauntlet) is
    // never mistaken for a stuck flag. `timeoutMs == 0` disables the gate. Mirrors
    // ShouldWaitForPatrol's by-reference-latch contract.
    inline bool ShouldBreakStuckCombat(bool phantom, std::uint32_t now,
                                       std::uint32_t timeoutMs, std::uint32_t& sinceMs)
    {
        if (!phantom || timeoutMs == 0)
        {
            sinceMs = 0;
            return false;
        }
        if (sinceMs == 0)
            sinceMs = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
        // `now >= sinceMs` guards the unsigned subtraction: false only on the ms-0
        // arming tick (sinceMs nudged to 1) or a backward clock step / getMSTime wrap,
        // where "no time has elapsed yet" is the right answer.
        return now >= sinceMs && (now - sinceMs) >= timeoutMs;
    }

    // Flagged-in-combat driving gate (pure, streak clock by reference).
    //
    // THE DISTINCTION: `flagged` is the core combat FLAG (Unit::IsInCombat);
    // `engaged` is whether anyone in the party is ACTUALLY FIGHTING — a victim, or
    // something meleeing/casting at us. The DC driving ladder used to stand down on
    // `flagged` alone, which is right for a real fight and catastrophic for a flag
    // with no fight behind it.
    //
    // Why that state exists at all (Arcatraz heroic, 2026-07-29): the Eredar
    // Soul-Eaters' Entropic Aura (36784) is a hostile area aura with a **45yd**
    // radius against a ~20yd creature aggro radius. That leaves a 25-yard ANNULUS
    // where the party is flagged in combat but nothing has aggroed it — no
    // attacker, no victim, no threat. Playerbots does not flip to the combat engine
    // on the flag (only AttackAction / PullMyTargetAction call ChangeEngine, and
    // both need a chosen target; UpdateAIInternal explicitly tolerates
    // non-combat-engine-while-flagged). So the combat ladder never runs either, and
    // with the driving ladder standing down on the flag the run freezes SILENTLY —
    // and self-locks, because the freeze happens before the party ever reaches
    // aggro range, so the fight it is waiting for can never start. Measured: five
    // frozen tanks at 20.8-34.8yd from the pack; the one that got inside 20yd
    // fought and died instead.
    //
    // The grace is the safety margin in the other direction: a real fight can have
    // a one-tick hole (the target dies and nothing has re-acquired yet), and
    // resuming the drive there would walk the tank out of a live fight. The
    // no-engagement state must therefore persist continuously for `graceMs` before
    // driving resumes. `sinceMs` is the caller-owned latch (0 = not streaking),
    // cleared the instant a real engagement reappears. `graceMs == 0` disables the
    // grace (drive as soon as the flag has nothing behind it).
    //
    // Returns TRUE when the driving ladder may run.
    inline bool MayDriveWhileFlagged(bool flagged, bool engaged, std::uint32_t now,
                                     std::uint32_t graceMs, std::uint32_t& sinceMs)
    {
        if (!flagged)
        {
            sinceMs = 0;
            return true;            // not in combat at all — the ordinary case
        }
        if (engaged)
        {
            sinceMs = 0;
            return false;           // a real fight owns the bot; stay out of it
        }
        if (graceMs == 0)
            return true;
        if (sinceMs == 0)
            sinceMs = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
        // `now >= sinceMs` guards the unsigned subtraction against a backward clock
        // step / getMSTime wrap, where "no time has elapsed yet" is the right answer.
        return now >= sinceMs && (now - sinceMs) >= graceMs;
    }

    // The conditional-event rung's combat gate (pure).
    //
    // MayDriveWhileFlagged is right for an event whose work happens BETWEEN
    // pulls and exactly wrong for one flagged DungeonEvent::drivesInCombat,
    // whose whole premise is that the driver keeps steering while the party
    // fights. That exemption used to live only in the COMBAT-engine copy of the
    // rung, which requires the bot to be running the combat ENGINE — a different
    // fact from being combat-FLAGGED, and the gap between them is a hole both
    // rungs fall through:
    //
    //   Playerbots never switches engines on Unit::IsInCombat (PlayerbotAI.cpp
    //   only clears stale targets there); the sole way into BOT_STATE_COMBAT is
    //   AttackAction::Attack. So a bot that is FLAGGED but holds an empty
    //   attacker set sits in the NON-combat engine — where this gate shuts it
    //   down — while the combat rung that would have exempted it is in an engine
    //   that is not running. The driver then gets no ticks at all.
    //
    // Halls of Reflection, tr-20260908-215109-1, wall 4 of the escape: the Lich
    // King flags the party every second, the twelve summons took the dps and the
    // healer, the tank's attacker set emptied, and HorDriveEscape was not called
    // once for thirty seconds. The tank stood beside the leader on a stale
    // spline while the ranged tanked the batch; recovery was accidental, since
    // TankAssistTrigger needs attacker count > 0 and had to wait for a summon to
    // wander into melee. Sibling tr-20260908-215112-9 held the same window open
    // for fifty-five seconds and wiped inside it.
    //
    // `sinceMs` is deliberately UNTOUCHED on the exempt path: the latch is shared
    // with every other MayDrive-gated rung, so zeroing it here would restart a
    // grace window one of them is already counting.
    //
    // Returns TRUE when the conditional-event rung may own this tick.
    inline bool EventDueGateOpen(bool drivesInCombat, bool flagged, bool engaged,
                                 std::uint32_t now, std::uint32_t graceMs,
                                 std::uint32_t& sinceMs)
    {
        if (drivesInCombat)
            return true;
        return MayDriveWhileFlagged(flagged, engaged, now, graceMs, sinceMs);
    }

    // Loot-roll rung starvation bound (pure, by-reference latch).
    //
    // The rung that answers an open loot roll sits at relevance 95, above the
    // whole driving ladder, and only one action runs per tick — so while it
    // fires and its action reports success, nothing else drives the bot. Safe
    // only while "fires" and "the vote lands" mean the same thing; when they
    // came apart the run froze for ten minutes with every watchdog reading zero
    // (tr-20260831-123946-18). The shared votable-roll predicate closes that
    // case; this bounds the CLASS.
    //
    // `signature` digests the rolls the bot could vote on this tick and
    // `votable` counts them (counted, not inferred from a zero digest). The
    // action clears its whole backlog in ONE Execute, so a healthy firing needs
    // a single tick and the next tick sees an empty set. Consecutive ticks on an
    // UNCHANGED set therefore mean the votes are not landing — yield the tick.
    // Any real change (a roll resolved, a new drop) re-arms immediately, which
    // is what keeps the latch from suppressing legitimate rolling.
    //
    // Returns TRUE while the rung may fire. After a call, `unchangedTicks ==
    // maxUnchangedTicks + 1` marks the single tick the bound first bit, for a
    // one-shot log.
    inline bool LootRollRungMayFire(std::uint32_t votable, std::uint64_t signature,
                                    std::uint32_t maxUnchangedTicks,
                                    std::uint64_t& lastSignature,
                                    std::uint32_t& unchangedTicks)
    {
        if (!votable)
        {
            lastSignature = 0;
            unchangedTicks = 0;
            return false;       // nothing pending — full budget for the next window
        }
        if (signature != lastSignature)
        {
            lastSignature = signature;
            unchangedTicks = 0;
        }
        return ++unchangedTicks <= maxUnchangedTicks;
    }

    // Bystander-detour borrow watchdog (pure, by-reference latch).
    //
    // Above commit range the tank's approach belongs to Advance (the long-path
    // glide). When a bystander pack's aggro sphere actually sits on the line to
    // the pull target, the pull BORROWS the tick and walks an orbit around it
    // instead — but while it holds the tick, Advance's own wedge/stall ladder is
    // not running, so the borrow must be bounded or a non-converging orbit would
    // freeze the run outright. Bounding it in time is the same "preference, never
    // refusal" rule the detour itself follows: when the borrow stops paying, hand
    // the walk back and let the straight route happen.
    //
    // It is a NO-PROGRESS clock, not a plain deadline: `bestDist` records the
    // closest the tank has been to the pack on this detour, and any tick that
    // beats it by `progressEpsilon` restamps the clock. A long legitimate arc
    // around a big sphere therefore never expires — only an orbit that has
    // genuinely stopped closing burns the budget.
    //
    // `sinceMs == 0` means "not currently borrowing" and arms the clock.
    // `timeoutMs == 0` disables the give-up entirely.
    //
    // The caller owns the latch either side of this: resetting it when the leg's
    // target changes, and deciding what a `false` means. The pull makes it
    // one-shot per pack (DcPullContext::avoidGaveUp) rather than simply retrying
    // once the clock would re-arm — the tank closing one more yard under Advance
    // is enough to satisfy the progress test, so a retrying caller would cancel
    // Advance's spline every fraction of a second and thrash the movement it was
    // supposed to be handing back.
    inline bool ShouldKeepAvoidDetour(std::uint32_t now, float curDist,
                                      std::uint32_t timeoutMs, float progressEpsilon,
                                      std::uint32_t& sinceMs, float& bestDist)
    {
        if (sinceMs == 0 || curDist < bestDist - progressEpsilon)
        {
            sinceMs = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
            bestDist = curDist;
            return true;
        }
        if (timeoutMs == 0)
            return true;
        // `now >= sinceMs` guards the unsigned subtraction against a backward clock
        // step / getMSTime wrap, where "no time has elapsed" is the right answer.
        return !(now >= sinceMs && (now - sinceMs) >= timeoutMs);
    }

    // --- chase leash -------------------------------------------------------
    // What the walk toward a MOVING trash target should do this tick.
    enum class ChaseVerdict : std::uint32_t
    {
        Follow = 0,   // walk in as planned
        Hold   = 1,   // stand still; let the mob come back to us
        GiveUp = 2,   // the hold ran out — the caller decides what that means
    };

    // Chase-leash gate (pure, by-reference latch).
    //
    // A pull target is latched by GUID and its position is re-read live every
    // tick, so a target that WALKS — a DB patrol, a wanderer, a mob repositioned
    // by its own script — turns every approach into a pursuit. The tank follows it
    // wherever it goes, and when the route it walks passes behind other packs the
    // tank walks through those packs' aggro arcs and arrives at the camp with the
    // whole room. That is a pursuit nobody chose: the pull was planned against the
    // pack's position AT SELECTION TIME (that is what sized the estimate and where
    // the camp was measured from), and the moment the pack leaves that spot the
    // plan is about ground the mob is no longer standing on.
    //
    // A human tank does not chase a patrol. It waits at the spot it picked, and
    // the patrol — by definition a loop — comes back. This is that rule:
    //
    //   `driftFromAnchor` : how far the target has moved from where it stood when
    //                       we committed to it (2D).
    //   `gapAtAnchor`     : distance from the tank's commit spot to that anchor.
    //   `gapNow`          : distance from the SAME commit spot to the target now.
    //                       Measured from the fixed origin, not the tank's live
    //                       position — from a moving origin the gap always shrinks
    //                       as we walk and the leash could never engage.
    //   `destinationHot`  : the target is currently standing inside ANOTHER pack's
    //                       aggro sphere. Reaching it means waking that pack no
    //                       matter how the route bends, so it is never walkable
    //                       ground however small the drift is.
    //
    // Follow while the target is still near where we picked it (`drift <=
    // leashYards`) OR while it has come at least as close to our commit spot as it
    // was then (`gapNow <= gapAtAnchor` — an inbound patrol rounding a wide loop
    // is exactly what we are waiting for, and must not be held on drift alone).
    // Otherwise the target is RECEDING or standing somewhere we must not follow it
    // to: arm the hold latch and stand still until it comes back, and report
    // GiveUp once the hold has burned `holdMs` so no patrol can stall a run.
    //
    // `leashYards` <= 0 disables the gate outright (Follow, latch cleared) —
    // the historical always-chase behaviour, expressible from config.
    // `holdMs` == 0 gives up on the first receding tick (never holds).
    // Latch/clear contract mirrors ShouldWaitForPatrol: the latch stays armed past
    // the timeout, so GiveUp repeats until the caller re-anchors or drops the
    // target rather than silently re-entering a fresh hold.
    inline ChaseVerdict DecideChase(float driftFromAnchor, float gapAtAnchor,
                                    float gapNow, bool destinationHot,
                                    float leashYards, std::uint32_t now,
                                    std::uint32_t holdMs, std::uint32_t& holdSince)
    {
        if (leashYards <= 0.0f)
        {
            holdSince = 0;
            return ChaseVerdict::Follow;
        }
        if (!destinationHot &&
            (driftFromAnchor <= leashYards || gapNow <= gapAtAnchor))
        {
            holdSince = 0;
            return ChaseVerdict::Follow;
        }
        if (holdMs == 0)
            return ChaseVerdict::GiveUp;
        if (holdSince == 0)
            holdSince = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
        // `now >= holdSince` guards the unsigned subtraction against a backward
        // clock step / getMSTime wrap, where "no time has elapsed" is right.
        if (now >= holdSince && (now - holdSince) >= holdMs)
            return ChaseVerdict::GiveUp;
        return ChaseVerdict::Hold;
    }

    // --- room-clear give-up clock ------------------------------------------
    // State for the room-trash value's no-progress valve (RoomClearTimeout).
    struct RoomClearClock
    {
        std::uint32_t lastRemaining = 0;  // count at the last progress (0 = none yet)
        std::uint32_t idleMs = 0;         // observed ready time since that progress
        std::uint32_t lastTickMs = 0;     // when the clock last ran (0 = never)
    };

    // The longest gap between two evaluations that still counts as observed. The
    // value polls every 500ms while it is read; a longer gap means nobody read it —
    // the at-boss gate stops reaching it while the party rests or loots — and
    // time nobody watched must not be charged as "stalled".
    inline constexpr std::uint32_t DC_ROOM_CLEAR_MAX_OBSERVED_GAP_MS = 2000;

    // One tick of the no-progress valve (pure). True once the room-clear should be
    // given up on: `idleMs` of READY time has passed with the remaining count never
    // dropping.
    //
    // Only time the party could have been pulling counts. A `busy` tick (in combat,
    // resting, looting, or not yet set) and an unobserved gap pause the clock
    // instead of re-arming it, so a room whose packs each need a fight plus a
    // full-mana rest (Karazhan's Banquet Hall: eight to nine elites per formation)
    // does not read as stalled between formations, while a respawn churn that never
    // lowers the count still adds up its ready gaps and gives up.
    //
    // Outside the room (`inRoom` false) the clock is held re-armed: the travel leg
    // is not a stall. Progress (a smaller count, or the first sighting) re-arms it.
    inline bool RoomClearGiveUpDue(RoomClearClock& c, std::uint32_t remaining, bool inRoom,
                                   bool busy, std::uint32_t now, std::uint32_t timeoutMs)
    {
        std::uint32_t const prevTick = c.lastTickMs;
        c.lastTickMs = now ? now : 1;

        if (remaining == 0)
        {
            c.lastRemaining = 0;
            c.idleMs = 0;
            return false;
        }
        if (!inRoom || c.lastRemaining == 0 || remaining < c.lastRemaining)
        {
            c.lastRemaining = remaining;
            c.idleMs = 0;
            return false;
        }
        // A patroller walking into the radius raises the count. Track the new high
        // so that killing it reads as progress, but leave the idle time alone.
        if (remaining > c.lastRemaining)
            c.lastRemaining = remaining;

        // `now >= prevTick` guards the unsigned subtraction against a clock wrap.
        std::uint32_t const elapsed = (prevTick && now >= prevTick) ? now - prevTick : 0;
        if (!busy && elapsed <= DC_ROOM_CLEAR_MAX_OBSERVED_GAP_MS)
            c.idleMs += elapsed;

        return timeoutMs && c.idleMs > timeoutMs;
    }

    // Engage-fizzle handoff latch (pure). An advanced-pull "camp fight" ended with
    // the tank out of combat but the pulled pack still ALIVE and IDLE — the drag
    // fizzled (a planted caster evaded home the moment the tank broke LOS at camp).
    // Re-pulling repeats the exact same fizzle, so after `maxFizzles` consecutive
    // fizzles of the same pack the caller hands it to the normal walk-in engage
    // instead. `pulledAliveIdle` is the caller's verdict that the pull target
    // resolved and is alive & out of combat (a fizzle this tick). `sameTarget` is
    // true when this fizzle is of the SAME pack the latch already holds (compared
    // against the OLD latched target, before the caller re-stamps it). On a fizzle
    // of a new pack the count restarts at 1; a pack that died or is still being
    // fought (`pulledAliveIdle == false`) clears the count to 0. `fizzleCount` is
    // the by-reference latch (mirrors ShouldWaitForPatrol's contract) and is left
    // holding the updated consecutive-fizzle count for the caller's diagnostics.
    // Returns true when the pack should be handed off (count reached `maxFizzles`).
    // The game-state read (guid identity, alive/combat) and the fizzleTarget guid
    // bookkeeping stay in DungeonClearPullAction's Engage-cleanup branch.
    bool ShouldHandoffFizzledPull(bool pulledAliveIdle, bool sameTarget,
                                  std::uint32_t maxFizzles, std::uint32_t& fizzleCount);

    // Squared 2D distance from point P to segment (A,B).
    float DistSqToSegment2D(float px, float py,
                            float ax, float ay,
                            float bx, float by);

    // The point on polyline `route` whose XY projection is nearest (px,py), with
    // Z linearly interpolated along the winning segment. False (and `out`
    // untouched) when the polyline holds fewer than two points.
    //
    // 2D on purpose, unlike PathProgressCursor below: the caller is asking
    // "where on this walk do I come closest to that mob", and a mob standing in
    // a side room off a corridor is on the corridor's floor by construction —
    // the consumer (the en-route sweep in DcTargeting) has already restricted
    // candidates to the bot's own Z level via BystanderSpheres, so the storey
    // ambiguity a 3D cursor exists to resolve cannot arise here. Interpolating Z
    // rather than snapping to a vertex matters for the line-of-sight ray the
    // caller then shoots from `out`: a ramp leg's endpoints can sit several
    // yards above and below the point actually nearest the mob.
    bool NearestPointOnPolyline2D(std::vector<G3D::Vector3> const& route,
                                  float px, float py, G3D::Vector3& out);

    // True if the 2D segment (A,B) intersects the axis-aligned box
    // [minX,maxX] x [minY,maxY]. Liang-Barsky slab clip: returns true even
    // when BOTH endpoints lie outside the box but the segment passes through
    // it (the case that matters for a thin door panel a path step straddles).
    bool SegmentIntersectsAABB2D(float ax, float ay, float bx, float by,
                                 float minX, float minY,
                                 float maxX, float maxY);

    // Index of the route-polyline vertex the bot has walked up to — its PROGRESS
    // CURSOR along that route. Measured in 3D on purpose.
    //
    // A 2D-nearest pick reads the wrong FLOOR wherever a dungeon stacks rooms
    // over one another. Shadowfang Keep's tower is the worst case: the navmesh
    // column above the Fenrus room carries surfaces at Z = 88, 94, 96, 102, 105,
    // 114, 129 (the floor the tank is standing on), 139, 156 and 168, because the
    // staircase up to Wolf Master Nandos climbs almost directly overhead. The
    // route vertex on the landing 10yd UP sat 1.81yd from the tank in 2D while
    // the tank's own floor vertex sat 1.98yd away — so a 2D cursor snapped a
    // storey up, and both door consumers then read the corridor as if the tank
    // were already at the top of the stairs: the blocking-door scan started from
    // up there (synthesising a bot -> overhead bee-line straight through the
    // ceiling) and flagged Arugal's Lair 27yd ABOVE the tank, while
    // DistAlongPathToClosedDoor called that door "8.0yd along path" when the real
    // walk was the whole staircase. The tank parked at the foot of the stairs on
    // a door it was nowhere near and — Arugal's Lair being script-only since the
    // voidwalker fix — auto-paused the run instead of walking up to kill Nandos,
    // whose death is the only thing that ever opens it (runs
    // tr-20260802-101606-12 / -16 / -17).
    //
    // Returns 0 for an empty route; callers guard emptiness themselves.
    std::size_t PathProgressCursor(std::vector<G3D::Vector3> const& route,
                                   float botX, float botY, float botZ);

    // True when the straight leg from (px,py,pz) to route[cursor] may be treated
    // as CORRIDOR rather than as a bee-line through geometry.
    //
    // Every consumer that scans "the route ahead of the bot" has to chain from
    // the bot's own position to the cursor vertex, because the cached route was
    // built from wherever the last rebuild happened and does not start under the
    // bot's feet. That joining leg is SYNTHESISED — nothing pathfound it — so it
    // is only route-like while the bot is actually standing on the route. Once
    // the bot is far off it, the leg is a straight line through whatever the map
    // happens to contain, and a consumer that reads geometry off it reads the
    // wrong rooms.
    //
    // The bound is DungeonPathFollower::RESNAP_RADIUS by construction, which is
    // the module's existing answer to the same question: how far may a bot be
    // from a route vertex and still be considered ON that route (Resnap will
    // re-anchor its cursor there) rather than needing a rebuild from where it
    // stands. Callers pass it in so this stays a leaf header, and add the LOS
    // half of Resnap's own gate themselves — see DungeonClearBlockingDoorValue,
    // where the distance half alone would still have let a folded-back dungeon
    // synthesise a joining leg straight through a mountain.
    //
    // False for an empty route or an out-of-range cursor: with no vertex to join
    // to there is no corridor to read.
    bool PathCursorIsJoinable(std::vector<G3D::Vector3> const& route,
                              std::size_t cursor,
                              float px, float py, float pz, float maxGap);

    // Travel still REMAINING from the bot to where `route` first enters the
    // door band of (doorX,doorY,doorZ), or FLT_MAX when the route never does
    // within `maxLookAhead` of the bot's progress cursor, or does so only well
    // behind that cursor (more than `behindSlack` — a door already walked
    // past). The pure body of
    // DcEngageGeometry::DistAlongPathToClosedDoor; the door-blocked walk-in parks
    // when this drops to DC_DOOR_STOP_DISTANCE.
    //
    // Remaining travel = the JOINING LEG (bot -> its progress-cursor vertex, 3D)
    // + the along-route gap from that vertex to the band entry. The joining leg
    // used to be left out, which measured from the route vertex nearest the bot
    // as if the bot stood on it. Karazhan, tr-20260924-130027-4: engage-trash
    // pulled the tank 28yd off its route beside the Strange Bookcase; the vertex
    // nearest it still sat 7.4yd short of the doorway, so the walk-in read "at
    // door" and parked 34yd from the door, outside click range. The blocked-door
    // watchdog (which trusts the "at door" read) then auto-paused the run five
    // seconds later without a single click. The blocking-door value, which
    // flags the door in the first place, already chains the same bot -> cursor
    // leg into its own look-ahead; this keeps the two measures consistent.
    float DoorTravelRemaining(std::vector<G3D::Vector3> const& route,
                              float botX, float botY, float botZ,
                              float doorX, float doorY, float doorZ,
                              float band, float zBand,
                              float maxLookAhead, float behindSlack);

    // Index of the LATEST crumb within `rejoinRadius` (3D) of `cur`, or
    // TrailRejoinNone if none qualifies. Used by the breadcrumb recorder: on a
    // >kJump discontinuity (a drag-back / drop-down), rather than wiping the
    // whole trail, rejoin at the most recent crumb near where the bot now stands
    // and truncate everything ahead of it. Latest-wins so a trail that loops near
    // itself rejoins at the most recent pass, keeping the walked-distance
    // semantics the camp walk-back relies on intact. 3D on purpose — a crumb
    // directly above/below (different floor) must not count as a rejoin.
    std::size_t FindTrailRejoin(std::vector<Position> const& crumbs,
                                Position const& cur, float rejoinRadius);

    // The one shared breadcrumb-trail walk-back primitive (Kernel A). One step of
    // a backward walk, produced by WalkTrailBack for each crumb newest -> oldest.
    // `index` is the crumb's index in the source vector. `along` is the
    // accumulated 3D walked distance from the anchor to `crumb` (the far, away-
    // from-anchor end of this segment); `alongPrev` is that distance at the near
    // end `segStart`. `PointAt(d)` returns the point exactly `d` yards back along
    // THIS segment (linear interpolation, `d` clamped to [alongPrev, along]) — the
    // ce2e89e scout-lag interpolation fix, now available to every walk-back caller
    // instead of one. Positions carry Z, so the walk's jump guard (see
    // WalkTrailBack) and any distance test the caller runs off these fields catch a
    // vertical seam a 2D measure would miss (the seam-undermap / trail-dance bug
    // class: "the arrived? test and the target must share a metric").
    struct TrailStep
    {
        std::size_t index;
        float       alongPrev;
        float       along;
        Position    segStart;   // near end (toward the anchor)
        Position    crumb;      // this crumb (far end, away from the anchor)

        Position PointAt(float d) const
        {
            float const span = along - alongPrev;   // == this segment's 3D length
            if (span <= 0.0001f)
                return crumb;
            float t = (d - alongPrev) / span;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
            return Position(
                segStart.GetPositionX() + (crumb.GetPositionX() - segStart.GetPositionX()) * t,
                segStart.GetPositionY() + (crumb.GetPositionY() - segStart.GetPositionY()) * t,
                segStart.GetPositionZ() + (crumb.GetPositionZ() - segStart.GetPositionZ()) * t);
        }
    };

    // Walk `crumbs` newest -> oldest starting from `anchor`, accumulating 3D
    // segment length, and call `visit(step)` for each crumb. Stop when a segment
    // exceeds `jumpGuard`: a gap that large is a drag / teleport / drop-down seam,
    // and nothing beyond it is contiguously "behind" the anchor (measuring the
    // segment in 3D makes the guard catch a vertical drop a 2D measure treats as
    // contiguous — the "camp/trail lands on the wrong floor" bug). `visit` returns
    // false to stop the walk early (e.g. the caller accepted a point). Returns the
    // total distance walked (the last visited step's `along`, or 0 for an empty /
    // immediately-discontinuous trail). This is the single primitive behind
    // ComputeSafeCamp, ComputeTrailCamp, GetLeaderScoutTrailPoint and
    // GetLeaderScoutTrail; each supplies its own acceptance predicate (which may
    // probe reachability off the map) as `visit`, so the accounting, the jump
    // guard, and the interpolation live in exactly one tested place.
    template <class Visit>
    inline float WalkTrailBack(std::vector<Position> const& crumbs, Position const& anchor,
                               float jumpGuard, Visit&& visit)
    {
        Position prev = anchor;
        float along = 0.0f;
        for (std::size_t i = crumbs.size(); i-- > 0; )
        {
            Position const& c = crumbs[i];
            float const seg = prev.GetExactDist(&c);
            Position const segStart = prev;
            prev = c;
            if (seg > jumpGuard)
                break;
            float const alongPrev = along;
            along += seg;
            TrailStep const step{ i, alongPrev, along, segStart, c };
            if (!visit(step))
                break;
        }
        return along;
    }

    // The shared jump-guard threshold for WalkTrailBack: a 3D segment longer than
    // this is a trail discontinuity. Was re-declared as a local `kJumpGuard = 12`
    // in each of the four walk-back clones; hoisted here so the four agree by
    // construction.
    inline constexpr float TrailJumpGuard = 12.0f;

    // ---- off-line rejoin rung (DungeonClearAdvanceAction) -------------------

    // Is the bot off the corridor, WITH HYSTERESIS? The rejoin rung engages at
    // `engageBar` (OFF_PATH_THRESHOLD) and, once it owns the bot, holds until the
    // deviation falls under the lower `releaseBar`.
    //
    // A single bar released exactly where it engaged, so a bot parked in the band
    // around it alternated rejoin / escort-spline / rejoin tick by tick. That is
    // not cosmetic: the spline's opening leg is a straight line from the bot to
    // its cursor, harmless on the line and a chord across the inside of a bend off
    // it. Live (tr-20260830-115416-5, BWL) a tank sat at 4.1-6.9yd across the
    // anchor 16->18 hairpin and the sub-bar spline legs walked it into a navmesh
    // void for five seconds. Same shape as every other bare threshold this module
    // has had to give hysteresis (the transit pack leash, the moving-camp rung).
    //
    // `vertOff` (a corridor Z-band mismatch) is NOT hysteretic: a floor mismatch
    // is a binary fact about which storey the bot is on, not a drift to damp.
    inline bool IsOffLineWithHysteresis(float deviation, bool latched, bool vertOff,
                                        float engageBar, float releaseBar)
    {
        return deviation > (latched ? releaseBar : engageBar) || vertOff;
    }

    struct RejoinRefusalVerdict
    {
        bool  haltStaleMove = false;  // cancel the in-flight move; re-issue next tick
        float bestDeviation = 0.0f;   // baseline to carry into the next tick
    };

    // The rejoin rung issued no move this tick because stock MoveTo refused it.
    // Is the move already in flight doing this rung's job, or undoing it?
    //
    // MoveTo refuses whenever ANY move is queued, and DcMoveTo's
    // ResolveEscortConflict clears only an ESCORT generator — so on DC's own
    // per-point move (gen=POINT, the norm in this rung) a refusal used to hand the
    // tick back to whatever was moving the bot, unexamined. On tr-20260830-115416-5
    // that was the move carrying the tank OFF the line: 48 of 53 rejoins refused,
    // deviation growing 6.2 -> 31.1yd across 22 consecutive refused ticks.
    //
    // Judge it on NET PROGRESS, the yardstick the recovery ladder one rung up
    // already uses: track the best (smallest) deviation seen since the rung took
    // ownership and halt only once the current deviation has grown past it by
    // `slack`. Riding a working re-entry matters as much as halting a bad one —
    // cancelling on every refusal turns the healthy case into a stop/re-issue
    // stutter. `slack` absorbs a pathed re-entry legitimately swinging wide to
    // round a corner.
    //
    // Seeding `bestDeviation` with FLT_MAX yields exactly one tick of grace: the
    // first refusal establishes the baseline and never halts.
    inline RejoinRefusalVerdict DecideRejoinRefusal(float deviation, float bestDeviation,
                                                    float slack)
    {
        RejoinRefusalVerdict v;
        float const best = std::min(bestDeviation, deviation);
        v.haltStaleMove  = deviation > best + slack;
        v.bestDeviation  = v.haltStaleMove ? deviation : best;
        return v;
    }

    // The rejoin rung's progress yardstick: plan-view deviation plus any height
    // gap to the hop beyond the one-storey tolerance. Route deviation alone is 2D,
    // so on a spiral staircase a re-entry that walks the bot DOWN the helix under
    // its route point reads as converging — 3.2 -> 0.0yd while the tank sank 33yd
    // into Karazhan's Servants' Quarters stairwell below the Maiden route
    // (tr-20260927-101342-8). Counting the excess height makes that descent a
    // growing gap, which DecideRejoinRefusal halts. Within the tolerance (a ramp,
    // a stair flight) it is exactly the 2D deviation, as before.
    inline float RejoinGap(float deviation2d, float dz, float levelTolerance)
    {
        float const excess = std::max(0.0f, std::fabs(dz) - levelTolerance);
        return std::sqrt(deviation2d * deviation2d + excess * excess);
    }

    // Whether a stalled rejoin should rebuild the route from where the bot stands.
    // A bot that fell a storey off its route (Karazhan's Opera balcony onto the
    // audience floor, chasing a floor mob mid-fight, tr-20260927-190943-12) sits
    // under a hop 14yd overhead: 2.3yd in plan view, so the chunked re-entry's
    // distance gate never opens, and every MoveTo to the hop is refused because no
    // route arrives on its floor. A route built from the bot's position reaches the
    // objective by the level it is actually on. Once per spell without progress, so
    // a re-path that lands the bot off-level again falls through to the strike ladder.
    inline bool ShouldRepathOffLevel(float dz, float levelTolerance, bool alreadySpent)
    {
        return !alreadySpent && std::fabs(dz) > levelTolerance;
    }

    // Verdict of a stock path probe to a VERTICALLY SEPARATED boss: is the bot in
    // the boss's room / on its floor? `routed` = the probe returned NORMAL or
    // INCOMPLETE (stock had an opinion), `normal` = NORMAL, `endOnLevel` = the
    // route ends on the boss's level, `los` = the bot can see the boss.
    //
    // A route that exists answers the question by its length, as before. NO route
    // at all (PATHFIND_NOPATH) answers nothing: the probe cannot tell "a floor up"
    // from "on a dais the mesh doesn't join", so the question falls back to sight.
    // A boss a storey away is behind a floor slab; one on a throne is in plain view.
    // Treating NOPATH as "a floor away" parked tr-20261018-101829-3 5.1yd under
    // Emperor Thaurissan's throne for 7 minutes: not at the boss, not in his room
    // (so no room clear and no pre-clear hold), and Advance pursued a destination
    // stock refused every tick.
    inline bool ProbeSaysSameFloor(bool routed, bool normal, bool endOnLevel,
                                   float pathLength, float range, bool los)
    {
        if (!routed)
            return los;
        return normal && endOnLevel && pathLength <= range;
    }

    struct RejoinProgressVerdict
    {
        bool  progressed = false;  // the gap set a new best: re-entry is working
        bool  idle       = false;  // no new best this tick: counts toward the ladder
        float best       = 0.0f;   // baseline to carry into the next tick
    };

    // The rejoin rung's LIVENESS test: is the gap back to the route actually
    // closing? It used to count ticks whose MoveTo was refused, but on its own
    // re-issued point stock refuses as a duplicate for a flat MaxWaitForMove (5s)
    // — while the first move walks the bot in, and after loot or a drift halt
    // stopped it short. Both read as "issued nothing", so the ladder struck out
    // in ~10s on a flat floor with the route 6yd away (tp-20260927-114025-1, all
    // 10 "Stuck off the route" stalls). Progress is the question the ladder asks.
    //
    // `best` only moves on progress, so slow steady closing accumulates until it
    // clears `eps`. FLT_MAX (a fresh episode) makes this tick the baseline and
    // neither progress nor idle — an off-path rebuild that restarts the episode
    // every few ticks cannot reset the ladder with a free "progress" tick.
    inline RejoinProgressVerdict TrackRejoinProgress(float gap, float best, float eps)
    {
        RejoinProgressVerdict v;
        if (best == std::numeric_limits<float>::max())
        {
            v.best = gap;
            return v;
        }
        v.progressed = gap < best - eps;
        v.idle       = !v.progressed;
        v.best       = v.progressed ? gap : best;
        return v;
    }

    // May a follower's combat regroup anchor on this combat holder? Only one it can
    // actually fight: inside the engagement radius every other combat read already
    // bounds by, and reachable on its level. A holder that tagged the party and was
    // then stranded on another floor keeps the tank combat-flagged indefinitely
    // (instanced creatures never leash); anchoring on it walked Karazhan's whole
    // raid 140yd back from the Opera balcony to the stage corridor, eight times, for
    // nine minutes (tr-20260927-103044-10). Nothing qualifying -> the regroup falls
    // back to the tank.
    inline bool IsRegroupAnchorCandidate(float dist, float engagementRadius,
                                         bool levelReachable)
    {
        return dist <= engagementRadius && levelReachable;
    }
    // --- Room-clear straight pull ---------------------------------------------
    //
    // A disc the pull lane must stay clear of: the room-aggro boss at its skirt,
    // or another pack at the camp clearance.
    struct LaneKeepAway
    {
        float x;
        float y;
        float radius;
    };

    // One candidate lane for a room-clear pull: the tank tags the pack from
    // (standX, standY) and drags it straight back along the same bearing to
    // (campX, campY).
    struct StraightPullLane
    {
        float bearing = 0.0f;  // radians, pack -> stand
        float standX = 0.0f;
        float standY = 0.0f;
        float campX = 0.0f;
        float campY = 0.0f;
        float margin = 0.0f;   // min over keep-aways of dist(lane) - radius
        float walk = 0.0f;     // tank's straight-line distance to the stand spot
        float score = 0.0f;    // margin, less a small charge for the walk
    };

    // 2D distance from (px,py) to the segment (ax,ay)-(bx,by).
    inline float PointSegmentDist2d(float px, float py, float ax, float ay,
                                    float bx, float by)
    {
        float const dx = bx - ax;
        float const dy = by - ay;
        float const len2 = dx * dx + dy * dy;
        float t = 0.0f;
        if (len2 > 1e-6f)
            t = std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f);
        float const cx = ax + t * dx - px;
        float const cy = ay + t * dy - py;
        return std::sqrt(cx * cx + cy * cy);
    }

    // Walk-distance charge per yard in the lane score: 20yd of extra walk is
    // worth giving up 1yd of margin.
    inline constexpr float StraightPullWalkCost = 0.05f;

    // Clearance past this is as good as any more; the cap also keeps the walk
    // charge meaningful when nothing is in the room.
    inline constexpr float StraightPullMarginCap = 100.0f;

    // Ranks the straight-pull lanes around a pack, best first.
    //
    // In a room pre-clear the tank used to close on the pack from wherever it
    // stood and tag it at commit range, and the camp was the trail back the way
    // it came. From the far side of a room that is a diagonal in front of the
    // boss: the tag spot, the drag and every straggler's run all cross the
    // boss's aggro (Moroes, tr-20260926-192642-3 — the lane ran 23yd from a
    // dinner guest). A player walks up square in front of the pack and pulls it
    // straight back instead.
    //
    // For each of `bearings` directions out of the pack: stand spot `standDist`
    // out, camp straight on along the same line at the farthest drag in
    // [minDrag, drag] that is inside the camp box (when there is one). The lane
    // scored is stand -> camp, where the tank and the fight stand, AND the tank's
    // walk out to the stand spot; its margin is the worst clearance over the
    // keep-aways. Bearings with no in-box camp are dropped.
    //
    // The walk counts because that is where the second pack came from in
    // tr-20260927-210901-8: the lane itself cleared everything, and the tank woke
    // a Ghostly Steward and a whole Guest formation on its way to the stand spot.
    // A keep-away the tank is already standing inside is left out of the walk
    // term — every walk starts in it, so it would sink every lane equally and
    // rank nothing.
    inline std::vector<StraightPullLane> RankStraightPullLanes(
        float packX, float packY, float tankX, float tankY, float standDist,
        float drag, float minDrag, std::vector<LaneKeepAway> const& keepAways,
        bool hasBox, float boxMinX, float boxMaxX, float boxMinY, float boxMaxY,
        int bearings = 24)
    {
        std::vector<StraightPullLane> out;
        if (bearings <= 0 || standDist <= 0.0f || drag <= 0.0f)
            return out;
        minDrag = std::clamp(minDrag, 0.0f, drag);
        auto inBox = [&](float x, float y)
        {
            return !hasBox ||
                   (x >= boxMinX && x <= boxMaxX && y >= boxMinY && y <= boxMaxY);
        };

        constexpr float kTwoPi = 6.28318530718f;
        for (int i = 0; i < bearings; ++i)
        {
            float const a = kTwoPi * float(i) / float(bearings);
            float const ux = std::cos(a);
            float const uy = std::sin(a);

            StraightPullLane lane;
            lane.bearing = a;
            lane.standX = packX + ux * standDist;
            lane.standY = packY + uy * standDist;

            bool found = false;
            for (float d = drag; d >= minDrag - 1e-3f; d -= 1.0f)
            {
                float const cx = lane.standX + ux * d;
                float const cy = lane.standY + uy * d;
                if (inBox(cx, cy))
                {
                    lane.campX = cx;
                    lane.campY = cy;
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            lane.margin = StraightPullMarginCap;
            for (LaneKeepAway const& k : keepAways)
            {
                lane.margin = std::min(
                    lane.margin, PointSegmentDist2d(k.x, k.y, lane.standX, lane.standY,
                                                    lane.campX, lane.campY) -
                                     k.radius);
                float const tdx = tankX - k.x;
                float const tdy = tankY - k.y;
                if (tdx * tdx + tdy * tdy >= k.radius * k.radius)
                    lane.margin = std::min(
                        lane.margin, PointSegmentDist2d(k.x, k.y, tankX, tankY,
                                                        lane.standX, lane.standY) -
                                         k.radius);
            }
            float const wx = lane.standX - tankX;
            float const wy = lane.standY - tankY;
            lane.walk = std::sqrt(wx * wx + wy * wy);
            lane.score = lane.margin - StraightPullWalkCost * lane.walk;
            out.push_back(lane);
        }

        std::stable_sort(out.begin(), out.end(),
                         [](StraightPullLane const& l, StraightPullLane const& r)
                         { return l.score > r.score; });
        return out;
    }

    // Which room-trash unit a room clear pulls next: the nearest one whose lane has
    // not been refused, else — everything left is refused — the nearest of all, so
    // the clear can never stall on its own refusals. -1 when there is nothing.
    //
    // Refused = no clean straight lane (DcPullPlanner::ComputeRoomClearLane with
    // requireClean). Nearest-first alone took a Guest pack 26.8yd from a dinner
    // guest whose best lane's walk ran inside the next formation's reach
    // (tr-20260927-210901-8); pulling the packs that CAN be taken cleanly first
    // clears the room outside-in and opens lanes for the rest.
    //
    // `last` marks the boss-side ring (RoomAggroBoss::bossSideLastRadius): a ring
    // unit is never picked while any unit outside the ring is alive, refused or
    // not. The ring's penalty alone lost to the refusals — sk-20261019-105358, 13
    // of 14 unplanned Emperor wakes: every balcony pack refused "no clean lane",
    // the unrefused ring packs became the only pick, and the walk to them woke the
    // throne with both balconies still up. A refused pack outside the ring goes as
    // the last resort first; the ring follows, unrefused before refused.
    inline int PickRoomTrashIndex(std::vector<float> const& dist,
                                  std::vector<bool> const& refused,
                                  std::vector<bool> const& last = {})
    {
        // Tier: 0 outside the ring unrefused, 1 outside refused, 2 ring unrefused,
        // 3 ring refused. Lowest tier wins, nearest within it.
        int best = -1;
        int bestTier = 4;
        for (std::size_t i = 0; i < dist.size(); ++i)
        {
            bool const isRefused = i < refused.size() && refused[i];
            bool const isLast = i < last.size() && last[i];
            int const tier = (isLast ? 2 : 0) + (isRefused ? 1 : 0);
            if (tier < bestTier || (tier == bestTier && dist[i] < dist[best]))
            {
                best = static_cast<int>(i);
                bestTier = tier;
            }
        }
        return best;
    }

    // Lane refusals were measured against the room as it stood; a kill since then
    // (fewer live room-trash units than when the list was written) can open any of
    // them, so the list is dropped and every pack is measured again.
    inline bool ShouldDropLaneRefusals(uint32_t liveNow, uint32_t liveAtWrite)
    {
        return liveNow < liveAtWrite;
    }

    // Whether the walk to a room-clear stand spot rounds the pack on its commit
    // ring. The ring is a 2D circle snapped to the navmesh at the PACK's height,
    // so it only describes ground the tank can walk when both stand on one floor.
    // A pack on a ledge over the tank (the Emperor's corner balconies, 7.5yd up)
    // puts every ring point on the wrong tier: the two arcs collapse onto spots
    // beside the tank and it bounced between them for 6m39s instead of taking
    // the ramp (tr-20261019-183728-12). Across tiers the navmesh path to the
    // stand spot is the route; nothing on the floor below is in the pack's reach.
    inline bool StandWalkRoundsPack(float botZ, float packZ, float levelTolerance)
    {
        return std::fabs(botZ - packZ) <= levelTolerance;
    }

    // The distance a room clear ranks a pack by when off-level packs go last
    // (RoomAggroBoss::detourReachable): an off-level pack sorts behind every pack
    // on the tank's own floor, so the floor is cleared before the clear climbs to
    // a ledge, and among the ledge packs the nearest still goes first.
    //
    // A room with a boss-side ring (RoomAggroBoss::bossSideLastRadius) ranks the
    // packs inside it behind everything else, the ledges included: their lanes run
    // at the boss, so they are the pulls that wake him, and a wake while the rest
    // of the room still stands brings all of it. `distToBoss` < 0 = no live boss.
    constexpr float RoomTrashOffLevelPenalty = 1000.0f;
    constexpr float RoomTrashBossSidePenalty = 2000.0f;
    inline float RoomTrashPickDistance(float dist2d, float dz, float levelTolerance,
                                       bool offLevelLast, float distToBoss = -1.0f,
                                       float bossSideLastRadius = 0.0f)
    {
        if (bossSideLastRadius > 0.0f && distToBoss >= 0.0f && distToBoss < bossSideLastRadius)
            return dist2d + RoomTrashBossSidePenalty;
        return offLevelLast && std::fabs(dz) > levelTolerance
                   ? dist2d + RoomTrashOffLevelPenalty
                   : dist2d;
    }

    // A pull-mode camp the tank has left further behind than any live camp may sit
    // (maxTankGap: PartyMaxSpread + the largest legitimate standoff). No member can
    // be inside PartyMaxSpread of it and inside maxTankGap of the tank at once, so a
    // spread gate anchored on it can never pass. 0 = no backstop, nothing is stale.
    inline bool CampAnchorIsStale(float tankToCamp, float maxTankGap)
    {
        return maxTankGap > 0.0f && tankToCamp > maxTankGap;
    }

    // A stock chase toward `target` while a room-aggro boss still sleeps and its
    // room is being pre-cleared: true when the chase ends inside the boss's wake
    // sphere (ChaseEndToBoss), i.e. inside the boss's aggro. Mobs in there "come
    // with the boss" and are excluded from the room clear, but one woken early (a
    // social pull, a flee-for-assist) still drew the chase in after it.
    inline bool ChaseWakesSleepingBoss(float chaseEndToBoss, float wakeSphere, bool bossInCombat)
    {
        return !bossInCombat && chaseEndToBoss < wakeSphere;
    }

    // The stalled fallback (DungeonClearClearStalledAction) pulls one nearby mob
    // and is then done: once that mob is gone and the fight is over, the stall is
    // cleared and Advance tries the route again.
    inline bool StallPullIsDone(bool pullTargetAlive, bool inCombat)
    {
        return !pullTargetAlive && !inCombat;
    }

    // Advance's per-point MoveTo fallback: does this refused move count toward
    // DC_STUCK_LIMIT? Not when the movement arbiter only deferred it — an
    // equal-or-higher priority move still holds, which is a wait, not a shut way.
    // Counting those turned ~1s of a post-pull hold into a "movement isn't
    // progressing" stall (tr-20261019-182249-2). Pursuit already reads a deferred
    // move as alive.
    inline bool MoveToFallbackCountsStuck(bool moved, bool deferred)
    {
        return !moved && !deferred;
    }

    // Engage-trash's walk-in on one target: is it getting any nearer? True once the
    // distance has not improved by `minClose` for `limitMs` of walking. A gap
    // between ticks longer than `gapMs` (the action lost the tick to a leash
    // hold, a long-route deferral, a fight) restarts the window rather than
    // counting the time away against it.
    //
    // tr-20261019-182249-1 / sk-20261019-105358-31: the walk-in on a Doomforge
    // Arcanasmith past Argelmach's door stopped every tick to re-aim and Advance
    // re-issued its spline in between; the tank stood at (781.7,31.9) for 100s
    // through 118 yields, the stuck count it never reset climbed to the limit, and
    // the stall it set ended the run.
    struct WalkInWatch
    {
        float bestDist = -1.0f;
        uint32_t progressMs = 0;
        uint32_t lastTickMs = 0;
    };

    inline bool EngageWalkGivenUp(WalkInWatch& w, float dist, float minClose, uint32_t nowMs,
                                  uint32_t limitMs, uint32_t gapMs)
    {
        bool const restart = w.bestDist < 0.0f || nowMs - w.lastTickMs > gapMs;
        if (restart || dist < w.bestDist - minClose)
        {
            w.bestDist = dist;
            w.progressMs = nowMs;
        }
        w.lastTickMs = nowMs;
        return nowMs - w.progressMs >= limitMs;
    }

    // The assist camp's no-LOS close-in on a fight holder: true once every move
    // toward the SAME holder has been refused for `limitMs` running. A move that
    // goes restarts it; so does a new holder (its refusals start now).
    // `refusedSince` is the caller's state, 0 = not refusing.
    //
    // sk-20261019-105358 (-70/-72/-73, 2/17): a Reservist or Medic General
    // Angerforge summons at a random offset landed inside the geometry west of the
    // fight. Same floor, so the level gate passed it; every member drove at it on
    // a refused route (900-2000 refusals each), kept re-seeding combat on it over
    // the stuck-combat breaker, and walked off the tank until the stall watchdog.
    inline bool AssistCloseGivenUp(bool moved, bool sameTarget, uint32_t nowMs,
                                   uint32_t limitMs, uint32_t& refusedSince)
    {
        if (moved)
        {
            refusedSince = 0;
            return false;
        }
        if (!sameTarget || refusedSince == 0)
        {
            refusedSince = nowMs != 0 ? nowMs : 1;
            return false;
        }
        return nowMs - refusedSince >= limitMs;
    }

    // How far from the boss a chase toward a target ENDS (2D): a mover with reach
    // `range` stops that far short of the target on its own side, and one already
    // inside it does not move at all. range 0 = it ends on the target (melee, a
    // gap-closer's leap). Feeds ChaseWakesSleepingBoss.
    //
    // sk-20261019-105358 (-61/-57/-47, 900s each): a Doomforge Dragoon with no
    // combat movement, 26.4yd from the Emperor (sphere 28.3), woke and was fought
    // where it stands. Testing where the TARGET stood banned every reach toward
    // it, the ranged included, held 44-50yd out past their spell range — nothing
    // could ever kill it. A caster stopping 30yd short of it is 48yd from the boss.
    inline float ChaseEndToBoss(float botX, float botY, float targetX, float targetY,
                                float bossX, float bossY, float range)
    {
        float const dx = botX - targetX;
        float const dy = botY - targetY;
        float const toBot = std::sqrt(dx * dx + dy * dy);
        float endX = targetX;
        float endY = targetY;
        if (toBot <= range)
        {
            endX = botX;
            endY = botY;
        }
        else if (range > 0.0f)
        {
            endX = targetX + dx / toBot * range;
            endY = targetY + dy / toBot * range;
        }
        return std::sqrt((endX - bossX) * (endX - bossX) + (endY - bossY) * (endY - bossY));
    }
}

#endif
