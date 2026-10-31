/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BossPullbackRegistry.h"

#include <vector>

namespace
{
    // ---- the table ------------------------------------------------------
    //
    // ONE ROW: Plugger Spazzring (9499, Blackrock Depths 230). Before him it held
    // exactly one row in its life — Ghaz'an (18105, The Underbog 546) — retired
    // in S1593; see below. Adding a boss is a single entry here plus a matching
    // BossRosterPatch anchor.
    //
    // WHY PLUGGER. It is not his ground that kills the party, it is his death:
    // his ON_DEATH SmartAI (9499 rows 9-12) sets faction 14 on every Grim,
    // Guzzling and Hammered Patron within 100yd, and they aggro by proximity.
    // Fought at his counter he died with three patrons within 8yd and the way
    // out running through two more groups at 1-3yd, and each patron that wakes
    // brings its whole formation — tp-20261018-211235-1 ended the bar in a
    // brawl with up to seventeen of them. That is his authentic script, so
    // there is no upstream defect to fix instead. His anchor (PLUGGER_CAMP,
    // DungeonEventTables.h) is past the Bar Door, out of every patron's aggro
    // range.
    //
    // FORCE-AGGRO, because he cannot be tagged the ordinary way: he is faction
    // 674, neutral, so the proximity tag never fires, and a tank with neither a
    // trained opener nor a ranged weapon would hold in his bubble until the leg
    // watchdog. His route is dry and on the navmesh, so the stand-off resolves
    // to him and the walk-out is an ordinary walk-in. The force fires his own
    // aggro rows (EngageWithTarget + AttackStart), and he chases into melee —
    // his casts carry no combat-move flag — so he follows the tank out.
    //
    // WHY GHAZ'AN'S ROW WENT AWAY, so nobody re-adds it from the old reasoning:
    //
    // The row existed because Ghaz'an was always found swimming in his lake, and
    // the party had to be walked ~147yd away to fight him on dry ground — with
    // force-aggro at 150yd and a teleport-if-stuck, because there was no
    // reachable spot inside his aggro bubble and no reliable way for him to
    // climb out. Every one of those was compensation for a single upstream
    // cause: `at_underbog_ghazan` (areatrigger 4302) is the ONLY caller of his
    // ACTION_MOVE_TO_PLATFORM, and a headless bot party never sent the
    // CMSG_AREATRIGGER that runs it. So he never left the water, and everything
    // downstream was built around meeting him there.
    //
    // DcTestAreaTriggers now sends that packet (and the Underbog "Send Ghaz'an
    // to his platform" event covers a route that misses the volume), so he
    // climbs waypoint path 1383921 onto his platform like he does for a real
    // party. Probed against the live mmaps rather than assumed:
    //
    //   * his platform end (256.28, -458.73) has walkable navmesh at z 81.45 —
    //     the SAME surface as (274.72, -462.60), the ledge boss-nav already
    //     drives the tank to for the drop-down objective;
    //   * his whole 12yd MoveRandom circle is on it (probed E/W/N/S/NE/SE, all
    //     z 81.45), so he cannot wander off the deck;
    //   * the deck connects to the old anchor by a continuous walkable ramp —
    //     (154.16,-452.03) z 73.58 -> (170,-460) 73.58 -> (190,-468) 75.98 ->
    //     (205,-472) 79.67 -> (215,-475) 81.08 -> the deck.
    //
    // The "his platform and the pipe are missing from the extracted navmesh"
    // finding this row was justified with was measured around his WATER HOME
    // (193.74, -423.40, 43.58) and never around the platform itself. The
    // platform is meshed. On dry, connected ground he is an ordinary boss, and
    // an ordinary pull is strictly better than a forced one.
    //
    // A row belongs here only when the ground a boss stands on genuinely kills
    // the party AND no upstream cause can be fixed instead. Check the second
    // half first — this row spent a long time treating a symptom.
    std::vector<BossPullback> const& Rows()
    {
        static std::vector<BossPullback> const rows = {
            // map  entry  anchor x  y  z  forceAggro  summonIfStuck
            { 230, 9499, 871.0f, -235.0f, -43.27f, 40.0f, false },  // Blackrock Depths — Plugger Spazzring
            // Upper Blackrock Spire — The Beast. NOT a bad-ground row: his script
            // evades him whenever his own Y goes above -505 in combat, and the
            // corridor north to Drakkisath starts at y -503. Fought where he
            // lives the fight drifts there (Berserker Charge, Terrifying Roar,
            // Flamebreak knock-back), and he resets at full health. The camp is
            // 35yd south of that line on open Furnace floor; he is tagged the
            // normal way (his patrol starts when the party reaches the camp,
            // inside AT 2066). See DcBlackrockSpire::BEAST_CAMP_*.
            { 229, 10430, 60.0f, -540.0f, 111.0f, 0.0f, false },
            // Zul'Gurub — High Priestess Jeklik. Not a bad-ground row either: she
            // waits REACT_PASSIVE on a cave ledge 12yd above the floor that has no
            // navigable route, so the closest the tank gets is underneath it, out
            // of line of sight. Playerbots drops her as an invalid target every
            // tick and nothing ever starts the fight (tr-20261026-221350-1 sat
            // there 15 minutes). Forcing her into combat runs her own intro: bat
            // form, one waypoint down to the floor, then she fights. The camp is
            // a floor point the tank stood on in that run, 14yd from her. Her
            // script turns combat movement off, so the force skips AttackStart
            // (see DcForceBossAggroOnTank): it would cancel that flight.
            { 309, 14517, -12286.9f, -1384.0f, 132.9f, 40.0f, false },
        };
        return rows;
    }
}

BossPullback const* BossPullbackRegistry::Find(uint32 mapId, uint32 bossEntry)
{
    for (BossPullback const& r : Rows())
        if (r.mapId == mapId && r.bossEntry == bossEntry)
            return &r;
    return nullptr;
}

bool BossPullbackRegistry::HasRows(uint32 mapId)
{
    for (BossPullback const& r : Rows())
        if (r.mapId == mapId)
            return true;
    return false;
}
