/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Authored-data lints for the classic raid patches that summon a boss with no
// creature spawn row: Zul'Gurub's Arlokk (the Gong of Bethekk) and Temple of
// Ahn'Qiraj's Ouro (his mound), plus the BWL panel placement of the Razorgore
// orb run. No world state: these pin the shapes the runtime relies on.

#include "gtest/gtest.h"

#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/BossPullbackRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDifficulty.h"

namespace
{
    DungeonBossInfo Static(uint32 entry, uint32 index, uint32 mapId)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = index;
        b.mapId = mapId;
        return b;
    }

    DungeonBossInfo const* FindEntry(std::vector<DungeonBossInfo> const& v, uint32 entry)
    {
        for (DungeonBossInfo const& b : v)
            if (b.entry == entry)
                return &b;
        return nullptr;
    }
}

TEST(DcClassicRaidsTest, ZulGurubAddsArlokkOnTheGong)
{
    // The seven ZG bosses that do have spawn rows, at their DBC order.
    std::vector<DungeonBossInfo> base = {
        Static(14507, 0, 309), Static(14517, 1, 309), Static(14510, 2, 309),
        Static(11382, 3, 309), Static(14509, 5, 309), Static(11380, 8, 309),
        Static(14834, 9, 309),
    };
    auto const out = BossRosterRegistry::Apply(309, DcDiffKey::Raid(0), base);
    ASSERT_EQ(out.size(), 8u);

    DungeonBossInfo const* arlokk = FindEntry(out, 14515);
    ASSERT_NE(arlokk, nullptr);
    EXPECT_EQ(arlokk->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(arlokk->doneBossStateIndex, 3);  // DATA_ARLOKK
    EXPECT_GE(arlokk->encounterIndex, 32u);    // never aliases a DBC bit
    EXPECT_EQ(arlokk->orderOverride, 7);
    EXPECT_NEAR(arlokk->x, -11569.2f, 0.1f);   // on the gong
}

TEST(DcClassicRaidsTest, ZulGurubForcesJeklikOffHerLedge)
{
    // She waits passive on a ledge the navmesh can't reach, so the row has to
    // force her.
    BossPullback const* row = BossPullbackRegistry::Find(309, 14517);
    ASSERT_NE(row, nullptr);
    EXPECT_GT(row->forceAggroRange, 0.0f);
    EXPECT_FALSE(row->summonWhenStuckBelow);

    // Her roster anchor has to be the same camp, or boss navigation walks the
    // party under the ledge and the pull never arms (it gates on the anchor).
    std::vector<DungeonBossInfo> base = {
        Static(14507, 0, 309), Static(14517, 1, 309), Static(14510, 2, 309),
        Static(11382, 3, 309), Static(14509, 5, 309), Static(11380, 8, 309),
        Static(14834, 9, 309),
    };
    auto const out = BossRosterRegistry::Apply(309, DcDiffKey::Raid(0), base);
    DungeonBossInfo const* jeklik = FindEntry(out, 14517);
    ASSERT_NE(jeklik, nullptr);
    EXPECT_EQ(jeklik->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(jeklik->encounterIndex, 1u) << "keeps her own kill-bit";
    EXPECT_FLOAT_EQ(jeklik->x, row->campX);
    EXPECT_FLOAT_EQ(jeklik->y, row->campY);
    EXPECT_FLOAT_EQ(jeklik->z, row->campZ);
}

TEST(DcClassicRaidsTest, ZulGurubGongEventGatesArlokk)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(309, 1);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Conditional);
    // Repeatable keeps it pending (no not-spawned stall) until she dies.
    EXPECT_TRUE(ev->repeatable);
    EXPECT_EQ(ev->panelGatesBossEntry, 14515u);
    // The pull hold covers the gong platform only, not the ~58 elites within the
    // default 80yd.
    EXPECT_FLOAT_EQ(ev->summonHoldRadius, 20.0f);
    ASSERT_EQ(ev->steps.size(), 1u);
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(ev->steps[0].goEntry, 180526u);
}

TEST(DcClassicRaidsTest, AhnQirajAddsOuroOnHisMound)
{
    std::vector<DungeonBossInfo> base = {
        Static(15263, 0, 531), Static(15544, 1, 531), Static(15516, 2, 531),
        Static(15510, 3, 531), Static(15299, 4, 531), Static(15509, 5, 531),
        Static(15275, 6, 531), Static(15727, 8, 531),
    };
    auto const out = BossRosterRegistry::Apply(531, DcDiffKey::Raid(0), base);
    ASSERT_EQ(out.size(), 9u);

    DungeonBossInfo const* ouro = FindEntry(out, 15517);
    ASSERT_NE(ouro, nullptr);
    EXPECT_EQ(ouro->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(ouro->doneBossStateIndex, 8);  // DATA_OURO
    EXPECT_GE(ouro->encounterIndex, 32u);
    EXPECT_EQ(ouro->orderOverride, 7);
    EXPECT_NEAR(ouro->x, -9173.09f, 0.1f);   // on the spawner
}

TEST(DcClassicRaidsTest, AhnQirajMoundEventGarrisonsUntilOuroIsUp)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(531, 1);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Conditional);
    EXPECT_TRUE(ev->repeatable);
    EXPECT_EQ(ev->panelGatesBossEntry, 15517u);
    EXPECT_FLOAT_EQ(ev->summonHoldRadius, 0.0f);  // default 80yd: no trash near the mound
    ASSERT_EQ(ev->steps.size(), 1u);
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_EQ(ev->steps[0].creatureEntry, 15517u);  // the spawn gate
}

TEST(DcClassicRaidsTest, RazorgoreOrbRunListsAfterGrethok)
{
    // Display placement only: it must NOT gate Razorgore, which would also stand
    // the pull pipeline down around him.
    DungeonEvent const* ev = DungeonEventRegistry::Find(469, DcBlackwingLair::EVENT_RAZORGORE_ORB);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->panelSortAfterBossEntry, DcBlackwingLair::NPC_GRETHOK_THE_CONTROLLER);
    EXPECT_EQ(ev->panelGatesBossEntry, 0u);
}
