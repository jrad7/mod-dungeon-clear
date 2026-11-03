/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DcLeaderElection.h"

#include <vector>

using DcLeaderElection::Candidate;
using DcLeaderElection::Elect;
using DcLeaderElection::Mode;

namespace
{
    Candidate Tank(std::uint64_t guid, std::uint32_t gearScore = 0, bool mainTank = false, bool runOwner = false)
    {
        Candidate c;
        c.guid = guid;
        c.gearScore = gearScore;
        c.mainTank = mainTank;
        c.runOwner = runOwner;
        return c;
    }
}

TEST(DcLeaderElectionTest, NoCandidates)
{
    EXPECT_EQ(Elect({}, false, Mode::DungeonClear), -1);
    EXPECT_EQ(Elect({}, true, Mode::Playerbots), -1);
}

TEST(DcLeaderElectionTest, PartyTakesLowestGuid)
{
    std::vector<Candidate> c{Tank(9), Tank(4), Tank(7)};
    EXPECT_EQ(Elect(c, false, Mode::DungeonClear), 1);
}

TEST(DcLeaderElectionTest, RaidTakesBestGearThenLowestGuid)
{
    std::vector<Candidate> c{Tank(3, 64), Tank(5, 66), Tank(2, 66)};
    EXPECT_EQ(Elect(c, true, Mode::DungeonClear), 2);
}

TEST(DcLeaderElectionTest, RaidMainTankBeatsGear)
{
    std::vector<Candidate> c{Tank(3, 70), Tank(5, 60, true)};
    EXPECT_EQ(Elect(c, true, Mode::DungeonClear), 1);
}

TEST(DcLeaderElectionTest, PartyIgnoresMainTankFlag)
{
    std::vector<Candidate> c{Tank(3), Tank(5, 0, true)};
    EXPECT_EQ(Elect(c, false, Mode::DungeonClear), 0);
}

TEST(DcLeaderElectionTest, PlayerbotsTakesFirstInGroupOrder)
{
    std::vector<Candidate> c{Tank(9, 60), Tank(4, 70), Tank(7, 80)};
    EXPECT_EQ(Elect(c, true, Mode::Playerbots), 0);
    EXPECT_EQ(Elect(c, false, Mode::Playerbots), 0);
}

TEST(DcLeaderElectionTest, PlayerbotsTakesMainTankInRaid)
{
    std::vector<Candidate> c{Tank(9), Tank(4), Tank(7, 0, true)};
    EXPECT_EQ(Elect(c, true, Mode::Playerbots), 2);
}

// The run lives on the tank `dc on` reached. Out-geared, out-ranked or with a
// Main Tank flagged after the start, it still leads.
TEST(DcLeaderElectionTest, RunOwnerKeepsTheLead)
{
    std::vector<Candidate> c{Tank(3, 90, true), Tank(5, 95), Tank(8, 60, false, true)};
    EXPECT_EQ(Elect(c, true, Mode::DungeonClear), 2);
    EXPECT_EQ(Elect(c, true, Mode::Playerbots), 2);
    EXPECT_EQ(Elect(c, false, Mode::DungeonClear), 2);
}

TEST(DcLeaderElectionTest, OutOfRangeConfigIsTheDefaultRule)
{
    EXPECT_EQ(DcLeaderElection::ModeFromConfig(0), Mode::DungeonClear);
    EXPECT_EQ(DcLeaderElection::ModeFromConfig(1), Mode::Playerbots);
    EXPECT_EQ(DcLeaderElection::ModeFromConfig(2), Mode::DungeonClear);
    EXPECT_EQ(DcLeaderElection::ModeFromConfig(0xFFFFFFFFu), Mode::DungeonClear);
}

TEST(DcLeaderElectionTest, GearScoreOnlyNeededForDcRaids)
{
    EXPECT_TRUE(DcLeaderElection::NeedsGearScore(Mode::DungeonClear, true));
    EXPECT_FALSE(DcLeaderElection::NeedsGearScore(Mode::DungeonClear, false));
    EXPECT_FALSE(DcLeaderElection::NeedsGearScore(Mode::Playerbots, true));
}
