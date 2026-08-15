/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BossSpawnIndex.h"

#include <algorithm>

#include "CreatureData.h"
#include "CreatureSpawnEntry.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "ObjectMgr.h"

std::unordered_map<uint64, std::vector<DungeonBossInfo>> BossSpawnIndex::_store;
bool BossSpawnIndex::_built = false;

std::vector<DungeonBossInfo> const& BossSpawnIndex::Get(uint32 mapId, Difficulty difficulty)
{
    EnsureBuilt();
    static std::vector<DungeonBossInfo> const empty;
    auto it = _store.find(MakeKey(mapId, difficulty));
    if (it == _store.end())
        return empty;
    return it->second;
}

void BossSpawnIndex::EnsureBuilt()
{
    if (_built)
        return;
    Build();
    _built = true;
}

void BossSpawnIndex::Build()
{
    // 1. Build creditEntry -> encounter list, keyed by (mapId, difficulty).
    //    Only kill-creature encounters are usable here.
    struct EncounterRow
    {
        uint32 mapId;
        Difficulty difficulty;
        uint32 encounterIndex;
        std::string name;
        uint32 creditEntry;
    };

    std::unordered_multimap<uint32, EncounterRow> byCreditEntry;

    for (uint32 i = 0; i < sDungeonEncounterStore.GetNumRows(); ++i)
    {
        DungeonEncounterEntry const* dbc = sDungeonEncounterStore.LookupEntry(i);
        if (!dbc)
            continue;

        DungeonEncounterList const* list =
            sObjectMgr->GetDungeonEncounterList(dbc->mapId, Difficulty(dbc->difficulty));
        if (!list)
            continue;

        for (DungeonEncounter const* enc : *list)
        {
            if (!enc || enc->dbcEntry != dbc)
                continue;
            if (enc->creditType != ENCOUNTER_CREDIT_KILL_CREATURE)
                continue;

            EncounterRow row;
            row.mapId = dbc->mapId;
            row.difficulty = Difficulty(dbc->difficulty);
            row.encounterIndex = dbc->encounterIndex;
            row.name = dbc->encounterName[0] ? dbc->encounterName[0] : "";
            row.creditEntry = enc->creditEntry;
            byCreditEntry.emplace(row.creditEntry, std::move(row));
        }
    }

    if (byCreditEntry.empty())
        return;

    // 2. Walk every creature spawn once. For each spawn whose entry matches a
    //    boss credit entry on its mapId, record it.
    CreatureDataContainer const& spawns = sObjectMgr->GetAllCreatureData();
    for (auto const& kv : spawns)
    {
        CreatureData const& data = kv.second;
        uint32 const entry = DungeonClear::SpawnEntry(data);
        if (!entry)
            continue;

        auto range = byCreditEntry.equal_range(entry);
        for (auto it = range.first; it != range.second; ++it)
        {
            EncounterRow const& row = it->second;
            if (row.mapId != data.mapid)
                continue;

            // A spawn belongs to a difficulty bucket only when its spawnMask
            // bit says it exists there. Most 5-man spawns are mask 3 (both);
            // a heroic-only placement (Shattered Halls' Porung, mask 2) must
            // not leak coords into the normal bucket, nor a normal-only spawn
            // into heroic. Mask 0 is treated as "no filter" (defensive — some
            // custom rows leave it unset).
            if (data.spawnMask && !(data.spawnMask & (1u << row.difficulty)))
                continue;

            DungeonBossInfo info;
            info.entry = entry;
            info.encounterIndex = row.encounterIndex;
            info.name = row.name;
            info.mapId = row.mapId;
            info.x = data.posX;
            info.y = data.posY;
            info.z = data.posZ;
            _store[MakeKey(row.mapId, row.difficulty)].push_back(info);
        }
    }

    // 3. Sort each bucket by encounter index; deduplicate by entry (keep the
    //    first spawn — there is usually only one per dungeon).
    for (auto& kv : _store)
    {
        auto& v = kv.second;
        std::sort(v.begin(), v.end(),
                  [](DungeonBossInfo const& a, DungeonBossInfo const& b)
                  { return a.encounterIndex < b.encounterIndex; });

        std::vector<DungeonBossInfo> deduped;
        deduped.reserve(v.size());
        for (auto const& info : v)
        {
            bool seen = false;
            for (auto const& kept : deduped)
            {
                if (kept.entry == info.entry)
                {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                deduped.push_back(info);
        }
        v = std::move(deduped);
    }
}
