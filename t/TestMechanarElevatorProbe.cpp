/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Coordinate-derivation probe for the Mechanar (map 554) elevator TeleportParty.
// Not a committed regression: it reads the FULL (unsliced) mmaps dir from env
// DC_PROBE_MMAPS and snaps/validates the authored elevator anchors against the
// real navmesh, printing the snapped WoW coordinates so the event file can be
// corrected. GTEST_SKIPs when the env var is unset (normal CI / clean checkout).
//
//   DC_PROBE_MMAPS=/home/jared/azerothcore/env/dist/bin \
//     ./dungeon_clear_tests --gtest_filter='MechanarElevatorProbe.*'

#include "gtest/gtest.h"
#include "NavHarness.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
    constexpr uint32_t MAP_MECHANAR = 554;

    void Snap(dtNavMesh const* mesh, char const* label, float x, float y, float z)
    {
        G3D::Vector3 p;
        bool const ok = DcNavHarness::NearestPoint(mesh, x, y, z, /*hExt*/ 8.0f, /*vExt*/ 12.0f, p);
        if (ok)
            std::printf("  [snap] %-22s (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)  dz=%+.2f\n",
                        label, x, y, z, p.x, p.y, p.z, p.z - z);
        else
            std::printf("  [snap] %-22s (%.2f, %.2f, %.2f) -> OFF-MESH (no poly within 8h/12v)\n",
                        label, x, y, z);
    }

    void RouteLog(dtNavMesh const* mesh, char const* label,
                  float sx, float sy, float sz, float tx, float ty, float tz)
    {
        DcNavHarness::RouteResult const r =
            DcNavHarness::Route(mesh, MAP_MECHANAR, sx, sy, sz, tx, ty, tz);
        std::printf("  [route] %-28s reachable=%d complete=%d startFar=%d pts=%u len2d=%.1f maxStepZ=%.2f  %s\n",
                    label, r.reachable, r.corridorComplete, r.startFarFromPoly,
                    r.pointCount, r.routeLength2d, r.maxStepZ, r.failureReason.c_str());
    }
}

TEST(MechanarElevatorProbe, SnapAndRoute)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 554";

    std::shared_ptr<dtNavMesh> mesh = DcNavHarness::LoadMap(dir, MAP_MECHANAR);
    if (!mesh)
        GTEST_SKIP() << "no map-554 navmesh under " << dir << "/mmaps";

    std::printf("=== Mechanar (554) elevator probe ===\n");
    std::printf("-- floor-1 references --\n");
    Snap(mesh.get(), "Capacitus",        208.23f, -13.05f, -2.12f);
    Snap(mesh.get(), "Cache",            222.54f,  70.61f,  0.00f);
    Snap(mesh.get(), "Mo'arg door 1",    236.46f,  52.36f,  1.65f);
    Snap(mesh.get(), "Mo'arg door 2",    242.87f,  52.31f,  1.60f);
    // Final (navmesh-derived) elevator coords used by MechanarEvents.cpp.
    constexpr float BOARD_X = 249.0f, BOARD_Y = 52.0f, BOARD_Z = 0.6f;
    constexpr float TOP_X = 265.3f, TOP_Y = 52.0f, TOP_Z = 26.2f;
    constexpr float SEP_X = 326.52f, SEP_Y = 13.20f, SEP_Z = 27.92f;

    std::printf("-- elevator coords (final) --\n");
    Snap(mesh.get(), "BOARD (final)", BOARD_X, BOARD_Y, BOARD_Z);
    Snap(mesh.get(), "TOP LANDING (final)", TOP_X, TOP_Y, TOP_Z);
    std::printf("-- floor-2 references --\n");
    Snap(mesh.get(), "Sepethrea", SEP_X, SEP_Y, SEP_Z);

    std::printf("-- reachability --\n");
    // Floor 1 -> boarding: the tank must be able to walk to the boarding spot.
    DcNavHarness::RouteResult const toBoard =
        DcNavHarness::Route(mesh.get(), MAP_MECHANAR, 208.23f, -13.05f, -2.12f, BOARD_X, BOARD_Y, BOARD_Z);
    RouteLog(mesh.get(), "Capacitus -> BOARD", 208.23f, -13.05f, -2.12f, BOARD_X, BOARD_Y, BOARD_Z);
    // Top landing -> Sepethrea MUST be reachable+complete (party paths on after the lift).
    DcNavHarness::RouteResult const topToSep =
        DcNavHarness::Route(mesh.get(), MAP_MECHANAR, TOP_X, TOP_Y, TOP_Z, SEP_X, SEP_Y, SEP_Z);
    RouteLog(mesh.get(), "TOP -> Sepethrea", TOP_X, TOP_Y, TOP_Z, SEP_X, SEP_Y, SEP_Z);

    std::printf("=====================================\n");

    // Guardrails on the two invariants the TeleportParty depends on.
    G3D::Vector3 board, top;
    EXPECT_TRUE(DcNavHarness::NearestPoint(mesh.get(), BOARD_X, BOARD_Y, BOARD_Z, 4.0f, 4.0f, board))
        << "boarding checkpoint is off the navmesh";
    EXPECT_TRUE(DcNavHarness::NearestPoint(mesh.get(), TOP_X, TOP_Y, TOP_Z, 4.0f, 4.0f, top))
        << "top landing is off the navmesh — party would be stranded after the teleport";
    EXPECT_TRUE(toBoard.reachable) << "tank cannot walk to the boarding checkpoint on floor 1";
    EXPECT_TRUE(topToSep.reachable && topToSep.corridorComplete)
        << "top landing does not path forward to Sepethrea";
}

TEST(MechanarSepethreaRoomProbe, SnapAndRoute)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 554";
    std::shared_ptr<dtNavMesh> mesh = DcNavHarness::LoadMap(dir, MAP_MECHANAR);
    if (!mesh)
        GTEST_SKIP() << "no map-554 navmesh under " << dir << "/mmaps";

    // Sepethrea's chamber (floor 2). Boss + the three trash groups (from the world
    // DB creature spawns, map 554):
    constexpr float SEP_X = 326.52f, SEP_Y = 13.20f, SEP_Z = 27.92f;
    // Entrance references: elevator top landing + Nethermancer encounter door.
    constexpr float TOP_X = 265.3f, TOP_Y = 52.0f, TOP_Z = 26.2f;
    constexpr float DOOR_X = 267.9f, DOOR_Y = 52.3f, DOOR_Z = 27.0f;

    struct P { char const* label; float x, y, z; };
    // Pack A — near the entrance/exit (2 Astromage + 2 Centurion), x~272.
    P const packA[] = {
        {"A astromage",  272.1f, -21.0f, 26.4f},
        {"A astromage",  272.2f, -24.7f, 26.4f},
        {"A centurion",  274.1f, -28.7f, 26.4f},
        {"A centurion",  274.3f, -17.8f, 26.4f},
    };
    // Robots — Tempest-Forge Destroyers (one stationary, one roaming).
    P const robots[] = {
        {"robot (stationary)", 290.6f, 29.1f, 25.5f},
        {"robot (roaming)",    293.9f, -14.9f, 25.4f},
    };
    // Pack B — right in front of the boss (2 Centurion + 2 Astromage), x~309.
    P const packB[] = {
        {"B centurion",  309.2f, 10.3f, 25.5f},
        {"B astromage",  309.3f, 15.1f, 25.5f},
        {"B astromage",  309.4f,  5.3f, 25.5f},
        {"B centurion",  309.5f, 20.3f, 25.5f},
    };
    // Candidate camp/entrance anchors to evaluate (west side, near the door).
    P const camps[] = {
        {"camp@door",   DOOR_X, DOOR_Y, DOOR_Z},
        {"camp 280/30", 280.0f, 30.0f, 25.8f},
        {"camp 285/10", 285.0f, 10.0f, 25.6f},
        {"camp 278/-5", 278.0f, -5.0f, 25.8f},
    };

    auto dist2d = [](float ax, float ay, float bx, float by) {
        float const dx = ax - bx, dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    };

    std::printf("=== Mechanar (554) Sepethrea room probe ===\n");
    std::printf("-- boss + entrance --\n");
    Snap(mesh.get(), "Sepethrea", SEP_X, SEP_Y, SEP_Z);
    Snap(mesh.get(), "TOP LANDING", TOP_X, TOP_Y, TOP_Z);
    Snap(mesh.get(), "Nethermancer door", DOOR_X, DOOR_Y, DOOR_Z);

    auto dumpGroup = [&](char const* gname, P const* arr, size_t n) {
        std::printf("-- %s --\n", gname);
        for (size_t i = 0; i < n; ++i)
        {
            Snap(mesh.get(), arr[i].label, arr[i].x, arr[i].y, arr[i].z);
            std::printf("        dist2d->boss = %.1f\n",
                        dist2d(arr[i].x, arr[i].y, SEP_X, SEP_Y));
        }
    };
    dumpGroup("Pack A (entrance)", packA, 4);
    dumpGroup("Robots", robots, 2);
    dumpGroup("Pack B (front of boss)", packB, 4);

    std::printf("-- candidate camp anchors (dist2d to boss) --\n");
    for (P const& c : camps)
    {
        Snap(mesh.get(), c.label, c.x, c.y, c.z);
        std::printf("        dist2d->boss = %.1f\n", dist2d(c.x, c.y, SEP_X, SEP_Y));
    }

    std::printf("-- reachability (landing -> groups, and door -> boss) --\n");
    RouteLog(mesh.get(), "LANDING -> packA[0]", TOP_X, TOP_Y, TOP_Z, packA[0].x, packA[0].y, packA[0].z);
    RouteLog(mesh.get(), "LANDING -> robots[1]", TOP_X, TOP_Y, TOP_Z, robots[1].x, robots[1].y, robots[1].z);
    RouteLog(mesh.get(), "LANDING -> packB[0]", TOP_X, TOP_Y, TOP_Z, packB[0].x, packB[0].y, packB[0].z);
    RouteLog(mesh.get(), "LANDING -> Sepethrea", TOP_X, TOP_Y, TOP_Z, SEP_X, SEP_Y, SEP_Z);
    RouteLog(mesh.get(), "packB[0] -> camp 285/10", packB[0].x, packB[0].y, packB[0].z, 285.0f, 10.0f, 25.6f);

    // Walkable-extent scan: sample a grid across the chamber and report the
    // on-mesh bounding box + centroid, so the kite-containment (mod-playerbots
    // Sepethrea flame kite) can clamp to a room center + radius that never pushes
    // a kiter into a wall or out the west door.
    std::printf("-- room walkable-extent scan (z~26) --\n");
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    int onMesh = 0;
    for (float sx = 262.0f; sx <= 340.0f; sx += 2.0f)
        for (float sy = -40.0f; sy <= 60.0f; sy += 2.0f)
        {
            G3D::Vector3 p;
            if (DcNavHarness::NearestPoint(mesh.get(), sx, sy, 26.0f, 1.5f, 6.0f, p))
            {
                ++onMesh;
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            }
        }
    std::printf("  on-mesh samples=%d  bbox x[%.1f..%.1f] y[%.1f..%.1f]  center(%.1f,%.1f)\n",
                onMesh, minX, maxX, minY, maxY, (minX + maxX) / 2.0f, (minY + maxY) / 2.0f);
    std::printf("===========================================\n");

    SUCCEED();
}

TEST(MechanarGauntletProbe, SnapAndRoute)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 554";
    std::shared_ptr<dtNavMesh> mesh = DcNavHarness::LoadMap(dir, MAP_MECHANAR);
    if (!mesh)
        GTEST_SKIP() << "no map-554 navmesh under " << dir << "/mmaps";

    // Authored gauntlet coords from MechanarEvents.cpp.
    constexpr float SEP_X = 326.52f, SEP_Y = 13.20f, SEP_Z = 27.92f;
    constexpr float ENTRY_X = 138.0f, ENTRY_Y = 45.0f, ENTRY_Z = 25.4f;
    constexpr float NEAR_X = 138.0f, NEAR_Y = 48.0f, NEAR_Z = 25.4f;
    constexpr float ADV_X = 138.0f, ADV_Y = 90.0f, ADV_Z = 26.4f;
    constexpr float FAR_X = 138.0f, FAR_Y = 106.0f, FAR_Z = 26.4f;
    constexpr float PATH_X = 139.54f, PATH_Y = 149.32f, PATH_Z = 25.66f;

    std::printf("=== Mechanar (554) gauntlet probe ===\n");
    std::printf("-- bridge spawn refs --\n");
    Snap(mesh.get(), "near mob (Destroyer)", 137.8f, 53.2f, 25.0f);
    Snap(mesh.get(), "far mob (Netherbinder)", 141.4f, 102.8f, 26.5f);
    std::printf("-- authored gauntlet anchors --\n");
    Snap(mesh.get(), "bridge ENTRY", ENTRY_X, ENTRY_Y, ENTRY_Z);
    Snap(mesh.get(), "near zone", NEAR_X, NEAR_Y, NEAR_Z);
    Snap(mesh.get(), "advance", ADV_X, ADV_Y, ADV_Z);
    Snap(mesh.get(), "far zone", FAR_X, FAR_Y, FAR_Z);
    Snap(mesh.get(), "Pathaleon", PATH_X, PATH_Y, PATH_Z);

    std::printf("-- reachability --\n");
    RouteLog(mesh.get(), "Sepethrea -> bridge ENTRY", SEP_X, SEP_Y, SEP_Z, ENTRY_X, ENTRY_Y, ENTRY_Z);
    RouteLog(mesh.get(), "ENTRY -> advance", ENTRY_X, ENTRY_Y, ENTRY_Z, ADV_X, ADV_Y, ADV_Z);
    RouteLog(mesh.get(), "advance -> far zone", ADV_X, ADV_Y, ADV_Z, FAR_X, FAR_Y, FAR_Z);
    DcNavHarness::RouteResult const entryToPath =
        DcNavHarness::Route(mesh.get(), MAP_MECHANAR, ENTRY_X, ENTRY_Y, ENTRY_Z, PATH_X, PATH_Y, PATH_Z);
    RouteLog(mesh.get(), "ENTRY -> Pathaleon", ENTRY_X, ENTRY_Y, ENTRY_Z, PATH_X, PATH_Y, PATH_Z);
    DcNavHarness::RouteResult const sepToEntry =
        DcNavHarness::Route(mesh.get(), MAP_MECHANAR, SEP_X, SEP_Y, SEP_Z, ENTRY_X, ENTRY_Y, ENTRY_Z);
    std::printf("=====================================\n");

    // Guardrails: every zone the ClearRadius fights from must be on the mesh, and
    // the tank must be able to reach the bridge and traverse it to Pathaleon.
    G3D::Vector3 tmp;
    EXPECT_TRUE(DcNavHarness::NearestPoint(mesh.get(), ENTRY_X, ENTRY_Y, ENTRY_Z, 4.0f, 4.0f, tmp))
        << "bridge entry off the navmesh";
    EXPECT_TRUE(DcNavHarness::NearestPoint(mesh.get(), NEAR_X, NEAR_Y, NEAR_Z, 4.0f, 4.0f, tmp))
        << "near zone off the navmesh";
    EXPECT_TRUE(DcNavHarness::NearestPoint(mesh.get(), ADV_X, ADV_Y, ADV_Z, 4.0f, 4.0f, tmp))
        << "advance point off the navmesh";
    EXPECT_TRUE(DcNavHarness::NearestPoint(mesh.get(), FAR_X, FAR_Y, FAR_Z, 4.0f, 4.0f, tmp))
        << "far zone off the navmesh";
    EXPECT_TRUE(sepToEntry.reachable && sepToEntry.corridorComplete)
        << "cannot path from Sepethrea to the bridge entry";
    EXPECT_TRUE(entryToPath.reachable && entryToPath.corridorComplete)
        << "cannot traverse the bridge from entry to Pathaleon";
}
