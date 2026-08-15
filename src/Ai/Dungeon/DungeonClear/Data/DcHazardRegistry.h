/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCHAZARDREGISTRY_H
#define _PLAYERBOT_DCHAZARDREGISTRY_H

#include <vector>

#include "Define.h"

// Static registry of creatures whose damage is a hazard the combat AI cannot
// reason about on its own. Two distinct threats, both represented here:
//
//   (1) A PERSISTENT pulse the party should not LOITER in. The Arcatraz Sentinel
//   (map 552, entry 20869) channels 36716 -> 36717 "Energy Discharge" — 563-937
//   damage in a 15yd radius, every second, forever, including while dormant
//   (heroic 21586: 38828 -> 38829, 938-1562). A DORMANT Sentinel is rooted in
//   place (addon aura 31261), so its position is known at author time. You DO
//   fight the live one (it wakes AGGRESSIVE at 40% HP and chases), so this half is
//   only about not camping / resting / routing THROUGH the pulse — the in-combat
//   pulse while you kill it is healable. (The live one's <=10% "Explode", 36719,
//   is NOT handled here: 36719 STUNS the sentinel for its own 6s wind-up, so the
//   party bursts the helpless 10%-HP mob down before it detonates — pulling DPS
//   off it to "dodge" would only keep it alive long enough to actually explode.)
//
//   (2) A persistent pulse the party must actively VACATE, from a creature it
//   canNOT fight. On death the live Sentinel summons the "Destroyed Sentinel"
//   (21761) at the corpse (SmartAI event 6 -> spell 37394). That summon is
//   NOT_SELECTABLE + hostile and carries the SAME permanent 36716 -> 36717 pulse,
//   15yd/1s, until it despawns. The party has just been meleeing the Sentinel, so
//   the summon spawns right on top of them; they cannot target it to kill it and
//   have no reason to stay — but nothing moves them off, so they stand in it and
//   die. THIS is the run-wiper ("Destroyed Sentinel's Energy Discharge"), and it
//   ticks AFTER the kill, often out of combat. `vacateRadius` marks such an
//   emitter and drives the active retreat.
//
// Consumed in three ways, on two threads:
//
//   STATIC pathing (worker thread). Threat (1): a dormant emitter is rooted, so
//   route avoidance is hand-authored DcNavPenaltyRegistry boxes at the spawn
//   coords, keeping DcRouteFilter / LongRangePathfinder::BuildCoreFromMesh on
//   their worker-safe contract (no Player*/Map*/VMAP — the route producer runs on
//   DcPathWorker and CANNOT resolve a live creature). Do not "improve" this by
//   handing the filter a Map*.
//
//   LIVE point validation (map thread). Threat (1): camp anchors, engage
//   standoffs and skirt legs are kept out of the pulse cylinder (`radius`) via
//   DcHazard::PointIsHot / SegmentIsHot, backed by the 500ms-cached
//   DungeonClearHazardsValue.
//
//   LIVE active retreat (map thread). Threat (2): DungeonClearHazardVacate-
//   {Trigger,Action} drive every party bot (all roles) out of `vacateRadius` of a
//   live emitter that carries it, in BOTH the combat and non-combat engines (the
//   summon's pulse ticks whether or not the bot is still flagged in combat).
//   Reads the same cached value.
//
// Mirrors RoomAggroRegistry / DcNavPenaltyRegistry: adding an emitter is a
// single table edit inside DungeonClear/, never a core change.
struct DcHazardEmitter
{
    uint32 mapId{0};
    uint32 creatureEntry{0};

    // Keep-out radius (yd), 2D, for the persistent pulse (threat 1 — camp/route
    // placement). Sized as the aura's own radius plus a margin wide enough that a
    // bot which drifts a few yards mid-fight still does not clip it — but kept
    // BELOW caster range, so ranged DPS can still hold a firing line past one.
    float  radius{0.0f};

    // Vertical half-extent (yd). An emitter on another floor is not a hazard;
    // without this a Sentinel at z22 would sterilise the z48 catwalk above it.
    float  zBand{12.0f};

    // Active-vacate pulse (threat 2), yd. When > 0, this is a creature the party
    // must MOVE OUT of — it cannot be fought (the Destroyed Sentinel is
    // NOT_SELECTABLE), so no combat behaviour ever clears it. This is the raw
    // pulse radius; the retreat aims a hysteresis band beyond it. 0 => this
    // emitter is fought or merely avoided in placement, never actively fled.
    float  vacateRadius{0.0f};
};

class DcHazardRegistry
{
public:
    // True iff `mapId` has at least one emitter. Cheap early-out so the live
    // predicates only do real work on maps that actually need it.
    static bool HasEmitters(uint32 mapId);

    // The emitter row for (mapId, creatureEntry), or nullptr when that creature
    // is not a registered hazard. Linear scan — the table is small.
    static DcHazardEmitter const* Find(uint32 mapId, uint32 creatureEntry);

    // Pure geometry: true when (px,py,pz) lies inside `e`'s keep-out cylinder
    // centred on (ex,ey,ez). No game state — unit-testable.
    static bool PointInside(DcHazardEmitter const& e,
                            float ex, float ey, float ez,
                            float px, float py, float pz);

    // Pure geometry: true when the 2D segment (ax,ay)->(bx,by) clips `e`'s
    // keep-out circle centred on (ex,ey), with both endpoints inside the zBand
    // of `ez`. Mirrors DcEngageGeometry::NeedsRoomAggroSkirt. Unit-testable.
    static bool SegmentClips(DcHazardEmitter const& e,
                             float ex, float ey, float ez,
                             float ax, float ay, float az,
                             float bx, float by, float bz);
};

#endif
