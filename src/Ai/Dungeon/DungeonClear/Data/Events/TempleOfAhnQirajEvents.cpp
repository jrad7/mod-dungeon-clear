/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "InstanceScript.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

// --- Temple of Ahn'Qiraj (map 531) -------------------------------------------
//
// AUTHORED FROM THE CORE SCRIPTS (boss_ouro.cpp, temple_of_ahnqiraj.h) and the
// world DB spawn rows; not yet validated live.
//
// Eight encounters auto-derive from BossSpawnIndex: Skeram, Silithid Royalty
// (credit Vem), Sartura, Fankriss, Viscidus, Huhuran, the Twin Emperors (credit
// Vek'nilash) and C'Thun (the body, spawned from map load beside the Eye). The
// one with no creature spawn row is:
//
//   * OURO (15517). The world holds only his SPAWNER (npc_ouro_spawner, 15957)
//     on the mound. Its MoveInLineOfSight casts Summon Ouro the first time a
//     player comes within 40yd in line of sight while the encounter is not DONE.
//     So the tank only has to walk onto the mound: the boss anchor sits on the
//     spawner and the ordinary boss navigation drives the long haul there. The
//     repeatable event below is what keeps the anchor honest in the seconds
//     between arriving and Ouro existing. While it is pending,
//     DcTargeting::HasPendingSummonEvent holds off the advance's "not spawned"
//     stall, and its MoveToHoldUntilSpawn step garrisons the tank on the mound
//     until he is up.
//
// COMPLETION reads the instance boss-state slot (DATA_OURO = 8, written by
// BossAI on his death) rather than a DBC bit, the same call Molten Core makes
// for its summoned finale.
//
// ORDER: 7, between the Twin Emperors and C'Thun, matching where the DBC lists
// him and the classic path (the Twins' chamber leads down to his lair).

namespace
{
    constexpr uint32 kMapId = 531;

    constexpr uint32 kOuro = 15517;

    // temple_of_ahnqiraj.h boss-state slot.
    constexpr int32 kSlotOuro = 8;  // DATA_OURO

    // Ouro Spawner spawn (creature guid 88073), the centre of his mound.
    constexpr float kMoundX = -9173.09f, kMoundY = 2107.33f, kMoundZ = -65.0863f;

    // The spawner's trigger reach is 40yd; arm the garrison a little outside it
    // so the event is already pending when the summon lands.
    constexpr float kArmRange = 45.0f;

    constexpr uint32 kEventApproachMound = 1;

    bool OuroMoundDue(Player* bot, AiObjectContext* /*context*/)
    {
        if (DcTargeting::FindLiveCreatureOnMap(bot, kOuro))
            return false;
        // IN_PROGRESS covers his submerges: Ouro despawns and a dirt mound summons
        // a new one ~30s later, so for that window there is no live Ouro and the
        // garrison would drag the tank back to the mound mid-fight.
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (inst && (inst->GetBossState(kSlotOuro) == DONE || inst->GetBossState(kSlotOuro) == IN_PROGRESS))
            return false;
        return bot->GetDistance(kMoundX, kMoundY, kMoundZ) <= kArmRange;
    }
}

void RegisterTempleOfAhnQirajEvents(std::vector<DungeonEvent>& out)
{
    // APPROACH THE MOUND — a short garrison hop onto the spawner (the boss anchor
    // already brought the tank within 45yd), held until Ouro is alive. REPEATABLE
    // so it stays pending until he dies and re-arms after a wipe; the spawner
    // re-summons once the encounter has reset.
    out.push_back(EventBuilder(kMapId, kEventApproachMound, "Wake Ouro on his mound")
                      .Conditional(&OuroMoundDue)
                      .Repeatable()
                      .PanelBeforeBoss(kOuro)
                      .MoveToHoldUntilSpawn(kMoundX, kMoundY, kMoundZ, /*radius*/ 8.0f, kOuro)
                      .Build());
}

void RegisterTempleOfAhnQirajRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = kMapId;
    p.add.push_back(MakeBoss(kOuro, kMapId, "Ouro",
                             kMoundX, kMoundY, kMoundZ, /*completionFrom*/ 0,
                             /*orderOverride*/ 7, kSlotOuro));
    t.push_back(std::move(p));
}
