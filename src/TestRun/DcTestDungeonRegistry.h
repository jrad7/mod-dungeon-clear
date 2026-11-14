/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTDUNGEONREGISTRY_H
#define _PLAYERBOT_DCTESTDUNGEONREGISTRY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Hand-authored catalogue of the dungeons the `.dc test` harness can run:
// every map with a curated clear definition (roster patch / event file), one
// row per *enterable unit*. Split-wing maps whose wings are physically
// isolated (Dire Maul, Scarlet Monastery, Blackrock Spire, Blackrock Depths) get
// one row per wing, so each wing has its own token and entrance. On DM/SM the wing
// a run covers is decided by where the party stands; on Blackrock Spire,
// Blackrock Depths and Maraudon the row's token IS the run wing
// (DungeonWing::token, latched by the harness). Maraudon also keeps its
// whole-dungeon row, `maraudon`, whose wing lists every boss.
//
// Entrance coordinates are the world-DB areatrigger_teleport targets (the
// point just inside the instance portal) — safely on the navmesh, where a
// walked-in party would stand. Static vanilla/TBC data, so it lives in code
// like the module's other registries (wings, room-aggro, routes).

namespace DcTestDungeonRegistry
{
    // A SCENARIO row's own success condition (Karazhan chess plan, T1). Plain
    // data, so this catalogue — and src/TestRun as a whole — stays free of
    // content code: the harness evaluates it against the instance each monitor
    // tick, and nothing here knows what DATA id 9 means on map 532.
    //
    //   None                -> no predicate: DC's own all-cleared is the only
    //                          success (every full-dungeon row, and a scenario
    //                          whose focus boss is a plain kill).
    //   InstanceDataEquals  -> InstanceScript::GetData(dataId) == value, e.g.
    //                          Karazhan's DATA_CHESS_EVENT (8) reading DONE (3).
    struct SuccessPredicate
    {
        enum class Kind : std::uint8_t
        {
            None,
            InstanceDataEquals
        };

        Kind kind = Kind::None;
        std::uint32_t dataId = 0;
        std::uint32_t value = 0;

        bool IsSet() const { return kind != Kind::None; }
    };

    // Row-literal spelling: `InstanceDataEquals(9, 3)`.
    constexpr SuccessPredicate InstanceDataEquals(std::uint32_t dataId, std::uint32_t value)
    {
        return SuccessPredicate{SuccessPredicate::Kind::InstanceDataEquals, dataId, value};
    }

    // Whether the predicate holds for the instance-data value the harness read
    // off the live instance (GetData(p.dataId)). Pure so the kernel's half of
    // the contract is pinned in gtest; an unset predicate never holds — it
    // means "wait for all-cleared", not "already done".
    inline bool PredicateHolds(SuccessPredicate const& p, std::uint32_t observed)
    {
        switch (p.kind)
        {
            case SuccessPredicate::Kind::InstanceDataEquals:
                return observed == p.value;
            case SuccessPredicate::Kind::None:
                break;
        }
        return false;
    }

    // Stable token for logs / the record ("instanceData(8)==3"); "" when unset.
    std::string DescribePredicate(SuccessPredicate const& p);

    struct Row
    {
        char const* token;        // command token: ".dc test start <token>"
        char const* name;         // human-readable dungeon (wing) name
        std::uint32_t mapId;
        float x, y, z, o;         // entrance teleport target
        std::uint32_t recommendedLevel;  // default bot level for the run
        char const* wing;         // wing label for split maps, "" otherwise
        // Default bot level for a HEROIC run, and the "heroic offered" flag in
        // one: 0 = no heroic mode for this row. TBC rows carry 70, WotLK rows
        // 80. Classic dungeons have no heroic mode at all, so they stay 0 and
        // this stays a trailing member — those rows need no edit
        // (value-initialized).
        std::uint32_t heroicLevel = 0;

        // --- SCENARIO fields (Karazhan chess plan, T1) ---------------------
        // Every one is optional and its default is today's behaviour, so the
        // full-dungeon rows above spell none of them. A scenario is a row that
        // drops the raid at an IN-MAP point (x,y,z,o above — already just a
        // teleport target), scopes the run to one objective, and passes on its
        // own predicate instead of (or ahead of) DC's all-cleared.
        //
        // Parent row's token ("kara"). Non-null marks the row as a scenario:
        // it must name an existing non-scenario row on the SAME map, it is
        // never returned by the numeric map lookup (".dc test start 532" still
        // means the whole of Karazhan), and it inherits no heroic mode.
        char const* scenarioOf = nullptr;

        // Roster entries the run is scoped to. At start the harness fills the
        // tank's DcKey::Skipped with every OTHER roster entry — the same set a
        // hand-typed `dc skip` builds — and counts only these in bossesTotal /
        // bossRoster. Each must be on the parent map's live roster, or the run
        // refuses at Starting. Required (non-empty) on a scenario row.
        std::vector<std::uint32_t> focusEntries{};

        // Optional success predicate, checked every monitor tick. Once it
        // holds the run gets `successGraceS` for the event to finish its tail
        // (the chest loot) — all-cleared inside the grace wins as usual; the
        // grace running out is a success too, recorded as successBy "grace"
        // with tailPending.
        SuccessPredicate success{};
        std::uint32_t successGraceS = 0;

        // Per-row overrides of DungeonClear.TestRun.OverallTimeoutS /
        // NoProgressS; 0 = the global value. A long scripted event (chess:
        // ~1800 / 300) wants its own watchdog budget without loosening every
        // other run's.
        std::uint32_t overallTimeoutS = 0;
        std::uint32_t noProgressS = 0;
    };

    inline bool IsScenario(Row const& row) { return row.scenarioOf != nullptr; }

    // Row for a command argument: exact token match, or a numeric mapId when
    // that map has exactly one row (wing-split maps must be named by token —
    // a bare "429" cannot say which Dire Maul wing). nullptr when unknown.
    // Scenario rows are reachable by token only; the numeric fallback ignores
    // them, so a map with one dungeon row plus N scenarios stays unambiguous.
    Row const* Find(std::string const& tokenOrMapId);

    // Same lookup over an explicit table (gtest fixtures; Find() passes All()).
    // A retired token that has an alias (Aliases()) resolves to its target row.
    Row const* Find(std::string const& tokenOrMapId, std::vector<Row> const& rows);

    // Retired tokens that still resolve, so old commands, saved soak pools, test
    // plans and queued streamcast requests keep working: `brs` (Blackrock Spire
    // before the LBRS/UBRS split) -> `lbrs`, `brd` (Blackrock Depths before the
    // Detention Block/Upper City split) -> `brd-db`. Published in the sidecar as
    // "aliases" so the Test Deck and streamcast can normalise stored tokens.
    struct Alias
    {
        char const* from;
        char const* to;
    };
    std::vector<Alias> const& Aliases();

    // The alias target for `token`, or nullptr when it is not a retired token.
    char const* AliasTarget(std::string const& token);

    std::vector<Row> const& All();

    // Why a scenario row is malformed, or "" when it is fine (and for every
    // non-scenario row). Checks what is knowable without a live world: the
    // parent exists, is not itself a scenario and is on the same map; the focus
    // is non-empty with no zero / duplicate entries; a set predicate names a
    // data id; the grace only makes sense with a predicate. `rosterEntries`,
    // when given, is the parent map's roster, and every focus entry must be on
    // it — the harness passes the live roster at Starting, gtests a fixture.
    std::string ValidateScenario(Row const& row, std::vector<Row> const& rows,
                                 std::vector<std::uint32_t> const* rosterEntries = nullptr);

    // The scenario fragment of a row's sidecar object — `,"scenario":true,
    // "scenarioOf":"kara","focus":[22520],...` — or "" for a plain row. Split
    // out of WriteSidecar so the shape the Test Deck parses is gtest-pinned.
    std::string ScenarioSidecarFields(Row const& row);

    // Map.dbc's own expansion id for a row's map: 0 classic, 1 TBC, 2 WotLK.
    // Read from the DBC rather than carried as a hand-typed column, so the
    // catalogue cannot drift from the client build the server actually loaded
    // — and a row added to the wrong section of the table still reports the
    // truth. 0 for a map the store does not know.
    inline constexpr std::uint32_t kExpansionWrath = 2;
    std::uint32_t ExpansionOf(Row const& row);

    // The player cap of the instance a run actually enters (difficulty 0 —
    // DcTestRunJob always sets RAID_DIFFICULTY_10MAN_NORMAL / normal dungeon
    // difficulty): MapDifficulty.dbc's maxPlayers, falling back to Map.dbc's,
    // exactly as InstanceMap::GetMaxPlayers resolves it. The core refuses
    // entry past it (CANNOT_ENTER_MAX_PLAYERS), so a bigger run strands the
    // overflow at the door. Karazhan is 10, Gruul 25, Molten Core 40. 0 when
    // the stores do not know the map (unit tests): no cap.
    std::uint32_t MaxPlayers(Row const& row);

    // Whether a run of `size` members fits a map capped at `cap` (0 = no cap).
    inline bool SizeFits(std::size_t size, std::uint32_t cap)
    {
        return cap == 0 || size <= cap;
    }

    // Size a raid row launches at when nobody asked for one (the launch form's
    // defaultSize, and every raid entry of a `pool=` plan) — 10 for iteration
    // speed; RaidSizeMax still caps it by the map.
    inline constexpr std::uint32_t kRaidDefaultSize = 10;

    // The launch form's raid-size presets (10/25) that fit `cap`, and the
    // form's upper bound: min(kMaxPartySize, cap) — kept pure so the sidecar
    // shape is testable without DBC stores.
    std::vector<std::uint32_t> RaidSizePresets(std::uint32_t cap);
    std::uint32_t RaidSizeMax(std::uint32_t cap);

    // Dump the catalogue (plus the test-run caps the dashboard's start form
    // needs) to dc_test_dungeons.json in the worldserver cwd — the same
    // sidecar pattern as the live/record files (env override
    // DC_TEST_DUNGEONS_FILE). Written once at the first world tick; the
    // dashboard serves it via /api/testdungeons.
    void WriteSidecar();
}

#endif  // _PLAYERBOT_DCTESTDUNGEONREGISTRY_H
