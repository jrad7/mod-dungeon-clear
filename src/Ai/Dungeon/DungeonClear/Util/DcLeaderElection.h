/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_LEADER_ELECTION_H
#define _DC_LEADER_ELECTION_H

#include <cstddef>
#include <cstdint>
#include <vector>

// The leader-tank election rule, apart from the group walk that feeds it, so it
// can be unit tested. DcLeaderSignal::FindLeaderTank builds the candidates.
namespace DcLeaderElection
{
    // DungeonClear.LeaderElection
    enum class Mode : std::uint32_t
    {
        // Party: lowest GUID. Raid: the Main Tank flag, else highest gear score.
        DungeonClear = 0,
        // mod-playerbots' main tank (PlayerbotAI::GetMainTankGuid): the Main
        // Tank flag, else the first tank in group order.
        Playerbots = 1,
    };

    // An alive tank bot on the reference's map, in group order.
    struct Candidate
    {
        std::uint64_t guid = 0;
        bool runOwner = false;        // its own DcRunState is enabled
        bool mainTank = false;        // carries MEMBER_FLAG_MAINTANK (raid)
        std::uint32_t gearScore = 0;  // read only in DungeonClear mode in a raid
    };

    // True when Elect reads Candidate::gearScore, so the caller can skip
    // computing it.
    inline bool NeedsGearScore(Mode mode, bool isRaid)
    {
        return mode == Mode::DungeonClear && isRaid;
    }

    // Index of the leader in `candidates`, or -1 when there is none. A tank
    // that holds a run wins in every mode: the run lives on that bot alone, and
    // a new election mid-run would hand the lead to a tank with no run.
    inline int Elect(std::vector<Candidate> const& candidates, bool isRaid, Mode mode)
    {
        for (std::size_t i = 0; i < candidates.size(); ++i)
            if (candidates[i].runOwner)
                return static_cast<int>(i);
        if (candidates.empty())
            return -1;

        if (isRaid)
            for (std::size_t i = 0; i < candidates.size(); ++i)
                if (candidates[i].mainTank)
                    return static_cast<int>(i);

        if (mode == Mode::Playerbots)
            return 0;

        int best = 0;
        for (std::size_t i = 1; i < candidates.size(); ++i)
        {
            Candidate const& c = candidates[i];
            Candidate const& b = candidates[best];
            if (isRaid && c.gearScore != b.gearScore)
            {
                if (c.gearScore > b.gearScore)
                    best = static_cast<int>(i);
            }
            else if (c.guid < b.guid)
                best = static_cast<int>(i);
        }
        return best;
    }
}

#endif
