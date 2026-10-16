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

#include "Creature.h"
#include "DBCStores.h"
#include "Ai/Dungeon/DungeonClear/Util/DcFormation.h"
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
#include "SmartEnum.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/HealLeashRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcHazard.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPlayerbotCompat.h"
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
#include "Ai/Dungeon/DungeonClear/Util/DcPartyState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPathWorker.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Data/FightInPlaceRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
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
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Util/DcPlayerbotsConfig.h"

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

    // A TRANSIT OWNS THE COLUMN — follow-tank stands down for it. A true return
    // means "return false, this tick is not yours".
    //
    // Two rungs cannot both position the column, and during a transit they aim at
    // points that are nowhere near each other. DungeonClearTransitPackTrigger
    // (relevance 36) walks a follower to within `TransitPackLeash - margin` of the
    // LEADER'S PUBLISHED ROUTE CURSOR; this action (25) walks it to a crumb `lag`
    // yards behind the TANK. On an ordinary leg those coincide, because the tank
    // is at its own cursor. On the Blackwing Lair Suppression Rooms crossing it
    // does not: the Suppression Device aura is -80% movement speed and the tank
    // eats it almost continuously while followers spend much of the leg outside a
    // bubble, so the tank falls a MEASURED median 21yd — p90 31, max 48 — behind
    // the cursor it is publishing (tr-20260828-183508-4, 310 driver ticks; 80 of
    // them beyond the 25yd pack leash on their own).
    //
    // What that produced: the pack rung walks a follower forward to the cursor
    // band and, the instant it is inside, goes inert by design ("the fine
    // positioning inside the pack stays the raid strategy's") and hands the tick
    // down to follow-tank — which walks it 17yd back down the corridor to the
    // snared tank, out of the band again, so the pack rung re-arms and walks it
    // forward. In that run 114 follower moves inside the crossing corridor went
    // BACKWARDS along the route; the trail branch below was logging on every one
    // of them (80% of its moves went backwards, median 17.4yd), while the pack
    // rung as the sole mover went backwards 0 times in 57 and the tank 0 in 10.
    // That is the reported "the raid walks back through the suppression room over
    // and over"; it is followers-only because a leader never trails itself.
    //
    // It also starved the crossing it was undoing. The driver's advance gate is a
    // quorum on the followers' distance to the CURSOR, so dragging them off the
    // cursor is precisely what kept `pack trailing` holding 217 of 310 ticks.
    //
    // Standing down does not strand anyone: outside the band the pack rung
    // outranks this action anyway, and inside it the right behaviour is to hold
    // and fight rather than retrace 17yd through a 30s-respawn whelp field.
    // Returning false (not true) yields the tick to the rotation / rest / loot
    // pipeline exactly as the in-bubble scout-lag branch below does.
    bool StandDownForTransit(Player* bot)
    {
        Position anchor;
        if (!bot || !DcLeaderSignal::GetTransitAnchor(bot, anchor))
            return false;

        // Same loose end StandDownForRezzer clears, for the same reason: this
        // action installs a PERSISTENT MoveFollow generator, and with it no longer
        // executing nothing else cancels it — it would keep driving the follower
        // back to the tank underneath the pack rung's point moves. Clear it once.
        // Self-limiting: after this the active generator is the pack walk itself,
        // so the guard is false on every later tick and an in-flight transit glide
        // is never chopped (the driver deliberately avoids a stop packet per tick
        // of a two-minute elite fight; so must this).
        MotionMaster* mm = bot->GetMotionMaster();
        if (mm && mm->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        return true;
    }

    // THE ELECTED REZZER MUST BE ABLE TO WALK — every mover on this bot stands
    // down for it. Call at the top of a follower rung; a true return means
    // "return false, this tick is not yours".
    //
    // Relevance alone does not buy this. The rez rung outranks the follower stack
    // (RezParty 31.5 > AssistCamp 29 > HoldAtCamp 28 > FollowTank 25), but a rung
    // only keeps the tick while it succeeds, and heal-reposition (41) outranks the
    // rez rung outright. So the follower stack still gets ticks during a recovery,
    // and every one of them can undo the approach: scout-lag's in-the-bubble branch
    // calls StopBot(Hold), which tears the approach spline down, and heal-reposition
    // walks the healer BACK toward the tank for line of sight — in
    // tr-20260807-080834-115 that carried the elected rezzer from 81.9yd to 87.0yd
    // away from the body it was supposed to reach, and scout-lag then pinned it
    // there for 99 seconds until the recovery timed out and killed a run with four
    // members alive.
    //
    // Gated on IsElectedRezzer, which mirrors the rez trigger exactly — so this is
    // one bot, out of combat, for the length of one recovery. Mid-fight the healer
    // repositions normally; the rez rung is not armed then either.
    bool StandDownForRezzer(Player* bot)
    {
        if (!bot || !DcRezRecovery::IsElectedRezzer(bot))
            return false;

        // One loose end the stand-down would otherwise leave running: follow-tank
        // installs a PERSISTENT MoveFollow generator, and with follow-tank no longer
        // executing there is nobody left to cancel it — it would keep driving the
        // rezzer back to the tank underneath the rez rung's point moves. Clear it
        // the once. Self-limiting: after this the active generator is the approach
        // itself, so the guard is false on every later tick and an in-flight
        // approach glide is never touched.
        MotionMaster* mm = bot->GetMotionMaster();
        if (mm && mm->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        return true;
    }
}

bool DungeonClearFollowTankAction::Execute(Event /*event*/)
{
    // A recovery in progress and this bot is the one walking to the corpse:
    // following the tank is exactly the wrong thing to do. See StandDownForRezzer.
    if (StandDownForRezzer(bot))
        return false;

    ObjectGuid& followedTank =
        context->GetValue<ObjectGuid>(DcKey::FollowedTank)->RefGet();

    Player* tank = AI_VALUE(Player*, DcKey::PartyTank);
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
                     bot->GetName(), DcPlayerbotCompat::IsSelfBot(bot) ? 1 : 0);
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
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().lootYieldStartMs;
    bool const lootYield =
        AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) || AI_VALUE(bool, DcKey::Stock::CanLoot);
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

    // A transit is driving the column to a published cursor: every branch below
    // positions against the TANK instead, and while the tank is snared behind its
    // own cursor those two answers fight each other tick by tick. See
    // StandDownForTransit. Placed after the loot-policy upkeep above so a
    // follower's give-up timers keep advancing across the crossing (five minutes
    // of whelp corpses is exactly when an unfinishable one gets camped).
    if (StandDownForTransit(bot))
    {
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] follow-tank: standing down, the transit owns the column",
                  bot->GetName());
        return false;
    }

    // ON FOOT THROUGH THE JOUST: the lance and the horse are mod-playerbots' walk,
    // not ours. `toc mount` (64) outranks this rung but only keeps the ticks it
    // SUCCEEDS on, and its MoveTo fails every tick the walk to the horse is still
    // in flight — so without this the follower was turned back to the tank on each
    // of those ticks and never reached a horse farther away than follow distance
    // (see DcTocDriver::FollowerMountsItself). Clear a MoveFollow left from before
    // the stand-down, the same once-only teardown as the rezzer's; the walk to the
    // horse is a point move, so the guard never touches it.
    if (TocFollowerMountsItself(bot))
    {
        followedTank = tank->GetGUID();
        DcFollowerLifecycle::MarkFollowing(bot->GetGUID());
        MotionMaster* mm = bot->GetMotionMaster();
        if (mm && mm->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        return false;
    }

    // MOUNTED: FOLLOW WITH THE HORSE, NEVER THE PASSENGER. Every mover below acts
    // on the bot's own MotionMaster — the persistent MoveFollow, the trail glide's
    // escort spline — and on a vehicle the bot is the passenger: its position is
    // the seat's, and a generator on it walks nothing. Trial of the Champion puts
    // the whole party on horses for the joust, and a follower left to those
    // branches micro-steps in the saddle while its horse stands at the wall.
    //
    // DcMoveTo takes MovementAction::MoveTo's vehicle branch, which drives the
    // vehicle BASE when the seat has CAN_CONTROL (and refuses when it does not,
    // which is the right answer for a seat that cannot steer). Same golden-angle
    // fan as the Follow() slot at the bottom, so the riders spread the same way
    // the walkers do; inside the bubble the tick is yielded, as Follow()'s own
    // in-range early-out does.
    if (bot->GetVehicle())
    {
        // THE OCULUS: every rider flies its own drake on its own lane (the rider
        // rung), to a pad the tank may not even have reached yet. Following the
        // tank here would pull the drake off its lane every tick.
        if (bot->GetMapId() == DcOculus::MAP_ID)
            return false;

        followedTank = tank->GetGUID();
        DcFollowerLifecycle::MarkFollowing(bot->GetGUID());

        // A MoveFollow installed on foot, before the mount, would otherwise stay
        // on the passenger for the whole ride.
        MotionMaster* mm = bot->GetMotionMaster();
        if (mm && mm->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            mm->Clear();

        float const riderDist = std::min<float>(DC_PB_CONFIG(FollowDistance, followDistance), 6.0f);
        if (bot->GetExactDist(tank) <= riderDist + kTrailArrival)
            return false;

        float const riderAngle = Position::NormalizeOrientation(
            static_cast<float>(bot->GetGUID().GetCounter()) * 2.39996323f);
        bool const moved = DcMoveTo(tank->GetMapId(),
                                    tank->GetPositionX() + std::cos(riderAngle) * riderDist,
                                    tank->GetPositionY() + std::sin(riderAngle) * riderDist,
                                    tank->GetPositionZ(), false, false, /*normal_only=*/false);
        Unit* const horse = bot->GetVehicleBase();
        return moved || (horse && horse->isMoving());
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
        // The spread the TANK IS ACTUALLY ENFORCING, not the PartyMaxSpread setting.
        // GetSpreadGate overrides that setting — waived while a maneuver holds,
        // tightened on a sealed-encounter final approach — so clamping against the raw
        // value can order this follower to hold outside the gate the tank is waiting
        // on, and neither side ever moves. That is precisely the deadlock this clamp
        // exists to prevent, one indirection further out; see
        // DcPartyState::LeaderEffectiveMaxSpread for the live case
        // (tr-20260803-134213-2, 365+ "advance yielding: party not ready" against
        // "scout-lag: holding at trail point (18.2yd behind tank, lag 15.0)").
        float const maxSpread = DcPartyState::LeaderEffectiveMaxSpread(bot);
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
        // probeReachable=false: this resolve exists to MEASURE the crumb (the
        // arrival hold below), and the two branches that then move — the glide and
        // the single-point step — each run their own reachability gate on the
        // ground they actually use. Probing here bought a second full Detour query
        // per follower per tick whose answer was thrown away.
        Position trailPoint;
        if (DcLeaderSignal::GetLeaderScoutTrailPoint(bot, lag, trailPoint,
                                                     /*probeReachable=*/false))
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
            // Reached only when the glide window was too short / unreachable —
            // which is where the PROBED resolve belongs: this is the one branch
            // that walks the follower to the crumb, so it re-resolves with the
            // reachability / zone-line gate on (falling back to a farther-back
            // crumb when the exact-lag point is across a seam, exactly as before).
            if (DcLeaderSignal::GetLeaderScoutTrailPoint(bot, lag, trailPoint) &&
                DcMoveTo(bot->GetMapId(), trailPoint.GetPositionX(),
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
    float const dist = std::min<float>(DC_PB_CONFIG(FollowDistance, followDistance), 6.0f);

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
        // 3D, NOT 2D — the same metric everything else in this rung uses (the
        // crumb spacing is walked 3D distance, the arrival hold below is
        // GetExactDist, the tank's own spread gate is GetDistance). A 2D read
        // here shrinks with the cosine of the slope, so on a ramp the leash
        // arms at a LARGER true separation than on the flat and the correction
        // the rung then issues is correspondingly bigger. Identical reasoning
        // to the scout-lag branch above, which was fixed and this one was not.
        float const toTank = bot->GetExactDist(tank);
        // Only trail once the tank is beyond the tight follow bubble — i.e. a
        // real corridor traversal is involved, not a fan-out shuffle. Below this
        // the Follow() fan below keeps the cluster tight in healer LOS.
        float const trailEngage = dist + 2.0f;
        // Per-bot stagger so the column spreads single-file ALONG the
        // centered trail rather than every follower targeting the one crumb
        // at `dist` behind the tank and piling onto it. Stable per GUID, same
        // spirit as the golden-angle fan below but projected onto the trail.
        uint32 const slot = static_cast<uint32>(bot->GetGUID().GetCounter()) % 4u;
        float const lag = dist + static_cast<float>(slot) * 3.0f;
        // Past the leash AND past our own stagger slot, so the crumb we aim at is
        // genuinely ahead of us — a catch-up, never a retreat. The `toTank > lag`
        // half is what stops this rung gliding a follower BACKWARD down the trail
        // and handing it straight back to the Follow() fan below, which is the
        // ping-pong the party does behind a moving tank (worst on ramps, where the
        // two legs are down and back up the incline). Live numbers and the full
        // reasoning: DungeonClearMath::TrailFollowShouldEngage.
        if (DungeonClearMath::TrailFollowShouldEngage(toTank, trailEngage, lag))
        {
            Position trailPoint;
            // Skip the trail when the chosen crumb is one we already occupy: re-
            // issuing MoveTo to a point we're basically on micro-steps in place
            // (the scout-lag "two steps forward, two back" dance). Let Follow()
            // take it — it early-outs cleanly when in range.
            //
            // probeReachable=false: same reason as the scout-lag resolve above —
            // this answer is only MEASURED here, and both movers below gate their
            // own ground. This ran on every trailing tick of every follower.
            if (DcLeaderSignal::GetLeaderScoutTrailPoint(bot, lag, trailPoint,
                                                         /*probeReachable=*/false) &&
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
                // when the glide window was too short / unreachable — the one
                // branch here that MOVES to the crumb, so it re-resolves with the
                // probe on.
                if (DcLeaderSignal::GetLeaderScoutTrailPoint(bot, lag, trailPoint) &&
                    DcMoveTo(bot->GetMapId(), trailPoint.GetPositionX(),
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
    //
    // RAID: the subgroup picks one of eight ring segments and the seed fans
    // within it, and the follow distance grows with the same-map population —
    // 40 bots on one golden spiral at 5-man distance is a conga line whose
    // tail laps the corridor (DcFormation::RaidFollowAngle).
    uint32 const seed = static_cast<uint32>(bot->GetGUID().GetCounter());
    float angle = Position::NormalizeOrientation(static_cast<float>(seed) * 2.39996323f);
    float followDist = dist;
    if (bot->GetMap() && bot->GetMap()->IsRaid())
    {
        angle = DcFormation::RaidFollowAngle(bot->GetSubGroup(), seed);
        uint32 sameMap = 0;
        if (Group* group = bot->GetGroup())
            for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
                if (gref->GetSource() && gref->GetSource()->GetMapId() == bot->GetMapId())
                    ++sameMap;
        followDist = std::max(followDist, DcFormation::RingRadius(sameMap, followDist));
    }
    return Follow(tank, followDist, angle);
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
    // The camp is not this bot's business while it owes the party a rez — a camp
    // pin is one more mover cancelling the approach. See StandDownForRezzer.
    if (StandDownForRezzer(bot))
        return false;

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

    // SCRIPTED PULL: nobody may be INSIDE THE ROOM, ever.
    //
    // A radius is the wrong shape for this on its own, and the live runs proved it:
    // the leash let a follower drift the whole 12yd toward the room before anything
    // reacted, and it spent that drift logging "parked" — i.e. YIELDING the tick to
    // the very chase that was carrying it — because it was still inside the radius.
    // What the plan forbids is a PLACE, and the fight-in-place row already names it
    // exactly: the same box that keeps the pull pipeline out of Selin's room keeps
    // the party out of it. An in-room follower is recalled regardless of how far
    // camp happens to be; the radius below only handles ordinary drift on safe
    // ground.
    bool const scriptedCamp = DcLeaderSignal::IsLeaderScriptedPullActive(bot);
    bool const inNoGoRoom =
        scriptedCamp && FightInPlaceRegistry::IsNoPullZone(
                            bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY());

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
            context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().lootYieldStartMs;
        bool const lootYield =
            AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) || AI_VALUE(bool, DcKey::Stock::CanLoot);
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
        context->GetValue<ObjectGuid>(DcKey::FollowedTank)->Set(ObjectGuid::Empty);
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
        Unit* const healTarget = AI_VALUE(Unit*, DcKey::Stock::PartyToHeal);
        uint8 const lowestPct = AI_VALUE2(uint8, DcKey::Stock::Health, DcKey::Stock::PartyToHeal);
        if (healTarget && lowestPct < DC_PB_CONFIG(AlmostFullHealth, almostFullHealth))
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
    // SCRIPTED PULL camp FIGHT: anchored, but on a LEASH rather than the tight slot
    // pin. A follower here is fighting — it needs room to close on whatever reached
    // the camp, step out of a cleave, take a heal angle — and pinning it to a 2yd
    // slot mid-fight would fight its own rotation every tick.
    //
    // DC_SCRIPTED_PULL_FOLLOWER_LEASH, and it is deliberately TIGHTER than the tank's
    // (see both constants): the tank plants ON the camp and the pack piles onto it
    // there, so a follower has less legitimate ground to cover than the tank does.
    // Borrowing the tank's number would just be drift — every extra yard is one a
    // follower spends logging "parked" and yielding the tick to the chase that is
    // carrying it, and it arrives at the doorway before anything objects. What sizes
    // it is the ground-effect STEP-OUT, not melee reach; the constant's own comment
    // carries that derivation.
    //
    // BUT ONLY DURING THE FIGHT (!passive). While the tank is away tagging there is
    // nothing to fight and nothing to leave room for, and a radius is not a place: a
    // follower stops the instant it crosses the boundary, so it settles in a SHELL at
    // the leash distance, on whichever side it walked in from. That is fine when the
    // camp is a few yards behind the tank and invisible in the noise. It is not fine
    // for an authored camp reached down a corridor — every follower stops short, on
    // the corridor side, and the party stands the full leash away from the point the
    // row names, in different cover, facing a different sight-line.
    //
    // Live (tr-20260803-121459-1): the row said (134.14, -14.36). Every passive tick
    // logged "parked" at 6.0-7.9yd and never once inside, and the party actually
    // stood at (139.79, -7.66) — 8.8yd out, up the corridor. The reported
    // symptom was "the party camped nowhere near the coords I gave you", and it was
    // exactly right: the TANK's camp was the authored point, the party's was not.
    // While passive, therefore, they take the ordinary tight slot pin like any other
    // held follower, which is what puts them on the coordinate.
    Position const slot = DcPullPlanner::ComputeCampSlot(bot, camp);
    float const toCamp = bot->GetExactDist(&slot);
    // The scripted camp FIGHT — anchored, not held. Everything below that treats
    // "inside the radius" as "settled and waiting" is wrong here, because the bot is
    // mid-fight.
    bool const campFight = scriptedCamp && !passive;

    // AND STRETCHED, WHEN THE LEASH AND THE SHOT ARE MUTUALLY EXCLUSIVE. A camp fight
    // whose live target stands further from the camp than this follower can reach from
    // inside the leash is a fight the follower is forbidden to take part in: it walks
    // out, gets in range, is recalled, and is out of range again on arrival. See
    // ScriptedFollowerReachLeash for the measurement and for what bounds the stretch.
    //
    // Off the follower's OWN current target rather than anything global: that is the
    // mob its rotation is actually pointed at (the assist rung seeds it, an instance
    // kill order may have overridden it), so it is the only distance that decides
    // whether standing at the camp means fighting or watching. No target, a dead one,
    // or a keep-out room -> the plain leash, unchanged.
    float followerLeash = DC_SCRIPTED_PULL_FOLLOWER_LEASH;
    if (campFight && !inNoGoRoom)
    {
        if (Unit* const fightTarget =
                context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Get())
        {
            if (fightTarget->IsAlive() && fightTarget->GetMapId() == bot->GetMapId() &&
                bot->IsValidAttackTarget(fightTarget))
            {
                float const reach =
                    botAI->IsMelee(bot)
                        ? (bot->GetCombatReach() + fightTarget->GetCombatReach() + 1.0f)
                        : botAI->GetRange("spell");
                followerLeash = ScriptedFollowerReachLeash(
                    fightTarget->GetExactDist(&camp), reach);
                if (followerLeash > DC_SCRIPTED_PULL_FOLLOWER_LEASH)
                    DC_PULL_TRACE("[DC:{}] hold-at-camp: leash stretched to {:.1f}yd — "
                                  "{} is {:.1f}yd off the camp and our reach is {:.1f}",
                                  bot->GetName(), followerLeash,
                                  fightTarget->GetGUID().ToString(),
                                  fightTarget->GetExactDist(&camp), reach);
            }
        }
    }
    float const parkRadius = campFight ? followerLeash : DC_PULL_SLOT_RADIUS;
    if (toCamp <= parkRadius && !inNoGoRoom)
    {
        // IN BOUNDS DURING A CAMP FIGHT IS NOT "PARKED". The stop-and-face pair below
        // is the WAITING party's behaviour: settle in place and watch the tank work.
        // Run it on a fighting follower and it lands on every tick the bot is inside
        // the leash — StopMoving cancelling the step it just took toward its mob or
        // out of a cleave, and a facing packet turning it off its target and back
        // onto the tank. That is a bot twitching in place rather than one holding a
        // position, and it is the other half of what the player saw: the leash
        // supplies the ping-pong, this supplies the never-settles.
        //
        // Yield instead. In bounds and fighting means the hold has nothing to do —
        // the bot's own rotation and the avoid-aoe rungs own the tick, and the leash
        // below is still there for the moment it actually leaves.
        if (campFight)
        {
            DC_PULL_TRACE("[DC:{}] hold-at-camp: in bounds mid-fight ({:.1f}yd) -> "
                          "yielding to the rotation", bot->GetName(), toCamp);
            DcPullContext& held = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
            held.campHoldBest = 0.0f;
            held.campHoldClosest = 0.0f;
            held.campHoldRunaway = false;
            return false;
        }
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);
        // A waiting party watches their tank work: face the LEADER (not the
        // pack) once parked — never while still walking to camp.
        DcFaceIfNeeded(bot, DcLeaderSignal::FindLeaderTank(bot));
        DC_PULL_TRACE("[DC:{}] hold-at-camp: parked ({:.1f}yd, passive={})",
                      bot->GetName(), toCamp, passive);
        // Settled: re-arm the losing-ground ratchet and the runaway latch for the
        // next recall.
        DcPullContext& settled = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
        settled.campHoldBest = 0.0f;
        settled.campHoldClosest = 0.0f;
        settled.campHoldRunaway = false;
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
    // A scripted camp fight needs COMBAT priority for the same reason `passive`
    // does, and more urgently: the follower is IN combat with a stock MoveChase
    // generator already installed and aimed at a mob still inside the room, so a
    // MOVEMENT_NORMAL recall loses to it outright and the leash never bites.
    MovementPriority const prio = (passive || scriptedCamp)
                                      ? MovementPriority::MOVEMENT_COMBAT
                                      : MovementPriority::MOVEMENT_NORMAL;
    // LOSING GROUND. The recall re-issues the SAME destination every tick, and
    // DcMoveTo dedupes on destination — so once another generator has the bot, the
    // recall reports "already going there", issues nothing, and the follower sails
    // away logging moved=false. The standing-still backstop below cannot see it
    // either, because the bot is moving perfectly well; just outward.
    //
    // Live (tr-20260802-234400-1): a PASSIVE healer was carried from 1.7yd to 15yd
    // with moved=false on every tick, and a rogue drifted 0 -> 11.9yd while its own
    // action logged "parked" each step. Same ratchet the tank's drag-back uses:
    // give up ground against the best-so-far and the leg is hard-cancelled so the
    // next MoveTo genuinely re-issues.
    DcPullContext& ownPull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

    // A FOLLOWER STANDING IN FIRE IS ALLOWED TO BE OUT OF BOUNDS. Same call the tank's
    // camp leash makes and for the same reason (see DcInGroundEffect): the recall and
    // the step-out are both right, they want different places, and the recall is the
    // one that gives because the step-out is answering damage. Re-arm the ratchet on
    // the way past — no leg is in flight to measure — and let the walk home resume the
    // tick the bot is clear. The keep-out room is the exception: no ground effect
    // makes standing in the unpulled pack's room acceptable.
    if (campFight && !inNoGoRoom && DcInGroundEffect(context))
    {
        DC_PULL_DEBUG("[DC:{}] hold-at-camp: {:.1f}yd out but standing in a ground "
                      "effect -> yielding to the step-out rather than walking back "
                      "into it", bot->GetName(), toCamp);
        ownPull.campHoldBest = 0.0f;
        return false;
    }

    // A NEW CAMP IS A NEW RECALL. The advanced pull moves the camp as the tank
    // closes, and every campHold* distance was measured against the old one.
    if (std::hypot(camp.GetPositionX() - ownPull.campHoldKeyX,
                   camp.GetPositionY() - ownPull.campHoldKeyY) > DC_PULL_SLOT_RADIUS)
    {
        ownPull.campHoldKeyX = camp.GetPositionX();
        ownPull.campHoldKeyY = camp.GetPositionY();
        ownPull.campHoldBest = 0.0f;
        ownPull.campHoldClosest = 0.0f;
        ownPull.campHoldRunaway = false;
    }

    // A RECALL THAT RUNS AWAY IS STOOD DOWN, NOT RE-ISSUED. See CampRecallRanAway:
    // the ratchet below re-bases outward on every loss, so a recall whose own path
    // leads away from the slot is re-issued onto that same path indefinitely.
    if (!ownPull.campHoldRunaway)
    {
        if (ownPull.campHoldClosest <= 0.0f || toCamp < ownPull.campHoldClosest)
            ownPull.campHoldClosest = toCamp;
        else if (CampRecallRanAway(ownPull.campHoldClosest, toCamp))
        {
            ownPull.campHoldRunaway = true;
            DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
            DcMovement::ClearMovementWait(bot);

            // What was the recall actually walking? Never logged before, so the
            // one live case (tr-20260927-184653-9) could not say why a 10yd recall
            // went down a floor. One Detour query, on a latch that fires once.
            PathGenerator probe(bot);
            probe.CalculatePath(slot.GetPositionX(), slot.GetPositionY(), slot.GetPositionZ(),
                                /*forceDest*/ false);
            Movement::PointsArray const& pts = probe.GetPath();
            float pathLen = 0.0f;
            float lowZ = bot->GetPositionZ();
            for (std::size_t i = 0; i < pts.size(); ++i)
            {
                lowZ = std::min(lowZ, pts[i].z);
                if (i)
                    pathLen += (pts[i] - pts[i - 1]).length();
            }
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] hold-at-camp: recall RAN AWAY ({:.1f}yd from the slot, "
                     "closest {:.1f}) -> stood down, holding here | bot ({:.1f},{:.1f},{:.1f}) "
                     "slot ({:.1f},{:.1f},{:.1f}) camp ({:.1f},{:.1f},{:.1f}) | path type {} "
                     "{} pts {:.1f}yd, lowest z {:.1f}",
                     bot->GetName(), toCamp, ownPull.campHoldClosest, bot->GetPositionX(),
                     bot->GetPositionY(), bot->GetPositionZ(), slot.GetPositionX(),
                     slot.GetPositionY(), slot.GetPositionZ(), camp.GetPositionX(),
                     camp.GetPositionY(), camp.GetPositionZ(),
                     static_cast<uint32>(probe.GetPathType()), pts.size(), pathLen, lowZ);
        }
    }
    if (ownPull.campHoldRunaway)
    {
        // Held where the runaway was caught — the same return as "parked": own the
        // tick while the tank is tagging so nothing else drifts us, yield between
        // pulls so rest, loot and the tank-follow (which walks the tank's own
        // breadcrumbs, not a fresh path) carry on.
        DC_PULL_TRACE("[DC:{}] hold-at-camp: recall stood down after a runaway "
                      "({:.1f}yd out)", bot->GetName(), toCamp);
        return passive;
    }

    if (ScriptedPullLostGround(ownPull.campHoldBest, toCamp))
    {
        DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
        DcMovement::ClearMovementWait(bot);
        DC_PULL_DEBUG("[DC:{}] hold-at-camp: losing ground ({:.1f}yd vs best {:.1f}) "
                      "-> cancelled whatever is carrying us and re-issuing | bot "
                      "({:.1f},{:.1f},{:.1f}) slot ({:.1f},{:.1f},{:.1f})",
                      bot->GetName(), toCamp, ownPull.campHoldBest, bot->GetPositionX(),
                      bot->GetPositionY(), bot->GetPositionZ(), slot.GetPositionX(),
                      slot.GetPositionY(), slot.GetPositionZ());
        ownPull.campHoldBest = toCamp;
    }
    else if (toCamp < ownPull.campHoldBest || ownPull.campHoldBest <= 0.0f)
        ownPull.campHoldBest = toCamp;

    bool const moved =
        DcMoveTo(bot->GetMapId(), slot.GetPositionX(), slot.GetPositionY(), slot.GetPositionZ(),
               /*idle*/ false, /*react*/ false, /*normal_only*/ false,
               /*exact_waypoint*/ false, prio);
    DC_PULL_TRACE("[DC:{}] hold-at-camp: walking to camp ({:.1f}yd, passive={}, moved={}, "
                  "inRoom={})", bot->GetName(), toCamp, passive, moved, inNoGoRoom);

    // BACKSTOP — the recall was refused, the bot is standing still, and a stale
    // EQUAL-priority movement block is what is holding it down. The arbiter
    // only yields to a STRICTLY greater priority, so a combat mover that grabbed
    // this follower a moment ago (the camp assist seeding a target and flipping it
    // to the combat engine is the usual culprit) silently starves every recall tick
    // for the rest of its budget — and the action returns true either way, so it
    // looks perfectly healthy in the log while the follower drifts.
    //
    // Live (Magisters' Terrace, tr-20260802-233048-11): a hunter logged
    // "walking to camp ... moved=false" from 13.6yd out to 21.1yd, interleaved with
    // "move REFUSED and not moving -> IsWaitingForLastMove ... prio=3", and walked
    // into the room. The tank's drag-back and camp recall both carry this backstop
    // already; the follower hold never did.
    if (!moved && !bot->isMoving() && DcMoveDeferred(prio))
    {
        DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
        DcMovement::ClearMovementWait(bot);
        DC_PULL_DEBUG("[DC:{}] hold-at-camp: recall refused while standing at "
                      "{:.1f}yd -> cleared the stale movement wait",
                      bot->GetName(), toCamp);
    }

    return true;
}

namespace
{
    // The mob the PARTY is actually fighting, for a follower to assist onto. The old
    // lookup only considered the LEADER's attacker list + victim — which is EMPTY
    // whenever the pack is ranged, or the mobs fixated a DPS instead of the tank, so
    // the tank holds threat with nothing meleeing it. In that (common) case the old
    // code fell back to "move onto the leader", orbiting the tank at 0.0yd doing
    // nothing (proven live: `target=leader` at 0.0yd while assistWanted=1). Resolve
    // from the WHOLE fight instead:
    //   1. anything attacking the leader / the leader's own victim (the held pack), then
    //   2. the nearest hostile any NEARBY in-combat groupmate is fighting (their
    //      attackers + victim) — the mobs that fixated the DPS.
    // Returns the nearest valid, reachable such hostile, or nullptr → the caller
    // STANDS DOWN (never orbits the leader). Members are gated within 1.5x
    // PartyMaxSpread of the tank so this stays "the tank's fight", not a far skirmish.
    //
    // "REACHABLE" was a promise this comment made and the code did not keep: there
    // was no level test at all. While the party's own pack is alive that costs
    // nothing, because it is nearer than anything else and wins the rank. The
    // moment the pack DIES the rank re-resolves over whatever is still flagged in
    // combat with the group — and if that is a mob on another floor, this returned
    // it instead of returning nullptr. The caller then closes on it (the no-LOS
    // branch of AssistCampActionBase drives straight at the mob) and, because a
    // Player is handed PATHFIND_NORMAL even where no route exists, walks into the
    // geometry.
    //
    // Live on map 469 (tp-20260828-142623-1, 4 of 5 runs, 0/5 killed Broodlord):
    // the corridor pack died at 14:36:25, this picked a Blackwing Warlock standing
    // on the drake-hall floor 24.5yd STRAIGHT UP two ticks later, and the raid
    // crossed from the Broodlord approach hall at z 424.5 into Firemaw's room at
    // z 449.3 in eleven seconds.
    //
    // Note the rank is deliberately left in 2D. Ranking in 3D does NOT separate
    // these — the overhead Warlock is 29.8yd away against 49.5yd for the corridor
    // pack on our own floor, so it wins either metric. Only reachability tells
    // them apart, and this is the one place in the module that was missing it: the
    // ledge fix (fix/ledge-target-path-distance) wired IsLevelReachable into all
    // four DcTargeting pickers and none of the followers' own.
    Unit* PickPartyFightTarget(Player* bot, AiObjectContext* context, Player* leader)
    {
        Unit* best = nullptr;
        float bestDist = 0.0f;
        auto consider = [&](Unit* u)
        {
            if (!u || !u->IsAlive() || u->GetMapId() != bot->GetMapId())
                return;
            if (!bot->IsValidAttackTarget(u))
                return;
            float const d = bot->GetExactDist2d(u);
            if (best && d >= bestDist)
                return;
            // CHEAPEST LAST: only a candidate that would actually WIN the rank pays
            // for the probe, and the same-level answer is one float compare — so a
            // fight on one floor, which is nearly every fight in nearly every
            // dungeon, never reaches PathGenerator at all.
            //
            // MEMOISED, because the expensive case is not the rare one: the loop
            // below ranks every attacker of every groupmate within 1.5x
            // PartyMaxSpread, and in the state this probe was added for — a mob
            // formation overhead flagging a forty-man through the floor — that is a
            // dozen Detour queries, on every follower, on every tick, for the same
            // dozen candidates. The answer cannot change inside one tick.
            if (!DcTickMemoAccess::LevelReachable(bot, context, u))
                return;
            best = u;
            bestDist = d;
        };
        // EVERY HOLDER, NOT JUST THE MELEE ONES. getAttackers() lists units whose
        // CURRENT VICTIM is `src`; a mob that tagged the party from range and then
        // stood off is in none of it, while its CombatReference holds the whole
        // party flagged forever (instanced creatures never leash). That fight is
        // real, it is what the party is stuck in, and resolving nullptr for it is
        // what sends the caller to a fallback that cannot end.
        //
        // Live on map 604 (tr-20260830-195435-2 / -6): three Drakkari Raiders
        // (29982) dismount from the Gal'darah-causeway rhino stampede, hold all
        // five members at 16-26yd without ever closing, and the teardown reads
        // `attackers=0 victim=- held by Drakkari Raider(29982) 16.5yd 100%
        // reachable -> LEGITIMATE` for nine minutes.
        auto considerFrom = [&](Unit* src)
        {
            if (!src)
                return;
            std::vector<Unit*> holders;
            DcTargeting::CollectCombatHolders(src, holders);
            for (Unit* h : holders)
                consider(h);
        };

        // 1. The leader's own fight takes priority (the pack it is holding).
        considerFrom(leader);
        if (best)
            return best;

        // 2. Else whatever a nearby in-combat groupmate is fighting.
        if (Group* grp = bot->GetGroup())
        {
            float const reach = DcSettings::GetFloat(bot, "PartyMaxSpread") * 1.5f;
            for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            {
                Player* m = ref->GetSource();
                if (!m || m == bot || !m->IsAlive() || !m->IsInCombat())
                    continue;
                if (m->GetMapId() != bot->GetMapId())
                    continue;
                if (leader && m->GetExactDist2d(leader) > reach)
                    continue;  // keep it the tank's fight, not a far straggler's
                considerFrom(m);
            }
        }
        return best;
    }
}

bool DungeonClearAssistCampActionBase::Execute(Event /*event*/)
{
    Player* leader = DcLeaderSignal::FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;

    // A live, usable target the bot ALREADY has wins outright — an instance
    // strategy's kill order (MgT's focus values) seeds one, and overwriting it
    // with nearest-attacker-of-tank every tick made this action fight that
    // strategy: the bot's target ping-ponged once per second and the rotation
    // ran on neither pick (tp-20260806-212646-1, 39% of the focus churn). Bound
    // to the tank's fight radius so a stale far-away target can't hijack the
    // assist; anything outside falls through to the fresh pick below.
    Unit* target = nullptr;
    if (Unit* cur = context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Get())
    {
        // Same level gate as the fresh pick below. A target already in the slot —
        // seeded by an instance strategy's focus value, or held over from before
        // the party changed floors — must not keep its grip merely by being there;
        // that is the one route left by which an unreachable mob could still reach
        // the approach code.
        if (cur->IsAlive() && cur->GetMapId() == bot->GetMapId() &&
            bot->IsValidAttackTarget(cur) &&
            cur->GetExactDist2d(leader) <=
                DcSettings::GetFloat(bot, "PartyMaxSpread") * 1.5f &&
            DcTickMemoAccess::LevelReachable(bot, context, cur))
            target = cur;
    }

    // Resolve a REAL mob the party is fighting from the WHOLE group (see
    // PickPartyFightTarget). No concrete hostile resolves -> STAND DOWN. We
    // deliberately no longer fall back to "move onto the tank to gain LOS": that
    // dragged ranged DPS up the tank's back (the reported regression) and orbited a
    // looting tank at 0.0yd. When nothing resolves, follow-tank / the combat engine
    // own the bot; the fight target re-resolves within a tick or two once the tank's
    // attacker list populates.
    if (!target)
    {
        target = PickPartyFightTarget(bot, context, leader);
        if (!target)
            return false;

        // Seed the fight target so BOTH engines have a valid "current target":
        // select it, publish it, and open a combat window so the bot is in the
        // fight the instant it reaches range/sight. This target is on the TANK —
        // never on this bot's own attacker list — so without seeding, stock
        // target-acquisition finds nothing while the pack is out of sight.
        bot->SetSelection(target->GetGUID());
        context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(target);
    }
    if (!bot->IsInCombat())
        bot->SetInCombatWith(target);

    // This action is registered under two names — "...assist camp combat" runs in the
    // COMBAT engine, "...assist camp" in the NON-combat engine. Do NOT key this off
    // bot->IsInCombat(): the whole bug is the limbo where the bot is combat-FLAGGED
    // yet still ticking the non-combat engine (no target of its own), so IsInCombat
    // lies about the engine.
    bool const inCombatEngine = getName().find("combat") != std::string::npos;

    // NON-combat side (the limbo): flip into the combat engine IMMEDIATELY — do NOT
    // wait for LOS/range. Once there, the bot's rotation runs and the combat-side
    // assist below drives the approach. The DC drop-target suppressor
    // (DungeonClearMultiplier) keeps the out-of-LOS seed from bouncing the bot right
    // back out (drop target rel 99 >> reach rel 20 would win every tick otherwise).
    // This is the flip-early model: the party enters combat WITH the tank and drives
    // on the mob, not on the human/tank.
    if (!inCombatEngine)
    {
        botAI->ChangeEngine(BOT_STATE_COMBAT);
        DC_PULL_TRACE("[DC:{}] assist camp: seeded {} -> flip to combat engine",
                      bot->GetName(), target->GetGUID().ToString());
        return true;
    }

    // --- COMBAT engine ---------------------------------------------------------
    float const dist = bot->GetExactDist(target);
    float const range = botAI->IsMelee(bot)
        ? (bot->GetCombatReach() + target->GetCombatReach() + 1.0f)
        : (botAI->GetRange("spell") - CONTACT_DISTANCE);

    // HAVE LINE OF SIGHT -> hand positioning to STOCK combat; do NOT drive our own
    // move. Stock "reach spell" stops a ranged bot at its spell range (it goes inert
    // once inside range), and "reach melee" closes a melee bot to melee — the correct
    // per-class standoff. Driving DcMoveTo toward the mob here is exactly what marched
    // ranged DPS into melee even with a clear shot (the reported bug): our move aims
    // at the mob's feet and only our own in-range test could stop it. So:
    //   * in range: ENGAGE (face + Attack commits the victim so the rotation fires —
    //     SetInCombatWith alone never swings/casts), then YIELD (return false) so the
    //     rotation runs THIS tick; returning true would starve it at rel 35.
    //   * out of range: just YIELD — stock reach spell/melee (rel 20) closes to the
    //     right distance and stops. We re-arm next tick until in range.
    //
    // The in-range test MUST use the metric the stock reach action enforces, or the
    // handoff opens a dead band and the follower never engages at all. ReachSpell is
    // built with GetRange("spell") but tests it via `!IsWithinCombatRange(target,
    // dist)` (ReachTargetActions.cpp), and IsWithinCombatRange ADDS
    // GetCombatReach(bot) + GetCombatReach(target) (~3yd) to the threshold. So stock
    // stops closing at ~spellDistance + 3, while a bare `dist <= spellDistance -
    // CONTACT_DISTANCE` here demands ~3.5yd closer than stock will ever walk. A
    // ranged DPS parked in that gap is "in range, stop moving" to stock and "out of
    // range, yield to stock" to us — nobody acts and it stands there until the tank's
    // spread gate deadlocks the run (observed live: pinned at 29.4-29.7yd for
    // hundreds of ticks). Mirroring stock's own predicate makes the two windows a
    // clean partition with no gap, and keeps them in sync if SpellDistance is retuned.
    //
    // Melee is deliberately left on its own threshold: reachSum + 1.0 is already
    // WIDER than stock reach-melee's stop point (reachSum + MeleeDistance, 0.75), so
    // that side overlaps rather than gapping — which is why only ranged ever hung.
    bool const inAttackRange = DungeonClearMath::IsWithinAssistAttackRange(
        botAI->IsMelee(bot), dist, /*meleeRange*/ range,
        /*spellRange*/ botAI->GetRange("spell"),
        /*combatReachSum*/ bot->GetCombatReach() + target->GetCombatReach());

    if (bot->IsWithinLOSInMap(target))
    {
        if (inAttackRange)
        {
            if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, target))
                ServerFacade::instance().SetFacingTo(bot, target);
            bot->Attack(target, botAI->IsMelee(bot));
            DC_PULL_TRACE("[DC:{}] assist camp: engaged {} in range+LOS ({:.1f}yd) -> yield",
                          bot->GetName(), target->GetGUID().ToString(), dist);
        }
        else
        {
            DC_PULL_TRACE("[DC:{}] assist camp: LOS, out of range ({:.1f}yd) -> yield to "
                          "stock reach", bot->GetName(), dist);
        }
        return false;
    }

    // NO LINE OF SIGHT: close on the MOB (never the tank) to round the corner and
    // regain sight — the one case stock combat can't handle (its reach is distance-
    // only). Band the approach exactly as EngageDirect does — COMBAT priority can't be
    // interrupted by the bot's combat reflexes, so on a LONG assist run an
    // unconditional COMBAT move plows through every pack it aggros en route. COMBAT
    // only for the final close approach; past that NORMAL, so the follower stops and
    // fights what it pulls on the way in.
    MovementPriority const prio =
        (dist <= range + DC_COMBAT_APPROACH_RANGE)
            ? MovementPriority::MOVEMENT_COMBAT
            : MovementPriority::MOVEMENT_NORMAL;

    bool const closing =
        DcMoveTo(target->GetMapId(), target->GetPositionX(), target->GetPositionY(),
               target->GetPositionZ(), /*idle*/ false, /*react*/ false,
               /*normal_only*/ false, /*exact_waypoint*/ false, prio);

    DC_PULL_TRACE("[DC:{}] assist camp: no-LOS, closing on mob {} ({:.1f}yd, prio={}, "
                  "moved={})", bot->GetName(), target->GetGUID().ToString(), dist,
                  prio == MovementPriority::MOVEMENT_COMBAT ? "combat" : "normal", closing);

    return true;
}

bool DungeonClearRegroupCombatAction::Execute(Event /*event*/)
{
    // The leader tank, non-null only on an active run (gated again by the trigger).
    Player* tank = AI_VALUE(Player*, DcKey::PartyTank);
    if (!tank || tank == bot)
        return false;

    Map* map = bot->GetMap();
    if (!map)
        return false;

    bool const isHealer = PlayerbotAI::IsHeal(bot);
    bool const isMelee  = botAI->IsMelee(bot);

    // Anchor the reconnect. A healer PRE-POSITIONS on the tank (the goal is "be able
    // to heal the tank the moment damage starts", so the tank IS the anchor); a DPS
    // anchors on the fight the tank is holding, so it regains LOS on a target, not on
    // the tank's back. LeaderFightAnchor writes the tank's own position when no
    // concrete fight unit resolves, so anchorPos is always usable.
    Position anchorPos;
    Unit* anchorUnit = nullptr;
    if (isHealer)
        anchorPos = tank->GetPosition();
    else
        anchorUnit = DcTargeting::LeaderFightAnchor(bot, context, tank, anchorPos);

    // Role-range standoff ring. Ranged DPS: 0.8x spell range (inside range so target
    // wobble doesn't drop it back out). Healer: max(5, 0.6x heal range) — the same
    // band HealReposition parks a healer in, so the two never disagree. Melee has no
    // ring: it closes on the anchor (rounding the corner is what regains LOS), so it
    // falls through to the fractional approach below.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool sampled = false;
    if (!isMelee)
    {
        float ringRadius = 0.0f;
        float maxRadius = 0.0f;
        if (isHealer)
        {
            float const healRange = botAI->GetRange("heal");
            maxRadius = healRange;
            ringRadius = std::max(5.0f, healRange * 0.6f);
        }
        else
        {
            float const spellRange = botAI->GetRange("spell");
            maxRadius = spellRange;
            ringRadius = spellRange * 0.8f;
        }
        sampled = FindStandoffPoint(map, anchorPos, ringRadius, maxRadius, x, y, z);
    }

    // Fallback: no ring point validated (tight geometry, snap misses), or a melee.
    // Close on the anchor, stopping attackRange short so a ranged class doesn't
    // pile into the melee/AoE and a melee lands in swing range. The predicate
    // clears the instant a mob comes into sight, so this only ever walks far
    // enough to round the corner.
    //
    // Along the ROUTE to the anchor, never the straight line: see the heal
    // reposition's fallback and DcEngageGeometry::PathedCloseOn — the straight
    // line is how DPS walked into the Maiden hairpin's wall. No route onto the
    // anchor's floor -> hold.
    if (!sampled)
    {
        float const attackRange = isMelee
            ? (bot->GetCombatReach() + (anchorUnit ? anchorUnit->GetCombatReach() : 0.0f) + 1.0f)
            : std::max(5.0f, botAI->GetRange("spell") - CONTACT_DISTANCE);
        Position close;
        if (!DcEngageGeometry::PathedCloseOn(bot, anchorPos, attackRange, close))
        {
            DC_PULL_TRACE("[DC:{}] regroup: no route onto the anchor's floor ({:.1f}yd) "
                          "-> holding", bot->GetName(), bot->GetExactDist2d(&anchorPos));
            return false;
        }
        x = close.GetPositionX();
        y = close.GetPositionY();
        z = close.GetPositionZ();
    }

    // Re-issue guard: the trigger latches and re-fires every tick, but re-plotting a
    // near-identical spline each time stutters and clips casts (cf. spline-reissue
    // freeze). While already travelling toward within 3yd of the same point, own the
    // tick without touching the move. Owning it is correct HERE (unlike the no-op tail
    // below): a reconnect spline really is in flight, and the trigger only armed
    // because the bot has nothing in sight to shoot anyway — so nothing below is being
    // starved, and yielding would just let a lower mover fight this spline.
    if (bot->isMoving() && _lastDestValid &&
        _lastDest.GetExactDist2d(x, y) < 3.0f)
        return true;

    // Band the priority like EngageDirect / the camp assist: COMBAT only for the
    // final close approach, NORMAL beyond. An unconditional COMBAT regroup runs a
    // stranded follower straight through any packs between it and the anchor without
    // stopping to fight — the plow-through runaway. NORMAL on the long leg lets it
    // break off and clear what it aggros, then resume once that mob dies.
    float const toDest = bot->GetExactDist2d(x, y);
    MovementPriority const prio =
        (toDest <= DC_COMBAT_APPROACH_RANGE)
            ? MovementPriority::MOVEMENT_COMBAT
            : MovementPriority::MOVEMENT_NORMAL;

    DC_PULL_TRACE("[DC:{}] regroup: moving to standoff ({:.1f}yd, healer={}, "
                  "sampled={}, anchor={}, prio={})",
                  bot->GetName(), toDest, isHealer ? 1 : 0, sampled ? 1 : 0,
                  anchorUnit ? anchorUnit->GetGUID().ToString() : "tank",
                  prio == MovementPriority::MOVEMENT_COMBAT ? "combat" : "normal");

    // OWN THE TICK ONLY IF WE ACTUALLY MOVED. This action sits at rel 29, above stock
    // `reach spell` (20) and the entire class rotation (5-20), and Engine::DoNextAction
    // stops the tick at the first action that returns true — so an unconditional true
    // here is a full mute of the follower's damage for as long as the rung stays
    // latched. That is exactly what it was: when DcMoveTo declines (destination
    // unreachable, already there, movement throttled) the bot stood still AND cast
    // nothing, every tick, and since it never dealt damage it never gained the threat
    // that would have refilled `attackers` and released the latch. Yielding on a no-op
    // costs the reconnect nothing — the trigger stays latched and re-plots next tick —
    // and hands the tick back to the rotation, which is the whole point of the rung
    // sitting below stock combat movement.
    if (!DcMoveTo(map->GetId(), x, y, z, /*idle*/ false, /*react*/ false,
                  /*normal_only*/ false, /*exact_waypoint*/ false, prio))
        return false;

    _lastDest = Position(x, y, z, 0.0f);
    _lastDestValid = true;
    return true;
}

bool DungeonClearHealRepositionAction::Execute(Event /*event*/)
{
    // The elected rezzer is usually the healer (the election prefers healers), and
    // this rung outranks the rez rung — 41 vs 31.5 — so left ungated it takes the
    // tick outright and walks toward the tank for line of sight while the corpse it
    // is meant to reach lies the other way. Out of combat there is no heal worth
    // that; the rez is the recovery. See StandDownForRezzer (which is false in
    // combat, so the combat-engine copy of this rung is untouched).
    if (StandDownForRezzer(bot))
        return false;

    // The most-hurt heal target (LOS-blind, tank-biased). Stored as a GUID (like
    // the pull target), resolved live here. Re-read (trigger/action gap); bail if
    // it healed up or died in between.
    ObjectGuid const targetGuid = AI_VALUE(ObjectGuid, DcKey::HealTarget);
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

    // Stand a little INSIDE heal range so a step of target movement doesn't drop us
    // straight back out. Floor at 5yd so we never try to stack on the target.
    float const standoff = std::max(5.0f, healRange * 0.6f);

    // First ring point around the target that snaps, sits within heal range, has
    // LOS, and is reachable (shared with the combat regroup — see FindStandoffPoint).
    // Camp leash, on HealLeashRegistry maps only, and only while the healer is
    // fighting at a standing pull camp (the trigger has already deferred to the
    // passive holding phases). Everywhere else leashRadius stays 0 and nothing
    // below changes.
    Position camp;
    bool campPassive = false;
    float const leashRadius =
        (bot->IsInCombat() && DcLeaderSignal::GetLeaderCampHold(bot, camp, campPassive))
            ? HealLeashRegistry::Radius(map->GetId())
            : 0.0f;
    Position const* const leash = leashRadius > 0.0f ? &camp : nullptr;

    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    bool const haveDest =
        FindStandoffPoint(map, targetPos, standoff, healRange, dx, dy, dz, leash, leashRadius);

    // Fallback: no sampled point validated (tight geometry, snap misses). Close on
    // the target — rounding corners is what regains LOS — stopping 5yd short, at
    // a point ALONG THE ROUTE to it (DcEngageGeometry::PathedCloseOn).
    //
    // It used to be a point on the straight bot->target line. Across a wall that
    // point is in the wall or behind it, and the walk to it goes through: the
    // Maiden hairpin in Karazhan had healers walking into the wall toward a tank
    // on the other leg and dropping to the Servants' Quarters below
    // (tr-20260927-190943-12, "closing on heal target ... los=0, sampled=0"). With
    // no complete route onto the target's floor there is nowhere honest to walk,
    // so hold and let the stock heal stack work what is in sight.
    if (!haveDest)
    {
        Position close;
        if (!DcEngageGeometry::PathedCloseOn(bot, targetPos, /*stopShort*/ 5.0f, close))
        {
            DC_PULL_TRACE("[DC:{}] heal reposition: no route onto {}'s floor ({:.1f}yd, "
                          "los=0) -> holding", bot->GetName(),
                          target->GetGUID().ToString(), bot->GetExactDist2d(target));
            return false;
        }
        dx = close.GetPositionX();
        dy = close.GetPositionY();
        dz = close.GetPositionZ();

        // A leashed healer never takes the close past its leash: that walk is
        // exactly the one that carried it onto the Banquet Hall floor. Yield the
        // tick so the stock heal stack works whatever is in sight from the camp.
        if (leash && !HealLeashRegistry::WithinLeash(camp.GetPositionX(), camp.GetPositionY(),
                                                     leashRadius, dx, dy))
        {
            DC_PULL_TRACE("[DC:{}] heal reposition: leashed to camp ({:.0f}yd) -> holding, "
                          "heal target {} is {:.1f}yd out",
                          bot->GetName(), leashRadius, target->GetGUID().ToString(),
                          bot->GetExactDist2d(target));
            return false;
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

bool DungeonClearHazardVacateAction::Execute(Event /*event*/)
{
    // Sample the live hazard world ONCE and answer every question below from it.
    // Each DcHazard Player* entry point resolves three AiObjectContext values and
    // one ObjectAccessor lookup per guid; the fan loop below asks two of them per
    // candidate, so the unsampled form re-resolved the same unchanged world up to
    // twenty times per Execute on top of the NearestVacate the trigger just ran.
    // Nothing in this function can move an emitter, so one sample is the whole
    // tick's truth.
    DcHazard::LiveSet const live = DcHazard::Sample(bot);

    // The nearest emitter whose pulse the bot is in. Re-read here (trigger/action
    // gap); bail if it despawned or the bot cleared in between.
    DcHazard::VacateEmitter const danger =
        DcHazard::NearestVacate(live, bot->GetPositionX(), bot->GetPositionY(),
                                bot->GetPositionZ());
    if (!danger.ok)
    {
        _fleeToValid = false;
        return false;
    }

    Map* map = bot->GetMap();
    if (!map)
        return false;

    // COMMIT to one destination and ride it out. The trigger re-fires every tick
    // while the bot is in the band, and recomputing from scratch each time is what
    // turned this action into a thrash generator the moment a map had more than one
    // emitter in play: NearestVacate returns whichever pulse is momentarily closest,
    // so a step of drift re-elects a different centre and the away-bearing REVERSES.
    // tr-20260815-134844-5 logged four re-issues inside one second sending the tank
    // to (946.2,-279.2), then (968.0,-277.9) 22yd the other way, then back — net
    // travel about zero, in the middle of eight sludges. It died there.
    //
    // So while the committed point is still clean and the bot is still travelling
    // toward it, own the tick and DON'T touch the move. The re-plot is the bug.
    //
    // The commitment is TIME-CAPPED, because riding one is only safe as long as
    // the point deserved it. tr-20260815-154816-5 rode a single committed point
    // for 47 seconds — the candidate had passed "a path exists" while sitting on
    // the far side of a wall, so the walk to it left the room, went round, and
    // dragged the tank ~60yd with twelve sludges behind. The detour bound below is
    // what stops such a point being chosen at all; this cap is the backstop for
    // every other way a commitment can go stale (the emitter drifts, the bot
    // wedges) and keeps "committed" from ever meaning "unsupervised".
    uint32 const nowMs = getMSTime();
    if (_fleeToValid && bot->isMoving() &&
        GetMSTimeDiffToNow(_fleeSetAtMs) < DC_VACATE_COMMIT_MAX_MS &&
        bot->GetExactDist2d(&_fleeTo) > 3.0f &&
        !DcHazard::PointIsInVacateBand(live, _fleeTo.GetPositionX(), _fleeTo.GetPositionY(),
                                       _fleeTo.GetPositionZ()))
        return true;

    // Aim past the pulse rim by THIS row's slack — the Destroyed Sentinel's 6 to
    // leave-and-move-on, the Creeping Sludge's 9 to clear its wide hold band with
    // an arrival margin. Measured from the emitter centre.
    float const clearDist = danger.pulseRadius + danger.retreatSlack;

    // Bearing directly away from the emitter. If the bot is somehow exactly on
    // the centre (degenerate), fall back to its own facing so the direction is
    // still defined.
    float const ex = danger.x, ey = danger.y;
    float away = std::atan2(bot->GetPositionY() - ey, bot->GetPositionX() - ex);
    if (!std::isfinite(away) ||
        (bot->GetPositionX() == ex && bot->GetPositionY() == ey))
        away = bot->GetOrientation();

    // Try straight-away first, then fan the bearing out symmetrically so a wall
    // directly behind the bot doesn't trap it in the pulse — mirrors the
    // MoveAwayFromCreature sampler, but navmesh-validated + path-reachable like
    // the rest of DC. First candidate that snaps, lands outside the pulse, and
    // is reachable wins.
    static constexpr float kFan[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f, 2.4f, -2.4f, 3.14159f };

    // Best SECOND-CHOICE: a point that clears this emitter's own pulse and is a
    // short walk, but still sits in something else's band. Not a solution — the
    // trigger will fire again — but strictly better than standing here, and it is
    // what keeps a boxed-in bot moving now that the unvalidated last resort is
    // gone. Seeded with where the bot already stands, so it is only ever taken
    // when it genuinely opens distance from the emitter.
    float bestPartialClear = bot->GetExactDist2d(ex, ey);
    float bestPartialX = 0.0f, bestPartialY = 0.0f, bestPartialZ = 0.0f;
    bool  havePartial = false;

    for (float delta : kFan)
    {
        float const ang = away + delta;
        float const cx = ex + clearDist * std::cos(ang);
        float const cy = ey + clearDist * std::sin(ang);

        NavmeshSnap::Result const snap = NavmeshSnap::Snap(map, cx, cy, bot->GetPositionZ(), 12.0f);
        if (!snap.ok)
            continue;

        // The snap can pull the point back toward the emitter (onto the only
        // nearby mesh) — keep it only if it truly clears the pulse.
        float const dxp = snap.x - ex, dyp = snap.y - ey;
        float const fromEmitter = std::sqrt(dxp * dxp + dyp * dyp);
        if (fromEmitter < danger.pulseRadius)
            continue;

        // Record it as a fallback before the stricter screens below reject it.
        // Deliberately geometry-only here — the path probe is expensive and the
        // fallback is probed once, at the end, only if it is actually needed.
        if (fromEmitter > bestPartialClear + 2.0f)
        {
            bestPartialClear = fromEmitter;
            bestPartialX = snap.x;
            bestPartialY = snap.y;
            bestPartialZ = snap.z;
            havePartial = true;
        }

        // Don't flee this pulse straight into ANOTHER hazard's — Sentinels and
        // their summons sit in overlapping clusters on this map, and a Maraudon
        // sludge pack is nothing but overlap. PointIsHot covers every live
        // emitter's PLACEMENT keep-out (the fled emitter's own is only its raw
        // pulse, which this point already clears)...
        if (DcHazard::PointIsHot(live, snap.x, snap.y, snap.z))
            continue;

        // ...and this covers every live emitter's DANGER band, which is the one
        // that decides whether the trigger fires again. The two are different
        // predicates and a row whose hold band is wider than its placement radius
        // (Creeping Sludge: 11 vs 8) passes the first and fails the second. Landing
        // on such a point means fleeing again on arrival — forever. Reject it here
        // and let the fan keep looking.
        if (DcHazard::PointIsInVacateBand(live, snap.x, snap.y, snap.z))
            continue;

        // Reachable AND actually near: the path to this point must be commensurate
        // with the straight line to it. An unbounded reachability test cannot tell
        // "6yd away" from "6yd away through a wall, 60yd round" — both are
        // PATHFIND_NORMAL — and the second is not a retreat, it is a march. This is
        // the check that was missing in tr-20260815-154816-5.
        Position const cand(snap.x, snap.y, snap.z);
        float pathLen = 0.0f;
        if (!DcEngageGeometry::IsNavReachableWithin(bot, cand, DC_VACATE_DETOUR_RATIO,
                                                    DC_VACATE_DETOUR_SLACK, &pathLen))
            continue;

        DC_PULL_TRACE("[DC:{}] hazard vacate: clearing {:.0f}yd pulse, moving to "
                      "({:.1f},{:.1f}) delta={:.1f} straight={:.1f}yd path={:.1f}yd",
                      bot->GetName(), danger.pulseRadius, snap.x, snap.y, delta,
                      bot->GetExactDist2d(snap.x, snap.y), pathLen);
        DcMoveTo(map->GetId(), snap.x, snap.y, snap.z, /*idle*/ false, /*react*/ false,
                 /*normal_only*/ false, /*exact_waypoint*/ false,
                 MovementPriority::MOVEMENT_COMBAT);
        _fleeTo = Position(snap.x, snap.y, snap.z, 0.0f);
        _fleeToValid = true;
        _fleeSetAtMs = nowMs;
        return true;
    }

    // Nothing fully clears. Take the best partial — still in someone's band, but
    // further from THIS pulse and a short walk away — provided it is genuinely
    // short. Probed here rather than in the loop so the fallback costs one path
    // query, not ten.
    _fleeToValid = false;
    if (havePartial)
    {
        Position const partial(bestPartialX, bestPartialY, bestPartialZ);
        float pathLen = 0.0f;
        if (DcEngageGeometry::IsNavReachableWithin(bot, partial, DC_VACATE_DETOUR_RATIO,
                                                   DC_VACATE_DETOUR_SLACK, &pathLen))
        {
            DC_PULL_TRACE("[DC:{}] hazard vacate: no fully-clear bearing, taking partial "
                          "({:.1f},{:.1f}) {:.1f}yd from the {:.0f}yd pulse, path={:.1f}yd",
                          bot->GetName(), bestPartialX, bestPartialY, bestPartialClear,
                          danger.pulseRadius, pathLen);
            DcMoveTo(map->GetId(), bestPartialX, bestPartialY, bestPartialZ,
                     /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                     /*exact_waypoint*/ false, MovementPriority::MOVEMENT_COMBAT);
            return true;
        }
    }

    // Boxed in on every bearing. There used to be a last resort here that drove the
    // raw straight-away point through the mover regardless, on the reasoning that
    // trying to open distance beats standing in the pulse. That reasoning does not
    // survive contact with a wall: the mover pathfinds, so an unvalidated point
    // behind one produces exactly the long way round this action just spent ten
    // candidates rejecting — and it does it with no length bound at all, which is
    // the worst version of the tr-20260815-154816-5 march.
    //
    // So: yield the tick instead. The bot keeps fighting from where it stands and
    // the trigger re-fires next tick, by which time the emitters (or the bot) will
    // have moved and a bearing may open. Standing in a pulse for another tick is a
    // known, bounded cost; being walked out of the room is not.
    DC_PULL_TRACE("[DC:{}] hazard vacate: no bearing clears the {:.0f}yd pulse within "
                  "the detour bound — holding this tick",
                  bot->GetName(), danger.pulseRadius);
    return false;
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

        // Everything holding this groupmate in combat — the pack we must peel onto
        // the tank. CollectCombatHolders, not getAttackers(), for the reason it
        // documents: a RANGED OR STANDOFF TAGGER APPEARS IN NEITHER
        // getAttackers() NOR GetVictim(), so the old scan resolved nullptr for it
        // and dropped straight through to the groupmate fallback below — which
        // walks the tank at a flagged party member at MOVEMENT_COMBAT, claims the
        // tick at DcRel::LeaderAssist(24) over DcRel::Advance(15), and never
        // closes because the member is following the tank. Live on map 604
        // (tr-20260830-195435-2): 2m30s of
        //   leader assist: closing on party fight (13.9yd, target=groupmate, prio=combat)
        //   move REFUSED ... (dest 1914.8,743.7,136.5 at 45.3yd, prio=2)
        // — the boss approach starved by the assist, the tank pinned 27-42yd from
        // Gal'darah, and a boss that therefore never aggroed at all.
        //
        // Level-gated for the reason PickPartyFightTarget documents at length, and
        // it matters more here: the LEADER walks at whatever this resolves, so one
        // overhead pick takes the tank through the ceiling and the followers'
        // tethers bring the rest of the raid with it.
        std::vector<Unit*> holders;
        DcTargeting::CollectCombatHolders(member, holders);
        for (Unit* a : holders)
        {
            if (!a || !a->IsAlive() || a->GetMapId() != bot->GetMapId())
                continue;
            if (!bot->IsValidAttackTarget(a))
                continue;
            float const d = bot->GetExactDist2d(a);
            if (target && d >= bestTargetDist)
                continue;
            if (!DcTickMemoAccess::LevelReachable(bot, context, a))
                continue;
            target = a;
            bestTargetDist = d;
        }
    }

    // No concrete target and nobody resolvable to walk toward — let the rest of the
    // driving ladder (advance / stall) run.
    if (!target && !nearestFighter)
        return false;

    // THE GROUPMATE FALLBACK NEEDS SOMEWHERE TO GO. Walking at `nearestFighter`
    // when no attacker resolves is only defensible while the fight is somewhere
    // ELSE — the comment above says so: "the tank at least rounds the corner back
    // into sight". A flagged member standing ON the tank has no corner to round,
    // so the destination is the tank's own position, the mover refuses it, and the
    // unconditional `return true` at the bottom of this function burns the tick.
    // At DcRel::LeaderAssist (24) that starves DcRel::Advance (15) — one action
    // per tick — and the party never walks out of whatever is flagging it, so the
    // flag never clears either.
    //
    // Live: tp-20260828-175353-1, all five BWL raids. The party parked under the
    // upper suppression room, trash 24.3yd overhead flagged it through the floor
    // and went EVADING where it stood, IsLevelReachable (correctly) rejected every
    // holder, and the tank logged nothing but
    //   leader assist: closing on party fight (0.0yd, target=groupmate)
    //   move REFUSED ... (dest <own position> at 0.0yd)
    // for 3m17s, watchdogs all clear, until the operator aborted. Force-clearing
    // the phantom combat could not break it (59 clears in 3.5 min for one party):
    // the only escape is to WALK, and this rung is what was starving the walker.
    //
    // Bounded by the same distance the follower rungs treat as "already here", so
    // a genuine round-the-corner assist — the case this fallback exists for — is
    // untouched.
    if (!target && bestFighterDist <= DC_LEADER_ASSIST_COLOCATED_RANGE)
    {
        DC_PULL_TRACE("[DC:{}] leader assist: nothing reachable to assist and the "
                      "nearest flagged member is {:.1f}yd away -> yielding the tick "
                      "to the driving ladder",
                      bot->GetName(), bestFighterDist);
        return false;
    }

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
        context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(target);
        if (!bot->IsInCombat())
            bot->SetInCombatWith(target);
        // Engage for real, the way stock AttackAction does: switch to the combat
        // engine and start the attack. Only an attack switches the engine, and
        // a tank already combat-flagged by its party (no victim, nothing on it)
        // sat on the non-combat engine with this "succeeding" every tick and
        // nothing happening — while Raiders shot a DPS 23yd away (Pull Lab
        // gundrak-ranged-standoff). The combat engine's rotation, taunt and
        // reach take it from here.
        botAI->ChangeEngine(BOT_STATE_COMBAT);
        bot->Attack(target, botAI->IsMelee(bot));
        DC_PULL_TRACE("[DC:{}] leader assist: in sight of party fight ({:.1f}yd) "
                      "-> attacking, combat engine takes over",
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

    // Report the move's own verdict rather than discarding it. A refused move is a
    // tick this rung did not use, and claiming it anyway is what let the co-located
    // case above spin forever; the same claim on any other refusal (no path, a
    // duplicate order) would sit on the ladder just as hard.
    return DcMoveTo(moveTo->GetMapId(), moveTo->GetPositionX(), moveTo->GetPositionY(),
                    moveTo->GetPositionZ(), /*idle*/ false, /*react*/ false,
                    /*normal_only*/ false, /*exact_waypoint*/ false, prio);
}

namespace
{
    // The stock playerbots out-of-combat rez action per class (each subclasses
    // ResurrectPartyMemberAction and targets "party member to resurrect").
    // Druid battle-rez ("rebirth") is deliberately out of scope for v1 — this
    // rung only runs out of combat.
    char const* RezActionNameFor(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_PRIEST:  return "resurrection";
            case CLASS_PALADIN: return "redemption";
            case CLASS_SHAMAN:  return "ancestral spirit";
            case CLASS_DRUID:   return "revive";
            default:            return nullptr;
        }
    }

    // Comfortably inside the 30yd rez spell range, so a corpse a step beyond
    // the exact edge (or a snap on approach) never leaves the cast flapping
    // in and out of range.
    //
    // Deliberately NOT raised to the spell's 30yd. A rezzer seen orbiting a corpse
    // at 29-33yd is not a bot refusing a castable rez, it is a bot that failed to
    // WALK — see the movement priority below, which is the actual fix. Widening the
    // cast gate to the spell edge would only hide that, and would put the cast right
    // on the boundary where a single snap flaps it in and out of range.
    constexpr float DC_REZ_CAST_RANGE = 20.0f;

    // WHY the cast was refused — the one thing "not possible yet" never said.
    //
    // PlayerbotAI::CastSpell returns a bare bool and swallows every reason, so a
    // refusal reads identically whether the caster is combat-flagged, still gliding,
    // sitting, out of mana, out of LOS, or blocked by the instance. Four hundred
    // identical trace lines (tr-20260827-075417-165) narrow that to nothing, and
    // every candidate has a different fix. So name it, from both sides:
    //
    //   * the gates PlayerbotAI::CastSpell applies BEFORE it builds a spell at all —
    //     stand state and "moving with a cast time" — which never reach CheckCast and
    //     so leave no verdict of their own. Both are live here: every party rez is a
    //     multi-second cast, and this rung asks for a stop it may not have got yet
    //     ([[dc-stop-strength-noop-on-bots]]).
    //   * the core's own verdict, from the same probe CanCastSpell uses. Two codes
    //     matter and neither is visible any other way:
    //       SPELL_FAILED_AFFECTING_COMBAT             the caster is combat-FLAGGED.
    //         Every party rez carries SPELL_ATTR0_NOT_IN_COMBAT_ONLY_PEACEFUL
    //         (verified in Spell.dbc for all four), so the flag alone refuses it —
    //         and a bot can hold that flag while running the non-combat engine this
    //         rung lives on ([[pb-combat-flag-vs-combat-engine]]), which is exactly
    //         how a rung gated on !IsInCombat can still meet the refusal.
    //       SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED the INSTANCE refuses it while
    //         an encounter is in progress (Spell.cpp, "Xinef: exploit protection").
    //
    // INFO, not trace, because the question it answers is the first one asked of a
    // run that ends "couldn't get X resurrected in time". Throttled per bot — the
    // rung retries at tick rate — on the bot's own run state; see
    // Util/DcThrottle.h for why not in a file-scope thread_local map keyed by GUID.
    void RezRefusalDiag(PlayerbotAI* botAI, Player* bot, Player* target,
                        char const* rezAction)
    {
        if (DcRun::Of(botAI).Throttled(DcThrottle::RezRefusalLog, 3000))
            return;

        uint32 const spellId =
            botAI->GetAiObjectContext()->GetValue<uint32>("spell id", rezAction)->Get();
        SpellInfo const* const info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;

        char const* verdict = spellId ? "spell not in spellbook" : "no spell id for name";
        if (info)
        {
            // Same construction CanCastSpell uses for its probe: a throwaway Spell,
            // the unit target set, CheckCast(strict), delete. TRIGGERED_NONE so the
            // caster-state checks we are asking about are actually applied.
            Spell* probe = new Spell(bot, info, TRIGGERED_NONE);
            probe->m_targets.SetUnitTarget(target);
            SpellCastResult const result = probe->CheckCast(true);
            delete probe;
            verdict = EnumUtils::ToString(result).Constant;
        }

        uint32 const maxMana = bot->GetMaxPower(POWER_MANA);
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] rez party: '{}' on {} REFUSED \xe2\x80\x94 {} | flagged={} "
                 "moving={} standing={} mana={}% dist={:.1f}yd los={}",
                 bot->GetName(), rezAction, target->GetName(), verdict,
                 bot->IsInCombat() ? 1 : 0, bot->isMoving() ? 1 : 0,
                 bot->IsStandState() ? 1 : 0,
                 maxMana ? bot->GetPower(POWER_MANA) * 100 / maxMana : 0,
                 bot->GetExactDist(target), bot->IsWithinLOSInMap(target) ? 1 : 0);
    }
}

bool DungeonClearRezPartyAction::Execute(Event /*event*/)
{
    // Re-derive everything (the trigger's verdict is deterministic within the
    // tick): confirm this bot is still the elected rezzer and resolve the
    // target member kernel-index -> GUID -> Player at execution time.
    DcRezRecovery::Plan const plan = DcRezRecovery::Evaluate(bot);
    // PairedTargetFor resolves the corpse THIS bot is assigned: the single
    // election's target everywhere, or this bot's parallel pair on a raid run
    // (each paired rezzer walks to its own body). Empty = not a rezzer.
    ObjectGuid const targetGuid = DcRezRecovery::PairedTargetFor(plan, bot);
    if (plan.verdict.outcome != DcRezDecision::Outcome::Hold ||
        plan.verdict.reason != DcRezDecision::Reason::Recovering ||
        targetGuid.IsEmpty())
        return false;

    char const* rezAction = RezActionNameFor(bot->getClass());
    // Any other class was elected through the spell it knows (IsRezClass ->
    // KnownResurrectSpell); cast that one by name, the same lookup the four
    // stock names go through.
    if (!rezAction)
        if (SpellInfo const* known = DcRezRecovery::KnownResurrectSpell(bot))
            rezAction = known->SpellName[0];
    if (!rezAction)
        return false;  // election guarantees a rez class; belt-and-braces

    Player* target = ObjectAccessor::FindPlayer(targetGuid);
    if (!target || !target->isDead() || target->GetMapId() != bot->GetMapId())
        return false;  // vanished between trigger and action — re-elect next tick

    // FINISH THE REZ WE ALREADY LANDED.
    //
    // A completed resurrection does not raise anyone — it sends the corpse an
    // offer and waits for SMSG/CMSG_RESURRECT_RESPONSE. A bot has a session but no
    // client to answer that packet, so nothing ever accepts, and the corpse is left
    // holding a permanent offer. That is not merely a rez that did not land: it also
    // BLACKLISTS the corpse for good, because the stock target value filters on
    // `!player->isResurrectRequested()` (PartyMemberToResurrect.cpp:36). The rezzer
    // then stands over a body it can no longer see as a target, re-casting into a
    // refusal — "rez party: cast 'resurrection' ... not possible yet", four times a
    // second for 98 seconds in run tr-…-62 of the MgT audit — until the recovery
    // timeout disables the run.
    //
    // Accepting on the corpse's behalf is exactly what the client would have done,
    // and this is the right rung to do it from: we are the caster, so the offer is
    // ours, and we are already standing here every tick waiting on it.
    if (target->isResurrectRequestedBy(bot->GetGUID()))
    {
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] rez party: {} has our resurrect offer standing — accepting it "
                 "for them (a bot has no client to answer the prompt)",
                 bot->GetName(), target->GetName());
        target->ResurectUsingRequestData();
        return true;
    }

    // Beyond cast range (or no line of sight): close on the body.
    //
    // COMBAT priority, though the rezzer is out of combat by the trigger's gate.
    // The priority here is not a claim about combat, it is who wins the movement
    // arbitration: the movement arbiter (DcMoveDeferred) refuses any move whose
    // priority is not STRICTLY GREATER than the last one's while its block stands
    // (up to 5 seconds before mod-playerbots #2747; only under a hold after it). At
    // NORMAL this rung ties with follow-tank, scout-lag and hold-at-camp — which
    // move constantly — so the approach was refused over and over while the corpse
    // sat there: 259 consecutive "move REFUSED" in run -52 with the rezzer stranded
    // at 84yd, and 29-40yd orbits in runs -1 and -14.
    //
    // THE PRIORITY ALONE WAS NOT ENOUGH.
    // A rung outranking the follower stack in RELEVANCE (31.5 vs hold-at-camp 28 /
    // follow-tank 25) only gets the tick to itself while it RETURNS TRUE — Engine::
    // DoNextAction breaks out of the queue on a successful action and keeps walking
    // down it on a failed one. This branch used to return DcMoveTo's bool, and
    // DcMoveTo returns FALSE for a duplicate destination, which is the ordinary
    // every-tick case while a glide is already running. So every tick after the
    // first, the rez rung "failed", the engine fell through to scout-lag, and
    // scout-lag's inside-the-lag-bubble branch (StopBot(Hold)) tore the approach
    // spline down a few hundred ms in. The rezzer then could not re-issue either,
    // because its OWN cancelled leg had recorded a MOVEMENT_COMBAT wait sized to
    // the whole leg and the arbiter would not let an equal priority through.
    //
    // Live: tr-20260807-080834-115, 301 consecutive "approaching Rederen's body"
    // over 99 seconds with the distance pinned at 86.1 -> 85.8yd — 0.3yd of net
    // progress across an open, unblocked corridor — until the 90s budget expired
    // and the run was disabled with four members alive at 100% HP.
    float const dist = bot->GetExactDist(target);
    if (dist > DC_REZ_CAST_RANGE || !bot->IsWithinLOSInMap(target))
    {
        DC_PULL_TRACE("[DC:{}] rez party: approaching {}'s body ({:.1f}yd)",
                      bot->GetName(), target->GetName(), dist);

        // NOT FROM A SADDLE, AND NOT TO A BODY IN THE AIR. A rezzer on a vehicle
        // cannot walk (its position is the seat's) and cannot cast; and on The
        // Oculus a member who died on a drake hangs where the drake was, 50yd above
        // the nearest floor, where no walk ends within cast range. Owning the tick
        // there only spins until the recovery timeout — hand it back instead, and
        // let the flight driver's regroup take the unreachable body.
        if (bot->GetVehicle())
            return false;
        if (bot->GetMapId() == DcOculus::MAP_ID)
        {
            NavmeshSnap::Result const snap = NavmeshSnap::Snap(bot->GetMap(), target->GetPositionX(),
                                                               target->GetPositionY(), target->GetPositionZ(),
                                                               DcOculus::SNAP_RADIUS, DcOculus::SNAP_VERT);
            if (!snap.ok)
                return false;
        }
        // Standing still with a walk owed means the recorded wait is stale — the
        // leg it was sized for is not running any more (cancelled by whatever last
        // stopped us, or refused outright). Drop it so the replacement leg can go
        // out NOW instead of serving out the dead leg's remaining seconds. Does not
        // stop the bot and does not touch any generator, so an in-flight approach
        // glide is left alone: the isMoving() guard is what keeps this to the
        // replace-the-leg case the seam exists for.
        if (!bot->isMoving())
            DcMovement::ClearMovementWait(bot);

        DcMoveTo(target->GetMapId(), target->GetPositionX(), target->GetPositionY(),
                 target->GetPositionZ(), /*idle*/ false, /*react*/ false,
                 /*normal_only*/ false, /*exact_waypoint*/ false,
                 MovementPriority::MOVEMENT_COMBAT);

        // OWN THE TICK whatever MoveTo returned — a duplicate-destination refusal
        // means the glide we want is already running, not that we have nothing to
        // do. Same reason the pull's drag-back leg returns true unconditionally
        // (DcPullActions, "Own the tick ... so stock combat chase/attack can't grab
        // the tank"). Bounded by the recovery timeout, which is what stops a rezzer
        // that genuinely cannot reach the body from spinning here forever.
        return true;
    }

    // In range: settle (a cast can't start mid-glide), then fire the class rez AT
    // THE ELECTED CORPSE.
    //
    // Deliberately a direct cast rather than DoSpecificAction. The stock action
    // resolves its own target through the "party member to resurrect" value, which
    // walks the party in group order — so with two corpses down it would cast on
    // whichever one that walk reached first while we had walked to the one DcRez
    // Decision::PickTarget chose (healer, then tank, then group order). The two
    // agreeing was luck, and when they disagreed the cast simply failed the range
    // check on a body we were nowhere near, silently, every tick.
    //
    // A false return (out of mana, LOS lost on the settle) yields the tick so the
    // lower rungs — drink/eat at 26.5 — run; the recovery timeout backstops a rezzer
    // that never affords the cast.
    DcMovement::StopBot(bot, DcMovement::Stop::Soft);
    // The one prerequisite the stock action carried that a direct cast would
    // otherwise drop: a druid's feral forms are CAN_ONLY_CAST_SHAPESHIFT_SPELLS, so
    // Revive fails CheckShapeshift before it reaches the corpse. Shift back exactly
    // as CastReviveAction::getPrerequisites does. No-op for everyone else. (Same
    // mechanism as DcFormGate, which gates items rather than spells.)
    if (bot->GetShapeshiftForm() != FORM_NONE)
        botAI->DoSpecificAction("caster form", Event(), /*silent*/ true);
    bool const cast = botAI->CastSpell(rezAction, target);
    if (cast)
    {
        DC_PULL_TRACE("[DC:{}] rez party: cast '{}' on {} -> started", bot->GetName(),
                      rezAction, target->GetName());
        return true;
    }
    RezRefusalDiag(botAI, bot, target, rezAction);
    return false;
}
