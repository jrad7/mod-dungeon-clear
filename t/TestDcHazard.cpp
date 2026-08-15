/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Data/DcHazardRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"

// Pure tests for the hazard-emitter table and its geometry predicates. No map
// data or live game state required, so these run in every build.

namespace
{
    // The five Arcatraz Sentinel (20869) spawns on map 552, from the `creature`
    // table. Kept here so the route-penalty cases below assert against the real
    // coordinates rather than restating the boxes.
    constexpr float kSentinelA[3] = { 255.498f, 158.914f, 22.362f };
    constexpr float kSentinelB[3] = { 253.942f, 131.881f, 22.395f };
    constexpr float kSentinelC[3] = { 264.287f, -61.321f, 22.453f };
    constexpr float kSentinelD[3] = { 336.514f,  27.427f, 48.426f };
    constexpr float kSentinelE[3] = { 395.413f,  18.195f, 48.296f };
}

TEST(DcHazardRegistry, ReportsMapsWithEmitters)
{
    EXPECT_TRUE(DcHazardRegistry::HasEmitters(552));    // The Arcatraz
    EXPECT_FALSE(DcHazardRegistry::HasEmitters(0));
    EXPECT_FALSE(DcHazardRegistry::HasEmitters(554));   // The Mechanar — no rows
}

TEST(DcHazardRegistry, FindIsKeyedOnBothMapAndEntry)
{
    DcHazardEmitter const* sentinel = DcHazardRegistry::Find(552, 20869);
    ASSERT_NE(sentinel, nullptr);
    EXPECT_EQ(sentinel->mapId, 552u);
    EXPECT_EQ(sentinel->creatureEntry, 20869u);
    // 15yd Energy Discharge (36717) + 7yd drift margin.
    EXPECT_FLOAT_EQ(sentinel->radius, 22.0f);

    // The live Sentinel is fought, not vacated — no active-vacate radius.
    EXPECT_FLOAT_EQ(sentinel->vacateRadius, 0.0f);

    // The Destroyed Sentinel (21761) — the summon the party must flee — carries
    // the 15yd vacate pulse, and its camp keep-out radius is the RAW pulse (15),
    // not the padded 22, so a 19yd retreat point clears PointIsHot.
    DcHazardEmitter const* destroyed = DcHazardRegistry::Find(552, 21761);
    ASSERT_NE(destroyed, nullptr);
    EXPECT_FLOAT_EQ(destroyed->vacateRadius, 15.0f);
    EXPECT_FLOAT_EQ(destroyed->radius, 15.0f);

    DcHazardEmitter const* corpse = DcHazardRegistry::Find(552, 21303);
    ASSERT_NE(corpse, nullptr);
    // 8yd SmartAI OOC-LOS trigger + 4yd margin.
    EXPECT_FLOAT_EQ(corpse->radius, 12.0f);
    EXPECT_FLOAT_EQ(corpse->vacateRadius, 0.0f);

    EXPECT_EQ(DcHazardRegistry::Find(552, 99999), nullptr);   // right map, wrong entry
    EXPECT_EQ(DcHazardRegistry::Find(0, 20869), nullptr);     // right entry, wrong map
}

TEST(DcHazardRegistry, PointInsideRespectsRadius)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // Dead centre, and just inside the rim.
    EXPECT_TRUE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 21.5f, 0.0f, 0.0f));

    // Just outside the rim, and comfortably clear.
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 22.5f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 60.0f, 60.0f, 0.0f));
}

TEST(DcHazardRegistry, PointInsideRespectsZBand)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // Directly overhead but on the floor above — the pulse does not reach, and
    // sterilising the catwalk above a Sentinel would be a real routing loss.
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 22.4f, 0.0f, 0.0f, 48.4f));
    // Within the band.
    EXPECT_TRUE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 22.4f, 0.0f, 0.0f, 30.0f));
}

TEST(DcHazardRegistry, SegmentClipsCatchesAPassingLeg)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // A leg whose ENDPOINTS are both well clear but which passes straight
    // through the emitter. This is the case a point-only test misses, and the
    // reason camp validation walks the polyline rather than checking the anchor.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 0.0f,
                                               -50.0f, 0.0f, 0.0f,
                                                50.0f, 0.0f, 0.0f));

    // Same span, offset far enough sideways to miss the circle.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 0.0f,
                                                -50.0f, 40.0f, 0.0f,
                                                 50.0f, 40.0f, 0.0f));

    // Passes overhead on the floor above — both endpoints out of band.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                                -50.0f, 0.0f, 48.4f,
                                                 50.0f, 0.0f, 48.4f));

    // Climbing past the emitter: the far end is out of band but the near end is
    // inside it, so the leg still clips and must be rejected.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                               -10.0f, 0.0f, 24.0f,
                                                50.0f, 0.0f, 48.4f));
}

TEST(DcHazardRegistry, SegmentClipsCatchesAZBandStraddle)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // BOTH endpoints out of band, but on OPPOSITE sides — the leg descends
    // straight through the emitter's z. A naive "both out => clean" test waves
    // this through. Real geometry: Arcatraz has a z48 upper tier, the z22 floor
    // the Sentinels sit on, and Zereketh's z-10 chamber below.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                               0.0f, 0.0f, 48.0f,
                                               0.0f, 0.0f, -10.0f));

    // Same straddle, but offset far enough sideways to miss the circle entirely.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                                40.0f, 0.0f, 48.0f,
                                                40.0f, 0.0f, -10.0f));

    // Both out of band on the SAME side is still clean — this is the floor-above
    // case, and rejecting it would sterilise the catwalk over an emitter.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                                -50.0f, 0.0f, 48.0f,
                                                 50.0f, 0.0f, 50.0f));
}

TEST(DcHazardRegistry, ZeroRadiusEmitterIsInert)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 0.0f, /*zBand*/ 12.0f};
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 0.0f,
                                                -50.0f, 0.0f, 0.0f,
                                                 50.0f, 0.0f, 0.0f));
}

// ---- the route half -----------------------------------------------------
// The nav-penalty boxes that keep the long-range router off the Sentinels.
// These live in DcNavPenaltyRegistry but are authored as part of the hazard
// feature, so they are asserted here alongside it.

TEST(DcHazardRegistry, PenalizesEverySentinelSpawn)
{
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelA[0], kSentinelA[1], kSentinelA[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelB[0], kSentinelB[1], kSentinelB[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelC[0], kSentinelC[1], kSentinelC[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelD[0], kSentinelD[1], kSentinelD[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelE[0], kSentinelE[1], kSentinelE[2]), 1.0f);
}

TEST(DcHazardRegistry, SentinelBoxesStayOffTheRestOfTheInstance)
{
    // Mellichar's arena floor centroid — the objective anchor the finale parks
    // the party on. An over-wide box here would tax the one spot the run MUST
    // stand on, so this is the regression guard on box size.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 445.9f, -161.5f, 42.56f), 1.0f);

    // The three walk-in bosses.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 273.607f, -122.980f, -10.040f), 1.0f);  // Zereketh
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 137.234f,  128.506f,  22.5245f), 1.0f); // Dalliah
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 136.200f,  168.310f,  22.5245f), 1.0f); // Soccothrates

    // The two Containment Core security fields — the party has to stand on the
    // door line to walk through when they open.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 199.827f, 117.488f, 23.877f), 1.0f);
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 199.911f, 102.009f, 23.694f), 1.0f);

    // Same coordinates, different map => no volume applies.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(554, kSentinelA[0], kSentinelA[1], kSentinelA[2]), 1.0f);
}

TEST(DcHazardRegistry, SentinelBoxesDoNotOutrankTheNavmeshShortcutRows)
{
    // A hazard is a "prefer not to" — a navmesh shortcut a player cannot follow
    // is a "this route is wrong". If the two ever tie, a mandatory hazard
    // corridor starts looking as bad as a broken climb and routing gets worse,
    // not better. Keep the hazard tax strictly cheaper.
    float const hazard = DcNavPenaltyRegistry::PenaltyAt(552, kSentinelA[0], kSentinelA[1], kSentinelA[2]);
    float const shortcut = DcNavPenaltyRegistry::PenaltyAt(229, -126.1f, -390.3f, 44.4f);
    EXPECT_LT(hazard, shortcut);
}

// ===== The Eredar room's 45yd auras are cleared, not avoided =====
//
// The three multispawn points roll Eredar Soul-Eater (20879, a harmless 45yd
// slow) or Eredar Deathbringer (20880, a 45yd 450/750-per-2s damage pulse).
// Avoidance is the wrong tool at that width: sized honestly it refuses every
// route through the wing, and sized below caster range it is a lie — at 30yd you
// still take full damage. The party crosses and kills them instead
// (ArcatrazEvents.cpp event 2). These guard against the registry re-growing a
// keep-out row and re-introducing the pathing deadlock.

TEST(DcHazardArcatrazTest, EredarRoomIsNotRegisteredAsEmitters)
{
    EXPECT_EQ(DcHazardRegistry::Find(552, 20879), nullptr);
    EXPECT_EQ(DcHazardRegistry::Find(552, 20880), nullptr);
    // Heroic templates can never be GetEntry(), so a row for either would be dead
    // either way — assert it stays absent so nobody "fixes" the above by adding
    // these instead.
    EXPECT_EQ(DcHazardRegistry::Find(552, 21595), nullptr);
    EXPECT_EQ(DcHazardRegistry::Find(552, 21594), nullptr);
}

TEST(DcHazardArcatrazTest, RegisteredEmittersStillRejectLegs)
{
    // Every surviving emitter is a genuine route hazard: a leg through the
    // Sentinel's pulse must still be refused. Guards the SegmentClips path that
    // the removed loiter-only branch used to short-circuit.
    DcHazardEmitter const* e = DcHazardRegistry::Find(552, 20869);
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(*e, 0, 0, 22, -40, 0, 22, 40, 0, 22));
}
