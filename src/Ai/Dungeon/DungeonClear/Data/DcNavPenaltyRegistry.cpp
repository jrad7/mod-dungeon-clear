/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcNavPenaltyRegistry.h"

#include <array>

namespace
{
    // ---- the table ------------------------------------------------------
    // One row per navmesh shortcut a real player can't follow. Each box spans
    // only the MIDDLE Z band of its climb — the legitimate floor below and ledge
    // platform above sit just outside it, so a route that genuinely belongs down
    // there or up there is untaxed; only an edge climbing the face pays. A stiff
    // multiplier makes the A* corridor take the real way around whenever one
    // exists; it stays a cost, so the spot is never made unreachable.
    //
    // Lower Blackrock Spire (map 229), #1 — the big chasm climb. The navmesh
    // stitches a walkable poly up the wall between the lower walkway (~z30) and an
    // upper ledge (~z58), so the tank climbs a near-vertical face the party can't
    // follow. Observed bot traversal:
    //     [-127.33, -402.11, 30.32]  ->  [-124.88, -378.42, 58.40]
    // (≈28yd rise over ≈24yd ground = ~50°). Box mid-band z 33..56.
    //
    // Lower Blackrock Spire (map 229), #2 — a small ledge-hop further along the
    // (now-corrected) route, where some bots wedged on the step. Same class, much
    // smaller. Observed traversal:
    //     [-61.70, -382.77, 48.88]  <->  [-64.34, -378.49, 54.70]
    // (≈5.8yd rise over ≈5yd ground = ~49°). Tight box hugging the two endpoints,
    // mid-band z 49.4..54.2 so the lower walkway (≤~49) and the upper platform
    // (≥~54.7, which the proper route reaches from another direction) stay untaxed.
    //
    // Sethekk Halls (map 556) — the Talon King's back-door ramp. The instance is a
    // loop: Talon King Ikiss (44.7, 287.0, z25) sits on an upper ring you reach the
    // long way (west ramp near (-250, 210) up to z27, then east across the upper
    // room). But a narrow ramp climbs straight to his platform from a closed,
    // script-controlled door (GO 183398 at (44.8, 150.7, z0)) on the LOWER level
    // directly south of him. The mmap stitches that ramp into the navmesh ignoring
    // the shut door, so Detour's A* picks the short south climb, routes the party
    // back toward the entrance, and wedges at the door (it can't open it) — the
    // "can't reach Ikiss after Syth" symptom. The ramp is a single ~x45 column
    // (x≈40..50, void either side) rising y153->245, z0->27, opening onto the
    // shared platform only at y>=250. Box the whole climb up to (not into) the
    // platform — x 25..68, y 150..248, z -5..30 — so every edge on the shortcut is
    // taxed while the upper room / platform (y>=250) and the lower lobby (y<150)
    // stay untaxed and the legitimate west-ramp route is left at base cost.
    // The Arcatraz (map 552) — the five Arcatraz Sentinel (20869) spawns. NOT a
    // navmesh shortcut: this is the route half of DcHazardRegistry. Each rooted
    // Sentinel pulses 563-937 damage in 15yd every second, forever, so the
    // router should prefer a line that hugs the far wall. Boxes are the emitter
    // position +-22 in XY and +-12 in Z (the registry's radius / zBand), matching
    // DcHazardRegistry's rows so the route half and the live half agree.
    //
    // COST MULTIPLIER IS 8, NOT 40, AND HAZARDS ARE DELIBERATELY NOT WIRED INTO
    // THE StridedPathfinder HARD REJECT. Sentinels 138931 (255.5,158.9) and
    // 138932 (253.9,131.9) sit 27yd apart in what is the only corridor through
    // that stretch of the Containment Core: their boxes overlap and span it.
    // A cost is survivable there (the route still goes through, just last) —
    // a rejection would strand the party. 8 is enough to bend a route around an
    // emitter when floor space exists, without making an unavoidable corridor
    // rank worse than a genuinely broken navmesh shortcut at 40.
    //
    // If test runs show bots still walking the pulse, NARROW THE BOXES rather
    // than raising the multiplier — a wider tax on a mandatory corridor buys
    // nothing and starts competing with the shortcut rows above.
    //
    // Underbog (map 546) — a navmesh shortcut up normally unwalkable geometry:
    // the mmap stitches a walkable face the party can't follow, so Detour's A*
    // climbs it instead of taking the intended route. The shortcut runs between
    // (35.17, -364.37, 27.57) and (66.6, -357.99, 33.77). This sits in a very
    // wide-open area, so the box is drawn generously around the whole run (with
    // margin) rather than hugging a narrow band — an over-sized box here only
    // makes the router prefer the open floor around it, never strands anyone.
    // costMult 40 (a spot a real player can't be, same class as the LBRS/Sethekk
    // shortcut rows above).
    constexpr std::array<DcNavPenaltyVolume, 9> kVolumes = {{
        { 229, -134.0f, -406.0f, 33.0f, -118.0f, -374.0f, 56.0f, 40.0f },
        { 229,  -65.5f, -384.0f, 49.4f,  -60.5f, -377.0f, 54.2f, 40.0f },
        { 556,   25.0f,  150.0f, -5.0f,   68.0f,  248.0f, 30.0f, 40.0f },
        { 546,   25.0f, -375.0f, 22.0f,  77.0f, -347.0f, 40.0f, 40.0f },
        { 552,  233.5f,  136.9f, 10.4f,  277.5f,  180.9f, 34.4f,  8.0f },
        { 552,  231.9f,  109.9f, 10.4f,  275.9f,  153.9f, 34.4f,  8.0f },
        { 552,  242.3f,  -83.3f, 10.5f,  286.3f,  -39.3f, 34.5f,  8.0f },
        { 552,  314.5f,    5.4f, 36.4f,  358.5f,   49.4f, 60.4f,  8.0f },
        { 552,  373.4f,   -3.8f, 36.3f,  417.4f,   40.2f, 60.3f,  8.0f },
    }};

    // ---- polygonal no-go regions ----------------------------------------
    // Same contract as kVolumes (a route cost, and a hard reject in the
    // StridedPathfinder corridor screen), but a polygon footprint for a spot a
    // box can't hug.
    //
    // Sethekk Halls (map 556) — a room corner where the navmesh stitches a sliver
    // of floor out over a drop, so a bot that clips the corner falls under the
    // world. The five vertices are the measured arc that rounds the corner off,
    // all on the z≈26.7 floor; the polygon they enclose is the pocket to keep
    // routes out of. The corner is fenced with the arc itself rather than a box
    // because the arc's bounding box would spill well past it into open floor,
    // and the StridedPathfinder screen HARD-rejects (doesn't just tax) any
    // corridor entering the region — an over-sized footprint there could wall off
    // a legitimate lane and strand the party. costMult 40 matches the other 556
    // shortcut row (a spot a real player can't be, not a survivable hazard).
    //
    // Hellfire Ramparts (map 543) — a wall of a narrow corridor. The measured wall
    // runs the diagonal (-1367.45, 1645.24, 68.46) -> (-1335.65, 1668.71, 68.47)
    // (≈39.5yd long), so an axis-aligned box hugging it would be a ~32x23yd blob
    // spilling across the whole corner and swallowing the walkable corridor floor.
    // A polygon lets the footprint be a THIN strip laid along the wall instead: the
    // quad is that line inflated ±2yd on its perpendicular (≈4yd thick), so a route
    // that clips into / along the wall is fenced while the corridor centre stays
    // clear. Z band 62..76 straddles the ~z68 floor. costMult 40 (a spot a real
    // player can't be, same class as the shortcut rows above).
    constexpr std::array<DcNavPenaltyPolygon, 2> kPolygons = {{
        { 556, 15.0f, 38.0f, 40.0f, 5,
          { -233.29f, -230.34f, -209.82f, -192.94f, -192.04f },
          {  275.04f,  309.39f,  326.92f,  305.38f,  271.93f } },
        { 543, 62.0f, 76.0f, 40.0f, 4,
          { -1368.64f, -1336.84f, -1334.46f, -1366.26f },
          {  1646.85f,  1670.32f,  1667.10f,  1643.63f } },
    }};

    // Even-odd ray cast — true iff (x,y) is inside the polygon's XY footprint.
    // Handles convex or concave simple polygons and is winding-agnostic.
    bool PointInPolygonXY(DcNavPenaltyPolygon const& p, float x, float y)
    {
        bool inside = false;
        for (uint32 i = 0, j = p.vertCount - 1; i < p.vertCount; j = i++)
        {
            float const xi = p.vx[i], yi = p.vy[i];
            float const xj = p.vx[j], yj = p.vy[j];
            bool const straddles = (yi > y) != (yj > y);
            if (straddles && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                inside = !inside;
        }
        return inside;
    }
}

bool DcNavPenaltyRegistry::HasVolumes(uint32 mapId)
{
    for (auto const& v : kVolumes)
        if (v.mapId == mapId)
            return true;
    for (auto const& p : kPolygons)
        if (p.mapId == mapId)
            return true;
    return false;
}

float DcNavPenaltyRegistry::PenaltyAt(uint32 mapId, float x, float y, float z)
{
    float worst = 1.0f;
    for (auto const& v : kVolumes)
    {
        if (v.mapId != mapId)
            continue;
        if (x < v.minX || x > v.maxX)
            continue;
        if (y < v.minY || y > v.maxY)
            continue;
        if (z < v.minZ || z > v.maxZ)
            continue;
        if (v.costMult > worst)
            worst = v.costMult;
    }
    for (auto const& p : kPolygons)
    {
        if (p.mapId != mapId)
            continue;
        if (z < p.minZ || z > p.maxZ)
            continue;
        if (!PointInPolygonXY(p, x, y))
            continue;
        if (p.costMult > worst)
            worst = p.costMult;
    }
    return worst;
}
