/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCNAVPENALTYREGISTRY_H
#define _PLAYERBOT_DCNAVPENALTYREGISTRY_H

#include "Define.h"

// Hand-authored "no-go" volumes for the dungeon-clear route producer.
//
// Some dungeon navmeshes contain shortcuts a real player cannot follow: the mmap
// generator stitches a walkable poly up a ledge/chasm face, so Detour's A* climbs
// a near-vertical wall instead of taking the intended ramp, and the human party
// (which can't make that climb) gets left behind. Slope alone can't separate these
// from legitimate steep ramps (the mmap generator's own walkable limit is 60°,
// which overlaps the shortcut's ~50°), so a targeted, hand-authored volume is the
// reliable lever for a known-bad spot.
//
// One row per known-bad spot: an axis-aligned world-space box on a map plus a
// cost multiplier. DcRouteFilter::getCost multiplies the cost of any A* edge
// whose midpoint falls inside the box by that factor, so the search prefers the
// legitimate way around. The box is a COST, never a hard rejection (passFilter is
// untouched), so an over-sized box degrades to "the bot routes slower" rather
// than "the bot is stranded" — if the boxed edge is genuinely the only way
// through, the route still uses it.
//
// Mirrors RoomAggroRegistry / BossRosterRegistry: adding a fix is a single table
// edit inside DungeonClear/, never an mmap regen or a core change.
struct DcNavPenaltyVolume
{
    uint32 mapId{0};
    float  minX{0.0f}, minY{0.0f}, minZ{0.0f};
    float  maxX{0.0f}, maxY{0.0f}, maxZ{0.0f};
    float  costMult{1.0f};   // edge-cost multiplier inside the box (>= 1)
};

// Same contract as DcNavPenaltyVolume, but the XY footprint is a simple polygon
// (convex or not) plus a Z band instead of an axis-aligned box — for a bad spot
// whose shape is a room corner or curved edge that no single box can hug without
// also covering walkable floor. Vertices are world-space, in order around the
// ring; the first and last are implicitly joined. Up to 8 vertices; `vertCount`
// says how many of vx/vy are live.
struct DcNavPenaltyPolygon
{
    uint32 mapId{0};
    float  minZ{0.0f}, maxZ{0.0f};
    float  costMult{1.0f};
    uint32 vertCount{0};
    float  vx[8]{};
    float  vy[8]{};
};

class DcNavPenaltyRegistry
{
public:
    // True iff `mapId` has at least one penalty volume or polygon. Cheap
    // early-out so getCost only does the per-edge region test on maps that
    // actually need it.
    static bool HasVolumes(uint32 mapId);

    // The largest costMult of any volume or polygon on `mapId` that contains
    // (x,y,z), or 1.0 when the point lies in no region. Pure (no game state) —
    // unit-testable.
    static float PenaltyAt(uint32 mapId, float x, float y, float z);
};

#endif
