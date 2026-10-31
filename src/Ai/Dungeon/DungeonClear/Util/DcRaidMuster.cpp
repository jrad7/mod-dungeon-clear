/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRaidMuster.h"

#include "DcRun.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRaidMusterDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"
#include <string>

namespace
{
// --- RAID pre-boss muster (Plan C; see DcRaidMusterDecision.h) -----------

// The muster pushed the rest targets to full so bots actually eat/drink to
// the bars; retract on release — but never clobber an override the player
// set by hand (we only retract what we applied).
void RetractMusterRestOverride(Player* bot, DcRunState& run)
{
    if (!run.musterRestOverride)
        return;
    DcSettings::ResetOverride(bot->GetGUID(), "RestHealthPct");
    DcSettings::ResetOverride(bot->GetGUID(), "RestManaPct");
    run.musterRestOverride = false;
}

void ApplyMusterRestOverride(Player* bot, DcRunState& run)
{
    if (run.musterRestOverride)
        return;
    // A hand-set override outranks the muster: if the player pinned either
    // rest target for this run, leave both alone.
    if (DcSettings::HasOverride(bot->GetGUID(), "RestHealthPct") ||
        DcSettings::HasOverride(bot->GetGUID(), "RestManaPct"))
        return;
    DcSettings::SetOverride(bot->GetGUID(), "RestHealthPct", 100.0, nullptr);
    DcSettings::SetOverride(bot->GetGUID(), "RestManaPct", 100.0, nullptr);
    run.musterRestOverride = true;
}

// One ForceRebuff round: every same-map bot member opens a rebuff window
// (group-variant buffs, reagents, buff-first multiplier — all the stock
// machinery). The worldbuff strategy (simulated flasks/food) is installed
// by DcStrategyGate on raid maps, so its auras land during the same window.
void IssueRebuffRound(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI)
            continue;  // humans buff themselves
        if (!memberAI->HasStrategy("force rebuff", BOT_STATE_NON_COMBAT))
            memberAI->ChangeStrategy("+force rebuff", BOT_STATE_NON_COMBAT);
        memberAI->forceRebuff.Begin(/*replyToReadyCheck*/ false);
    }
}

// Close every rebuff window this muster opened. ForceRebuffState::Begin is
// its own 2-minute window, and while it is pending stock buff triggers
// bypass their check intervals and the buff-first multiplier zeroes healing
// out of combat — a window left open outlives the muster and keeps the raid
// buffing into the walk-in and the pull. End() is the off-switch; the
// always-on "force rebuff" strategy stays installed (AiFactory gives it to
// every bot), it simply has nothing pending to drive.
void CancelRebuffRound(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member->GetMapId() != bot->GetMapId())
            continue;
        if (PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member))
            memberAI->forceRebuff.End();
    }
}
}

// Full-stop muster gate: stage the raid at the standoff, top off to full, run
// the rebuff round, THEN release. True while the boss engage must hold this
// tick. 5-man dungeons never enter (raid maps only) — their pre-pull flow
// (Smart Rest bossPull, rest floors) is untouched.
//
// Called from BOTH DungeonClearAtBossTrigger (every tick, so the budgets are
// sampled on wall-clock) and DungeonClearEngageBossAction (the action-side half
// of the guard). Re-entrant within a tick: the phase transitions and their side
// effects — the rest override, the rebuff round, the announcements — all hang
// off `v.phase != before`, so the second call this tick is a pure read.
bool DcRaidMuster::Holds(Player* bot, PlayerbotAI* botAI, AiObjectContext* context,
                         DungeonBossInfo const& next)
{
    Map* const map = bot->GetMap();
    if (!map || !map->IsRaid() || next.kind != DungeonAnchorKind::Boss)
        return false;

    DcRunState& run = DcRun::Of(context);
    uint32 const now = getMSTime();

    // A different boss re-arms a fresh muster (kill/skip/`dc go`).
    if (run.musterBossEntry != next.entry)
    {
        RetractMusterRestOverride(bot, run);
        run.musterBossEntry = next.entry;
        run.musterPhase = static_cast<uint8>(DcRaidMusterDecision::Phase::Idle);
        run.musterPhaseSinceMs = 0;
        run.musterArmedMs = 0;
        run.musterRebuffIssuedMs = 0;
    }

    // Snapshots. Humans are never gated on for staging, and only held to
    // loose margins for topping — a muster must not deadlock on a player
    // who is standing where they mean to stand.
    bool staged = true;
    bool topped = true;
    bool rebuffPending = false;
    float const spread = DcSettings::GetFloat(bot, "PartyMaxSpread");
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                continue;
            PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
            bool const isBot = memberAI != nullptr;
            float const hpBar = isBot ? 99.0f : 90.0f;
            float const mpBar = isBot ? 99.0f : 80.0f;
            if (member->GetHealthPct() < hpBar)
                topped = false;
            if (member->GetMaxPower(POWER_MANA) > 0 &&
                member->GetPowerPct(POWER_MANA) < mpBar)
                topped = false;
            if (!isBot)
                continue;
            if (member != bot && bot->GetDistance(member) > spread)
                staged = false;
            if (memberAI->forceRebuff.IsPending())
                rebuffPending = true;
        }
    }

    DcRaidMusterDecision::Inputs in;
    in.staged = staged;
    in.topped = topped;
    in.rebuffDone = run.musterRebuffIssuedMs != 0 && !rebuffPending;
    in.nowMs = now;
    in.phaseSinceMs = run.musterPhaseSinceMs;
    in.armedSinceMs = run.musterArmedMs;
    in.restTimeoutMs = DcSettings::GetUInt(bot, "RaidMusterRestTimeoutSecs") * 1000;
    in.rebuffTimeoutMs = DcSettings::GetUInt(bot, "RaidMusterRebuffTimeoutSecs") * 1000;
    in.totalTimeoutMs = DcSettings::GetUInt(bot, "RaidMusterTotalTimeoutSecs") * 1000;

    auto const before = static_cast<DcRaidMusterDecision::Phase>(run.musterPhase);
    DcRaidMusterDecision::Verdict const v = DcRaidMusterDecision::Decide(before, in);

    if (v.phase != before)
    {
        run.musterPhase = static_cast<uint8>(v.phase);
        run.musterPhaseSinceMs = now;
        switch (v.phase)
        {
            case DcRaidMusterDecision::Phase::Resting:
                // Only reachable as Idle -> Resting: the muster arming for
                // this boss. Stamp the whole-muster budget here.
                run.musterArmedMs = now;
                ApplyMusterRestOverride(bot, run);
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] raid muster: staging at {} — topping the raid "
                         "off to full before the pull", bot->GetName(), next.name);
                DcStatusPublisher::SendAddonMessage(
                    botAI, "CHAT	Mustering at " + next.name +
                               " — topping off before the pull.");
                break;
            case DcRaidMusterDecision::Phase::Rebuffing:
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] raid muster: {}rebuff round for {}",
                         bot->GetName(), v.timedOut ? "(rest timed out) " : "",
                         next.name);
                break;
            case DcRaidMusterDecision::Phase::Ready:
                RetractMusterRestOverride(bot, run);
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] raid muster: {} for {} — releasing the pull",
                         bot->GetName(),
                         !v.timedOut ? "raid staged, topped and buffed"
                         : DcRaidMusterDecision::Expired(now, run.musterArmedMs,
                                                         in.totalTimeoutMs)
                             ? "muster budget spent; buffs cancelled, going with "
                               "what we have"
                             : "rebuff timed out; going with what we have",
                         next.name);
                break;
            default:
                break;
        }
    }
    if (v.beginRebuff)
    {
        IssueRebuffRound(bot);
        run.musterRebuffIssuedMs = now;
    }
    if (v.cancelRebuff)
        CancelRebuffRound(bot);
    return v.hold;
}
