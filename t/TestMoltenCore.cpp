/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDifficulty.h"

// Molten Core (map 409) — raid-support Plan E1 data. The eight statics derive
// from BossSpawnIndex at runtime; these tests pin the authored finale: the two
// script-summoned bosses and the Ragnaros summon event.

namespace
{
    constexpr uint32 kMap = 409;

    std::vector<DungeonBossInfo> Statics()
    {
        // Stand-ins for the eight derived kill-credit bosses (bits 0-7).
        std::vector<DungeonBossInfo> base;
        uint32 const entries[] = {12118, 11982, 12259, 12057, 12264, 12056, 12098, 11988};
        for (uint32 i = 0; i < 8; ++i)
        {
            DungeonBossInfo b;
            b.entry = entries[i];
            b.encounterIndex = i;
            b.mapId = kMap;
            base.push_back(b);
        }
        return base;
    }
}

TEST(DcMoltenCoreTest, RosterAppendsTheFinaleInOrder)
{
    auto const out =
        BossRosterRegistry::Apply(kMap, DcDiffKey::Raid(0), Statics());
    ASSERT_EQ(out.size(), 11u);

    // The eight statics clear in DBC order, except Geddon before Shazzrah...
    uint32 const order[] = {12118, 11982, 12259, 12057, 12056, 12264, 12098, 11988};
    for (uint32 i = 0; i < 8; ++i)
    {
        EXPECT_EQ(out[i].entry, order[i]) << i;
        EXPECT_EQ(BossOrderKey(out[i]), i) << i;
    }

    // ...and the swap leaves their real kill-bits alone...
    EXPECT_EQ(out[4].encounterIndex, 5u);  // Geddon
    EXPECT_EQ(out[5].encounterIndex, 4u);  // Shazzrah

    // ...then Majordomo, the summon objective, and Ragnaros.
    EXPECT_EQ(out[8].entry, 12018u);
    EXPECT_EQ(out[8].kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(out[8].doneBossStateIndex, 8);
    // Boss-state completion parks the encounterIndex out of mask range so no
    // static boss's bit can ever be misread as theirs.
    EXPECT_GE(out[8].encounterIndex, 32u);

    EXPECT_EQ(out[9].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(out[9].eventId, 1u);
    EXPECT_EQ(out[9].gateEntry, 11502u);  // a live Ragnaros satisfies it

    EXPECT_EQ(out[10].entry, 11502u);
    EXPECT_EQ(out[10].kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(out[10].doneBossStateIndex, 9);
    EXPECT_GE(out[10].encounterIndex, 32u);
}

TEST(DcMoltenCoreTest, SummonRagnarosEventShape)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(kMap, 1);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Anchored);
    EXPECT_TRUE(ev->required);

    ASSERT_EQ(ev->steps.size(), 3u);
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::Wait);

    EventStep const& gossip = ev->steps[1];
    EXPECT_EQ(gossip.kind, EventStepKind::Gossip);
    EXPECT_EQ(gossip.creatureEntry, 12018u);
    EXPECT_EQ(gossip.gossipOption, 0);
    // Re-entry with Ragnaros already summoned has no gossip Majordomo — skip.
    EXPECT_TRUE(gossip.skipIfMissing);

    EventStep const& wait = ev->steps[2];
    EXPECT_EQ(wait.kind, EventStepKind::WaitForSpawn);
    EXPECT_EQ(wait.creatureEntry, 11502u);
    EXPECT_TRUE(wait.wantAlive);
    // Must outlast the ~48s scripted intro.
    EXPECT_GE(wait.timeoutMs, 60000u);
}
