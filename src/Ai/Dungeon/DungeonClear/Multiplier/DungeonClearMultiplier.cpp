/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearMultiplier.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "Action.h"
#include "FollowActions.h"
#include "Player.h"
#include "Playerbots.h"
#include "Position.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcSmartRest.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

float DungeonClearMultiplier::GetValue(Action* action)
{
    if (!action || !botAI || !bot)
        return 1.0f;

    std::string const& name = action->getName();

    // Rest-target cap. Applies to EVERY bot in an active DC run — the leader tank
    // AND its followers — so the whole group stops eating/drinking at the group's
    // chosen target (DungeonClear.RestHealthPct / RestManaPct). Followers never
    // set `enabled`, so we gate on the cross-bot "dungeon clear party tank" value
    // (non-null only while the leader's clear runs and is unpaused) rather than
    // the per-bot enabled flag used below. The matching
    // DungeonClearNeeds{Eat,Drink} triggers raise the floor (drink/eat up to the
    // target even above the stock playerbots stop); this caps the ceiling so a
    // target BELOW the stock stop is honoured too. 0 = inherit, no cap.
    if (name == "food" || name == "drink")
    {
        if (Player* tank = AI_VALUE(Player*, DcKey::PartyTank))
        {
            // Smart Rest hysteresis: between rests eating/drinking is FULLY
            // suppressed (the stock medium-health/high-mana triggers included)
            // so the party keeps pushing; during a latched rest it runs
            // uncapped so everyone tops to 100. Humans have no multiplier and
            // can always eat/drink; while paused PartyTank is null, so stock
            // rest resumes for the duration of the pause.
            if (DcSettings::GetBool(bot, "SmartRest"))
                return DcSmartRest::IsLatched(tank) ? 1.0f : 0.0f;

            bool const isDrink = (name == "drink");
            uint32 const target =
                DcSettings::GetUInt(bot, isDrink ? "RestManaPct" : "RestHealthPct");
            if (target > 0)
            {
                float const pct =
                    isDrink ? bot->GetPowerPct(POWER_MANA) : bot->GetHealthPct();
                if (pct >= static_cast<float>(target))
                    return 0.0f;
            }
        }
    }

    // The stock anti-stack shuffle ("move out of collision", NonCombatStrategy
    // relevance 2) fights DC's positioning and can livelock a follower: the
    // follow-tank/camp-hold pin is a FIXED point, so when another unit stands
    // inside ContactDistance the shuffle hops the bot ~followDistance away in a
    // random direction, the persistent MoveFollow generator walks it straight
    // back onto the pin, and the two alternate forever — a rapid two-steps-out/
    // two-steps-back dance. The bot never stands still long enough to sit, so
    // it can't drink/eat, and the leader's between-pulls rest gate then stalls
    // the whole run on its mana. DC's own positioning decides where everyone
    // stands; drop the shuffle for every member of an active run (same
    // cross-bot gate as the rest cap above).
    if (name == "move out of collision" &&
        AI_VALUE(Player*, DcKey::PartyTank))
        return 0.0f;

    // Wander-style autonomous navigation (grind / rpg / travel). This is what
    // drives the bot around the world on its own.
    bool const isWander =
        name.find("grind") != std::string::npos ||
        name.find("rpg") != std::string::npos ||
        name == "travel" || name.find("travel ") != std::string::npos;

    // Proactive engagement: the bot's OWN target-picking that walks it to a mob
    // and pulls (the grind strategy fires "attack anything"; the pull strategy
    // fires "pull start"/"pull action"/"reach pull"). These run in the non-combat
    // engine BEFORE combat starts, so they bypass DC's triggers entirely — which
    // is exactly how a paused/parked tank still strolls THROUGH a navmesh-passable
    // door to a pack on the far side (the pack only aggros once the tank arrives,
    // so it never shows as combat). DC drives all engagement through its own
    // engage-trash/engage-boss actions, so the stock pickers must stay suppressed
    // whenever DC owns the bot — active OR paused. Reactive defense (a mob that
    // actually aggros the bot) is combat-engine and untouched here.
    bool const isProactiveEngage =
        name == "attack anything" ||
        name == "move random" ||
        name == "pull action" || name == "pull start" || name == "reach pull";

    // FOLLOWER in advanced-pull camp-hold. Followers never set `enabled`, so this
    // sits above the enabled gate below. In pull mode the party holds and
    // leapfrogs camp-to-camp (DungeonClearHoldAtCampAction); when it is parked the
    // hold action YIELDS the tick so the bot can rest/loot at camp — but that
    // would also let stock follow-master / wander / self-pull drag it off toward
    // the scouting tank. Suppress exactly those for a camp-held follower; food /
    // drink / loot / reactive combat fall through untouched so the party still
    // recovers. Only resolve the leader for an action we might actually suppress,
    // so the per-tick leader lookup stays off the hot path.
    if (isWander || isProactiveEngage || dynamic_cast<FollowAction*>(action))
    {
        Position camp;
        bool passive = false;
        if (DcLeaderSignal::GetLeaderCampHold(bot, camp, passive))
            return 0.0f;
    }

    // Stock follow-master (FollowAction, relevance ~1) points a bot at its MASTER —
    // the human party leader — NOT the dungeon-clear tank. While a DC run is active
    // DC owns 100% of positioning: follow-tank (rel 25) trails the tank out of
    // combat, and the assist/regroup rungs drive the party into the tank's fight in
    // combat. The failure this closes: when BOTH legitimately stand down for a tick
    // — a follower flagged into combat (follow-tank bails on IsInCombat) during the
    // brief threat-lead before the assist releases, with the pulled pack dragged out
    // of its line of sight so stock combat has no visible target — the ONLY surviving
    // movement action is stock follow-master, and it walks the follower to the HUMAN
    // instead of the tank (the reported "dps run to me, not the tank" bug). Worse,
    // FollowAction installs a PERSISTENT MoveFollow generator, so once it grabs a
    // follower it keeps dragging it to the master even as the assist flickers back
    // on. Suppress it for EVERY member of an active run (cross-bot PartyTank gate —
    // followers never set `enabled`, and this sits above the enabled/paused gates
    // like the camp-hold and rest-cap blocks; PartyTank resolves the leader only
    // while the clear runs unpaused, and to the tank itself for the leader — which
    // subsumes the tank-rubberband guard that used to live in the active branch).
    // DC's own follow-tank redirect is a DcMovementAction, not a FollowAction, so
    // this never touches it. When paused/off PartyTank is null and stock follow
    // resumes (a paused run hands positioning back to the player).
    if (dynamic_cast<FollowAction*>(action) && AI_VALUE(Player*, DcKey::PartyTank))
        return 0.0f;

    bool const enabled = DcRun::Of(context).enabled;
    bool const paused = DcRun::Of(context).paused;

    // DC off: fully stock behavior, nothing suppressed.
    if (!enabled)
        return 1.0f;

    // Paused is a HOLD, not a hand-back to stock AI. This used to return 1.0
    // (full stock), which let the tank grind/pull off on its own — that's how a
    // paused tank walked straight THROUGH a door the player paused at to deal
    // with. Keep autonomous wandering AND proactive engagement suppressed so a
    // paused tank simply holds and follows the party like any member (follow is
    // left at 1.0, unlike the active branch below) until the player resumes.
    if (paused)
        return (isWander || isProactiveEngage) ? 0.0f : 1.0f;

    // --- Active (enabled && !paused) ------------------------------------------
    // (Stock follow-master is already suppressed above for every active-run member,
    // tank included — the tank must never rubberband back toward its master when
    // Advance parks at the party-spread limit, and followers must never drift to the
    // human mid-fight. That guard is the cross-bot PartyTank block above the enabled
    // gate, because followers never set `enabled` and would return early here.)
    //
    // Suppress wander AND the stock proactive-engagement pickers while DC is
    // active — DC owns engagement via its own engage-trash/engage-boss actions.
    // Anything else (loot, food, drink, reactive combat, our own dungeon-clear
    // actions, and follow for non-tanks) is untouched.
    if (isWander || isProactiveEngage)
        return 0.0f;
    return 1.0f;
}

float DungeonClearCombatMultiplier::GetValue(Action* action)
{
    if (!action || !botAI || !bot)
        return 1.0f;

    // Touch EXACTLY ONE combat action. Fast-path everything else so a fight's full
    // action list pays only a single string compare per tick — the combat engine
    // otherwise stays fully stock.
    if (action->getName() != "drop target")
        return 1.0f;

    // Drop-target ping-pong guard. Engine transitions are action-driven: the stock
    // "drop target" (CombatStrategy, relevance 99) fires whenever the current target
    // is "invalid", and InvalidTargetValue treats OUT-OF-LINE-OF-SIGHT as invalid
    // (AttackersValue::IsValidTarget -> IsWithinLOSInMap). It then leaves the combat
    // engine. So when the flip-early party-assist seeds the tank's mob and flips a
    // follower into the combat engine to close on it (DungeonClearAssistCampAction),
    // drop target (99) out-ranks reach spell/melee (20) every tick and bounces the
    // bot straight back to the non-combat engine before it can move — the 1-tick
    // engine ping-pong that froze the party mid-fight (observed live: 1679 flip
    // attempts, 0 engages). Suppress drop target ONLY for that transient case: an
    // active tank-fight assist, a non-healer, and a current target that is alive,
    // same-map and attackable but merely out of LOS (still being closed on). A dead /
    // despawned / truly-invalid target is NOT out-of-LOS-only, so it still drops
    // normally and the bot moves on or leaves combat cleanly.
    if (PlayerbotAI::IsHeal(bot) || !DcLeaderSignal::IsLeaderFightAssistWanted(bot))
        return 1.0f;

    Unit* tgt = AI_VALUE(Unit*, DcKey::Stock::CurrentTarget);
    if (tgt && tgt->IsAlive() && tgt->GetMapId() == bot->GetMapId() &&
        bot->IsValidAttackTarget(tgt) && !bot->IsWithinLOSInMap(tgt))
        return 0.0f;

    return 1.0f;
}
