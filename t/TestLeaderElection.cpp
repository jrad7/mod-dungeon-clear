/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DcLeaderElection.h"

#include <vector>

using DcLeaderElection::Candidate;
using DcLeaderElection::Elect;

namespace
{
    Candidate Tank(bool mainTank = false, bool runOwner = false)
    {
        Candidate c;
        c.mainTank = mainTank;
        c.runOwner = runOwner;
        return c;
    }
}

TEST(DcLeaderElectionTest, NoCandidates)
{
    EXPECT_EQ(Elect({}), -1);
}

// No Main Tank flag and no candidate named by GetMainTankGuid's walk (a real
// player or an off-map tank is first in the group): the first candidate leads.
TEST(DcLeaderElectionTest, FirstInGroupOrderWithoutAMainTank)
{
    std::vector<Candidate> c{Tank(), Tank(), Tank()};
    EXPECT_EQ(Elect(c), 0);
}

// GetMainTankGuid named this tank: the Main Tank flag, or the first living tank
// in the group. It leads wherever it stands in the candidate list.
TEST(DcLeaderElectionTest, PlayerbotsMainTankLeads)
{
    std::vector<Candidate> c{Tank(), Tank(), Tank(true)};
    EXPECT_EQ(Elect(c), 2);
}

// The run lives on the tank `dc on` reached. A Main Tank flagged after the
// start, or the first tank in the group, does not take it away.
TEST(DcLeaderElectionTest, RunOwnerKeepsTheLead)
{
    std::vector<Candidate> c{Tank(true), Tank(), Tank(false, true)};
    EXPECT_EQ(Elect(c), 2);
}

TEST(DcLeaderElectionTest, RunOwnerAndMainTankAreTheSameTank)
{
    std::vector<Candidate> c{Tank(), Tank(true, true)};
    EXPECT_EQ(Elect(c), 1);
}

TEST(DcLeaderElectionTest, SingleTankLeads)
{
    std::vector<Candidate> c{Tank()};
    EXPECT_EQ(Elect(c), 0);
}
