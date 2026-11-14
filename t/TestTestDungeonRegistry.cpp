/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <set>
#include <string>
#include <vector>

#include "TestRun/DcTestDungeonRegistry.h"

using DcTestDungeonRegistry::All;
using DcTestDungeonRegistry::Find;
using DcTestDungeonRegistry::RaidSizeMax;
using DcTestDungeonRegistry::RaidSizePresets;
using DcTestDungeonRegistry::Row;
using DcTestDungeonRegistry::SizeFits;

TEST(DcTestDungeonRegistryTest, TokensAreUnique)
{
    std::set<std::string> seen;
    for (Row const& row : All())
        EXPECT_TRUE(seen.insert(row.token).second) << "duplicate token: " << row.token;
}

TEST(DcTestDungeonRegistryTest, EveryRowIsPlausible)
{
    for (Row const& row : All())
    {
        EXPECT_NE(row.mapId, 0u) << row.token;
        EXPECT_GT(row.recommendedLevel, 0u) << row.token;
        EXPECT_LE(row.recommendedLevel, 80u) << row.token;
        EXPECT_STRNE(row.name, "") << row.token;
        // Entrance must be a real point, not a zero-initialized placeholder.
        EXPECT_TRUE(row.x != 0.f || row.y != 0.f) << row.token;
    }
}

TEST(DcTestDungeonRegistryTest, HeroicLevelMatchesTheExpansionCap)
{
    // heroicLevel is both the heroic default level AND the "heroic offered"
    // gate, and a heroic run is run at its expansion's cap: TBC rows carry 70,
    // WotLK rows 80. Classic dungeons have no heroic difficulty, so 0.
    std::set<std::uint32_t> const tbcMaps =
        {543, 542, 547, 546, 557, 558, 556, 560, 555, 545, 540, 269, 553, 554, 552, 585};
    std::set<std::uint32_t> const wotlkMaps =
        {574, 575, 576, 578, 595, 599, 600, 601, 602, 604, 608, 619, 632, 650, 658, 668};
    for (Row const& row : All())
    {
        if (tbcMaps.count(row.mapId))
            EXPECT_EQ(row.heroicLevel, 70u) << row.token;
        else if (wotlkMaps.count(row.mapId))
            EXPECT_EQ(row.heroicLevel, 80u) << row.token;
        else
            EXPECT_EQ(row.heroicLevel, 0u) << row.token;
    }
}

TEST(DcTestDungeonRegistryTest, FindByToken)
{
    Row const* row = Find("deadmines");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->mapId, 36u);
    EXPECT_STREQ(row->wing, "");
}

TEST(DcTestDungeonRegistryTest, FindByNumericMapId)
{
    Row const* row = Find("36");
    ASSERT_NE(row, nullptr);
    EXPECT_STREQ(row->token, "deadmines");
}

TEST(DcTestDungeonRegistryTest, GruulsLairIsATbcRaidRow)
{
    // The first non-classic raid: runs at the TBC cap with no heroic mode
    // (a raid's size is its difficulty), reachable by token and by map id.
    Row const* row = Find("gruul");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->mapId, 565u);
    EXPECT_EQ(row->recommendedLevel, 70u);
    EXPECT_EQ(row->heroicLevel, 0u);
    EXPECT_STREQ(row->wing, "");
    EXPECT_EQ(Find("565"), row);
}

TEST(DcTestDungeonRegistryTest, KarazhanIsATbcRaidRow)
{
    // The first 10-man-only raid: TBC cap, no heroic mode, reachable by token
    // and by map id, entering at the main gate (areatrigger 4131).
    Row const* row = Find("kara");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->mapId, 532u);
    EXPECT_EQ(row->recommendedLevel, 70u);
    EXPECT_EQ(row->heroicLevel, 0u);
    EXPECT_STREQ(row->wing, "");
    EXPECT_NEAR(row->x, -11100.0f, 0.01f);
    EXPECT_EQ(Find("532"), row);
}

TEST(DcTestDungeonRegistryTest, SizeFitsHonoursTheMapCap)
{
    // 0 = the stores do not know the map: no cap.
    EXPECT_TRUE(SizeFits(40, 0));
    EXPECT_TRUE(SizeFits(10, 10));
    EXPECT_FALSE(SizeFits(11, 10));  // Karazhan refuses the 11th
    EXPECT_TRUE(SizeFits(25, 25));
    EXPECT_FALSE(SizeFits(26, 25));
}

TEST(DcTestDungeonRegistryTest, RaidSizeBoundsFollowTheMapCap)
{
    // Karazhan (10): the form offers no 25 preset and tops out at 10.
    EXPECT_EQ(RaidSizePresets(10), (std::vector<std::uint32_t>{10}));
    EXPECT_EQ(RaidSizeMax(10), 10u);
    // Gruul (25) and Molten Core (40) keep both presets.
    EXPECT_EQ(RaidSizePresets(25), (std::vector<std::uint32_t>{10, 25}));
    EXPECT_EQ(RaidSizeMax(25), 25u);
    EXPECT_EQ(RaidSizePresets(40), (std::vector<std::uint32_t>{10, 25}));
    EXPECT_EQ(RaidSizeMax(40), 40u);
    // Unknown map: the module's own 40 ceiling, both presets.
    EXPECT_EQ(RaidSizePresets(0), (std::vector<std::uint32_t>{10, 25}));
    EXPECT_EQ(RaidSizeMax(0), 40u);
}

TEST(DcTestDungeonRegistryTest, NumericLookupOnWingSplitMapIsRefused)
{
    // 429 = Dire Maul, three isolated wings; a bare mapId cannot pick one.
    EXPECT_EQ(Find("429"), nullptr);
    // 189 = Scarlet Monastery, four wings.
    EXPECT_EQ(Find("189"), nullptr);
    // 229 = Blackrock Spire, LBRS + UBRS.
    EXPECT_EQ(Find("229"), nullptr);
    // 230 = Blackrock Depths, Detention Block + Upper City.
    EXPECT_EQ(Find("230"), nullptr);
}

TEST(DcTestDungeonRegistryTest, BlackrockDepthsRowsAreItsWings)
{
    Row const* db = Find("brd-db");
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(db->mapId, 230u);
    EXPECT_STREQ(db->wing, "Detention Block");

    Row const* uc = Find("brd-uc");
    ASSERT_NE(uc, nullptr);
    EXPECT_EQ(uc->mapId, 230u);
    EXPECT_STREQ(uc->wing, "Upper City");
    // Both wings enter at the one portal, as the dungeon finder sends them.
    EXPECT_TRUE(uc->x == db->x && uc->y == db->y);
}

TEST(DcTestDungeonRegistryTest, RetiredBrdTokenAliasesToTheDetentionBlock)
{
    EXPECT_STREQ(DcTestDungeonRegistry::AliasTarget("brd"), "brd-db");
    Row const* row = Find("brd");
    ASSERT_NE(row, nullptr);
    EXPECT_STREQ(row->token, "brd-db");
}

TEST(DcTestDungeonRegistryTest, BlackrockSpireRowsAreItsWings)
{
    Row const* lbrs = Find("lbrs");
    ASSERT_NE(lbrs, nullptr);
    EXPECT_EQ(lbrs->mapId, 229u);
    EXPECT_STREQ(lbrs->wing, "LBRS");
    EXPECT_EQ(lbrs->recommendedLevel, 58u);

    Row const* ubrs = Find("ubrs");
    ASSERT_NE(ubrs, nullptr);
    EXPECT_EQ(ubrs->mapId, 229u);
    EXPECT_STREQ(ubrs->wing, "UBRS");
    EXPECT_EQ(ubrs->recommendedLevel, 60u);
    // UBRS drops in the shared hall at the Dragonspine Door, not at the portal.
    EXPECT_FALSE(ubrs->x == lbrs->x && ubrs->y == lbrs->y);
}

TEST(DcTestDungeonRegistryTest, RetiredBrsTokenAliasesToLbrs)
{
    EXPECT_STREQ(DcTestDungeonRegistry::AliasTarget("brs"), "lbrs");
    EXPECT_EQ(DcTestDungeonRegistry::AliasTarget("lbrs"), nullptr);
    Row const* row = Find("brs");
    ASSERT_NE(row, nullptr);
    EXPECT_STREQ(row->token, "lbrs");

    // An alias whose target is not in the table resolves to nothing.
    std::vector<Row> const noLbrs = {
        { "deadmines", "The Deadmines", 36, -16.40f, -383.07f, 61.78f, 1.860f, 24, "" },
    };
    EXPECT_EQ(Find("brs", noLbrs), nullptr);

    // Every alias points at a real row and never shadows a live token.
    for (DcTestDungeonRegistry::Alias const& a : DcTestDungeonRegistry::Aliases())
    {
        EXPECT_NE(Find(a.to), nullptr) << a.from << " -> " << a.to;
        for (Row const& r : All())
            EXPECT_STRNE(r.token, a.from) << "alias '" << a.from << "' shadows a live row";
    }
}

TEST(DcTestDungeonRegistryTest, WingSplitMapsHaveWingTokens)
{
    // Dire Maul: three wing rows, each labelled.
    int dmRows = 0;
    for (Row const& row : All())
        if (row.mapId == 429)
        {
            ++dmRows;
            EXPECT_STRNE(row.wing, "") << row.token;
        }
    EXPECT_EQ(dmRows, 3);

    // Scarlet Monastery: four wing rows.
    int smRows = 0;
    for (Row const& row : All())
        if (row.mapId == 189)
        {
            ++smRows;
            EXPECT_STRNE(row.wing, "") << row.token;
        }
    EXPECT_EQ(smRows, 4);

    // Blackrock Spire: two wing rows.
    int brsRows = 0;
    for (Row const& row : All())
        if (row.mapId == 229)
        {
            ++brsRows;
            EXPECT_STRNE(row.wing, "") << row.token;
        }
    EXPECT_EQ(brsRows, 2);

    // Maraudon: the unlabelled whole-dungeon row plus its three dungeon-finder
    // wings — so, like the other split maps, the bare mapId is ambiguous.
    ASSERT_NE(Find("maraudon"), nullptr);
    EXPECT_STREQ(Find("maraudon")->wing, "");
    EXPECT_EQ(Find("349"), nullptr);
    int maraRows = 0;
    for (Row const& row : All())
        if (row.mapId == 349 && *row.wing)
            ++maraRows;
    EXPECT_EQ(maraRows, 3);
}

TEST(DcTestDungeonRegistryTest, UnknownLookupsReturnNull)
{
    EXPECT_EQ(Find(""), nullptr);
    EXPECT_EQ(Find("naxxramas"), nullptr);
    EXPECT_EQ(Find("0"), nullptr);
    EXPECT_EQ(Find("99999"), nullptr);
    EXPECT_EQ(Find("36x"), nullptr);
}

// ---- scenarios (Karazhan chess plan, T1) -----------------------------------------

namespace
{
    using DcTestDungeonRegistry::InstanceDataEquals;
    using DcTestDungeonRegistry::IsScenario;
    using DcTestDungeonRegistry::PredicateHolds;
    using DcTestDungeonRegistry::ScenarioSidecarFields;
    using DcTestDungeonRegistry::SuccessPredicate;
    using DcTestDungeonRegistry::ValidateScenario;

    // A test-only catalogue: one parent on map 532, a well-formed scenario of
    // it (the shape T2's kara-chess row will have), and a second map's row so
    // a cross-map parent can be refused. The scenario is spelled positionally
    // exactly as a real row in All() would be.
    std::vector<Row> ScenarioFixture()
    {
        return {
            { "kara", "Karazhan", 532, -11100.00f, -2003.98f, 49.89f, 0.577f, 70, "" },
            { "gruul", "Gruul's Lair", 565, 62.78f, 35.46f, -3.98f, 1.418f, 70, "" },
            { "kara-chess", "Karazhan: Chess", 532, -11109.8f, -1852.8f, 221.1f, 0.f, 70, "", 0,
              "kara", {22520}, InstanceDataEquals(8, 3), 60, 1800, 300 },
        };
    }

    Row ChessRow() { return ScenarioFixture()[2]; }
}

TEST(DcTestDungeonRegistryTest, EveryCatalogueScenarioValidates)
{
    // No live roster here (BossSpawnIndex needs the world DB), so this pins
    // the row shape; the harness re-validates against the live roster at start.
    for (Row const& row : All())
        EXPECT_EQ(ValidateScenario(row, All()), "") << row.token;
}

TEST(DcTestDungeonRegistryTest, WellFormedScenarioValidates)
{
    std::vector<Row> const rows = ScenarioFixture();
    std::vector<std::uint32_t> const roster = {15550, 15687, 16457, 22520, 15690};
    EXPECT_TRUE(IsScenario(rows[2]));
    EXPECT_FALSE(IsScenario(rows[0]));
    EXPECT_EQ(ValidateScenario(rows[2], rows), "");
    EXPECT_EQ(ValidateScenario(rows[2], rows, &roster), "");
}

TEST(DcTestDungeonRegistryTest, ScenarioFocusMustBeOnTheParentRoster)
{
    std::vector<Row> const rows = ScenarioFixture();
    std::vector<std::uint32_t> const roster = {15550, 15687};
    EXPECT_NE(ValidateScenario(rows[2], rows, &roster).find("22520"), std::string::npos);
}

TEST(DcTestDungeonRegistryTest, MalformedScenariosAreRefused)
{
    std::vector<Row> rows = ScenarioFixture();

    Row r = ChessRow();
    r.scenarioOf = "naxx";
    EXPECT_NE(ValidateScenario(r, rows), "");  // unknown parent

    r = ChessRow();
    r.scenarioOf = "gruul";
    EXPECT_NE(ValidateScenario(r, rows), "");  // parent on another map

    r = ChessRow();
    r.scenarioOf = "kara-chess";
    EXPECT_NE(ValidateScenario(r, rows), "");  // its own parent

    r = ChessRow();
    r.focusEntries.clear();
    EXPECT_NE(ValidateScenario(r, rows), "");  // nothing to focus on

    r = ChessRow();
    r.focusEntries = {22520, 22520};
    EXPECT_NE(ValidateScenario(r, rows), "");  // duplicate focus

    r = ChessRow();
    r.focusEntries = {0};
    EXPECT_NE(ValidateScenario(r, rows), "");

    r = ChessRow();
    r.success = InstanceDataEquals(0, 3);
    EXPECT_NE(ValidateScenario(r, rows), "");  // predicate names no data id

    r = ChessRow();
    r.success = SuccessPredicate{};
    EXPECT_NE(ValidateScenario(r, rows), "");  // grace without a predicate
    r.successGraceS = 0;
    EXPECT_EQ(ValidateScenario(r, rows), "");  // focus-only scenario is fine

    r = ChessRow();
    r.heroicLevel = 70;
    EXPECT_NE(ValidateScenario(r, rows), "");  // scenarios have no heroic

    // A scenario of a scenario.
    rows.push_back(ChessRow());
    rows.back().token = "kara-chess-2";
    rows.back().scenarioOf = "kara-chess";
    EXPECT_NE(ValidateScenario(rows.back(), rows), "");

    // Scenario knobs on a full-dungeon row.
    Row plain = rows[0];
    plain.focusEntries = {22520};
    EXPECT_NE(ValidateScenario(plain, rows), "");
}

TEST(DcTestDungeonRegistryTest, NumericLookupIgnoresScenarios)
{
    std::vector<Row> const rows = ScenarioFixture();
    // 532 has the parent AND a scenario; the number still means the parent.
    Row const* hit = Find("532", rows);
    ASSERT_NE(hit, nullptr);
    EXPECT_STREQ(hit->token, "kara");
    // The scenario is reachable by its token.
    Row const* chess = Find("kara-chess", rows);
    ASSERT_NE(chess, nullptr);
    EXPECT_TRUE(IsScenario(*chess));

    // A map whose only row is a scenario has no numeric match at all.
    std::vector<Row> const orphan = {rows[2]};
    EXPECT_EQ(Find("532", orphan), nullptr);
    EXPECT_NE(Find("kara-chess", orphan), nullptr);
}

TEST(DcTestDungeonRegistryTest, InstanceDataPredicate)
{
    SuccessPredicate const p = InstanceDataEquals(8, 3);
    EXPECT_TRUE(p.IsSet());
    EXPECT_TRUE(PredicateHolds(p, 3));
    EXPECT_FALSE(PredicateHolds(p, 1));
    EXPECT_EQ(DcTestDungeonRegistry::DescribePredicate(p), "instanceData(8)==3");

    // An unset predicate never holds — it means "wait for all-cleared".
    SuccessPredicate const none;
    EXPECT_FALSE(none.IsSet());
    EXPECT_FALSE(PredicateHolds(none, 0));
    EXPECT_EQ(DcTestDungeonRegistry::DescribePredicate(none), "");
}

TEST(DcTestDungeonRegistryTest, ScenarioSidecarShape)
{
    // The fragment the Test Deck's catalogue parses (GET /api/testdungeons).
    EXPECT_EQ(ScenarioSidecarFields(ChessRow()),
              ",\"scenario\":true,\"scenarioOf\":\"kara\",\"focus\":[22520],"
              "\"success\":\"instanceData(8)==3\",\"successGraceS\":60,"
              "\"overallTimeoutS\":1800,\"noProgressS\":300");
    // A plain row adds nothing, so its sidecar object is byte-identical to
    // what it was before scenarios existed.
    EXPECT_EQ(ScenarioSidecarFields(ScenarioFixture()[0]), "");
}
