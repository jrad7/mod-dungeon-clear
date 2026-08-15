/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTDUNGEONREGISTRY_H
#define _PLAYERBOT_DCTESTDUNGEONREGISTRY_H

#include <cstdint>
#include <string>
#include <vector>

// Hand-authored catalogue of the dungeons the `.dc test` harness can run:
// every map with a curated clear definition (roster patch / event file), one
// row per *enterable unit*. Split-wing maps whose wings are physically
// isolated (Dire Maul, Scarlet Monastery) get one row per wing — the wing a
// run covers is decided by where the party stands, so each wing needs its own
// token and entrance. Maraudon's wings interconnect, so it stays one row.
//
// Entrance coordinates are the world-DB areatrigger_teleport targets (the
// point just inside the instance portal) — safely on the navmesh, where a
// walked-in party would stand. Static vanilla/TBC data, so it lives in code
// like the module's other registries (wings, room-aggro, routes).

namespace DcTestDungeonRegistry
{
    struct Row
    {
        char const* token;        // command token: ".dc test start <token>"
        char const* name;         // human-readable dungeon (wing) name
        std::uint32_t mapId;
        float x, y, z, o;         // entrance teleport target
        std::uint32_t recommendedLevel;  // default bot level for the run
        char const* wing;         // wing label for split maps, "" otherwise
        // Default bot level for a HEROIC run, and the "heroic offered" flag in
        // one: 0 = no heroic mode for this row. TBC rows carry 70. WotLK rows
        // deliberately stay 0 until their heroic content pass lands (the
        // TBC-only scope gate — see the heroic-dungeons plan doc); flipping
        // them to 80 is the whole enablement. Trailing member so the classic /
        // WotLK aggregate rows need no edit (value-initialized to 0).
        std::uint32_t heroicLevel = 0;
    };

    // Row for a command argument: exact token match, or a numeric mapId when
    // that map has exactly one row (wing-split maps must be named by token —
    // a bare "429" cannot say which Dire Maul wing). nullptr when unknown.
    Row const* Find(std::string const& tokenOrMapId);

    std::vector<Row> const& All();

    // Dump the catalogue (plus the test-run caps the dashboard's start form
    // needs) to dc_test_dungeons.json in the worldserver cwd — the same
    // sidecar pattern as the live/record files (env override
    // DC_TEST_DUNGEONS_FILE). Written once at the first world tick; the
    // dashboard serves it via /api/testdungeons.
    void WriteSidecar();
}

#endif  // _PLAYERBOT_DCTESTDUNGEONREGISTRY_H
