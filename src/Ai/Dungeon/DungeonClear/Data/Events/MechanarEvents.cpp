/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- The Mechanar (map 554) -------------------------------------------------
//
// Verified from the core (instance_mechanar.cpp / mechanar.h / the boss scripts)
// and the world DB. Three set-pieces sit between the five real DBC bosses and
// are expressed with EXISTING primitives — no net-new EventStepKind:
//
//   1. LOOT THE CACHE OF THE LEGION (floor 1). A locked chest (GO 184465 normal /
//      184849 heroic, Lock.dbc 1706 = a LOCK_KEY_ITEM lock) that requires the
//      Cache of the Legion Key (item 30438). Blizzard forms that key by combining
//      the two Jagged Crystals the Gatewatchers drop (Gyro-Kill 19218 -> Blue
//      30436, Iron-Hand 19710 -> Red 30437) via the crystals' on-use spell 36565.
//      A bot never right-clicks a crystal and the two crystals can split across
//      bags, so (per the design decision) we GRANT the key to the leader at the
//      cache and then USE it ON the chest (the Deadmines-cannon / Durnholde-barrel
//      item-use path): only the key ITEM as the cast item satisfies the KEY_ITEM
//      lock (core Spell::CanOpenLock), and the resulting Player::SendLoot fires the
//      loot-response that the stock "store loot" handler auto-drains. The stock
//      loot pipeline can NOT do this itself (IgnoreChests blacklists the chest and
//      OpenLootAction has no LOCK_KEY_ITEM handling), so the hook owns the open.
//      The chest is consumable (leaves GO_READY once used). All of that is hook 4
//      (GrantCacheKeyAndLoot, ObjectiveHookRegistry).
//
//   2. RIDE THE FACTORY ELEVATOR (floor 1 -> floor 2). The elevator (GO 183788,
//      a type-11 timer-driven transport) lifts the party ~25 yd from the Mo'arg
//      doors (~z1.6) to the Nethermancer door (267.9, 52.3, 27). The framework has
//      no moving-transport support and the navmesh does not bridge the floors, so
//      we FAKE the ride with TeleportParty (the Old Hillsbrad drake pattern): reach
//      the boarding checkpoint past the Mo'arg doors, blink the whole party to the
//      top landing. It cannot fire early — the boarding spot sits behind the twin
//      Mo'arg passage doors, which the core only opens once BOTH Gatewatchers die
//      (DOOR_TYPE_PASSAGE) — and it is ordered after Capacitus besides.
//
//   3. CROSS THE BRIDGE GAUNTLET (floor 2, before Pathaleon). Advancing the bridge
//      after Nethermancer Sepethrea spawns waves of adds; Pathaleon the Calculator
//      (19220) stays greater-invisible and CanAIAttack()==false until
//      GetPersistentData(DATA_BRIDGE_MOB_DEATH_COUNT) >= 4, then ethereal-teleports
//      in and DoZoneInCombat()s the party. If DC engaged his boss anchor early it
//      would bee-line an untargetable boss and livelock, so a PERSISTENT anchored
//      event stands down the pull pipeline and drives the clear: clear the near
//      wave cluster, advance to trigger the far cluster, clear it (well over the 4
//      deaths that make him attackable), then engage + kill Pathaleon. The event
//      owning the kill keeps the still-invisible boss off DC's independent engage
//      until the waves are down.
//
// The five bosses are ordinary DBC encounters (auto-derived); the roster patch
// below only REORDERS them onto the travel path and interleaves the three
// objectives (mirrors the ZulFarrak reorder+objectives shape).

namespace
{
    // --- floor-1 bosses (reorder targets) --------------------------------
    constexpr uint32 MECH_GYROKILL  = 19218;  // Gatewatcher Gyro-Kill  (drops Jagged Blue Crystal 30436)
    constexpr uint32 MECH_IRONHAND  = 19710;  // Gatewatcher Iron-Hand  (drops Jagged Red Crystal 30437)
    constexpr uint32 MECH_CAPACITUS = 19219;  // Mechano-Lord Capacitus
    constexpr uint32 MECH_SEPETHREA = 19221;  // Nethermancer Sepethrea (floor 2)
    constexpr uint32 MECH_PATHALEON = 19220;  // Pathaleon the Calculator (bridge end)

    // --- (1) Cache of the Legion -----------------------------------------
    // Anchored ON the chest so boss-nav delivers the tank into interaction range;
    // the Custom step (hook 4) grants the key and holds until the consumable chest
    // is looted + gone. Chest spawn (both difficulty rows share the spot):
    constexpr float MECH_CACHE_X = 222.54f;
    constexpr float MECH_CACHE_Y = 70.61f;
    constexpr float MECH_CACHE_Z = 0.0f;
    constexpr uint32 MECH_HOOK_GRANT_CACHE_KEY = 4;  // ObjectiveHookRegistry id
    // Generous: the loot pipeline may take a few seconds after the key is granted;
    // only a genuinely stuck loot escalates to a stall for the human.
    constexpr uint32 MECH_CACHE_TIMEOUT = 120000;  // 2 min

    // --- (2) Factory elevator (fake the ride) ----------------------------
    // Boarding checkpoint: past the Mo'arg doors (236.5/242.9, 52, floor z~0.6), on
    // the shaft base. Top landing: on the upper platform ~2.6yd short of the
    // Nethermancer door (267.9, 52.3, ~26). Both are NAVMESH-VALIDATED against the
    // real map-554 mmaps (S921 DcNavHarness probe, TestMechanarElevatorProbe):
    //   * boarding snaps on-mesh at (249.07, 52.0, 0.60) and Capacitus->boarding
    //     routes reachable+complete (the tank walks there on floor 1);
    //   * the landing snaps on-mesh at (265.33, 52.0, 26.17) and landing->Sepethrea
    //     routes reachable+complete (len ~73yd, maxStepZ 1.48 — clean floor-2 path
    //     onward), so the teleported party is never stranded off-mesh.
    constexpr float MECH_BOARD_X = 249.0f;
    constexpr float MECH_BOARD_Y = 52.0f;
    constexpr float MECH_BOARD_Z = 0.6f;
    constexpr float MECH_TOP_X = 265.3f;
    constexpr float MECH_TOP_Y = 52.0f;
    constexpr float MECH_TOP_Z = 26.2f;

    // --- (3) Bridge gauntlet ---------------------------------------------
    // Two wave clusters on the north bridge (x~138): near y40-53, far y100-112,
    // Pathaleon at the end (139.5, 149.3, 25.7). Zones are sized to cover their
    // cluster WITHOUT reaching Pathaleon (so ClearRadius never tries to target the
    // still-invisible boss). Even a premature near-zone completion is safe: the
    // first sub-wave alone (4 blood elves) already meets the 4-death gate that
    // makes Pathaleon attackable.
    // NAVMESH-VALIDATED against the real map-554 mmaps (S921 DcNavHarness probe,
    // TestMechanarGauntletProbe): all four anchors + both wave-cluster spawn points
    // snap on-mesh, and the full traversal routes reachable+complete —
    // Sepethrea->entry (len ~230yd), entry->advance, advance->far, and
    // entry->Pathaleon (len ~104yd, maxStepZ 0.67, no under-map pops). Zone radii
    // are still first-cut — tune (esp. the advance point that spawns the far set)
    // if the wave-timing needs it on a live instance.
    constexpr float MECH_BRIDGE_ENTRY_X = 138.0f;
    constexpr float MECH_BRIDGE_ENTRY_Y = 45.0f;
    constexpr float MECH_BRIDGE_ENTRY_Z = 25.4f;
    constexpr float MECH_NEAR_X = 138.0f, MECH_NEAR_Y = 48.0f, MECH_NEAR_Z = 25.4f;
    constexpr float MECH_ADVANCE_X = 138.0f, MECH_ADVANCE_Y = 90.0f, MECH_ADVANCE_Z = 26.4f;
    constexpr float MECH_FAR_X = 138.0f, MECH_FAR_Y = 106.0f, MECH_FAR_Z = 26.4f;
    constexpr float MECH_WAVE_RADIUS = 22.0f;   // near zone (y26..70)
    constexpr float MECH_FAR_RADIUS = 24.0f;    // far zone  (y82..130)
    constexpr float MECH_WAVE_ZBAND = 12.0f;
    // Wave survival + the boss kill can run several minutes; keep the long holds
    // from escalating to a stall.
    constexpr uint32 MECH_GAUNTLET_TIMEOUT = 300000;  // 5 min per hold/kill
}

void RegisterMechanarEvents(std::vector<DungeonEvent>& out)
{
    // (1) LOOT THE CACHE OF THE LEGION.
    // Non-persistent anchored: the objective navigates the tank onto the chest,
    // then hook 4 grants key 30438 and holds Running until the stock loot pipeline
    // consumes the chest (gone from range) — see GrantCacheKeyAndLoot. A combat gap
    // that rewinds a non-persistent event just re-runs the idempotent grant.
    out.push_back(
        EventBuilder(554, 1, "Loot the Cache of the Legion")
            .Anchored(/*orderIndex (doc)*/ 10)
            .Custom(MECH_HOOK_GRANT_CACHE_KEY)
                .Timeout(MECH_CACHE_TIMEOUT)
            .Build());

    // (2) RIDE THE FACTORY ELEVATOR (faked with TeleportParty).
    // PERSISTENT with a MoveTo step 0 so the at-objective trigger goes sticky
    // (stepIndex > 0) before the teleport — matching the OH ride. TeleportParty is
    // synchronous + idempotent and pulls stranded followers across, so a tick-gap
    // restart never double-teleports (the leader is no longer at the bottom
    // checkpoint once lifted).
    out.push_back(
        EventBuilder(554, 2, "Ride the Factory Elevator")
            .Anchored(/*orderIndex (doc)*/ 11)
            .Persistent()
            .MoveTo(MECH_BOARD_X, MECH_BOARD_Y, MECH_BOARD_Z, /*radius*/ 8.0f)
            .TeleportParty(/*checkpoint*/ MECH_BOARD_X, MECH_BOARD_Y, MECH_BOARD_Z,
                           /*landing*/ MECH_TOP_X, MECH_TOP_Y, MECH_TOP_Z,
                           /*radius*/ 12.0f)
            .Build());

    // (3) CROSS THE BRIDGE GAUNTLET, THEN SLAY PATHALEON.
    // PERSISTENT so it stands down the pull pipeline (no premature engage of the
    // invisible boss) and its progress survives the many wave combat gaps. Step 0
    // (MoveTo onto the bridge) bumps stepIndex so the persistence sticky-trigger
    // engages and the tank may advance the whole bridge from the anchor. The two
    // ClearRadius zones fight through the wave sets (position-based, so the
    // scripted wave composition doesn't need enumerating); the final
    // KillCreatureEngage takes Pathaleon once the >=4 deaths have made him
    // attackable (he also DoZoneInCombat()s the party himself ~25s after the 4th
    // death, so the party is usually already pulled in).
    out.push_back(
        EventBuilder(554, 3, "Cross the bridge gauntlet and slay Pathaleon")
            .Anchored(/*orderIndex (doc)*/ 12)
            .Persistent()
            .MoveTo(MECH_BRIDGE_ENTRY_X, MECH_BRIDGE_ENTRY_Y, MECH_BRIDGE_ENTRY_Z, /*radius*/ 8.0f)
            .ClearRadius(MECH_NEAR_X, MECH_NEAR_Y, MECH_NEAR_Z, MECH_WAVE_RADIUS, MECH_WAVE_ZBAND)
                .Timeout(MECH_GAUNTLET_TIMEOUT)
            .MoveTo(MECH_ADVANCE_X, MECH_ADVANCE_Y, MECH_ADVANCE_Z, /*radius*/ 8.0f)
            .ClearRadius(MECH_FAR_X, MECH_FAR_Y, MECH_FAR_Z, MECH_FAR_RADIUS, MECH_WAVE_ZBAND)
                .Timeout(MECH_GAUNTLET_TIMEOUT)
            .KillCreatureEngage(MECH_PATHALEON, /*count*/ 1, /*searchRadius*/ 120.0f)
                .Timeout(MECH_GAUNTLET_TIMEOUT)
            .Build());
}

// --- roster patch: reorder the five DBC bosses + interleave three objectives ---
void RegisterMechanarRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // The DBC encounter order does not match the travel path; reorder the three
    // DBC bosses and ADD the two Gatewatchers onto a contiguous 1..8 key scale
    // shared with the three objectives (orderOverride 4/5/7), so the strictly-
    // ordinal picker walks:
    //   1. Gatewatcher Gyro-Kill
    //   2. Mechano-Lord Capacitus
    //   3. Gatewatcher Iron-Hand          (both crystals now looted across 1 & 3)
    //   4. Loot the Cache of the Legion   (objective, eventId 1)  — past the Mo'arg doors
    //   5. Ride the Factory Elevator      (objective, eventId 2)  — up to floor 2
    //   6. Nethermancer Sepethrea
    //   7. Cross the bridge gauntlet      (objective, eventId 3)  — clears waves + kills Pathaleon
    //   8. Pathaleon the Calculator       (already dead once the gauntlet event completes)
    //
    // Capacitus sits at order 2, BETWEEN the Gatewatchers, on purpose: the tank
    // reaches him from Gyro-Kill (NW) and approaches the pit from the west/north,
    // so the SE Driller pack (y-52..-63) is NOT on the pull approach — it falls on
    // the post-Capacitus walk DOWN to Iron-Hand (SE) and gets handled as ordinary
    // trash-clear once the boss is already dead. That removes the whole reason the
    // Capacitus room-aggro pre-clear existed (it pre-cleared that SE pack when the
    // old order approached him last, up the corridor from the dead Iron-Hand), so
    // the RoomAggroRegistry entry for 19219 is gone.
    //
    // CRITICAL: the two Gatewatchers are NOT auto-derived. instance_encounters has
    // no ENCOUNTER_CREDIT_KILL_CREATURE row for 19218/19710 (they are door-gating
    // mini-bosses, not DungeonEncounter.dbc encounters), so BossSpawnIndex never
    // lists them and a bare `reorder` entry is a no-op — the picker jumped straight
    // to Capacitus and the party never killed them, leaving the Mo'arg passage
    // doors (each opens on its Gatewatcher's death) shut and the elevator blocked.
    // So ADD them as explicit bosses whose completion reads the instance script's
    // own boss-state slots (DATA_GATEWATCHER_GYROKILL=0 / _IRON_HAND=1 from
    // mechanar.h) via MakeBoss's doneBossStateIndex — the one sanctioned use of
    // GetBossState, keyed off the instance header, not a coincidental DBC index.
    //
    // Objective encounterIndex uses 10/11/12 — bits the 5-encounter mask never
    // sets, so an objective only completes via its event, never a stray mask bit.
    constexpr int32 MECH_DATA_GYROKILL = 0;   // DATA_GATEWATCHER_GYROKILL (mechanar.h)
    constexpr int32 MECH_DATA_IRONHAND = 1;   // DATA_GATEWATCHER_IRON_HAND (mechanar.h)
    BossRosterPatch p;
    p.mapId = 554;
    p.reorder = {
        { MECH_CAPACITUS, 2 },
        { MECH_SEPETHREA, 6 },
        { MECH_PATHALEON, 8 },
    };
    p.add = {
        MakeBoss(MECH_GYROKILL, 554, "Gatewatcher Gyro-Kill",
                 85.53f, 20.20f, 15.00f, /*completionFrom*/ 0,
                 /*orderOverride*/ 1, /*doneBossStateIndex*/ MECH_DATA_GYROKILL),
        MakeBoss(MECH_IRONHAND, 554, "Gatewatcher Iron-Hand",
                 181.85f, -77.12f, 0.01f, /*completionFrom*/ 0,
                 /*orderOverride*/ 3, /*doneBossStateIndex*/ MECH_DATA_IRONHAND),
        MakeObjective(OBJ(1), /*encounterIndex*/ 10, 554, "Loot the Cache of the Legion",
                      MECH_CACHE_X, MECH_CACHE_Y, MECH_CACHE_Z, /*arriveRadius*/ 5.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1, /*orderOverride*/ 4),
        MakeObjective(OBJ(2), /*encounterIndex*/ 11, 554, "Ride the Factory Elevator",
                      MECH_BOARD_X, MECH_BOARD_Y, MECH_BOARD_Z, /*arriveRadius*/ 8.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 2, /*orderOverride*/ 5),
        MakeObjective(OBJ(3), /*encounterIndex*/ 12, 554, "Cross the bridge gauntlet",
                      MECH_BRIDGE_ENTRY_X, MECH_BRIDGE_ENTRY_Y, MECH_BRIDGE_ENTRY_Z,
                      /*arriveRadius*/ 10.0f, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 3, /*orderOverride*/ 7),
    };
    t.push_back(std::move(p));
}
