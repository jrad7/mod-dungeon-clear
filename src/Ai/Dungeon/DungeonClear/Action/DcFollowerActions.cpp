/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearActions.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "MoveSplineInitArgs.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Position.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproach.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproachIo.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDoorPolicy.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPathWorker.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/StridedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/SwimPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h"
#include "Playerbots.h"
#include "DcActionShared.h"

using namespace DcActionShared;

namespace
{
    // Trail-arrival tolerance. A follower gliding up the tank's breadcrumb trail
    // stops HOLDING once it is within this many yards of its target crumb, so its
    // true rest distance from the tank is the lag PLUS up to this slack. The
    // scout-lag spread clamp below must budget for it (see kScoutLagSpreadMargin)
    // or the followers park just outside the tank's readiness gate and deadlock.
    constexpr float kTrailArrival = 4.0f;

    // True while a continuous escort-spline glide is in flight. The follower trail
    // re-issue guards key off this (NOT the LastMovement wait) so a healthy glide
    // is left to finish and chain seamlessly into the next window, exactly as
    // DcAdvanceAction does for the tank — relaunching MoveSplinePath every tick is
    // what made followers half-step.
    bool TrailSplineRunning(Player* bot)
    {
        MotionMaster* mm = bot->GetMotionMaster();
        return mm && mm->GetCurrentMovementGeneratorType() == ESCORT_MOTION_TYPE &&
               bot->isMoving();
    }

    // Issue the follower's centered breadcrumb window (live pos .. the crumb `lag`
    // yards behind the tank) as ONE continuous escort spline — the same smooth
    // glide the tank's advance uses, replacing the per-tick single-point MoveTo
    // that made followers visibly half-step at each crumb. Returns true iff a
    // spline was launched (a usable >= 2-point window existed and SplinePath took
    // it); callers fall back to the single-point step / Follow fan on false.
    bool IssueTrailGlide(PlayerbotAI* botAI, Player* bot, float lag)
    {
        std::vector<Position> trail;
        if (!DcLeaderSignal::GetLeaderScoutTrail(bot, lag, trail) || trail.size() < 2)
            return false;
        Movement::PointsArray window;
        window.reserve(trail.size());
        for (Position const& p : trail)
            window.emplace_back(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ());
        return DcMovement::SplinePath(botAI, window);
    }
}

bool DungeonClearFollowTankAction::Execute(Event /*event*/)
{
    ObjectGuid& followedTank =
        context->GetValue<ObjectGuid>("dungeon clear followed tank")->RefGet();

    Player* tank = AI_VALUE(Player*, "dungeon clear party tank");
    if (!tank || tank == bot)
    {
        // No DC tank: tear down the leftover continuous MoveFollow we
        // installed while following. MoveFollow is a persistent MotionMaster
        // order; once the tank's DC flag clears, this action stops being
        // selected and nothing else cancels it for a self-bot (its ordinary
        // follow targets itself and no-ops without clearing), so it would
        // stay glued to the tank. Clear it once, forget the tank, and let the
        // bot revert to stock behavior (a self-bot then stands still as the
        // leader; normal bots fall back to following their master).
        if (!followedTank.IsEmpty())
        {
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] follow-tank: released (DC tank gone) -> cleared "
                     "follow generator (selfRealPlayer={})",
                     bot->GetName(), botAI && IsSelfBot(bot) ? 1 : 0);
            followedTank = ObjectGuid::Empty;
            // Cleanly torn down by us -> drop the orphan-reaper mark; there is no
            // longer a follow generator for it to chase down.
            DcFollowerLifecycle::UnmarkFollowing(bot->GetGUID());
        }
        return false;
    }

    // Leader is dropping down a narrow one-way hole the followers can't path
    // (Wailing Caverns' return-fall off Verdan's shelf). HOLD here at the ledge
    // top instead of following: a MoveFollow toward the now-far-below tank finds
    // no navmesh route and produces a degenerate path that clips the follower
    // straight down through the hole wall. StopBot(Hold) tears down any follow
    // generator already installed (otherwise it keeps driving the clip). The
    // leader teleports the whole party to the landing the instant it lands (the
    // DropInHole RunStep gate), so the right behavior until then is to stand
    // still. Keep the teardown/orphan bookkeeping live so a generator we installed
    // is still cancelled when the DC tank later goes away.
    if (DcLeaderSignal::IsLeaderDroppingInHole(bot))
    {
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        followedTank = tank->GetGUID();
        DcFollowerLifecycle::MarkFollowing(bot->GetGUID());
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] follow-tank: leader dropping down the hole -> holding at "
                  "the ledge top until it lands", bot->GetName());
        return true;
    }

    // Loot yield (with commit-timeout). Followers run ONLY this action while DC
    // is active (their own DC is never enabled — `dc on` is tank-only — so the
    // advance/engage triggers are all inactive for them; follow-tank, relevance
    // 25, outranks the loot actions (open loot 8, move to loot 7, loot 6) every
    // non-combat tick). Without yielding here, the instant `move to loot` walks
    // a follower past followDistance toward a corpse, follow-tank wins the next
    // tick and yanks it straight back — so followers can only ever pick up
    // corpses already sitting inside the tank's follow bubble, and a corpse a
    // few yards out never gets looted at all. The tank pauses between pulls to
    // let loot resolve (see the advance loot-yield in Execute); mirror that here
    // so the party can actually path out to corpses during that window.
    //
    // Stepping aside on EITHER loot flag covers the whole pickup lifecycle:
    // "has available loot" is true only at ~3-15yd and flips FALSE at ~3yd when
    // "can loot" flips TRUE, so yielding on just one would re-assert follow at
    // the 3yd boundary and pull the bot off the corpse before `open loot` ran
    // — the same oscillation the tank's advance yield avoids. The stack is
    // populated by `add all loot` (relevance 5), which gets its tick whenever
    // the follower is already within followDistance and Follow() no-ops (returns
    // false) — i.e. while clustered on the resting tank, exactly when corpses
    // are nearby. The timeout stops a follower parking forever on a corpse it
    // can't finish (group-roll pending, bags full) and being left behind, which
    // would also stall the tank on its party-spread gate.
    //
    // Strip already-given-up loot first (above the loot pipeline's relevance),
    // so a corpse this follower abandoned can't keep the flags below true and
    // re-arm the yield each time it drifts back within lootDistance of the
    // tank — the corpse<->tank ping-pong.
    DcLootPolicy::StripSkippedLoot(botAI);
    // Proactively skip a corpse with nothing takeable for this follower (un-
    // finishable group-roll/reserved loot, or below DungeonClear.LootMinQuality)
    // BEFORE it walks over — so it never steps off follow for it and never adds
    // to the tank's IsAnyPartyMemberLooting wait. Event-driven counterpart to
    // the camp/timeout cutoffs, which only fire after the walk is wasted.
    DcLootPolicy::MaybeSkipUnworthyLoot(botAI);
    // Fast-skip a corpse this follower has been camped on too long instead of
    // waiting out the full yield timeout: an un-finishable corpse (group-roll
    // items pending, bags full) otherwise wastes 15s here AND keeps the tank's
    // IsAnyPartyMemberLooting true, stalling the whole party on it.
    DcLootPolicy::MaybeGiveUpCampedLoot(botAI, DC_LOOT_CAMP_TIMEOUT_MS, DC_LOOT_GIVEUP_TTL_MS);
    uint32& lootYieldStart =
        context->GetValue<DcApproachState&>("dungeon clear approach state")->Get().lootYieldStartMs;
    bool const lootYield =
        AI_VALUE(bool, "has available loot") || AI_VALUE(bool, "can loot");
    if (lootYield)
    {
        uint32 const now = getMSTime();
        if (lootYieldStart == 0)
            lootYieldStart = now;

        if (now - lootYieldStart < DC_LOOT_YIELD_TIMEOUT_MS)
        {
            // Hand the tick to move-to-loot / open-loot. Don't issue a follow
            // move that would drag the bot off the corpse.
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] follow-tank yielding: loot in progress ({}ms)",
                      bot->GetName(), now - lootYieldStart);
            return false;
        }
        // Waited long enough — give up on THIS corpse (blacklist it so the
        // flags drop next tick and we stop being yanked back to it), then resume
        // following to rejoin the tank. Leave lootYieldStart expired so we keep
        // following until the flags clear (which resets the timer).
        DcLootPolicy::GiveUpCurrentLoot(botAI, DC_LOOT_GIVEUP_TTL_MS);
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] follow-tank loot-yield timed out after {}ms -> giving up on corpse, following",
                 bot->GetName(), now - lootYieldStart);
    }
    else
    {
        lootYieldStart = 0;  // not looting -> reset the commit timer
    }

    // In DYNAMIC pull mode, trail the tank at a lag distance while it scouts toward
    // the next pack and decides Leeroy vs Advanced (leader out of combat, phase
    // Idle). The tight follow bubble otherwise dragged the party onto the tank's
    // heels and into the pack's aggro arc before the tank committed, accidentally
    // pulling — the reported bug. This can NOT be done through Follow()'s distance
    // arg: Follow() early-outs whenever the bot is already inside the global
    // followDistance and refuses to re-issue an existing follow on the same target,
    // so it can only ever CLOSE a gap, never widen one (the previous attempt was a
    // silent no-op). Manage the spacing directly instead: hold inside the lag
    // bubble (let the tank pull ahead), and when the tank has pulled beyond it step
    // up to the lag ring and stop rather than charging back into the tight bubble.
    // The instant the tank commits — enters combat (Leeroy) or marks a camp
    // (Advanced hands the party to hold-at-camp, so follow-tank stands down) — this
    // gate flips false and the party reverts to the tight follow / camp hold below.
    if (DcLeaderSignal::IsLeaderDynamicScouting(bot))
    {
        // The scout-lag deliberately PARKS followers `lag` yards behind the tank
        // (the `toTank <= lag` hold branch below stops them there). But the tank's
        // own between-pulls gate (DcAdvanceAction::TryBetweenPullsRest ->
        // IsBetweenPullsReady -> GetSpreadGate) refuses to advance until every
        // member is within DungeonClear.PartyMaxSpread. If the configured lag
        // exceeds that spread the two gates deadlock: the followers hold at the lag
        // ring OUTSIDE the spread the tank requires, so the tank waits forever for a
        // party that, by design, is never closing — the reported hang when
        // PartyMaxSpread is lowered toward PullDynamicPartyLag. Never lag farther than
        // the spread the tank will accept.
        //
        // The margin is NOT cosmetic: a follower's true rest distance is the lag
        // PLUS the trail-arrival slack (it HOLDS as soon as it is within
        // kTrailArrival of its crumb at `lag` behind the tank — see the arrival
        // hold below), so it can settle ~lag + kTrailArrival behind a stopped tank.
        // A bare `lag <= maxSpread` therefore still deadlocked at spread 15 / lag 15
        // (clamped 12, but resting ~16 > 15). Budget the arrival slack plus a couple
        // yards of walked-vs-straight / tick jitter so the held follower always
        // lands strictly inside the gate. At the default 25yd spread / 15yd lag this
        // is a no-op (15 < 25 - 6).
        constexpr float kScoutLagSpreadMargin = kTrailArrival + 2.0f;
        float const maxSpread = DcSettings::GetFloat(bot, "PartyMaxSpread");
        float const lag =
            std::min(DcSettings::GetFloat(bot, "PullDynamicPartyLag"),
                     std::max(2.0f, maxSpread - kScoutLagSpreadMargin));
        // 3D, NOT 2D: the tank's spread gate (IsPartyReady) measures GetDistance,
        // which is 3D — so the hold metric here MUST match it or they diverge on a
        // ramp/incline. A follower directly down-slope of the tank reads a small 2D
        // gap (it would hold) while its true 3D distance — what the gate enforces —
        // blows past PartyMaxSpread, and the tank waits forever for a follower that,
        // measured in 2D, thinks it is already close enough. This is the "worst on
        // inclines and ramps" deadlock. The trail-point accumulation (GetExactDist,
        // 3D) and the arrival hold below are already 3D; this was the last 2D read.
        float const toTank = bot->GetExactDist(tank);
        // Keep the teardown/orphan-reaper bookkeeping live across the whole window
        // so a follow generator we (or a prior tick) installed is still cancelled
        // when the DC tank goes away.
        followedTank = tank->GetGUID();
        DcFollowerLifecycle::MarkFollowing(bot->GetGUID());
        if (toTank <= lag)
        {
            // Inside the lag bubble: hold position. Tear down any leftover
            // continuous MoveFollow so the bot actually stops instead of creeping
            // back to the tight followDistance.
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            DC_PULL_TRACE("[DC:{}] scout-lag: holding {:.1f}yd behind tank (lag {:.1f})",
                          bot->GetName(), toTank, lag);
            // Return FALSE, not true: the bot is already stopped, so yield the tick
            // to the lower-relevance pipeline (eat/drink, loot) exactly as stock
            // Follow() does on its "no need to follow" in-range early-out. Consuming
            // the tick here (return true) suppressed the out-of-combat rest routine
            // — the party held forever at the lag ring and never drank, deadlocking
            // the tank's between-pulls wait on the party's mana.
            return false;
        }
        // Tank pulled beyond the lag: step up to a point `lag` yards behind the
        // tank ALONG ITS BREADCRUMB TRAIL — the ground the tank actually walked,
        // which its escort spline already corridor-centered. The earlier version
        // projected a geometric lag point off the tank (tx + bearing*lag) and
        // MoveTo'd it through the raw PathGenerator: no corridor centering, so
        // followers cut their own wall-hugging corners, and the projected point
        // itself (bot's own Z, straight off the tank) could land on a ledge lip —
        // the reported "hugging walls / falling off ledges in dynamic pull". Trail
        // points are reachability-gated centered crumbs, so the move stays on the
        // safe route the tank already cleared.
        Position trailPoint;
        if (DcLeaderSignal::GetLeaderScoutTrailPoint(bot, lag, trailPoint))
        {
            // ARRIVAL HOLD. The hold gate above measures STRAIGHT-LINE distance to
            // the tank (toTank <= lag), but the trail point is the crumb at `lag`
            // yards of WALKED (path) distance behind the tank. On a curved corridor
            // the two disagree: a crumb 15yd back along the trail can sit ~16yd
            // straight-line from the tank, so a follower standing ON that crumb
            // still reads toTank > lag and never satisfies the hold gate. Without
            // this guard it re-issues MoveTo to the crumb it already occupies every
            // tick, micro-stepping around it forever — the reported "two steps
            // forward, two steps back" dance — and, because it's perpetually
            // "moving", never sits to drink/eat, stalling the tank's between-pulls
            // rest gate on its mana. So: if the bot has effectively reached the
            // trail point, HOLD here (same teardown + tick-yield as the in-bubble
            // branch) instead of demanding a straight-line gate it can't meet while
            // parked on a curved crumb. The slack is just the path-vs-straight
            // curvature over one lag; a few yards covers it without parking short.
            if (bot->GetExactDist(&trailPoint) <= kTrailArrival)
            {
                DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                DC_PULL_TRACE("[DC:{}] scout-lag: holding at trail point "
                              "({:.1f}yd behind tank, lag {:.1f})",
                              bot->GetName(), toTank, lag);
                return false;
            }
            // Smooth glide along the centered crumb trail. Leave a healthy escort
            // glide in flight alone so it chains seamlessly instead of relaunching
            // (and stuttering) every tick, then ride the whole crumb window as ONE
            // continuous spline rather than the per-crumb single-point MoveTo below.
            if (TrailSplineRunning(bot))
                return true;
            if (IssueTrailGlide(botAI, bot, lag))
            {
                DC_PULL_DEBUG("[DC:{}] scout-lag: gliding tank's centered breadcrumbs "
                              "(was {:.1f}yd behind, lag {:.1f})",
                              bot->GetName(), toTank, lag);
                return true;
            }
            // normal_only: reject (don't straight-line to) a point that isn't
            // reachable over a real navmesh path. Crumbs are already gated for
            // reachability, but keep the guard as a belt-and-braces backstop.
            // Reached only when the glide window was too short / unreachable.
            if (DcMoveTo(bot->GetMapId(), trailPoint.GetPositionX(),
                       trailPoint.GetPositionY(), trailPoint.GetPositionZ(),
                       false, false, /*normal_only=*/true))
            {
                DC_PULL_DEBUG("[DC:{}] scout-lag: trailing tank along breadcrumbs "
                              "(was {:.1f}yd behind, lag {:.1f})",
                              bot->GetName(), toTank, lag);
                return true;
            }
        }
        // No trail yet (pull mode just toggled on, tank hasn't moved) or the trail
        // point was unreachable: fall through to a normal follow so the party never
        // gets permanently stranded.
    }

    // Room-aggro skirt for followers. While the leader clears a room before a boss
    // whose engage drags the WHOLE room into combat, the tank routes its OWN
    // approach AROUND the boss's aggro sphere (RoomAggroSkirtPoint). A follower
    // close-following the tank, though, makes a STRAIGHT line to it — and if the
    // tank is on the far side of the sphere (or just walked around it), that line
    // cuts through the aggro range and wakes the boss even though the tank dodged
    // it: the reported failure. So when the direct line to the tank clips the
    // sphere, walk the follower around it on the same short-arc detour the tank
    // uses (AggroSafeApproachPoint with the TANK as the move target), and revert to
    // the normal follow the instant a straight shot at the tank is clear. Skipped
    // entirely outside an active room clear (the lookup is a cheap no-op). The
    // dynamic scout-lag branch above already keeps the party on the tank's safe
    // breadcrumb trail in pull mode, so this only governs the tight close-follow.
    {
        Position bossCenter;
        float safeRadius = 0.0f;
        if (DcLeaderSignal::GetLeaderRoomAggroSphere(bot, bossCenter, safeRadius) &&
            DcEngageGeometry::NeedsRoomAggroSkirt(
                bot->GetPositionX(), bot->GetPositionY(),
                tank->GetPositionX(), tank->GetPositionY(),
                bossCenter.GetPositionX(), bossCenter.GetPositionY(), safeRadius))
        {
            if (std::optional<Position> wp = DcEngageGeometry::AggroSafeApproachPoint(
                    bot, bossCenter.GetPositionX(), bossCenter.GetPositionY(),
                    bossCenter.GetPositionZ(), safeRadius, tank))
            {
                // Keep the teardown/orphan-reaper bookkeeping live so a leftover
                // continuous MoveFollow is still cancelled when the DC tank goes
                // away; the point-move below supersedes the follow generator (same
                // as the scout-lag trail branch above — no explicit Stop needed).
                followedTank = tank->GetGUID();
                DcFollowerLifecycle::MarkFollowing(bot->GetGUID());
                bool const moved = DcMoveTo(bot->GetMapId(), wp->GetPositionX(),
                                          wp->GetPositionY(), wp->GetPositionZ(),
                                          false, false, /*normal_only=*/false);
                if (moved || bot->isMoving())
                {
                    DC_PULL_DEBUG("[DC:{}] follow-tank: skirting room-aggro sphere "
                                  "(r={:.1f}) -> detour ({:.1f}, {:.1f})",
                                  bot->GetName(), safeRadius,
                                  wp->GetPositionX(), wp->GetPositionY());
                    return true;
                }
            }
        }
    }

    // Tighter cluster than default. Keeps followers in healer LOS and out
    // of mob aggro-radius arcs during the advance. Default followDistance
    // (~10yd) had them strung out by the time the tank engaged.
    float const dist = std::min<float>(sPlayerbotAIConfig.followDistance, 6.0f);

    // Centered trail-follow. Stock Follow() / MoveFollow re-paths to the follow
    // slot through the core PathGenerator, which returns Detour's taut, wall-
    // HUGGING line — so followers scrape walls and clip ledge edges the whole
    // advance, the exact thing PathCenterEnable removes for the TANK. The tank's
    // escort route is already corridor-centered, and its actual footsteps are
    // recorded as breadcrumbs (DcAdvanceAction::RecordBreadcrumb). So once the
    // tank has pulled ahead, walk the follower UP that centered crumb trail
    // instead of re-deriving a parallel wall-hugging path of its own: the
    // centering is inherited for free — the tank paid the navmesh/VMAP cost once
    // when it built the route, and nothing here re-runs CorridorCenter. This is
    // the same mechanism the dynamic scout-lag branch above uses, generalized to
    // the ordinary close-follow. When the follower is already caught up near the
    // tank we DON'T trail — we fall through to the golden-angle Follow() fan,
    // whose spread keeps healers in LOS and the party out of one stack, and over
    // that short a hop wall-hugging is irrelevant. Gated on the same switch as
    // the centering itself (PathCenterEnable): with centering off the tank's
    // crumbs are the wall-hugging line anyway, so there is nothing to inherit
    // and the stock Follow() fan is the right fallback.
    if (DcSettings::GetBool(ObjectGuid::Empty, "PathCenterEnable"))
    {
        float const toTank = bot->GetExactDist2d(tank);
        // Only trail once the tank is beyond the tight follow bubble — i.e. a
        // real corridor traversal is involved, not a fan-out shuffle. Below this
        // the Follow() fan below keeps the cluster tight in healer LOS.
        float const trailEngage = dist + 2.0f;
        if (toTank > trailEngage)
        {
            // Per-bot stagger so the column spreads single-file ALONG the
            // centered trail rather than every follower targeting the one crumb
            // at `dist` behind the tank and piling onto it. Stable per GUID, same
            // spirit as the golden-angle fan below but projected onto the trail.
            uint32 const slot = static_cast<uint32>(bot->GetGUID().GetCounter()) % 4u;
            float const lag = dist + static_cast<float>(slot) * 3.0f;
            Position trailPoint;
            // Skip the trail when the chosen crumb is one we already occupy: re-
            // issuing MoveTo to a point we're basically on micro-steps in place
            // (the scout-lag "two steps forward, two back" dance). Let Follow()
            // take it — it early-outs cleanly when in range.
            if (DcLeaderSignal::GetLeaderScoutTrailPoint(bot, lag, trailPoint) &&
                bot->GetExactDist(&trailPoint) > kTrailArrival)
            {
                // Keep the teardown / orphan-reaper bookkeeping live; the point-
                // move / glide supersedes any MoveFollow a prior tick installed
                // (same as the scout-lag trail / room-aggro skirt branches above —
                // no explicit Stop needed).
                followedTank = tank->GetGUID();
                DcFollowerLifecycle::MarkFollowing(bot->GetGUID());
                // Smooth glide along the centered crumb trail: leave a healthy
                // escort glide alone (re-issue discipline keyed on splineRunning,
                // not the LastMovement wait), then ride the whole crumb window as
                // ONE continuous spline instead of the per-crumb single-point
                // MoveTo below — the per-crumb relaunch is what made followers
                // half-step the whole advance.
                if (TrailSplineRunning(bot))
                    return true;
                if (IssueTrailGlide(botAI, bot, lag))
                {
                    DC_PULL_DEBUG("[DC:{}] follow-tank: gliding tank's centered "
                                  "breadcrumbs ({:.1f}yd behind, lag {:.1f})",
                                  bot->GetName(), toTank, lag);
                    return true;
                }
                // normal_only: never straight-line to a crumb that isn't reachable
                // over a real navmesh path (belt-and-braces — crumbs are already
                // reachability-gated in GetLeaderScoutTrailPoint). Reached only
                // when the glide window was too short / unreachable.
                if (DcMoveTo(bot->GetMapId(), trailPoint.GetPositionX(),
                           trailPoint.GetPositionY(), trailPoint.GetPositionZ(),
                           false, false, /*normal_only=*/true))
                {
                    DC_PULL_DEBUG("[DC:{}] follow-tank: trailing tank's centered "
                                  "breadcrumbs ({:.1f}yd behind, lag {:.1f})",
                                  bot->GetName(), toTank, lag);
                    return true;
                }
            }
            // No usable trail yet (tank just moved off / crumb unreachable): fall
            // through to the stock follow so the party never strands.
        }
    }

    // Remember who we're chasing so the teardown branch above can cancel this
    // continuous MoveFollow once the DC tank goes away.
    followedTank = tank->GetGUID();
    // Record that this player now carries a follow generator so the world-tick
    // orphan reaper can cancel it if the AI is deleted out from under us — a
    // self-bot leaving bot mode never runs the teardown branch above.
    DcFollowerLifecycle::MarkFollowing(bot->GetGUID());
    // Explicit per-bot angle: every follower here is a self-bot (master ==
    // itself), and MovementAction::GetFollowAngle() skips the master while
    // scanning the group — a self-bot never matches its own entry and falls out
    // at 0.0f — so the default Follow() overload gave the WHOLE party the same
    // follow slot and stacked it on one point (which is also what kept the
    // stock collision shuffle permanently armed). Same deterministic
    // golden-angle fan as ComputeCampSlot, so the cluster spreads evenly and
    // each bot's slot never moves between ticks.
    uint32 const seed = static_cast<uint32>(bot->GetGUID().GetCounter());
    float const angle =
        Position::NormalizeOrientation(static_cast<float>(seed) * 2.39996323f);
    return Follow(tank, dist, angle);
}

bool DungeonClearFilterLootAction::Execute(Event /*event*/)
{
    // Same loot-floor enforcement the advance/follow-tank actions run inline
    // while active (see TryLootYield), lifted out so it keeps running while the
    // run is paused. Drop anything already given up from the stock stack/target,
    // proactively skip any in-range corpse/chest below DungeonClear.LootMinQuality
    // (or an un-finishable group-roll, or any chest while IgnoreChests is set),
    // and time out a corpse we've been camped on. All three only prune the stock
    // loot stack/target — no movement here.
    DcLootPolicy::StripSkippedLoot(botAI);
    DcLootPolicy::MaybeSkipUnworthyLoot(botAI);
    DcLootPolicy::MaybeGiveUpCampedLoot(botAI, DC_LOOT_CAMP_TIMEOUT_MS, DC_LOOT_GIVEUP_TTL_MS);
    // Return false: we only removed loot the bot must NOT take. Returning false
    // lets the engine fall through to the stock loot pipeline (open loot 8, move
    // to loot 7, ...) this same tick so whatever survived the filter is still
    // collected. This action sits just above that pipeline (relevance 9) so the
    // prune always runs first.
    return false;
}
bool DungeonClearCampHoldActionBase::Execute(Event /*event*/)
{
    Position camp;
    bool passive = false;
    if (!DcLeaderSignal::GetLeaderCampHold(bot, camp, passive))
        return false;

    // Healers hold at camp like everyone else but are pinned with the "stay"
    // strategy instead of "+passive" (ApplyFollowerPassive) so they can heal the
    // tank through the drag-back without RUNNING FORWARD to close heal range —
    // "stay" suppresses playerbots' reach-to-heal movement while leaving the
    // cast-heal action free. The position pin below still applies — we only stop
    // OWNING the tick once they're parked AND someone needs a heal, so the combat
    // engine can run the in-place heal cast.
    bool const isHealer = PlayerbotAI::IsHeal(bot);

    // Go passive (attack nothing) ONLY while the tank is actually tagging (a
    // holding phase). While merely holding at camp between pulls the party stays
    // ready to defend; the reaper strips any DC passive once we leave a holding
    // phase. Idempotent; the matching release is centralized in
    // ReapStrandedPassives so it fires on every exit.
    if (passive)
        DcFollowerLifecycle::ApplyFollowerPassive(bot);

    // Loot yield (scout phase only). The pack dies AT camp, so its corpses sit
    // right where the party holds. Without this the camp hold and the stock loot
    // pipeline fight each other: move-to-loot walks a follower a few yards to a
    // corpse, hold-at-camp yanks it back (corpse just outside the hold radius),
    // and un-finishable / below-floor corpses keep the loot flags armed forever —
    // the ping-pong the player saw. Mirror the follow-tank loot yield exactly:
    // prune corpses we must not take (skipped / below DungeonClear.LootMinQuality /
    // camped too long), then step ASIDE (return false, don't pull back to camp)
    // while a takeable corpse is in progress, bounded by a commit timeout. Skip
    // entirely while the tank is tagging (passive) — the party must stay pinned.
    if (!passive)
    {
        DcLootPolicy::StripSkippedLoot(botAI);
        DcLootPolicy::MaybeSkipUnworthyLoot(botAI);
        DcLootPolicy::MaybeGiveUpCampedLoot(botAI, DC_LOOT_CAMP_TIMEOUT_MS,
                                                DC_LOOT_GIVEUP_TTL_MS);
        uint32& lootYieldStart =
            context->GetValue<DcApproachState&>("dungeon clear approach state")->Get().lootYieldStartMs;
        bool const lootYield =
            AI_VALUE(bool, "has available loot") || AI_VALUE(bool, "can loot");
        if (lootYield)
        {
            uint32 const nowMs = getMSTime();
            if (lootYieldStart == 0)
                lootYieldStart = nowMs;
            if (nowMs - lootYieldStart < DC_LOOT_YIELD_TIMEOUT_MS)
            {
                // Hand the tick to move-to-loot / open-loot; do NOT yank back to
                // camp (that is the ping-pong).
                DC_PULL_TRACE("[DC:{}] hold-at-camp yielding: loot in progress ({}ms)",
                              bot->GetName(), nowMs - lootYieldStart);
                return false;
            }
            // Waited long enough — blacklist THIS corpse so the flags drop and we
            // stop being drawn back to it, then resume holding at camp.
            DcLootPolicy::GiveUpCurrentLoot(botAI, DC_LOOT_GIVEUP_TTL_MS);
            DC_PULL_TRACE("[DC:{}] hold-at-camp loot-yield timed out -> giving up corpse",
                          bot->GetName());
        }
        else
        {
            lootYieldStart = 0;  // not looting -> reset the commit timer
        }
    }

    // Cancel any persistent MoveFollow that follow-tank installed before the
    // pull. While holding we own this bot's movement, but MoveFollow lives in
    // the same ACTIVE MotionMaster slot and StopMoving does NOT remove the
    // generator — it re-asserts on the next motion update and walks the
    // follower right back out after the advancing tank (this is exactly how the
    // party ended up trailing the tank to the mob). Clear it once; follow-tank
    // re-installs it when the pull releases. Same persistent-generator gotcha as
    // the DC-tank-gone teardown / [[selfbot-stale-movefollow]].
    if (bot->GetMotionMaster() &&
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        context->GetValue<ObjectGuid>("dungeon clear followed tank")->Set(ObjectGuid::Empty);
        DcFollowerLifecycle::UnmarkFollowing(bot->GetGUID());
    }

    // Held ranged healer: clamp its movement so it can APPROACH the tank to reach
    // heal range but NEVER cross to the threat side of it. The healer carries the
    // "stay" pin (ApplyFollowerPassive) which disables every stock mover — so on
    // the yield below the cast-heal fires but nothing can walk the bot, killing
    // the overshoot where a pure ranged healer ran past the tank to a heal/follow
    // point that sits behind the tank (and behind = toward the pack when the tank
    // faces camp on the drag-back). DC supplies the approach itself, clamped to
    // the camp side of the heal target, so "approach yes, never past the tank".
    if (passive && isHealer)
    {
        Unit* const healTarget = AI_VALUE(Unit*, "party member to heal");
        uint8 const lowestPct = AI_VALUE2(uint8, "health", "party member to heal");
        if (healTarget && lowestPct < sPlayerbotAIConfig.almostFullHealth)
        {
            float const healRange = botAI->GetRange("heal");
            bool const canCast = bot->GetExactDist(healTarget) <= healRange &&
                                 bot->IsWithinLOSInMap(healTarget);
            if (canCast)
            {
                // In range + LOS: hold here and YIELD so the (movement-suppressed)
                // cast-heal lands in place. "stay" guarantees no mover runs.
                DcMovement::StopBot(bot, DcMovement::Stop::Soft);
                DC_PULL_TRACE("[DC:{}] healer: in range -> casting in place", bot->GetName());
                return false;
            }
            // Out of range/LOS: drive a clamped approach toward a point at ~85%
            // heal range on the CAMP side of the target. Own the tick (don't let
            // the camp slot yank it straight back) and use MOVEMENT_COMBAT so it
            // wins over any stale combat mover.
            Position const approach =
                DcPullPlanner::ComputeHealApproach(bot, healTarget, camp, healRange);
            if (bot->GetExactDist(&approach) > DC_PULL_SLOT_RADIUS)
            {
                DcMoveTo(bot->GetMapId(), approach.GetPositionX(), approach.GetPositionY(),
                       approach.GetPositionZ(), /*idle*/ false, /*react*/ false,
                       /*normal_only*/ false, /*exact_waypoint*/ false,
                       MovementPriority::MOVEMENT_COMBAT);
                DC_PULL_TRACE("[DC:{}] healer: clamped approach to heal target", bot->GetName());
                return true;
            }
            // At the clamped point but still no LOS/range (corner): hold, yield for
            // a possible cast — never push past the clamp toward the pack.
            DcMovement::StopBot(bot, DcMovement::Stop::Soft);
            return false;
        }
        // Nobody needs healing — fall through to the normal camp-slot pin so the
        // healer holds at / returns to camp like any other held follower.
    }

    // Park at the leader's camp. Each follower aims for its own fuzzed slot — a
    // deterministic 1-2yd offset off the shared anchor, snapped to the navmesh —
    // so the party fans out instead of stacking on one identical point. Settle on
    // the slot with a tight tolerance (the wide hold radius would let the bot stop
    // before the variance ever showed); fall back to the anchor when the slot
    // probe failed (slot == camp).
    Position const slot = DcPullPlanner::ComputeCampSlot(bot, camp);
    float const toCamp = bot->GetExactDist(&slot);
    if (toCamp <= DC_PULL_SLOT_RADIUS)
    {
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);
        // A waiting party watches their tank work: face the LEADER (not the
        // pack) once parked — never while still walking to camp.
        DcFaceIfNeeded(bot, DcLeaderSignal::FindLeaderTank(bot));
        DC_PULL_TRACE("[DC:{}] hold-at-camp: parked ({:.1f}yd, passive={})",
                      bot->GetName(), toCamp, passive);
        // During a holding phase (the tank is tagging) OWN the tick so nothing can
        // break the hold while the pull is live. While merely camped between pulls
        // (scout phase, passive==false) YIELD so the party can rest / loot at camp
        // — the multiplier suppresses wander / follow / self-pull for a camp-held
        // follower, so yielding here can't let it drift off toward the tank. A
        // healer's in-place / clamped-approach healing is handled in the dedicated
        // block above and returns before reaching here; once nobody needs a heal it
        // falls through to this camp pin like any other held follower.
        return passive;
    }
    // Priority matters the moment the party is already IN COMBAT when a pull
    // commits — e.g. a dynamic LEEROY->ADVANCED upgrade that lands after the tank
    // (and the following party) already ran into the pack. During a holding phase
    // (passive) the party MUST retreat to the camp even mid-fight, so move at
    // MOVEMENT_COMBAT — the same priority the tank's drag-back uses — otherwise a
    // MOVEMENT_NORMAL move loses to the stock combat MoveChase generator already
    // installed on the engaged follower and it just DPSes the pack where it stands
    // (the "party didn't run back, chaos" case). While merely scouting between pulls
    // (passive == false, usually out of combat) NORMAL is right: it must NOT stomp
    // the loot/rest pipeline the party runs at camp.
    MovementPriority const prio = passive ? MovementPriority::MOVEMENT_COMBAT
                                          : MovementPriority::MOVEMENT_NORMAL;
    bool const moved =
        DcMoveTo(bot->GetMapId(), slot.GetPositionX(), slot.GetPositionY(), slot.GetPositionZ(),
               /*idle*/ false, /*react*/ false, /*normal_only*/ false,
               /*exact_waypoint*/ false, prio);
    DC_PULL_TRACE("[DC:{}] hold-at-camp: walking to camp ({:.1f}yd, passive={}, moved={})",
                  bot->GetName(), toCamp, passive, moved);
    return true;
}

bool DungeonClearAssistCampActionBase::Execute(Event /*event*/)
{
    Player* leader = DcLeaderSignal::FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;

    // Nearest live unit the leader is meleeing — the pulled pack. LOS-blind on
    // purpose: this action exists precisely for the case the drag-back parked the
    // pack out of the camp's line of sight, where the stock target picker never
    // acquires it. getAttackers() covers the melee pack the tank is holding; the
    // leader's victim is the fallback for the (rare) all-ranged grab.
    Unit* target = nullptr;
    float bestDist = 0.0f;
    for (Unit* a : leader->getAttackers())
    {
        if (!a || !a->IsAlive() || a->GetMapId() != bot->GetMapId())
            continue;
        if (!bot->IsValidAttackTarget(a))
            continue;
        float const d = bot->GetExactDist2d(a);
        if (!target || d < bestDist)
        {
            target = a;
            bestDist = d;
        }
    }
    if (!target)
        target = leader->GetVictim();

    // No concrete target resolvable (e.g. brief threat-table gap): close on the
    // leader instead, which puts us back in sight of whatever it is fighting so
    // stock combat can pick the target up the moment we round the corner.
    Unit* const moveTo = target ? target : static_cast<Unit*>(leader);

    if (target)
    {
        // Commit the follower to the pack even without LOS: select it, publish it
        // as the current target, and open a combat timer so the bot is in the
        // fight (and in the combat engine) the instant movement carries it back
        // into sight — instead of standing idle at camp.
        bot->SetSelection(target->GetGUID());
        context->GetValue<Unit*>("current target")->Set(target);
        if (!bot->IsInCombat())
            bot->SetInCombatWith(target);

        // Already in sight of the pack: don't lurch the body onto the mob. The
        // SetInCombatWith above flips the bot into the combat engine next tick,
        // where its own rotation/heal logic (un-suppressed there, unlike the stock
        // proactive pickers DC mutes for followers) handles positioning. This is
        // the case the non-combat assist now covers that it used to drop: an idle
        // follower WITH line of sight that DC's multiplier otherwise leaves stuck.
        if (bot->IsWithinLOSInMap(target))
        {
            DC_PULL_TRACE("[DC:{}] assist camp: in sight of pack ({:.1f}yd) -> "
                          "committed + forced combat, rotation takes over",
                          bot->GetName(), bot->GetExactDist(target));
            return true;
        }
    }

    // Band the approach priority exactly as EngageDirect does. COMBAT priority
    // can't be interrupted by the bot's combat reflexes, so on a LONG assist run
    // (the follower is far behind because the tank scouted/Leeroy-charged well
    // ahead, or it is out of LOS at the far end of a corridor) an unconditional
    // COMBAT move makes the follower plow straight through every pack it aggros
    // en route without stopping to fight — dragging a mob train to the far point
    // and wiping the group. Use COMBAT only for the final close approach; past
    // that, NORMAL, so the follower stops and fights what it pulls on the way in
    // (stock combat takes the intervening pack; the assist re-fires and resumes
    // once it dies — a controlled leapfrog instead of a runaway charge).
    float const distance = bot->GetExactDist(moveTo);
    float const attackRange = botAI->IsMelee(bot)
        ? (bot->GetCombatReach() + moveTo->GetCombatReach() + 1.0f)
        : (botAI->GetRange("spell") - CONTACT_DISTANCE);
    MovementPriority const prio =
        (distance <= attackRange + DC_COMBAT_APPROACH_RANGE)
            ? MovementPriority::MOVEMENT_COMBAT
            : MovementPriority::MOVEMENT_NORMAL;

    DC_PULL_TRACE("[DC:{}] assist camp: closing on out-of-LOS fight ({:.1f}yd, "
                  "target={}, prio={})", bot->GetName(), distance,
                  target ? target->GetGUID().ToString() : "leader",
                  prio == MovementPriority::MOVEMENT_COMBAT ? "combat" : "normal");

    DcMoveTo(moveTo->GetMapId(), moveTo->GetPositionX(), moveTo->GetPositionY(),
           moveTo->GetPositionZ(), /*idle*/ false, /*react*/ false,
           /*normal_only*/ false, /*exact_waypoint*/ false, prio);
    return true;
}

bool DungeonClearRegroupCombatAction::Execute(Event /*event*/)
{
    // The leader tank, non-null only on an active run (gated again by the trigger).
    Player* tank = AI_VALUE(Player*, "dungeon clear party tank");
    if (!tank || tank == bot)
        return false;

    // Close on the tank, but stop a few yards short rather than stacking on its
    // cell — enough to restore line of sight and heal/cast range without crowding
    // it (or piling a ranged class into melee/AoE). Pathing rounds corners, which
    // is exactly what regains a stranded healer's sight of its heal target. Once
    // the bot is back inside the leash (and, for a healer, back in LOS) the trigger
    // goes inert and stock combat rotation owns the tick again.
    constexpr float standoff = 5.0f;
    float const dist = bot->GetExactDist2d(tank);

    float x = tank->GetPositionX();
    float y = tank->GetPositionY();
    float const z = tank->GetPositionZ();
    if (dist > standoff)
    {
        float const frac = (dist - standoff) / dist;
        x = bot->GetPositionX() + (tank->GetPositionX() - bot->GetPositionX()) * frac;
        y = bot->GetPositionY() + (tank->GetPositionY() - bot->GetPositionY()) * frac;
    }

    // Band the priority like EngageDirect / the camp assist: COMBAT only for the
    // final close approach, NORMAL beyond. An unconditional COMBAT regroup runs a
    // stranded follower (a healer that lost LOS while the tank pushed far ahead)
    // straight through any packs between it and the tank without stopping to
    // fight — the same plow-through runaway. NORMAL on the long leg lets it break
    // off and clear what it aggros, then resume regrouping once that mob dies.
    float const toDest = bot->GetExactDist2d(x, y);
    MovementPriority const prio =
        (toDest <= DC_COMBAT_APPROACH_RANGE)
            ? MovementPriority::MOVEMENT_COMBAT
            : MovementPriority::MOVEMENT_NORMAL;

    DC_PULL_TRACE("[DC:{}] regroup: closing on tank {} ({:.1f}yd, los={}, prio={})",
                  bot->GetName(), tank->GetName(), dist,
                  bot->IsWithinLOSInMap(tank) ? 1 : 0,
                  prio == MovementPriority::MOVEMENT_COMBAT ? "combat" : "normal");

    DcMoveTo(tank->GetMapId(), x, y, z, /*idle*/ false, /*react*/ false,
           /*normal_only*/ false, /*exact_waypoint*/ false, prio);
    return true;
}

bool DungeonClearHealRepositionAction::Execute(Event /*event*/)
{
    // The most-hurt heal target (LOS-blind, tank-biased). Stored as a GUID (like
    // the pull target), resolved live here. Re-read (trigger/action gap); bail if
    // it healed up or died in between.
    ObjectGuid const targetGuid = AI_VALUE(ObjectGuid, "dungeon clear heal target");
    if (targetGuid.IsEmpty())
        return false;
    Unit* target = ObjectAccessor::GetUnit(*bot, targetGuid);
    if (!target || !target->IsAlive())
        return false;

    Map* map = bot->GetMap();
    if (!map)
        return false;

    float const healRange = botAI->GetRange("heal");
    Position const targetPos = target->GetPosition();
    float const tx = targetPos.GetPositionX();
    float const ty = targetPos.GetPositionY();
    float const tz = targetPos.GetPositionZ();

    // Stand a little INSIDE heal range so a step of target movement doesn't drop us
    // straight back out. Floor at 5yd so we never try to stack on the target.
    float const standoff = std::max(5.0f, healRange * 0.6f);

    // Ring of standoff points around the target, ordered bot-side first. Take the
    // first that snaps to the navmesh, has LOS to the target, sits within heal
    // range, and is PATHFIND_NORMAL reachable.
    std::vector<Position> const cands =
        DungeonClearMath::HealStandoffCandidates(targetPos, bot->GetPosition(),
                                                 standoff, /*ringPoints*/ 7);

    constexpr float kEyeBump = 2.0f;  // eye height for the LOS ray (cf. ChordClear)
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    bool haveDest = false;
    for (Position const& c : cands)
    {
        NavmeshSnap::Result const snap =
            NavmeshSnap::Snap(map, c.GetPositionX(), c.GetPositionY(), tz, 8.0f);
        if (!snap.ok)
            continue;

        float const sdx = snap.x - tx;
        float const sdy = snap.y - ty;
        if (std::sqrt(sdx * sdx + sdy * sdy) > healRange)
            continue;

        if (!map->isInLineOfSight(snap.x, snap.y, snap.z + kEyeBump, tx, ty,
                                  tz + kEyeBump, bot->GetPhaseMask(),
                                  LINEOFSIGHT_CHECK_VMAP,
                                  VMAP::ModelIgnoreFlags::Nothing))
            continue;

        PathGenerator gen(bot);
        gen.CalculatePath(snap.x, snap.y, snap.z, /*forceDest*/ false);
        if (gen.GetPathType() != PATHFIND_NORMAL)
            continue;

        dx = snap.x;
        dy = snap.y;
        dz = snap.z;
        haveDest = true;
        break;
    }

    // Fallback: no sampled point validated (tight geometry, snap misses). Close
    // straight on the target with pathfinding — rounding corners is what regains
    // LOS — stopping 5yd short, exactly the old regroup behaviour.
    if (!haveDest)
    {
        float const dist = bot->GetExactDist2d(target);
        dx = tx;
        dy = ty;
        dz = tz;
        if (dist > 5.0f)
        {
            float const frac = (dist - 5.0f) / dist;
            dx = bot->GetPositionX() + (tx - bot->GetPositionX()) * frac;
            dy = bot->GetPositionY() + (ty - bot->GetPositionY()) * frac;
        }
    }

    // Band the priority like the assist/regroup actions: COMBAT only on the final
    // close leg, NORMAL beyond, so a long run back stops to fight what it aggros
    // instead of plowing a mob train to the target.
    float const toDest = bot->GetExactDist2d(dx, dy);
    MovementPriority const prio =
        (toDest <= DC_COMBAT_APPROACH_RANGE)
            ? MovementPriority::MOVEMENT_COMBAT
            : MovementPriority::MOVEMENT_NORMAL;

    DC_PULL_TRACE("[DC:{}] heal reposition: closing on heal target {} "
                  "({:.1f}yd, los={}, sampled={}, prio={})",
                  bot->GetName(), target->GetGUID().ToString(),
                  bot->GetExactDist2d(target),
                  bot->IsWithinLOSInMap(target) ? 1 : 0, haveDest ? 1 : 0,
                  prio == MovementPriority::MOVEMENT_COMBAT ? "combat" : "normal");

    DcMoveTo(map->GetId(), dx, dy, dz, /*idle*/ false, /*react*/ false,
           /*normal_only*/ false, /*exact_waypoint*/ false, prio);
    return true;
}

bool DungeonClearLeaderAssistAction::Execute(Event /*event*/)
{
    // Leader-side assist. The trigger guarantees: this bot IS the leader, it is out
    // of combat with no visible target of its own, and a groupmate is (latched) in
    // combat with a pack the tank never saw. Find what the party is fighting, take
    // threat, and move onto it. The inverse of the follower assist, which finds
    // what the LEADER is fighting and drives the follower to it.
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    // Nearest hostile attacking an in-combat groupmate (the pack a follower pulled
    // around the corner), plus the nearest in-combat member as a fallback move-to
    // so the tank at least rounds the corner back into sight when no concrete
    // attacker resolves (brief threat-table gap). LOS-blind on purpose: the whole
    // point is the fight the tank's own sight-gated picker can't reach.
    Unit* target = nullptr;
    float bestTargetDist = 0.0f;
    Player* nearestFighter = nullptr;
    float bestFighterDist = 0.0f;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive())
            continue;
        if (member->GetMapId() != bot->GetMapId() || !member->IsInCombat())
            continue;

        float const md = bot->GetExactDist2d(member);
        if (!nearestFighter || md < bestFighterDist)
        {
            nearestFighter = member;
            bestFighterDist = md;
        }

        // Everything meleeing this groupmate, plus its own victim — the pack we
        // must peel onto the tank.
        for (Unit* a : member->getAttackers())
        {
            if (!a || !a->IsAlive() || a->GetMapId() != bot->GetMapId())
                continue;
            if (!bot->IsValidAttackTarget(a))
                continue;
            float const d = bot->GetExactDist2d(a);
            if (!target || d < bestTargetDist)
            {
                target = a;
                bestTargetDist = d;
            }
        }
        if (!target)
        {
            Unit* const victim = member->GetVictim();
            if (victim && victim->IsAlive() && victim->GetMapId() == bot->GetMapId() &&
                bot->IsValidAttackTarget(victim))
            {
                target = victim;
                bestTargetDist = bot->GetExactDist2d(victim);
            }
        }
    }

    // No concrete target and nobody resolvable to walk toward — let the rest of the
    // driving ladder (advance / stall) run.
    if (!target && !nearestFighter)
        return false;

    Unit* const moveTo = target ? target : static_cast<Unit*>(nearestFighter);

    // Take threat ONLY once the pack is in sight. Force-combating while still out of
    // LOS would flip the tank into its combat engine next tick — where THIS
    // (non-combat) trigger goes inert and the approach would stall mid-corner — and
    // stock combat can't reliably chase a target it can't see. So while out of LOS
    // we just keep walking toward the fight (the trigger re-fires every tick and
    // drives us on); the instant we round the corner into sight we commit, and stock
    // combat / the tank rotation close the final gap and hold the pack.
    if (target && bot->IsWithinLOSInMap(target))
    {
        bot->SetSelection(target->GetGUID());
        context->GetValue<Unit*>("current target")->Set(target);
        if (!bot->IsInCombat())
            bot->SetInCombatWith(target);
        DC_PULL_TRACE("[DC:{}] leader assist: in sight of party fight ({:.1f}yd) "
                      "-> took threat, combat engine takes over",
                      bot->GetName(), bot->GetExactDist(target));
        return true;
    }

    // Band the approach exactly as the follower assist / regroup do: COMBAT only on
    // the final close leg, NORMAL beyond, so on a long run back the tank stops and
    // fights anything it pulls en route instead of plowing a mob train to the fight.
    float const distance = bot->GetExactDist(moveTo);
    float const attackRange = botAI->IsMelee(bot)
        ? (bot->GetCombatReach() + moveTo->GetCombatReach() + 1.0f)
        : (botAI->GetRange("spell") - CONTACT_DISTANCE);
    MovementPriority const prio =
        (distance <= attackRange + DC_COMBAT_APPROACH_RANGE)
            ? MovementPriority::MOVEMENT_COMBAT
            : MovementPriority::MOVEMENT_NORMAL;

    DC_PULL_TRACE("[DC:{}] leader assist: closing on party fight ({:.1f}yd, "
                  "target={}, prio={})", bot->GetName(), distance,
                  target ? target->GetGUID().ToString() : "groupmate",
                  prio == MovementPriority::MOVEMENT_COMBAT ? "combat" : "normal");

    DcMoveTo(moveTo->GetMapId(), moveTo->GetPositionX(), moveTo->GetPositionY(),
           moveTo->GetPositionZ(), /*idle*/ false, /*react*/ false,
           /*normal_only*/ false, /*exact_waypoint*/ false, prio);
    return true;
}
