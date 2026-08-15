/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcHazardRegistry.h"

#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"

#include <array>
#include <cmath>

namespace
{
    // ---- the table ------------------------------------------------------
    //
    // The Arcatraz (map 552), entry 20869 "Arcatraz Sentinel". Mechanical elite.
    // SmartAI resets it to REACT_AGGRESSIVE at 40% HP, and it carries three addon
    // auras: 11838 (threat-to-zero, so it does not aggro from threat while idle),
    // 31261 (Permanent Feign Death — the ROOT that pins a DORMANT one in place,
    // REMOVED on aggro so it then chases and melees), and 36716 "Energy Discharge"
    // — SPELL_AURA_PERIODIC_TRIGGER_SPELL, period 1000ms, firing 36717 for 563-937
    // at EffectRadiusIndex 18 = 15.0yd. Nothing removes 36716, so the 15yd pulse
    // runs dormant AND in combat. Heroic (21586) swaps 36716->38828->38829,
    // 938-1562, same 15yd/1s. Five spawns, dormant coords:
    //     (255.498, 158.914, 22.362)   (253.942, 131.881, 22.395)
    //     (264.287, -61.321, 22.453)   (336.514,  27.427, 48.426)
    //     (395.413,  18.195, 48.296)
    // radius 22 = the 15yd pulse plus 7yd of drift margin. Below the 30yd caster
    // range on purpose, so a ranged bot can still hold a line past one.
    //
    // The live Sentinel's <=10% "Explode" (36719 -> 36722, ~5000 in 10yd) is NOT
    // registered as a threat: 36719 also MOD_STUNs the sentinel for its own 6s
    // wind-up, so the party simply bursts the helpless 10%-HP mob down before it
    // detonates. Pulling DPS off it to dodge would keep it alive long enough to
    // actually explode — a self-inflicted wound. So no explode handling here.
    //
    // Entry 21761 "Destroyed Sentinel" — the run-wiper the live one summons on
    // death (event 6 -> spell 37394, which summons the fixed creature 21761 for
    // both normal and heroic). NOT_SELECTABLE (unit_flags 33555200) so the party
    // cannot target/kill it, hostile, and it carries the permanent 36716 -> 36717
    // pulse (15yd, 1s, 563-937). It spawns right where the party just killed the
    // Sentinel and ticks until it despawns, often after combat has ended — so
    // vacateRadius drives an active retreat in BOTH engines. Its `radius` (the
    // camp/route keep-out consumed by PointIsHot) is the RAW 15yd pulse, NOT the
    // padded 22 the live fixture uses: the retreat aims pulse+slack = 19yd, and a
    // padded radius would make PointIsHot reject that point as inside this very
    // emitter's cylinder, so the retreat could never find a clear spot.
    //
    // Entries 21303 "Defender Corpse" / 21304 "Warder Corpse" — proximity bombs.
    // SmartAI event 10 (OOC line of sight, param2 = 8) or event 4 (on aggro) fires
    // actionlist 2130400: cast 36599 "Bloody Explosion" + 36593 "Corpse Burst",
    // then despawn. One-shot, avoidance-only. Tighter 12 = 8yd trigger plus 4yd
    // margin, no route penalty box (12yd is inside ordinary pathing jitter).
    //
    // NOT REGISTERED: the Eredar room's two 45yd auras. Its three spawn points are
    // `creature_multispawn`, each rolling Eredar Soul-Eater (20879, "Entropic
    // Aura" 36784 — 45yd, -25% haste and speed, no damage) or Eredar Deathbringer
    // (20880, "Unholy Aura" 27987/38844 -> 27988/38845 — 45yd, 450 normal / 750
    // heroic SCHOOL_DAMAGE every 2s to every enemy in range), re-rolled on every
    // respawn.
    //
    // The Deathbringer's pulse is genuinely lethal, and a keep-out is still the
    // wrong tool for it: 45yd is wider than the ranges the party works at, so
    // sized honestly it refuses every route through the wing, and sized to obey
    // the threat-1 rule (below caster range) it is a lie — at 30yd you take full
    // damage. There is no standing-off from it and no routing around it. The only
    // answer is to cross and kill, which is a set-piece: ArcatrazEvents.cpp event
    // 2, a ClearRadius over the three spawns filtered to both entries.

    // Two corpse clusters overlap Sentinels — the (264.3,-61.3) Sentinel sits
    // beside a Defender Corpse at (272.1,-59.0), and the (395.4,18.2) Sentinel
    // sits inside the corpse pair (392.1,24.9)/(395.1,27.6) — so routing through
    // either takes the pulse AND trips a bomb. Both are covered by the Sentinel
    // penalty boxes in DcNavPenaltyRegistry.
    constexpr std::array<DcHazardEmitter, 4> kEmitters = {{
        { 552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f, /*vacate*/  0.0f },  // Arcatraz Sentinel (fought)
        { 552, 21761, /*radius*/ 15.0f, /*zBand*/ 12.0f, /*vacate*/ 15.0f },  // Destroyed Sentinel (summon — VACATE)
        { 552, 21303, /*radius*/ 12.0f, /*zBand*/  8.0f, /*vacate*/  0.0f },  // Defender Corpse
        { 552, 21304, /*radius*/ 12.0f, /*zBand*/  8.0f, /*vacate*/  0.0f },  // Warder Corpse
    }};
}

bool DcHazardRegistry::HasEmitters(uint32 mapId)
{
    for (auto const& e : kEmitters)
        if (e.mapId == mapId)
            return true;
    return false;
}

DcHazardEmitter const* DcHazardRegistry::Find(uint32 mapId, uint32 creatureEntry)
{
    for (auto const& e : kEmitters)
        if (e.mapId == mapId && e.creatureEntry == creatureEntry)
            return &e;
    return nullptr;
}

bool DcHazardRegistry::PointInside(DcHazardEmitter const& e,
                                   float ex, float ey, float ez,
                                   float px, float py, float pz)
{
    if (e.radius <= 0.0f)
        return false;
    if (std::fabs(pz - ez) > e.zBand)
        return false;

    float const dx = px - ex;
    float const dy = py - ey;
    return dx * dx + dy * dy < e.radius * e.radius;
}

bool DcHazardRegistry::SegmentClips(DcHazardEmitter const& e,
                                    float ex, float ey, float ez,
                                    float ax, float ay, float az,
                                    float bx, float by, float bz)
{
    if (e.radius <= 0.0f)
        return false;

    // Reject only when both endpoints are out of band ON THE SAME SIDE. Two
    // endpoints out of band on OPPOSITE sides means the leg descends straight
    // THROUGH the band — a ramp from the Arcatraz z48 upper tier down toward
    // Zereketh's z-10 chamber passes the emitter's exact z with |dz| large at
    // both ends, and a naive `both out => clean` test would wave it through.
    float const da = az - ez;
    float const db = bz - ez;
    if (std::fabs(da) > e.zBand && std::fabs(db) > e.zBand && (da > 0.0f) == (db > 0.0f))
        return false;

    float const clipSq = DungeonClearMath::DistSqToSegment2D(ex, ey, ax, ay, bx, by);
    return clipSq < e.radius * e.radius;
}
