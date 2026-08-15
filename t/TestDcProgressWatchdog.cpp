/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Unit tests for the shared progress watchdog (nav review F11). Pins the two
// progress signals the route-glide / door-walk-in wedge detectors and the swim
// leg were consolidated onto, at the exact thresholds those sites use, so the
// consolidation stays behavior-preserving.

#include "gtest/gtest.h"
#include "DcProgressWatchdog.h"

namespace
{
    constexpr float MIN_MOVE  = 0.5f;  // DC_STUCK_DISPLACEMENT
    constexpr float MIN_CLOSE = 0.5f;  // swim closing-distance epsilon
}

// ---- TickDisplacement (route glide / door walk-in wedge) -----------------

TEST(DcProgressWatchdog, DisplacementCountsMovingButWedgedTicks)
{
    DcProgressWatchdog w;
    // Moving yet barely displaced == a wedge tick; the counter accumulates.
    EXPECT_EQ(w.TickDisplacement(true, 0.1f, MIN_MOVE), 1u);
    EXPECT_EQ(w.TickDisplacement(true, 0.0f, MIN_MOVE), 2u);
    EXPECT_EQ(w.TickDisplacement(true, 0.49f, MIN_MOVE), 3u);
}

TEST(DcProgressWatchdog, DisplacementRealMovementResets)
{
    DcProgressWatchdog w;
    w.TickDisplacement(true, 0.1f, MIN_MOVE);
    w.TickDisplacement(true, 0.1f, MIN_MOVE);
    ASSERT_EQ(w.stuckTicks, 2u);
    // A tick displaced at/over the threshold is real progress -> reset.
    EXPECT_EQ(w.TickDisplacement(true, MIN_MOVE, MIN_MOVE), 0u);
}

TEST(DcProgressWatchdog, DisplacementNotMovingResets)
{
    DcProgressWatchdog w;
    w.TickDisplacement(true, 0.0f, MIN_MOVE);
    w.TickDisplacement(true, 0.0f, MIN_MOVE);
    ASSERT_EQ(w.stuckTicks, 2u);
    // isMoving()==false (a spline micro-stop) is NOT a wedge -> reset. This is
    // the door-walk-in stutter fix and matches the original posStuck.
    EXPECT_EQ(w.TickDisplacement(false, 0.0f, MIN_MOVE), 0u);
}

TEST(DcProgressWatchdog, DisplacementFirstUnsampledTickIsNotAWedge)
{
    DcProgressWatchdog w;
    // The callers pass moving=false on the first, un-baselined tick (the old
    // (0,0,0) lastPos sentinel) so it can never read as a wedge.
    EXPECT_EQ(w.TickDisplacement(false, 0.0f, MIN_MOVE), 0u);
}

// ---- TickClosing (swim leg) ----------------------------------------------

TEST(DcProgressWatchdog, ClosingFirstSampleArmsAndIsProgress)
{
    DcProgressWatchdog w;
    EXPECT_TRUE(w.TickClosing(40.0f, MIN_CLOSE, 1000u));
    EXPECT_FLOAT_EQ(w.bestDist, 40.0f);
    EXPECT_EQ(w.lastProgressMs, 1000u);
}

TEST(DcProgressWatchdog, ClosingImprovementBeyondEpsilonIsProgress)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    EXPECT_TRUE(w.TickClosing(39.0f, MIN_CLOSE, 2000u));  // 1.0 closer > 0.5
    EXPECT_FLOAT_EQ(w.bestDist, 39.0f);
    EXPECT_EQ(w.lastProgressMs, 2000u);
}

TEST(DcProgressWatchdog, ClosingTinyImprovementIsNotProgress)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    // Only 0.4yd closer (< 0.5 epsilon): no progress, best/clock unchanged so the
    // stale timer keeps running.
    EXPECT_FALSE(w.TickClosing(39.6f, MIN_CLOSE, 2000u));
    EXPECT_FLOAT_EQ(w.bestDist, 40.0f);
    EXPECT_EQ(w.lastProgressMs, 1000u);
}

TEST(DcProgressWatchdog, ClosingMovingAwayIsNotProgress)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    EXPECT_FALSE(w.TickClosing(45.0f, MIN_CLOSE, 2000u));
    EXPECT_FLOAT_EQ(w.bestDist, 40.0f);  // best is the global minimum
    EXPECT_EQ(w.lastProgressMs, 1000u);
}

TEST(DcProgressWatchdog, ClosingTracksGlobalMinimumAcrossPoints)
{
    DcProgressWatchdog w;
    // Approach the first point down to 5yd...
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    w.TickClosing(5.0f, MIN_CLOSE, 2000u);
    ASSERT_FLOAT_EQ(w.bestDist, 5.0f);
    // ...cursor advances, distance to the NEW point jumps to 12yd. That is not
    // nearer than the 5yd global best, so it is not progress (matches the
    // original leg-wide lastDistToPoint), and the stale clock keeps its stamp.
    EXPECT_FALSE(w.TickClosing(12.0f, MIN_CLOSE, 3000u));
    EXPECT_EQ(w.lastProgressMs, 2000u);
    // Swimming past the old min re-registers progress and re-stamps the clock.
    EXPECT_TRUE(w.TickClosing(4.0f, MIN_CLOSE, 4000u));
    EXPECT_EQ(w.lastProgressMs, 4000u);
}

// ---- TickClosing tick budget (pursuit / final approach) ------------------

TEST(DcProgressWatchdog, ClosingNoProgressIncrementsStuckTicks)
{
    DcProgressWatchdog w;
    ASSERT_TRUE(w.TickClosing(40.0f, MIN_CLOSE, 1000u));  // arms, stuck stays 0
    EXPECT_EQ(w.stuckTicks, 0u);
    // Not getting nearer (frozen bot, or bee-line grinding a corner) accrues the
    // tick budget the pursuit / final-approach latches read.
    EXPECT_FALSE(w.TickClosing(40.0f, MIN_CLOSE, 1100u));
    EXPECT_EQ(w.stuckTicks, 1u);
    EXPECT_FALSE(w.TickClosing(40.2f, MIN_CLOSE, 1200u));  // 0.2 < 0.5 epsilon
    EXPECT_EQ(w.stuckTicks, 2u);
}

TEST(DcProgressWatchdog, ClosingProgressResetsStuckTicks)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    w.TickClosing(40.0f, MIN_CLOSE, 1100u);
    w.TickClosing(40.0f, MIN_CLOSE, 1200u);
    ASSERT_EQ(w.stuckTicks, 2u);
    // A real gain toward the boss clears the latch.
    EXPECT_TRUE(w.TickClosing(38.0f, MIN_CLOSE, 1300u));
    EXPECT_EQ(w.stuckTicks, 0u);
}

// ---- Reset ---------------------------------------------------------------

TEST(DcProgressWatchdog, ResetClearsEverything)
{
    DcProgressWatchdog w;
    w.TickDisplacement(true, 0.0f, MIN_MOVE);
    w.TickClosing(10.0f, MIN_CLOSE, 500u);
    w.Reset();
    EXPECT_EQ(w.stuckTicks, 0u);
    EXPECT_LT(w.bestDist, 0.0f);       // unset sentinel
    EXPECT_EQ(w.lastProgressMs, 0u);
    // After Reset the next closing sample re-arms as a fresh first sample.
    EXPECT_TRUE(w.TickClosing(99.0f, MIN_CLOSE, 600u));
}
