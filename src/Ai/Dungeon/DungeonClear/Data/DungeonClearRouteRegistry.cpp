/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearRouteRegistry.h"

std::unordered_map<DungeonClearRouteRegistry::Key, std::vector<WaypointHint>, DungeonClearRouteRegistry::KeyHash>&
DungeonClearRouteRegistry::Store()
{
    static std::unordered_map<Key, std::vector<WaypointHint>, KeyHash> instance;
    return instance;
}

void DungeonClearRouteRegistry::Register(uint32 mapId, Difficulty difficulty, uint32 bossEntry,
                                         std::vector<WaypointHint> hints)
{
    Store()[Key{mapId, difficulty, bossEntry}] = std::move(hints);
}

std::vector<WaypointHint> const* DungeonClearRouteRegistry::Get(uint32 mapId, Difficulty difficulty, uint32 bossEntry)
{
    auto const& s = Store();
    auto it = s.find(Key{mapId, difficulty, bossEntry});
    // Heroic shares the normal dungeon's geometry, and the hand-authored routes
    // are registered under normal — fall back so a heroic run still gets its
    // waypoint hints. A difficulty-specific row, when one exists, wins.
    if (it == s.end() && difficulty != DUNGEON_DIFFICULTY_NORMAL)
        it = s.find(Key{mapId, DUNGEON_DIFFICULTY_NORMAL, bossEntry});
    if (it == s.end())
        return nullptr;
    return &it->second;
}
