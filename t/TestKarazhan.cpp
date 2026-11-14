/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>

#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/SealedEncounterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDifficulty.h"
#include "Ai/Dungeon/DungeonClear/Util/DcKaraLibraryPit.h"
#include "TestRun/DcTestDungeonRegistry.h"

// Karazhan (map 532). The ten derived anchors come from BossSpawnIndex at runtime;
// these tests pin the authored patch against stand-ins for them.

using namespace DcKarazhan;

namespace
{
    // The auto-derived roster: one row per kill-credit spawn, DBC bit order.
    // Attumen (bit 0) is absent — his credit entry has no spawn.
    std::vector<DungeonBossInfo> Derived()
    {
        std::pair<uint32, uint32> const rows[] = {
            { NPC_MOROES, 1 },    { NPC_MAIDEN, 2 },      { NPC_BARNES, 3 },
            { NPC_CURATOR, 4 },   { NPC_TERESTIAN, 5 },   { NPC_ARAN, 6 },
            { NPC_NETHERSPITE, 7 }, { NPC_CHESS, 8 },     { NPC_PRINCE, 9 },
            { NPC_NIGHTBANE, 10 },
        };
        std::vector<DungeonBossInfo> base;
        for (auto const& [entry, bit] : rows)
        {
            DungeonBossInfo b;
            b.entry = entry;
            b.encounterIndex = bit;
            b.mapId = MAP;
            base.push_back(b);
        }
        return base;
    }

    std::vector<DungeonBossInfo> Patched()
    {
        return BossRosterRegistry::Apply(MAP, DcDiffKey::Raid(0), Derived());
    }

    DungeonBossInfo const* FindEntry(std::vector<DungeonBossInfo> const& v, uint32 entry)
    {
        auto it = std::find_if(v.begin(), v.end(),
                               [entry](DungeonBossInfo const& b) { return b.entry == entry; });
        return it == v.end() ? nullptr : &*it;
    }
}

TEST(DcKarazhanTest, RosterOpensOnMidnightAtBitZero)
{
    auto const out = Patched();
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.front().entry, NPC_MIDNIGHT);
    EXPECT_EQ(out.front().kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(out.front().encounterIndex, BIT_ATTUMEN);
    EXPECT_EQ(out.front().doneBossStateIndex, -1);  // the DBC bit is the completion
}

TEST(DcKarazhanTest, NothingIsSkippedByDesign)
{
    for (DungeonBossInfo const& b : Patched())
        EXPECT_FALSE(b.skipByDesign) << b.entry;
}

// Prince is reachable only through the chess exit door, which opens only on the
// win: the clear must order him after the chess objective, keep his real kill
// bit, and never reach him first.
TEST(DcKarazhanTest, PrinceComesStraightAfterChess)
{
    auto const out = Patched();
    DungeonBossInfo const* prince = FindEntry(out, NPC_PRINCE);
    DungeonBossInfo const* chess = FindEntry(out, BossRosterRegistry::ObjectiveEntry(OBJ_CHESS));
    ASSERT_NE(prince, nullptr);
    ASSERT_NE(chess, nullptr);
    EXPECT_EQ(prince->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(prince->encounterIndex, 9u) << "his real DBC bit is the completion";
    EXPECT_EQ(BossOrderKey(*prince), static_cast<uint32>(ORDER_PRINCE));
    // Nothing sits between the two.
    for (DungeonBossInfo const& b : out)
        EXPECT_FALSE(BossOrderKey(b) > BossOrderKey(*chess) && BossOrderKey(b) < BossOrderKey(*prince))
            << b.entry;
    EXPECT_LT(BossOrderKey(*chess), BossOrderKey(*prince));
}

TEST(DcKarazhanTest, NetherspaceIsSealed)
{
    SealedEncounterRow const* row = SealedEncounterRegistry::Find(MAP, NPC_PRINCE);
    ASSERT_NE(row, nullptr);
    // Prince and the platform are inside.
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, -10962.2f, -2018.7f, 275.4f));
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, -10930.0f, -1990.0f, 275.5f));
    // The door, the landing outside it (same height) and the hall far below are out.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11018.5f, -1967.9f, 276.6f));
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11056.0f, -1966.3f, 274.7f));
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -10962.2f, -2018.7f, 221.0f));
    // The approach arms on the landing before the door, not in the chess hall.
    EXPECT_TRUE(SealedEncounterRegistry::InApproachRange(*row, -11026.0f, -1967.0f, 275.0f,
                                                         -10962.2f, -2018.7f, 275.4f));
    EXPECT_FALSE(SealedEncounterRegistry::InApproachRange(*row, -11098.8f, -1853.6f, 221.2f,
                                                          -10962.2f, -2018.7f, 275.4f));
}

// --- Chess --------------------------------------------------------------------
// The Status Bar (22520) credits the encounter and never dies; the chess
// objective plays the game and completes on GetData(DATA_CHESS_EVENT) == DONE.

TEST(DcKarazhanTest, ChessObjectiveReplacesTheStatusBar)
{
    auto const out = Patched();
    EXPECT_EQ(FindEntry(out, NPC_CHESS), nullptr) << "never a boss anchor on a trigger that cannot die";
    DungeonBossInfo const* chess = FindEntry(out, BossRosterRegistry::ObjectiveEntry(3));
    ASSERT_NE(chess, nullptr);
    EXPECT_EQ(chess->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(chess->eventId, EV_CHESS);
    EXPECT_EQ(BossOrderKey(*chess), static_cast<uint32>(ORDER_CHESS));
    EXPECT_EQ(chess->doneInstanceDataId, static_cast<int32>(DATA_CHESS_EVENT));
    EXPECT_EQ(chess->doneInstanceDataValue, CHESS_EVENT_DONE);
    EXPECT_EQ(chess->doneBossStateIndex, -1) << "the script sets no boss state for chess";
    EXPECT_FALSE(chess->skipByDesign);
    // After Netherspite, before Prince.
    EXPECT_LT(BossOrderKey(*FindEntry(out, NPC_NETHERSPITE)), BossOrderKey(*chess));
    EXPECT_LT(BossOrderKey(*chess), BossOrderKey(*FindEntry(out, NPC_PRINCE)));
    // Anchored in the hall, off the board.
    EXPECT_NEAR(chess->z, HALL_Z, 0.5f);
    EXPECT_EQ(AnchorDoneByInstanceValues(*chess, 0, 3), DcAnchorDoneVia::InstanceData);
    EXPECT_EQ(AnchorDoneByInstanceValues(*chess, 0, 4), DcAnchorDoneVia::None) << "SPECIAL is PvP, not the win";
}

TEST(DcKarazhanTest, ChessEventShape)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, EV_CHESS);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Anchored);
    EXPECT_EQ(ev->orderIndex, static_cast<uint32>(ORDER_CHESS));
    EXPECT_TRUE(ev->persistent);
    EXPECT_TRUE(ev->required);
    ASSERT_EQ(ev->steps.size(), 4u);
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(ev->steps[0].x, HALL_X);
    EXPECT_EQ(ev->steps[1].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[1].hookId, DC_HOOK_RAID_MUSTER);
    EXPECT_EQ(ev->steps[2].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[2].hookId, HOOK_KZ_CHESS_SETUP);
    EXPECT_EQ(ev->steps[3].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[3].hookId, HOOK_KZ_CHESS_PLAY);
    // Long enough for three full games, the retries between them and the chest.
    EXPECT_GE(ev->steps[3].timeoutMs, 3u * 15u * 60u * 1000u);
    EXPECT_TRUE(ObjectiveHookRegistry::Has(HOOK_KZ_CHESS_SETUP));
    EXPECT_TRUE(ObjectiveHookRegistry::Has(HOOK_KZ_CHESS_PLAY));
}

TEST(DcKarazhanTest, BarnesAndThePerchAreNotBossAnchors)
{
    for (DungeonBossInfo const& b : Patched())
    {
        if (b.kind != DungeonAnchorKind::Boss)
            continue;
        EXPECT_NE(b.entry, NPC_BARNES);
        // Nightbane may be re-added on the terrace, but never at the perch.
        if (b.entry == NPC_NIGHTBANE)
            EXPECT_LT(b.z, 120.0f);
    }
}

TEST(DcKarazhanTest, OrderKeysAreStrictlyIncreasing)
{
    auto const out = Patched();
    for (size_t i = 1; i < out.size(); ++i)
        EXPECT_LT(BossOrderKey(out[i - 1]), BossOrderKey(out[i]))
            << out[i - 1].entry << " then " << out[i].entry;
}

TEST(DcKarazhanTest, ReorderKeepsTheRealKillBits)
{
    auto const out = Patched();
    std::pair<uint32, uint32> const bits[] = {
        { NPC_MOROES, 1 }, { NPC_MAIDEN, 2 }, { NPC_CURATOR, 4 }, { NPC_TERESTIAN, 5 },
        { NPC_ARAN, 6 },   { NPC_NETHERSPITE, 7 },
    };
    for (auto const& [entry, bit] : bits)
    {
        DungeonBossInfo const* b = FindEntry(out, entry);
        ASSERT_NE(b, nullptr) << entry;
        EXPECT_EQ(b->encounterIndex, bit) << entry;
    }
    EXPECT_EQ(BossOrderKey(*FindEntry(out, NPC_CURATOR)), static_cast<uint32>(ORDER_CURATOR));
}

// --- sealed rooms (K5) ---------------------------------------------------------
// Aran's library door shuts 15s after the pull; Netherspite's Massive Door shuts
// on the pull. Both rooms sit over and under other floors, so the rows carry a
// floor band: a member one floor down must read as OUTSIDE.

TEST(DcKarazhanTest, AranLibraryIsSealed)
{
    SealedEncounterRow const* row = SealedEncounterRegistry::Find(MAP, NPC_ARAN);
    ASSERT_NE(row, nullptr);
    // Aran and the room centre are inside.
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, -11165.5f, -1911.7f, 232.0f));
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, -11158.0f, -1920.0f, 233.0f));
    // The first room-side poly past the door is inside.
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, -11188.0f, -1889.1f, 232.5f));
    // The door, its sill and the corridor are out.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11189.5f, -1880.9f, 233.3f));
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11187.7f, -1883.3f, 232.5f));
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11196.0f, -1876.0f, 232.3f));
    // Curator's floor directly under the library is out.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11158.0f, -1920.0f, 165.8f));
    // The approach arms before the door (8yd out) but not far down the corridor.
    EXPECT_TRUE(SealedEncounterRegistry::InApproachRange(*row, -11195.9f, -1876.1f, 232.3f,
                                                         -11165.5f, -1911.7f, 232.0f));
    EXPECT_FALSE(SealedEncounterRegistry::InApproachRange(*row, -11210.0f, -1855.0f, 228.0f,
                                                          -11165.5f, -1911.7f, 232.0f));
}

TEST(DcKarazhanTest, NetherspiteWatchIsSealed)
{
    SealedEncounterRow const* row = SealedEncounterRegistry::Find(MAP, NPC_NETHERSPITE);
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, -11134.0f, -1582.8f, 278.8f));
    // The Massive Door and the corridor behind it are out.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11186.2f, -1665.1f, 281.4f));
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11192.5f, -1670.1f, 281.4f));
    // Another floor in the same footprint is out.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, -11134.0f, -1582.8f, 180.0f));
    // Armed 8yd out along the corridor.
    EXPECT_TRUE(SealedEncounterRegistry::InApproachRange(*row, -11192.5f, -1670.1f, 281.4f,
                                                         -11134.0f, -1582.8f, 278.8f));
}

TEST(DcKarazhanTest, TwoDimensionalRowsIgnoreZ)
{
    // A row with no band keeps its old 2D meaning under the 3D overload.
    SealedEncounterRow const* selin = SealedEncounterRegistry::Find(585, 24723);
    ASSERT_NE(selin, nullptr);
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*selin, 242.07f, 0.3f, -500.0f));
}

// --- Nightbane (K7) -----------------------------------------------------------
// EncounterState: NOT_STARTED 0, IN_PROGRESS 1, FAIL 2, DONE 3.

TEST(DcKarazhanTest, NightbaneUrnObjectiveOwnsTheEncounter)
{
    auto const out = Patched();
    DungeonBossInfo const* urn = FindEntry(out, BossRosterRegistry::ObjectiveEntry(1));
    ASSERT_NE(urn, nullptr);
    EXPECT_EQ(urn->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(urn->eventId, EV_NIGHTBANE);
    EXPECT_EQ(BossOrderKey(*urn), static_cast<uint32>(ORDER_NIGHTBANE_URN));
    // Completion is the boss-state slot (10, the DBC order since AC #28315).
    EXPECT_EQ(urn->doneBossStateIndex, STATE_NIGHTBANE);

    // Step 0 clicks the urn, and the at-objective trigger only holds for step 0
    // while the tank is inside arriveRadius. So the anchor must put the tank in
    // the executor's 5yd use range (DC_EVENT_GO_USE_RANGE) on arrival, or the
    // click walk leaves the ring and Advance hauls it back.
    float const ux = urn->x - URN_X, uy = urn->y - URN_Y, uz = urn->z - URN_Z;
    EXPECT_LT(std::sqrt(ux * ux + uy * uy + uz * uz) + urn->arriveRadius, 5.0f);

    DungeonBossInfo const* nb = FindEntry(out, NPC_NIGHTBANE);
    ASSERT_NE(nb, nullptr);
    EXPECT_EQ(nb->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(nb->encounterIndex, BIT_NIGHTBANE);
    EXPECT_FLOAT_EQ(nb->z, LANDING_Z);  // the terrace, not the perch
    EXPECT_GT(BossOrderKey(*nb), BossOrderKey(*urn));
}

TEST(DcKarazhanTest, NightbaneEventShape)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, EV_NIGHTBANE);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Anchored);
    EXPECT_TRUE(ev->persistent);
    EXPECT_TRUE(ev->required);
    ASSERT_EQ(ev->steps.size(), 5u);

    // The urn is clicked first: nothing is summoned until it is. Then the intro
    // is started by hand if the urn script did not (mod-individual-progression's
    // copy looks him up by his pre-#28315 slot). The party tops off while he
    // flies the intro.
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(ev->steps[0].goEntry, GO_BLACKENED_URN);
    EXPECT_EQ(ev->steps[1].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[1].hookId, HOOK_KZ_NIGHTBANE_INTRO);
    EXPECT_TRUE(ObjectiveHookRegistry::Has(HOOK_KZ_NIGHTBANE_INTRO));
    EXPECT_GT(ev->steps[1].timeoutMs, 0u);
    EXPECT_EQ(ev->steps[2].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[2].hookId, DC_HOOK_RAID_MUSTER);

    // The intro wait clears on IN_PROGRESS or DONE and never rewinds (NOT_STARTED
    // is the state it is waiting OUT of).
    EventStep const& intro = ev->steps[3];
    EXPECT_EQ(intro.bossStateId, STATE_NIGHTBANE);
    EXPECT_TRUE(intro.bossStateClearMask & DcBossStateBit(1));
    EXPECT_TRUE(intro.bossStateClearMask & DcBossStateBit(3));
    EXPECT_EQ(intro.restartOnBossStateMask, 0u);

    // The fight hold clears on DONE and rewinds on an evade.
    EventStep const& fight = ev->steps[4];
    EXPECT_EQ(fight.bossStateId, STATE_NIGHTBANE);
    EXPECT_EQ(DecideBossStateGate(3, fight.bossStateClearMask, fight.restartOnBossStateMask),
              BossStateGateVerdict::Clear);
    EXPECT_EQ(DecideBossStateGate(0, fight.bossStateClearMask, fight.restartOnBossStateMask),
              BossStateGateVerdict::Restart);
    EXPECT_EQ(DecideBossStateGate(1, fight.bossStateClearMask, fight.restartOnBossStateMask),
              BossStateGateVerdict::Hold);
    EXPECT_GE(fight.timeoutMs, 600000u);

    // The muster point keeps the party inside his 45yd evade check.
    float const dx = NB_MUSTER_X - LANDING_X;
    float const dy = NB_MUSTER_Y - LANDING_Y;
    EXPECT_LT(std::sqrt(dx * dx + dy * dy) + fight.radius, 45.0f);
}

// --- Opera (K6) ---------------------------------------------------------------

TEST(DcKarazhanTest, OperaObjectiveReplacesBarnes)
{
    auto const out = Patched();
    DungeonBossInfo const* opera = FindEntry(out, BossRosterRegistry::ObjectiveEntry(2));
    ASSERT_NE(opera, nullptr);
    EXPECT_EQ(opera->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(opera->eventId, EV_OPERA);
    EXPECT_EQ(BossOrderKey(*opera), static_cast<uint32>(ORDER_OPERA));
    // Completion is the Opera slot (3, the DBC order since AC #28315).
    EXPECT_EQ(opera->doneBossStateIndex, STATE_OPERA);
    // Barnes is never a boss anchor: the tank would attack a friendly NPC.
    EXPECT_EQ(FindEntry(out, NPC_BARNES), nullptr);
    // The Opera sits between Maiden and the Nightbane urn.
    EXPECT_LT(BossOrderKey(*FindEntry(out, NPC_MAIDEN)), BossOrderKey(*opera));
    EXPECT_LT(BossOrderKey(*opera),
              BossOrderKey(*FindEntry(out, BossRosterRegistry::ObjectiveEntry(1))));
}

TEST(DcKarazhanTest, OperaEventShape)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, EV_OPERA);
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->activation, EventActivation::Anchored);
    EXPECT_EQ(ev->orderIndex, static_cast<uint32>(ORDER_OPERA));
    EXPECT_TRUE(ev->persistent);
    EXPECT_TRUE(ev->required);
    ASSERT_EQ(ev->steps.size(), 5u);

    EXPECT_EQ(ev->steps[0].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[0].hookId, DC_HOOK_RAID_MUSTER);

    EventStep const& barnes = ev->steps[1];
    EXPECT_EQ(barnes.kind, EventStepKind::Gossip);
    EXPECT_EQ(barnes.creatureEntry, NPC_BARNES);
    EXPECT_EQ(barnes.gossipOption, 0);
    EXPECT_TRUE(barnes.waitForStill);
}

TEST(DcKarazhanTest, OperaCastWaitResolvesOnEveryPlay)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, EV_OPERA);
    ASSERT_NE(ev, nullptr);
    EventStep const& cast = ev->steps[2];
    EXPECT_EQ(cast.kind, EventStepKind::MoveTo);
    EXPECT_TRUE(cast.wantAlive);
    std::vector<uint32> const gate = EventStepGateEntries(cast);
    for (uint32 first : { NPC_DOROTHEE, NPC_GRANDMOTHER, NPC_JULIANNE })
        EXPECT_NE(std::find(gate.begin(), gate.end(), first), gate.end()) << first;
    EXPECT_EQ(gate.size(), 3u);
    // Long enough for Barnes' walk and speech.
    EXPECT_GE(cast.timeoutMs, 90000u);
}

TEST(DcKarazhanTest, OperaGrandmotherStepIsSkippedWhenAbsent)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, EV_OPERA);
    ASSERT_NE(ev, nullptr);
    EventStep const& granny = ev->steps[3];
    EXPECT_EQ(granny.kind, EventStepKind::Gossip);
    EXPECT_EQ(granny.creatureEntry, NPC_GRANDMOTHER);
    // Oz and R&J have no Grandmother: the step must not wait for one.
    EXPECT_TRUE(granny.skipIfMissing);
    // It comes after the cast wait, never before the curtain summons her.
    EXPECT_EQ(ev->steps[2].kind, EventStepKind::MoveTo);
}

TEST(DcKarazhanTest, OperaHoldsUntilDoneAndRetriesAFail)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, EV_OPERA);
    ASSERT_NE(ev, nullptr);
    EventStep const& hold = ev->steps[4];
    EXPECT_EQ(hold.bossStateId, STATE_OPERA);
    uint32 const clear = hold.bossStateClearMask;
    uint32 const restart = hold.restartOnBossStateMask;
    EXPECT_EQ(DecideBossStateGate(3, clear, restart), BossStateGateVerdict::Clear);    // DONE
    EXPECT_EQ(DecideBossStateGate(1, clear, restart), BossStateGateVerdict::Hold);     // playing
    EXPECT_EQ(DecideBossStateGate(2, clear, restart), BossStateGateVerdict::Restart);  // FAIL
    // The hold keeps the party on the stage (it cannot leave anyway: the stage
    // has no way down but the doors).
    EXPECT_FLOAT_EQ(hold.x, STAGE_X);
    EXPECT_FLOAT_EQ(hold.y, STAGE_Y);
}

// --- the Test Deck scenario (T2) ---------------------------------------------------

TEST(DcKarazhanTest, TheChessScenarioPlaysTheChessObjective)
{
    DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find("kara-chess");
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(DcTestDungeonRegistry::IsScenario(*row));
    EXPECT_STREQ(row->scenarioOf, "kara");
    EXPECT_EQ(row->mapId, MAP);
    ASSERT_EQ(row->focusEntries.size(), 1u);

    // The focus is the chess objective on the patched roster, and it is playable.
    auto const out = Patched();
    DungeonBossInfo const* chess = FindEntry(out, row->focusEntries.front());
    ASSERT_NE(chess, nullptr);
    EXPECT_EQ(chess->eventId, EV_CHESS);
    EXPECT_FALSE(chess->skipByDesign) << "a skip-by-design focus is refused at start";

    // It lands on the objective's own anchor, so the walk-in step is done on arrival.
    EXPECT_NEAR(row->x, chess->x, 1.0f);
    EXPECT_NEAR(row->y, chess->y, 1.0f);

    // Success is the win itself, with time for the chest.
    EXPECT_TRUE(row->success.IsSet());
    EXPECT_EQ(row->success.dataId, DATA_CHESS_EVENT);
    EXPECT_EQ(row->success.value, CHESS_EVENT_DONE);
    EXPECT_GE(row->successGraceS, 30u);
    EXPECT_GE(row->overallTimeoutS, 1800u);

    // `.dc test start 532` still means the whole of Karazhan.
    DcTestDungeonRegistry::Row const* whole = DcTestDungeonRegistry::Find("532");
    ASSERT_NE(whole, nullptr);
    EXPECT_STREQ(whole->token, "kara");
}

// ---- the void under the Guardian's Library (event 4, hook 65) ----------------------
//
// tr-20261022-194409-1044: the tank went through the library floor at the stair foot
// and ended on unlinked castle-shell navmesh under Illhoof. Points from that run.
TEST(DcKarazhanTest, LibraryPitBoxHoldsTheFallAndNotTheLibrary)
{
    using DcKaraLibraryPit::InPit;
    EXPECT_TRUE(InPit(-11244.8f, -1879.7f, 107.4f));   // the landing under the stair foot
    EXPECT_TRUE(InPit(-11203.2f, -1729.7f, 119.1f));   // where it wandered, under Illhoof
    EXPECT_TRUE(InPit(-11205.0f, -1735.0f, 121.9f));   // the member teleported down to it

    EXPECT_FALSE(InPit(-11244.2f, -1879.0f, 136.0f));  // the library floor it fell from
    EXPECT_FALSE(InPit(-11210.0f, -1885.4f, 152.2f));  // the stair-top camp
    EXPECT_FALSE(InPit(-11240.6f, -1704.3f, 179.3f));  // Illhoof
    EXPECT_FALSE(InPit(-11170.0f, -1910.0f, 91.8f));   // the z~92 interior corridor east of it
    EXPECT_FALSE(InPit(-11137.0f, -1917.3f, 49.8f));   // the stables

    // The landing is out of the box, or the rescue would re-fire on arrival.
    EXPECT_FALSE(InPit(DcKaraLibraryPit::LANDING_X, DcKaraLibraryPit::LANDING_Y, DcKaraLibraryPit::LANDING_Z));
}

TEST(DcKarazhanTest, LibraryPitRescueEventShape)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(MAP, DcKaraLibraryPit::EVENT_PIT_RESCUE);
    ASSERT_NE(ev, nullptr);
    ASSERT_EQ(ev->steps.size(), 1u);
    EXPECT_EQ(ev->steps[0].hookId, DcKaraLibraryPit::HOOK_PIT_RESCUE);
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::Custom);
    EXPECT_TRUE(ObjectiveHookRegistry::Has(DcKaraLibraryPit::HOOK_PIT_RESCUE));
}
