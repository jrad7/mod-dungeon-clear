/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_LEADER_ELECTION_H
#define _DC_LEADER_ELECTION_H

#include <cstddef>
#include <vector>

// The leader-tank election rule, apart from the group walk that feeds it, so it
// can be unit tested. DcLeaderSignal::FindLeaderTank builds the candidates.
namespace DcLeaderElection
{
    // An alive tank bot on the reference's map, in group order.
    struct Candidate
    {
        bool runOwner = false;  // its own DcRunState is enabled
        bool mainTank = false;  // it is the tank PlayerbotAI::GetMainTankGuid names
    };

    // Index of the leader in `candidates`, or -1 when there is none.
    //
    // The leader is mod-playerbots' main tank, so the playerbots raid strategies
    // and dungeon clear agree on who tanks first: the member GetMainTankGuid
    // names (the Main Tank flag, else the first living tank in group order).
    // GetMainTankGuid can name a real player, or a member on another map, which
    // dungeon clear cannot lead with; the first candidate, in the same group
    // order, leads then.
    //
    // A tank that holds a run wins over both: the run lives on the bot `dc on`
    // reached, and a new election mid-run would hand the lead to a tank with no
    // run, and nobody would drive.
    inline int Elect(std::vector<Candidate> const& candidates)
    {
        for (std::size_t i = 0; i < candidates.size(); ++i)
            if (candidates[i].runOwner)
                return static_cast<int>(i);
        for (std::size_t i = 0; i < candidates.size(); ++i)
            if (candidates[i].mainTank)
                return static_cast<int>(i);
        return candidates.empty() ? -1 : 0;
    }
}

#endif
