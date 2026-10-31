/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "GameObject.h"
#include "InstanceScript.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

// --- Zul'Gurub (map 309) -----------------------------------------------------
//
// AUTHORED FROM THE CORE SCRIPTS (boss_arlokk.cpp, instance_zulgurub.cpp,
// zulgurub.h) and the world DB spawn rows; not yet validated live.
//
// Seven bosses auto-derive from BossSpawnIndex (kill-credit rows + creature
// spawns): Venoxis, Jeklik, Mar'li, Mandokir, Thekal, Jin'do, Hakkar.
//
//   * HIGH PRIESTESS JEKLIK (14517) is re-anchored on a floor camp below her
//     cave ledge, keeping her own kill-bit, and BossPullbackRegistry has the
//     tank force her into combat from there. She waits passive on a ledge with
//     no navigable route, so the ordinary engage never lands (see her row).
//
// Three encounters have no creature spawn row and are therefore silently absent:
//
//   * HIGH PRIESTESS ARLOKK (14515). Summoned by clicking the Gong of Bethekk
//     (GO 180526, go_gong_of_bethekk::OnGossipHello) at PosSummonArlokk; she then
//     walks to the gong. One of the five priests, so she belongs on the clear.
//     This patch adds her: a boss anchor ON the gong (the RFD Tuten'kash shape)
//     plus a repeatable event that rings it. The gong script refuses while she is
//     already within 25yd, and flags itself NOT_SELECTABLE once rung and again
//     when the instance loads with her DONE, which is what the condition reads.
//   * EDGE OF MADNESS (Gri'lek / Hazza'rah / Renataki / Wushoolay) needs a
//     Gurubashi Mojo Madness on the Brazier of Madness, and GAHZ'RANKA needs a
//     Mudskunk Lure fished up at the Pool of Muckrot. Both are optional,
//     item-gated, and bots cannot farm the items, so they stay off the roster.
//
// COMPLETION reads the instance boss-state slot (DATA_ARLOKK = 3), the slot
// BossAI writes on her death, rather than a DBC bit: the slots are pinned by
// zulgurub.h, the DBC bit numbers are not pinned anywhere in this module (same
// call as Molten Core's Majordomo / Ragnaros).
//
// ORDER: 7, between Thekal and Jin'do, matching the DBC encounter order the
// statics sort by. Order is a travel preference only; it never gates completion.

namespace
{
    constexpr uint32 kMapId = 309;

    constexpr uint32 kArlokk = 14515;
    constexpr uint32 kJeklik = 14517;

    // Jeklik's pull-back camp; must match her BossPullbackRegistry row.
    constexpr float kJeklikCampX = -12286.9f, kJeklikCampY = -1384.0f, kJeklikCampZ = 132.9f;
    constexpr uint32 kGongOfBethekk = 180526;

    // zulgurub.h boss-state slot.
    constexpr int32 kSlotArlokk = 3;  // DATA_ARLOKK

    // Gong of Bethekk spawn (gameobject guid 28667).
    constexpr float kGongX = -11569.2f, kGongY = -1627.87f, kGongZ = 41.2767f;

    // The click reaches this far; matches the RFD gong's ring range.
    constexpr float kGongRingRange = 30.0f;
    constexpr float kGongSearch = 35.0f;

    // Pull stand-down radius around the gong and around Arlokk once she is up.
    // The default 80yd holds the pull off ~58 elites (the Zulian Panther pens,
    // Hakkari Shadow Hunters, the Blood Drinkers and Berserker on the way in), so
    // the tank would walk into them instead of pulling them. 20yd covers the gong
    // platform and her home point (PosMoveOnSpawn, 7yd from the gong).
    constexpr float kGongHoldRadius = 20.0f;

    constexpr uint32 kEventRingGong = 1;

    bool ArlokkGongDue(Player* bot, AiObjectContext* /*context*/)
    {
        if (DcTargeting::FindLiveCreatureOnMap(bot, kArlokk))
            return false;
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (inst && inst->GetBossState(kSlotArlokk) == DONE)
            return false;
        GameObject* gong = bot->FindNearestGameObject(kGongOfBethekk, 200.0f);
        if (!gong || gong->HasGameObjectFlag(GO_FLAG_NOT_SELECTABLE))
            return false;
        return bot->IsWithinDist(gong, kGongRingRange);
    }
}

void RegisterZulGurubEvents(std::vector<DungeonEvent>& out)
{
    // RING THE GONG — one click. REPEATABLE so the event stays pending (which
    // keeps the not-spawned stall off Arlokk's anchor) until she is dead, and so a
    // wipe can ring it again once the gong resets. PanelBeforeBoss folds the row
    // into Arlokk's and holds the dynamic pull off while the tank parks on the
    // gong, but only within kGongHoldRadius so the trash around it is pulled
    // first.
    //
    // She spawns 62yd east (PosSummonArlokk) and runs to her home beside the gong.
    // Reaching it unengaged despawns her and re-enables the gong
    // (boss_arlokk JustReachedHome), so the tank waiting on the gong is what makes
    // her aggro on the way in.
    out.push_back(EventBuilder(kMapId, kEventRingGong, "Ring the Gong of Bethekk")
                      .Conditional(&ArlokkGongDue)
                      .Repeatable()
                      .PanelBeforeBoss(kArlokk)
                      .SummonHoldRadius(kGongHoldRadius)
                      .UseGO(kGongOfBethekk, kGongSearch)
                      .Build());
}

void RegisterZulGurubRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = kMapId;
    p.remove = { kJeklik };
    p.add.push_back(MakeBoss(kJeklik, kMapId, "High Priestess Jeklik",
                             kJeklikCampX, kJeklikCampY, kJeklikCampZ,
                             /*completionFrom*/ kJeklik));
    p.add.push_back(MakeBoss(kArlokk, kMapId, "High Priestess Arlokk",
                             kGongX, kGongY, kGongZ, /*completionFrom*/ 0,
                             /*orderOverride*/ 7, kSlotArlokk));
    t.push_back(std::move(p));
}
