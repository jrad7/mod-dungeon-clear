/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Molten Core (map 409) — raid-support Plan E1, the pilot raid ----------
//
// AUTHORED FROM THE CORE SCRIPTS (instance_molten_core.cpp,
// boss_majordomo_executus.cpp, boss_ragnaros.cpp); not yet validated live —
// the first `.dc test start mc size=10` runs are the acceptance test.
//
// Eight static bosses auto-derive from BossSpawnIndex (kill-credit rows +
// creature spawns, DBC bits 0-7) and need nothing here. The finale is two
// SCRIPT-SUMMONED encounters with no creature spawn rows, which is why this
// patch exists:
//
//   * MAJORDOMO EXECUTUS (12018, boss-state slot 8). Auto-summoned by the
//     instance script at MajordomoSummonPos the moment bosses 0-7 are DONE —
//     EXCEPT Lucifron, whom CheckMajordomoExecutus skips (the roster still
//     clears him for loot and flow; his bit simply isn't required for the
//     spawn). He NEVER dies: the encounter completes when his eight
//     Flamewaker adds (4x 11663 healer, 4x 11664 elite) are dead, at which
//     point the script fires the encounter credit MANUALLY
//     (UpdateEncounterState + SetBossState(8, DONE)), turns him friendly and
//     teleports him to the Ragnaros chamber. Completion is therefore read off
//     the instance BOSS-STATE SLOT (doneBossStateIndex 8) — never a corpse,
//     which will never exist. Engaging him normally starts the add fight; the
//     playerbots `moltencore` strategy target-excludes Majordomo himself, so
//     the raid fights the adds while the boss stand-down keeps DC's hands off.
//   * RAGNAROS (11502, boss-state slot 9). Summoned only by GOSSIP (menu 4108,
//     option 0) on the teleported, friendly Majordomo. ~48s of scripted RP
//     during which he is immune, then HE pulls the whole raid
//     (DoZoneInCombat). The summon is DC's job: an objective anchor at
//     Majordomo's Ragnaros-side position drives the gossip and waits for the
//     spawn (the RFD-gong summon shape); the boss anchor after it owns the
//     fight, completion via boss-state slot 9.
//
// Clear order: Baron Geddon goes before Shazzrah, against their DBC bits (5 and
// 4). Only the order keys swap; both keep their real kill-bits.
//
// Index-space note (the Drak'Tharon lesson, inverted): the two added bosses
// use doneBossStateIndex — the ONE sanctioned GetBossState seam — because the
// slots (8/9) are authored explicitly from molten_core.h's DATA_ constants,
// while their DBC bit numbers are not pinned anywhere in this module. MakeBoss
// parks their encounterIndex past bit 31 so the completed-mask check can never
// misread another boss's bit.
//
// Known combat losses accepted for v1 (the strategy's, not DC's): Magmadar
// fear chaos and Ragnaros knockback/submerge inefficiency. Muster refinement
// for the pre-gossip stage (the Plan C muster arms at BOSS anchors, so the
// summon objective itself gets only the ordinary between-pulls readiness plus
// the leading dwell below) is deliberately left to live iteration.

namespace
{
    constexpr uint32 kMapId = 409;

    constexpr uint32 kShazzrah  = 12264;
    constexpr uint32 kGeddon    = 12056;
    constexpr uint32 kMajordomo = 12018;
    constexpr uint32 kRagnaros  = 11502;

    // molten_core.h boss-state slots.
    constexpr int32 kSlotMajordomo = 8;
    constexpr int32 kSlotRagnaros  = 9;

    // boss_majordomo_executus.cpp authored positions.
    // Majordomo's battle spawn (the add fight):
    constexpr float kMajX = 759.542f, kMajY = -1173.43f, kMajZ = -118.974f;
    // His post-victory teleport spot by Ragnaros' lair (the gossip target):
    constexpr float kRagGossipX = 848.933f, kRagGossipY = -812.875f, kRagGossipZ = -229.601f;
    // Ragnaros' summon position (the fight anchor):
    constexpr float kRagX = 838.308f, kRagY = -831.467f, kRagZ = -232.185f;

    constexpr uint32 kEventSummonRagnaros = 1;
}

void RegisterMoltenCoreEvents(std::vector<DungeonEvent>& out)
{
    // Summon Ragnaros: dwell so the raid closes up at the anchor, gossip the
    // friendly Majordomo (menu 4108 option 0 — the only option he offers
    // there), then hold through the ~48s scripted intro until Ragnaros is a
    // live unit. He zone-pulls the raid himself when the RP ends, which arms
    // the boss stand-down; the boss anchor after this objective owns the rest.
    DungeonEvent ev;
    ev.mapId = kMapId;
    ev.id = kEventSummonRagnaros;
    ev.name = "Summon Ragnaros";
    ev.activation = EventActivation::Anchored;
    ev.orderIndex = 9;
    ev.required = true;

    EventStep dwell;
    dwell.kind = EventStepKind::Wait;
    dwell.durationMs = 5000;
    ev.steps.push_back(dwell);

    EventStep gossip;
    gossip.kind = EventStepKind::Gossip;
    gossip.creatureEntry = kMajordomo;
    gossip.x = kRagGossipX;
    gossip.y = kRagGossipY;
    gossip.z = kRagGossipZ;
    gossip.radius = 40.0f;
    gossip.gossipOption = 0;
    // A re-entered instance that already summoned Ragnaros has no gossip
    // Majordomo to click — skip to the spawn wait, which the live (or fought)
    // Ragnaros satisfies via the objective's gateEntry.
    gossip.skipIfMissing = true;
    ev.steps.push_back(gossip);

    EventStep waitSpawn;
    waitSpawn.kind = EventStepKind::WaitForSpawn;
    waitSpawn.creatureEntry = kRagnaros;
    waitSpawn.x = kRagX;
    waitSpawn.y = kRagY;
    waitSpawn.z = kRagZ;
    waitSpawn.radius = 100.0f;
    waitSpawn.wantAlive = true;
    // The scripted intro alone is ~48s; give the whole summon sequence room.
    waitSpawn.timeoutMs = 120000;
    ev.steps.push_back(waitSpawn);

    out.push_back(std::move(ev));
}

void RegisterMoltenCoreRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = kMapId;

    // The eight statics keep their derived DBC order (bits 0-7) except for
    // Geddon and Shazzrah, who swap. The finale slots in after them.
    p.reorder = {
        { kGeddon,   4 },
        { kShazzrah, 5 },
    };
    p.add.push_back(MakeBoss(kMajordomo, kMapId, "Majordomo Executus",
                             kMajX, kMajY, kMajZ, /*completionFrom*/ 0,
                             /*orderOverride*/ 8, kSlotMajordomo));
    p.add.push_back(MakeObjective(OBJ(1), /*encounterIndex*/ 9, kMapId,
                                  "Summon Ragnaros",
                                  kRagGossipX, kRagGossipY, kRagGossipZ,
                                  /*arriveRadius*/ 10.0f, /*gateEntry*/ kRagnaros,
                                  /*hook*/ 0, kEventSummonRagnaros,
                                  /*orderOverride*/ 9));
    p.add.push_back(MakeBoss(kRagnaros, kMapId, "Ragnaros",
                             kRagX, kRagY, kRagZ, /*completionFrom*/ 0,
                             /*orderOverride*/ 10, kSlotRagnaros));

    t.push_back(std::move(p));
}
