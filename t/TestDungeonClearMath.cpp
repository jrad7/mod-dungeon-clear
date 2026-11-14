/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DungeonClearMath.h"
#include "DcProgressWatchdog.h"

#include <limits>

// Test point directly on the segment (midpoint)
TEST(DungeonClearMathTest, PointOnSegmentMidpoint)
{
    float px = 5.0f;
    float py = 0.0f;
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 10.0f;
    float by = 0.0f;

    // Midpoint (5,0) on segment (0,0)-(10,0) -> distance should be 0
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(px, py, ax, ay, bx, by), 0.0f, 1e-5f);
}

// Test point on the segment endpoints
TEST(DungeonClearMathTest, PointOnSegmentEndpoints)
{
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 10.0f;
    float by = 0.0f;

    // At A (0,0) -> distance should be 0
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(0.0f, 0.0f, ax, ay, bx, by), 0.0f, 1e-5f);

    // At B (10,0) -> distance should be 0
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(10.0f, 0.0f, ax, ay, bx, by), 0.0f, 1e-5f);
}

// Test point collinear with segment but outside the endpoints
TEST(DungeonClearMathTest, PointCollinearOutsideSegment)
{
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 10.0f;
    float by = 0.0f;

    // Before A (-2,0) -> closest point should be A (0,0) -> distance squared should be 4
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(-2.0f, 0.0f, ax, ay, bx, by), 4.0f, 1e-5f);

    // After B (12,0) -> closest point should be B (10,0) -> distance squared should be 4
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(12.0f, 0.0f, ax, ay, bx, by), 4.0f, 1e-5f);
}

// Test point with perpendicular offset from segment
TEST(DungeonClearMathTest, PointWithPerpendicularOffset)
{
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 10.0f;
    float by = 0.0f;

    // Above midpoint (5,3) -> closest point should be (5,0) -> distance squared should be 9
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(5.0f, 3.0f, ax, ay, bx, by), 9.0f, 1e-5f);

    // Below midpoint (5,-4) -> closest point should be (5,0) -> distance squared should be 16
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(5.0f, -4.0f, ax, ay, bx, by), 16.0f, 1e-5f);
}

// Test zero-length segment (A == B)
TEST(DungeonClearMathTest, ZeroLengthSegment)
{
    float ax = 5.0f;
    float ay = 5.0f;
    float bx = 5.0f;
    float by = 5.0f;

    // Segment is a single point (5,5). Distance to (5,8) should be 3 -> distance squared should be 9
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(5.0f, 8.0f, ax, ay, bx, by), 9.0f, 1e-5f);

    // Distance to (2,5) should be 3 -> distance squared should be 9
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(2.0f, 5.0f, ax, ay, bx, by), 9.0f, 1e-5f);
}

// Test diagonal segment
TEST(DungeonClearMathTest, DiagonalSegment)
{
    // Segment from (0,0) to (6,6)
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 6.0f;
    float by = 6.0f;

    // Point (0,6) -> projection on line is (3,3) -> distance to (3,3) is sqrt(3^2 + 3^2) = sqrt(18)
    // Distance squared should be 18
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(0.0f, 6.0f, ax, ay, bx, by), 18.0f, 1e-5f);

    // Point (3,3) is on the diagonal -> distance should be 0
    EXPECT_NEAR(DungeonClearMath::DistSqToSegment2D(3.0f, 3.0f, ax, ay, bx, by), 0.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Dynamic pull aggro estimate (Leeroy vs Advanced). combatSpread 6 and zTolerance
// 5 mirror the shipped defaults; assistRadius 10 is the engine's default
// CreatureFamilyAssistanceRadius (the caller passes the live config value). The
// caller's Leeroy ceiling is 5 (Advanced when count > 5). Each mob's aggroReach is
// the 6th brace field; the common trash radius used below is ~18yd (a mid-level
// mob's real aggro radius).
// ---------------------------------------------------------------------------
namespace
{
    using DungeonClearMath::DynPullMob;
    constexpr float kSpread = 6.0f;
    constexpr float kAssistR = 10.0f;  // engine CreatureFamilyAssistanceRadius default
    constexpr float kZTol = 5.0f;   // DC_Z_LEVEL_TOLERANCE
    constexpr unsigned kCeil = 5u;  // PullDynamicMaxLeeroyMobs
    constexpr float kReach = 18.0f; // a representative mob aggro radius

    unsigned Count(std::vector<DynPullMob> const& m, std::size_t t)
    {
        return DungeonClearMath::EstimateAggroCount(m, t, kSpread, kAssistR, kZTol);
    }
    // Advanced iff the estimate exceeds the Leeroy ceiling (mirrors the caller).
    bool Advanced(std::vector<DynPullMob> const& m, std::size_t t)
    {
        return Count(m, t) > kCeil;
    }
}

// A single lone mob -> count 1 -> Leeroy.
TEST(DungeonClearDynamicPullTest, SingleMobLeeroy)
{
    std::vector<DynPullMob> mobs = { {0.0f, 0.0f, 0.0f, false, 0u, kReach} };
    EXPECT_EQ(Count(mobs, 0), 1u);
    EXPECT_FALSE(Advanced(mobs, 0));
}

// One bunched pack of 3 (all within aggro reach of the camp) -> count 3 -> Leeroy.
TEST(DungeonClearDynamicPullTest, LoneSmallPackLeeroy)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {3.0f, 0.0f, 0.0f, true, 0u, kReach},
        {0.0f, 4.0f, 0.0f, true, 0u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 3u);
    EXPECT_FALSE(Advanced(mobs, 0));
}

// The motivating case: THREE loose 2-mob packs near each other, all within aggro
// reach of the camp spot -> 6 mobs aggro -> count 6 > ceiling 5 -> Advanced. The
// old pack-adjacency model also Advanced here (any neighbour flips it), but now it
// is because SIX bodies pile in, and raising the ceiling to 6 would Leeroy them.
TEST(DungeonClearDynamicPullTest, ThreeClusteredPairsAdvanced)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {3.0f, 0.0f, 0.0f, true, 0u, kReach},
        {10.0f, 0.0f, 0.0f, true, 0u, kReach}, {13.0f, 0.0f, 0.0f, true, 0u, kReach},
        {0.0f, 10.0f, 0.0f, true, 0u, kReach}, {3.0f, 10.0f, 0.0f, true, 0u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 6u);
    EXPECT_TRUE(Advanced(mobs, 0));
}

// The SAME six mobs, but the two other pairs are spaced well beyond aggro reach of
// the camp (and of each other) -> only the target pair aggros -> count 2 ->
// Leeroy. This is the fix: spaced trivial packs are no longer all camp-pulled.
TEST(DungeonClearDynamicPullTest, ThreeSpacedPairsLeeroy)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {3.0f, 0.0f, 0.0f, true, 0u, kReach},
        // 40yd / 80yd out: beyond reach(18)+spread(6)=24 of the camp, and beyond an
        // assist hop from the target pair.
        {40.0f, 0.0f, 0.0f, true, 0u, kReach}, {43.0f, 0.0f, 0.0f, true, 0u, kReach},
        {80.0f, 0.0f, 0.0f, true, 0u, kReach}, {83.0f, 0.0f, 0.0f, true, 0u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 2u);
    EXPECT_FALSE(Advanced(mobs, 0));
}

// A mob within its aggro reach + combat spread of the camp counts even though it
// is outside dead-centre reach: reach 18 + spread 6 = 24, mob at 22yd -> counts.
// The "messy combat drift" case.
TEST(DungeonClearDynamicPullTest, CombatSpreadPullsInDriftMob)
{
    std::vector<DynPullMob> single = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {22.0f, 0.0f, 0.0f, true, 0u, kReach}
    };
    EXPECT_EQ(Count(single, 0), 2u);  // 22 <= 18 + 6

    // Push it to 25yd (> 24) and, with no assist bridge, it drops out -> count 1.
    std::vector<DynPullMob> beyond = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {25.0f, 0.0f, 0.0f, true, 0u, kReach}
    };
    EXPECT_EQ(Count(beyond, 0), 1u);
}

// A mob within aggro reach but NOT chainEligible (behind a wall / door / a floor
// away) does not aggro -> not counted.
TEST(DungeonClearDynamicPullTest, NotEligibleNotCounted)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {5.0f, 0.0f, 0.0f, false, 0u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 1u);  // packmate gated out
}

// One assist hop, no transitivity. A is the target; B aggros from proximity; C is
// within assistRadius of B (a seed mob) -> joins; D is within assistRadius of C
// only (NOT of any seed) -> must NOT join, because assisted mobs don't chain.
TEST(DungeonClearDynamicPullTest, AssistHopIsExactlyOneRing)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach},   // A target
        {3.0f, 0.0f, 0.0f, true, 0u, kReach},    // B proximity-aggros (seed)
        {11.0f, 0.0f, 0.0f, true, 0u, 1.0f},     // C: 8yd from B (<=assist 10) -> hop
        {20.0f, 0.0f, 0.0f, true, 0u, 1.0f}      // D: 9yd from C, 17yd from B -> NO
    };
    // tiny reach on C/D means they cannot proximity-aggro the camp themselves.
    EXPECT_EQ(Count(mobs, 0), 3u);  // A + B + C, NOT D
}

// 3D: a mob within aggro reach in plan view but a full floor (20yd) ABOVE the
// target -> not counted. A flat 2D estimate would have pulled it in.
TEST(DungeonClearDynamicPullTest, MobOnFloorAboveNotCounted)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {5.0f, 0.0f, 20.0f, true, 0u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 1u);
}

// 3D: the SAME 2D geometry but on our level (within zTolerance) -> counted,
// proving the height gate is what excludes the overhead mob, not the layout.
TEST(DungeonClearDynamicPullTest, MobOnSameLevelCounted)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach}, {5.0f, 0.0f, 3.0f, true, 0u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 2u);
}

// Formation closure: a strung-out FORMATION (shared packId) where only the target
// is near the camp still counts in FULL — you can't pull half a formation. Six
// members, each 20yd from the next (beyond proximity/assist of the others), all
// share id 7 -> count 6 > ceiling -> Advanced. With packId 0 they'd be lone mobs
// out of reach and the count would be 1.
TEST(DungeonClearDynamicPullTest, SpreadFormationCountsInFull)
{
    std::vector<DynPullMob> mobs = {
        {0.0f,  0.0f, 0.0f, false, 7u, kReach}, {20.0f, 0.0f, 0.0f, true, 7u, kReach},
        {40.0f, 0.0f, 0.0f, true,  7u, kReach}, {60.0f, 0.0f, 0.0f, true, 7u, kReach},
        {80.0f, 0.0f, 0.0f, true,  7u, kReach}, {100.0f, 0.0f, 0.0f, true, 7u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 6u);
    EXPECT_TRUE(Advanced(mobs, 0));
}

// Formation closure unions across HEIGHT too: a formation is atomic even if a
// member sits a floor up. Six members share id 3, one of them overhead (20yd up)
// -> still counted -> Advanced. Contrast MobOnFloorAboveNotCounted, where a
// packId-0 overhead mob is correctly excluded by the z-gate.
TEST(DungeonClearDynamicPullTest, FormationClosureUnionsAcrossFloors)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 3u, kReach}, {2.0f, 0.0f, 0.0f, true, 3u, kReach},
        {4.0f, 0.0f, 0.0f, true,  3u, kReach}, {0.0f, 2.0f, 0.0f, true, 3u, kReach},
        {2.0f, 2.0f, 0.0f, true,  3u, kReach},
        {2.0f, 1.0f, 20.0f, false, 3u, kReach}  // overhead but same formation
    };
    EXPECT_EQ(Count(mobs, 0), 6u);
    EXPECT_TRUE(Advanced(mobs, 0));
}

// DISTINCT packIds do NOT union. Target's own 3-mob formation (id 1) is near the
// camp; a separate 3-mob formation (id 2) is 50yd away, out of reach and not an
// assist hop -> only id 1 counts -> count 3 -> Leeroy.
TEST(DungeonClearDynamicPullTest, DistinctFormationsDoNotUnion)
{
    std::vector<DynPullMob> mobs = {
        {0.0f,  0.0f, 0.0f, false, 1u, kReach}, {4.0f,  0.0f, 0.0f, true, 1u, kReach},
        {8.0f,  0.0f, 0.0f, true,  1u, kReach},
        {0.0f, 50.0f, 0.0f, true,  2u, kReach}, {4.0f, 50.0f, 0.0f, true, 2u, kReach},
        {8.0f, 50.0f, 0.0f, true,  2u, kReach}
    };
    EXPECT_EQ(Count(mobs, 0), 3u);
    EXPECT_FALSE(Advanced(mobs, 0));
}

// ---------------------------------------------------------------------------
// ShouldAbortPullForCc — the pull CC-assist grace gate.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldAbortPullForCc;

// Not impaired: never abort, latch cleared to 0 (even from a stale value).
TEST(DungeonClearCcAssistTest, NotImpairedClearsLatch)
{
    std::uint32_t out = 12345u;
    EXPECT_FALSE(ShouldAbortPullForCc(false, 10000u, 20000u, 1000u, out));
    EXPECT_EQ(out, 0u);
}

// First impaired tick arms the latch (to `now`) but does not yet abort.
TEST(DungeonClearCcAssistTest, FirstImpairedTickArmsButHolds)
{
    std::uint32_t out = 0u;
    EXPECT_FALSE(ShouldAbortPullForCc(true, 0u, 5000u, 1000u, out));
    EXPECT_EQ(out, 5000u);
}

// Sustained impairment aborts once the grace has elapsed; the latch is preserved.
TEST(DungeonClearCcAssistTest, AbortsAfterGraceElapsed)
{
    std::uint32_t out = 0u;
    // Armed at 5000, grace 1000ms: still holding at 5999, aborts at 6000.
    EXPECT_FALSE(ShouldAbortPullForCc(true, 5000u, 5999u, 1000u, out));
    EXPECT_EQ(out, 5000u);
    EXPECT_TRUE(ShouldAbortPullForCc(true, 5000u, 6000u, 1000u, out));
    EXPECT_EQ(out, 5000u);
}

// A micro-CC that clears within the grace re-arms fresh next time, so a flicker
// never accumulates toward an abort.
TEST(DungeonClearCcAssistTest, FlickerDoesNotAccumulate)
{
    std::uint32_t out = 0u;
    // Impaired at 5000 (arm), clears at 5500 (latch -> 0), impaired again at 5800
    // re-arms at 5800 — not at the original 5000 — so the grace restarts.
    EXPECT_FALSE(ShouldAbortPullForCc(true, 0u, 5000u, 1000u, out));
    EXPECT_EQ(out, 5000u);
    EXPECT_FALSE(ShouldAbortPullForCc(false, out, 5500u, 1000u, out));
    EXPECT_EQ(out, 0u);
    EXPECT_FALSE(ShouldAbortPullForCc(true, out, 5800u, 1000u, out));
    EXPECT_EQ(out, 5800u);
    // 5800 + 1000 = 6800 is when it would abort, proving the 5000 spell didn't count.
    EXPECT_FALSE(ShouldAbortPullForCc(true, out, 6700u, 1000u, out));
    EXPECT_TRUE(ShouldAbortPullForCc(true, out, 6800u, 1000u, out));
}

// Zero grace aborts on the very first impaired tick.
TEST(DungeonClearCcAssistTest, ZeroGraceAbortsImmediately)
{
    std::uint32_t out = 0u;
    EXPECT_TRUE(ShouldAbortPullForCc(true, 0u, 5000u, 0u, out));
}

// Latching at now == 0 stores 1, not 0 (0 means "clear"), so a tank impaired at
// the very first millisecond still latches and can abort.
TEST(DungeonClearCcAssistTest, ZeroNowLatchesToOne)
{
    std::uint32_t out = 99u;
    EXPECT_FALSE(ShouldAbortPullForCc(true, 0u, 0u, 1000u, out));
    EXPECT_EQ(out, 1u);
}

// ---------------------------------------------------------------------------
// ShouldAbandonPlantedDrag — the pack-cannot-follow gate. A mob holding
// UNIT_STATE_NO_COMBAT_MOVEMENT has no chase generator, so a drag-back cannot
// work at any distance and the tank must turn around and fight it where it
// stands. Shares the ShouldAbortPullForCc latch contract by delegation; these
// pin the behaviour at THIS call site so a future re-implementation that stops
// delegating still has to honour it.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldAbandonPlantedDrag;

// A mob that is chasing normally never abandons the drag, and clears any latch.
TEST(DungeonClearPlantedDragTest, ChasingPackKeepsTheDrag)
{
    std::uint32_t out = 4321u;
    EXPECT_FALSE(ShouldAbandonPlantedDrag(false, 3000u, 9000u, 1500u, out));
    EXPECT_EQ(out, 0u);
}

// First planted tick arms the streak but keeps dragging — the state may be the
// transient kind (a caster planting only for the duration of a cast).
TEST(DungeonClearPlantedDragTest, FirstPlantedTickArmsButKeepsDragging)
{
    std::uint32_t out = 0u;
    EXPECT_FALSE(ShouldAbandonPlantedDrag(true, 0u, 8000u, 1500u, out));
    EXPECT_EQ(out, 8000u);
}

// Held for the confirm window: the drag is abandoned. 1500ms is the shipped
// value and lands well inside the 10s return-leg watchdog it pre-empts.
TEST(DungeonClearPlantedDragTest, SustainedPlantAbandonsAfterConfirmWindow)
{
    std::uint32_t out = 0u;
    EXPECT_FALSE(ShouldAbandonPlantedDrag(true, 8000u, 9499u, 1500u, out));
    EXPECT_EQ(out, 8000u);
    EXPECT_TRUE(ShouldAbandonPlantedDrag(true, 8000u, 9500u, 1500u, out));
    EXPECT_EQ(out, 8000u);
}

// A creature that toggles combat movement off only while it casts must NOT lose
// its drag: each pause re-arms fresh, so brief plants never accumulate.
TEST(DungeonClearPlantedDragTest, TransientPlantDoesNotAccumulate)
{
    std::uint32_t out = 0u;
    // Plants at 8000, resumes chasing at 8900 (streak cleared), plants again at
    // 9200 — the window restarts there, not at 8000.
    EXPECT_FALSE(ShouldAbandonPlantedDrag(true, 0u, 8000u, 1500u, out));
    EXPECT_FALSE(ShouldAbandonPlantedDrag(false, out, 8900u, 1500u, out));
    EXPECT_EQ(out, 0u);
    EXPECT_FALSE(ShouldAbandonPlantedDrag(true, out, 9200u, 1500u, out));
    EXPECT_EQ(out, 9200u);
    // Had 8000 counted, this would already have fired; it must not.
    EXPECT_FALSE(ShouldAbandonPlantedDrag(true, out, 10699u, 1500u, out));
    EXPECT_TRUE(ShouldAbandonPlantedDrag(true, out, 10700u, 1500u, out));
}

// ---------------------------------------------------------------------------
// ShouldTripCampSafety — the camp-safety valve gate (attribution + grace).
// Mirrors the ShouldAbortPullForCc fixture: same latch/clear contract.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldTripCampSafety;

// Splash from the pack being dragged is an ordinary drag, never a valve trip —
// and it must not so much as arm the latch.
TEST(DungeonClearCampSafetyTest, CampSafetyDoesNotTripWhenOnlyThePulledPackIsHitting)
{
    std::uint32_t out = 12345u;
    EXPECT_FALSE(ShouldTripCampSafety(true, 26.0f, 65.0f,
                                      /*attackerIsPullTarget*/ true,
                                      10000u, 20000u, 0u, out));
    EXPECT_EQ(out, 0u);
}

// A third-party attacker below the floor qualifies (zero grace trips at once).
TEST(DungeonClearCampSafetyTest, CampSafetyTripsForAThirdPartyAttacker)
{
    std::uint32_t out = 0u;
    EXPECT_TRUE(ShouldTripCampSafety(true, 26.0f, 65.0f,
                                     /*attackerIsPullTarget*/ false,
                                     0u, 5000u, 0u, out));
}

// A qualifying flicker that clears inside the grace re-arms fresh next time.
TEST(DungeonClearCampSafetyTest, CampSafetyGraceSwallowsAFlicker)
{
    std::uint32_t out = 0u;
    EXPECT_FALSE(ShouldTripCampSafety(true, 40.0f, 65.0f, false, 0u, 5000u, 1500u, out));
    EXPECT_EQ(out, 5000u);
    // Health recovers above the floor -> latch cleared.
    EXPECT_FALSE(ShouldTripCampSafety(true, 80.0f, 65.0f, false, out, 5600u, 1500u, out));
    EXPECT_EQ(out, 0u);
    // Qualifies again at 5800 — the grace restarts from there, not from 5000.
    EXPECT_FALSE(ShouldTripCampSafety(true, 40.0f, 65.0f, false, out, 5800u, 1500u, out));
    EXPECT_EQ(out, 5800u);
    EXPECT_FALSE(ShouldTripCampSafety(true, 40.0f, 65.0f, false, out, 7200u, 1500u, out));
}

// Sustained qualifying state fires once the grace has elapsed.
TEST(DungeonClearCampSafetyTest, CampSafetyFiresAfterSustained)
{
    std::uint32_t out = 0u;
    EXPECT_FALSE(ShouldTripCampSafety(true, 40.0f, 65.0f, false, 0u, 5000u, 1500u, out));
    EXPECT_EQ(out, 5000u);
    EXPECT_FALSE(ShouldTripCampSafety(true, 40.0f, 65.0f, false, out, 6499u, 1500u, out));
    EXPECT_TRUE(ShouldTripCampSafety(true, 40.0f, 65.0f, false, out, 6500u, 1500u, out));
    EXPECT_EQ(out, 5000u);
}

// graceMs == 0 preserves today's behaviour exactly: first qualifying tick fires.
TEST(DungeonClearCampSafetyTest, CampSafetyGraceZeroFiresImmediately)
{
    std::uint32_t out = 0u;
    EXPECT_TRUE(ShouldTripCampSafety(true, 40.0f, 65.0f, false, 0u, 5000u, 0u, out));
}

// Health back above the floor clears the latch (even from a stale value).
TEST(DungeonClearCampSafetyTest, CampSafetyLatchClearsWhenHealthRecovers)
{
    std::uint32_t out = 9999u;
    EXPECT_FALSE(ShouldTripCampSafety(true, 90.0f, 65.0f, false, 9999u, 20000u, 0u, out));
    EXPECT_EQ(out, 0u);
}

// Out of combat, or a disabled valve (floor <= 0), never qualifies.
TEST(DungeonClearCampSafetyTest, CampSafetyRespectsCombatAndDisable)
{
    std::uint32_t out = 7u;
    EXPECT_FALSE(ShouldTripCampSafety(false, 10.0f, 65.0f, false, 7u, 20000u, 0u, out));
    EXPECT_EQ(out, 0u);
    out = 7u;
    EXPECT_FALSE(ShouldTripCampSafety(true, 10.0f, 0.0f, false, 7u, 20000u, 0u, out));
    EXPECT_EQ(out, 0u);
}

// ---------------------------------------------------------------------------
// ShouldStandDownForPull — the pull-mode blocking-trash stand-down gate.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldStandDownForPull;

// The pack the pull pipeline is (or is about to be) working is always its own,
// whatever the phase — the trigger must never preempt the pull for it.
TEST(DungeonClearPullStandDownTest, PullModeStandDownHoldsForThePullsOwnTarget)
{
    EXPECT_TRUE(ShouldStandDownForPull(/*packIsPullsOwn*/ true, /*idle*/ true));
    EXPECT_TRUE(ShouldStandDownForPull(/*packIsPullsOwn*/ true, /*idle*/ false));
}

// A bystander — a pack the pull never selected — with nothing in flight is
// exactly what the aggro-shaped scan exists for: the walk-in owns the tick.
TEST(DungeonClearPullStandDownTest, PullModeStandDownReleasesForABystanderWhenPullIsIdle)
{
    EXPECT_FALSE(ShouldStandDownForPull(/*packIsPullsOwn*/ false, /*idle*/ true));
}

// Once a maneuver is in flight the trigger keeps standing down even for a
// bystander — the maneuver must not be thrashed.
TEST(DungeonClearPullStandDownTest, PullModeStandDownHoldsForABystanderMidManeuver)
{
    EXPECT_TRUE(ShouldStandDownForPull(/*packIsPullsOwn*/ false, /*idle*/ false));
}

// ---------------------------------------------------------------------------
// ShouldYieldToEngageWalk — Advance keeps off a live engage-trash walk-in.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldYieldToEngageWalk;

// tr-20260924-081931-3: engage-trash won every other tick; a fresh, moving
// walk-in on a live target must keep the tick from Advance.
TEST(DungeonClearEngageWalkYieldTest, YieldsToAFreshMovingWalkIn)
{
    EXPECT_TRUE(ShouldYieldToEngageWalk(true, true, true, 10000u, 10150u, 1000u));
    EXPECT_TRUE(ShouldYieldToEngageWalk(true, true, true, 10000u, 11000u, 1000u));
}

// Engage-trash gone quiet past the hold: a real stand-down, Advance resumes.
TEST(DungeonClearEngageWalkYieldTest, ReleasesOnceTheHoldLapses)
{
    EXPECT_FALSE(ShouldYieldToEngageWalk(true, true, true, 10000u, 11001u, 1000u));
}

// A walk that is not moving is a wedge — Advance's stuck ladder must see it.
TEST(DungeonClearEngageWalkYieldTest, ReleasesWhenTheWalkStopsMoving)
{
    EXPECT_FALSE(ShouldYieldToEngageWalk(true, true, false, 10000u, 10100u, 1000u));
}

TEST(DungeonClearEngageWalkYieldTest, ReleasesForADeadOrMissingTarget)
{
    EXPECT_FALSE(ShouldYieldToEngageWalk(true, false, true, 10000u, 10100u, 1000u));
    EXPECT_FALSE(ShouldYieldToEngageWalk(false, true, true, 10000u, 10100u, 1000u));
    EXPECT_FALSE(ShouldYieldToEngageWalk(true, true, true, 0u, 100u, 1000u));
}

// getMSTime() wraps; the unsigned difference still reads the true elapsed time.
TEST(DungeonClearEngageWalkYieldTest, SurvivesMsTimeWrap)
{
    EXPECT_TRUE(ShouldYieldToEngageWalk(true, true, true, 0xFFFFFF00u, 0x00000064u, 1000u));
    EXPECT_FALSE(ShouldYieldToEngageWalk(true, true, true, 0xFFFFFF00u, 0x00000500u, 1000u));
}

// ---------------------------------------------------------------------------
// ShouldTagBossFromStandoff — the boss engage's walk-in is refused outright.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldTagBossFromStandoff;

// sk-20261019-003633 tr-…-12: the walk to Emperor Dagran Thaurissan on his raised
// throne refused at 30.5yd, tank parked, boss in sight — tag once it has lasted.
TEST(DungeonClearBossStandoffTagTest, TagsAPersistentRefusalInSight)
{
    EXPECT_FALSE(ShouldTagBossFromStandoff(true, false, true, 30.5f, 40.0f, 10000u, 12999u, 3000u));
    EXPECT_TRUE(ShouldTagBossFromStandoff(true, false, true, 30.5f, 40.0f, 10000u, 13000u, 3000u));
}

TEST(DungeonClearBossStandoffTagTest, HoldsWhileWalkingOrOutOfSightOrFar)
{
    EXPECT_FALSE(ShouldTagBossFromStandoff(false, false, true, 30.5f, 40.0f, 10000u, 20000u, 3000u));
    EXPECT_FALSE(ShouldTagBossFromStandoff(true, true, true, 30.5f, 40.0f, 10000u, 20000u, 3000u));
    EXPECT_FALSE(ShouldTagBossFromStandoff(true, false, false, 30.5f, 40.0f, 10000u, 20000u, 3000u));
    EXPECT_FALSE(ShouldTagBossFromStandoff(true, false, true, 40.1f, 40.0f, 10000u, 20000u, 3000u));
    EXPECT_FALSE(ShouldTagBossFromStandoff(true, false, true, 30.5f, 40.0f, 0u, 20000u, 3000u));
}

TEST(DungeonClearBossStandoffTagTest, SurvivesMsTimeWrap)
{
    EXPECT_TRUE(ShouldTagBossFromStandoff(true, false, true, 30.5f, 40.0f, 0xFFFFFF00u, 0x00000C00u, 3000u));
    EXPECT_FALSE(ShouldTagBossFromStandoff(true, false, true, 30.5f, 40.0f, 0xFFFFFF00u, 0x00000064u, 3000u));
}

// ---------------------------------------------------------------------------
// ShouldDropTrashSticky — a stale engage-trash sticky yields to the fresh pick.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldDropTrashSticky;

// tr-20260927-201144-5: sticky ~90yd back down the ramp, the trigger's pick
// patrolling 22-38yd ahead. The walk-back loop ends by retargeting.
TEST(DungeonClearTrashStickyTest, DropsAStickyFarBehindTheFreshPick)
{
    EXPECT_TRUE(ShouldDropTrashSticky(true, false, false, true, 90.0f, 30.0f, 15.0f));
}

// Two roughly equidistant corridor mobs: keep the sticky, no flip-flop.
TEST(DungeonClearTrashStickyTest, KeepsTheStickyAgainstAComparablePick)
{
    EXPECT_FALSE(ShouldDropTrashSticky(true, false, false, true, 30.0f, 22.0f, 15.0f));
    EXPECT_FALSE(ShouldDropTrashSticky(true, false, false, true, 45.0f, 30.0f, 15.0f));
}

// tr-20261114-113223-1: the sticky Usher on the opera floor (~44yd) is NEARER than
// the balcony Patron the trigger names (52-58yd), so the margin never fires, but the
// corridor scan rejects it. Out of band means stale, whatever the distance.
TEST(DungeonClearTrashStickyTest, DropsAnOutOfBandStickyEvenWhenNearer)
{
    EXPECT_TRUE(ShouldDropTrashSticky(true, false, false, false, 44.0f, 57.0f, 15.0f));
    EXPECT_TRUE(ShouldDropTrashSticky(true, false, false, false, 30.0f, 22.0f, 15.0f));
}

// A sticky already fighting the party is never abandoned, in band or out.
TEST(DungeonClearTrashStickyTest, KeepsAStickyInCombat)
{
    EXPECT_FALSE(ShouldDropTrashSticky(true, false, true, true, 90.0f, 30.0f, 15.0f));
    EXPECT_FALSE(ShouldDropTrashSticky(true, false, true, false, 44.0f, 57.0f, 15.0f));
}

TEST(DungeonClearTrashStickyTest, KeepsTheStickyWithNoOtherPick)
{
    EXPECT_FALSE(ShouldDropTrashSticky(false, false, false, true, 90.0f, 0.0f, 15.0f));
    EXPECT_FALSE(ShouldDropTrashSticky(false, false, false, false, 90.0f, 0.0f, 15.0f));
    EXPECT_FALSE(ShouldDropTrashSticky(true, true, false, false, 90.0f, 90.0f, 15.0f));
}

// ---------------------------------------------------------------------------
// ShouldReleaseStandingPull — the orphaned-pull release gate.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldReleaseStandingPull;

// The live freeze: the effective mode is off (a persistent anchored event drives
// the tank), the tank is out of combat, and a pull is still standing at Engage
// with a marked camp. Nothing else can clean that up — release it.
TEST(DungeonClearPullReleaseTest, ReleasesAPullLeftStandingWhenTheModeIsForcedOff)
{
    EXPECT_TRUE(ShouldReleaseStandingPull(/*effectiveOn*/ false, /*standing*/ true,
                                          /*partyInCombat*/ false, /*holdingPhase*/ false,
                                          /*bossPullback*/ false));
}

// Pull mode on: the pull's own FSM owns its lifecycle, and a camp mid-run is
// exactly what the party is meant to be holding at.
TEST(DungeonClearPullReleaseTest, NeverReleasesWhileThePullModeIsOn)
{
    EXPECT_FALSE(ShouldReleaseStandingPull(/*effectiveOn*/ true, /*standing*/ true,
                                           /*partyInCombat*/ false, /*holdingPhase*/ false,
                                           /*bossPullback*/ false));
}

// Nothing standing (phase Idle, no camp) — the common case every tick with pull
// mode off. Must not churn.
TEST(DungeonClearPullReleaseTest, NoOpWhenNothingIsStanding)
{
    EXPECT_FALSE(ShouldReleaseStandingPull(/*effectiveOn*/ false, /*standing*/ false,
                                           /*partyInCombat*/ false, /*holdingPhase*/ false,
                                           /*bossPullback*/ false));
}

// A maneuver in flight — anyone in the party fighting, or a holding phase
// (Forming/Advancing/Returning) — must finish: clearing its camp mid-drag would
// dump the party out of the hold and into the inbound pack.
TEST(DungeonClearPullReleaseTest, NeverReleasesAManeuverInFlight)
{
    EXPECT_FALSE(ShouldReleaseStandingPull(/*effectiveOn*/ false, /*standing*/ true,
                                           /*partyInCombat*/ true, /*holdingPhase*/ false,
                                           /*bossPullback*/ false));
    EXPECT_FALSE(ShouldReleaseStandingPull(/*effectiveOn*/ false, /*standing*/ true,
                                           /*partyInCombat*/ false, /*holdingPhase*/ true,
                                           /*bossPullback*/ false));
}

// THE CAMP-FIGHT CASE, and the reason the combat input is party-wide rather than
// the leader's own flag. Phase Engage with a camp marked: the pack has been
// dragged home and handed to the followers, and the TANK is flag-clear for a
// stretch of that fight (a scripted stage tags at range, so the pack arrives
// strung out and threat lands on whoever it reaches first). Releasing there
// dismantles the camp underneath a live fight and frees the tank to go form the
// next pull with the last pack still up — the rotunda's pack-stacking collapse.
TEST(DungeonClearPullReleaseTest, NeverReleasesACampTheFollowersAreStillFightingAt)
{
    EXPECT_FALSE(ShouldReleaseStandingPull(/*effectiveOn*/ false, /*standing*/ true,
                                           /*partyInCombat*/ true, /*holdingPhase*/ false,
                                           /*bossPullback*/ false));
}

// A pull-back drag (Ghaz'an out of the Underbog lake) runs with pull mode off BY
// DESIGN — its camp is the hand-authored anchor and must survive.
TEST(DungeonClearPullReleaseTest, NeverReleasesABossPullback)
{
    EXPECT_FALSE(ShouldReleaseStandingPull(/*effectiveOn*/ false, /*standing*/ true,
                                           /*partyInCombat*/ false, /*holdingPhase*/ false,
                                           /*bossPullback*/ true));
}

// ---------------------------------------------------------------------------
// ShouldAdvanceUnclassifiedAggro — the unclassified-aggro gate on sweep maps.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldAdvanceUnclassifiedAggro;

// The live case (tr-20260927-003140-1, Kara ballroom): mode dropped to off after
// the camp fight, the tank walked the route, a cluster the band never picked bit
// it at 18yd. In combat, Idle, bool off, no verdict, sweep map -> ADVANCED.
TEST(DungeonClearUnclassifiedAggroTest, AdvancesAnUnsizedAggroOnASweepMap)
{
    EXPECT_TRUE(ShouldAdvanceUnclassifiedAggro(/*inCombat*/ true, /*phaseIdle*/ true,
                                               /*modeOn*/ false, /*hasVerdict*/ false,
                                               /*sweepMap*/ true, /*bossInFight*/ false));
}

// A standing verdict is the classifier's choice — a LEEROY walk-in stays a
// walk-in, an ADVANCED pull is already the maneuver's. Never overridden.
TEST(DungeonClearUnclassifiedAggroTest, AStandingVerdictStands)
{
    EXPECT_FALSE(ShouldAdvanceUnclassifiedAggro(true, true, false, /*hasVerdict*/ true, true, false));
}

// Bool already on: the pull action is live and its own Idle branch drags the
// aggro home. Nothing to flip.
TEST(DungeonClearUnclassifiedAggroTest, NothingToDoWhenTheModeIsAlreadyOn)
{
    EXPECT_FALSE(ShouldAdvanceUnclassifiedAggro(true, true, /*modeOn*/ true, false, true, false));
}

// Off a sweep map an unplanned aggro is a lone patrol; dragging it costs the full
// FSM for nothing. The registry row is the evidence gate.
TEST(DungeonClearUnclassifiedAggroTest, SweepMapsOnly)
{
    EXPECT_FALSE(ShouldAdvanceUnclassifiedAggro(true, true, false, false, /*sweepMap*/ false, false));
}

// Not an aggro taken while scouting: out of combat there is nothing to answer,
// and a non-Idle phase is a maneuver in flight that owns its own verdict.
TEST(DungeonClearUnclassifiedAggroTest, OnlyAnAggroTakenWhileScouting)
{
    EXPECT_FALSE(ShouldAdvanceUnclassifiedAggro(/*inCombat*/ false, true, false, false, true, false));
    EXPECT_FALSE(ShouldAdvanceUnclassifiedAggro(true, /*phaseIdle*/ false, false, false, true, false));
}

// A boss (or his summoned add) among the attackers is the at-boss path's fight,
// never a trash drag-back. tr-20260927-094926-4: Maiden's flag landed at Idle with
// no verdict after the raid muster released her pull, and the gate dragged her.
TEST(DungeonClearUnclassifiedAggroTest, NeverDragsABoss)
{
    EXPECT_FALSE(ShouldAdvanceUnclassifiedAggro(true, true, false, false, true,
                                                /*bossInFight*/ true));
}

// ---------------------------------------------------------------------------
// ShouldDropPullVerdict — the no-target verdict-drop grace gate.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldDropPullVerdict;

// Target present: never drop, latch cleared to 0 (even from a stale value).
TEST(DungeonClearVerdictGraceTest, PresentClearsLatch)
{
    std::uint32_t out = 12345u;
    EXPECT_FALSE(ShouldDropPullVerdict(true, 10000u, 20000u, 1500u, out));
    EXPECT_EQ(out, 0u);
}

// First null tick arms the latch (to `now`) but does not yet drop the verdict.
TEST(DungeonClearVerdictGraceTest, FirstNullTickArmsButHolds)
{
    std::uint32_t out = 0u;
    EXPECT_FALSE(ShouldDropPullVerdict(false, 0u, 5000u, 1500u, out));
    EXPECT_EQ(out, 5000u);
}

// A target lost continuously past the grace drops the verdict; latch preserved.
TEST(DungeonClearVerdictGraceTest, DropsAfterGraceElapsed)
{
    std::uint32_t out = 0u;
    // Armed at 5000, grace 1500ms: still holding at 6499, drops at 6500.
    EXPECT_FALSE(ShouldDropPullVerdict(false, 5000u, 6499u, 1500u, out));
    EXPECT_EQ(out, 5000u);
    EXPECT_TRUE(ShouldDropPullVerdict(false, 5000u, 6500u, 1500u, out));
    EXPECT_EQ(out, 5000u);
}

// A transient null (door-veto flicker, cache mid-rebuild) that resolves within
// the grace re-arms fresh next time, so flickers never accumulate to a drop.
TEST(DungeonClearVerdictGraceTest, FlickerDoesNotAccumulate)
{
    std::uint32_t out = 0u;
    // Lost at 5000 (arm), present again at 5400 (latch -> 0), lost again at 5800
    // re-arms at 5800 — not at the original 5000 — so the grace restarts.
    EXPECT_FALSE(ShouldDropPullVerdict(false, 0u, 5000u, 1500u, out));
    EXPECT_EQ(out, 5000u);
    EXPECT_FALSE(ShouldDropPullVerdict(true, out, 5400u, 1500u, out));
    EXPECT_EQ(out, 0u);
    EXPECT_FALSE(ShouldDropPullVerdict(false, out, 5800u, 1500u, out));
    EXPECT_EQ(out, 5800u);
    // 5800 + 1500 = 7300 is when it drops, proving the 5000 loss didn't count.
    EXPECT_FALSE(ShouldDropPullVerdict(false, out, 7299u, 1500u, out));
    EXPECT_TRUE(ShouldDropPullVerdict(false, out, 7300u, 1500u, out));
}

// Zero grace drops on the very first null tick (the pre-grace behavior).
TEST(DungeonClearVerdictGraceTest, ZeroGraceDropsImmediately)
{
    std::uint32_t out = 0u;
    EXPECT_TRUE(ShouldDropPullVerdict(false, 0u, 5000u, 0u, out));
}

// Latching at now == 0 stores 1, not 0 (0 means "present"), so a target lost at
// the very first millisecond still latches and can drop.
TEST(DungeonClearVerdictGraceTest, ZeroNowLatchesToOne)
{
    std::uint32_t out = 99u;
    EXPECT_FALSE(ShouldDropPullVerdict(false, 0u, 0u, 1500u, out));
    EXPECT_EQ(out, 1u);
}

// ---------------------------------------------------------------------------
// ShouldRollInForLeeroy — the Leeroy roll-in scout-lag release gate.
// ---------------------------------------------------------------------------
using DungeonClearMath::ShouldRollInForLeeroy;

// Only decision == 1 (standing Leeroy) releases; 0 (none, still scouting) and
// 2 (Advanced — the camp machinery owns the party) hold the lag even in range.
TEST(DungeonClearRollInTest, OnlyLeeroyDecisionRollsIn)
{
    EXPECT_FALSE(ShouldRollInForLeeroy(0u, true, 10.0f, 20.0f, 8.0f));
    EXPECT_TRUE(ShouldRollInForLeeroy(1u, true, 10.0f, 20.0f, 8.0f));
    EXPECT_FALSE(ShouldRollInForLeeroy(2u, true, 10.0f, 20.0f, 8.0f));
}

// A dead/unresolvable verdict target never rolls in, regardless of distance.
TEST(DungeonClearRollInTest, DeadTargetNeverRollsIn)
{
    EXPECT_FALSE(ShouldRollInForLeeroy(1u, false, 10.0f, 20.0f, 8.0f));
    EXPECT_FALSE(ShouldRollInForLeeroy(1u, false, 0.0f, 20.0f, 8.0f));
}

// Boundary is inclusive at commitRange + lead; just beyond it holds.
TEST(DungeonClearRollInTest, BoundaryAtCommitPlusLead)
{
    EXPECT_TRUE(ShouldRollInForLeeroy(1u, true, 28.0f, 20.0f, 8.0f));
    EXPECT_FALSE(ShouldRollInForLeeroy(1u, true, 28.1f, 20.0f, 8.0f));
}

// lead == 0 releases only once the tank reaches the commit range itself.
TEST(DungeonClearRollInTest, ZeroLeadReleasesOnlyAtCommitRange)
{
    EXPECT_FALSE(ShouldRollInForLeeroy(1u, true, 20.5f, 20.0f, 0.0f));
    EXPECT_TRUE(ShouldRollInForLeeroy(1u, true, 20.0f, 20.0f, 0.0f));
    EXPECT_TRUE(ShouldRollInForLeeroy(1u, true, 15.0f, 20.0f, 0.0f));
}

// Degenerate distances (tank on top of / past the pack) still roll in — the
// tank could not be more committed.
TEST(DungeonClearRollInTest, ZeroOrNegativeDistanceRollsIn)
{
    EXPECT_TRUE(ShouldRollInForLeeroy(1u, true, 0.0f, 20.0f, 8.0f));
    EXPECT_TRUE(ShouldRollInForLeeroy(1u, true, -1.0f, 20.0f, 8.0f));
}

// --- FindTrailRejoin (breadcrumb truncate-don't-clear) --------------------

using DungeonClearMath::FindTrailRejoin;
using DungeonClearMath::TrailRejoinNone;

// Helper: build a straight-line trail of `n` crumbs spaced 4yd on +X at z=0.
static std::vector<Position> MakeLine(std::size_t n)
{
    std::vector<Position> v;
    for (std::size_t i = 0; i < n; ++i)
        v.emplace_back(static_cast<float>(i) * 4.0f, 0.0f, 0.0f, 0.0f);
    return v;
}

TEST(DungeonClearTrailRejoinTest, RejoinsAtExactCampCrumb)
{
    // 7 crumbs at x = 0,4,..,24. Bot stands on the LAST crumb (x=24): even though
    // crumb 5 (x=20) is also within 6yd behind it, latest-wins returns crumb 6
    // (the standing-at-camp -> rejoin-the-camp-crumb case).
    std::vector<Position> trail = MakeLine(7);
    Position const cur(24.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(FindTrailRejoin(trail, cur, 6.0f), 6u);
}

TEST(DungeonClearTrailRejoinTest, RejoinsAtNearestWithinRadius)
{
    // 2yd past the last crumb (x=24): within 6yd of crumbs 5 (x=20) and 6
    // (x=24); latest-wins picks crumb 6.
    std::vector<Position> trail = MakeLine(7);
    Position const cur(26.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(FindTrailRejoin(trail, cur, 6.0f), 6u);
}

TEST(DungeonClearTrailRejoinTest, LatestWinsOnSelfLoop)
{
    // Trail loops back near its own start: an early crumb and a late crumb both
    // sit at the origin. Rejoin must pick the LATEST (index 6), not index 0.
    std::vector<Position> trail;
    trail.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);     // 0  (start)
    trail.emplace_back(10.0f, 0.0f, 0.0f, 0.0f);    // 1
    trail.emplace_back(20.0f, 0.0f, 0.0f, 0.0f);    // 2
    trail.emplace_back(20.0f, 10.0f, 0.0f, 0.0f);   // 3
    trail.emplace_back(10.0f, 10.0f, 0.0f, 0.0f);   // 4
    trail.emplace_back(0.0f, 10.0f, 0.0f, 0.0f);    // 5
    trail.emplace_back(1.0f, 1.0f, 0.0f, 0.0f);     // 6  (back near start)
    Position const cur(0.5f, 0.5f, 0.0f, 0.0f);
    EXPECT_EQ(FindTrailRejoin(trail, cur, 6.0f), 6u);
}

TEST(DungeonClearTrailRejoinTest, NoRejoinOnTrueTeleport)
{
    std::vector<Position> trail = MakeLine(10);     // all near the X axis
    Position const cur(500.0f, 500.0f, 0.0f, 0.0f); // far away -> teleport
    EXPECT_EQ(FindTrailRejoin(trail, cur, 6.0f), TrailRejoinNone);
}

TEST(DungeonClearTrailRejoinTest, RadiusBoundaryIsInclusive)
{
    std::vector<Position> trail = MakeLine(3);      // crumbs at x = 0,4,8
    // Exactly 6yd from crumb 2 (x=8) -> at the boundary -> rejoins (<=).
    Position const cur(14.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(FindTrailRejoin(trail, cur, 6.0f), 2u);
    // Just past the boundary from any crumb -> no rejoin.
    Position const past(14.01f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(FindTrailRejoin(trail, past, 6.0f), TrailRejoinNone);
}

TEST(DungeonClearTrailRejoinTest, VerticalSeparationIsNotARejoin)
{
    // A crumb directly below the bot (same X/Y, 20yd down) must NOT count: the
    // rejoin test is 3D so a different floor never rejoins.
    std::vector<Position> trail;
    trail.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);
    Position const cur(0.0f, 0.0f, 20.0f, 0.0f);
    EXPECT_EQ(FindTrailRejoin(trail, cur, 6.0f), TrailRejoinNone);
}

TEST(DungeonClearTrailRejoinTest, EmptyTrailAndBadRadius)
{
    std::vector<Position> empty;
    Position const cur(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(FindTrailRejoin(empty, cur, 6.0f), TrailRejoinNone);
    // Non-positive radius never rejoins even on a coincident crumb.
    std::vector<Position> one = MakeLine(1);
    EXPECT_EQ(FindTrailRejoin(one, cur, 0.0f), TrailRejoinNone);
}

// --- ShouldPlantEarly (turn-and-plant drag-back gate) ---------------------

using DungeonClearMath::ShouldPlantEarly;

// Helper: N attackers all at the same distance.
static std::vector<float> Attackers(std::size_t n, float dist)
{
    return std::vector<float>(n, dist);
}

// Pack gathered (all within glue) for the required ticks past half the leg:
// arms on tick 1, plants on tick 2 (glueTicksNeeded == 2).
TEST(DungeonClearPlantTest, GatheredPackPlantsAfterDebounce)
{
    std::uint32_t ticks = 0;
    std::vector<float> const dists = Attackers(3, 4.0f);
    // legStartDist 40, distToCamp 10 (well past half) -> qualifies each tick.
    EXPECT_FALSE(ShouldPlantEarly(dists, 6.0f, 2u, false, 10.0f, 40.0f, ticks));
    EXPECT_EQ(ticks, 1u);
    EXPECT_TRUE(ShouldPlantEarly(dists, 6.0f, 2u, false, 10.0f, 40.0f, ticks));
    EXPECT_EQ(ticks, 2u);
}

// A single straggler outside the glue radius vetoes the plant and resets the
// debounce latch.
TEST(DungeonClearPlantTest, StragglerVetoesAndResets)
{
    std::uint32_t ticks = 1;  // armed from a prior gathered tick
    std::vector<float> dists = Attackers(2, 4.0f);
    dists.push_back(9.0f);    // one mob still 9yd back
    EXPECT_FALSE(ShouldPlantEarly(dists, 6.0f, 2u, false, 10.0f, 40.0f, ticks));
    EXPECT_EQ(ticks, 0u);
}

// LOS-break pulls never plant short (must reach the corner); latch reset.
TEST(DungeonClearPlantTest, LosPullNeverPlants)
{
    std::uint32_t ticks = 5;
    std::vector<float> const dists = Attackers(3, 2.0f);
    EXPECT_FALSE(ShouldPlantEarly(dists, 6.0f, 2u, true, 5.0f, 40.0f, ticks));
    EXPECT_EQ(ticks, 0u);
}

// Less than half the return leg covered vetoes (distToCamp > legStartDist/2).
TEST(DungeonClearPlantTest, FirstHalfOfLegRequired)
{
    std::uint32_t ticks = 0;
    std::vector<float> const dists = Attackers(3, 3.0f);
    // legStartDist 40 -> half is 20; distToCamp 25 is too early.
    EXPECT_FALSE(ShouldPlantEarly(dists, 6.0f, 2u, false, 25.0f, 40.0f, ticks));
    EXPECT_EQ(ticks, 0u);
    // Exactly at half qualifies (inclusive).
    EXPECT_FALSE(ShouldPlantEarly(dists, 6.0f, 2u, false, 20.0f, 40.0f, ticks));
    EXPECT_EQ(ticks, 1u);
}

// Nothing chasing (evade/fizzle): never a plant, latch cleared.
TEST(DungeonClearPlantTest, EmptyAttackersNeverPlants)
{
    std::uint32_t ticks = 1;
    std::vector<float> const none;
    EXPECT_FALSE(ShouldPlantEarly(none, 6.0f, 2u, false, 5.0f, 40.0f, ticks));
    EXPECT_EQ(ticks, 0u);
}

// An unstamped leg (legStartDist <= 0) can never qualify.
TEST(DungeonClearPlantTest, NoLegStartNeverPlants)
{
    std::uint32_t ticks = 0;
    std::vector<float> const dists = Attackers(2, 2.0f);
    EXPECT_FALSE(ShouldPlantEarly(dists, 6.0f, 2u, false, 1.0f, 0.0f, ticks));
    EXPECT_EQ(ticks, 0u);
}

// A noisy gather/break/gather sequence cannot accumulate across the break.
TEST(DungeonClearPlantTest, FlickerDoesNotAccumulate)
{
    std::uint32_t ticks = 0;
    std::vector<float> const tight = Attackers(2, 3.0f);
    std::vector<float> loose = Attackers(1, 3.0f);
    loose.push_back(10.0f);
    EXPECT_FALSE(ShouldPlantEarly(tight, 6.0f, 2u, false, 10.0f, 40.0f, ticks)); // 1
    EXPECT_FALSE(ShouldPlantEarly(loose, 6.0f, 2u, false, 10.0f, 40.0f, ticks)); // reset
    EXPECT_EQ(ticks, 0u);
    EXPECT_FALSE(ShouldPlantEarly(tight, 6.0f, 2u, false, 10.0f, 40.0f, ticks)); // 1 again
    EXPECT_TRUE(ShouldPlantEarly(tight, 6.0f, 2u, false, 10.0f, 40.0f, ticks));  // 2
}

// --- ShouldHoldThreatLead (threat-lead damage hold) -----------------------
// Signature: (exempt, beingAttacked, combatSinceMs, firstHitMs, now, leadMs,
// noHitCapMs, tankHp, panicHp). Pull Lab O2: a DPS landed damage 0.0-1.2s after
// the tank's first hit in 20/33 scenarios, because only DC's own assist honoured
// the lead and it measured from the combat flag, not the first hit.

TEST(DungeonClearMathTest, ThreatLeadHoldsDpsUntilTheLeadFromTheFirstHit)
{
    using DungeonClearMath::ShouldHoldThreatLead;
    // Flag at 1000, first hit at 3000: still held at 4000 (1.0s after the hit),
    // released at 4500 (1.5s) — though 3.5s have passed since the flag.
    EXPECT_TRUE(ShouldHoldThreatLead(false, false, 1000, 3000, 4000, 1500, 2500, 100.0f, 60.0f));
    EXPECT_FALSE(ShouldHoldThreatLead(false, false, 1000, 3000, 4500, 1500, 2500, 100.0f, 60.0f));
}

TEST(DungeonClearMathTest, ThreatLeadNoHitIsCapped)
{
    using DungeonClearMath::ShouldHoldThreatLead;
    // No hit yet (a taunt pull, a stunned tank): held lead + cap from the flag.
    EXPECT_TRUE(ShouldHoldThreatLead(false, false, 1000, 0, 4999, 1500, 2500, 100.0f, 60.0f));
    EXPECT_FALSE(ShouldHoldThreatLead(false, false, 1000, 0, 5000, 1500, 2500, 100.0f, 60.0f));
}

TEST(DungeonClearMathTest, ThreatLeadBypasses)
{
    using DungeonClearMath::ShouldHoldThreatLead;
    EXPECT_FALSE(ShouldHoldThreatLead(true, false, 1000, 3000, 3100, 1500, 2500, 100.0f, 60.0f));   // healer/tank
    EXPECT_FALSE(ShouldHoldThreatLead(false, true, 1000, 3000, 3100, 1500, 2500, 100.0f, 60.0f));   // being hit
    EXPECT_FALSE(ShouldHoldThreatLead(false, false, 0, 0, 3100, 1500, 2500, 100.0f, 60.0f));        // no fight
    EXPECT_FALSE(ShouldHoldThreatLead(false, false, 1000, 3000, 3100, 0, 2500, 100.0f, 60.0f));     // feature off
    EXPECT_FALSE(ShouldHoldThreatLead(false, false, 1000, 3000, 3100, 1500, 2500, 40.0f, 60.0f));   // tank panic
    EXPECT_TRUE(ShouldHoldThreatLead(false, false, 1000, 3000, 2900, 1500, 2500, 100.0f, 60.0f));   // stamp ahead
}

// --- ShouldReleaseFollower (threat-lead window) ---------------------------

using DungeonClearMath::ShouldReleaseFollower;

// Signature: ShouldReleaseFollower(isHealer, alreadyInCombat, combatSinceMs, now,
// leadMs, tankHp, panicHp). The existing lead-window cases all model a FRESH,
// out-of-combat DPS/healer (alreadyInCombat == false).

// Healers release immediately, regardless of an unexpired lead.
TEST(DungeonClearReleaseTest, HealerReleasesImmediately)
{
    EXPECT_TRUE(ShouldReleaseFollower(true, false, 5000u, 5100u, 1500u, 100.0f, 60.0f));
}

// DPS held inside the lead window, released once it elapses.
TEST(DungeonClearReleaseTest, DpsHeldThenReleased)
{
    // 100ms into a 1500ms lead -> held.
    EXPECT_FALSE(ShouldReleaseFollower(false, false, 5000u, 5100u, 1500u, 100.0f, 60.0f));
    // Exactly at the lead boundary -> released (inclusive).
    EXPECT_TRUE(ShouldReleaseFollower(false, false, 5000u, 6500u, 1500u, 100.0f, 60.0f));
    // Well past -> released.
    EXPECT_TRUE(ShouldReleaseFollower(false, false, 5000u, 9000u, 1500u, 100.0f, 60.0f));
}

// A tank below the panic HP releases the party early despite the lead.
TEST(DungeonClearReleaseTest, PanicHpBypassesLead)
{
    // 100ms in, but tank at 55% < 60% panic -> release now.
    EXPECT_TRUE(ShouldReleaseFollower(false, false, 5000u, 5100u, 1500u, 55.0f, 60.0f));
    // Tank healthy -> still held.
    EXPECT_FALSE(ShouldReleaseFollower(false, false, 5000u, 5100u, 1500u, 80.0f, 60.0f));
    // panicHp 0 disables the bypass: a near-dead tank stays held inside the lead.
    EXPECT_FALSE(ShouldReleaseFollower(false, false, 5000u, 5100u, 1500u, 1.0f, 0.0f));
}

// leadMs == 0 turns the feature off: DPS release at once.
TEST(DungeonClearReleaseTest, ZeroLeadOff)
{
    EXPECT_TRUE(ShouldReleaseFollower(false, false, 5000u, 5000u, 0u, 100.0f, 60.0f));
}

// combatSince == 0 (leader not observed in combat): gate is moot, release.
TEST(DungeonClearReleaseTest, NoCombatStampReleases)
{
    EXPECT_TRUE(ShouldReleaseFollower(false, false, 0u, 5100u, 1500u, 100.0f, 60.0f));
}

// alreadyInCombat bypass: a follower ALREADY flagged in combat is released ONTO the
// tank's fight regardless of an unexpired lead — the "dps run to me, not the tank"
// fix. It must not be stranded through the lead where stock follow-master (-> the
// human) would win the tick. Bypasses even a healthy tank deep inside the lead.
TEST(DungeonClearReleaseTest, AlreadyInCombatBypassesLead)
{
    // 100ms into a 1500ms lead, tank at full HP -> a FRESH DPS is held...
    EXPECT_FALSE(ShouldReleaseFollower(false, false, 5000u, 5100u, 1500u, 100.0f, 60.0f));
    // ...but the SAME instant, an already-in-combat DPS is released.
    EXPECT_TRUE(ShouldReleaseFollower(false, true, 5000u, 5100u, 1500u, 100.0f, 60.0f));
    // Still released even at t == combatSince (0ms elapsed).
    EXPECT_TRUE(ShouldReleaseFollower(false, true, 5000u, 5000u, 1500u, 100.0f, 60.0f));
}

// --- Elite weighting: pull weight in thirds of an elite -------------------
//
// The verdict weighs the counted set instead of its raw body count: an elite is a
// full unit (3 thirds), a normal a third (1). The DynPullMob `elite` flag is the
// 8th brace field. Body count (EstimateAggroCount's return) is unchanged — only
// the weightThirdsOut tally and the verdict that compares it to a x3-scaled
// ceiling react to it.

namespace
{
    // Pull weight, in thirds of an elite, of the counted set.
    unsigned Weight(std::vector<DynPullMob> const& m, std::size_t t)
    {
        std::uint32_t w = 0;
        DungeonClearMath::EstimateAggroCount(m, t, kSpread, kAssistR, kZTol,
                                             /*excludeLonePatrollers*/ false,
                                             /*countedOut*/ nullptr, &w);
        return w;
    }
    // The production verdict: weighted tally vs the x3-scaled ceiling.
    bool WeightedAdvanced(std::vector<DynPullMob> const& m, std::size_t t)
    {
        return Weight(m, t) > kCeil * 3u;
    }
}

// All-normal pack: weight == body count (each normal is 1 third).
TEST(DungeonClearEliteWeightTest, AllNormalWeighsOnePerBody)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach, false, /*elite*/ false},
        {3.0f, 0.0f, 0.0f, true,  0u, kReach, false, /*elite*/ false},
        {0.0f, 4.0f, 0.0f, true,  0u, kReach, false, /*elite*/ false}
    };
    EXPECT_EQ(Count(mobs, 0), 3u);
    EXPECT_EQ(Weight(mobs, 0), 3u);  // 3 normals = 3 thirds
}

// All-elite pack: each body weighs 3 thirds.
TEST(DungeonClearEliteWeightTest, AllEliteWeighsThreePerBody)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach, false, /*elite*/ true},
        {3.0f, 0.0f, 0.0f, true,  0u, kReach, false, /*elite*/ true},
        {0.0f, 4.0f, 0.0f, true,  0u, kReach, false, /*elite*/ true}
    };
    EXPECT_EQ(Count(mobs, 0), 3u);
    EXPECT_EQ(Weight(mobs, 0), 9u);  // 3 elites = 9 thirds
}

// The motivating fix: a big room of NORMAL trash that body-counts ABOVE the
// ceiling (6 > 5) now weighs only 2 elite-equivalents (6 thirds) -> stays Leeroy.
TEST(DungeonClearEliteWeightTest, LargeNormalRoomStaysLeeroy)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach, false, false},
        {3.0f, 0.0f, 0.0f, true,  0u, kReach, false, false},
        {6.0f, 0.0f, 0.0f, true,  0u, kReach, false, false},
        {9.0f, 0.0f, 0.0f, true,  0u, kReach, false, false},
        {12.0f, 0.0f, 0.0f, true, 0u, kReach, false, false},
        {15.0f, 0.0f, 0.0f, true, 0u, kReach, false, false}
    };
    EXPECT_EQ(Count(mobs, 0), 6u);             // six bodies
    EXPECT_EQ(Weight(mobs, 0), 6u);            // = 2 elite-equiv
    EXPECT_FALSE(WeightedAdvanced(mobs, 0));   // 6 <= ceiling 15 thirds -> Leeroy
}

// Six ELITE bodies clear the same ceiling (18 > 15 thirds) -> Advanced, as before.
TEST(DungeonClearEliteWeightTest, LargeEliteRoomGoesAdvanced)
{
    std::vector<DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach, false, true},
        {3.0f, 0.0f, 0.0f, true,  0u, kReach, false, true},
        {6.0f, 0.0f, 0.0f, true,  0u, kReach, false, true},
        {9.0f, 0.0f, 0.0f, true,  0u, kReach, false, true},
        {12.0f, 0.0f, 0.0f, true, 0u, kReach, false, true},
        {15.0f, 0.0f, 0.0f, true, 0u, kReach, false, true}
    };
    EXPECT_EQ(Count(mobs, 0), 6u);
    EXPECT_EQ(Weight(mobs, 0), 18u);
    EXPECT_TRUE(WeightedAdvanced(mobs, 0));    // 18 > 15 thirds -> Advanced
}

// Just past the ceiling on weight: six elites (18) is Advanced, five (15) is the
// boundary (15 == ceiling, not >) so Leeroy — confirms the > comparison and scale.
TEST(DungeonClearEliteWeightTest, FiveElitesIsTheLeeroyBoundary)
{
    std::vector<DynPullMob> five = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach, false, true},
        {3.0f, 0.0f, 0.0f, true,  0u, kReach, false, true},
        {6.0f, 0.0f, 0.0f, true,  0u, kReach, false, true},
        {9.0f, 0.0f, 0.0f, true,  0u, kReach, false, true},
        {12.0f, 0.0f, 0.0f, true, 0u, kReach, false, true}
    };
    EXPECT_EQ(Weight(five, 0), 15u);
    EXPECT_FALSE(WeightedAdvanced(five, 0));    // 15 == ceiling -> Leeroy
}

// --- Patrol-wait: EstimateAggroCount lone-patroller exclusion --------------

// Helper: the reduced pass (lone patrollers excluded).
static unsigned CountReduced(std::vector<DungeonClearMath::DynPullMob> const& m,
                             std::size_t t)
{
    return DungeonClearMath::EstimateAggroCount(m, t, kSpread, kAssistR, kZTol,
                                                /*excludeLonePatrollers*/ true);
}

// A lone patroller (packId 0) that tips a clean 2-mob Leeroy over the ceiling is
// dropped in the reduced pass: full counts it, reduced does not.
TEST(DungeonClearPatrolWaitTest, LonePatrollerDropsInReducedPass)
{
    // Target pair (count 2) + four more bodies in reach, one of them a lone
    // patroller. Full = 6 (> ceiling 5 => Advanced); reduced without the patroller
    // = 5 (<= ceiling => clean Leeroy) => patrol-contended.
    std::vector<DungeonClearMath::DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach},               // 0 target
        {3.0f, 0.0f, 0.0f, true,  0u, kReach},               // 1 packmate
        {6.0f, 0.0f, 0.0f, true,  0u, kReach},               // 2
        {9.0f, 0.0f, 0.0f, true,  0u, kReach},               // 3
        {12.0f, 0.0f, 0.0f, true, 0u, kReach},               // 4
        {15.0f, 0.0f, 0.0f, true, 0u, kReach, /*patrol*/ true} // 5 lone patroller
    };
    EXPECT_EQ(Count(mobs, 0), 6u);
    EXPECT_EQ(CountReduced(mobs, 0), 5u);
}

// A patroller that shares an atomic pack (packId != 0) is NOT droppable — you
// cannot wait out half a formation — so the reduced pass still counts it.
TEST(DungeonClearPatrolWaitTest, FormationPatrollerStaysInReducedPass)
{
    // Mob 5 is a patroller but linked (packId 7) to mob 4; both come as a unit.
    std::vector<DungeonClearMath::DynPullMob> mobs = {
        {0.0f, 0.0f, 0.0f, false, 0u, kReach},                 // 0 target
        {3.0f, 0.0f, 0.0f, true,  0u, kReach},                 // 1
        {6.0f, 0.0f, 0.0f, true,  0u, kReach},                 // 2
        {9.0f, 0.0f, 0.0f, true,  0u, kReach},                 // 3
        {12.0f, 0.0f, 0.0f, true, 7u, kReach},                 // 4 formation
        {15.0f, 0.0f, 0.0f, true, 7u, kReach, /*patrol*/ true} // 5 patroller, linked
    };
    EXPECT_EQ(Count(mobs, 0), 6u);
    EXPECT_EQ(CountReduced(mobs, 0), 6u);  // linked patroller survives
}

// --- ShouldMusterForScriptedStage (the scripted-stage muster latch) -------

using DungeonClearMath::ShouldMusterForScriptedStage;

// No stage due: never hold, latch cleared. This is every tick of every dungeon
// without a plan, so it must be the cheap, quiet answer.
TEST(DungeonClearMusterGateTest, NoStagePendingProceeds)
{
    uint32 since = 12345;  // stale latch from a previous stage
    EXPECT_FALSE(ShouldMusterForScriptedStage(/*stagePending*/ false, /*toppedUp*/ false,
                                              since, /*now*/ 20000, /*waitMs*/ 40000,
                                              /*minMs*/ 8000, since));
    EXPECT_EQ(since, 0u);
}

// Stage due and the party is already topped up: arm nothing, pull now. The
// substance floor binds only once ARMED — a party that never fell below the
// floors pays nothing.
TEST(DungeonClearMusterGateTest, AToppedUpPartyProceedsImmediately)
{
    uint32 since = 0;
    EXPECT_FALSE(ShouldMusterForScriptedStage(true, /*toppedUp*/ true, since, 20000, 40000,
                                              8000, since));
    EXPECT_EQ(since, 0u);
}

// Stage due, party short: hold, and latch the moment the muster began.
TEST(DungeonClearMusterGateTest, AShortPartyHoldsAndLatches)
{
    uint32 since = 0;
    EXPECT_TRUE(ShouldMusterForScriptedStage(true, /*toppedUp*/ false, since, 20000, 40000,
                                             8000, since));
    EXPECT_EQ(since, 20000u);
    // Still short 10s later, still inside the budget: keep holding on the ORIGINAL
    // stamp (a re-arm every tick would make the wait unbounded).
    EXPECT_TRUE(ShouldMusterForScriptedStage(true, false, since, 30000, 40000, 8000, since));
    EXPECT_EQ(since, 20000u);
}

// THE BOUND, and the reason it exists: the muster floors sit above what stock bots
// eat and drink back up to, so a party that can never reach them must not be able to
// stall the run forever. Once the budget is spent the stage arms on the ordinary
// floors — a pull at 70% mana beats a run that never continues.
TEST(DungeonClearMusterGateTest, TheWaitIsBounded)
{
    uint32 since = 0;
    EXPECT_TRUE(ShouldMusterForScriptedStage(true, false, since, 1000, 40000, 8000, since));
    EXPECT_FALSE(ShouldMusterForScriptedStage(true, false, since, 41000, 40000, 8000, since));
    // ...and the latch STAYS armed past the timeout, so the same stage cannot
    // muster a second time the next tick.
    EXPECT_EQ(since, 1000u);
    EXPECT_FALSE(ShouldMusterForScriptedStage(true, false, since, 41100, 40000, 8000, since));
    EXPECT_EQ(since, 1000u);
}

// THE SUBSTANCE FLOOR. The release test is an instantaneous percentage over a
// 5-point band: one AoE heal closed it in 1-5s and stages armed against parties
// that never sat down (tp-20260806-212646-1, 115/184 musters <=5s the plan
// before). An ARMED muster therefore holds through minMs even if the floors go
// green first; only past the floor does topping up release and disarm — so the
// NEXT stage still gets its own full budget rather than inheriting a spent one.
TEST(DungeonClearMusterGateTest, AnArmedMusterHoldsTheSubstanceFloor)
{
    uint32 since = 0;
    EXPECT_TRUE(ShouldMusterForScriptedStage(true, false, since, 5000, 40000, 8000, since));
    EXPECT_EQ(since, 5000u);
    // Percentages closed 4s in (an AoE heal): still inside the floor — hold.
    EXPECT_TRUE(ShouldMusterForScriptedStage(true, /*toppedUp*/ true, since, 9000, 40000,
                                             8000, since));
    EXPECT_EQ(since, 5000u);
    // Past the floor and topped up: release and disarm.
    EXPECT_FALSE(ShouldMusterForScriptedStage(true, /*toppedUp*/ true, since, 14000, 40000,
                                              8000, since));
    EXPECT_EQ(since, 0u);
}

// waitMs == 0 disables the muster outright (proceed on the first tick, whatever
// minMs says), and the now == 0 corner must not underflow into a colossal elapsed.
TEST(DungeonClearMusterGateTest, DegenerateInputs)
{
    uint32 since = 0;
    EXPECT_FALSE(ShouldMusterForScriptedStage(true, false, since, 20000, /*waitMs*/ 0,
                                              /*minMs*/ 8000, since));

    since = 0;
    EXPECT_TRUE(ShouldMusterForScriptedStage(true, false, since, /*now*/ 0, 40000, 8000, since));
    EXPECT_EQ(since, 1u);  // never stores 0 — that means "clear"
}

// --- ShouldWaitForPatrol (the patrol-wait latch gate) ---------------------

using DungeonClearMath::ShouldWaitForPatrol;

// Not contended (full at/under ceiling): never wait, latch cleared.
TEST(DungeonClearPatrolGateTest, NotContendedProceeds)
{
    std::uint32_t since = 1234u;
    // full 5 <= ceiling 5 -> not contended.
    EXPECT_FALSE(ShouldWaitForPatrol(5u, 5u, 5u, since, 9000u, 8000u, since));
    EXPECT_EQ(since, 0u);
}

// Reduced ALSO over the ceiling: the patrol isn't the sole cause -> proceed
// (commit Advanced), don't wait.
TEST(DungeonClearPatrolGateTest, ReducedStillOverIsNotPatrolContended)
{
    std::uint32_t since = 0u;
    EXPECT_FALSE(ShouldWaitForPatrol(8u, 6u, 5u, since, 1000u, 8000u, since));
    EXPECT_EQ(since, 0u);
}

// Contended (full over, reduced under): arm the latch and hold until the budget.
TEST(DungeonClearPatrolGateTest, ContendedWaitsThenTimesOut)
{
    std::uint32_t since = 0u;
    // First contended tick: arm at now=1000, hold.
    EXPECT_TRUE(ShouldWaitForPatrol(6u, 5u, 5u, since, 1000u, 8000u, since));
    EXPECT_EQ(since, 1000u);
    // Still inside the 8s budget -> keep waiting.
    EXPECT_TRUE(ShouldWaitForPatrol(6u, 5u, 5u, since, 5000u, 8000u, since));
    EXPECT_EQ(since, 1000u);
    // At/after the budget -> proceed (latch stays armed so it can't re-wait).
    EXPECT_FALSE(ShouldWaitForPatrol(6u, 5u, 5u, since, 9000u, 8000u, since));
    EXPECT_EQ(since, 1000u);
}

// The patrol leaving (reduced==full, both fine) clears an armed latch and proceeds.
TEST(DungeonClearPatrolGateTest, PatrolLeavingClearsLatch)
{
    std::uint32_t since = 1000u;  // armed
    EXPECT_FALSE(ShouldWaitForPatrol(4u, 4u, 5u, since, 3000u, 8000u, since));
    EXPECT_EQ(since, 0u);
}

// Ceiling boundary: contended needs full strictly above and reduced at/under.
TEST(DungeonClearPatrolGateTest, CeilingBoundary)
{
    std::uint32_t since = 0u;
    // full 6 > 5 and reduced 5 <= 5 -> contended -> wait.
    EXPECT_TRUE(ShouldWaitForPatrol(6u, 5u, 5u, since, 100u, 8000u, since));
    // full == ceiling -> not contended.
    since = 0u;
    EXPECT_FALSE(ShouldWaitForPatrol(5u, 4u, 5u, since, 100u, 8000u, since));
}

// waitMs == 0 proceeds immediately even when contended.
TEST(DungeonClearPatrolGateTest, ZeroBudgetProceeds)
{
    std::uint32_t since = 0u;
    EXPECT_FALSE(ShouldWaitForPatrol(6u, 5u, 5u, since, 100u, 0u, since));
}

// --- ShouldHandoffFizzledPull (advanced-pull engage-fizzle latch) ----------
using DungeonClearMath::ShouldHandoffFizzledPull;

TEST(DungeonClearFizzleTest, NonFizzleClearsCount)
{
    // Pack died or is still being fought (pulledAliveIdle == false): clear latch.
    std::uint32_t count = 5u;
    EXPECT_FALSE(ShouldHandoffFizzledPull(/*aliveIdle*/ false, /*same*/ false, 2u, count));
    EXPECT_EQ(count, 0u);
}

TEST(DungeonClearFizzleTest, NewTargetRestartsAtOne)
{
    // A fizzle of a different pack than the latch holds restarts the run at 1.
    std::uint32_t count = 4u;
    EXPECT_FALSE(ShouldHandoffFizzledPull(/*aliveIdle*/ true, /*same*/ false, 2u, count));
    EXPECT_EQ(count, 1u);
}

TEST(DungeonClearFizzleTest, SameTargetRunReachesHandoff)
{
    // Two consecutive same-pack fizzles reach DC_PULL_FIZZLE_MAX (2) -> handoff.
    std::uint32_t count = 0u;
    EXPECT_FALSE(ShouldHandoffFizzledPull(true, false, 2u, count));  // fizzle #1 (new)
    EXPECT_EQ(count, 1u);
    EXPECT_TRUE(ShouldHandoffFizzledPull(true, true, 2u, count));    // fizzle #2 (same)
    EXPECT_EQ(count, 2u);
}

TEST(DungeonClearFizzleTest, InterruptingKillResetsRun)
{
    // A same-pack fizzle, then the pack dies/gets fought (reset), then it fizzles
    // again as a "new" run — must NOT hand off on the single post-reset fizzle.
    std::uint32_t count = 1u;                                        // one prior fizzle
    EXPECT_FALSE(ShouldHandoffFizzledPull(false, false, 2u, count)); // fought -> reset
    EXPECT_EQ(count, 0u);
    EXPECT_FALSE(ShouldHandoffFizzledPull(true, false, 2u, count));  // fresh fizzle
    EXPECT_EQ(count, 1u);
}

TEST(DungeonClearFizzleTest, ZeroMaxHandsOffOnFirstFizzle)
{
    std::uint32_t count = 0u;
    EXPECT_TRUE(ShouldHandoffFizzledPull(true, false, 0u, count));
}

// --- WalkTrailBack (Kernel A: the one shared breadcrumb walk-back primitive) --

using DungeonClearMath::TrailStep;
using DungeonClearMath::TrailJumpGuard;
using DungeonClearMath::WalkTrailBack;

TEST(DungeonClearWalkTrailBackTest, AccumulatesAlongNewestToOldest)
{
    // 4 crumbs at x = 0,4,8,12. Anchor sits on the newest (x=12). The walk must
    // visit them oldest-index-descending (12 -> 8 -> 4 -> 0) with `along` growing
    // 0,4,8,12 and `index` counting down 3,2,1,0.
    std::vector<Position> trail = MakeLine(4);
    Position const anchor(12.0f, 0.0f, 0.0f, 0.0f);
    std::vector<std::size_t> indices;
    std::vector<float> alongs;
    float const total = WalkTrailBack(trail, anchor, TrailJumpGuard,
        [&](TrailStep const& s) -> bool
        {
            indices.push_back(s.index);
            alongs.push_back(s.along);
            return true;
        });
    ASSERT_EQ(indices.size(), 4u);
    EXPECT_EQ(indices[0], 3u);
    EXPECT_EQ(indices[3], 0u);
    EXPECT_FLOAT_EQ(alongs[0], 0.0f);   // anchor sits on crumb 3
    EXPECT_FLOAT_EQ(alongs[1], 4.0f);
    EXPECT_FLOAT_EQ(alongs[3], 12.0f);
    EXPECT_FLOAT_EQ(total, 12.0f);
}

TEST(DungeonClearWalkTrailBackTest, VisitReturningFalseStopsEarly)
{
    // The accept predicate accepts the first crumb at least 6yd back and stops
    // (the camp/scout "first reachable past the setback" shape).
    std::vector<Position> trail = MakeLine(6);   // x = 0..20
    Position const anchor(20.0f, 0.0f, 0.0f, 0.0f);
    std::size_t accepted = DungeonClearMath::TrailRejoinNone;
    WalkTrailBack(trail, anchor, TrailJumpGuard,
        [&](TrailStep const& s) -> bool
        {
            if (s.along < 6.0f)
                return true;
            accepted = s.index;
            return false;
        });
    // Anchor at crumb 5 (x=20); crumb 3 (x=12) is the first at along >= 6 (=8yd).
    EXPECT_EQ(accepted, 3u);
}

// Named regression: dc-scout-lag-trail-dance / dc-multihop — INTERPOLATE the
// exact-lag point instead of snapping to the next crumb. Snapping to the first
// crumb past lag overshoots by up to one crumb spacing (~4yd), which parked
// followers outside PartyMaxSpread and deadlocked the between-pulls gate.
TEST(DungeonClearWalkTrailBackTest, InterpolatesExactLagPointOnCrossingSegment)
{
    std::vector<Position> trail = MakeLine(11);          // x = 0..40, spacing 4
    Position const tank(40.0f, 0.0f, 0.0f, 0.0f);        // anchor on newest crumb
    float const lag = 9.0f;
    Position interp;
    bool crossed = false;
    WalkTrailBack(trail, tank, TrailJumpGuard,
        [&](TrailStep const& s) -> bool
        {
            if (s.along < lag)
                return true;
            // Only the CROSSING segment interpolates; past it PointAt yields the crumb.
            interp = (s.alongPrev < lag) ? s.PointAt(lag) : s.crumb;
            crossed = true;
            return false;
        });
    ASSERT_TRUE(crossed);
    // Exactly 9yd behind the tank at x=40 => x=31 (crumb-snap would give x=28,
    // 12yd back — the 3yd overshoot the interpolation fix removes).
    EXPECT_FLOAT_EQ(interp.GetPositionX(), 31.0f);
    EXPECT_FLOAT_EQ(interp.GetPositionY(), 0.0f);
    EXPECT_FLOAT_EQ(tank.GetExactDist(&interp), 9.0f);
}

// Named regression: dc-pull-breadcrumb-seam-undermap — a 2D jump > guard stops
// the walk so no camp/trail point is chosen across a drag/teleport seam.
TEST(DungeonClearWalkTrailBackTest, HorizontalSeamStopsWalk)
{
    std::vector<Position> trail;
    trail.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);    // 0
    trail.emplace_back(4.0f, 0.0f, 0.0f, 0.0f);    // 1
    trail.emplace_back(8.0f, 0.0f, 0.0f, 0.0f);    // 2
    trail.emplace_back(25.0f, 0.0f, 0.0f, 0.0f);   // 3 (17yd gap from crumb 2)
    Position const anchor(25.0f, 0.0f, 0.0f, 0.0f);
    std::vector<std::size_t> visited;
    WalkTrailBack(trail, anchor, TrailJumpGuard,
        [&](TrailStep const& s) -> bool { visited.push_back(s.index); return true; });
    // Only the newest crumb is contiguous with the anchor; the 17yd gap is a seam.
    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], 3u);
}

// Named regression: dc-pull-breadcrumb-seam-undermap (vertical form) — a drop
// that is short in plan view but > guard in 3D must read as a seam, so a camp
// pick never lands on the wrong floor ("tank runs under the map").
TEST(DungeonClearWalkTrailBackTest, VerticalSeamStopsWalkEvenWhen2DContiguous)
{
    std::vector<Position> trail;
    trail.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);    // 0
    trail.emplace_back(4.0f, 0.0f, 0.0f, 0.0f);    // 1  (lower floor)
    trail.emplace_back(6.0f, 0.0f, 20.0f, 0.0f);   // 2  (2yd in 2D, +20yd Z: upper floor)
    Position const anchor(6.0f, 0.0f, 20.0f, 0.0f);
    std::vector<std::size_t> visited;
    WalkTrailBack(trail, anchor, TrailJumpGuard,
        [&](TrailStep const& s) -> bool { visited.push_back(s.index); return true; });
    // crumb 2 -> crumb 1 is ~20yd in 3D (> guard) though only 2yd in 2D: the walk
    // stops, so nothing on the lower floor is treated as "behind" the upper camp.
    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], 2u);
}

TEST(DungeonClearWalkTrailBackTest, EmptyTrailWalksNothing)
{
    std::vector<Position> empty;
    Position const anchor(0.0f, 0.0f, 0.0f, 0.0f);
    int count = 0;
    float const total = WalkTrailBack(empty, anchor, TrailJumpGuard,
        [&](TrailStep const&) -> bool { ++count; return true; });
    EXPECT_EQ(count, 0);
    EXPECT_FLOAT_EQ(total, 0.0f);
}

TEST(DungeonClearWalkTrailBackTest, PointAtClampsOutsideSegment)
{
    // A single crumb 4yd back: PointAt below the segment returns the near end,
    // above returns the far end (crumb); the exact midpoint interpolates.
    std::vector<Position> trail = MakeLine(2);        // crumbs at x=0, x=4
    Position const anchor(4.0f, 0.0f, 0.0f, 0.0f);
    Position lo, mid, hi;
    WalkTrailBack(trail, anchor, TrailJumpGuard,
        [&](TrailStep const& s) -> bool
        {
            if (s.index != 0u)
                return true;                          // segment (x=4)->(x=0), along 0..4
            lo = s.PointAt(-1.0f);
            mid = s.PointAt(2.0f);
            hi = s.PointAt(99.0f);
            return false;
        });
    EXPECT_FLOAT_EQ(lo.GetPositionX(), 4.0f);         // clamped to segStart (near end)
    EXPECT_FLOAT_EQ(mid.GetPositionX(), 2.0f);        // midpoint
    EXPECT_FLOAT_EQ(hi.GetPositionX(), 0.0f);         // clamped to crumb (far end)
}

// ===== Phantom-combat escape hatch (IsPhantomCombat / ShouldBreakStuckCombat) =====

// The classifier is phantom ONLY when in combat with none of the three "real fight"
// signals. Each signal independently rules phantom out.
TEST(DungeonClearStuckCombatTest, PhantomOnlyWhenNothingFightable)
{
    // In combat, nothing meleeing, no victim, no legitimate (reachable) holder.
    EXPECT_TRUE(DungeonClearMath::IsPhantomCombat(true, false, false, false));

    // Not in combat -> never phantom, whatever else holds.
    EXPECT_FALSE(DungeonClearMath::IsPhantomCombat(false, false, false, false));
}

TEST(DungeonClearStuckCombatTest, AnyRealFightSignalIsNotPhantom)
{
    // Something is meleeing us (getAttackers non-empty).
    EXPECT_FALSE(DungeonClearMath::IsPhantomCombat(true, true,  false, false));
    // We have a victim of our own.
    EXPECT_FALSE(DungeonClearMath::IsPhantomCombat(true, false, true,  false));
    // A legitimate (alive, non-evading, path-REACHABLE) holder — this is the flee/
    // kite case: the pursuer is reachable, so combat is never treated as phantom.
    EXPECT_FALSE(DungeonClearMath::IsPhantomCombat(true, false, false, true));
}

// A holder the party can never attack is not a fight, however close and willing it
// is. This is the guard that ends the Utgarde Pinnacle Skadi-gauntlet wedge, where a
// Flame Breath Trigger (28351) chased a hunter pet 80yd out of the hall and held the
// whole party PvE-flagged until the 600s no-progress watchdog killed the run
// (tr-20260902-121659-12, -13).
TEST(DungeonClearStuckCombatTest, TriggerCreatureIsNeverAResolvableHolder)
{
    // The wedge itself: a creature carrying CREATURE_FLAG_EXTRA_TRIGGER.
    EXPECT_TRUE(DungeonClearMath::IsUnresolvableCombatHolder(true, true));

    // An ordinary creature is resolvable — the party can kill it and the fight ends.
    EXPECT_FALSE(DungeonClearMath::IsUnresolvableCombatHolder(true, false));
}

TEST(DungeonClearStuckCombatTest, NonCreatureHoldersStayResolvable)
{
    // A PLAYER holder is never "unresolvable" — the trigger flag is a creature
    // template property and a non-creature can never carry it. Both arms pinned so
    // a future caller that forgets to pass isCreature can't silently start
    // force-clearing PvP combat.
    EXPECT_FALSE(DungeonClearMath::IsUnresolvableCombatHolder(false, false));
    EXPECT_FALSE(DungeonClearMath::IsUnresolvableCombatHolder(false, true));
}

// The ordering property the fix depends on: unresolvable OUTRANKS prosecuting.
// A trigger parked on top of the party is at distance 0.0, so IsHolderProsecutingFight
// says "toe to toe, this is a real fight" forever — which is exactly how it hid. The
// scan drops the holder before that question is ever asked.
TEST(DungeonClearStuckCombatTest, TriggerOnTopOfUsWouldOtherwiseReadAsProsecuting)
{
    constexpr float engageRange = 22.0f;

    // What the old code saw: dist 0.0, not closing -> prosecuting, so never phantom.
    EXPECT_TRUE(DungeonClearMath::IsHolderProsecutingFight(true, 0.0f, engageRange, false));

    // What the new guard says about that same holder, before distance is consulted.
    EXPECT_TRUE(DungeonClearMath::IsUnresolvableCombatHolder(true, true));

    // Composed the way ScanCombatHolders composes them: dropping the holder leaves
    // no legitimate holder at all, which is what finally arms the phantom hatch.
    bool const haveHolder = !DungeonClearMath::IsUnresolvableCombatHolder(true, true);
    EXPECT_FALSE(DungeonClearMath::IsHolderProsecutingFight(haveHolder, 0.0f, engageRange, false));
    EXPECT_TRUE(DungeonClearMath::IsPhantomCombat(true, false, false, haveHolder));
}

// Reachability says a holder COULD come; IsHolderProsecutingFight says whether it IS.
TEST(DungeonClearStuckCombatTest, HolderInEngageRangeAlwaysProsecutes)
{
    constexpr float engageRange = 22.0f;

    // Inside engage range is a fight whatever the closing tracker says — a mob toe to
    // toe with us that simply cannot get closer must never read as stale.
    EXPECT_TRUE(DungeonClearMath::IsHolderProsecutingFight(true, 5.0f, engageRange, false));
    EXPECT_TRUE(DungeonClearMath::IsHolderProsecutingFight(true, engageRange, engageRange, false));

    // The opaque script-forced case reports distance 0 with a legitimate verdict, so it
    // lands here too and is never cleared.
    EXPECT_TRUE(DungeonClearMath::IsHolderProsecutingFight(true, 0.0f, engageRange, false));
}

TEST(DungeonClearStuckCombatTest, FarHolderProsecutesOnlyWhileClosing)
{
    constexpr float engageRange = 22.0f;

    // A chaser / a mob we are kiting keeps improving its closest-ever distance.
    EXPECT_TRUE(DungeonClearMath::IsHolderProsecutingFight(true, 70.0f, engageRange, true));

    // Far and no longer closing: the instanced no-leash straggler that tagged us and
    // stopped. This is the arm that lets the hatch fire (tr-20260804-153254-2).
    EXPECT_FALSE(DungeonClearMath::IsHolderProsecutingFight(true, 70.0f, engageRange, false));

    // No legitimate holder at all -> nothing prosecuting, whatever the other inputs.
    EXPECT_FALSE(DungeonClearMath::IsHolderProsecutingFight(false, 1.0f, engageRange, true));
}

// The closing signal is produced by DcProgressWatchdog::TickClosing, so wire the two
// together the way the trigger does and prove the stale-holder shape converges: a
// holder that stops reads as prosecuting exactly once (the arming sample) and never
// again, while a party that walks AWAY from it cannot re-arm it.
TEST(DungeonClearStuckCombatTest, StoppedHolderStopsProsecutingAndStaysStopped)
{
    constexpr float engageRange = 22.0f;
    DcProgressWatchdog watch;
    std::uint32_t now = 1000;

    auto prosecuting = [&](float dist)
    {
        now += 200;
        bool const closing = watch.TickClosing(dist, 0.5f, now);
        return DungeonClearMath::IsHolderProsecutingFight(true, dist, engageRange, closing);
    };

    // First sample arms the tracker and counts as progress.
    EXPECT_TRUE(prosecuting(70.0f));
    // Holder is stationary: no improvement, so it stops counting as a fight.
    EXPECT_FALSE(prosecuting(70.0f));
    EXPECT_FALSE(prosecuting(70.0f));
    // The party shuttles away and back — distance gets WORSE then returns to the same
    // value, which is not an improvement on the closest-ever. Still stale.
    EXPECT_FALSE(prosecuting(99.0f));
    EXPECT_FALSE(prosecuting(70.0f));
    // It finally starts chasing -> prosecuting again, and the hatch goes back to sleep.
    EXPECT_TRUE(prosecuting(60.0f));
    EXPECT_TRUE(prosecuting(40.0f));
}

// The engage-range clause judges the nearest holder the bot can SEE. A holder out of
// LOS — tr-20261018-101829-1's Angerforge Reservists, 14-16yd away on the floor ~11yd
// below the tank — is in range by the numbers, but no playerbots action can target it
// (AttackersValue requires LOS), so treating it as toe-to-toe kept the hatch shut for
// the whole 900s no-progress window. The trigger passes +inf when no holder is seen.
TEST(DungeonClearStuckCombatTest, UnseenHolderInRangeProsecutesOnlyWhileClosing)
{
    constexpr float engageRange = 22.0f;
    constexpr float unseen = std::numeric_limits<float>::infinity();

    // What the old code saw: a legitimate holder at 14.4yd -> "a fight", forever.
    EXPECT_TRUE(DungeonClearMath::IsHolderProsecutingFight(true, 14.4f, engageRange, false));

    // Same holder, judged by SEEN distance: stationary below us -> not a fight, so the
    // hatch's streak clock runs and the force-clear fires.
    EXPECT_FALSE(DungeonClearMath::IsHolderProsecutingFight(true, unseen, engageRange, false));
    EXPECT_TRUE(DungeonClearMath::IsPhantomCombat(
        true, /*hasAttacker (seen)*/ false, false,
        DungeonClearMath::IsHolderProsecutingFight(true, unseen, engageRange, false)));

    // A mob chasing us around a corner is unseen too, but it is CLOSING — still a fight.
    EXPECT_TRUE(DungeonClearMath::IsHolderProsecutingFight(true, unseen, engageRange, true));
}

// The streak gate: a transient phantom tick must not fire; only a phantom state held
// continuously for the timeout does, and any break resets the clock.
TEST(DungeonClearStuckCombatTest, StreakGateArmsHoldsAndFires)
{
    std::uint32_t since = 0;
    constexpr std::uint32_t timeout = 15000;

    // Not phantom -> stays disarmed.
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(false, 1000, timeout, since));
    EXPECT_EQ(since, 0u);

    // First phantom tick arms the clock to `now` but does not fire.
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(true, 1000, timeout, since));
    EXPECT_EQ(since, 1000u);

    // Still phantom, just short of the timeout -> hold, clock unchanged.
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(true, 1000 + timeout - 1, timeout, since));
    EXPECT_EQ(since, 1000u);

    // Phantom held for the full timeout -> fire.
    EXPECT_TRUE(DungeonClearMath::ShouldBreakStuckCombat(true, 1000 + timeout, timeout, since));
}

TEST(DungeonClearStuckCombatTest, AnyBreakResetsTheStreak)
{
    std::uint32_t since = 0;
    constexpr std::uint32_t timeout = 15000;

    // Arm, then run most of the way toward firing.
    DungeonClearMath::ShouldBreakStuckCombat(true, 1000, timeout, since);
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(true, 1000 + timeout - 100, timeout, since));

    // A single non-phantom tick (a reachable target reappeared) resets the clock...
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(false, 1000 + timeout - 50, timeout, since));
    EXPECT_EQ(since, 0u);

    // ...so the next phantom streak must run the FULL timeout again from scratch.
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(true, 2000 + timeout, timeout, since));
    EXPECT_EQ(since, 2000u + timeout);
    EXPECT_TRUE(DungeonClearMath::ShouldBreakStuckCombat(true, 2000 + 2 * timeout, timeout, since));
}

TEST(DungeonClearStuckCombatTest, ZeroTimeoutDisablesTheRecovery)
{
    std::uint32_t since = 0;
    // timeout 0 = feature off: never fires, and keeps the clock disarmed even while
    // the phantom state holds.
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(true, 5000, 0, since));
    EXPECT_EQ(since, 0u);
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(true, 99999, 0, since));
    EXPECT_EQ(since, 0u);
}

TEST(DungeonClearStuckCombatTest, ArmingAtTimeZeroAvoidsTheSentinel)
{
    std::uint32_t since = 0;
    // getMSTime() is ~0 only in the first server ms; arming must not leave `since`
    // at the 0 "disarmed" sentinel or the clock would re-arm every tick and never
    // accumulate. It is nudged to 1 instead.
    EXPECT_FALSE(DungeonClearMath::ShouldBreakStuckCombat(true, 0, 15000, since));
    EXPECT_EQ(since, 1u);
}

// ===== Flagged-in-combat driving gate (MayDriveWhileFlagged) =====
//
// The DC driving ladder used to stand down on the raw core combat FLAG. A hostile
// area aura sets that flag with no fight behind it (Arcatraz Entropic Aura, 45yd vs
// a ~20yd aggro radius), and because playerbots never enters the combat engine on
// the flag alone, BOTH ladders went inert and the run froze permanently. S1356.

TEST(DcFlaggedCombatGateTest, NotFlaggedAlwaysDrives)
{
    std::uint32_t since = 12345;
    EXPECT_TRUE(DungeonClearMath::MayDriveWhileFlagged(false, false, 1000, 5000, since));
    EXPECT_EQ(since, 0u);   // streak cleared so a later flag starts clean
}

TEST(DcFlaggedCombatGateTest, RealFightAlwaysStandsDown)
{
    std::uint32_t since = 0;
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, true, 1000, 5000, since));
    EXPECT_EQ(since, 0u);
    // ...and no amount of elapsed time changes that while the fight is live.
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, true, 999000, 5000, since));
}

TEST(DcFlaggedCombatGateTest, FlaggedWithNoFightResumesOnlyAfterTheGrace)
{
    std::uint32_t since = 0;
    constexpr std::uint32_t grace = 5000;

    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000, grace, since));
    EXPECT_EQ(since, 1000u);
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000 + grace - 1, grace, since));
    EXPECT_TRUE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000 + grace, grace, since));
}

TEST(DcFlaggedCombatGateTest, ARetargetHoleInARealFightNeverResumesDriving)
{
    // THE REGRESSION THAT MATTERS. A target dies, nothing has re-acquired for a
    // tick or two, then the fight continues. Without the streak reset those holes
    // would accumulate and the tank would walk out of a live fight.
    std::uint32_t since = 0;
    constexpr std::uint32_t grace = 5000;

    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000, grace, since));
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 3000, grace, since));
    // something re-acquires -> streak broken
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, true, 3200, grace, since));
    EXPECT_EQ(since, 0u);
    // wedged again: the clock restarts, the old 2000ms is NOT credited
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 3400, grace, since));
    EXPECT_EQ(since, 3400u);
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 3400 + grace - 1, grace, since));
    EXPECT_TRUE(DungeonClearMath::MayDriveWhileFlagged(true, false, 3400 + grace, grace, since));
}

// ===== Conditional-event rung gate (EventDueGateOpen) =====
//
// The same gate, plus the drivesInCombat exemption that used to live only in the
// combat-ENGINE copy of the rung. Being combat-FLAGGED and running the combat
// ENGINE are different facts: playerbots enters BOT_STATE_COMBAT only from
// AttackAction::Attack, so a flagged bot with an empty attacker set sits in the
// non-combat engine, where the exemption was unreachable and both rungs were off
// at once. HoR tr-20260908-215109-1 lost thirty seconds of wall 4 to it;
// tr-20260908-215112-9 lost fifty-five and wiped.

TEST(DcEventDueGateTest, DrivesInCombatIgnoresALiveFight)
{
    // THE REGRESSION. Flagged AND really engaged — MayDriveWhileFlagged's hard
    // stand-down — must still drive an event that owns the fight.
    std::uint32_t since = 0;
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, true, 1000, 5000, since));
    EXPECT_TRUE(DungeonClearMath::EventDueGateOpen(true, true, true, 1000, 5000, since));
}

TEST(DcEventDueGateTest, DrivesInCombatLeavesTheSharedLatchAlone)
{
    // The latch is shared with every other MayDrive-gated rung, so the exempt
    // path must not restart a grace window one of them is already counting.
    std::uint32_t since = 4242;
    EXPECT_TRUE(DungeonClearMath::EventDueGateOpen(true, true, true, 9000, 5000, since));
    EXPECT_EQ(since, 4242u);
    EXPECT_TRUE(DungeonClearMath::EventDueGateOpen(true, false, false, 9000, 5000, since));
    EXPECT_EQ(since, 4242u);
}

TEST(DcEventDueGateTest, AnOrdinaryEventKeepsTheOldGateExactly)
{
    // drivesInCombat false -> byte-for-byte MayDriveWhileFlagged, latch included.
    constexpr std::uint32_t grace = 5000;
    std::uint32_t plain = 0;
    std::uint32_t gated = 0;

    for (std::uint32_t now : {1000u, 3000u, 3200u, 3400u, 3400u + grace})
    {
        bool const engaged = now == 3200u;
        EXPECT_EQ(DungeonClearMath::MayDriveWhileFlagged(true, engaged, now, grace, plain),
                  DungeonClearMath::EventDueGateOpen(false, true, engaged, now, grace, gated));
        EXPECT_EQ(plain, gated);
    }
}

TEST(DcEventDueGateTest, OutOfCombatDrivesEitherWay)
{
    std::uint32_t since = 777;
    EXPECT_TRUE(DungeonClearMath::EventDueGateOpen(false, false, false, 1000, 5000, since));
    EXPECT_EQ(since, 0u);   // the ordinary path still clears the streak
}

TEST(DcFlaggedCombatGateTest, ZeroGraceResumesImmediately)
{
    std::uint32_t since = 0;
    EXPECT_TRUE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000, 0, since));
}

TEST(DcFlaggedCombatGateTest, ArmingAtTimeZeroAvoidsTheSentinel)
{
    std::uint32_t since = 0;
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 0, 5000, since));
    EXPECT_EQ(since, 1u);
}

// The rest gates ask the same kernel a DIFFERENT question — "is the PARTY
// flagged with nothing fighting?" (DcCombatFlag::IsPhantomFlag) — so they feed it
// a different `flagged` input and must therefore keep their own streak. These two
// pin the reason the latches are separate (DcApproachState::flaggedNoEngageSinceMs
// vs partyFlaggedNoEngageSinceMs).

TEST(DcFlaggedCombatGateTest, TwoQuestionsKeepTwoStreaks)
{
    // The tank drops combat while a follower is still flagged: the driving latch
    // clears (its input went false) while the party latch keeps streaking.
    std::uint32_t driving = 0;
    std::uint32_t party = 0;
    constexpr std::uint32_t grace = 5000;

    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000, grace, driving));
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000, grace, party));

    // Tank unflags at t=2000; the party (a follower still in the aura) does not.
    EXPECT_TRUE(DungeonClearMath::MayDriveWhileFlagged(false, false, 2000, grace, driving));
    EXPECT_EQ(driving, 0u);   // driving streak reset...
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 2000, grace, party));
    EXPECT_EQ(party, 1000u);  // ...party streak untouched, still counting from 1000

    // The party waiver lands on ITS OWN schedule, not the tank's.
    EXPECT_TRUE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000 + grace, grace, party));
}

TEST(DcFlaggedCombatGateTest, TheRestWaiverDoesNotFireInThePostFightWindow)
{
    // THE REGRESSION THAT MATTERS for the rest gate: every fight ends with a few
    // seconds of "still flagged, nothing engaged yet". Waiving the HP/mana floors
    // there would send the tank off to the next pull instead of drinking. The
    // grace is what makes the waiver mean "this flag is never going to clear".
    std::uint32_t party = 0;
    constexpr std::uint32_t grace = 5000;

    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000, grace, party));
    EXPECT_FALSE(DungeonClearMath::MayDriveWhileFlagged(true, false, 1000 + grace - 1, grace, party));
    // Combat drops for real -> streak cleared, ordinary floors apply again.
    EXPECT_TRUE(DungeonClearMath::MayDriveWhileFlagged(false, false, 1000 + grace, grace, party));
    EXPECT_EQ(party, 0u);
}

// ===== Bystander-detour borrow watchdog (ShouldKeepAvoidDetour) =====
//
// The pull borrows the approach tick from Advance to walk around another pack's
// aggro sphere. Advance's wedge ladder is parked while it holds the tick, so the
// borrow is bounded by a NO-PROGRESS clock: closing on the pack keeps it alive
// indefinitely; orbiting without closing hands the walk back.

TEST(DungeonClearAvoidDetourTest, ProgressKeepsTheBorrowAliveIndefinitely)
{
    std::uint32_t since = 0;
    float best = 0.0f;
    constexpr std::uint32_t timeout = 8000;

    // First tick arms the clock and records the distance.
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(1000, 60.0f, timeout, 1.0f, since, best));
    EXPECT_EQ(since, 1000u);
    EXPECT_FLOAT_EQ(best, 60.0f);

    // A long arc that keeps closing restamps every time, so the budget never runs
    // out even though far more than `timeout` has elapsed since the detour began.
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(20000, 50.0f, timeout, 1.0f, since, best));
    EXPECT_EQ(since, 20000u);
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(60000, 40.0f, timeout, 1.0f, since, best));
    EXPECT_EQ(since, 60000u);
    EXPECT_FLOAT_EQ(best, 40.0f);
}

TEST(DungeonClearAvoidDetourTest, SidewaysOrbitBurnsTheBudgetAndGivesUp)
{
    std::uint32_t since = 0;
    float best = 0.0f;
    constexpr std::uint32_t timeout = 8000;

    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(1000, 40.0f, timeout, 1.0f, since, best));

    // Orbiting: the tank is moving, but never getting closer to the pack than the
    // 40yd it started at (it even drifts out to 42). Hold until the budget expires…
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(4000, 41.0f, timeout, 1.0f, since, best));
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(1000 + timeout - 1, 42.0f, timeout, 1.0f, since, best));
    // …then hand the walk back to Advance.
    EXPECT_FALSE(DungeonClearMath::ShouldKeepAvoidDetour(1000 + timeout, 42.0f, timeout, 1.0f, since, best));
    // The latch is KEPT after a give-up, so the predicate keeps saying no while
    // the orbit keeps not closing.
    EXPECT_FALSE(DungeonClearMath::ShouldKeepAvoidDetour(1000 + timeout + 500, 41.5f, timeout, 1.0f, since, best));
}

TEST(DungeonClearAvoidDetourTest, ClosingDistanceAloneWouldReArmTheClock)
{
    std::uint32_t since = 0;
    float best = 0.0f;
    constexpr std::uint32_t timeout = 8000;

    ASSERT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(1000, 40.0f, timeout, 1.0f, since, best));
    ASSERT_FALSE(DungeonClearMath::ShouldKeepAvoidDetour(1000 + timeout, 40.0f, timeout, 1.0f, since, best));

    // Distance closed while the borrow was off beats the recorded best, so the
    // PREDICATE re-arms on a fresh clock. This is exactly why the pull caller does
    // NOT lean on it after a give-up: one yard of Advance's own walking satisfies
    // this, and re-borrowing that fast would cancel Advance's spline every few
    // hundred ms. DcPullContext::avoidGaveUp is the one-shot latch on top.
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(30000, 30.0f, timeout, 1.0f, since, best));
    EXPECT_EQ(since, 30000u);
    EXPECT_FLOAT_EQ(best, 30.0f);
}

TEST(DungeonClearAvoidDetourTest, SubEpsilonJitterIsNotProgress)
{
    std::uint32_t since = 0;
    float best = 0.0f;
    constexpr std::uint32_t timeout = 8000;

    ASSERT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(1000, 40.0f, timeout, 1.0f, since, best));

    // A glide's tick-to-tick jitter must not count as closing, or an orbit that
    // creeps inward by centimetres would hold the borrow open forever.
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(2000, 39.5f, timeout, 1.0f, since, best));
    EXPECT_EQ(since, 1000u);              // clock NOT restamped
    EXPECT_FLOAT_EQ(best, 40.0f);         // best NOT lowered
    EXPECT_FALSE(DungeonClearMath::ShouldKeepAvoidDetour(1000 + timeout, 39.5f, timeout, 1.0f, since, best));
}

TEST(DungeonClearAvoidDetourTest, ZeroTimeoutNeverGivesUp)
{
    std::uint32_t since = 0;
    float best = 0.0f;

    ASSERT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(1000, 40.0f, 0, 1.0f, since, best));
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(999000, 40.0f, 0, 1.0f, since, best));
}

TEST(DungeonClearAvoidDetourTest, ArmingAtTimeZeroAvoidsTheSentinel)
{
    std::uint32_t since = 0;
    float best = 0.0f;
    // Same sentinel hazard as the stuck-combat clock: `since == 0` means "not
    // borrowing", so arming on server-ms 0 must nudge to 1 or the clock re-arms
    // every tick and the budget never accumulates.
    EXPECT_TRUE(DungeonClearMath::ShouldKeepAvoidDetour(0, 40.0f, 8000, 1.0f, since, best));
    EXPECT_EQ(since, 1u);
}

// --- Camp-assist attack range ------------------------------------------------
// Regression cover for the DC<->stock handoff dead band: DC's engage test and the
// stock reach action's keep-closing test must partition the distance line with no
// gap, or a ranged follower parks between them and never engages.

TEST(DungeonClearAssistRangeTest, RangedEngagesWhereStockReachParksIt)
{
    // The live repro (Steamvault, 2026-07-20): SpellDistance 28.5, player+humanoid
    // combat reach ~3.2. Stock reach spell goes inert at 28.5 + 3.2 = 31.7, so it
    // parked ranged DPS at 29.4-29.7yd and stopped. The old test here demanded
    // 28.5 - CONTACT_DISTANCE = 28.0, so every one of those ticks read "out of
    // range -> yield to stock" and nobody ever acted.
    float const spell = 28.5f;
    float const reachSum = 3.2f;
    for (float dist : {29.4f, 29.5f, 29.6f, 29.7f})
        EXPECT_TRUE(DungeonClearMath::IsWithinAssistAttackRange(
            /*isMelee*/ false, dist, /*meleeRange*/ 4.0f, spell, reachSum))
            << "ranged must engage at " << dist << "yd — stock reach will not close further";
}

TEST(DungeonClearAssistRangeTest, RangedWindowIsExactComplementOfStockKeepClosing)
{
    // The invariant that keeps the dead band closed: stock keeps closing exactly
    // while dist > spell + reachSum, so DC must engage from exactly dist <= that.
    // Any daylight between these two bounds is a hang.
    float const spell = 28.5f;
    float const reachSum = 3.2f;
    float const stockStopsAt = spell + reachSum;

    EXPECT_TRUE(DungeonClearMath::IsWithinAssistAttackRange(false, stockStopsAt, 4.0f, spell, reachSum));
    EXPECT_TRUE(DungeonClearMath::IsWithinAssistAttackRange(false, stockStopsAt - 0.01f, 4.0f, spell, reachSum));
    // Beyond it stock is still walking the bot in, so yielding is correct.
    EXPECT_FALSE(DungeonClearMath::IsWithinAssistAttackRange(false, stockStopsAt + 0.01f, 4.0f, spell, reachSum));
}

TEST(DungeonClearAssistRangeTest, RangedWindowTracksCombatReach)
{
    // A big-model target has a larger combat reach, which pushes stock's stop point
    // further out; the engage window must follow it rather than stay pinned to a
    // bare spell distance.
    EXPECT_TRUE(DungeonClearMath::IsWithinAssistAttackRange(false, 34.0f, 4.0f, 28.5f, /*reachSum*/ 6.0f));
    EXPECT_FALSE(DungeonClearMath::IsWithinAssistAttackRange(false, 34.0f, 4.0f, 28.5f, /*reachSum*/ 3.2f));
}

TEST(DungeonClearAssistRangeTest, MeleeKeepsItsOwnReachInclusiveThreshold)
{
    // Melee already overlaps stock reach-melee (reachSum + 1.0 vs reachSum + 0.75),
    // so it passes its own precomputed threshold and must ignore the spell terms —
    // double-counting reach here would let melee "engage" from yards away.
    EXPECT_TRUE(DungeonClearMath::IsWithinAssistAttackRange(true, 4.0f, /*meleeRange*/ 4.2f, 28.5f, 3.2f));
    EXPECT_FALSE(DungeonClearMath::IsWithinAssistAttackRange(true, 5.0f, /*meleeRange*/ 4.2f, 28.5f, 3.2f));
    // Well inside spell range but outside melee reach: still not engageable.
    EXPECT_FALSE(DungeonClearMath::IsWithinAssistAttackRange(true, 20.0f, /*meleeRange*/ 4.2f, 28.5f, 3.2f));
}

TEST(DungeonClearAssistRangeTest, TriggerStandDownUsesTheSameWindowAsTheAction)
{
    // Issue #18. S1116 fixed the ACTION but left ShouldAssistCampFight (the trigger
    // that gates it) on a bare `dist <= GetRange("spell")`, so the dead band moved
    // into the trigger: the action engaged from <= spell + reachSum while the
    // stand-down needed <= spell. A ranged follower in between made the trigger fire
    // forever on a mob the action had already decided was in range.
    //
    // Live trace: SpellDistance 28.5, casters pinned at 29.0-31.0yd with the two
    // biggest clusters at 30.6 and 30.8. reachSum is read off the trace rather than
    // guessed — the action engaged out to 31.0yd, so spell + reachSum >= 31.0.
    float const spell = 28.5f;
    float const reachSum = 2.5f;

    for (float dist : {29.0f, 30.1f, 30.6f, 30.8f})
    {
        EXPECT_TRUE(DungeonClearMath::IsWithinAssistAttackRange(
            /*isMelee*/ false, dist, /*meleeRange*/ 4.0f, spell, reachSum))
            << "trigger must stand down at " << dist
            << "yd — the action already engages there and stock will not close";
        EXPECT_GT(dist, spell) << "the old bare-spell test would have kept firing here";
    }
}

TEST(DungeonClearAssistRangeTest, MeleeTriggerArmStaysWiderThanTheActionArm)
{
    // The trigger keeps `reach + 5.0` for melee on purpose. It must remain LOOSER
    // than the action's `reachSum + 1.0` so the two overlap; if this ever inverts,
    // melee gains the gap that ranged just lost.
    float const botReach = 1.5f;
    float const targetReach = 2.1f;
    float const triggerMeleeRange = botReach + 5.0f;
    float const actionMeleeRange = botReach + targetReach + 1.0f;
    EXPECT_GT(triggerMeleeRange, actionMeleeRange);

    // A melee bot between the two thresholds stands down and lets stock reach-melee
    // (stop point reachSum + 0.75) walk it the rest of the way.
    float const between = 0.5f * (actionMeleeRange + triggerMeleeRange);
    EXPECT_TRUE(DungeonClearMath::IsWithinAssistAttackRange(
        /*isMelee*/ true, between, triggerMeleeRange, 28.5f, botReach + targetReach));
}

// --- DecideChase (the chase leash) ---------------------------------------
// A pull/engage target is latched by GUID and re-aimed at its live position every
// tick, so a mob that WALKS turns the approach into a pursuit across the room —
// waking every pack the mob's route passes behind. The leash pins the approach to
// the ground the plan was made against and waits a receding target out instead.

using DungeonClearMath::ChaseVerdict;
using DungeonClearMath::DecideChase;

namespace
{
    // Named wrapper so the call sites below read as the scenario they are.
    ChaseVerdict Chase(float drift, float gapAtAnchor, float gapNow, bool hot,
                       float leash, std::uint32_t now, std::uint32_t holdMs,
                       std::uint32_t& hold)
    {
        return DecideChase(drift, gapAtAnchor, gapNow, hot, leash, now, holdMs, hold);
    }
}

TEST(DungeonClearChaseLeashTest, AStationaryPackIsAlwaysFollowed)
{
    std::uint32_t hold = 0;
    EXPECT_EQ(Chase(0.0f, 30.0f, 30.0f, false, 15.0f, 1000u, 6000u, hold),
              ChaseVerdict::Follow);
    EXPECT_EQ(hold, 0u);
}

TEST(DungeonClearChaseLeashTest, WobbleInsideTheLeashIsNotAChase)
{
    // Ordinary wander/patrol wobble around a spawn must never hold the tank —
    // a RANDOM_MOTION radius is typically 5-10yd and the leash sits above it.
    std::uint32_t hold = 0;
    EXPECT_EQ(Chase(14.9f, 30.0f, 44.0f, false, 15.0f, 1000u, 6000u, hold),
              ChaseVerdict::Follow);
    EXPECT_EQ(hold, 0u);
}

TEST(DungeonClearChaseLeashTest, ARecedingTargetIsHeldThenGivenUpOn)
{
    std::uint32_t hold = 0;
    // Past the leash AND further from our commit spot than when we picked it.
    EXPECT_EQ(Chase(20.0f, 30.0f, 44.0f, false, 15.0f, 1000u, 6000u, hold),
              ChaseVerdict::Hold);
    EXPECT_EQ(hold, 1000u);           // latch armed at first receding tick
    // Still holding partway through the wait, latch untouched.
    EXPECT_EQ(Chase(26.0f, 30.0f, 50.0f, false, 15.0f, 5000u, 6000u, hold),
              ChaseVerdict::Hold);
    EXPECT_EQ(hold, 1000u);
    // Wait elapsed: give up rather than stall the run on a patrol that left.
    EXPECT_EQ(Chase(30.0f, 30.0f, 55.0f, false, 15.0f, 7000u, 6000u, hold),
              ChaseVerdict::GiveUp);
}

TEST(DungeonClearChaseLeashTest, AnInboundPatrolIsFollowedDespiteTheDrift)
{
    // The whole point of holding is that a patrol loops back. A mob that has come
    // at least as close to our commit spot as it was when picked is that return
    // leg — following it is what lets the tank tag it without ever advancing.
    std::uint32_t hold = 1000u;
    EXPECT_EQ(Chase(40.0f, 30.0f, 22.0f, false, 15.0f, 3000u, 6000u, hold),
              ChaseVerdict::Follow);
    EXPECT_EQ(hold, 0u) << "the hold must disarm the moment it comes back";
}

TEST(DungeonClearChaseLeashTest, AHotDestinationIsHeldEvenWhenItIsComingToUs)
{
    // Standing inside another pack's aggro sphere is not walkable ground however
    // close it is: reaching it wakes that pack no matter how the route bends. This
    // is the half en-route avoidance structurally cannot cover — it steers around
    // spheres IN THE WAY, and a sphere containing the destination has no way past.
    std::uint32_t hold = 0;
    EXPECT_EQ(Chase(2.0f, 30.0f, 10.0f, /*hot*/ true, 15.0f, 1000u, 6000u, hold),
              ChaseVerdict::Hold);
    EXPECT_EQ(hold, 1000u);
    // It steps clear of the neighbour: resume at once, latch disarmed.
    EXPECT_EQ(Chase(2.0f, 30.0f, 10.0f, /*hot*/ false, 15.0f, 1200u, 6000u, hold),
              ChaseVerdict::Follow);
    EXPECT_EQ(hold, 0u);
}

TEST(DungeonClearChaseLeashTest, ZeroLeashIsTheHistoricalAlwaysChase)
{
    std::uint32_t hold = 4242u;
    EXPECT_EQ(Chase(500.0f, 5.0f, 500.0f, true, 0.0f, 1000u, 6000u, hold),
              ChaseVerdict::Follow);
    EXPECT_EQ(hold, 0u) << "a disabled gate must not leave a latch behind";
}

TEST(DungeonClearChaseLeashTest, ZeroHoldGivesUpImmediatelyWithoutArming)
{
    std::uint32_t hold = 0;
    EXPECT_EQ(Chase(20.0f, 30.0f, 44.0f, false, 15.0f, 1000u, 0u, hold),
              ChaseVerdict::GiveUp);
    EXPECT_EQ(hold, 0u);
}

TEST(DungeonClearChaseLeashTest, ArmingOnMillisecondZeroDoesNotReadAsUnarmed)
{
    // Same corner every latch in this file guards: 0 is the "unarmed" sentinel, so
    // arming at getMSTime() == 0 must nudge to 1 or the hold restarts every tick.
    std::uint32_t hold = 0;
    EXPECT_EQ(Chase(20.0f, 30.0f, 44.0f, false, 15.0f, 0u, 6000u, hold),
              ChaseVerdict::Hold);
    EXPECT_EQ(hold, 1u);
}

TEST(DungeonClearChaseLeashTest, ABackwardClockStepCannotFakeAnExpiry)
{
    // getMSTime() wrap / a backward step must read as "no time has elapsed", not as
    // a huge unsigned elapsed that gives up on a target we only just started
    // waiting for.
    std::uint32_t hold = 5000u;
    EXPECT_EQ(Chase(20.0f, 30.0f, 44.0f, false, 15.0f, /*now*/ 100u, 6000u, hold),
              ChaseVerdict::Hold);
    EXPECT_EQ(hold, 5000u);
}

// ---------------------------------------------------------------------------
// PathProgressCursor — where along a route the bot has walked up to.
// ---------------------------------------------------------------------------

namespace
{
    // Shadowfang Keep's tower, taken from the navmesh: the staircase from the
    // Fenrus room (Z 129) up to Wolf Master Nandos (Z 156) climbs almost
    // directly overhead, so a route vertex on the landing 10yd UP sits CLOSER in
    // 2D than the vertex on the floor the tank is actually standing on.
    std::vector<G3D::Vector3> SfkStaircaseRoute()
    {
        return {
            G3D::Vector3(-130.67f, 2169.07f, 129.16f),  // the tank's own floor
            G3D::Vector3(-129.33f, 2168.00f, 138.76f),  // landing, one storey up
            G3D::Vector3(-128.00f, 2174.93f, 155.83f),  // top of the stairs
            G3D::Vector3(-120.70f, 2162.00f, 155.80f),  // Nandos, at the gate
        };
    }
}

TEST(DungeonClearPathCursorTest, PicksTheVertexOnTheBotsOwnFloor)
{
    // The tank is at the foot of the stairs. Vertex 1 is 1.81yd away in 2D and
    // vertex 0 is 1.98yd away, so a 2D pick lands a storey up; in 3D vertex 1 is
    // 9.9yd away and vertex 0 wins.
    EXPECT_EQ(DungeonClearMath::PathProgressCursor(SfkStaircaseRoute(),
                                                   -130.9f, 2167.1f, 129.0f),
              0u);
}

TEST(DungeonClearPathCursorTest, FollowsTheBotUpTheStairs)
{
    // Standing on the landing, the cursor moves with it — the floor vertex below
    // is now the far one.
    EXPECT_EQ(DungeonClearMath::PathProgressCursor(SfkStaircaseRoute(),
                                                   -129.5f, 2168.2f, 138.8f),
              1u);
    // And at the top, the tank's progress is the last leg, not the stairwell it
    // is standing directly above.
    EXPECT_EQ(DungeonClearMath::PathProgressCursor(SfkStaircaseRoute(),
                                                   -121.0f, 2162.5f, 155.8f),
              3u);
}

TEST(DungeonClearPathCursorTest, EmptyRouteReturnsZero)
{
    EXPECT_EQ(DungeonClearMath::PathProgressCursor({}, 0.0f, 0.0f, 0.0f), 0u);
}

TEST(DungeonClearPathCursorTest, FlatRouteIsUnaffectedByTheZTerm)
{
    // The ordinary single-storey case must behave exactly as the old 2D pick did.
    std::vector<G3D::Vector3> const flat{
        G3D::Vector3(0.0f, 0.0f, 100.0f),
        G3D::Vector3(10.0f, 0.0f, 100.0f),
        G3D::Vector3(20.0f, 0.0f, 100.0f),
    };
    EXPECT_EQ(DungeonClearMath::PathProgressCursor(flat, 11.0f, 2.0f, 100.0f), 1u);
    EXPECT_EQ(DungeonClearMath::PathProgressCursor(flat, 19.0f, -3.0f, 100.5f), 2u);
}

// ---------------------------------------------------------------------------
// DoorTravelRemaining — how far the door-blocked walk-in still has to go.
// ---------------------------------------------------------------------------

namespace
{
    // A straight corridor along +X, a vertex every 4yd, with a shut door at
    // x=40. Door band 8yd / z-band 6yd as in DungeonClearTuning.h, so the route
    // enters the band on the leg starting at x=28.
    std::vector<G3D::Vector3> DoorCorridor(float length = 60.0f)
    {
        std::vector<G3D::Vector3> route;
        for (float x = 0.0f; x <= length; x += 4.0f)
            route.emplace_back(x, 0.0f, 0.0f);
        return route;
    }

    float Remaining(std::vector<G3D::Vector3> const& route,
                    float bx, float by, float bz,
                    float doorX = 40.0f, float doorZ = 0.0f,
                    float lookAhead = 100.0f)
    {
        return DungeonClearMath::DoorTravelRemaining(
            route, bx, by, bz, doorX, 0.0f, doorZ,
            /*band*/ 8.0f, /*zBand*/ 6.0f, lookAhead, /*behindSlack*/ 15.0f);
    }

    constexpr float kDoorStopDistance = 10.0f;  // DC_DOOR_STOP_DISTANCE
}

TEST(DungeonClearDoorTravelTest, OnTheRouteNearTheDoorReadsAtTheDoor)
{
    float const r = Remaining(DoorCorridor(), 24.0f, 0.5f, 0.0f);
    EXPECT_NEAR(r, 4.5f, 0.01f);
    EXPECT_LE(r, kDoorStopDistance);
}

TEST(DungeonClearDoorTravelTest, OffTheRouteBesideTheDoorStillHasToWalkBack)
{
    // tr-20260924-130027-4 (Karazhan, Strange Bookcase): engage-trash dragged the
    // tank 28yd off its route. The route vertex nearest it still sat a few yards
    // short of the doorway, and without the joining leg the walk-in read "at
    // door", parked 34yd from the bookcase and let the watchdog auto-pause the
    // run. The 28yd back onto the route is travel it still owes.
    float const r = Remaining(DoorCorridor(), 24.0f, 28.0f, 0.0f);
    EXPECT_NEAR(r, 32.0f, 0.01f);
    EXPECT_GT(r, kDoorStopDistance);
}

TEST(DungeonClearDoorTravelTest, FarAlongTheRouteIsNotAtTheDoor)
{
    EXPECT_NEAR(Remaining(DoorCorridor(), 4.0f, 0.0f, 0.0f), 24.0f, 0.01f);
}

TEST(DungeonClearDoorTravelTest, DoorWellBehindTheBotIsNotABlocker)
{
    EXPECT_EQ(Remaining(DoorCorridor(), 56.0f, 0.0f, 0.0f),
              std::numeric_limits<float>::max());
}

TEST(DungeonClearDoorTravelTest, DoorOnAnotherFloorIsNeverEntered)
{
    EXPECT_EQ(Remaining(DoorCorridor(), 24.0f, 0.0f, 0.0f, 40.0f, /*doorZ*/ 20.0f),
              std::numeric_limits<float>::max());
}

TEST(DungeonClearDoorTravelTest, LookAheadCountsFromTheBotNotTheRouteStart)
{
    // A single long polyline built far behind the tank: the door's band is 16yd
    // ahead of the bot but 168yd from the route's start.
    std::vector<G3D::Vector3> const route = DoorCorridor(200.0f);
    EXPECT_NEAR(Remaining(route, 152.0f, 0.0f, 0.0f, /*doorX*/ 180.0f), 16.0f, 0.01f);
    // And a door past the look-ahead from the bot is not placed at all.
    EXPECT_EQ(Remaining(route, 20.0f, 0.0f, 0.0f, /*doorX*/ 180.0f),
              std::numeric_limits<float>::max());
}

TEST(DungeonClearDoorTravelTest, EmptyRouteIsUnplaced)
{
    EXPECT_EQ(Remaining({}, 0.0f, 0.0f, 0.0f), std::numeric_limits<float>::max());
}

// ---------------------------------------------------------------------------
// PathCursorIsJoinable — may the bot -> cursor leg be read as corridor?
//
// The cursor answers "which vertex is nearest"; it cannot answer "and is the
// bot anywhere near this route at all". Consumers that chain a synthesised leg
// from the bot to that vertex need the second answer too, because the leg is a
// straight line through whatever the map contains — see the Blackwing Lair
// regression at the bottom of this block, and CRITICAL #4 in
// DungeonClearBlockingDoorValue.cpp.

namespace
{
    // Resnap's own limit, the bound the door scan passes in. Mirrored here as a
    // literal so the test still means something if the follower's constant moves
    // for a movement reason: this block is about the RULE, not the number.
    float constexpr kResnapRadius = 45.0f;
}

TEST(DungeonClearPathCursorTest, JoinableWhileTheBotIsOnItsRoute)
{
    std::vector<G3D::Vector3> const route{
        G3D::Vector3(0.0f, 0.0f, 100.0f),
        G3D::Vector3(20.0f, 0.0f, 100.0f),
        G3D::Vector3(40.0f, 0.0f, 100.0f),
    };
    // Dead on the line, and drifted a few yards off it: both are the ordinary
    // case the joining leg exists to cover.
    EXPECT_TRUE(DungeonClearMath::PathCursorIsJoinable(route, 1u, 20.0f, 0.0f, 100.0f,
                                                       kResnapRadius));
    EXPECT_TRUE(DungeonClearMath::PathCursorIsJoinable(route, 1u, 18.0f, 5.0f, 100.0f,
                                                       kResnapRadius));
    // A sparse authored route's own leg length (the BWL anchors run to ~24yd)
    // must not read as off-route.
    EXPECT_TRUE(DungeonClearMath::PathCursorIsJoinable(route, 1u, -4.0f, 0.0f, 100.0f,
                                                       kResnapRadius));
}

TEST(DungeonClearPathCursorTest, NotJoinableBeyondTheResnapRadius)
{
    std::vector<G3D::Vector3> const route{
        G3D::Vector3(0.0f, 0.0f, 100.0f),
        G3D::Vector3(20.0f, 0.0f, 100.0f),
    };
    EXPECT_FALSE(DungeonClearMath::PathCursorIsJoinable(route, 0u, -46.0f, 0.0f, 100.0f,
                                                        kResnapRadius));
    // Measured in 3D, like the cursor pick itself: a vertex the bot is nearly
    // under in plan view but a tower's worth of Z away is not one to join to.
    EXPECT_FALSE(DungeonClearMath::PathCursorIsJoinable(route, 0u, 1.0f, 1.0f, 150.0f,
                                                        kResnapRadius));
}

TEST(DungeonClearPathCursorTest, NotJoinableWithNoVertexToJoinTo)
{
    std::vector<G3D::Vector3> const route{ G3D::Vector3(0.0f, 0.0f, 100.0f) };
    EXPECT_FALSE(DungeonClearMath::PathCursorIsJoinable({}, 0u, 0.0f, 0.0f, 0.0f,
                                                        kResnapRadius));
    EXPECT_FALSE(DungeonClearMath::PathCursorIsJoinable(route, 1u, 0.0f, 0.0f, 100.0f,
                                                        kResnapRadius));
}

// The live case, with the run's real coordinates: Blackwing Lair, the raid
// standing on Vaelastrasz's floor the moment it turns for Broodlord Lashlayer
// (run tr-20260828-103056-1).
//
// The authored Broodlord route is registered as an ANCHOR route, so the path is
// its twenty waypoints and nothing between them ("segs=21 firstSegPts=1"). Its
// FIRST anchor, the staging shelf, is 150yd away — but BWL folds back on itself,
// so its LAST anchor, the Broodlord standoff, is only 82yd away through solid
// rock and therefore wins the nearest-vertex pick. Chaining a joining leg to it
// draws an 82yd line through the mountain that leaves the map's walkable space
// entirely and passes within half a yard of the Broodlord passage portcullis,
// 342yd away along the real walk.
//
// Both halves of the regression are asserted: the cursor really does land on the
// route's END (so nothing here depends on a cursor change), and the joinability
// gate really does refuse that leg.
TEST(DungeonClearPathCursorTest, BlackwingLairFoldbackIsNotJoinable)
{
    // The three anchors that matter, in registry order (see
    // RegisterBlackwingLairRoute): staging, the Taskmaster ramp, the standoff.
    std::vector<G3D::Vector3> const bwl{
        G3D::Vector3(-7630.9f, -915.5f, 437.3f),     // 0  staging
        G3D::Vector3(-7627.03f, -926.86f, 440.63f),  // 1
        G3D::Vector3(-7650.86f, -999.24f, 440.61f),  // 6  mid lower room
        G3D::Vector3(-7707.85f, -1075.17f, 445.96f), // 11 the ramp
        G3D::Vector3(-7590.51f, -1041.64f, 449.85f), // 18
        G3D::Vector3(-7573.8f, -1033.5f, 449.3f),    // 19 the Broodlord standoff
    };
    // The tank's real position at teardown.
    float constexpr tankX = -7507.6f;
    float constexpr tankY = -1003.9f;
    float constexpr tankZ = 409.9f;

    // The fold-back: the nearest vertex is the LAST one, not the one the raid is
    // about to walk to.
    EXPECT_EQ(DungeonClearMath::PathProgressCursor(bwl, tankX, tankY, tankZ),
              bwl.size() - 1);

    // ...and it is 82yd away through a mountain, so no corridor may be read off
    // the leg that reaches it. This is what stops GO 179365 being flagged.
    EXPECT_FALSE(DungeonClearMath::PathCursorIsJoinable(bwl, bwl.size() - 1,
                                                        tankX, tankY, tankZ,
                                                        kResnapRadius));

    // The route's own start — where the raid was actually headed — is further
    // still, which is exactly why the cursor did not pick it.
    EXPECT_FALSE(DungeonClearMath::PathCursorIsJoinable(bwl, 0u, tankX, tankY, tankZ,
                                                        kResnapRadius));
}

// --- PullTagStopDistance (where the tag walk-in stops) ---------------------
using DungeonClearMath::PullTagStopDistance;

namespace
{
    // The live numbers this was rebuilt against: MgT's Sunblade elites carry
    // detection_range 20 and are level 70-71 against a level-70 party, so
    // GetAggroRange is 20-21yd. A bear tank's reach plus a humanoid's plus one is
    // ~4.3yd, which is the value the rotunda log kept printing as its STOP distance.
    float constexpr kAggro  = 20.0f;
    float constexpr kMelee  = 4.3f;
    // As passed by DcPullActions: 1500ms grace, 3yd/s, and a scripted stage's floor
    // is the aggro edge less DC_PULL_SCRIPTED_CREEP_LIMIT (4yd).
    uint32 constexpr kGrace = 1500;
    float constexpr kRate   = 3.0f;
    float constexpr kScriptedFloor = kAggro - 2.0f - 4.0f;   // 14.0
}

TEST(DcPullTagStopTest, StopsTwoYardsInsideTheAggroRadius)
{
    // The notice test is centre-to-centre distance vs GetAggroRange, re-run only on
    // relocation — so the tank has to ARRIVE strictly inside the radius. Two yards.
    bool force = true;
    EXPECT_FLOAT_EQ(PullTagStopDistance(kAggro, kMelee, 0, kGrace, kRate, 0.0f, force),
                    18.0f);
    EXPECT_FALSE(force);
    // Still 18 all the way through the grace: the arrival tick almost always trips
    // the notice on its own, and re-creeping immediately would just add a redundant
    // micro-move.
    EXPECT_FLOAT_EQ(PullTagStopDistance(kAggro, kMelee, kGrace, kGrace, kRate, 0.0f,
                                        force),
                    18.0f);
}

TEST(DcPullTagStopTest, AnOrdinaryPullMayCreepAllTheWayToContact)
{
    // Unbounded is right for a corridor pull: the pack it eventually touches is the
    // pack it came for, and a tank parked exactly at the edge is never re-evaluated.
    bool force = false;
    EXPECT_NEAR(PullTagStopDistance(kAggro, kMelee, kGrace + 1000, kGrace, kRate, 0.0f,
                                    force),
                15.0f, 0.01f);
    EXPECT_FALSE(force);
    EXPECT_FLOAT_EQ(PullTagStopDistance(kAggro, kMelee, kGrace + 10000, kGrace, kRate,
                                        0.0f, force),
                    kMelee);
    EXPECT_TRUE(force);
}

TEST(DcPullTagStopTest, AScriptedStageCreepsFourYardsAndNoFurther)
{
    // THE REGRESSION. A scripted stage's clock used to run from the start of
    // Advancing, which on a rotunda row is the far end of a 60-77yd walk to the stand
    // spot — so the first walk-in tick already carried 10s+ of creep, the stop point
    // collapsed onto the melee floor, and the tank body-pulled the formation from the
    // inside. Live: `closing to aggro edge (27.5yd, stop 4.3)` on every stage of
    // tp-20260803-232932-1, 10/10 runs stalled or wiped in that room.
    //
    // Two things stop it coming back. The caller now measures from stand-spot
    // arrival, which is this function's `closingMs` argument and is asserted in
    // DcPullActions rather than here. And the floor below, which bounds what any
    // amount of clock can spend.
    bool force = true;
    for (uint32 ms : {kGrace + 5000, kGrace + 20000, kGrace + 120000})
    {
        EXPECT_FLOAT_EQ(PullTagStopDistance(kAggro, kMelee, ms, kGrace, kRate,
                                            kScriptedFloor, force),
                        kScriptedFloor)
            << "closingMs=" << ms;
        EXPECT_FALSE(force) << "closingMs=" << ms;
    }
    // And the four yards it may spend are real — the creep still exists, because a
    // few more yards of approach is what generates the relocations the notice needs.
    EXPECT_NEAR(PullTagStopDistance(kAggro, kMelee, kGrace + 1000, kGrace, kRate,
                                    kScriptedFloor, force),
                15.0f, 0.01f);
    // 14.0 is a long way outside the pack. The rotunda's rows are authored with
    // 12-24yd of margin measured at the aggro edge and none at all at the spawn, so
    // "how far past the edge may this creep" is the difference between the plan
    // working and the plan taking a neighbouring formation.
    EXPECT_GT(kScriptedFloor, kMelee + 8.0f);
}

TEST(DcPullTagStopTest, ForceTagOnlyWhenClosingCannotCrossTheThreshold)
{
    // The core floors aggro at 5yd, so a much-higher-level tank can face a pack whose
    // radius is inside its own melee reach. Closing can never trip the notice there —
    // the caller has to swing — and that is the ONLY thing forceTag means.
    bool force = false;
    EXPECT_FLOAT_EQ(PullTagStopDistance(5.0f, kMelee, 0, kGrace, kRate, 0.0f, force),
                    kMelee);
    EXPECT_TRUE(force);

    // A scripted stage's floor does not rescue a genuinely-too-small radius: body
    // contact is the last floor either way.
    EXPECT_FLOAT_EQ(PullTagStopDistance(5.0f, kMelee, 0, kGrace, kRate, 1.0f, force),
                    kMelee);
    EXPECT_TRUE(force);
}

// ===========================================================================
// NearestPointOnPolyline2D — the point on a walk a mob comes closest to. Backs
// the en-route sweep's "would it actually see us" veto (DcTargeting::
// FindEnRouteAggroPack), which shoots one LOS ray from the mob to this point.
// ===========================================================================

TEST(DungeonClearMathTest, NearestPointOnPolylineNeedsTwoPoints)
{
    G3D::Vector3 out(-1.0f, -1.0f, -1.0f);
    EXPECT_FALSE(DungeonClearMath::NearestPointOnPolyline2D({}, 0.0f, 0.0f, out));
    EXPECT_FALSE(DungeonClearMath::NearestPointOnPolyline2D(
        {G3D::Vector3(1.0f, 2.0f, 3.0f)}, 0.0f, 0.0f, out));
    // Untouched on failure — a caller must never shoot a ray at a stale point.
    EXPECT_FLOAT_EQ(out.x, -1.0f);
}

TEST(DungeonClearMathTest, NearestPointOnPolylineProjectsOntoTheNearestLeg)
{
    // An L: out along +X, then up along +Y.
    std::vector<G3D::Vector3> const route{G3D::Vector3(0.0f, 0.0f, 0.0f),
                                          G3D::Vector3(20.0f, 0.0f, 0.0f),
                                          G3D::Vector3(20.0f, 20.0f, 0.0f)};
    G3D::Vector3 out;

    // Beside the first leg.
    ASSERT_TRUE(DungeonClearMath::NearestPointOnPolyline2D(route, 8.0f, 5.0f, out));
    EXPECT_FLOAT_EQ(out.x, 8.0f);
    EXPECT_FLOAT_EQ(out.y, 0.0f);

    // Beside the second.
    ASSERT_TRUE(DungeonClearMath::NearestPointOnPolyline2D(route, 26.0f, 12.0f, out));
    EXPECT_FLOAT_EQ(out.x, 20.0f);
    EXPECT_FLOAT_EQ(out.y, 12.0f);
}

TEST(DungeonClearMathTest, NearestPointOnPolylineClampsToTheEnds)
{
    std::vector<G3D::Vector3> const route{G3D::Vector3(0.0f, 0.0f, 0.0f),
                                          G3D::Vector3(20.0f, 0.0f, 0.0f)};
    G3D::Vector3 out;

    // Behind the start and past the end both clamp to a real endpoint rather
    // than running off the infinite line.
    ASSERT_TRUE(DungeonClearMath::NearestPointOnPolyline2D(route, -30.0f, 4.0f, out));
    EXPECT_FLOAT_EQ(out.x, 0.0f);
    ASSERT_TRUE(DungeonClearMath::NearestPointOnPolyline2D(route, 55.0f, 4.0f, out));
    EXPECT_FLOAT_EQ(out.x, 20.0f);
}

TEST(DungeonClearMathTest, NearestPointOnPolylineInterpolatesZ)
{
    // A ramp climbing 10yd over 20yd of run. The Z matters: the LOS ray is shot
    // from this point + eye height, and snapping to a vertex instead would aim
    // the ray 5yd under (or over) the floor the walker is actually on.
    std::vector<G3D::Vector3> const route{G3D::Vector3(0.0f, 0.0f, 0.0f),
                                          G3D::Vector3(20.0f, 0.0f, 10.0f)};
    G3D::Vector3 out;
    ASSERT_TRUE(DungeonClearMath::NearestPointOnPolyline2D(route, 10.0f, 3.0f, out));
    EXPECT_FLOAT_EQ(out.x, 10.0f);
    EXPECT_FLOAT_EQ(out.z, 5.0f);
}

TEST(DungeonClearMathTest, NearestPointOnPolylineToleratesDegenerateLegs)
{
    // The route smoother emits coincident points at anchor joins; a zero-length
    // leg must not divide by zero, it collapses to its own start point.
    std::vector<G3D::Vector3> const route{G3D::Vector3(0.0f, 0.0f, 0.0f),
                                          G3D::Vector3(0.0f, 0.0f, 0.0f),
                                          G3D::Vector3(20.0f, 0.0f, 0.0f)};
    G3D::Vector3 out;
    ASSERT_TRUE(DungeonClearMath::NearestPointOnPolyline2D(route, 6.0f, 2.0f, out));
    EXPECT_FLOAT_EQ(out.x, 6.0f);
    EXPECT_FLOAT_EQ(out.y, 0.0f);
}

// --- PointTowardFrom: the transit pack's hold point --------------------------
//
// Same defect the heal-reposition fallback once had: the hold
// point is walked back along the bearing from the route anchor, but z used to
// stay at the ANCHOR's height — the anchor's floor over a point up to a leash
// away. Flat legs hid it; the Suppression Rooms ramps do not.

TEST(DungeonClearMathTest, PointTowardFromCarriesZAlongTheBearing)
{
    Position const anchor(0.0f, 0.0f, 100.0f, 0.0f);
    Position const toward(0.0f, 40.0f, 140.0f, 0.0f);   // 40yd out, 40yd up

    // Half way out is half way up.
    Position const p = DungeonClearMath::PointTowardFrom(anchor, toward, 20.0f, 1.0f);
    EXPECT_NEAR(p.GetPositionY(), 20.0f, 1e-3f);
    EXPECT_NEAR(p.GetPositionZ(), 120.0f, 1e-3f);
}

// A flat leg — the common case — must not move at all.
TEST(DungeonClearMathTest, PointTowardFromFlatLegKeepsItsHeight)
{
    Position const anchor(0.0f, 0.0f, 440.0f, 0.0f);
    Position const toward(0.0f, 25.0f, 440.0f, 0.0f);

    Position const p = DungeonClearMath::PointTowardFrom(anchor, toward, 21.0f, 1.0f);
    EXPECT_NEAR(p.GetPositionY(), 21.0f, 1e-3f);
    EXPECT_NEAR(p.GetPositionZ(), 440.0f, 1e-3f);
}

// THE BUG, on the crossing's steepest authored leg: anchors 30 -> 31 climb
// 445.96 -> 451.31 over 20.0yd. A follower down at the anchor-30 end gets a hold
// point three quarters of the way back along that ramp, and the anchor's height
// there is four yards of solid rock.
TEST(DungeonClearMathTest, PointTowardFromNeverAuthorsTheAnchorsFloor)
{
    Position const anchor(-7695.20f, -1090.66f, 451.31f, 0.0f);
    Position const bot(-7707.85f, -1075.17f, 445.96f, 0.0f);

    Position const p = DungeonClearMath::PointTowardFrom(anchor, bot, 15.0f, 1.0f);

    // Strictly between the two ends, never ON the anchor's floor.
    EXPECT_LT(p.GetPositionZ(), 451.31f);
    EXPECT_GT(p.GetPositionZ(), 445.96f);
    // 3/4 of the way down: 451.31 - 0.75 * 5.35.
    EXPECT_NEAR(p.GetPositionZ(), 447.30f, 5e-2f);
}

// Clamped to the target when it is nearer than the requested distance, so the
// hold point never overshoots the bot it is pulling in.
TEST(DungeonClearMathTest, PointTowardFromClampsToTheNearerTarget)
{
    Position const anchor(0.0f, 0.0f, 10.0f, 0.0f);
    Position const toward(0.0f, 5.0f, 15.0f, 0.0f);

    Position const p = DungeonClearMath::PointTowardFrom(anchor, toward, 21.0f, 1.0f);
    EXPECT_NEAR(p.GetPositionY(), 5.0f, 1e-3f);
    EXPECT_NEAR(p.GetPositionZ(), 15.0f, 1e-3f);
}

// Stacked within the bearing floor: the anchor comes back untouched, no NaN.
TEST(DungeonClearMathTest, PointTowardFromDegenerateBearingReturnsTheAnchor)
{
    Position const anchor(12.0f, 34.0f, 56.0f, 1.5f);
    Position const toward(12.2f, 34.1f, 90.0f, 0.0f);

    Position const p = DungeonClearMath::PointTowardFrom(anchor, toward, 21.0f, 1.0f);
    EXPECT_FALSE(std::isnan(p.GetPositionX()));
    EXPECT_NEAR(p.GetPositionX(), 12.0f, 1e-3f);
    EXPECT_NEAR(p.GetPositionZ(), 56.0f, 1e-3f);
}

// ==== follow-tank centered breadcrumb trail: engage gate ===================
//
// The rung engages on a leash measured to the TANK but aims at a crumb `lag`
// yards behind it, and the per-bot stagger grows `lag` while the leash stays
// put. Below, the shipped live numbers: Playerbots.FollowDistance 1.5 gives
// dist 1.5, leash 3.5, and crumbs at 1.5 / 4.5 / 7.5 / 10.5.

// The slot-0 follower's crumb is inside the leash, so past the leash is always
// past the slot — the original behaviour, unchanged.
TEST(DungeonClearMathTest, TrailFollowEngagesForTheNearestStaggerSlot)
{
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(3.4f, 3.5f, 1.5f));
    EXPECT_TRUE(DungeonClearMath::TrailFollowShouldEngage(3.6f, 3.5f, 1.5f));
    EXPECT_TRUE(DungeonClearMath::TrailFollowShouldEngage(19.7f, 3.5f, 1.5f));
}

// THE REGRESSION. A follower 5.1-7.1yd behind the tank with the lag-10.5 slot is
// past the leash but INSIDE its own slot: its crumb sits farther from the tank
// than it does, so trailing to it is a walk backward, out of the leash, straight
// back into the Follow() fan that walks it forward again. Observed live in
// DungeonClearPull.log at exactly these distances.
TEST(DungeonClearMathTest, TrailFollowNeverRetreatsToReachItsStaggerSlot)
{
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(5.1f, 3.5f, 10.5f));
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(5.9f, 3.5f, 10.5f));
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(7.1f, 3.5f, 10.5f));
    // Same shape one slot in.
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(5.9f, 3.5f, 7.5f));
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(4.0f, 3.5f, 4.5f));
}

// Genuinely fallen behind: the crumb is now ahead of us and trailing closes the
// gap. The rung must still engage, or the party never inherits the tank's
// corridor-centered line on a long leg.
TEST(DungeonClearMathTest, TrailFollowStillEngagesOnceTrulyBehind)
{
    EXPECT_TRUE(DungeonClearMath::TrailFollowShouldEngage(10.6f, 3.5f, 10.5f));
    EXPECT_TRUE(DungeonClearMath::TrailFollowShouldEngage(19.0f, 3.5f, 10.5f));
    EXPECT_TRUE(DungeonClearMath::TrailFollowShouldEngage(19.8f, 3.5f, 7.5f));
}

// The leash still binds when it is the larger of the two: a follower inside the
// follow bubble does not trail just because its slot is tighter still.
TEST(DungeonClearMathTest, TrailFollowLeashStillBindsInsideTheBubble)
{
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(3.0f, 8.0f, 1.5f));
    EXPECT_FALSE(DungeonClearMath::TrailFollowShouldEngage(7.9f, 8.0f, 6.0f));
    EXPECT_TRUE(DungeonClearMath::TrailFollowShouldEngage(8.1f, 8.0f, 6.0f));
}

// ---- off-line rejoin rung: hysteresis + refusal handling ------------------
// Root cause of the BWL ping-pong + wall clip, tr-20260830-115416-5. See
// DungeonClearMath::IsOffLineWithHysteresis / DecideRejoinRefusal.

TEST(DungeonClearMathTest, OffLineEngagesAtTheEngageBarWhenUnlatched)
{
    // Unlatched: only the 6yd engage bar counts. The live failure sat at 4.1-6.9yd,
    // so both sides of that band must be decidable.
    EXPECT_FALSE(DungeonClearMath::IsOffLineWithHysteresis(4.1f, false, false, 6.0f, 3.0f));
    EXPECT_FALSE(DungeonClearMath::IsOffLineWithHysteresis(6.0f, false, false, 6.0f, 3.0f));
    EXPECT_TRUE(DungeonClearMath::IsOffLineWithHysteresis(6.9f, false, false, 6.0f, 3.0f));
}

TEST(DungeonClearMathTest, OffLineHoldsToTheReleaseBarOnceLatched)
{
    // THE FIX. Latched at 6.9yd, the rung must keep the bot through the whole
    // 3-6yd band instead of handing it back to the escort spline, whose straight
    // opening leg is what cut the BWL anchor 16->18 bend into a navmesh void.
    EXPECT_TRUE(DungeonClearMath::IsOffLineWithHysteresis(5.9f, true, false, 6.0f, 3.0f));
    EXPECT_TRUE(DungeonClearMath::IsOffLineWithHysteresis(4.1f, true, false, 6.0f, 3.0f));
    EXPECT_TRUE(DungeonClearMath::IsOffLineWithHysteresis(3.1f, true, false, 6.0f, 3.0f));
    // Genuinely back on the corridor -> released.
    EXPECT_FALSE(DungeonClearMath::IsOffLineWithHysteresis(3.0f, true, false, 6.0f, 3.0f));
    EXPECT_FALSE(DungeonClearMath::IsOffLineWithHysteresis(0.4f, true, false, 6.0f, 3.0f));
}

TEST(DungeonClearMathTest, OffLineVerticalMismatchIsNotHysteretic)
{
    // A floor mismatch is binary: it fires from either latch state and does not
    // linger once the bot is back on its own storey.
    EXPECT_TRUE(DungeonClearMath::IsOffLineWithHysteresis(0.0f, false, true, 6.0f, 3.0f));
    EXPECT_TRUE(DungeonClearMath::IsOffLineWithHysteresis(0.0f, true, true, 6.0f, 3.0f));
    EXPECT_FALSE(DungeonClearMath::IsOffLineWithHysteresis(0.0f, false, false, 6.0f, 3.0f));
}

TEST(DungeonClearMathTest, OffLineHysteresisCannotFlapInsideTheBand)
{
    // Walk a bot back and forth across 6yd the way the live tank did and assert
    // the latch never toggles twice without a genuine re-entry between.
    bool latched = false;
    float const devs[] = {4.1f, 6.9f, 5.6f, 5.8f, 6.9f, 4.1f, 5.7f, 2.9f, 4.5f};
    int toggles = 0;
    for (float d : devs)
    {
        bool const next = DungeonClearMath::IsOffLineWithHysteresis(d, latched, false, 6.0f, 3.0f);
        if (next != latched)
            ++toggles;
        latched = next;
    }
    // Exactly two: off->on at 6.9, on->off at 2.9. Pre-fix this sequence toggled
    // six times, and every "off" tick launched an unscreened escort leg.
    EXPECT_EQ(toggles, 2);
    // Released at 2.9, the bar goes back to the 6yd ENGAGE side, so the trailing
    // 4.5 stays on-line. That residual 3-6yd band is exactly what the spline
    // window's opening-leg LOS screen covers (see FillHopObs) — the two halves of
    // the fix meet here and neither is redundant.
    EXPECT_FALSE(latched);
}

TEST(DungeonClearMathTest, RejoinRefusalGivesTheFirstTickGrace)
{
    // FLT_MAX seed = no rejoin of ours in flight yet. The first refusal only
    // establishes the baseline; halting here would stutter the healthy case.
    DungeonClearMath::RejoinRefusalVerdict const v = DungeonClearMath::DecideRejoinRefusal(
        6.2f, std::numeric_limits<float>::max(), 3.0f);
    EXPECT_FALSE(v.haltStaleMove);
    EXPECT_FLOAT_EQ(v.bestDeviation, 6.2f);
}

TEST(DungeonClearMathTest, RejoinGapIsPlanViewWithinOneStorey)
{
    // A ramp or stair flight inside the level tolerance measures exactly as before.
    EXPECT_FLOAT_EQ(DungeonClearMath::RejoinGap(6.2f, 0.0f, 5.0f), 6.2f);
    EXPECT_FLOAT_EQ(DungeonClearMath::RejoinGap(6.2f, -4.9f, 5.0f), 6.2f);
    EXPECT_FLOAT_EQ(DungeonClearMath::RejoinGap(6.2f, 5.0f, 5.0f), 6.2f);
}

// tr-20260927-190943-12: tank on the Opera audience floor, hop on the balcony
// 14.2yd overhead, 2.3yd in plan view.
TEST(DungeonClearMathTest, RepathOffLevelFiresUnderTheBalcony)
{
    EXPECT_TRUE(DungeonClearMath::ShouldRepathOffLevel(-14.2f, 5.0f, false));
    EXPECT_TRUE(DungeonClearMath::ShouldRepathOffLevel(14.2f, 5.0f, false));
}

TEST(DungeonClearMathTest, RepathOffLevelIgnoresARampOrStairFlight)
{
    EXPECT_FALSE(DungeonClearMath::ShouldRepathOffLevel(4.9f, 5.0f, false));
    EXPECT_FALSE(DungeonClearMath::ShouldRepathOffLevel(-5.0f, 5.0f, false));
}

TEST(DungeonClearMathTest, RepathOffLevelIsSpentOnce)
{
    EXPECT_FALSE(DungeonClearMath::ShouldRepathOffLevel(-14.2f, 5.0f, true));
}

// tr-20261018-101829-3: tank 30yd from Emperor Thaurissan, 5.1yd under his
// throne, stock PathGenerator NOPATH to him, boss in plain view.
TEST(DungeonClearMathTest, NoRouteToADaisBossFallsBackToSight)
{
    EXPECT_TRUE(DungeonClearMath::ProbeSaysSameFloor(false, false, false, 0.0f, 32.3f, true));
}

TEST(DungeonClearMathTest, NoRouteAndNoSightIsAnotherFloor)
{
    // A boss a storey away is behind the floor slab.
    EXPECT_FALSE(DungeonClearMath::ProbeSaysSameFloor(false, false, false, 0.0f, 32.3f, false));
}

TEST(DungeonClearMathTest, ARouteStillDecidesByLengthWhateverTheSight)
{
    // Walkway over Rethilgore: in sight, but the stairs make it a long walk.
    EXPECT_FALSE(DungeonClearMath::ProbeSaysSameFloor(true, true, true, 80.0f, 32.3f, true));
    EXPECT_TRUE(DungeonClearMath::ProbeSaysSameFloor(true, true, true, 30.0f, 32.3f, false));
    // INCOMPLETE is an answer ("not reachable from here"), not a fall-back.
    EXPECT_FALSE(DungeonClearMath::ProbeSaysSameFloor(true, false, true, 20.0f, 32.3f, true));
    // A short route that ends on the wrong level is not the boss's floor.
    EXPECT_FALSE(DungeonClearMath::ProbeSaysSameFloor(true, true, false, 20.0f, 32.3f, true));
}

TEST(DungeonClearMathTest, RejoinGapGrowsWhileSinkingUnderTheRoutePoint)
{
    // tr-20260927-101342-8: 2D deviation 3.2 -> 0.0yd while the tank walked the
    // Servants' Quarters helix from 12.9 to 33.3yd under the route point. The gap
    // must read that as drifting away, so DecideRejoinRefusal halts the move.
    float const before = DungeonClearMath::RejoinGap(3.2f, -12.9f, 5.0f);
    float const after  = DungeonClearMath::RejoinGap(0.0f, -33.3f, 5.0f);
    EXPECT_NEAR(before, std::hypot(3.2f, 7.9f), 1e-4f);
    EXPECT_FLOAT_EQ(after, 28.3f);
    EXPECT_TRUE(DungeonClearMath::DecideRejoinRefusal(after, before, 3.0f).haltStaleMove);
    // The same numbers read in plan view alone looked like convergence.
    EXPECT_FALSE(DungeonClearMath::DecideRejoinRefusal(0.0f, 3.2f, 3.0f).haltStaleMove);
}

namespace
{
    // Replays gaps through TrackRejoinProgress from a fresh episode; returns the
    // ticks counted idle, i.e. what the refusal ladder would have accumulated.
    uint32_t CountRejoinIdle(std::vector<float> const& gaps, float eps = 0.25f)
    {
        float best = std::numeric_limits<float>::max();
        uint32_t idle = 0;
        for (float g : gaps)
        {
            DungeonClearMath::RejoinProgressVerdict const v =
                DungeonClearMath::TrackRejoinProgress(g, best, eps);
            best = v.best;
            if (v.progressed)
                idle = 0;
            else if (v.idle)
                ++idle;
        }
        return idle;
    }
}

TEST(DungeonClearMathTest, RejoinProgressNeverCountsAConvergingBot)
{
    // tr-20260927-132444-20: one rejoin issued, then 8 duplicate refusals while
    // the tank closed 15.2 -> 9.6yd — the old ladder struck at 9.6. Closing is
    // progress on every tick, however many re-issues stock refused.
    EXPECT_EQ(CountRejoinIdle({15.2f, 14.5f, 13.8f, 13.1f, 12.4f, 11.7f, 11.0f, 10.3f, 9.6f}), 0u);
}

TEST(DungeonClearMathTest, RejoinProgressCountsAFrozenBotEvenIfItIssues)
{
    // tr-20260901-223655-10 (HoL): a constant 267.2yd. Whether the ticks were
    // refused or issued a move that bought nothing, every one after the baseline
    // must reach the ladder.
    EXPECT_EQ(CountRejoinIdle(std::vector<float>(20, 267.2f)), 19u);
}

TEST(DungeonClearMathTest, RejoinProgressFirstTickIsOnlyTheBaseline)
{
    // An off-path rebuild restarts the episode every few ticks; its first tick
    // must not read as progress (free ladder reset) nor as idle.
    DungeonClearMath::RejoinProgressVerdict const v = DungeonClearMath::TrackRejoinProgress(
        11.3f, std::numeric_limits<float>::max(), 0.25f);
    EXPECT_FALSE(v.progressed);
    EXPECT_FALSE(v.idle);
    EXPECT_FLOAT_EQ(v.best, 11.3f);
}

TEST(DungeonClearMathTest, RejoinProgressSlowClosingAccumulates)
{
    // 0.1yd a tick is under eps, but best holds still until the gap clears it.
    EXPECT_EQ(CountRejoinIdle({10.0f, 9.9f, 9.8f}), 2u);
    EXPECT_EQ(CountRejoinIdle({10.0f, 9.9f, 9.8f, 9.7f}), 0u);
}

TEST(DungeonClearMathTest, RejoinProgressCountsAStopShortOfTheLine)
{
    // tr-20260927-114033-3: closed to 5.8yd, then loot stopped the tank. The
    // standing ticks count (the rung then re-issues with the lock released).
    EXPECT_EQ(CountRejoinIdle({26.9f, 20.0f, 12.0f, 5.8f, 5.8f, 5.8f, 5.8f}), 3u);
}

TEST(DungeonClearMathTest, RejoinRefusalRidesAWorkingReEntry)
{
    // Deviation holding or shrinking means the in-flight move IS the re-entry.
    // Cancelling it every refused tick is the stop/re-issue stutter this guards.
    EXPECT_FALSE(DungeonClearMath::DecideRejoinRefusal(6.2f, 6.2f, 3.0f).haltStaleMove);
    EXPECT_FALSE(DungeonClearMath::DecideRejoinRefusal(4.0f, 6.2f, 3.0f).haltStaleMove);
    EXPECT_FALSE(DungeonClearMath::DecideRejoinRefusal(0.5f, 6.2f, 3.0f).haltStaleMove);
    // The baseline tracks the best seen, so a later drift is measured from 0.5.
    EXPECT_FLOAT_EQ(DungeonClearMath::DecideRejoinRefusal(0.5f, 6.2f, 3.0f).bestDeviation, 0.5f);
}

TEST(DungeonClearMathTest, RejoinRefusalIsBlindToABotThatNeverMovesAtAll)
{
    // THE BLIND SPOT — and the whole reason DcApproachState::rejoinRefusals had to
    // be added alongside this verdict. DecideRejoinRefusal only ever asks "is the
    // move in flight carrying me AWAY?". A bot whose DcMoveTo is refused on
    // GEOMETRY rather than on contention never moves at all, so its deviation is
    // perfectly CONSTANT — which reads here, correctly and uselessly, as a re-entry
    // holding its ground. tr-20260901-223655-10 (Halls of Lightning) sat frozen at
    // 267.2yd for 3924 consecutive refused ticks and this verdict never once asked
    // for a halt, while the rung it advises reset stuckCount and cleared the stall
    // on every one of them.
    //
    // The fix is NOT to tighten the slack here — that would break
    // RejoinRefusalRidesAWorkingReEntry above, which needs a held deviation to mean
    // "ride it". Liveness is a different question and is counted by the rung.
    float best = std::numeric_limits<float>::max();
    for (int tick = 0; tick < 10; ++tick)
    {
        DungeonClearMath::RejoinRefusalVerdict const v =
            DungeonClearMath::DecideRejoinRefusal(267.2f, best, 3.0f);
        EXPECT_FALSE(v.haltStaleMove) << "tick " << tick;
        best = v.bestDeviation;
    }
    EXPECT_FLOAT_EQ(best, 267.2f);
}

TEST(DungeonClearMathTest, RejoinRefusalToleratesRoundingWideWithinSlack)
{
    // A pathed re-entry rounding a corner may swing a couple of yards wider
    // before it closes. Inside the slack that must not be read as drift.
    EXPECT_FALSE(DungeonClearMath::DecideRejoinRefusal(9.2f, 6.2f, 3.0f).haltStaleMove);
    EXPECT_TRUE(DungeonClearMath::DecideRejoinRefusal(9.3f, 6.2f, 3.0f).haltStaleMove);
}

TEST(DungeonClearMathTest, RejoinRefusalHaltsTheLiveBwlRunaway)
{
    // THE FIX, on the measured deviations from tr-20260830-115416-5. Pre-fix all
    // 22 of these ticks returned "success" having moved nothing, and the tank
    // drifted 6.2 -> 31.1yd before Resnap blew its 45yd radius.
    float const devs[] = {6.2f, 7.6f, 9.0f, 10.5f, 10.6f, 12.6f, 14.1f, 15.7f,
                          17.2f, 19.9f, 21.2f, 22.6f, 24.0f, 26.2f, 27.2f, 28.3f,
                          29.4f, 30.0f, 30.6f, 31.1f, 31.1f, 31.0f};
    float best = std::numeric_limits<float>::max();
    int halts = 0;
    float worstBeforeFirstHalt = 0.0f;
    for (float d : devs)
    {
        DungeonClearMath::RejoinRefusalVerdict const v =
            DungeonClearMath::DecideRejoinRefusal(d, best, 3.0f);
        if (v.haltStaleMove)
            ++halts;
        else if (halts == 0)
            worstBeforeFirstHalt = d;
        best = v.bestDeviation;
    }
    // It must halt, and it must halt EARLY — the whole point is to stop the
    // runaway near the corridor, not after it has crossed the map.
    EXPECT_GT(halts, 0);
    EXPECT_LT(worstBeforeFirstHalt, 10.0f);
}

TEST(DungeonClearMathTest, RejoinRefusalRebaselinesAfterAHalt)
{
    // After a halt the next tick must not re-halt on the same stale baseline —
    // otherwise the rung cancels every tick and never lets a re-issue land.
    DungeonClearMath::RejoinRefusalVerdict const halted =
        DungeonClearMath::DecideRejoinRefusal(12.0f, 6.2f, 3.0f);
    ASSERT_TRUE(halted.haltStaleMove);
    EXPECT_FLOAT_EQ(halted.bestDeviation, 12.0f);
    EXPECT_FALSE(DungeonClearMath::DecideRejoinRefusal(12.0f, halted.bestDeviation, 3.0f)
                     .haltStaleMove);
}

// ===== Loot-roll rung starvation bound (LootRollRungMayFire) =====
//
// The rung sits at relevance 95 above the whole driving ladder, and one action
// runs per tick. tr-20260831-123946-18: a roll whose Loot had been emptied made
// Group::CountRollVote refuse the vote, so the rung fired and its action
// reported success on all 3142 ticks of the freeze while `dungeon clear advance`
// (15) was pushed every tick and executed on none. This bounds that class.

TEST(DungeonClearMathTest, LootRollRungFiresForAHealthyWindow)
{
    std::uint64_t sig = 0;
    std::uint32_t ticks = 0;

    // The action clears its whole backlog in one Execute, so a healthy window is
    // one firing followed by an empty set.
    EXPECT_TRUE(DungeonClearMath::LootRollRungMayFire(2, 0xABCD, 5, sig, ticks));
    EXPECT_FALSE(DungeonClearMath::LootRollRungMayFire(0, 0, 5, sig, ticks));
    EXPECT_EQ(ticks, 0u);   // empty set restores the full budget
}

TEST(DungeonClearMathTest, LootRollRungStandsDownOnAnUnchangedSet)
{
    std::uint64_t sig = 0;
    std::uint32_t ticks = 0;

    // Same unresolvable roll every tick: fires for the budget, then yields.
    for (std::uint32_t i = 0; i < 5; ++i)
        EXPECT_TRUE(DungeonClearMath::LootRollRungMayFire(1, 0xDEAD, 5, sig, ticks)) << "tick " << i;

    EXPECT_FALSE(DungeonClearMath::LootRollRungMayFire(1, 0xDEAD, 5, sig, ticks));
    EXPECT_EQ(ticks, 6u);   // == max + 1: the one tick the one-shot log fires on

    // And it STAYS down while nothing changes — without this the freeze resumes.
    for (int i = 0; i < 50; ++i)
        EXPECT_FALSE(DungeonClearMath::LootRollRungMayFire(1, 0xDEAD, 5, sig, ticks));
}

TEST(DungeonClearMathTest, LootRollRungReArmsWhenTheRollSetChanges)
{
    std::uint64_t sig = 0;
    std::uint32_t ticks = 0;

    while (DungeonClearMath::LootRollRungMayFire(1, 0xDEAD, 5, sig, ticks)) {}
    EXPECT_FALSE(DungeonClearMath::LootRollRungMayFire(1, 0xDEAD, 5, sig, ticks));

    // A new drop lands: the digest changes and the rung must roll on it at once.
    // A latch that suppressed this would trade one bug for a bot that stops
    // rolling for the rest of the run.
    EXPECT_TRUE(DungeonClearMath::LootRollRungMayFire(2, 0xBEEF, 5, sig, ticks));
    EXPECT_EQ(ticks, 1u);
}

TEST(DungeonClearMathTest, LootRollRungCountsRollsRatherThanTrustingTheDigest)
{
    std::uint64_t sig = 0;
    std::uint32_t ticks = 0;

    // votable == 0 is the ONLY "nothing pending" signal. A digest that happens
    // to be zero with rolls still pending must still fire, or a hash collision
    // would silently disable rolling.
    EXPECT_TRUE(DungeonClearMath::LootRollRungMayFire(1, 0, 5, sig, ticks));
    EXPECT_EQ(ticks, 1u);
}

// --- RoomClearGiveUpDue (the room-clear no-progress valve) -----------------
// Only READY time with no drop in the count may give up a room clear. A fight, a
// rest or a loot between packs pauses the clock; an unobserved gap (nobody read
// the value) is not charged either.

using DungeonClearMath::RoomClearClock;
using DungeonClearMath::RoomClearGiveUpDue;

namespace
{
    // Advance the clock in 500ms polls from `from` to `to` with a fixed count.
    bool Poll(RoomClearClock& c, std::uint32_t remaining, bool busy,
              std::uint32_t from, std::uint32_t to, std::uint32_t timeoutMs)
    {
        bool due = false;
        for (std::uint32_t t = from; t <= to; t += 500)
            due = RoomClearGiveUpDue(c, remaining, /*inRoom*/ true, busy, t, timeoutMs);
        return due;
    }
}

TEST(DungeonClearRoomClearClockTest, ReadyStallGivesUpAfterTimeout)
{
    RoomClearClock c;
    EXPECT_FALSE(Poll(c, 5, false, 1000, 30000, 30000));
    EXPECT_TRUE(Poll(c, 5, false, 30500, 32000, 30000));
}

TEST(DungeonClearRoomClearClockTest, BusyTimeDoesNotCount)
{
    // A long fight then a long rest after the first formation: 90s busy.
    RoomClearClock c;
    EXPECT_FALSE(Poll(c, 4, false, 1000, 5000, 30000));
    EXPECT_FALSE(Poll(c, 4, true, 5500, 95000, 30000));
    // Ready again: the clock resumes from ~4s, not from 94s.
    EXPECT_FALSE(Poll(c, 4, false, 95500, 110000, 30000));
    EXPECT_TRUE(Poll(c, 4, false, 110500, 125000, 30000));
}

TEST(DungeonClearRoomClearClockTest, UnobservedGapIsNotCharged)
{
    RoomClearClock c;
    EXPECT_FALSE(Poll(c, 4, false, 1000, 3000, 30000));
    // Nobody read the value for a minute (the at-boss gate never reached it).
    EXPECT_FALSE(RoomClearGiveUpDue(c, 4, true, false, 63000, 30000));
    EXPECT_LT(c.idleMs, 5000u);
}

TEST(DungeonClearRoomClearClockTest, RespawnChurnStillGivesUp)
{
    // Pull, fight, rest, ready for a few seconds, repeat — the count never drops.
    RoomClearClock c;
    std::uint32_t t = 1000;
    bool due = false;
    for (int cycle = 0; cycle < 12 && !due; ++cycle)
    {
        due = Poll(c, 3, false, t, t + 4000, 30000);
        t += 4500;
        due = due || Poll(c, 3, true, t, t + 20000, 30000);
        t += 20500;
    }
    EXPECT_TRUE(due);
}

TEST(DungeonClearRoomClearClockTest, ProgressAndTravelReArm)
{
    RoomClearClock c;
    EXPECT_FALSE(Poll(c, 6, false, 1000, 25000, 30000));
    // A kill re-arms.
    EXPECT_FALSE(Poll(c, 5, false, 25500, 50000, 30000));
    EXPECT_EQ(c.lastRemaining, 5u);
    // Leaving the room re-arms too.
    EXPECT_FALSE(RoomClearGiveUpDue(c, 5, /*inRoom*/ false, false, 50500, 30000));
    EXPECT_EQ(c.idleMs, 0u);
    // An empty room resets everything.
    EXPECT_FALSE(RoomClearGiveUpDue(c, 0, true, false, 51000, 30000));
    EXPECT_EQ(c.lastRemaining, 0u);
}

TEST(DungeonClearRoomClearClockTest, PatrollerWalkingInThenDyingIsProgress)
{
    RoomClearClock c;
    EXPECT_FALSE(Poll(c, 5, false, 1000, 20000, 30000));
    // A Skeletal Waiter walks into the radius: 6. Idle time is kept.
    EXPECT_FALSE(RoomClearGiveUpDue(c, 6, true, false, 20500, 30000));
    std::uint32_t const idleBefore = c.idleMs;
    EXPECT_GT(idleBefore, 15000u);
    // It dies: back to 5, which is below the new high, so progress.
    EXPECT_FALSE(RoomClearGiveUpDue(c, 5, true, false, 21000, 30000));
    EXPECT_EQ(c.idleMs, 0u);
}

TEST(DungeonClearRoomClearClockTest, ZeroTimeoutNeverGivesUp)
{
    RoomClearClock c;
    EXPECT_FALSE(Poll(c, 5, false, 1000, 200000, 0));
}

// --- Room-clear straight pull (RankStraightPullLanes) -------------------------
//
// Karazhan Banquet Hall, tr-20260926-192642-3: the west Phantom Guest formation
// was tagged from the dais side and dragged to an east camp; the lane passed
// 23yd from a dinner guest and Moroes joined the trash fight.
namespace
{
    constexpr float kMoroesX = -10982.7f, kMoroesY = -1877.9f;
    constexpr float kPackX = -11003.0f, kPackY = -1896.0f;
    constexpr float kTankX = -10971.8f, kTankY = -1939.7f;  // previous camp
    // Moroes' camp box (RoomAggroRegistry row).
    constexpr float kBoxMinX = -11013.0f, kBoxMaxX = -10900.0f;
    constexpr float kBoxMinY = -1940.0f, kBoxMaxY = -1800.0f;

    std::vector<DungeonClearMath::StraightPullLane> MoroesLanes()
    {
        std::vector<DungeonClearMath::LaneKeepAway> const keep = {
            {kMoroesX, kMoroesY, 32.0f}};
        return DungeonClearMath::RankStraightPullLanes(
            kPackX, kPackY, kTankX, kTankY, /*standDist*/ 23.7f, /*drag*/ 25.0f,
            /*minDrag*/ 12.5f, keep, true, kBoxMinX, kBoxMaxX, kBoxMinY, kBoxMaxY);
    }
}

TEST(DungeonClearStraightPullTest, PointSegmentDistance)
{
    using DungeonClearMath::PointSegmentDist2d;
    EXPECT_NEAR(PointSegmentDist2d(0, 5, -10, 0, 10, 0), 5.0f, 1e-4f);
    EXPECT_NEAR(PointSegmentDist2d(13, 4, -10, 0, 10, 0), 5.0f, 1e-4f);  // past an end
    EXPECT_NEAR(PointSegmentDist2d(3, 4, 0, 0, 0, 0), 5.0f, 1e-4f);      // degenerate
}

TEST(DungeonClearStraightPullTest, MoroesWestPackPullsStraightSouthNotAcrossTheDais)
{
    auto const lanes = MoroesLanes();
    ASSERT_FALSE(lanes.empty());
    auto const& best = lanes.front();

    // Pulled back away from the dais (south), not east across its front.
    EXPECT_LT(std::sin(best.bearing), -0.7f);
    auto dist = [](float x, float y) { return std::hypot(x - kMoroesX, y - kMoroesY); };
    EXPECT_GT(dist(best.standX, best.standY), 40.0f);
    EXPECT_GT(dist(best.campX, best.campY), 50.0f);

    // The lane this run actually used: tag (-10975.5,-1911.7), camp (-10954.1,-1924.5).
    float const oldMargin = DungeonClearMath::PointSegmentDist2d(
                                kMoroesX, kMoroesY, -10975.5f, -1911.7f, -10954.1f,
                                -1924.5f) - 32.0f;
    EXPECT_LT(oldMargin, 3.0f);
    EXPECT_GT(best.margin, oldMargin + 8.0f);
}

TEST(DungeonClearStraightPullTest, EveryCampIsInsideTheBox)
{
    for (auto const& l : MoroesLanes())
    {
        EXPECT_GE(l.campX, kBoxMinX);
        EXPECT_LE(l.campX, kBoxMaxX);
        EXPECT_GE(l.campY, kBoxMinY);
        EXPECT_LE(l.campY, kBoxMaxY);
    }
}

TEST(DungeonClearStraightPullTest, BearingWithNoInBoxCampIsDropped)
{
    // Box is a 10yd square far east of the pack: only bearings pointing into it
    // can land a camp there.
    std::vector<DungeonClearMath::LaneKeepAway> const none;
    auto const lanes = DungeonClearMath::RankStraightPullLanes(
        0, 0, 0, 0, 20.0f, 25.0f, 12.5f, none, true, 35.0f, 45.0f, -5.0f, 5.0f);
    ASSERT_FALSE(lanes.empty());
    for (auto const& l : lanes)
        EXPECT_GT(std::cos(l.bearing), 0.95f);
}

TEST(DungeonClearStraightPullTest, OpenRoomTiesBreakTowardTheTank)
{
    std::vector<DungeonClearMath::LaneKeepAway> const none;
    auto const lanes = DungeonClearMath::RankStraightPullLanes(
        0, 0, 0, -50.0f, 20.0f, 25.0f, 12.5f, none, false, 0, 0, 0, 0);
    ASSERT_EQ(lanes.size(), 24u);
    EXPECT_NEAR(lanes.front().standX, 0.0f, 1e-3f);
    EXPECT_NEAR(lanes.front().standY, -20.0f, 1e-3f);
}

TEST(DungeonClearStraightPullTest, WalkToTheStandSpotCountsAgainstTheMargin)
{
    // tr-20260927-210901-8: the lane cleared every pack, and the tank woke a Ghostly
    // Steward on its way OUT to the stand spot. Pack at the origin, tank 40yd east;
    // a bystander stands north of the straight walk to the west stand spot.
    std::vector<DungeonClearMath::LaneKeepAway> const keep = {{20.0f, 20.0f, 25.0f}};
    auto const lanes = DungeonClearMath::RankStraightPullLanes(
        0, 0, 40.0f, 0, 20.0f, 25.0f, 12.5f, keep, false, 0, 0, 0, 0);
    ASSERT_FALSE(lanes.empty());
    for (auto const& l : lanes)
    {
        float const walk = DungeonClearMath::PointSegmentDist2d(20.0f, 20.0f, 40.0f, 0,
                                                                l.standX, l.standY);
        EXPECT_LE(l.margin, walk - 25.0f + 1e-3f);
    }
}

TEST(DungeonClearStraightPullTest, AKeepAwayTheTankStandsInDoesNotSinkEveryWalk)
{
    // The tank is already inside the disc: every walk starts in it, so the walk term
    // skips it and the lanes that lead AWAY keep their margin.
    std::vector<DungeonClearMath::LaneKeepAway> const keep = {{40.0f, 0.0f, 25.0f}};
    auto const lanes = DungeonClearMath::RankStraightPullLanes(
        0, 0, 30.0f, 0, 20.0f, 25.0f, 12.5f, keep, false, 0, 0, 0, 0);
    ASSERT_FALSE(lanes.empty());
    EXPECT_GT(lanes.front().margin, 0.0f);
    EXPECT_LT(std::cos(lanes.front().bearing), -0.7f);  // pulled west, away from it
}

TEST(DungeonClearRoomTrashTest, NearestUnrefusedPackGoesFirst)
{
    std::vector<float> const dist = {10.0f, 20.0f, 30.0f};
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {false, false, false}), 0);
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {true, false, false}), 1);
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {true, true, false}), 2);
}

TEST(DungeonClearRoomTrashTest, AllRefusedFallsBackToTheNearest)
{
    std::vector<float> const dist = {25.0f, 12.0f, 30.0f};
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {true, true, true}), 1);
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex({}, {}), -1);
    // A short refusal vector reads as "not refused" past its end.
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {true}), 1);
}

// sk-20261019-003633: the throne-side floor (Ambassador 29.9yd from the Emperor)
// sorted ahead of both balconies, and every wake there met the full balconies. A
// boss-side ring puts it behind everything, ledges included; outside the ring the
// off-level order is unchanged.
TEST(DungeonClearRoomTrashTest, BossSidePacksGoLast)
{
    using DungeonClearMath::RoomTrashPickDistance;
    float const corner = RoomTrashPickDistance(10.0f, 0.0f, 5.0f, true, 29.9f, 36.0f);
    float const balcony = RoomTrashPickDistance(45.0f, 6.9f, 5.0f, true, 39.6f, 36.0f);
    float const floor = RoomTrashPickDistance(50.0f, 0.0f, 5.0f, true, 60.0f, 36.0f);
    EXPECT_LT(floor, balcony);
    EXPECT_LT(balcony, corner);
    EXPECT_FLOAT_EQ(floor, 50.0f);
    // Nearest-first still holds inside the ring.
    EXPECT_LT(corner, RoomTrashPickDistance(20.0f, 0.0f, 5.0f, true, 25.0f, 36.0f));
    // No ring, or no live boss: the old order exactly.
    EXPECT_FLOAT_EQ(RoomTrashPickDistance(10.0f, 0.0f, 5.0f, true, 29.9f, 0.0f), 10.0f);
    EXPECT_FLOAT_EQ(RoomTrashPickDistance(10.0f, 0.0f, 5.0f, true, -1.0f, 36.0f), 10.0f);
}

// sk-20261019-105358: every balcony pack refused "no clean lane", so the
// unrefused throne-side ring became the only pick and its walk woke the Emperor
// with both balconies up. The ring now waits for everything outside it, refused
// or not.
TEST(DungeonClearRoomTrashTest, TheRingWaitsForRefusedPacksOutsideIt)
{
    // 0: balcony, refused. 1: ring Senator 28.6yd off him, unrefused. 2: ring, nearer.
    std::vector<float> const dist = {2045.0f, 2030.0f, 2020.0f};
    std::vector<bool> const ring = {false, true, true};
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {true, false, false}, ring), 0);
    // An unrefused pack outside the ring still beats a refused one.
    std::vector<float> const four = {45.0f, 60.0f, 2030.0f, 2020.0f};
    std::vector<bool> const ring4 = {false, false, true, true};
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(four, {false, true, false, false}, ring4), 0);
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(four, {true, false, false, false}, ring4), 1);
    // Only the ring left: unrefused before refused, nearest first.
    std::vector<float> const left = {2030.0f, 2020.0f, 2010.0f};
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(left, {false, false, true}, {true, true, true}), 1);
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(left, {true, true, true}, {true, true, true}), 2);
    // No ring vector: the old order exactly.
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {true, false, false}), 2);
}

// sk-20261019-105358 (-61/-57/-47): the planted Doomforge Dragoon 26.4yd from the
// Emperor (sphere 28.3) woke and nothing could reach it. A caster stopping at
// spell range on the camp side ends well outside the sphere; melee and a leap end
// on it, inside.
TEST(DungeonClearRoomTrashTest, TheChaseBanRulesOnWhereTheChaseEnds)
{
    using DungeonClearMath::ChaseEndToBoss;
    using DungeonClearMath::ChaseWakesSleepingBoss;
    // Boss at the origin, the Dragoon 26.4yd east, a caster 48yd past it.
    float const caster = ChaseEndToBoss(74.4f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 30.0f);
    EXPECT_NEAR(caster, 56.4f, 0.01f);
    EXPECT_FALSE(ChaseWakesSleepingBoss(caster, 28.3f, false));
    float const melee = ChaseEndToBoss(74.4f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(melee, 26.4f, 0.01f);
    EXPECT_TRUE(ChaseWakesSleepingBoss(melee, 28.3f, false));
    // Already inside range: the mover stays where it is.
    EXPECT_NEAR(ChaseEndToBoss(40.0f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 30.0f), 40.0f, 0.01f);
    // Coming at it from beyond the boss, spell range ends 3.6yd from him: banned.
    float const fromBehind = ChaseEndToBoss(-10.0f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 30.0f);
    EXPECT_NEAR(fromBehind, 3.6f, 0.01f);
    EXPECT_TRUE(ChaseWakesSleepingBoss(fromBehind, 28.3f, false));
}

// sk-20261019-105358 (-70/-72/-73): an Angerforge summon inside the geometry held
// every member on refused routes until the stall watchdog. Five seconds of
// refusals on the same holder give it up; a move or a new holder restarts.
TEST(DungeonClearAssistTest, ARefusedHolderIsGivenUp)
{
    using DungeonClearMath::AssistCloseGivenUp;
    uint32_t since = 0;
    EXPECT_FALSE(AssistCloseGivenUp(false, true, 1000, 5000, since));
    EXPECT_EQ(since, 1000u);
    EXPECT_FALSE(AssistCloseGivenUp(false, true, 5999, 5000, since));
    EXPECT_TRUE(AssistCloseGivenUp(false, true, 6000, 5000, since));
    // A move that goes restarts the clock.
    EXPECT_FALSE(AssistCloseGivenUp(true, true, 6100, 5000, since));
    EXPECT_EQ(since, 0u);
    EXPECT_FALSE(AssistCloseGivenUp(false, true, 6200, 5000, since));
    EXPECT_FALSE(AssistCloseGivenUp(false, true, 11199, 5000, since));
    // A new holder starts its own refusals now.
    EXPECT_FALSE(AssistCloseGivenUp(false, false, 11200, 5000, since));
    EXPECT_EQ(since, 11200u);
    EXPECT_TRUE(AssistCloseGivenUp(false, true, 16200, 5000, since));
    // getMSTime 0 still starts a clock.
    since = 0;
    EXPECT_FALSE(AssistCloseGivenUp(false, true, 0, 5000, since));
    EXPECT_NE(since, 0u);
}

// tr-20261019-182249-1: the walk-in on a Doomforge Arcanasmith stood still at
// (781.7,31.9) for 100s. Eight seconds without closing give it up; a walk that
// closes, or a gap in the ticks, restarts the measure.
TEST(DungeonClearEngageTrashTest, AWalkInThatGainsNoGroundIsGivenUp)
{
    using DungeonClearMath::EngageWalkGivenUp;
    DungeonClearMath::WalkInWatch w;
    EXPECT_FALSE(EngageWalkGivenUp(w, 40.0f, 0.5f, 1000, 8000, 2000));
    // Frozen: the same 40yd tick after tick.
    uint32_t t = 1000;
    for (; t < 8900; t += 200)
        EXPECT_FALSE(EngageWalkGivenUp(w, 40.0f, 0.5f, t, 8000, 2000));
    EXPECT_TRUE(EngageWalkGivenUp(w, 40.0f, 0.5f, 9000, 8000, 2000));

    // A walk that closes never trips it.
    DungeonClearMath::WalkInWatch walking;
    float d = 60.0f;
    for (uint32_t ms = 0; ms < 20000; ms += 200, d -= 1.0f)
        EXPECT_FALSE(EngageWalkGivenUp(walking, d, 0.5f, ms + 1, 8000, 2000));

    // Losing the tick for longer than the gap restarts the window.
    DungeonClearMath::WalkInWatch held;
    EXPECT_FALSE(EngageWalkGivenUp(held, 30.0f, 0.5f, 1000, 8000, 2000));
    EXPECT_FALSE(EngageWalkGivenUp(held, 30.0f, 0.5f, 7000, 8000, 2000));
    EXPECT_FALSE(EngageWalkGivenUp(held, 30.0f, 0.5f, 10000, 8000, 2000));
}

// tr-20261019-182249-1: the stall outlived its own pull — the fallback went on
// pulling every reachable mob and Advance never got the tick that clears it.
TEST(DungeonClearStallTest, TheStallEndsWithItsPull)
{
    EXPECT_TRUE(DungeonClearMath::StallPullIsDone(false, false));
    // Still fighting it, or anything else: not yet.
    EXPECT_FALSE(DungeonClearMath::StallPullIsDone(true, false));
    EXPECT_FALSE(DungeonClearMath::StallPullIsDone(false, true));
    EXPECT_FALSE(DungeonClearMath::StallPullIsDone(true, true));
}

TEST(DungeonClearStallTest, AnArbiterDeferralIsNotStuck)
{
    // A refused move counts toward DC_STUCK_LIMIT...
    EXPECT_TRUE(DungeonClearMath::MoveToFallbackCountsStuck(false, false));
    // ...unless the arbiter only deferred it behind a move that still holds.
    EXPECT_FALSE(DungeonClearMath::MoveToFallbackCountsStuck(false, true));
    // An issued move is never stuck.
    EXPECT_FALSE(DungeonClearMath::MoveToFallbackCountsStuck(true, false));
    EXPECT_FALSE(DungeonClearMath::MoveToFallbackCountsStuck(true, true));
}

TEST(DungeonClearRoomTrashTest, AKillDropsTheRefusals)
{
    EXPECT_TRUE(DungeonClearMath::ShouldDropLaneRefusals(23u, 24u));
    EXPECT_FALSE(DungeonClearMath::ShouldDropLaneRefusals(24u, 24u));
    // A late spawn or a rescan that grew the list is not a kill.
    EXPECT_FALSE(DungeonClearMath::ShouldDropLaneRefusals(25u, 24u));
}

TEST(DungeonClearMathTest, RegroupAnchorRejectsTheStrandedFarHolder)
{
    // tr-20260927-103044-10: a Ghostly Philanthropist stranded at the Opera stage
    // corridor held the raid in combat from 141-145yd. It must not anchor the regroup.
    EXPECT_FALSE(DungeonClearMath::IsRegroupAnchorCandidate(141.8f, 100.0f, true));
    // Inside the radius but on a floor the bot cannot path to is no anchor either.
    EXPECT_FALSE(DungeonClearMath::IsRegroupAnchorCandidate(40.0f, 100.0f, false));
}

TEST(DungeonClearMathTest, RegroupAnchorKeepsAFightablePack)
{
    EXPECT_TRUE(DungeonClearMath::IsRegroupAnchorCandidate(25.0f, 100.0f, true));
    EXPECT_TRUE(DungeonClearMath::IsRegroupAnchorCandidate(100.0f, 100.0f, true));
}

// Emperor's throne room: a balcony pack straight overhead is "nearest" on the 2D
// line and a ramp walk away, so off-level packs go after every floor pack — and
// only in a room that asks for it.
TEST(DungeonClearRoomTrashTest, OffLevelPacksGoLastWhenTheRoomAsks)
{
    float const overhead = DungeonClearMath::RoomTrashPickDistance(8.0f, 6.8f, 5.0f, true);
    float const farFloor = DungeonClearMath::RoomTrashPickDistance(60.0f, 0.4f, 5.0f, true);
    EXPECT_GT(overhead, farFloor);
    EXPECT_FLOAT_EQ(farFloor, 60.0f);
    EXPECT_FLOAT_EQ(DungeonClearMath::RoomTrashPickDistance(8.0f, -6.8f, 5.0f, true),
                    overhead);
    // Among the ledge packs the nearest still goes first.
    EXPECT_LT(overhead, DungeonClearMath::RoomTrashPickDistance(30.0f, 6.8f, 5.0f, true));
    // Every other room ranks on the plain 2D distance.
    EXPECT_FLOAT_EQ(DungeonClearMath::RoomTrashPickDistance(8.0f, 6.8f, 5.0f, false), 8.0f);

    std::vector<float> const dist = {overhead, farFloor};
    EXPECT_EQ(DungeonClearMath::PickRoomTrashIndex(dist, {false, false}), 1);
}

// tr-20261018-230400-6: the camp sat at the throne-room door, 105yd from the tank at
// the Emperor's standoff. No member can be near both, so the camp anchor is stale.
TEST(DungeonClearSpreadGateTest, ACampTheTankLeftBehindIsStale)
{
    float const maxTankGap = 25.0f + 35.0f;
    EXPECT_TRUE(DungeonClearMath::CampAnchorIsStale(104.7f, maxTankGap));
    // A camp a legitimate standoff behind the tank anchors as before.
    EXPECT_FALSE(DungeonClearMath::CampAnchorIsStale(35.0f, maxTankGap));
    EXPECT_FALSE(DungeonClearMath::CampAnchorIsStale(maxTankGap, maxTankGap));
    // No backstop, nothing to call stale.
    EXPECT_FALSE(DungeonClearMath::CampAnchorIsStale(500.0f, 0.0f));
}

// tr-20261018-230400-9: the fight carried the tank 22yd from a sleeping Emperor
// (sphere 28.3) and woke the room.
TEST(DungeonClearRoomTrashTest, NoChaseIntoASleepingBossSphere)
{
    EXPECT_TRUE(DungeonClearMath::ChaseWakesSleepingBoss(21.0f, 28.3f, false));
    EXPECT_FALSE(DungeonClearMath::ChaseWakesSleepingBoss(35.0f, 28.3f, false));
    // Once he is up, the room clear is over and the chase is free.
    EXPECT_FALSE(DungeonClearMath::ChaseWakesSleepingBoss(21.0f, 28.3f, true));
}

// tr-20261019-183728-12: the tank on the throne-room floor (z -92.7) rounded a
// pack on the SW balcony (z -85.06) on its 2D commit ring and bounced between two
// spots beside itself for 6m39s instead of taking the ramp.
TEST(DungeonClearRoomTrashTest, TheStandWalkRoundsAPackOnlyOnItsOwnFloor)
{
    EXPECT_FALSE(DungeonClearMath::StandWalkRoundsPack(-92.7f, -85.06f, 5.0f));
    EXPECT_FALSE(DungeonClearMath::StandWalkRoundsPack(-85.06f, -92.7f, 5.0f));
    // Same floor, or a gentle slope across it: round the pack as before.
    EXPECT_TRUE(DungeonClearMath::StandWalkRoundsPack(-92.6f, -91.9f, 5.0f));
    EXPECT_TRUE(DungeonClearMath::StandWalkRoundsPack(-90.0f, -85.0f, 5.0f));
}

// Blizzard (Shade of Aran) is a template-unselectable static spawn with no TRIGGER
// flag: it outlives Aran and held three Karazhan members flagged for 199s, read as a
// LEGIT holder (tr-20261022-104155-559).
TEST(DungeonClearStuckCombatTest, UnflaggedScriptHelpersAreUnresolvableByEntry)
{
    EXPECT_TRUE(DungeonClearMath::IsUnresolvableCombatHolder(true, false, 17161));
    // Only creatures, and only listed entries.
    EXPECT_FALSE(DungeonClearMath::IsUnresolvableCombatHolder(false, false, 17161));
    EXPECT_FALSE(DungeonClearMath::IsUnresolvableCombatHolder(true, false, 16524));  // Aran himself
}

// The leader-assist groupmate fallback's hysteresis. Unlatched it is the plain
// co-located threshold; latched on the member it just yielded beside, it holds out
// to the release band, so a follower trailing the tank at 5-6yd can no longer flip
// the rung every tick (tr-20261022-104155-559 / -193320-1035).
TEST(DungeonClearLeaderAssistTest, GroupmateFallbackHasHysteresis)
{
    float const in = 5.0f, out = 15.0f;
    EXPECT_TRUE(DungeonClearMath::LeaderAssistFallbackYields(4.2f, false, in, out));
    EXPECT_FALSE(DungeonClearMath::LeaderAssistFallbackYields(5.6f, false, in, out));
    EXPECT_TRUE(DungeonClearMath::LeaderAssistFallbackYields(5.6f, true, in, out));
    EXPECT_TRUE(DungeonClearMath::LeaderAssistFallbackYields(15.0f, true, in, out));
    // A real round-the-corner distance walks again.
    EXPECT_FALSE(DungeonClearMath::LeaderAssistFallbackYields(15.1f, true, in, out));
}
